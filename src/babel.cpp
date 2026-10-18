#include "core.hpp"
#include <cstring>
#include <zlib.h>
namespace fls {
QByteArray readFile(const QString &p) {
  QFile f(p);
  require(f.open(QIODevice::ReadOnly), "Не вдалося прочитати: " + p);
  return f.readAll();
}
void writeFile(const QString &p, const QByteArray &d) {
  QDir().mkpath(QFileInfo(p).absolutePath());
  QSaveFile f(p);
  require(f.open(QIODevice::WriteOnly), "Не вдалося створити: " + p);
  require(f.write(d) == d.size() && f.commit(), "Помилка збереження: " + p);
}
quint32 u32(const QByteArray &d, int o, bool le) {
  require(o >= 0 && o + 4 <= d.size(), "Обрізані двійкові дані");
  auto *p = (const unsigned char *)d.constData() + o;
  return le ? quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) |
                  (quint32(p[3]) << 24)
            : (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
                  (quint32(p[2]) << 8) | p[3];
}
quint16 u16(const QByteArray &d, int o, bool le) {
  require(o >= 0 && o + 2 <= d.size(), "Обрізані дані");
  auto *p = (const unsigned char *)d.constData() + o;
  return le ? p[0] | (p[1] << 8) : (p[0] << 8) | p[1];
}
void put32(QByteArray &d, int o, quint32 v, bool le) {
  require(o >= 0 && o + 4 <= d.size(), "Запис за межі даних");
  for (int i = 0; i < 4; i++)
    d[o + i] = char(v >> (8 * (le ? i : 3 - i)));
}
void put16(QByteArray &d, int o, quint16 v, bool le) {
  require(o >= 0 && o + 2 <= d.size(), "Запис за межі даних");
  d[o] = char(v >> (le ? 0 : 8));
  d[o + 1] = char(v >> (le ? 8 : 0));
}
float f32(const QByteArray &d, int o, bool le) {
  quint32 v = u32(d, o, le);
  float f;
  memcpy(&f, &v, 4);
  require(std::isfinite(f), "Некоректна координата гліфа");
  return f;
}
void putFloat(QByteArray &d, int o, float f, bool le) {
  quint32 v;
  memcpy(&v, &f, 4);
  put32(d, o, v, le);
}
QString sha(const QByteArray &d) {
  return QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex();
}
QString fileSha(const QString &p) {
  QFile f(p);
  require(f.open(QIODevice::ReadOnly), "Відсутній файл: " + p);
  QCryptographicHash h(QCryptographicHash::Sha256);
  require(h.addData(&f), "Не вдалося обчислити контрольну суму");
  return h.result().toHex();
}
static QByteArray inflateBlock(const QByteArray &d, int size) {
  require(size > 0 && size <= 64 * 1024 * 1024, "Неприпустимий розмір блока");
  QByteArray out(size + 1, 0);
  z_stream z{};
  require(inflateInit(&z) == Z_OK, "zlib init");
  z.next_in = (Bytef *)d.constData();
  z.avail_in = d.size();
  z.next_out = (Bytef *)out.data();
  z.avail_out = out.size();
  int rc = inflate(&z, Z_SYNC_FLUSH);
  bool ok = (rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR) &&
            z.total_out == quint64(size) && z.avail_in == 0;
  inflateEnd(&z);
  require(ok, "Не вдалося розпакувати блок BABEL");
  out.resize(size);
  return out;
}
static QByteArray deflateBlock(const QByteArray &d) {
  z_stream z{};
  require(deflateInit(&z, 9) == Z_OK, "zlib init");
  QByteArray out(compressBound(d.size()) + 32, 0);
  z.next_in = (Bytef *)d.constData();
  z.avail_in = d.size();
  z.next_out = (Bytef *)out.data();
  z.avail_out = out.size();
  int rc = deflate(&z, Z_SYNC_FLUSH);
  bool ok = rc == Z_OK && z.avail_in == 0;
  auto n = z.total_out;
  deflateEnd(&z);
  require(ok, "Не вдалося стиснути блок");
  out.resize(n);
  return out;
}
static QString utf16(const QByteArray &d, int off, int units) {
  QString s;
  for (int i = 0; i < units; i++)
    s += QChar(u16(d, off + i * 2));
  require(s.isValidUtf16(), "Пошкоджений UTF-16");
  return s;
}
Babel Babel::load(const QString &p) { return parse(readFile(p)); }
Babel Babel::parse(const QByteArray &data) {
  Babel b;
  b.original = data;
  require(data.left(4) == QByteArray::fromHex("5b010000"),
          "Невідома версія BABEL (очікується Fable II/III 0x15B)");
  auto n = u32(data, 4);
  require(n < 1000000 && 8ull + n * 12ull + 4 <= quint64(data.size()),
          "Пошкоджений індекс BABEL");
  int pos = 8 + n * 12;
  quint32 nb = u32(data, pos);
  pos += 4;
  require(nb < 100000, "Забагато блоків");
  QSet<quint32> bids, ids;
  QHash<quint64, QString> lookup;
  for (quint32 i = 0; i < nb; i++) {
    int start = pos;
    Block bl;
    bl.id = u32(data, pos);
    auto c = u32(data, pos + 4), u = u32(data, pos + 8);
    pos += 12;
    require(!bids.contains(bl.id) && c <= quint32(data.size() - pos),
            "Пошкоджений блок BABEL");
    bids.insert(bl.id);
    bl.raw = inflateBlock(data.mid(pos, c), u);
    pos += c;
    bl.serialized = data.mid(start, pos - start);
    int off = 0;
    while (off < bl.raw.size()) {
      quint32 units = u32(bl.raw, off);
      require(units > 0 && units <= quint32((bl.raw.size() - off - 4) / 2),
              "Пошкоджена довжина рядка");
      QString text = utf16(bl.raw, off + 4, units);
      require(text.endsWith(QChar(0)), "Рядок без завершення");
      text.chop(1);
      require(!text.contains(QChar(0)), "Вбудований NUL");
      bl.strings.append({off, text});
      lookup.insert((quint64(bl.id) << 32) | quint32(off), text);
      off += 4 + units * 2;
    }
    b.blocks.append(bl);
  }
  b.tail = data.mid(pos);
  for (quint32 i = 0; i < n; i++) {
    int o = 8 + 12 * i;
    Entry e{u32(data, o), u32(data, o + 4), u32(data, o + 8), {}};
    require(!ids.contains(e.id), "Дубль ідентифікатора");
    ids.insert(e.id);
    quint64 key = (quint64(e.block) << 32) | e.offset;
    require(lookup.contains(key), "Невідоме посилання на текст");
    e.source = lookup.value(key);
    b.entries.append(e);
  }
  return b;
}
QByteArray Babel::build(const QHash<quint32, QString> &changes) const {
  QHash<quint64, QString> replacements;
  QHash<quint64, quint32> offsets;
  for (auto &e : entries)
    if (changes.contains(e.id)) {
      auto text = changes.value(e.id);
      validate(e.source, text);
      auto key = (quint64(e.block) << 32) | e.offset;
      require(!replacements.contains(key) || replacements.value(key) == text,
              "Конфлікт спільного рядка");
      replacements.insert(key, text);
    }
  QByteArray stream;
  for (auto &bl : blocks) {
    QByteArray raw;
    bool changed = false;
    for (auto &s : bl.strings) {
      quint64 key = (quint64(bl.id) << 32) | quint32(s.first);
      offsets.insert(key, raw.size());
      auto text = replacements.value(key, s.second);
      changed |= text != s.second;
      int off = raw.size();
      raw.append(QByteArray(4 + 2 * (text.size() + 1), 0));
      put32(raw, off, text.size() + 1);
      for (int i = 0; i < text.size(); i++)
        put16(raw, off + 4 + 2 * i, text.at(i).unicode());
    }
    if (!changed) {
      stream += bl.serialized;
      continue;
    }
    auto comp = deflateBlock(raw);
    QByteArray hdr(12, 0);
    put32(hdr, 0, bl.id);
    put32(hdr, 4, comp.size());
    put32(hdr, 8, raw.size());
    stream += hdr + comp;
  }
  QByteArray out = original.left(8);
  for (auto &e : entries) {
    QByteArray item(12, 0);
    put32(item, 0, e.id);
    put32(item, 4, e.block);
    put32(item, 8, offsets.value((quint64(e.block) << 32) | e.offset));
    out += item;
  }
  QByteArray count(4, 0);
  put32(count, 0, blocks.size());
  out += count + stream + tail;
  auto check = parse(out);
  require(check.entries.size() == entries.size() && check.tail == tail,
          "Перевірка складання не пройдена");
  for (int i = 0; i < entries.size(); i++)
    require(check.entries[i].id == entries[i].id &&
                check.entries[i].source ==
                    changes.value(entries[i].id, entries[i].source),
            "Текст пошкоджено під час складання");
  return out;
}
static QRegularExpression rx() {
  static const QRegularExpression re(
      R"(<[^>]*>|%[A-Za-z_][A-Za-z0-9_]*%|%\d+%|(?<!\d)%(?:\d+\$)?[-+ #0]*\d*(?:\.\d+)?(?:ll|[hlL])?[diuoxXfFeEgGcs]|%(?:\d+\$)?[-+#0]*\d*(?:\.\d+)?(?:ll|[hlL])?[diuoxXfFeEgGcs]|%\d+)");
  return re;
}
QStringList tokens(const QString &s) {
  QStringList r;
  auto it = rx().globalMatch(s);
  while (it.hasNext())
    r << it.next().captured();
  r.sort();
  return r;
}
void validate(const QString &s, const QString &t) {
  require(t.isValidUtf16() && !t.contains(QChar(0)),
          "Некоректний текст перекладу");
  require(s.trimmed().isEmpty() || !t.trimmed().isEmpty(), "Порожній переклад");
  require(tokens(s) == tokens(t), "Змінено теги або підстановки");
  require(s.count('\n') == t.count('\n'), "Змінено кількість переносів рядків");
}
bool technical(const QString &s) {
  auto t = s;
  t.remove(rx());
  static const QRegularExpression letters("\\p{L}"),
      asset("^[A-Z][A-Z0-9]*_[A-Z0-9_]+$");
  return !t.contains(letters) || asset.match(s).hasMatch();
}
Plan protect(const QString &s, const QMap<QString, QString> &gloss) {
  Plan p;
  QString pattern = rx().pattern();
  QStringList terms;
  for (auto it = gloss.begin(); it != gloss.end(); ++it)
    if (!it.key().isEmpty())
      terms << it.key();
  std::sort(terms.begin(), terms.end(),
            [](auto &a, auto &b) { return a.size() > b.size(); });
  for (auto &t : terms)
    pattern += "|(?<![\\p{L}\\p{N}_])" + QRegularExpression::escape(t) +
               "(?![\\p{L}\\p{N}_])";
  auto it = QRegularExpression(pattern).globalMatch(s);
  int pos = 0;
  while (it.hasNext()) {
    auto m = it.next();
    auto marker = QString("⟦F%1⟧").arg(p.restore.size(), 4, 10, QChar('0'));
    require(!s.contains(marker), "Конфлікт захищеної мітки");
    p.masked += s.mid(pos, m.capturedStart() - pos) + marker;
    p.restore.append({marker, gloss.value(m.captured(), m.captured())});
    pos = m.capturedEnd();
  }
  p.masked += s.mid(pos);
  return p;
}
QString restore(const QString &out, const Plan &p, const QString &source) {
  auto t = out.trimmed();
  for (auto &item : p.restore) {
    require(t.count(item.first) == 1, "Модель змінила захищену мітку");
    t.replace(item.first, item.second);
  }
  require(!t.contains(QRegularExpression("⟦F\\d+⟧")), "Зайва захищена мітка");
  auto lead = QRegularExpression("^\\s*").match(source).captured(),
       end = QRegularExpression("\\s*$").match(source).captured();
  t = lead + t + end;
  validate(source, t);
  return t;
}
} // namespace fls
