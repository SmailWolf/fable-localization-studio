#include "core.hpp"
#include <QtWidgets>
#include <iostream>
#include <thread>
using namespace fls;
namespace {
QString engineFile(const QString &root, bool cuda = false) {
  QString path = root + "/engine/" + (cuda ? "cuda" : "cpu") + "/llama-server";
#ifdef Q_OS_WIN
  path += ".exe";
#endif
  return path;
}
class Window : public QMainWindow {
  QString root;
  Settings cfg;
  Progress progress;
  std::thread worker;
  QVector<Row> data;
  QLineEdit *source, *model, *project, *ttf, *glossary;
  QComboBox *from, *to, *quality, *backend, *stateFilter, *game;
  QJsonObject gameSources;
  QString activeGame = "fable2", editScope;
  QHash<QString, QHash<quint32, QString>> projectEdits;
  int sortColumn = 0;
  Qt::SortOrder sortOrder = Qt::AscendingOrder;
  QSpinBox *threads, *context, *batch, *maximum, *limit;
  QPushButton *start, *stop, *open, *exportButton, *seed, *save, *retryErrors;
  QTableWidget *table;
  QPlainTextEdit *editor, *original, *fontChars;
  QPushButton *previous, *next;
  QLabel *pageLabel, *rowError;
  int page = 0;
  QHash<quint32, QString> edits;
  quint32 editingId = 0;
  bool loadingEditor = false;
  QLabel *summary, *status, *missing, *atlas;
  QProgressBar *bar;
  QListWidget *fonts;
  QLineEdit *search;
  QTimer timer;
  void error(const std::exception &e) {
    QMessageBox::warning(this, "Не вдалося виконати",
                         QString::fromUtf8(e.what()));
  }
  QLineEdit *pathField(QFormLayout *form, const QString &label,
                       bool file = true) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto *l = new QLineEdit;
    auto *b = new QPushButton("Обрати…");
    h->addWidget(l, 1);
    h->addWidget(b);
    form->addRow(label, w);
    connect(b, &QPushButton::clicked, this, [this, label, file, l] {
      auto p = file ? QFileDialog::getOpenFileName(this, label, l->text())
                    : QFileDialog::getSaveFileName(this, label, l->text());
      if (!p.isEmpty())
        l->setText(p);
    });
    return l;
  }
  Settings settings() {
    Settings s;
    s.source = source->text();
    s.game = game->currentData().toString();
    s.model = model->text();
    s.project = project->text();
    s.sourceLang = from->currentData().toString();
    s.targetLang = to->currentData().toString();
    s.quality = quality->currentData().toString();
    s.threads = threads->value();
    s.context = context->value();
    s.batch = batch->value();
    s.maxTokens = maximum->value();
    s.limit = limit->value();
    s.layers = backend->currentIndex() == 0 ? 0 : 99;
    s.engine = engineFile(root, s.layers != 0);
    s.glossary = glossary->text();
    return s;
  }
  QStringList fontPaths() {
    QStringList r;
    for (int i = 0; i < fonts->count(); i++)
      r << fonts->item(i)->text();
    return r;
  }
  void persist() {
    auto s = settings();
    auto portable = [&](const QString &p) {
      return p.startsWith(root + "/") ? QDir(root).relativeFilePath(p) : p;
    };
    gameSources[activeGame] = s.source;
    QJsonObject j{{"source", s.source},
                  {"game", s.game},
                  {"gameSources", gameSources},
                  {"model", portable(s.model)},
                  {"project", portable(s.project)},
                  {"sourceLang", s.sourceLang},
                  {"targetLang", s.targetLang},
                  {"quality", s.quality},
                  {"backend", backend->currentIndex()},
                  {"threads", s.threads},
                  {"context", s.context},
                  {"batch", s.batch},
                  {"maxTokens", s.maxTokens},
                  {"limit", s.limit},
                  {"ttf", portable(ttf->text())},
                  {"glossary", portable(s.glossary)}};
    writeFile(root + "/state/settings.json", QJsonDocument(j).toJson());
  }
  void refresh() {
    try {
      cfg = settings();
      auto scope = cfg.project + "|" + projectKey(cfg);
      if (scope != editScope) {
        projectEdits[editScope] = edits;
        edits = projectEdits.value(scope);
        editScope = scope;
        page = 0;
      }
      data = rows(cfg);
      int translated = 0, errors = 0, preserved = 0;
      for (auto &r : data) {
        translated += r.status == "draft" || r.status == "manual";
        errors += r.status == "error";
        preserved += r.status == "preserved";
      }
      summary->setText(QString("%1 записів   •   %2 перекладено   •   %3 "
                               "помилок   •   %4 службових")
                           .arg(data.size())
                           .arg(translated)
                           .arg(errors)
                           .arg(preserved));
      fillTable();
      persist();
    } catch (const std::exception &e) {
      error(e);
    }
  }
  static QString stateLabel(const QString &state) {
    return state == "draft"       ? "Чернетка"
           : state == "manual"    ? "Збережено вручну"
           : state == "error"     ? "Помилка"
           : state == "preserved" ? "Службове"
                                  : "Очікує";
  }
  void fillTable() {
    QSignalBlocker blocker(table);
    quint32 selected = table->currentRow() >= 0
                           ? data.value(table->item(table->currentRow(), 0)
                                            ->data(Qt::UserRole)
                                            .toInt())
                                 .id
                           : 0;
    table->setRowCount(0);
    loadingEditor = true;
    original->clear();
    editor->clear();
    editingId = 0;
    loadingEditor = false;
    rowError->clear();
    QString filter = search->text();
    int shown = 0, matched = 0, selectedRow = -1;
    QVector<int> matching;
    for (int n = 0; n < data.size(); n++) {
      auto &r = data[n];
      if (!stateFilter->currentData().toString().isEmpty() &&
          r.status != stateFilter->currentData().toString())
        continue;
      if (!filter.isEmpty() &&
          !r.source.contains(filter, Qt::CaseInsensitive) &&
          !r.target.contains(filter, Qt::CaseInsensitive) &&
          !r.status.contains(filter, Qt::CaseInsensitive) &&
          !r.error.contains(filter, Qt::CaseInsensitive) &&
          !QString::number(r.id, 16).rightJustified(8, '0').contains(
              filter, Qt::CaseInsensitive))
        continue;
      matching << n;
    }
    // Sort the entire result before pagination, keeping data indices stable
    // so selection, edits and saves always address the original record.
    QCollator collator(QLocale("uk_UA"));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    auto stateRank = [](const QString &state) {
      return state == "error"     ? 0
             : state == "pending" ? 1
             : state == "draft"   ? 2
             : state == "manual"  ? 3
                                  : 4;
    };
    std::sort(matching.begin(), matching.end(), [&](int a, int b) {
      const auto &x = data[a];
      const auto &y = data[b];
      int cmp = 0;
      if (sortColumn == 0)
        cmp = x.id < y.id ? -1 : x.id > y.id ? 1 : 0;
      else if (sortColumn == 3)
        cmp = stateRank(x.status) - stateRank(y.status);
      else
        cmp = collator.compare(sortColumn == 1 ? x.source : x.target,
                               sortColumn == 1 ? y.source : y.target);
      if (cmp == 0)
        return x.id < y.id;
      return sortOrder == Qt::AscendingOrder ? cmp < 0 : cmp > 0;
    });
    matched = matching.size();
    page = qBound(0, page, qMax(0, (matched - 1) / 600));
    for (int n : matching.mid(page * 600, 600)) {
      auto &r = data[n];
      table->insertRow(shown);
      if (r.id == selected)
        selectedRow = shown;
      QString label = stateLabel(r.status);
      for (int c = 0; c < 4; c++) {
        auto *item = new QTableWidgetItem(
            c == 0   ? QString::number(r.id, 16).rightJustified(8, '0')
            : c == 1 ? r.source
            : c == 2 ? r.target
                     : label);
        item->setData(Qt::UserRole, n);
        if (!r.error.isEmpty())
          item->setToolTip(r.error);
        table->setItem(shown, c, item);
      }
      shown++;
    }
    table->resizeRowsToContents();
    previous->setEnabled(page > 0);
    next->setEnabled((page + 1) * 600 < matched);
    pageLabel->setText(QString("Сторінка %1 / %2 • Знайдено: %3")
                           .arg(page + 1)
                           .arg(qMax(1, (matched + 599) / 600))
                           .arg(matched));
    blocker.unblock();
    if (shown)
      table->selectRow(selectedRow >= 0 ? selectedRow : 0);
  }
  void busy(bool b) {
    for (auto *w : {start, open, exportButton, seed, save, retryErrors})
      w->setEnabled(!b);
    stop->setEnabled(b);
    game->setEnabled(!b);
    source->setEnabled(!b);
    from->setEnabled(!b);
    to->setEnabled(!b);
    project->setEnabled(!b);
    for (auto *w : QList<QWidget *>{model, quality, backend, threads, context,
                                    batch, maximum, limit, glossary})
      w->setEnabled(!b);
  }
  void launch(bool errorsOnly = false) {
    if (worker.joinable())
      worker.join();
    try {
      cfg = settings();
      cfg.errorsOnly = errorsOnly;
      if (errorsOnly)
        cfg.limit = 0;
      require(QFileInfo::exists(cfg.source), "Оберіть book.babel");
      require(cfg.sourceLang != cfg.targetLang,
              "Оберіть різні мови джерела й перекладу");
      persist();
      progress.stop = false;
      {
        QMutexLocker l(&progress.mutex);
        progress.running = true;
        progress.errors = 0;
        progress.done = 0;
        progress.message = "Підготовка…";
      }
      busy(true);
      worker = std::thread([this, s = cfg] {
        try {
          translateJob(s, progress);
        } catch (const std::exception &e) {
          QMutexLocker l(&progress.mutex);
          progress.message =
              "Зупинено через помилку: " + QString::fromUtf8(e.what());
        }
        QMutexLocker l(&progress.mutex);
        progress.running = false;
      });
    } catch (const std::exception &e) {
      busy(false);
      error(e);
    }
  }

public:
  void discoverFonts() {
    if (fonts->count() || !QFileInfo::exists(source->text()))
      return;
    auto dir = QFileInfo(source->text()).dir();
    dir.cdUp();
    for (const auto &path :
         {dir.filePath("fonts"), QDir(dir.filePath("..")).filePath("fonts")}) {
      QDir glyph(path);
      for (const auto &name : glyph.entryList({"*.bft", "*.BFT"}, QDir::Files))
        fonts->addItem(glyph.filePath(name));
    }
  }
  void testGames() {
    require(game->currentData().toString() == "fable2" && data.size() == 56000,
            "Fable II profile load failed");
    const auto originalBook = source->text();
    const auto originalKey = projectKey(settings());
    auto originalFontPaths = fontPaths();
    table->selectRow(0);
    const auto firstId = editingId;
    editor->setPlainText("Незбережена правка Fable II");
    game->setCurrentIndex(game->findData("fable3"));
    require(data.size() == 68439 && settings().game == "fable3",
            "Fable III profile load failed");
    require(projectKey(settings()) != originalKey && !edits.contains(firstId),
            "Games share progress or unsaved edits");
    for (const auto &path : fontPaths())
      require(!originalFontPaths.contains(path), "Old game fonts retained");
    game->setCurrentIndex(game->findData("fable2"));
    require(source->text() == originalBook && data.size() == 56000,
            "Fable II book not restored");
    require(edits.value(firstId) == "Незбережена правка Fable II",
            "Game switch lost draft edit");
    require(fontPaths() == originalFontPaths, "Fable II fonts not restored");
    auto saved =
        QJsonDocument::fromJson(readFile(root + "/state/settings.json"))
            .object();
    require(saved["game"].toString() == "fable2" &&
                saved["gameSources"].toObject().contains("fable3"),
            "Game profiles not persisted");
    fprintf(stderr, "PASS Fable II/III switching, remembered books, separate "
                    "progress and edits, correct fonts, persisted profiles\n");
  }
  void testTable() {
    int errors = 0;
    quint32 maximumId = 0;
    for (const auto &r : data) {
      errors += r.status == "error";
      maximumId = qMax(maximumId, r.id);
    }
    require(errors > 0, "Test needs error rows");
    stateFilter->setCurrentIndex(stateFilter->findData("error"));
    require(table->rowCount() == qMin(errors, 600), "Error filter count wrong");
    for (int i = 0; i < table->rowCount(); ++i)
      require(data[table->item(i, 0)->data(Qt::UserRole).toInt()].status ==
                  "error",
              "Error filter leaked another state");
    table->selectRow(0);
    const auto edited = editingId;
    editor->setPlainText("Незбережена перевірка сортування");
    stateFilter->setCurrentIndex(0);
    QMetaObject::invokeMethod(table->horizontalHeader(), "sectionClicked",
                              Q_ARG(int, 0));
    require(sortOrder == Qt::DescendingOrder &&
                data[table->item(0, 0)->data(Qt::UserRole).toInt()].id ==
                    maximumId,
            "Sort only covers one page");
    next->click();
    require(page == 1, "Sorted pagination failed");
    quint32 last = maximumId;
    for (int i = 0; i < table->rowCount(); ++i) {
      auto n = table->item(i, 0)->data(Qt::UserRole).toInt();
      require(data[n].id < last && table->item(i, 1)->text() == data[n].source,
              "Sorted rows mismatched");
      last = data[n].id;
    }
    QMetaObject::invokeMethod(table->horizontalHeader(), "sectionClicked",
                              Q_ARG(int, 3));
    require(table->item(0, 3)->text() == "Помилка",
            "Status sort must show errors first");
    stateFilter->setCurrentIndex(stateFilter->findData("error"));
    search->setText(QString::number(edited, 16).rightJustified(8, '0'));
    require(table->rowCount() == 1 &&
                editor->toPlainText() == "Незбережена перевірка сортування",
            "Unsaved edit lost after sort/filter");
    search->setText("this-text-does-not-exist-000000");
    require(table->rowCount() == 0 && editor->toPlainText().isEmpty(),
            "Empty filter failed");
    search->clear();
    findChild<QTabWidget *>()->setCurrentIndex(1);
    fprintf(
        stderr,
        "PASS state filtering, global ID sort, sorted pagination, row mapping, "
        "errors-first status sort, unsaved edit retention, empty results\n");
  }
  void testEditor() {
    require(original->isReadOnly(), "Original must be read only");
    next->click();
    require(page == 1 && table->rowCount() > 0, "Pagination failed");
    previous->click();
    search->setText("pending");
    int chosen = -1;
    for (int i = 0; i < table->rowCount(); ++i) {
      auto &r = data[table->item(i, 0)->data(Qt::UserRole).toInt()];
      if (tokens(r.source).isEmpty() && !r.source.contains('\n')) {
        chosen = i;
        break;
      }
    }
    require(chosen >= 0, "No test row");
    table->selectRow(chosen);
    const auto id = editingId;
    const auto sourceText = original->toPlainText();
    editor->setPlainText("Ручний тест Ї Є Ґ ʼ");
    table->selectRow((chosen + 1) % table->rowCount());
    table->selectRow(chosen);
    require(editor->toPlainText() == "Ручний тест Ї Є Ґ ʼ",
            "Edit lost on row switch");
    for (auto *button : findChildren<QPushButton *>()) {
      if (button->text() == "Копіювати оригінал")
        button->click();
    }
    require(QApplication::clipboard()->text() == sourceText,
            "Original copy failed");
    for (auto *button : findChildren<QPushButton *>()) {
      if (button->text() == "Копіювати переклад")
        button->click();
    }
    require(QApplication::clipboard()->text() == "Ручний тест Ї Є Ґ ʼ",
            "Translation copy failed");
    save->click();
    auto saved = rows(settings());
    bool found = false;
    for (auto &r : saved)
      if (r.id == id)
        found = r.status == "manual" && r.target == "Ручний тест Ї Є Ґ ʼ";
    require(found, "Manual translation not persisted");
    search->setText(QString::number(id, 16).rightJustified(8, '0'));
    require(table->rowCount() == 1, "ID search failed");
    findChild<QTabWidget *>()->setCurrentIndex(1);
    fprintf(stderr, "PASS UI pagination, draft retention, original/translation "
                    "copy, manual save, ID search\n");
  }
  Window(QString r) : root(std::move(r)) {
    setWindowTitle("Fable Localization Studio — локальний переклад");
    resize(1280, 860);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(22, 18, 22, 18);
    auto *title = new QLabel("Fable Localization Studio");
    title->setStyleSheet("font-size:26px;font-weight:700;color:#e7c88b");
    layout->addWidget(title);
    layout->addWidget(
        new QLabel("Fable II · Fable III     |     TranslateGemma 4B     |     "
                   "Без витрат квоти Codex під час перекладу"));
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    auto *setup = new QWidget;
    auto *form = new QFormLayout(setup);
    game = new QComboBox;
    game->addItem("Fable II (Fable 2)", "fable2");
    game->addItem("Fable III (Fable 3)", "fable3");
    form->addRow("Частина гри", game);
    source = pathField(form, "Текст гри · book.babel");
    project = pathField(form, "База прогресу · SQLite", false);
    project->setText(root + "/state/project.sqlite");
    model = pathField(form, "Модель TranslateGemma · GGUF");
    model->setText(root + "/models/translategemma-4b-Q4_K_M.gguf");
    from = new QComboBox;
    to = new QComboBox;
    QVector<QPair<QString, QString>> langs{
        {"Англійська", "en"},    {"Українська", "uk"},    {"Білоруська", "be"},
        {"Польська", "pl"},      {"Німецька", "de"},      {"Французька", "fr"},
        {"Іспанська", "es"},     {"Італійська", "it"},    {"Чеська", "cs"},
        {"Словацька", "sk"},     {"Угорська", "hu"},      {"Румунська", "ro"},
        {"Болгарська", "bg"},    {"Сербська", "sr"},      {"Хорватська", "hr"},
        {"Грецька", "el"},       {"Турецька", "tr"},      {"Російська", "ru"},
        {"Португальська", "pt"}, {"Нідерландська", "nl"}, {"Шведська", "sv"},
        {"Японська", "ja"},      {"Корейська", "ko"},     {"Китайська", "zh"}};
    for (auto &v : langs) {
      from->addItem(v.first, v.second);
      to->addItem(v.first, v.second);
    }
    to->setCurrentIndex(1);
    form->addRow("Мова оригіналу", from);
    form->addRow("Мова перекладу", to);
    quality = new QComboBox;
    quality->addItem("Швидко — 1 варіант", "fast");
    quality->addItem("Збалансовано — 2 варіанти", "balanced");
    quality->addItem("Ретельно — 4 варіанти", "thorough");
    quality->setCurrentIndex(1);
    form->addRow("Профіль перекладу", quality);
    auto *hint = new QLabel("Ретельні режими порівнюють придатні варіанти за "
                            "впевненістю моделі. Це не замінює редактуру.");
    hint->setWordWrap(true);
    form->addRow(hint);
    backend = new QComboBox;
    backend->addItems({"CPU — сумісний режим", "CUDA — відеокарта NVIDIA"});
    form->addRow("Обчислення", backend);
    auto number = [&](const QString &name, int min, int max, int val) {
      auto *s = new QSpinBox;
      s->setRange(min, max);
      s->setValue(val);
      form->addRow(name, s);
      return s;
    };
    threads =
        number("Потоки CPU", 1, 128, qMax(1, QThread::idealThreadCount() / 2));
    context = number("Контекст моделі", 2048, 32768, 8192);
    batch = number("Розмір обробки", 32, 2048, 256);
    maximum = number("Максимум токенів відповіді", 128, 16384, 4096);
    limit = number("Проба: різних текстів за запуск · 0 = усі", 0, 100000, 10);
    glossary = pathField(form, "Необовʼязковий словник термінів · JSON");
    glossary->setPlaceholderText(
        "Порожньо — модель відмінює власні назви самостійно");
    open = new QPushButton("Відкрити тексти / оновити стан");
    seed = new QPushButton("Імпортувати готовий переклад JSON");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(open);
    buttons->addWidget(seed);
    form->addRow(buttons);
    auto *note = new QLabel(
        "Оригінали не змінюються. Експорт замінює обрану мовну папку гри; "
        "нового пункту мови не додає.\nДля повного запуску встановіть «0» "
        "замість проби. Після зупинки наступний запуск продовжить чергу.");
    note->setWordWrap(true);
    form->addRow(note);
    auto *advanced = new QCheckBox("Розширені налаштування моделі та проєкту");
    form->addRow(advanced);
    auto toggleAdvanced = [this, form](bool visible) {
      for (auto *w : QList<QWidget *>{project->parentWidget(), threads, context,
                                      batch, maximum, glossary->parentWidget()})
        form->setRowVisible(w, visible);
    };
    toggleAdvanced(false);
    connect(advanced, &QCheckBox::toggled, this, toggleAdvanced);
    auto *setupScroll = new QScrollArea;
    setupScroll->setWidgetResizable(true);
    setupScroll->setWidget(setup);
    tabs->addTab(setupScroll, "1. Проєкт і модель");
    auto *texts = new QWidget;
    auto *tl = new QVBoxLayout(texts);
    summary = new QLabel("Відкрийте book.babel, щоб побачити тексти.");
    tl->addWidget(summary);
    search = new QLineEdit;
    search->setPlaceholderText(
        "Пошук тексту, ID або стану: pending / error / draft / manual");
    auto *filterBar = new QHBoxLayout;
    stateFilter = new QComboBox;
    stateFilter->addItem("Усі стани", "");
    stateFilter->addItem("Лише помилки", "error");
    stateFilter->addItem("Очікують перекладу", "pending");
    stateFilter->addItem("Чернетки", "draft");
    stateFilter->addItem("Збережено вручну", "manual");
    stateFilter->addItem("Службові", "preserved");
    filterBar->addWidget(search, 1);
    filterBar->addWidget(stateFilter);
    tl->addLayout(filterBar);
    auto *pages = new QHBoxLayout;
    previous = new QPushButton("← Попередні");
    next = new QPushButton("Наступні →");
    pageLabel = new QLabel;
    retryErrors = new QPushButton("Повторити помилки");
    pages->addWidget(retryErrors);
    connect(retryErrors, &QPushButton::clicked, this, [&] { launch(true); });
    pages->addWidget(previous);
    pages->addWidget(pageLabel, 1);
    pages->addWidget(next);
    tl->addLayout(pages);
    table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({"ID", "Оригінал", "Переклад", "Стан"});
    table->horizontalHeader()->setSectionsClickable(true);
    table->horizontalHeader()->setSortIndicatorShown(true);
    table->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
    table->horizontalHeader()->setToolTip(
        "Натисніть заголовок для сортування всіх записів. Ще раз — зворотний "
        "порядок. Стан: спочатку помилки.");
    connect(
        table->horizontalHeader(), &QHeaderView::sectionClicked, this,
        [this](int column) {
          sortOrder = column == sortColumn && sortOrder == Qt::AscendingOrder
                          ? Qt::DescendingOrder
                          : Qt::AscendingOrder;
          sortColumn = column;
          table->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
          page = 0;
          fillTable();
        });
    connect(stateFilter, &QComboBox::currentIndexChanged, this, [this] {
      page = 0;
      fillTable();
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tl->addWidget(table, 1);
    auto *panes = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    auto *right = new QVBoxLayout;
    left->addWidget(new QLabel("Оригінал (можна виділяти й копіювати текст)"));
    right->addWidget(new QLabel("Переклад — введіть або вставте свій текст"));
    original = new QPlainTextEdit;
    original->setObjectName("originalText");
    original->setReadOnly(true);
    editor = new QPlainTextEdit;
    editor->setObjectName("translationText");
    editor->setPlaceholderText("Перекладайте вручну без запуску моделі. "
                               "Збережіть кнопкою або Ctrl+S.");
    original->setMaximumHeight(120);
    editor->setMaximumHeight(120);
    left->addWidget(original);
    right->addWidget(editor);
    auto *copySource = new QPushButton("Копіювати оригінал");
    auto *copyTarget = new QPushButton("Копіювати переклад");
    auto *paste = new QPushButton("Вставити переклад");
    left->addWidget(copySource);
    auto *editActions = new QHBoxLayout;
    editActions->addWidget(copyTarget);
    editActions->addWidget(paste);
    right->addLayout(editActions);
    panes->addLayout(left, 1);
    panes->addLayout(right, 1);
    tl->addLayout(panes);
    rowError = new QLabel;
    rowError->setWordWrap(true);
    rowError->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tl->addWidget(rowError);
    connect(copySource, &QPushButton::clicked, this, [this] {
      QApplication::clipboard()->setText(original->toPlainText());
    });
    connect(copyTarget, &QPushButton::clicked, this, [this] {
      QApplication::clipboard()->setText(editor->toPlainText());
    });
    connect(paste, &QPushButton::clicked, editor, &QPlainTextEdit::paste);
    connect(previous, &QPushButton::clicked, this, [&] {
      --page;
      fillTable();
    });
    connect(next, &QPushButton::clicked, this, [&] {
      ++page;
      fillTable();
    });
    save = new QPushButton("Зберегти редагування обраного рядка");
    save->setShortcut(QKeySequence::Save);
    tl->addWidget(save);
    tabs->addTab(texts, "2. Тексти й редактура");
    auto *fontTab = new QWidget;
    auto *fl = new QVBoxLayout(fontTab);
    auto *fontHint = new QLabel(
        "Додайте всі BFT, що використовуються грою. У Fable II це також три "
        "файли з data/language/fonts.\nВідсутні символи беруться з TTF/OTF; "
        "оригінальні гліфи зберігаються. Підтримано Xbox 360 BFT v1 (BC4) і PC "
        "BFT v1/v3 (RGBA/SDF). Інші формати відхиляються.");
    fontHint->setWordWrap(true);
    fl->addWidget(fontHint);
    fonts = new QListWidget;
    fonts->setMaximumHeight(130);
    fl->addWidget(fonts);
    auto *fb = new QHBoxLayout;
    auto *add = new QPushButton("Додати BFT…");
    auto *clear = new QPushButton("Очистити список");
    fb->addWidget(add);
    fb->addWidget(clear);
    fl->addLayout(fb);
    auto *ff = new QFormLayout;
    ttf = pathField(ff, "Шрифт для нових літер · TTF/OTF");
    ttf->setText(root + "/fonts/NotoSans-Regular.ttf");
    fl->addLayout(ff);
    fontChars = new QPlainTextEdit(QString::fromUtf8(
        "АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯ абвгґдеєжзиіїйклмнопрстуфхцчшщьюя ʼ "
        "Љљ Њњ Ћћ Ђђ Ѓѓ Ќќ Ўў Ёё Ыы Ээ"));
    fontChars->setMaximumHeight(65);
    fl->addWidget(fontChars);
    missing = new QLabel;
    missing->setWordWrap(true);
    fl->addWidget(missing);
    auto *check = new QPushButton("Перевірити й показати вибраний BFT");
    auto *generate = new QPushButton("Додати літери та зберегти новий BFT…");
    auto *fbuttons = new QHBoxLayout;
    fbuttons->addWidget(check);
    fbuttons->addWidget(generate);
    fl->addLayout(fbuttons);
    atlas = new QLabel;
    atlas->setAlignment(Qt::AlignCenter);
    auto *scroll = new QScrollArea;
    scroll->setWidget(atlas);
    scroll->setWidgetResizable(true);
    fl->addWidget(scroll, 1);
    tabs->addTab(fontTab, "3. Кирилиця та шрифти");
    auto *actions = new QHBoxLayout;
    start = new QPushButton("Почати / продовжити переклад");
    start->setObjectName("primary");
    stop = new QPushButton("Зупинити й зберегти");
    stop->setEnabled(false);
    exportButton = new QPushButton("Експортувати патч…");
    actions->addWidget(start);
    actions->addWidget(stop);
    actions->addStretch();
    actions->addWidget(exportButton);
    layout->addLayout(actions);
    bar = new QProgressBar;
    bar->setRange(0, 100);
    layout->addWidget(bar);
    status = new QLabel("Готово до роботи. Спочатку перекладіть 10 текстів і "
                        "перегляньте результат.");
    status->setWordWrap(true);
    layout->addWidget(status);
    setStyleSheet(
        "QMainWindow,QWidget{background:#20252b;color:#ece9e1;font-size:13px}"
        "QLineEdit,QPlainTextEdit,QSpinBox,QComboBox,QTableWidget,QListWidget{"
        "background:#151a20;border:1px solid "
        "#48515c;border-radius:4px;padding:5px;selection-background-color:#"
        "615236}QPushButton{background:#343d48;border:1px solid "
        "#566171;border-radius:5px;padding:9px}QPushButton:hover{background:#"
        "455367}QPushButton:disabled{color:#7c828a}QPushButton#primary{"
        "background:#675330;border-color:#bc9654}QHeaderView::section{"
        "background:#343d48;padding:6px}QProgressBar{border:1px solid "
        "#48515c;border-radius:4px;text-align:center}QProgressBar::chunk{"
        "background:#ab8a50}");
    connect(open, &QPushButton::clicked, this, [&] {
      refresh();
      discoverFonts();
    });
    connect(seed, &QPushButton::clicked, this, [&] {
      auto p = QFileDialog::getOpenFileName(this, "Готовий переклад",
                                            root + "/data", "JSON (*.json)");
      if (p.isEmpty())
        return;
      try {
        importSeed(settings(), p);
        refresh();
      } catch (const std::exception &e) {
        error(e);
      }
    });
    connect(search, &QLineEdit::textChanged, this, [&] {
      page = 0;
      fillTable();
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, [&] {
      auto items = table->selectedItems();
      if (!items.isEmpty()) {
        auto n = items[0]->data(Qt::UserRole).toInt();
        if (n < data.size()) {
          original->setPlainText(data[n].source);
          loadingEditor = true;
          editingId = data[n].id;
          editor->setPlainText(edits.value(editingId, data[n].target));
          loadingEditor = false;
          rowError->setText(data[n].error.isEmpty()
                                ? QString()
                                : "Помилка цього запису: " + data[n].error);
        }
      }
    });
    table->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto *copyCell = new QAction("Копіювати текст поточної клітинки", table);
    copyCell->setShortcut(QKeySequence::Copy);
    copyCell->setShortcutContext(Qt::WidgetShortcut);
    table->addAction(copyCell);
    connect(copyCell, &QAction::triggered, this, [&] {
      if (auto *item = table->currentItem())
        QApplication::clipboard()->setText(item->text());
    });
    connect(editor, &QPlainTextEdit::textChanged, this, [&] {
      if (!loadingEditor && editingId)
        edits[editingId] = editor->toPlainText();
    });
    connect(save, &QPushButton::clicked, this, [&] {
      try {
        auto items = table->selectedItems();
        require(!items.isEmpty(), "Оберіть рядок");
        auto row = data[items[0]->data(Qt::UserRole).toInt()];
        validate(row.source, editor->toPlainText());
        QJsonObject j{{QString::number(row.id, 16).rightJustified(8, '0'),
                       QJsonObject{{"source", row.source},
                                   {"target", editor->toPlainText()}}}};
        auto temp = root + "/state/manual-edit.json";
        writeFile(temp, QJsonDocument(j).toJson());
        importSeed(settings(), temp);
        edits.remove(row.id);
        refresh();
        status->setText("Ручний переклад збережено");
      } catch (const std::exception &e) {
        error(e);
      }
    });
    connect(start, &QPushButton::clicked, this, [&] { launch(); });
    connect(stop, &QPushButton::clicked, this, [&] {
      progress.stop = true;
      status->setText("Зупинка поточного запиту…");
    });
    connect(exportButton, &QPushButton::clicked, this, [&] {
      auto folder = QFileDialog::getExistingDirectory(
          this, "Окрема папка для готового патча", root + "/exports");
      if (folder.isEmpty())
        return;
      try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        exportJob(settings(), folder, fontPaths(), ttf->text());
        QApplication::restoreOverrideCursor();
        QMessageBox::information(
            this, "Патч збережено",
            "Збережено: " + folder +
                "\nПерегляньте manifest.json. Готовий машинний переклад ще "
                "потребує перевірки у грі.");
      } catch (const std::exception &e) {
        QApplication::restoreOverrideCursor();
        error(e);
      }
    });
    connect(add, &QPushButton::clicked, this, [&] {
      auto list = QFileDialog::getOpenFileNames(
          this, "Шрифти Fable", QFileInfo(source->text()).absolutePath(),
          "BFT (*.bft *.BFT)");
      for (auto &p : list)
        if (fonts->findItems(p, Qt::MatchExactly).isEmpty())
          fonts->addItem(p);
    });
    connect(clear, &QPushButton::clicked, this, [&] { fonts->clear(); });
    connect(check, &QPushButton::clicked, this, [&] {
      try {
        require(fonts->currentItem(), "Оберіть BFT зі списку");
        auto f = Font::load(fonts->currentItem()->text());
        auto m = f.missing(fontChars->toPlainText());
        missing->setText(m.isEmpty() ? "Усі вказані символи присутні."
                                     : "Відсутні: " + m);
        auto image = f.atlas();
        atlas->setPixmap(
            QPixmap::fromImage(image).scaled(900, 600, Qt::KeepAspectRatio));
      } catch (const std::exception &e) {
        error(e);
      }
    });
    connect(generate, &QPushButton::clicked, this, [&] {
      try {
        require(fonts->currentItem(), "Оберіть BFT зі списку");
        auto f = Font::load(fonts->currentItem()->text());
        int count = 0;
        auto d = f.add(ttf->text(), fontChars->toPlainText(), &count);
        auto path = QFileDialog::getSaveFileName(
            this, "Новий BFT",
            root + "/exports/" +
                QFileInfo(fonts->currentItem()->text()).fileName(),
            "BFT (*.bft)");
        if (path.isEmpty())
          return;
        require(QFileInfo(path).absoluteFilePath() !=
                    QFileInfo(fonts->currentItem()->text()).absoluteFilePath(),
                "Оберіть новий файл; оригінал зберігається");
        writeFile(path, d);
        auto patched = Font::load(path);
        require(patched.missing(fontChars->toPlainText()).isEmpty(),
                "Не всі символи додано");
        atlas->setPixmap(QPixmap::fromImage(patched.atlas())
                             .scaled(900, 600, Qt::KeepAspectRatio));
        missing->setText(QString("Додано %1 гліфів. Усі символи покриті. "
                                 "Потрібна перевірка у грі.")
                             .arg(count));
      } catch (const std::exception &e) {
        error(e);
      }
    });
    connect(&timer, &QTimer::timeout, this, [&] {
      bool done = false;
      {
        QMutexLocker l(&progress.mutex);
        if (!start->isEnabled()) {
          status->setText(progress.message);
          bar->setRange(0, qMax(1, progress.total));
          bar->setValue(progress.done);
          done = !progress.running;
        }
      }
      if (done) {
        if (worker.joinable())
          worker.join();
        busy(false);
        refresh();
      }
    });
    timer.start(300);
    if (QFileInfo::exists(root + "/state/settings.json")) {
      auto o = QJsonDocument::fromJson(readFile(root + "/state/settings.json"))
                   .object();
      auto path = [&](const QString &p) {
        return p.isEmpty() || QFileInfo(p).isAbsolute()
                   ? p
                   : QDir(root).absoluteFilePath(p);
      };
      auto remembered = o["gameSources"].toObject();
      for (auto it = remembered.begin(); it != remembered.end(); ++it)
        gameSources[it.key()] = it.value();
      auto inferredGame = o["source"].toString().contains(QRegularExpression(
                              "fable[ _-]*(?:iii|3)",
                              QRegularExpression::CaseInsensitiveOption))
                              ? "fable3"
                              : "fable2";
      activeGame = o["game"].toString(inferredGame);
      game->setCurrentIndex(qMax(0, game->findData(activeGame)));
      activeGame = game->currentData().toString();
      source->setText(
          o["source"].toString(gameSources.value(activeGame).toString()));
      model->setText(path(o["model"].toString(model->text())));
      project->setText(path(o["project"].toString(project->text())));
      ttf->setText(path(o["ttf"].toString(ttf->text())));
      glossary->setText(path(o["glossary"].toString()));
      from->setCurrentIndex(
          qMax(0, from->findData(o["sourceLang"].toString("en"))));
      to->setCurrentIndex(
          qMax(0, to->findData(o["targetLang"].toString("uk"))));
      quality->setCurrentIndex(
          qMax(0, quality->findData(o["quality"].toString("balanced"))));
      backend->setCurrentIndex(o["backend"].toInt());
      threads->setValue(o["threads"].toInt(threads->value()));
      context->setValue(o["context"].toInt(8192));
      batch->setValue(o["batch"].toInt(256));
      maximum->setValue(o["maxTokens"].toInt(4096));
      limit->setValue(o["limit"].toInt(limit->value()));
    }
    if (source->text().isEmpty())
      source->setText(gameSources.value(activeGame).toString());
    connect(game, &QComboBox::currentIndexChanged, this, [this] {
      gameSources[activeGame] = source->text();
      activeGame = game->currentData().toString();
      source->setText(gameSources.value(activeGame).toString());
      fonts->clear();
      atlas->clear();
      missing->clear();
      search->clear();
      stateFilter->setCurrentIndex(0);
      if (QFileInfo::exists(source->text())) {
        refresh();
        discoverFonts();
      } else {
        projectEdits[editScope] = edits;
        edits.clear();
        editScope.clear();
        data.clear();
        page = 0;
        fillTable();
        summary->setText("Оберіть book.babel для " + game->currentText());
        persist();
      }
      status->setText("Обрана гра: " + game->currentText());
    });
    if (QFileInfo::exists(source->text())) {
      refresh();
      discoverFonts();
    }
  }
  ~Window() {
    progress.stop = true;
    if (worker.joinable())
      worker.join();
  }
  void closeEvent(QCloseEvent *e) override {
    progress.stop = true;
    e->accept();
  }
};
} // namespace
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QString root =
      QDir(QCoreApplication::applicationDirPath() + "/..").absolutePath();
  try {
    auto args = app.arguments();
    if (args.contains("--self-test"))
      return selfTest(root);
    if (args.contains("--smoke")) {
      Settings s;
      s.model = root + "/models/translategemma-4b-Q4_K_M.gguf";
      s.engine = engineFile(root);
      s.project = root + "/state/smoke.sqlite";
      s.threads = 8;
      s.context = 4096;
      s.maxTokens = 256;
      s.quality = "fast";
      Progress p;
      return smokeTest(s, p);
    }
    auto value = [&](const QString &name, const QString &fallback = QString()) {
      int i = args.indexOf(name);
      return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    if (args.contains("--init") || args.contains("--run") ||
        args.contains("--export")) {
      Settings s;
      s.source = value("--book");
      s.game = value("--game", "fable2");
      require(s.game == "fable2" || s.game == "fable3",
              "--game: fable2 або fable3");
      s.project = value("--project", root + "/state/project.sqlite");
      s.model =
          value("--model", root + "/models/translategemma-4b-Q4_K_M.gguf");
      s.sourceLang = value("--from", "en");
      s.targetLang = value("--to", "uk");
      s.quality = value("--quality", "balanced");
      s.threads = value("--threads", "8").toInt();
      s.context = value("--context", "8192").toInt();
      s.maxTokens = value("--max-tokens", "4096").toInt();
      s.limit = value("--limit", "10").toInt();
      s.errorsOnly = args.contains("--retry-errors");
      s.layers = args.contains("--cuda") ? 99 : 0;
      s.engine = engineFile(root, s.layers != 0);
      s.glossary = value("--glossary");
      require(!s.source.isEmpty(), "Потрібен --book /path/book.babel");
      if (args.contains("--seed"))
        importSeed(s, value("--seed"));
      if (args.contains("--run")) {
        Progress p;
        translateJob(s, p);
        std::cout << p.message.toStdString() << std::endl;
      }
      if (args.contains("--export")) {
        QStringList fontFiles;
        for (int i = 0; i + 1 < args.size(); i++)
          if (args[i] == "--font")
            fontFiles << args[i + 1];
        exportJob(s, value("--export"), fontFiles,
                  value("--ttf", root + "/fonts/NotoSans-Regular.ttf"));
      }
      auto r = rows(s);
      int translated = 0, errors = 0;
      for (auto &e : r) {
        translated += e.status == "manual" || e.status == "draft";
        errors += e.status == "error";
      }
      std::cout << QJsonDocument(QJsonObject{{"entries", int(r.size())},
                                             {"translated", translated},
                                             {"errors", errors}})
                       .toJson()
                       .toStdString();
      return 0;
    }
    Window window(value("--test-root", root));
    if (args.contains("--game-test")) {
      window.testGames();
      window.show();
      app.processEvents();
      window.grab().save(value("--game-test"));
      return 0;
    }
    if (args.contains("--table-test")) {
      window.testTable();
      window.show();
      app.processEvents();
      window.grab().save(value("--table-test"));
      return 0;
    }
    if (args.contains("--ui-test")) {
      window.testEditor();
      window.show();
      app.processEvents();
      window.grab().save(value("--ui-test"));
      return 0;
    }
    window.show();
    if (args.contains("--screenshot")) {
      QString path = value("--screenshot");
      QTimer::singleShot(500, [&window, path, &app] {
        window.grab().save(path);
        app.quit();
      });
    }

    return app.exec();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
