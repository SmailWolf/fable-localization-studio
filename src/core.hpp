#pragma once
#include <QtCore>
#include <QtGui>
#include <QtNetwork>
#include <QtSql>
#include <atomic>
#include <cmath>
#include <functional>
#include <stdexcept>
namespace fls {
using Fail = std::runtime_error;
inline void require(bool ok, const QString &s) {
  if (!ok)
    throw Fail(s.toStdString());
}
QByteArray readFile(const QString &);
void writeFile(const QString &, const QByteArray &);
quint32 u32(const QByteArray &, int, bool = false);
quint16 u16(const QByteArray &, int, bool = false);
void put32(QByteArray &, int, quint32, bool = false);
void put16(QByteArray &, int, quint16, bool = false);
float f32(const QByteArray &, int, bool);
void putFloat(QByteArray &, int, float, bool);
QString sha(const QByteArray &);
QString fileSha(const QString &);
struct Entry {
  quint32 id, block, offset;
  QString source;
};
struct Block {
  quint32 id;
  QByteArray serialized, raw;
  QVector<QPair<int, QString>> strings;
};
struct Babel {
  QByteArray original, tail;
  QVector<Entry> entries;
  QVector<Block> blocks;
  static Babel load(const QString &);
  static Babel parse(const QByteArray &);
  QByteArray build(const QHash<quint32, QString> &) const;
};
QStringList tokens(const QString &);
bool technical(const QString &);
void validate(const QString &, const QString &);
struct Plan {
  QString masked;
  QVector<QPair<QString, QString>> restore;
};
Plan protect(const QString &, const QMap<QString, QString> &);
QString restore(const QString &, const Plan &, const QString &);
struct Font {
  QByteArray data, header, records, map, texture;
  bool le = false;
  int version = 0, count = 0, mapCount = 0, stride = 0, width = 0, height = 0,
      payload = 0;
  static Font load(const QString &);
  int glyph(QChar) const;
  QString missing(const QString &) const;
  QImage atlas() const;
  QByteArray add(const QString &ttf, const QString &chars,
                 int *added = nullptr) const;
};
struct Settings {
  bool errorsOnly = false;
  QString game = "fable2";
  QString source, project, model, engine, sourceLang = "en", targetLang = "uk",
                                          quality = "balanced", seed, glossary;
  int threads = 8, layers = 0, context = 4096, batch = 256, maxTokens = 1024,
      limit = 0;
};
struct Progress {
  QMutex mutex;
  QString message;
  int done = 0, total = 0, errors = 0;
  bool running = false;
  std::atomic_bool stop = false;
};
struct Row {
  quint32 id;
  QString source, target, status, error;
};
QString projectKey(const Settings &);
QVector<Row> rows(const Settings &);
void importSeed(const Settings &, const QString &);
void translateJob(const Settings &, Progress &);
void exportJob(const Settings &, const QString &folder,
               const QStringList &fonts, const QString &ttf);
int selfTest(const QString &root);
int smokeTest(const Settings &, Progress &);
} // namespace fls
