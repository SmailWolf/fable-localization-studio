#include "core.hpp"
namespace fls {
int selfTest(const QString &root) {
  int passed = 0;
  QJsonArray checks;
  auto ok = [&](bool v, const QString &name) {
    require(v, "TEST FAILED: " + name);
    passed++;
    checks.append(name);
    fprintf(stderr, "PASS %s\n", name.toUtf8().constData());
  };
  auto rejects = [&](auto fn, const QString &name) {
    bool failed = false;
    try {
      fn();
    } catch (const std::exception &) {
      failed = true;
    }
    ok(failed, name);
  };
  QString text = "Press <a_img> for %1 gold.\nKilled %1% of beetles.";
  auto p = protect(text, {});
  auto t = restore(p.masked, p, text);
  ok(t == text, "Protected markup/printf/percent/newlines roundtrip");
  rejects([&] { validate(text, QString(text).replace("%1", "%2")); },
          "Placeholder corruption rejected");
  rejects([&] { validate("A\nB", "AB"); }, "Removed line break rejected");
  p = protect("Rose holds <a_img> and %1", {{"Rose", "Роуз"}});
  ok(restore(p.masked, p, "Rose holds <a_img> and %1").startsWith("Роуз"),
     "Glossary and tags restored independently");
  rejects(
      [&] {
        restore(p.masked + p.restore[0].first, p, "Rose holds <a_img> and %1");
      },
      "Duplicate protected marker rejected");
  ok(tokens("Health falls to 75% of your maximum.").isEmpty(),
     "Literal numeric percentages are not printf placeholders");
  ok(tokens("%d %+03d %1 %1% %name% <sighs>").size() == 6,
     "Real printf, numbered placeholders, tags remain protected");
  ok(tokens("2%d").size() == 1,
     "Printf after a literal number stays protected");
  rejects([&] { validate("Cost x%d <sighs>", "Ціна x%d"); },
          "Manual edits cannot remove game tags");
  for (auto path : {qEnvironmentVariable("FABLE_TEST_FABLE2_BOOK"),
                    qEnvironmentVariable("FABLE_TEST_FABLE3_BOOK")})
    if (QFileInfo::exists(path)) {
      auto b = Babel::load(path);
      ok(b.build({}) == b.original, "Byte-identical BABEL roundtrip: " + path);
      QHash<quint32, QString> changes;
      for (int n = 0; n < qMin(25, int(b.entries.size())); n++)
        changes[b.entries[n].id] = b.entries[n].source + " Її Єє Ґґ Іі ʼ";
      auto rebuilt = b.build(changes);
      auto check = Babel::parse(rebuilt);
      ok(check.entries.size() == b.entries.size() && check.tail == b.tail,
         "All IDs/offsets/tail verified on changed BABEL");
      rejects([&] { Babel::parse(b.original.left(80)); },
              "Truncated BABEL rejected");
    }
  QStringList files =
      qEnvironmentVariable("FABLE_TEST_FONTS").split(';', Qt::SkipEmptyParts);
  QString alphabet =
      QString::fromUtf8("АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопр"
                        "стуфхцчшщьюяʼ Љљ Њњ Ћћ Ђђ Ѓѓ Ќќ Ўў");
  int index = 0;
  for (auto &path : files)
    if (QFileInfo::exists(path)) {
      auto f = Font::load(path);
      ok(f.add("", "ABC") == f.data, "No-change BFT stays byte-identical");
      auto old = f.atlas();
      int added = 0;
      auto data =
          f.add(qEnvironmentVariable("FABLE_TEST_TTF",
                                     root + "/fonts/NotoSans-Regular.ttf"),
                alphabet, &added);
      QString out =
          root + "/state/tests/font-" + QString::number(index++) + ".bft";
      writeFile(out, data);
      auto result = Font::load(out);
      ok(added > 0 && result.missing(alphabet).isEmpty(),
         "True Ukrainian/Serbian glyphs: " + path);
      auto image = result.atlas();
      bool same = true;
      for (int n = 0; n < f.count && same; n++) {
        int o = n * f.stride;
        int x0 = qFloor(
                qMin(f32(f.records, o, f.le), f32(f.records, o + 8, f.le)) *
                f.width),
            x1 = qCeil(
                qMax(f32(f.records, o, f.le), f32(f.records, o + 8, f.le)) *
                f.width),
            y0 = qFloor(f32(f.records, o + 4, f.le) * f.height),
            y1 = qCeil(f32(f.records, o + 12, f.le) * f.height);
        for (int y = qMax(0, y0); y < qMin(f.height, y1) && same; y++)
          for (int x = qMax(0, x0); x < qMin(f.width, x1); x++)
            if (old.pixel(x, y) != image.pixel(x, y)) {
              same = false;
              break;
            }
      }
      ok(same, "All original glyph pixels unchanged");
      image.save(root + "/state/tests/font-" + QString::number(index) + ".png");
    }
  writeFile(root + "/state/tests/report.json",
            QJsonDocument(QJsonObject{{"passed", passed},
                                      {"checks", checks},
                                      {"game_tested", false}})
                .toJson());
  return 0;
}
} // namespace fls
