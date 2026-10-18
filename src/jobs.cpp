#include "core.hpp"
#include <cmath>
namespace fls {
struct DB {
  QString name;
  QSqlDatabase d;
  DB(const QString &p) {
    QDir().mkpath(QFileInfo(p).absolutePath());
    name = QUuid::createUuid().toString();
    d = QSqlDatabase::addDatabase("QSQLITE", name);
    d.setDatabaseName(p);
    require(d.open(), "Не вдалося відкрити базу: " + d.lastError().text());
    exec("PRAGMA busy_timeout=30000");
    exec("PRAGMA journal_mode=WAL");
    exec("CREATE TABLE IF NOT EXISTS records(project TEXT,id INTEGER,source "
         "TEXT,target TEXT,status TEXT,error TEXT,PRIMARY KEY(project,id))");
    exec("CREATE TABLE IF NOT EXISTS memory(key TEXT PRIMARY KEY,target TEXT)");
  }
  void exec(const QString &s) {
    QSqlQuery q(d);
    require(q.exec(s), q.lastError().text());
  }
  ~DB() {
    d.close();
    d = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
};
QString projectKey(const Settings &s) {
  return sha(
      (fileSha(s.source) + "|" + s.sourceLang + "|" + s.targetLang).toUtf8());
}
static void initialize(DB &db, const Babel &b, const QString &key) {
  {
    QSqlQuery n(db.d);
    n.prepare("SELECT count(*) FROM records WHERE project=?");
    n.addBindValue(key);
    require(n.exec() && n.next(), "Не вдалося прочитати стан бази");
    if (n.value(0).toInt() == b.entries.size())
      return;
  }
  require(db.d.transaction(), "Не вдалося почати транзакцію");
  QSqlQuery q(db.d);
  q.prepare("INSERT OR IGNORE INTO records VALUES(?,?,?,? ,?,?)");
  for (auto &e : b.entries) {
    q.addBindValue(key);
    q.addBindValue(qulonglong(e.id));
    q.addBindValue(e.source);
    q.addBindValue(technical(e.source) ? e.source : QString());
    q.addBindValue(technical(e.source) ? "preserved" : "pending");
    q.addBindValue("");
    require(q.exec(), q.lastError().text());
  }
  require(db.d.commit(), "Помилка фіксації бази");
}
QVector<Row> rows(const Settings &s) {
  auto b = Babel::load(s.source);
  auto key = projectKey(s);
  DB db(s.project);
  initialize(db, b, key);
  QSqlQuery q(db.d);
  q.prepare("SELECT id,source,target,status,error FROM records WHERE project=? "
            "ORDER BY id");
  q.addBindValue(key);
  require(q.exec(), q.lastError().text());
  QVector<Row> r;
  while (q.next())
    r.append({q.value(0).toUInt(), q.value(1).toString(), q.value(2).toString(),
              q.value(3).toString(), q.value(4).toString()});
  return r;
}
void importSeed(const Settings &s, const QString &path) {
  auto b = Babel::load(s.source);
  auto key = projectKey(s);
  DB db(s.project);
  initialize(db, b, key);
  QJsonParseError err;
  auto doc = QJsonDocument::fromJson(readFile(path), &err);
  require(err.error == QJsonParseError::NoError && doc.isObject(),
          "Некоректний JSON перекладу");
  auto obj = doc.object();
  QHash<quint32, QString> original;
  for (auto &e : b.entries)
    original[e.id] = e.source;
  db.d.transaction();
  QSqlQuery q(db.d);
  q.prepare("UPDATE records SET target=?,status='manual',error='' WHERE "
            "project=? AND id=?");
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    bool ok;
    auto id = it.key().toUInt(&ok, 16);
    require(ok && original.contains(id),
            "Невідомий ID у перекладі: " + it.key());
    auto item = it.value().toObject();
    require(!item.contains("uk") || s.targetLang == "uk",
            "Цей JSON містить український переклад, оберіть цільову мову uk");
    auto text = item.contains("target") ? item.value("target").toString()
                                        : item.value("uk").toString();
    require(item.value("source").toString() == original[id],
            "Оригінал не відповідає ID " + it.key());
    validate(original[id], text);
    q.addBindValue(text);
    q.addBindValue(key);
    q.addBindValue(qulonglong(id));
    require(q.exec(), q.lastError().text());
  }
  require(db.d.commit(), "Помилка імпорту");
}
static void update(Progress &p, const QString &t) {
  QMutexLocker lock(&p.mutex);
  p.message = t;
}
struct EngineFail : Fail {
  using Fail::Fail;
};
static QJsonObject http(const QUrl &base, const QString &path,
                        const QJsonObject *body, std::atomic_bool &stop,
                        int timeout = 600000) {
  QNetworkAccessManager manager;
  QNetworkRequest req(base.resolved(QUrl(path)));
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("Authorization", QByteArray("Bearer fable-local-model"));
  req.setTransferTimeout(timeout);
  QNetworkReply *r =
      body ? manager.post(req,
                          QJsonDocument(*body).toJson(QJsonDocument::Compact))
           : manager.get(req);
  QEventLoop loop;
  QTimer timer;
  timer.setInterval(50);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
    if (stop)
      r->abort();
  });
  QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timer.start();
  loop.exec();
  auto data = r->readAll();
  bool ok = r->error() == QNetworkReply::NoError;
  auto reason = r->errorString();
  r->deleteLater();
  require(!stop, "Зупинено користувачем");
  if (!ok)
    throw EngineFail(("Рушій перекладу: " + reason + " " +
                      QString::fromUtf8(data.left(1200)))
                         .toStdString());
  auto doc = QJsonDocument::fromJson(data);
  require(doc.isObject(), "Рушій повернув не JSON");
  return doc.object();
}
struct Model {
  QProcess process;
  QUrl url;
  Settings s;
  Progress &p;
  QString identity;
  Model(const Settings &cfg, Progress &pr) : s(cfg), p(pr) {
    require(QFileInfo::exists(s.model) && readFileMagic(s.model),
            "Оберіть TranslateGemma GGUF");
    require(QFileInfo(s.engine).isExecutable(), "Відсутній llama-server");
    update(p, "Обчислення ідентичності моделі…");
    identity = fileSha(s.model);
    QTcpServer probe;
    require(probe.listen(QHostAddress::LocalHost, 0), "Не вдалося обрати порт");
    int port = probe.serverPort();
    probe.close();
    url = QUrl("http://127.0.0.1:" + QString::number(port));
    QStringList args{"--model",
                     s.model,
                     "--alias",
                     "fable-translategemma",
                     "--host",
                     "127.0.0.1",
                     "--port",
                     QString::number(port),
                     "--api-key",
                     "fable-local-model",
                     "--jinja",
                     "--ctx-size",
                     QString::number(s.context),
                     "--parallel",
                     "1",
                     "--threads",
                     QString::number(s.threads),
                     "--n-gpu-layers",
                     QString::number(s.layers),
                     "--batch-size",
                     QString::number(s.batch),
                     "--ubatch-size",
                     QString::number(qMin(128, s.batch)),
                     "--no-webui"};
    if (s.layers == 0)
      args << "--device" << "none";
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardOutputFile(QFileInfo(s.project).absolutePath() +
                                      "/model.log",
                                  QIODevice::Truncate);
    process.start(s.engine, args);
    require(process.waitForStarted(10000), "Не вдалося запустити модель");
    update(p, "Завантаження TranslateGemma 4B…");
    QElapsedTimer elapsed;
    elapsed.start();
    bool ready = false;
    while (elapsed.elapsed() < 180000 && !p.stop) {
      if (process.state() == QProcess::NotRunning)
        throw Fail("Рушій завершився. Дивіться model.log; перевірте CPU/CUDA "
                   "та памʼять");
      try {
        http(url, "/health", nullptr, p.stop, 700);
        ready = true;
        break;
      } catch (const Fail &) {
      }
      QThread::msleep(150);
    }
    if (!ready) {
      shutdown();
      throw Fail(p.stop ? "Зупинено" : "Модель не завантажилася за 180 секунд");
    }
    auto props = http(url, "/props", nullptr, p.stop);
    auto tmpl = props.value("chat_template").toString();
    if (!tmpl.contains("source_lang_code") ||
        !tmpl.contains("target_lang_code")) {
      shutdown();
      throw Fail("GGUF не містить шаблону TranslateGemma; звичайна Gemma не "
                 "підходить");
    }
  }
  static bool readFileMagic(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.read(4) == "GGUF";
  }
  void shutdown() {
    if (process.state() != QProcess::NotRunning) {
      process.terminate();
      if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(3000);
      }
    }
  }
  ~Model() { shutdown(); }
  QString translate(const QString &source,
                    const QMap<QString, QString> &gloss) {
    QStringList lines = source.split('\n', Qt::KeepEmptyParts);
    QStringList output;
    for (auto &line : lines) {
      if (line.trimmed().isEmpty() || technical(line)) {
        output << line;
        continue;
      }
      auto plan = protect(line, gloss);
      int budget = s.quality == "fast" ? 1 : s.quality == "thorough" ? 4 : 2;
      QString selected, lastError;
      double best = -INFINITY;
      for (int n = 0; n < budget; n++) {
        require(!p.stop, "Зупинено");
        QJsonObject request{
            {"model", "fable-translategemma"},
            {"messages", QJsonArray{QJsonObject{{"role", "user"},
                                                {"content", plan.masked}}}},
            {"chat_template_kwargs",
             QJsonObject{{"source_lang_code", s.sourceLang},
                         {"target_lang_code", s.targetLang}}},
            {"max_tokens", s.maxTokens},
            {"temperature", budget == 1 ? 0.0 : 0.15},
            {"seed", n},
            {"logprobs", true},
            {"top_logprobs", 1},
            {"stream", false}};
        try {
          auto result = http(url, "/v1/chat/completions", &request, p.stop);
          auto choice = result["choices"].toArray().at(0).toObject();
          require(choice["finish_reason"].toString() != "length",
                  "Відповідь обрізано. Збільште ліміт токенів");
          auto rawOutput = choice["message"].toObject()["content"].toString();
          if (s.targetLang == "uk" || s.targetLang == "be")
            rawOutput.replace(QChar(0x27), QChar(0x02bc));
          // Each request represents exactly one original line. Model wrapping
          // must not introduce new game line breaks.
          rawOutput.replace(QRegularExpression("[\\r\\n]+"), " ");
          auto text = restore(rawOutput, plan, line);
          validate(line, text);
          require(text.size() >= line.size() / 5 || line.size() < 100,
                  "Підозріло скорочений переклад");
          require(!(text == line && line.size() > 80),
                  "Довгий текст залишився без змін");
          double score = 0;
          int count = 0;
          for (auto v : choice["logprobs"].toObject()["content"].toArray()) {
            auto o = v.toObject();
            if (o["logprob"].isDouble()) {
              score += o["logprob"].toDouble();
              count++;
            }
          }
          score = count ? score / count : -n;
          if (selected.isEmpty() || score > best) {
            best = score;
            selected = text;
          }
        } catch (const EngineFail &) {
          throw;
        } catch (const Fail &e) {
          lastError = e.what();
          if (p.stop)
            throw;
        }
      }
      if (selected.isEmpty() && !plan.restore.isEmpty()) {
        // If the model cannot retain markers, translate only their surrounding
        // text. Protected game tokens never pass through generation here.
        QString fallback;
        int offset = 0;
        for (const auto &item : plan.restore) {
          const int at = plan.masked.indexOf(item.first, offset);
          require(at >= offset, "Некоректний план захисту");
          auto fragment = plan.masked.mid(offset, at - offset);
          fallback +=
              fragment.trimmed().isEmpty() ? fragment : translate(fragment, {});
          fallback += item.second;
          offset = at + item.first.size();
        }
        auto fragment = plan.masked.mid(offset);
        fallback +=
            fragment.trimmed().isEmpty() ? fragment : translate(fragment, {});
        validate(line, fallback);
        selected = fallback;
      }
      require(!selected.isEmpty(),
              lastError.isEmpty() ? "Модель не повернула придатного перекладу"
                                  : lastError);
      output << selected;
    }
    auto joined = output.join('\n');
    validate(source, joined);
    return joined;
  }
};
static QMap<QString, QString> loadGlossary(const QString &p) {
  QMap<QString, QString> r;
  if (p.isEmpty())
    return r;
  QJsonParseError e;
  auto doc = QJsonDocument::fromJson(readFile(p), &e);
  require(e.error == QJsonParseError::NoError && doc.isObject(),
          "Некоректний словник термінів");
  auto o = doc.object();
  for (auto i = o.begin(); i != o.end(); ++i)
    if (i.value().isString())
      r[i.key()] = i.value().toString();
  return r;
}
void translateJob(const Settings &s, Progress &p) {
  update(p, "Читання текстів і збереження черги…");
  auto b = Babel::load(s.source);
  auto key = projectKey(s);
  DB db(s.project);
  initialize(db, b, key);
  auto glossary = loadGlossary(s.glossary);
  auto all = rows(s);
  QHash<QString, QVector<quint32>> pending;
  QStringList order;
  int already = 0;
  QHash<QString, QString> trusted;
  QSet<QString> ambiguous;
  for (auto &r : all)
    if (r.status == "manual") {
      if (trusted.contains(r.source) && trusted[r.source] != r.target)
        ambiguous.insert(r.source);
      else
        trusted[r.source] = r.target;
    }
  for (auto &key : ambiguous)
    trusted.remove(key);
  auto queued = all;
  std::stable_sort(queued.begin(), queued.end(),
                   [](const Row &a, const Row &b) {
                     return (a.status == "error") < (b.status == "error");
                   });
  for (auto &r : queued) {
    if (s.errorsOnly && r.status != "error")
      continue;
    if (r.status == "pending" || r.status == "error") {
      if (!pending.contains(r.source))
        order << r.source;
      pending[r.source] << r.id;
    } else
      already++;
  }
  {
    QMutexLocker lock(&p.mutex);
    p.total = s.errorsOnly ? int(std::count_if(all.begin(), all.end(),
                                               [](const Row &r) {
                                                 return r.status == "error";
                                               }))
                           : all.size();
    p.done = already;
    p.errors = 0;
  }
  if (order.isEmpty()) {
    update(p, "Усі записи вже оброблено");
    return;
  }
  Model model(s, p);
  QString policy = model.identity + "|" + s.sourceLang + "|" + s.targetLang +
                   "|" + s.quality + "|v1|" + [&] {
                     QJsonObject o;
                     for (auto i = glossary.begin(); i != glossary.end(); ++i)
                       o[i.key()] = i.value();
                     return sha(QJsonDocument(o).toJson());
                   }();
  int completed = 0;
  for (auto &source : order) {
    if (p.stop)
      break;
    if (s.limit > 0 && completed >= s.limit)
      break;
    update(p, QString("Переклад: %1").arg(source.left(110)));
    QString target, error, status = "draft";
    auto memoryKey = sha((policy + "|" + source).toUtf8());
    QSqlQuery cache(db.d);
    cache.prepare("SELECT target FROM memory WHERE key=?");
    cache.addBindValue(memoryKey);
    cache.exec();
    try {
      if (trusted.contains(source))
        target = trusted[source];
      else if (cache.next())
        target = cache.value(0).toString();
      else
        target = model.translate(source, glossary);
      validate(source, target);
      QSqlQuery save(db.d);
      save.prepare("INSERT OR REPLACE INTO memory VALUES(?,?)");
      save.addBindValue(memoryKey);
      save.addBindValue(target);
      require(save.exec(), save.lastError().text());
    } catch (const EngineFail &) {
      // Transport/context failures affect all rows: leave current row queued.
      throw;
    } catch (const Fail &e) {
      if (p.stop)
        break;
      error = e.what();
      status = "error";
      target.clear();
    }
    db.d.transaction();
    QSqlQuery save(db.d);
    save.prepare("UPDATE records SET target=?,status=?,error=? WHERE project=? "
                 "AND id=?");
    for (auto id : pending[source]) {
      save.addBindValue(target);
      save.addBindValue(status);
      save.addBindValue(error);
      save.addBindValue(key);
      save.addBindValue(qulonglong(id));
      require(save.exec(), save.lastError().text());
    }
    require(db.d.commit(), "Помилка збереження прогресу");
    {
      QMutexLocker lock(&p.mutex);
      p.done += pending[source].size();
      if (!error.isEmpty())
        p.errors += pending[source].size();
    }
    completed++;
    // Invalid content stays visible as an error; it cannot block other rows.
  }
  update(p, p.stop ? "Зупинено. Прогрес збережено"
                   : QString("Обробку завершено. Нових різних текстів: %1. "
                             "Перегляньте помилки перед експортом")
                         .arg(completed));
}
void exportJob(const Settings &s, const QString &folder,
               const QStringList &fonts, const QString &ttf) {
  auto b = Babel::load(s.source);
  auto all = rows(s);
  QHash<quint32, QString> changes;
  QString translated;
  int errors = 0, pending = 0;
  for (auto &r : all) {
    if (r.status == "draft" || r.status == "manual") {
      validate(r.source, r.target);
      changes[r.id] = r.target;
      translated += r.target;
    } else if (r.status == "error")
      errors++;
    else if (r.status == "pending")
      pending++;
  }
  require(!changes.isEmpty(), "Немає перекладів для експорту");
  QString culture =
      QFileInfo(QFileInfo(s.source).absolutePath()).dir().dirName();
  if (!QRegularExpression("^[a-z]{2}-[a-z0-9]{2,4}$",
                          QRegularExpression::CaseInsensitiveOption)
           .match(culture)
           .hasMatch())
    culture = "en-uk";
  auto dest = QFileInfo(folder).absoluteFilePath();
  auto resolved = QFileInfo(dest).canonicalFilePath();
  if (!resolved.isEmpty())
    dest = resolved;
  auto inputDir = QFileInfo(s.source).canonicalPath();
  int at = inputDir.lastIndexOf("/data/language");
  if (at >= 0) {
    auto game = inputDir.left(at);
    require(dest != game && !dest.startsWith(game + "/"),
            "Експортуйте поза папкою гри. Оригінали не перезаписуються");
  }
  require(QDir().mkpath(dest), "Не вдалося створити папку експорту");
  dest = QFileInfo(dest).canonicalFilePath();
  if (at >= 0) {
    auto game = inputDir.left(at);
    require(dest != game && !dest.startsWith(game + "/"),
            "Папка експорту посилається на гру");
  }
  QString prefix = dest + "/data/language/" + culture;
  QVector<QPair<QString, QByteArray>> staged;
  QJsonArray fontReport;
  bool seenFont = false, fontLE = false;
  if (!fonts.isEmpty())
    for (auto &path : fonts) {
      auto f = Font::load(path);
      require(!seenFont || fontLE == f.le,
              "Не змішуйте PC та Xbox 360 шрифти в одному патчі");
      seenFont = true;
      fontLE = f.le;
      int added = 0;
      auto bytes = f.add(ttf, translated, &added);
      QString relative =
          "data/language/" + culture + "/fonts/" + QFileInfo(path).fileName();
      if (QFileInfo(path).fileName().startsWith("couriernew_") ||
          QFileInfo(path).fileName().startsWith("timesnewroman_") ||
          (QFileInfo(path).dir().dirName() == "fonts" &&
           (QFileInfo(path).dir().absolutePath().endsWith("/language/fonts") ||
            QFileInfo(path).dir().absolutePath().endsWith(
                "/language org/fonts"))))
        relative = "data/language/fonts/" + QFileInfo(path).fileName();
      require(std::none_of(staged.begin(), staged.end(),
                           [&](auto &v) { return v.first == relative; }),
              "Дубль назви шрифту в експорті");
      staged.append({relative, bytes});
      fontReport.append(QJsonObject{{"file", relative},
                                    {"added", added},
                                    {"platform", f.le ? "PC" : "Xbox 360"},
                                    {"version", f.version}});
    }
  auto bytes = b.build(changes);
  QJsonObject patch;
  for (auto &e : b.entries)
    if (changes.contains(e.id))
      patch[QString::number(e.id, 16).rightJustified(8, '0')] =
          QJsonObject{{"source", e.source}, {"target", changes[e.id]}};
  staged.append({"data/language/" + culture + "/text/book.babel", bytes});
  staged.append({"translations.json", QJsonDocument(patch).toJson()});
  for (auto &item : staged) {
    auto existing = dest + "/" + item.first;
    QString walk = dest;
    for (auto &part : item.first.split('/')) {
      walk += "/" + part;
      require(!QFileInfo(walk).isSymLink(),
              "Експорт не записує через символьні посилання");
    }
    require(QFileInfo(existing).canonicalFilePath() !=
                QFileInfo(s.source).canonicalFilePath(),
            "Заборонено змінювати оригінал");
    writeFile(existing, item.second);
  }
  QJsonObject manifest{{"format", "FableLocalizationStudio-1"},
                       {"game", s.game},
                       {"source_sha256", sha(b.original)},
                       {"babel_sha256", sha(bytes)},
                       {"source_language", s.sourceLang},
                       {"target_language", s.targetLang},
                       {"entries", b.entries.size()},
                       {"translated", changes.size()},
                       {"pending", pending},
                       {"errors", errors},
                       {"fonts", fontReport},
                       {"font_coverage_checked", !fonts.isEmpty()},
                       {"game_tested", false}};
  writeFile(dest + "/manifest.json", QJsonDocument(manifest).toJson());
  writeFile(
      dest + "/README.txt",
      QString("Локальний переклад Fable II/III\nПерекладено %1 із %2. Ще не "
              "оброблено: %3. Помилки: %4.\nПакет замінює текст вибраної мови "
              "%5. Нову мову в меню гри не додає. Спершу збережіть оригінали. "
              "Потім копіюйте data в папку гри.\nМашинний переклад потребує "
              "редактури. Пакет у грі не перевірено.\n")
          .arg(changes.size())
          .arg(b.entries.size())
          .arg(pending)
          .arg(errors)
          .arg(culture)
          .toUtf8());
}
int smokeTest(const Settings &s, Progress &p) {
  Model m(s, p);
  auto t = m.translate("Hello, little Sparrow!", {});
  qInfo().noquote() << t;
  require(t != QString("Hello, little Sparrow!"), "Модель не переклала пробу");
  auto x = m.translate("Press <a_img> to receive %1 gold.", {});
  qInfo().noquote() << x;
  validate("Press <a_img> to receive %1 gold.", x);
  writeFile(QFileInfo(s.project).absolutePath() + "/smoke-result.json",
            QJsonDocument(QJsonObject{{"model", s.model},
                                      {"sample_1", t},
                                      {"sample_2", x},
                                      {"tokens_verified", true}})
                .toJson());
  fprintf(stdout, "%s\n%s\n", t.toUtf8().constData(), x.toUtf8().constData());
  return 0;
}
} // namespace fls
