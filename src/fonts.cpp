#include "core.hpp"
#include <cmath>
#include <cstring>
namespace fls {
static int tiled(int x, int y, int width) {
  int aw = (width + 31) & ~31;
  int macro = ((x >> 5) + (y >> 5) * (aw >> 5)) << 10;
  int micro = ((x & 7) + ((y & 6) << 2)) << 3;
  int o = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << 6) +
          ((y & 1) << 4);
  return ((o & ~511) << 3) + ((o & 448) << 2) + (o & 63) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}
static QVector<int> palette(int a, int b) {
  QVector<int> p{a, b};
  if (a > b)
    for (int i = 1; i <= 6; i++)
      p << ((7 - i) * a + i * b) / 7;
  else {
    for (int i = 1; i <= 4; i++)
      p << ((5 - i) * a + i * b) / 5;
    p << 0 << 255;
  }
  return p;
}
static QByteArray unswap(QByteArray b) {
  for (int i = 0; i < 8; i += 2)
    std::swap(b[i], b[i + 1]);
  return b;
}
Font Font::load(const QString &p) {
  Font f;
  f.data = readFile(p);
  f.le = f.data.left(4) != QByteArray::fromHex("00000001");
  f.version = u32(f.data, 0, f.le);
  require(f.version == 1 || f.version == 3, "Непідтримувана версія BFT");
  require(f.le || f.version == 1, "Невідома консольна версія BFT");
  f.count = u32(f.data, 21, f.le);
  f.stride = f.version == 3 ? 28 : 22;
  require(f.count > 0 && f.count < 65535, "Неприпустима кількість гліфів");
  int pos = 25 + f.count * f.stride;
  f.mapCount = u32(f.data, pos, f.le);
  require(f.mapCount > 0 && f.mapCount <= 65536 &&
              quint64(pos) + 4 + 2ull * f.mapCount <= quint64(f.data.size()),
          "Пошкоджена таблиця BFT");
  f.header = f.data.left(25);
  f.records = f.data.mid(25, f.count * f.stride);
  f.map = f.data.mid(pos + 4, f.mapCount * 2);
  f.texture = f.data.mid(pos + 4 + f.mapCount * 2);
  if (f.le) {
    require(u32(f.texture, 0, true) == 0xabcbbbf3 &&
                u32(f.texture, 4, true) == 4,
            "Невідома PC-текстура BFT");
    f.width = u32(f.texture, 20, true);
    f.height = u32(f.texture, 24, true);
    f.payload = u32(f.texture, 44, true);
    require(f.payload == 92 &&
                quint64(f.width) * f.height * 4 == u32(f.texture, 8, true) &&
                f.texture.size() == f.payload + qint64(f.width) * f.height * 4,
            "Непідтримувана PC-текстура: очікується RGBA8");
  } else {
    require(u32(f.texture, 0) == 0xfffffffe && u32(f.texture, 80) == 13,
            "Невідомий Xbox 360 атлас");
    f.width = u32(f.texture, 16);
    f.height = u32(f.texture, 20);
    f.payload = 88;
    require(f.texture.size() == 88 + qint64(f.width) * f.height / 2 &&
                u32(f.texture, 84) == quint32(f.width * f.height / 2),
            "Непідтримуваний Xbox BC4 атлас");
  }
  require(f.width > 0 && f.height > 0 && f.width <= 4096 && f.height <= 4096,
          "Некоректні розміри атласу");
  return f;
}
int Font::glyph(QChar c) const {
  return c.unicode() < mapCount ? u16(map, c.unicode() * 2, le) : 65535;
}
QString Font::missing(const QString &s) const {
  QString r;
  for (auto c : s)
    if (!c.isSpace() && !c.isLowSurrogate() && glyph(c) >= count &&
        !r.contains(c))
      r += c;
  return r;
}
QImage Font::atlas() const {
  if (le)
    return QImage((const uchar *)texture.constData() + payload, width, height,
                  width * 4, QImage::Format_RGBA8888)
        .copy()
        .convertToFormat(QImage::Format_ARGB32);
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  for (int y = 0; y < height / 4; y++)
    for (int x = 0; x < width / 4; x++) {
      int off = tiled(x, y, width / 4);
      require(off + 8 <= texture.size() - payload,
              "Текстура Xbox має непідтримуване вирівнювання");
      auto b = unswap(texture.mid(payload + off, 8));
      auto p = palette((uchar)b[0], (uchar)b[1]);
      quint64 bits = 0;
      for (int i = 0; i < 6; i++)
        bits |= quint64((uchar)b[i + 2]) << (8 * i);
      for (int k = 0; k < 16; k++)
        image.setPixel(x * 4 + k % 4, y * 4 + k / 4,
                       qRgba(255, 255, 255, p[(bits >> (3 * k)) & 7]));
    }
  return image;
}
static QByteArray bc4(const QImage &i, int x, int y) {
  int mn = 255, mx = 0;
  QVector<int> a;
  for (int k = 0; k < 16; k++) {
    int v = qAlpha(i.pixel(x + k % 4, y + k / 4));
    a << v;
    mn = qMin(mn, v);
    mx = qMax(mx, v);
  }
  auto p = palette(mx, mn);
  quint64 bits = 0;
  for (int k = 0; k < 16; k++) {
    int index = 0, d = 1000;
    for (int n = 0; n < 8; n++)
      if (qAbs(p[n] - a[k]) < d) {
        d = qAbs(p[n] - a[k]);
        index = n;
      }
    bits |= quint64(index) << (k * 3);
  }
  QByteArray b(8, 0);
  b[0] = char(mx);
  b[1] = char(mn);
  for (int j = 0; j < 6; j++)
    b[j + 2] = char(bits >> (j * 8));
  return unswap(b);
}
static QImage sdf(const QImage &alpha) {
  int w = alpha.width(), h = alpha.height();
  QVector<QPoint> edges;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      bool inside = qAlpha(alpha.pixel(x, y)) >= 128;
      bool border = false;
      for (auto d :
           {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
        int xx = x + d.x(), yy = y + d.y();
        if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
            (qAlpha(alpha.pixel(xx, yy)) >= 128) != inside)
          border = true;
      }
      if (border)
        edges << QPoint(x, y);
    }
  QImage out(w, h, QImage::Format_ARGB32);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      double distance = 8;
      for (auto &e : edges)
        distance =
            qMin(distance, std::hypot(double(x - e.x()), double(y - e.y())));
      bool inside = qAlpha(alpha.pixel(x, y)) >= 128;
      int v = qBound(0, int(128 + (inside ? -1 : 1) * distance * 16), 255);
      out.setPixel(x, y, qRgba(v, v, v, v));
    }
  return out;
}
QByteArray Font::add(const QString &ttf, const QString &chars,
                     int *added) const {
  auto needed = missing(chars);
  if (added)
    *added = 0;
  if (needed.isEmpty())
    return data;
  require(!ttf.isEmpty(), "Для відсутніх символів потрібен TTF/OTF: " + needed);
  QRawFont rf(ttf, double(u32(header, 4, le)), QFont::PreferFullHinting);
  require(rf.isValid(), "Не вдалося відкрити TTF/OTF");
  int ai = glyph('A');
  require(ai < count, "Шрифт не містить A для калібрування");
  int ao = ai * stride;
  double refAdvance =
      version == 3 ? f32(records, ao + 24, le) : u16(records, ao + 20, le);
  auto ag = rf.glyphIndexesForString("A");
  auto av = rf.advancesForGlyphIndexes(ag);
  if (!av.isEmpty() && av[0].x() > 0)
    rf.setPixelSize(rf.pixelSize() * refAdvance / av[0].x());
  auto image = atlas();
  int H = height, W = width;
  QBitArray used(W * H, false);
  for (int n = 0; n < count; n++) {
    int o = n * stride;
    double x0 = f32(records, o, le) * W, y0 = f32(records, o + 4, le) * H,
           x1 = f32(records, o + 8, le) * W, y1 = f32(records, o + 12, le) * H;
    int x = qBound(0, int(std::floor(qMin(x0, x1))) - 1, W),
        y = qBound(0, int(std::floor(qMin(y0, y1))) - 1, H),
        right = qBound(0, int(std::ceil(qMax(x0, x1))) + 1, W),
        bottom = qBound(0, int(std::ceil(qMax(y0, y1))) + 1, H);
    for (int yy = y; yy < bottom; yy++)
      for (int xx = x; xx < right; xx++)
        used.setBit(yy * W + xx);
  }
  QByteArray rec = records, mp = map;
  int N = count, MC = mapCount;
  QSet<int> touched;
  QVector<QPair<int, QRect>> placements;
  for (QChar ch : needed) {
    require(!ch.isSurrogate(), "BFT не підтримує нові символи поза BMP");
    auto gs = rf.glyphIndexesForString(QString(ch));
    require(gs.size() == 1 && gs[0] != 0,
            "У TTF немає символу: " + QString(ch));
    auto bbox = rf.boundingRect(gs[0]);
    auto adv = rf.advancesForGlyphIndexes(gs)[0].x();
    int cellH = qRound(
        qAbs(f32(records, ao + 12, le) - f32(records, ao + 4, le)) * height);
    int cw = version == 3 ? cellH : qCeil(qMax(bbox.width(), adv)) + 2;
    int cellW = ((cw + 3) / 4) * 4;
    cellH = ((cellH + 3) / 4) * 4;
    require(cellW > 0 && cellH > 0 && cellW <= W, "Завеликий гліф");
    QPoint place(-1, -1);
    while (place.x() < 0) {
      for (int y = 0; y + cellH <= H && place.x() < 0; y += 4)
        for (int x = 0; x + cellW <= W; x += 4) {
          bool free = true;
          for (int yy = y; yy < y + cellH && free; yy++)
            for (int xx = x; xx < x + cellW; xx++)
              if (used.testBit(yy * W + xx)) {
                free = false;
                break;
              }
          if (free) {
            place = {x, y};
            break;
          }
        }
      if (place.x() < 0) {
        require(H < 4096, "В атласі Xbox 360 бракує місця. Імпортуйте "
                          "інший BFT або менший TTF");
        QImage bigger(W, H * 2, QImage::Format_ARGB32);
        bigger.fill(qRgba(255, 255, 255, 0));
        for (int y = 0; y < H; y++)
          memcpy(bigger.scanLine(y), image.constScanLine(y), W * 4);
        image = bigger;
        H *= 2;
        used.resize(W * H);
      }
    }
    for (int y = place.y(); y < place.y() + cellH; y++)
      for (int x = place.x(); x < place.x() + cellW; x++)
        used.setBit(y * W + x);
    QImage cell(cellW, cellH, QImage::Format_ARGB32);
    cell.fill(Qt::transparent);
    QPainter painter(&cell);
    QGlyphRun run;
    run.setRawFont(rf);
    run.setGlyphIndexes(gs);
    run.setPositions({QPointF(1 - qMin(0.0, bbox.left()), 0)});
    painter.setPen(Qt::white);
    double baseline =
        version == 3 ? cellH - 8 : qMin(cellH - 1, int(u32(header, 17, le)));
    painter.drawGlyphRun(QPointF(0, baseline), run);
    painter.end();
    if (version == 3)
      cell = sdf(cell);
    QPainter draw(&image);
    draw.setCompositionMode(QPainter::CompositionMode_Source);
    draw.drawImage(place, cell);
    draw.end();
    QByteArray record(stride, 0);
    putFloat(record, 0, float(place.x() + 0.0) / W, le);
    putFloat(record, 4, float(place.y()) / height, le);
    putFloat(record, 8, float(place.x() + cellW) / W, le);
    putFloat(record, 12, float(place.y() + cellH) / height, le);
    if (version == 3) {
      putFloat(record, 16, bbox.left(), le);
      putFloat(record, 20, bbox.width(), le);
      putFloat(record, 24, adv, le);
    } else {
      put16(record, 16, quint16(qint16(qFloor(bbox.left()))), le);
      put16(record, 18, cellW, le);
      put16(record, 20, qRound(adv), le);
    }
    rec += record;
    if (ch.unicode() >= MC) {
      int next = ch.unicode() + 1;
      mp.resize(next * 2);
      for (int c = MC; c < next; c++)
        put16(mp, c * 2, 65535, le);
      MC = next;
    }
    put16(mp, ch.unicode() * 2, N++, le);
    require(N < 65535, "Забагато гліфів");
    placements << qMakePair(ch.unicode(), QRect(place, QSize(cellW, cellH)));
    if (added)
      (*added)++;
  }
  if (H != height)
    for (int n = 0; n < N; n++) {
      int o = n * stride;
      putFloat(rec, o + 4, f32(rec, o + 4, le) * height / H, le);
      putFloat(rec, o + 12, f32(rec, o + 12, le) * height / H, le);
    }
  auto tex = texture;
  if (le) {
    QByteArray payloadData;
    auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    payloadData =
        QByteArray((const char *)rgba.constBits(), rgba.sizeInBytes());
    tex = texture.left(payload);
    put32(tex, 8, payloadData.size(), true);
    put32(tex, 24, H, true);
    tex += payloadData;
  } else {
    if (H != height) {
      int newSize = W * H / 2;
      tex.resize(payload + newSize);
      memset(tex.data() + texture.size(), 0, tex.size() - texture.size());
      put32(tex, 4, quint32(newSize) | 4u);
      put32(tex, 20, H);
      put32(tex, 84, newSize);
    }
    for (auto &pl : placements) {
      auto r = pl.second;
      for (int y = r.top() / 4; y < (r.y() + r.height()) / 4; y++)
        for (int x = r.left() / 4; x < (r.x() + r.width()) / 4; x++) {
          int off = payload + tiled(x, y, W / 4);
          auto encoded = bc4(image, x * 4, y * 4);
          require(off + 8 <= tex.size(), "BC4 за межами текстури");
          memcpy(tex.data() + off, encoded.constData(), 8);
        }
    }
  }
  auto hdr = header;
  put32(hdr, 21, N, le);
  QByteArray mc(4, 0);
  put32(mc, 0, MC, le);
  return hdr + rec + mc + mp + tex;
}
} // namespace fls
