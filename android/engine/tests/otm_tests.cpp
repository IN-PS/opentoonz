// OpenToonz Mobile engine tests (host build).  Exercises the same engine code
// that is compiled into libotmengine.so for Android arm64.
//
// usage: otm_tests <output-dir>
#include "engine.h"
#include "gifencoder.h"
#include "projectio.h"

#include "timage_io.h"
#include "tlevel_io.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "tpixelcm.h"
#include "tsystem.h"
#include "tundo.h"

#include <QCoreApplication>
#include <QDir>

#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace otm;

static int g_fail = 0, g_pass = 0;
static std::string g_out;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (c)                                                               \
      g_pass++;                                                          \
    else {                                                               \
      g_fail++;                                                          \
      fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
    }                                                                    \
  } while (0)

static double nowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch())
      .count();
}

static long rssKB() {
  std::ifstream f("/proc/self/status");
  std::string l;
  while (std::getline(f, l))
    if (l.rfind("VmRSS:", 0) == 0) return std::stol(l.substr(6));
  return -1;
}

// canvas-space RGBA render of a frame
struct Img {
  int w, h;
  std::vector<uint8_t> px;
  const uint8_t *at(int x, int y) const { return &px[(y * w + x) * 4]; }
};
static Img render(Engine &e, int row, bool onion = false) {
  Img im{e.doc()->width(), e.doc()->height(), {}};
  im.px.resize(im.w * im.h * 4);
  e.renderRGBA(row, im.px.data(), im.w * 4, 0, 0, im.w - 1, im.h - 1, onion);
  return im;
}
static bool isWhite(const uint8_t *p) {
  return p[0] > 245 && p[1] > 245 && p[2] > 245;
}
static bool isDark(const uint8_t *p) {
  return p[0] < 60 && p[1] < 60 && p[2] < 60;
}
static void savePng(const Img &im, const std::string &name) {
  TRaster32P r(im.w, im.h);
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      const uint8_t *s = im.at(x, y);
      r->pixels(im.h - 1 - y)[x] = TPixel32(s[0], s[1], s[2], 255);
    }
  TImageWriter::save(TFilePath(g_out + "/" + name), TRasterImageP(r));
}

static void stroke(Engine &e, std::vector<std::pair<double, double>> pts,
                   double pressure = 1.0) {
  e.pointerDown(pts[0].first, pts[0].second, pressure);
  for (size_t i = 1; i < pts.size(); i++)
    e.pointerMove(pts[i].first, pts[i].second, pressure);
  e.pointerUp(pts.back().first, pts.back().second);
}
static std::vector<std::pair<double, double>> circle(double cx, double cy,
                                                     double r, int n = 64) {
  std::vector<std::pair<double, double>> v;
  for (int i = 0; i <= n; i++) {
    double a = 2 * M_PI * i / n;
    v.push_back({cx + r * cos(a), cy + r * sin(a)});
  }
  return v;
}
static std::vector<std::pair<double, double>> line(double x0, double y0,
                                                   double x1, double y1,
                                                   int n = 20) {
  std::vector<std::pair<double, double>> v;
  for (int i = 0; i <= n; i++)
    v.push_back({x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n});
  return v;
}

static void section(const char *name) { printf("[%s]\n", name); }

// --------------------------------------------------------------------------
static void testBrushAndRender(Engine &e) {
  section("brush stroke on Toonz Raster (RasterStrokeGenerator)");
  e.newDocument(640, 360, 12);
  e.tool                 = Tool::Brush;
  e.settings.brushMax    = 8;
  e.doc()->currentStyle  = 1;  // black
  Img before = render(e, 0);
  CHECK(isWhite(before.at(100, 100)));
  stroke(e, line(50, 100, 300, 100));
  Img after = render(e, 0);
  CHECK(isDark(after.at(100, 100)));
  CHECK(isDark(after.at(299, 100)));
  CHECK(isWhite(after.at(100, 120)));  // thickness 8 -> y=120 untouched
  // the drawing really is CM32 with ink #1
  TToonzImageP ti = e.doc()->image(e.doc()->layer(0), 0);
  CHECK(ti);
  TPixelCM32 pix = ti->getRaster()->pixels(359 - 100)[100];
  CHECK(pix.getInk() == 1 && pix.getTone() < 50);
  savePng(after, "01_brush.png");
}

static void testFramesAndOnion(Engine &e) {
  section("multi-frame animation + onion skin");
  e.newDocument(640, 360, 12);
  e.tool = Tool::Brush;
  e.settings.brushMax = 6;
  for (int f = 0; f < 6; f++) {
    if (f > 0) e.insertBlankFrame();
    CHECK(e.doc()->currentRow == f);
    stroke(e, circle(100 + f * 80, 180, 30));
  }
  CHECK(e.doc()->frameCount() == 6);
  Layer *l = e.doc()->layer(0);
  CHECK(l->drawings.size() == 6);
  // each frame shows its own circle only
  Img f3 = render(e, 3, false);
  CHECK(isDark(f3.at(100 + 3 * 80 + 30, 180)));
  CHECK(isWhite(f3.at(100 + 2 * 80 + 30, 180)));
  // onion: previous drawings tinted red, next tinted green
  e.doc()->onion.enabled = true;
  e.doc()->onion.back    = 2;
  e.doc()->onion.forward = 1;
  e.setCurrentRow(3);
  Img o3 = render(e, 3, true);
  const uint8_t *prev = o3.at(100 + 2 * 80 + 30, 180);
  const uint8_t *next = o3.at(100 + 4 * 80 + 30, 180);
  const uint8_t *far  = o3.at(100 + 0 * 80 + 30, 180);  // 3 back: not shown
  printf("  onion prev=(%d,%d,%d) next=(%d,%d,%d)\n", prev[0], prev[1], prev[2],
         next[0], next[1], next[2]);
  CHECK(prev[0] > prev[1] + 40 && prev[0] > prev[2] + 40);  // reddish
  CHECK(next[1] > next[0] + 20);                              // greenish
  CHECK(isWhite(far));
  CHECK(isDark(o3.at(100 + 3 * 80 + 30, 180)));  // current not tinted
  savePng(o3, "02_onion_frame4.png");
  // hold + duplicate + delete
  e.setCurrentRow(5);
  e.holdFrame();
  CHECK(e.doc()->frameCount() == 7 && l->cell(6) == l->cell(5));
  e.duplicateFrame();
  CHECK(e.doc()->frameCount() == 8 && l->cell(7) != l->cell(6));
  e.deleteFrame();
  CHECK(e.doc()->frameCount() == 7);
  // onion skips holds: at row 6 (hold of 5) back onion is row 4's drawing
  auto rows = Compositor::onionRows(*e.doc(), l, 6);
  CHECK(!rows.empty() && rows[0].first == -1 && l->cell(rows[0].second) == l->cell(4));
}

static void testLayers(Engine &e) {
  section("layers: order, visibility, opacity, lock");
  e.newDocument(400, 300, 12);
  e.tool = Tool::Brush;
  e.settings.brushMax = 20;
  e.doc()->currentStyle = 1;
  stroke(e, line(50, 150, 350, 150));  // black, layer 0
  int l1 = e.addLayer();
  CHECK(e.doc()->layerCount() == 2 && l1 == 1);
  e.doc()->currentStyle = 3;  // red
  stroke(e, line(200, 50, 200, 250));  // red vertical over black
  Img a = render(e, 0);
  const uint8_t *c = a.at(200, 150);
  CHECK(c[0] > 180 && c[1] < 80);  // red is on top
  e.moveLayer(1, 0);               // red below black
  Img b = render(e, 0);
  CHECK(isDark(b.at(200, 150)));
  e.undo();  // undo move
  CHECK(e.doc()->layer(1)->uid != e.doc()->layer(0)->uid);
  Img a2 = render(e, 0);
  CHECK(a2.at(200, 150)[0] > 180);
  e.setLayerVisible(1, false);
  CHECK(isDark(render(e, 0).at(200, 150)));
  CHECK(isWhite(render(e, 0).at(200, 60)));
  e.setLayerVisible(1, true);
  e.setLayerOpacity(1, 128);
  const uint8_t *h = render(e, 0).at(200, 60);  // red @50% over white
  printf("  50%% red over white = (%d,%d,%d)\n", h[0], h[1], h[2]);
  CHECK(h[0] > 200 && h[1] > 100 && h[1] < 160);
  e.setLayerOpacity(1, 255);
  e.setLayerLocked(1, true);
  e.doc()->currentLayer = 1;
  auto before = e.doc()->layer(1)->drawings.size();
  stroke(e, line(10, 10, 100, 10));
  CHECK(isWhite(render(e, 0).at(50, 10)));  // locked: nothing drawn
  CHECK(e.doc()->layer(1)->drawings.size() == before);
  e.setLayerLocked(1, false);
  int n = e.doc()->layerCount();
  e.deleteLayer(1);
  CHECK(e.doc()->layerCount() == n - 1);
  e.undo();
  CHECK(e.doc()->layerCount() == n);
  CHECK(render(e, 0).at(200, 60)[0] > 180);
  savePng(render(e, 0), "03_layers.png");
}

static void testFillEraser(Engine &e) {
  section("fill (toonzlib fill.cpp) + eraser + palette recolour");
  e.newDocument(400, 300, 12);
  e.tool = Tool::Brush;
  e.settings.brushMax = 4;
  e.doc()->currentStyle = 1;
  stroke(e, circle(200, 150, 80, 120));
  e.tool = Tool::Fill;
  e.doc()->currentStyle = 7;  // blue (default palette: 2 white,3 red,..,6 green,7 blue)
  Engine::DirtyRect d = e.pointerDown(200, 150, 1);
  e.pointerUp(200, 150);
  CHECK(!d.empty());
  TToonzImageP ti = e.doc()->image(e.doc()->layer(0), 0);
  TPixelCM32 in   = ti->getRaster()->pixels(299 - 150)[200];
  TPixelCM32 out  = ti->getRaster()->pixels(299 - 20)[20];
  CHECK(in.getPaint() == 7);
  CHECK(out.getPaint() == 0);  // outside the closed line is untouched
  Img f = render(e, 0);
  const uint8_t *p = f.at(200, 150);
  CHECK(p[2] > 180 && p[0] < 90);
  CHECK(isWhite(f.at(20, 20)));
  savePng(f, "04_fill.png");
  // palette recolour: change style 7 -> green, the fill follows (Toonz raster)
  e.setStyleColor(7, TPixel32(0, 200, 0));
  p = render(e, 0).at(200, 150);
  CHECK(p[1] > 180 && p[2] < 60);
  e.undo();
  CHECK(render(e, 0).at(200, 150)[2] > 180);
  // fill lines mode recolours the ink
  e.settings.fillMode = FillMode::Lines;
  e.doc()->currentStyle = 3;
  e.pointerDown(280, 150, 1);
  e.pointerUp(280, 150);
  TPixelCM32 ink = ti->getRaster()->pixels(299 - 150)[280];
  CHECK(ink.getInk() == 3);
  e.settings.fillMode = FillMode::Areas;
  // eraser removes lines and paint
  e.tool = Tool::Eraser;
  e.settings.eraserSize = 30;
  e.settings.eraseMode = EraseMode::LinesAndAreas;
  stroke(e, line(100, 150, 300, 150));
  ti = e.doc()->image(e.doc()->layer(0), 0);
  TPixelCM32 er = ti->getRaster()->pixels(299 - 150)[200];
  CHECK(er.getPaint() == 0 && er.getTone() == 255);
  CHECK(isWhite(render(e, 0).at(200, 150)));
  CHECK(!isWhite(render(e, 0).at(200, 120)));  // outside eraser path kept
  savePng(render(e, 0), "05_eraser.png");
  // undo eraser
  e.undo();
  CHECK(e.doc()->image(e.doc()->layer(0), 0) &&
        TToonzImageP(e.doc()->image(e.doc()->layer(0), 0))
                ->getRaster()
                ->pixels(299 - 150)[200]
                .getPaint() == 7);
}

static void testSelection(Engine &e) {
  section("selection + transform (TRop::resample / TRop::over on CM32)");
  e.newDocument(400, 300, 12);
  e.tool = Tool::Brush;
  e.settings.brushMax = 10;
  stroke(e, line(50, 50, 100, 50));
  CHECK(isDark(render(e, 0).at(75, 50)));
  CHECK(e.selectRect(40, 40, 110, 60));
  // floating selection is still rendered in place
  CHECK(isDark(render(e, 0).at(75, 50)));
  e.transformSelection(200, 100, 1.0, 0);
  Img m = render(e, 0);
  CHECK(isWhite(m.at(75, 50)));
  CHECK(isDark(m.at(275, 150)));
  e.commitSelection();
  Img c = render(e, 0);
  CHECK(isWhite(c.at(75, 50)));
  CHECK(isDark(c.at(275, 150)));
  savePng(c, "06_moved.png");
  // undo puts it back
  e.undo();
  Img u = render(e, 0);
  CHECK(isDark(u.at(75, 50)));
  CHECK(isWhite(u.at(275, 150)));
  // scale x2 + rotate 90
  CHECK(e.selectRect(40, 40, 110, 60));
  e.transformSelection(100, 100, 2.0, 90);
  e.commitSelection();
  Img s = render(e, 0);
  // rotated: now vertical line centred at (175,150), length ~100*... at x=175
  CHECK(isDark(s.at(175, 150)));
  CHECK(isDark(s.at(175, 110)));
  CHECK(isWhite(s.at(130, 150)));
  savePng(s, "07_scaled_rotated.png");
  // delete selection
  e.undo();
  CHECK(e.selectRect(40, 40, 110, 60));
  e.deleteSelection();
  CHECK(isWhite(render(e, 0).at(75, 50)));
  e.undo();
  CHECK(isDark(render(e, 0).at(75, 50)));
  // cancel
  CHECK(e.selectRect(40, 40, 110, 60));
  e.transformSelection(50, 0, 1, 0);
  e.cancelSelection();
  CHECK(isDark(render(e, 0).at(75, 50)));
}

static void testUndoRedo(Engine &e) {
  section("undo / redo (TUndoManager)");
  e.newDocument(300, 200, 12);
  e.tool = Tool::Brush;
  stroke(e, line(10, 100, 290, 100));
  CHECK(isDark(render(e, 0).at(150, 100)));
  CHECK(e.undo());
  CHECK(isWhite(render(e, 0).at(150, 100)));
  CHECK(e.redo());
  CHECK(isDark(render(e, 0).at(150, 100)));
  e.insertBlankFrame();
  CHECK(e.doc()->frameCount() == 2);
  e.undo();
  CHECK(e.doc()->frameCount() == 1);
  e.redo();
  CHECK(e.doc()->frameCount() == 2);
  // drawing on an empty cell creates a drawing; undo removes cell too
  e.setCurrentRow(4);
  stroke(e, line(10, 20, 100, 20));
  CHECK(e.doc()->frameCount() == 5 && e.doc()->layer(0)->cell(4) > 0);
  e.undo();
  CHECK(e.doc()->layer(0)->cell(4) == 0);
}

static void writeTestPng(const std::string &path, int w, int h) {
  TRaster32P r(w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      r->pixels(y)[x] = TPixel32(x * 255 / w, y * 255 / h, 128, 255);
  TImageWriter::save(TFilePath(path), TRasterImageP(r));
}

static void testImport(Engine &e) {
  section("import PNG + image sequence (raster layers)");
  e.newDocument(400, 300, 12);
  std::string p = g_out + "/in_gradient.png";
  writeTestPng(p, 200, 150);
  CHECK(e.importImageFile(p, true, 0));
  CHECK(e.doc()->layerCount() == 2);
  CHECK(e.doc()->layer(1)->type == LayerType::Raster);
  Img im = render(e, 0);
  const uint8_t *c = im.at(200, 150);  // centre of the fitted image
  printf("  imported centre = (%d,%d,%d)\n", c[0], c[1], c[2]);
  CHECK(std::abs(c[0] - 127) < 12 && std::abs(c[2] - 128) < 6);
  savePng(im, "08_import.png");
  // sequence: 3 files to 3 consecutive rows
  for (int i = 1; i <= 3; i++) writeTestPng(g_out + "/seq." + std::to_string(i) + ".png", 64 * i, 64);
  int base = e.doc()->layerCount();
  for (int i = 0; i < 3; i++)
    CHECK(e.importImageFile(g_out + "/seq." + std::to_string(i + 1) + ".png",
                            i == 0, i));
  CHECK(e.doc()->layerCount() == base + 1);
  CHECK(e.doc()->frameCount() == 3);
  // brush & eraser on a raster layer
  e.tool = Tool::Brush;
  e.doc()->currentStyle = 1;
  stroke(e, line(20, 280, 380, 280));
  CHECK(isDark(render(e, 0).at(200, 280)));
  e.tool = Tool::Eraser;
  e.settings.eraserSize = 40;
  stroke(e, line(150, 150, 250, 150));
  // erased part of the top raster layer reveals the layer below
  CHECK(e.doc()->layer(e.doc()->currentLayer)->type == LayerType::Raster);
  e.undo();
  e.undo();
}

// Equal up to premultiply/depremultiply rounding of PNG-stored raster layers.
static bool sameImg(const Img &a, const Img &b, int tol = 2) {
  if (a.w != b.w || a.h != b.h) return false;
  int maxd = 0, n = 0, fx = -1, fy = -1;
  for (size_t i = 0; i < a.px.size(); i++) {
    int d = std::abs((int)a.px[i] - (int)b.px[i]);
    if (d > tol) {
      if (n++ == 0) fx = (int)(i / 4) % a.w, fy = (int)(i / 4) / a.w;
    }
    maxd = std::max(maxd, d);
  }
  if (n) printf("  image diff: %d channels > %d (max %d), first at (%d,%d)\n", n, tol, maxd, fx, fy);
  return n == 0;
}

static void testSaveLoad(Engine &e) {
  section("save / load project (TLV + TPL + PNG + project.otm)");
  e.newDocument(320, 240, 8);
  e.tool = Tool::Brush;
  e.settings.brushMax = 5;
  for (int f = 0; f < 4; f++) {
    if (f) e.insertBlankFrame();
    stroke(e, circle(60 + f * 60, 120, 25));
    e.tool = Tool::Fill;
    e.doc()->currentStyle = 4 + f;
    e.pointerDown(60 + f * 60, 120, 1);
    e.pointerUp(60 + f * 60, 120);
    e.tool = Tool::Brush;
    e.doc()->currentStyle = 1;
  }
  e.holdFrame();
  std::string p = g_out + "/in_small.png";
  writeTestPng(p, 80, 60);
  e.setCurrentRow(0);
  e.importImageFile(p, true, 0);
  e.setLayerOpacity(1, 200);
  e.renameLayer(0, "Ink & Paint");
  e.addStyle(TPixel32(12, 34, 56));
  e.doc()->onion.enabled = true;
  int frames = e.doc()->frameCount();
  std::vector<Img> ref;
  for (int r = 0; r < frames; r++) ref.push_back(render(e, r));
  std::string dir = g_out + "/project1";
  std::string err;
  CHECK(e.save(dir, &err));
  if (!err.empty()) printf("  save error: %s\n", err.c_str());
  CHECK(QFile::exists(QString::fromStdString(dir + "/project.otm")));
  CHECK(QFile::exists(QString::fromStdString(dir + "/palette.tpl")));
  QDir lv(QString::fromStdString(dir + "/levels"));
  QStringList files = lv.entryList(QDir::Files);
  printf("  levels: %s\n", files.join(", ").toStdString().c_str());
  CHECK(files.filter(".tlv").size() == 1);
  CHECK(files.filter(".png").size() == 1);
  // independent check of the TLV with OpenToonz's own reader
  if (!files.filter(".tlv").isEmpty()) {
    TLevelReaderP lr(TFilePath(dir + "/levels/" +
                               files.filter(".tlv")[0].toStdString()));
    TLevelP level = lr->loadInfo();
    CHECK(level->getFrameCount() == 4);
  }
  // reload into a new engine state
  e.newDocument(100, 100, 12);
  CHECK(e.load(dir, &err));
  if (!err.empty()) printf("  load error: %s\n", err.c_str());
  CHECK(e.doc()->width() == 320 && e.doc()->height() == 240 &&
        e.doc()->fps() == 8);
  CHECK(e.doc()->frameCount() == frames);
  CHECK(e.doc()->layerCount() == 2);
  CHECK(e.doc()->layer(0)->name == "Ink & Paint");
  CHECK(e.doc()->layer(1)->opacity == 200);
  CHECK(e.doc()->onion.enabled);
  CHECK(e.styles().back().color == TPixel32(12, 34, 56));
  bool all = true;
  for (int r = 0; r < frames; r++) all = all && sameImg(ref[r], render(e, r));
  CHECK(all);
  // edit + re-save (autosave path) overwrites cleanly
  e.setCurrentRow(1);
  e.tool = Tool::Brush;
  stroke(e, line(0, 0, 319, 239));
  CHECK(e.doc()->dirty);
  CHECK(e.save(dir, &err));
  CHECK(!e.doc()->dirty);
  Img edited = render(e, 1);
  int editedLayer = e.doc()->currentLayer;
  int editedCell  = e.doc()->layer(editedLayer)->cell(1);
  e.newDocument(100, 100, 12);
  CHECK(e.load(dir, &err));
  bool same = sameImg(edited, render(e, 1));
  CHECK(same);
  if (!same) {
    printf("  DIAG edited layer %d cell %d -> reloaded cell %d, cur layer %d\n",
           editedLayer, editedCell, e.doc()->layer(editedLayer)->cell(1),
           e.doc()->currentLayer);
    QDir lv2(QString::fromStdString(dir + "/levels"));
    for (auto &f : lv2.entryInfoList(QDir::Files))
      printf("  DIAG file %s %lld\n", f.fileName().toStdString().c_str(),
             (long long)f.size());
  }
  // corrupted project is reported, not crashing
  {
    std::ofstream bad(g_out + "/badproj/project.otm");
  }
  QDir().mkpath(QString::fromStdString(g_out + "/badproj"));
  { std::ofstream bad(g_out + "/badproj/project.otm"); bad << "garbage"; }
  CHECK(!e.load(g_out + "/badproj", &err));
  printf("  bad project -> \"%s\"\n", err.c_str());
}

// The Android app saves on a background executor thread while the UI thread
// keeps using the engine; QProcess (LZO helpers for TLV) must work there too.
static void testWorkerThreadSave(Engine &e) {
  section("save/load from a worker thread (Android autosave path)");
  e.newDocument(320, 240, 12);
  e.tool = Tool::Brush;
  stroke(e, circle(160, 120, 50));
  e.insertBlankFrame();
  stroke(e, line(10, 10, 300, 200));
  std::string err1, err2;
  bool ok1 = false, ok2 = false;
  std::string dir = g_out + "/threadproj";
  std::thread t([&] { ok1 = e.save(dir, &err1); });
  // UI thread keeps drawing meanwhile (serialised by the engine mutex)
  stroke(e, line(10, 200, 300, 10));
  t.join();
  CHECK(ok1);
  if (!ok1) printf("  worker save error: %s\n", err1.c_str());
  std::thread t2([&] { ok2 = e.save(dir, &err2); });
  t2.join();
  CHECK(ok2);
  Img a = render(e, 1);
  e.newDocument(64, 64, 12);
  std::thread t3([&] { ok1 = e.load(dir, &err1); });
  t3.join();
  CHECK(ok1 && sameImg(a, render(e, 1)));
}

static void testExport(Engine &e) {
  section("export GIF + PNG sequence");
  e.newDocument(320, 180, 10);
  e.tool = Tool::Brush;
  e.settings.brushMax = 12;
  for (int f = 0; f < 8; f++) {
    if (f) e.insertBlankFrame();
    e.doc()->currentStyle = 2 + (f % 6);
    stroke(e, circle(40 + f * 30, 90, 20));
  }
  std::string err;
  std::string gif = g_out + "/anim.gif";
  double t0 = nowMs();
  CHECK(e.exportGif(gif, 0, &err));
  printf("  gif export: %.1f ms for %d frames\n", nowMs() - t0,
         e.doc()->frameCount());
  std::ifstream g(gif, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(g)), {});
  CHECK(data.size() > 1000 && data.substr(0, 6) == "GIF89a" &&
        (uint8_t)data.back() == 0x3B);
  CHECK(e.exportPngSequence(g_out + "/pngseq", &err));
  CHECK(QDir(QString::fromStdString(g_out + "/pngseq")).entryList(QDir::Files).size() == 8);
}

static void testMemoryAndPerf(Engine &e) {
  section("memory (TImageCache compression) + performance, 1280x720 x 120 frames");
  long rss0 = rssKB();
  e.newDocument(1280, 720, 24);
  e.tool = Tool::Brush;
  e.settings.brushMax = 6;
  double tStroke = 0;
  int segs = 0;
  for (int f = 0; f < 120; f++) {
    if (f) e.insertBlankFrame();
    auto pts = circle(200 + (f % 40) * 20, 360, 100 + (f % 7) * 10, 48);
    double t0 = nowMs();
    stroke(e, pts);
    tStroke += nowMs() - t0;
    segs += (int)pts.size();
    e.setCurrentRow(f);  // compresses cold drawings
  }
  long rss1 = rssKB();
  printf("  stroke segment avg: %.3f ms\n", tStroke / segs);
  long raw = 120L * 1280 * 720 * 4 / 1024;
  printf("  process RSS: %ld KB -> %ld KB (+%ld KB) for 120 CM32 frames; "
         "uncompressed drawings alone would be %ld KB\n",
         rss0, rss1, rss1 - rss0, raw);
  CHECK(rss1 - rss0 < raw / 4);  // TImageCache compression: >4x saving
  CHECK(e.memoryKB() > 0);
  Img im{1280, 720, std::vector<uint8_t>(1280 * 720 * 4)};
  double t0 = nowMs();
  for (int i = 0; i < 10; i++)
    e.renderRGBA(i * 5, im.px.data(), 1280 * 4, 0, 0, 1279, 719, true);
  printf("  full-frame render (onion on): %.2f ms\n", (nowMs() - t0) / 10);
  t0 = nowMs();
  for (int i = 0; i < 100; i++)
    e.renderRGBA(60, im.px.data(), 1280 * 4, 600, 300, 663, 363, true);
  printf("  64x64 dirty-rect render: %.3f ms\n", (nowMs() - t0) / 100);
  std::string err;
  t0 = nowMs();
  CHECK(e.save(g_out + "/bigproject", &err));
  printf("  save 120 frames (full): %.0f ms\n", nowMs() - t0);
  // incremental save: only the modified level is re-encoded
  e.addLayer();
  stroke(e, line(10, 10, 600, 300));
  t0 = nowMs();
  CHECK(e.save(g_out + "/bigproject", &err));
  double inc = nowMs() - t0;
  printf("  save after editing 1 frame of a new layer (incremental): %.0f ms\n", inc);
  t0 = nowMs();
  CHECK(e.save(g_out + "/bigproject", &err));
  printf("  save with no level changes: %.0f ms\n", nowMs() - t0);
  t0 = nowMs();
  CHECK(e.load(g_out + "/bigproject", &err));
  printf("  load 120 frames: %.0f ms\n", nowMs() - t0);
  CHECK(e.doc()->frameCount() == 120);
  CHECK(e.doc()->layerCount() == 2);
  // the reused (hard-linked) 120-frame TLV is intact after the swaps
  bool ok = true;
  Layer *l0 = e.doc()->layer(0);
  for (int r = 0; r < 120; r += 17) ok = ok && e.doc()->image(l0, r);
  CHECK(ok);
}

static void testGifQuantizer() {
  section("GIF quantizer");
  std::vector<uint8_t> px(64 * 64 * 4);
  for (int i = 0; i < 64 * 64; i++) {
    px[i * 4 + 0] = (i % 4) * 80;
    px[i * 4 + 1] = 10;
    px[i * 4 + 2] = 200;
    px[i * 4 + 3] = 255;
  }
  std::vector<uint8_t> pal, idx;
  GifEncoder::quantize(px.data(), 64 * 64, pal, idx);
  bool exact = true;
  for (int i = 0; i < 64 * 64; i++) {
    int k = idx[i];
    exact = exact && std::abs(pal[k * 3] - px[i * 4]) <= 4 &&
            std::abs(pal[k * 3 + 2] - px[i * 4 + 2]) <= 4;
  }
  CHECK(exact);
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  QCoreApplication app(argc, argv);
  setenv("OTM_LZOCOMPRESS", OTM_TEST_LZO_DIR "/lzocompress", 1);
  setenv("OTM_LZODECOMPRESS", OTM_TEST_LZO_DIR "/lzodecompress", 1);
  g_out = argc > 1 ? argv[1] : "otm_test_out";
  QDir().mkpath(QString::fromStdString(g_out));
  Engine e;
  testBrushAndRender(e);
  testFramesAndOnion(e);
  testLayers(e);
  testFillEraser(e);
  testSelection(e);
  testUndoRedo(e);
  testImport(e);
  testSaveLoad(e);
  testWorkerThreadSave(e);
  testExport(e);
  testGifQuantizer();
  testMemoryAndPerf(e);
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
