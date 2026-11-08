#include "tools.h"

#include "toonz/fill.h"
#include "toonz/rasterstrokegenerator.h"
#include "toonz/ttilesaver.h"
#include "toonz/ttileset.h"
#include "tpixelcm.h"
#include "trasterimage.h"
#include "trop.h"
#include "tropcm.h"
#include "ttoonzimage.h"
#include "tundo.h"

#include <algorithm>
#include <cmath>
#include "tcolorstyles.h"

namespace otm {

namespace {

template <class P>
P sub(const P &ras, TRect r) {
  return ras->extract(r);
}

TRasterP rasterOf(const TImageP &img) {
  if (TToonzImageP ti = img) return ti->getRaster();
  if (TRasterImageP ri = img) return ri->getRaster();
  return TRasterP();
}

// Pastes the tiles of a tile set onto a raster (desktop ToonzImageUtils
// equivalent, kept local to avoid pulling toonzimageutils' fx dependencies).
void pasteTiles(const TRasterP &ras, const TTileSet *set) {
  if (!ras || !set) return;
  if (const TTileSetCM32 *cm = dynamic_cast<const TTileSetCM32 *>(set)) {
    for (int i = 0; i < cm->getTileCount(); i++) {
      const TTileSetCM32::Tile *t = cm->getTile(i);
      TRasterCM32P r;
      t->getRaster(r);
      if (r) ras->copy(r, t->m_rasterBounds.getP00());
    }
  } else if (const TTileSetFullColor *fc =
                 dynamic_cast<const TTileSetFullColor *>(set)) {
    for (int i = 0; i < fc->getTileCount(); i++) {
      const TTileSetFullColor::Tile *t = fc->getTile(i);
      TRasterP r;
      t->getRaster(r);
      if (r) ras->copy(r, t->m_rasterBounds.getP00());
    }
  }
}

class RasterTilesUndo final : public TUndo {
  Document *m_doc;
  int m_uid, m_drawing;
  std::shared_ptr<TTileSet> m_before, m_after;
  std::string m_label;

public:
  RasterTilesUndo(Document *doc, int uid, int drawing, TTileSet *before,
                  TTileSet *after, const std::string &label)
      : m_doc(doc)
      , m_uid(uid)
      , m_drawing(drawing)
      , m_before(before)
      , m_after(after)
      , m_label(label) {}

  void apply(const TTileSet *set) const {
    Layer *l = m_doc->layerByUid(m_uid);
    if (!l) return;
    TImageP img = m_doc->drawing(l, m_drawing, true);
    pasteTiles(rasterOf(img), set);
    m_doc->commitDrawing(l, m_drawing, img);
  }
  void undo() const override { apply(m_before.get()); }
  void redo() const override { apply(m_after.get()); }
  int getSize() const override {
    return sizeof(*this) + m_before->getMemorySize() + m_after->getMemorySize();
  }
  QString getHistoryString() override {
    return QString::fromStdString(m_label);
  }
};

class CellsUndo final : public TUndo {
  Document *m_doc;
  int m_uid;
  std::vector<int> m_before, m_after;
  std::string m_label;

public:
  CellsUndo(Document *doc, int uid, const std::vector<int> &before,
            const std::vector<int> &after, const std::string &label)
      : m_doc(doc), m_uid(uid), m_before(before), m_after(after), m_label(label) {}
  void undo() const override {
    if (Layer *l = m_doc->layerByUid(m_uid)) l->cells = m_before;
    m_doc->touch(m_uid);
  }
  void redo() const override {
    if (Layer *l = m_doc->layerByUid(m_uid)) l->cells = m_after;
    m_doc->touch(m_uid);
  }
  int getSize() const override {
    return sizeof(*this) + (int)(m_before.size() + m_after.size()) * 4;
  }
  QString getHistoryString() override {
    return QString::fromStdString(m_label);
  }
};

}  // namespace

void pushCellsUndo(Document *doc, Layer *layer, const std::vector<int> &before,
                   const std::string &label) {
  if (before == layer->cells) return;
  doc->touch(layer);
  TUndoManager::manager()->add(
      new CellsUndo(doc, layer->uid, before, layer->cells, label));
}

//=============================================================================
// RasterChange

RasterChange::RasterChange(Document *doc, Layer *layer, int drawingNo,
                           const TImageP &img)
    : m_doc(doc), m_layerUid(layer->uid), m_drawing(drawingNo), m_img(img) {
  if (TToonzImageP ti = img) {
    TRasterCM32P ras = ti->getRaster();
    m_cmSet.reset(new TTileSetCM32(ras->getSize()));
    m_cmSaver.reset(new TTileSaverCM32(ras, m_cmSet.get()));
  } else if (TRasterImageP ri = img) {
    TRasterP ras = ri->getRaster();
    m_fcSet.reset(new TTileSetFullColor(ras->getSize()));
    m_fcSaver.reset(new TTileSaverFullColor(ras, m_fcSet.get()));
  }
}

RasterChange::~RasterChange() {}

void RasterChange::save(const TRect &r) {
  if (r.isEmpty()) return;
  if (m_cmSaver) m_cmSaver->save(r);
  if (m_fcSaver) m_fcSaver->save(r);
}

void RasterChange::commit(const std::string &label) {
  TRasterP ras = rasterOf(m_img);
  TTileSet *before = m_cmSet ? (TTileSet *)m_cmSet.get() : m_fcSet.get();
  if (!before || before->getTileCount() == 0) return;
  std::vector<TRect> rects;
  before->getRects(rects);
  TTileSet *after = nullptr;
  if (m_cmSet)
    after = new TTileSetCM32(ras->getSize());
  else
    after = new TTileSetFullColor(ras->getSize());
  for (const TRect &r : rects) after->add(ras, r);
  if (TToonzImageP ti = m_img) {
    TRect sb = ti->getSavebox() + before->getBBox();
    ti->setSavebox(sb * ras->getBounds());
  }
  TUndoManager::manager()->add(new RasterTilesUndo(
      m_doc, m_layerUid, m_drawing, m_cmSet ? (TTileSet *)m_cmSet.release()
                                            : (TTileSet *)m_fcSet.release(),
      after, label));
  m_cmSaver.reset();
  m_fcSaver.reset();
  m_doc->commitDrawing(m_doc->layerByUid(m_layerUid), m_drawing, m_img);
}

//=============================================================================
// StrokeSession

StrokeSession::StrokeSession(Document *doc, const ToolSettings &s, Tool tool)
    : m_doc(doc), m_s(s), m_tool(tool) {}

StrokeSession::~StrokeSession() {}

double StrokeSession::thickness(double pressure) const {
  if (m_tool == Tool::Eraser) return m_s.eraserSize;
  if (!m_s.pressure) return m_s.brushMax;
  // same curve as the desktop ToonzRasterBrushTool (computeThickness)
  double t = pressure * pressure * pressure;
  return m_s.brushMin + (m_s.brushMax - m_s.brushMin) * t;
}

TRect StrokeSession::begin(const TPointD &p, double pressure) {
  m_layer = m_doc->layer(m_doc->currentLayer);
  if (!m_layer || m_layer->locked || !m_layer->visible) return TRect();
  if (m_tool == Tool::Eraser && m_layer->cell(m_doc->currentRow) == 0)
    return TRect();  // nothing to erase
  m_cellsBefore = m_layer->cells;
  m_drawing = m_doc->ensureDrawing(m_layer, m_doc->currentRow, &m_cellCreated);
  m_img     = m_doc->drawing(m_layer, m_drawing, true);
  if (!m_img) return TRect();
  m_change.reset(new RasterChange(m_doc, m_layer, m_drawing, m_img));
  m_valid = true;
  m_sp         = p;
  m_spPressure = pressure;

  TThickPoint tp(p, std::max(1.0, thickness(pressure)));
  int style = m_doc->currentStyle;
  m_softMode = soft();
  if (m_softMode) {
    TRasterP ras = rasterOf(m_img);
    m_cov        = TRasterGR8P(ras->getSize());
    m_cov->clear();
    m_backup   = ras->clone();
    m_lastSoft = tp;
    TRect r    = stampSegment(tp, tp, true);
    applySoft(r);
    m_total += r;
    return r;
  }
  if (TToonzImageP ti = m_img) {
    TRasterCM32P ras = ti->getRaster();
    if (m_tool == Tool::Brush) {
      m_gen.reset(new RasterStrokeGenerator(ras, BRUSH, NONE, style, tp, false,
                                            0, false, m_s.antialias, false));
    } else {
      ColorType ct = m_s.eraseMode == EraseMode::Lines   ? INK
                     : m_s.eraseMode == EraseMode::Areas ? PAINT
                                                         : INKNPAINT;
      bool pencil = m_s.eraseMode == EraseMode::Areas;
      m_gen.reset(new RasterStrokeGenerator(ras, ERASE, ct, 0, tp, false, 0,
                                            false, !pencil, false));
    }
  } else {
    // Full colour layer: rasterize the stroke as a CM32 mask with the same
    // OpenToonz brush, then composite it over a snapshot of the drawing.
    TRaster32P ras = TRasterImageP(m_img)->getRaster();
    m_mask         = TRasterCM32P(ras->getSize());
    m_mask->clear();
    m_base = ras->clone();
    m_gen.reset(new RasterStrokeGenerator(m_mask, BRUSH, NONE,
                                          m_tool == Tool::Brush ? style : 1, tp,
                                          false, 0, false, m_s.antialias, false));
  }
  return paintPiece(false);
}

TRect StrokeSession::paintPiece(bool last) {
  TRasterP ras = rasterOf(m_img);
  TRect r      = m_gen->getLastRect() * ras->getBounds();
  if (r.isEmpty()) return r;
  m_change->save(r);
  bool pencil = (m_tool == Tool::Brush && !m_s.antialias) ||
                (m_tool == Tool::Eraser && m_s.eraseMode == EraseMode::Areas);
  m_gen->generateLastPieceOfStroke(pencil, last);
  if (m_mask) {
    TRaster32P ras32 = ras;
    TRaster32P dst   = sub(ras32, r);
    dst->copy(sub(m_base, r));
    if (m_tool == Tool::Brush) {
      TRaster32P tmp(r.getSize());
      tmp->clear();
      TRop::convert(tmp, sub(m_mask, r), TPaletteP(m_doc->palette()), false);
      TRop::over(dst, tmp);
    } else {
      TRasterCM32P mk = sub(m_mask, r);
      for (int y = 0; y < dst->getLy(); y++) {
        TPixel32 *d         = dst->pixels(y);
        const TPixelCM32 *m = mk->pixels(y);
        for (int x = 0; x < dst->getLx(); x++, d++, m++) {
          int keep = m->getInk() ? m->getTone() : 255;  // 255 = untouched
          if (keep == 255) continue;
          d->r = d->r * keep / 255;
          d->g = d->g * keep / 255;
          d->b = d->b * keep / 255;
          d->m = d->m * keep / 255;
        }
      }
    }
  }
  m_total += r;
  return r;
}

// Stabilizer: exponential smoothing of the input (lazy brush).
TThickPoint StrokeSession::smooth(const TPointD &p, double pressure) {
  double k = 1.0 / (1.0 + 1.5 * std::max(0, std::min(10, m_s.stabilizer)));
  m_sp         = m_sp + (p - m_sp) * k;
  m_spPressure = m_spPressure + (pressure - m_spPressure) * k;
  return TThickPoint(m_sp, std::max(1.0, thickness(m_spPressure)));
}

TRect StrokeSession::move(const TPointD &p, double pressure) {
  if (!m_valid) return TRect();
  TThickPoint tp = smooth(p, pressure);
  if (m_softMode) {
    TRect r    = stampSegment(m_lastSoft, tp, false);
    m_lastSoft = tp;
    applySoft(r);
    m_total += r;
    return r;
  }
  m_gen->add(tp);
  return paintPiece(false);
}

bool StrokeSession::soft() const {
  if (m_tool == Tool::Eraser) return m_s.eraserHardness < 100;
  return m_s.hardness < 100 || m_s.opacity < 100 || m_s.grain > 0 || m_s.scatter > 0;
}

// Paper grain: smooth value noise on a fixed canvas-space grid (2 octaves).
static inline double hash01(int x, int y) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
  h          = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}
static double valueNoise(double x, double y) {
  int ix = (int)std::floor(x), iy = (int)std::floor(y);
  double fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  double a = hash01(ix, iy), b = hash01(ix + 1, iy), c = hash01(ix, iy + 1),
         d = hash01(ix + 1, iy + 1);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static double paperGrain(int px, int py) {
  return 0.65 * valueNoise(px / 2.3, py / 2.3) + 0.35 * valueNoise(px / 0.9, py / 0.9);
}

static inline double softFalloff(double d, double r, double hard) {
  if (hard >= 0.999) return std::max(0.0, std::min(1.0, r - d + 0.5));
  double inner = r * hard;
  if (d <= inner) return 1.0;
  if (d >= r) return 0.0;
  double t = (d - inner) / (r - inner);
  return 1.0 - t * t * (3 - 2 * t);
}

// Stamps discs along a->b into the per-stroke coverage buffer (max blend,
// so overlapping stamps do not build up: uniform stroke opacity).
TRect StrokeSession::stampSegment(const TThickPoint &a, const TThickPoint &b,
                                  bool first) {
  bool eraser   = m_tool == Tool::Eraser;
  double hard   = (eraser ? m_s.eraserHardness : m_s.hardness) / 100.0;
  double opac   = eraser ? 1.0 : m_s.opacity / 100.0;
  double len    = tdistance(TPointD(a.x, a.y), TPointD(b.x, b.y));
  double rmin   = std::max(0.5, std::min(a.thick, b.thick) * 0.5);
  double step   = std::max(0.5, rmin * 0.15);
  int n         = std::max(1, (int)std::ceil(len / step));
  TRect bounds  = m_cov->getBounds(), dirty;
  for (int i = first ? 0 : 1; i <= n; i++) {
    double t = (double)i / n;
    double x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
    double r = std::max(0.5, (a.thick + (b.thick - a.thick) * t) * 0.5);
    if (!eraser && m_s.scatter > 0) {  // spray-like dab jitter
      auto rnd = [this]() {
        m_rng = m_rng * 1664525u + 1013904223u;
        return ((m_rng >> 8) & 0xFFFF) / 65535.0 - 0.5;
      };
      double k = r * 2.0 * m_s.scatter / 100.0;
      x += rnd() * k, y += rnd() * k;
    }
    TRect sr(tfloor(x - r - 1), tfloor(y - r - 1), tceil(x + r + 1),
             tceil(y + r + 1));
    sr = sr * bounds;
    if (sr.isEmpty()) continue;
    for (int py = sr.y0; py <= sr.y1; py++) {
      TPixelGR8 *c = m_cov->pixels(py) + sr.x0;
      for (int px = sr.x0; px <= sr.x1; px++, c++) {
        double d = std::hypot(px + 0.5 - x, py + 0.5 - y);
        double f = softFalloff(d, r, hard);
        if (!eraser && m_s.grain > 0)
          f *= std::max(0.0, 1.0 - m_s.grain / 100.0 * 1.6 * paperGrain(px, py));
        int v    = (int)(opac * f * 255 + 0.5);
        if (v > c->value) c->value = v;
      }
    }
    dirty += sr;
  }
  return dirty;
}

// Recomputes the drawing inside r from the pre-stroke backup + coverage.
void StrokeSession::applySoft(const TRect &r) {
  if (r.isEmpty()) return;
  m_change->save(r);
  bool eraser = m_tool == Tool::Eraser;
  int style   = m_doc->currentStyle;
  if (TToonzImageP ti = m_img) {
    TRasterCM32P ras = ti->getRaster(), bak = m_backup;
    bool lines = m_s.eraseMode != EraseMode::Areas;
    bool areas = m_s.eraseMode != EraseMode::Lines;
    for (int y = r.y0; y <= r.y1; y++) {
      TPixelCM32 *d       = ras->pixels(y) + r.x0;
      const TPixelCM32 *o = bak->pixels(y) + r.x0;
      const TPixelGR8 *c  = m_cov->pixels(y) + r.x0;
      for (int x = r.x0; x <= r.x1; x++, d++, o++, c++) {
        int cv = c->value;
        if (!cv) {
          *d = *o;
          continue;
        }
        int inkA = 255 - o->getTone();
        if (eraser) {
          int ink = o->getInk(), paint = o->getPaint(), a = inkA;
          if (lines) a = a * (255 - cv) / 255;
          if (areas && cv > 127) paint = 0;
          *d = TPixelCM32(a ? ink : 0, paint, 255 - a);
        } else if (o->getInk() == style || inkA == 0) {
          int a = inkA + cv * (255 - inkA) / 255;  // same ink: "over"
          *d    = TPixelCM32(style, o->getPaint(), 255 - a);
        } else if (cv > inkA) {  // CM32 holds one ink per pixel: strongest wins
          *d = TPixelCM32(style, o->getPaint(), 255 - cv);
        } else
          *d = *o;
      }
    }
  } else {
    TRaster32P ras = rasterOf(m_img), bak = m_backup;
    TPixel32 col   = m_doc->palette()->getStyle(style)->getMainColor();
    for (int y = r.y0; y <= r.y1; y++) {
      TPixel32 *d       = ras->pixels(y) + r.x0;
      const TPixel32 *o = bak->pixels(y) + r.x0;
      const TPixelGR8 *c = m_cov->pixels(y) + r.x0;
      for (int x = r.x0; x <= r.x1; x++, d++, o++, c++) {
        int cv = c->value * col.m / 255;
        if (eraser) {
          int k = 255 - c->value;
          *d    = TPixel32(o->r * k / 255, o->g * k / 255, o->b * k / 255,
                           o->m * k / 255);
        } else {  // premultiplied "over"
          int k = 255 - cv;
          *d    = TPixel32(col.r * cv / 255 + o->r * k / 255,
                           col.g * cv / 255 + o->g * k / 255,
                           col.b * cv / 255 + o->b * k / 255, cv + o->m * k / 255);
        }
      }
    }
  }
}

TRect StrokeSession::end(const TPointD &p, double pressure) {
  if (!m_valid) return TRect();
  // stabilizer catch-up so the stroke ends where the pointer was released
  std::vector<TThickPoint> tail;
  if (m_s.stabilizer > 0)
    for (int i = 0; i < 6 && tdistance(m_sp, p) > 0.5; i++) tail.push_back(smooth(p, pressure));
  tail.push_back(TThickPoint(p, std::max(1.0, thickness(pressure))));
  if (m_softMode) {
    for (auto &tp : tail) {
      TRect r    = stampSegment(m_lastSoft, tp, false);
      m_lastSoft = tp;
      applySoft(r);
      m_total += r;
    }
  } else {
    for (size_t i = 0; i + 1 < tail.size(); i++) {
      m_gen->add(tail[i]);
      paintPiece(false);
    }
    // last piece, closing the stroke (as the desktop tools do on button up)
    m_gen->add(tail.back());
    paintPiece(true);
  }
  m_valid = false;
  TUndoManager::manager()->beginBlock();
  if (m_cellCreated) pushCellsUndo(m_doc, m_layer, m_cellsBefore, "New drawing");
  m_change->commit(m_tool == Tool::Brush ? "Brush" : "Eraser");
  TUndoManager::manager()->endBlock();
  m_change.reset();
  m_gen.reset();
  m_mask   = TRasterCM32P();
  m_base   = TRaster32P();
  m_cov    = TRasterGR8P();
  m_backup = TRasterP();
  return m_total;
}

//=============================================================================
// Fill / picker

TRect fillAt(Document *doc, const ToolSettings &s, const TPointD &p) {
  Layer *layer = doc->layer(doc->currentLayer);
  if (!layer || layer->locked || !layer->visible) return TRect();
  TPoint ip((int)std::floor(p.x), (int)std::floor(p.y));
  if (!TRect(doc->size()).contains(ip)) return TRect();

  std::vector<int> cellsBefore = layer->cells;
  bool created                 = false;
  int drawing = doc->ensureDrawing(layer, doc->currentRow, &created);
  TImageP img = doc->drawing(layer, drawing, true);
  if (!img) return TRect();

  FillParameters params;
  params.m_styleId      = doc->currentStyle;
  params.m_p            = ip;
  params.m_emptyOnly    = s.fillEmptyOnly;
  params.m_minFillDepth = std::max(0, std::min(15, s.fillDepth));
  params.m_maxFillDepth = params.m_minFillDepth;
  params.m_palette      = doc->palette();

  TRect dirty;
  TUndoManager::manager()->beginBlock();
  if (created) pushCellsUndo(doc, layer, cellsBefore, "New drawing");
  if (TToonzImageP ti = img) {
    TRasterCM32P ras = ti->getRaster();
    TTileSetCM32 *before = new TTileSetCM32(ras->getSize());
    {
      TTileSaverCM32 saver(ras, before);
      params.m_palette = nullptr;  // no auto-paint inks in mobile palettes
      if (s.fillMode != FillMode::Lines) {
        params.m_fillType = L"Areas";
        fill(ras, params, &saver);
      }
      if (s.fillMode != FillMode::Areas)
        inkFill(ras, ip, params.m_styleId, 2, &saver);
    }
    if (before->getTileCount() > 0) {
      std::vector<TRect> rects;
      before->getRects(rects);
      TTileSetCM32 *after = new TTileSetCM32(ras->getSize());
      for (auto &r : rects) after->add(ras, r);
      dirty = before->getBBox();
      ti->setSavebox((ti->getSavebox() + dirty) * ras->getBounds());
      TUndoManager::manager()->add(new RasterTilesUndo(
          doc, layer->uid, drawing, before, after, "Fill"));
      doc->commitDrawing(layer, drawing, img);
    } else
      delete before;
  } else if (TRasterImageP ri = img) {
    TRaster32P ras = ri->getRaster();
    TTileSetFullColor *before = new TTileSetFullColor(ras->getSize());
    {
      TTileSaverFullColor saver(ras, before);
      fill(ras, TRaster32P(), params, &saver);
    }
    if (before->getTileCount() > 0) {
      std::vector<TRect> rects;
      before->getRects(rects);
      TTileSetFullColor *after = new TTileSetFullColor(ras->getSize());
      for (auto &r : rects) after->add(ras, r);
      dirty = before->getBBox();
      TUndoManager::manager()->add(new RasterTilesUndo(
          doc, layer->uid, drawing, before, after, "Fill"));
      doc->commitDrawing(layer, drawing, img);
    } else
      delete before;
  }
  TUndoManager::manager()->endBlock();
  if (created && dirty.isEmpty()) dirty = TRect(doc->size());
  return dirty;
}

int pickStyleAt(Document *doc, const TPointD &p) {
  Layer *layer = doc->layer(doc->currentLayer);
  TImageP img  = doc->image(layer, doc->currentRow);
  TToonzImageP ti = img;
  if (!ti) return -1;
  TRasterCM32P ras = ti->getRaster();
  TPoint ip((int)std::floor(p.x), (int)std::floor(p.y));
  if (!ras->getBounds().contains(ip)) return -1;
  TPixelCM32 pix = ras->pixels(ip.y)[ip.x];
  int tone       = pix.getTone();
  if (tone < 128 || (tone < 255 && pix.getPaint() == 0)) return pix.getInk();
  if (pix.getPaint() != 0) return pix.getPaint();
  return -1;
}

//=============================================================================
// Shapes

std::vector<TPointD> shapePoints(ShapeType t, const TPointD &a, const TPointD &b) {
  std::vector<TPointD> pts;
  auto seg = [&](TPointD p, TPointD q) {
    int n = std::max(1, (int)(tdistance(p, q) / 4));
    for (int i = (pts.empty() ? 0 : 1); i <= n; i++) pts.push_back(p + (q - p) * ((double)i / n));
  };
  if (t == ShapeType::Line)
    seg(a, b);
  else if (t == ShapeType::Rect) {
    TPointD c(b.x, a.y), d(a.x, b.y);
    seg(a, c), seg(c, b), seg(b, d), seg(d, a);
  } else {
    TPointD ctr = (a + b) * 0.5;
    double rx = std::abs(b.x - a.x) * 0.5, ry = std::abs(b.y - a.y) * 0.5;
    int n = std::max(24, (int)((rx + ry) * 0.6));
    for (int i = 0; i <= n; i++) {
      double ang = 2 * M_PI * i / n;
      pts.push_back(ctr + TPointD(rx * cos(ang), ry * sin(ang)));
    }
  }
  return pts;
}

//=============================================================================
// Selection

bool Selection::lift(const TRect &rectIn) {
  if (m_active) commit();
  Layer *layer = m_doc->layer(m_doc->currentLayer);
  if (!layer || layer->locked || !layer->visible) return false;
  int drawing = layer->cell(m_doc->currentRow);
  TImageP img = m_doc->drawing(layer, drawing, true);
  if (!img) return false;
  TRasterP ras = rasterOf(img);
  TRect rect   = rectIn * ras->getBounds();
  if (rect.getLx() < 2 || rect.getLy() < 2) return false;

  m_layerUid = layer->uid;
  m_drawing  = drawing;
  m_img      = img;
  m_rect     = rect;
  m_change.reset(new RasterChange(m_doc, layer, drawing, img));
  m_change->save(rect);
  TRasterP part = sub(ras, rect)->clone();
  if (TRasterCM32P cm = part)
    m_floating = TToonzImageP(cm, cm->getBounds());
  else
    m_floating = TRasterImageP(part);
  sub(ras, rect)->clear();
  dx = dy = angle = 0;
  scale           = 1;
  m_active        = true;
  return true;
}

bool Selection::liftPolygon(const std::vector<TPointD> &poly) {
  if (poly.size() < 3) return false;
  // (TRectD::operator+= ignores zero-area rects: compute the bbox directly)
  double x0 = poly[0].x, y0 = poly[0].y, x1 = x0, y1 = y0;
  for (auto &p : poly)
    x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x),
    y1 = std::max(y1, p.y);
  TRect rect(tfloor(x0), tfloor(y0), tceil(x1), tceil(y1));
  if (!lift(rect)) return false;
  // keep in the floating copy only the pixels inside the polygon (pixel
  // centres, even-odd rule); outside pixels go back to the drawing.
  TRasterP fl  = rasterOf(m_floating);
  TRasterP ras = rasterOf(m_img);
  TRasterCM32P flcm = fl;
  TRaster32P fl32   = fl;
  int lifted = 0;
  for (int y = 0; y < fl->getLy(); y++) {
    double sy = m_rect.y0 + y + 0.5;
    std::vector<double> xs;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
      const TPointD &a = poly[i], &c = poly[j];
      if ((a.y > sy) != (c.y > sy))
        xs.push_back(a.x + (sy - a.y) * (c.x - a.x) / (c.y - a.y));
    }
    std::sort(xs.begin(), xs.end());
    auto inside = [&](double px) {
      int n = 0;
      for (double v : xs) if (v < px) n++;
      return n & 1;
    };
    for (int x = 0; x < fl->getLx(); x++) {
      if (inside(m_rect.x0 + x + 0.5)) {
        lifted++;
        continue;
      }
      // outside: restore the source pixel and clear it in the floating copy
      TPoint dp(m_rect.x0 + x, m_rect.y0 + y);
      if (flcm) {
        TRasterCM32P rcm = ras;
        rcm->pixels(dp.y)[dp.x] = flcm->pixels(y)[x];
        flcm->pixels(y)[x]      = TPixelCM32();
      } else {
        TRaster32P r32 = ras;
        r32->pixels(dp.y)[dp.x] = fl32->pixels(y)[x];
        fl32->pixels(y)[x]      = TPixel32::Transparent;
      }
    }
  }
  if (!lifted) {
    cancel();
    return false;
  }
  return true;
}

void Selection::setTransform(double ndx, double ndy, double nscale,
                             double nangle) {
  dx    = ndx;
  dy    = ndy;
  scale = std::max(0.05, std::min(20.0, nscale));
  angle = nangle;
}

TAffine Selection::affine() const {
  TPointD half(m_rect.getLx() * 0.5, m_rect.getLy() * 0.5);
  TPointD centre = TPointD(m_rect.x0, m_rect.y0) + half;
  return TTranslation(centre + TPointD(dx, dy)) * TRotation(angle) *
         TScale(scale) * TTranslation(-half);
}

TRectD Selection::bounds() const {
  return affine() * TRectD(0, 0, m_rect.getLx(), m_rect.getLy());
}

TRect Selection::commit() {
  if (!m_active) return TRect();
  m_active     = false;
  TRasterP ras = rasterOf(m_img);
  TRasterP fl  = rasterOf(m_floating);
  TRect dirty  = m_rect;
  bool identity = std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9 &&
                  std::abs(scale - 1) < 1e-9 && std::abs(angle) < 1e-9;
  if (identity) {
    TRop::over(ras, fl, m_rect.getP00());
  } else {
    TRectD b = bounds();
    TRect db(tfloor(b.x0), tfloor(b.y0), tceil(b.x1), tceil(b.y1));
    db = db * ras->getBounds();
    if (!db.isEmpty()) {
      m_change->save(db);
      TRasterP tmp = ras->create(db.getLx(), db.getLy());
      tmp->clear();
      TRop::resample(tmp, fl, TTranslation(-db.x0, -db.y0) * affine(),
                     TRop::Triangle);
      TRop::over(ras, tmp, db.getP00());
      dirty += db;
    }
  }
  m_change->commit("Transform");
  m_change.reset();
  m_floating = TImageP();
  return dirty;
}

TRect Selection::cancel() {
  if (!m_active) return TRect();
  m_active = false;
  TRop::over(rasterOf(m_img), rasterOf(m_floating), m_rect.getP00());
  m_doc->commitDrawing(m_doc->layerByUid(m_layerUid), m_drawing, m_img);
  m_change.reset();
  m_floating = TImageP();
  return m_rect;
}

TRect Selection::erase() {
  if (!m_active) return TRect();
  m_active = false;
  m_change->commit("Delete selection");
  m_change.reset();
  m_floating = TImageP();
  return m_rect;
}

}  // namespace otm
