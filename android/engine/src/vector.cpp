#include "vector.h"

#include "tcolorstyles.h"
#include "tregion.h"
#include "tstroke.h"
#include "tundo.h"

#include <algorithm>
#include <cmath>

namespace otm {

// Vector drawings use OpenToonz's convention: origin at the camera centre,
// coordinates in Stage units (Stage::inch = 53.33333 units per inch).  The
// mobile canvas is treated as a standard 16-inch-wide camera, so one unit is
// width/(16*53.333) canvas pixels (2.25 px for 1920 wide, like desktop).
static const double kStageInch = 53.33333;
double vectorPxPerUnit(const Document &doc) { return doc.width() / (16.0 * kStageInch); }
static TAffine imageToRaster(const Document &doc) {
  return TTranslation(doc.width() * 0.5, doc.height() * 0.5) * TScale(vectorPxPerUnit(doc));
}

// ---------------------------------------------------------------- raster
namespace {

struct Cov {  // float coverage buffer over a clip rectangle
  int w, h;
  std::vector<float> v;
  Cov(int w_, int h_) : w(w_), h(h_), v((size_t)w_ * h_, 0.f) {}
  float &at(int x, int y) { return v[(size_t)y * w + x]; }
};

// Even-odd polygon fill with 4 sub-scanlines per pixel (anti-aliased).
void fillPolygon(Cov &c, const std::vector<std::vector<TPointD>> &contours) {
  double y0 = 1e30, y1 = -1e30;
  for (auto &ct : contours)
    for (auto &p : ct) y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  int sy0 = std::max(0, (int)std::floor(y0 * 4)), sy1 = std::min(c.h * 4 - 1, (int)std::ceil(y1 * 4));
  std::vector<double> xs;
  for (int sy = sy0; sy <= sy1; sy++) {
    double y = (sy + 0.5) / 4.0;
    xs.clear();
    for (auto &ct : contours)
      for (size_t i = 0, j = ct.size() - 1; i < ct.size(); j = i++) {
        const TPointD &a = ct[i], &b = ct[j];
        if ((a.y > y) != (b.y > y)) xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    std::sort(xs.begin(), xs.end());
    int row = sy / 4;
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      double xa = std::max(0.0, xs[k]), xb = std::min((double)c.w, xs[k + 1]);
      if (xb <= xa) continue;
      int ia = (int)xa, ib = (int)xb;
      if (ia == ib) {
        c.at(ia, row) += (float)((xb - xa) * 0.25);
        continue;
      }
      c.at(ia, row) += (float)((ia + 1 - xa) * 0.25);
      for (int x = ia + 1; x < ib && x < c.w; x++) c.at(x, row) += 0.25f;
      if (ib < c.w) c.at(ib, row) += (float)((xb - ib) * 0.25);
    }
  }
}

// Anti-aliased stroke: discs stamped along the centre line (max coverage).
void strokeCoverage(Cov &c, const TStroke *s, const TAffine &aff, double rScale) {
  double len = s->getLength() * rScale;
  int n      = std::max(2, (int)(len / 0.5));
  for (int i = 0; i <= n; i++) {
    double w      = s->getParameterAtLength(s->getLength() * i / n);
    TThickPoint t = s->getThickPoint(w);
    TPointD p     = aff * TPointD(t.x, t.y);
    double r      = t.thick * 0.5 * rScale;
    if (r < 0.3) r = 0.3;  // hairline
    int x0 = std::max(0, (int)std::floor(p.x - r - 1)), x1 = std::min(c.w - 1, (int)std::ceil(p.x + r + 1));
    int y0 = std::max(0, (int)std::floor(p.y - r - 1)), y1 = std::min(c.h - 1, (int)std::ceil(p.y + r + 1));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        double d = std::hypot(x + 0.5 - p.x, y + 0.5 - p.y);
        float f  = (float)std::max(0.0, std::min(1.0, r - d + 0.5));
        if (f > c.at(x, y)) c.at(x, y) = f;
      }
  }
}

void composite(const TRaster32P &out, Cov &c, TPixel32 col, int opacity,
               const TPixel32 *cs) {
  if (cs) {  // OpenToonz applyColorScale semantics (onion tint)
    col.r = 255 - (255 - col.r) * (255 - cs->r) / 255;
    col.g = 255 - (255 - col.g) * (255 - cs->g) / 255;
    col.b = 255 - (255 - col.b) * (255 - cs->b) / 255;
  }
  for (int y = 0; y < c.h; y++) {
    TPixel32 *d = out->pixels(y);
    for (int x = 0; x < c.w; x++) {
      float f = std::min(1.f, c.at(x, y));
      if (f <= 0) continue;
      int a = (int)(f * col.m * opacity / 255 + 0.5);
      if (!a) continue;
      int k = 255 - a;
      d[x].r = col.r * a / 255 + d[x].r * k / 255;
      d[x].g = col.g * a / 255 + d[x].g * k / 255;
      d[x].b = col.b * a / 255 + d[x].b * k / 255;
      d[x].m = a + d[x].m * k / 255;
    }
  }
}

void regionContour(const TRegion *r, const TAffine &aff, std::vector<TPointD> &pts) {
  for (UINT i = 0; i < r->getEdgeCount(); i++) {
    TEdge *e = r->getEdge(i);
    if (!e || !e->m_s) continue;
    double w0 = e->m_w0, w1 = e->m_w1;
    double len = std::abs(e->m_s->getLength(std::min(w0, w1), std::max(w0, w1)));
    int n = std::max(2, (int)(len / 2.0));
    for (int k = 0; k <= n; k++) {
      TThickPoint t = e->m_s->getThickPoint(w0 + (w1 - w0) * k / n);
      pts.push_back(aff * TPointD(t.x, t.y));
    }
  }
}

void drawRegion(const Document &doc, const TRegion *r, const TRaster32P &out,
                const TAffine &aff, int opacity, const TPixel32 *cs) {
  TColorStyle *st = doc.palette()->getStyle(r->getStyle());
  if (r->getStyle() > 0 && st) {
    std::vector<std::vector<TPointD>> contours(1);
    regionContour(r, aff, contours[0]);
    if (contours[0].size() >= 3) {
      Cov c(out->getLx(), out->getLy());
      fillPolygon(c, contours);
      composite(out, c, st->getMainColor(), opacity, cs);
    }
  }
  for (UINT i = 0; i < r->getSubregionCount(); i++)  // islands drawn on top
    drawRegion(doc, r->getSubregion(i), out, aff, opacity, cs);
}

}  // namespace

void rasterizeVector(const Document &doc, const TVectorImageP &vi,
                     const TRaster32P &out, const TAffine &aff0, int opacity,
                     const TPixel32 *colorScale) {
  if (!vi || opacity <= 0) return;
  TAffine aff  = aff0 * imageToRaster(doc);
  double scale = std::sqrt(std::abs(aff.det()));
  // OpenToonz order: filled regions below, strokes above
  for (UINT i = 0; i < vi->getRegionCount(); i++)
    drawRegion(doc, vi->getRegion(i), out, aff, opacity, colorScale);
  for (UINT i = 0; i < vi->getStrokeCount(); i++) {
    const TStroke *s = vi->getStroke(i);
    TColorStyle *st  = doc.palette()->getStyle(s->getStyle());
    if (!st || s->getStyle() == 0) continue;
    TRectD b = aff * s->getBBox();
    TRect bb(std::max(0, (int)b.x0 - 4), std::max(0, (int)b.y0 - 4),
             std::min(out->getLx() - 1, (int)b.x1 + 4), std::min(out->getLy() - 1, (int)b.y1 + 4));
    if (bb.isEmpty()) continue;
    TRaster32P sub = out->extract(bb);
    Cov c(bb.getLx(), bb.getLy());
    strokeCoverage(c, s, TTranslation(-bb.x0, -bb.y0) * aff, scale);
    composite(sub, c, st->getMainColor(), opacity, colorScale);
  }
}

// ---------------------------------------------------------------- undo
namespace {
class VectorUndo final : public TUndo {
  Document *m_doc;
  int m_uid, m_drawing;
  TVectorImageP m_b, m_a;
  std::string m_label;

public:
  VectorUndo(Document *d, int uid, int dr, TVectorImageP b, TVectorImageP a, std::string l)
      : m_doc(d), m_uid(uid), m_drawing(dr), m_b(b), m_a(a), m_label(l) {}
  void set(const TVectorImageP &v) const {
    if (Layer *l = m_doc->layerByUid(m_uid)) {
      TVectorImageP c = v->clone();
      c->setPalette(m_doc->palette());
      m_doc->setDrawing(l, m_drawing, c);
    }
  }
  void undo() const override { set(m_b); }
  void redo() const override { set(m_a); }
  int getSize() const override { return sizeof(*this) + 4096; }
};
}  // namespace

void pushVectorUndo(Document *doc, Layer *layer, int drawing,
                    const TVectorImageP &before, const TVectorImageP &after,
                    const std::string &label) {
  TUndoManager::manager()->add(
      new VectorUndo(doc, layer->uid, drawing, before, after->clone(), label));
  doc->commitDrawing(layer, drawing, TImageP(after.getPointer()));
}

static std::vector<int> doc_cells(Document *doc) {
  Layer *l = doc->layer(doc->currentLayer);
  return l ? l->cells : std::vector<int>();
}

static TPointD toImage(const Document &doc, const TPointD &p) {
  double k = vectorPxPerUnit(doc);
  return TPointD((p.x - doc.width() * 0.5) / k, (p.y - doc.height() * 0.5) / k);
}

static TVectorImageP currentVector(Document *doc, Layer **layerOut, int *drawingOut,
                                   bool create, bool *created) {
  Layer *layer = doc->layer(doc->currentLayer);
  if (!layer || layer->locked || !layer->visible || layer->type != LayerType::Vector)
    return TVectorImageP();
  int d = create ? doc->ensureDrawing(layer, doc->currentRow, created)
                 : layer->cell(doc->currentRow);
  TVectorImageP vi = doc->drawing(layer, d, true);
  if (vi) vi->setPalette(doc->palette());
  *layerOut   = layer;
  *drawingOut = d;
  return vi;
}

// ---------------------------------------------------------------- stroke
VectorStroke::VectorStroke(Document *doc, const ToolSettings &s, Tool tool)
    : m_doc(doc), m_s(s), m_tool(tool) {}

// thickness in Stage units (tool sizes are given in canvas pixels)
double VectorStroke::thickness(double pressure) const {
  double k = vectorPxPerUnit(*m_doc);
  if (m_tool == Tool::Eraser) return m_s.eraserSize / k;
  if (!m_s.pressure) return m_s.brushMax / k;
  double t = pressure * pressure * pressure;
  return (m_s.brushMin + (m_s.brushMax - m_s.brushMin) * t) / k;
}

bool VectorStroke::begin(const TPointD &rp, double pressure) {
  m_cellsBefore = doc_cells(m_doc);
  m_vi = currentVector(m_doc, &m_layer, &m_drawing, m_tool == Tool::Brush, &m_created);
  if (!m_vi) return false;
  m_before     = m_vi->clone();
  m_valid      = true;
  m_sp         = rp;
  m_spPressure = pressure;
  TPointD p    = toImage(*m_doc, rp);
  if (m_tool == Tool::Eraser)
    eraseAt(p);
  else {
    m_pts.push_back(TThickPoint(p, thickness(pressure)));
    updatePreview();
  }
  return true;
}

void VectorStroke::move(const TPointD &rp, double pressure) {
  if (!m_valid) return;
  double k     = 1.0 / (1.0 + 1.5 * std::max(0, std::min(10, m_s.stabilizer)));
  m_sp         = m_sp + (rp - m_sp) * k;
  m_spPressure = m_spPressure + (pressure - m_spPressure) * k;
  TPointD p    = toImage(*m_doc, m_tool == Tool::Eraser ? rp : m_sp);
  if (m_tool == Tool::Eraser) {
    eraseAt(p);
    return;
  }
  if (!m_pts.empty() &&
      tdistance(TPointD(m_pts.back().x, m_pts.back().y), p) * vectorPxPerUnit(*m_doc) < 1.0)
    return;
  m_pts.push_back(TThickPoint(p, thickness(m_spPressure)));
  updatePreview();
}

// While drawing, the stroke is a polyline (control points = samples).
void VectorStroke::updatePreview() {
  if (m_preview >= 0) {
    delete m_vi->removeStroke(m_preview, false);
    m_preview = -1;
  }
  std::vector<TThickPoint> cp;
  for (size_t i = 0; i < m_pts.size(); i++) {
    if (i) cp.push_back((m_pts[i - 1] + m_pts[i]) * 0.5);
    cp.push_back(m_pts[i]);
  }
  if (cp.size() == 1) cp.push_back(cp[0] + TThickPoint(0.01, 0, 0)), cp.push_back(cp[0] + TThickPoint(0.02, 0, 0));
  TStroke *s = new TStroke(cp);
  s->setStyle(m_doc->currentStyle);
  m_preview = m_vi->addStroke(s, true, false);
}

void VectorStroke::eraseAt(const TPointD &p) {
  double r = m_s.eraserSize * 0.5 / vectorPxPerUnit(*m_doc);
  for (int i = (int)m_vi->getStrokeCount() - 1; i >= 0; i--) {
    TStroke *s = m_vi->getStroke(i);
    double w, d2;
    if (s->getNearestW(p, w, d2) && std::sqrt(d2) <= r + s->getThickPoint(w).thick * 0.5) {
      delete m_vi->removeStroke(i, true);
      m_changed = true;
    }
  }
}

void VectorStroke::end(const TPointD &rp, double pressure) {
  if (!m_valid) return;
  m_valid = false;
  if (m_tool == Tool::Brush) {
    // catch up with the release point, then fit the final curve
    for (int i = 0; i < 6 && m_s.stabilizer > 0; i++) move(rp, pressure);
    TPointD p = toImage(*m_doc, rp);
    if (m_pts.empty() || tdistance(TPointD(m_pts.back().x, m_pts.back().y), p) > 0.5)
      m_pts.push_back(TThickPoint(p, thickness(pressure)));
    if (m_preview >= 0) delete m_vi->removeStroke(m_preview, false), m_preview = -1;
    std::vector<TThickPoint> pts = m_pts;
    if (pts.size() == 1) pts.push_back(pts[0] + TThickPoint(0.5, 0, 0));
    // same fitting as the desktop vector brush (accuracy ~ 20)
    TStroke *s = TStroke::interpolate(pts, 1.0, true);
    if (s) {
      s->setStyle(m_doc->currentStyle);
      m_vi->addStroke(s);
      m_changed = true;
    }
  }
  if (!m_changed) return;
  TUndoManager::manager()->beginBlock();
  if (m_created) pushCellsUndo(m_doc, m_layer, m_cellsBefore, "New drawing");
  pushVectorUndo(m_doc, m_layer, m_drawing, m_before, m_vi,
                 m_tool == Tool::Brush ? "Vector brush" : "Vector eraser");
  TUndoManager::manager()->endBlock();
}

bool vectorAddShape(Document *doc, const ToolSettings &s,
                    const std::vector<TPointD> &rpts, bool closed) {
  Layer *layer;
  int drawing;
  bool created         = false;
  std::vector<int> cb  = doc_cells(doc);
  TVectorImageP vi     = currentVector(doc, &layer, &drawing, true, &created);
  if (!vi || rpts.size() < 2) return false;
  TVectorImageP before = vi->clone();
  std::vector<TThickPoint> pts;
  for (auto &p : rpts) pts.push_back(TThickPoint(toImage(*doc, p), s.brushMax / vectorPxPerUnit(*doc)));
  TStroke *st = TStroke::interpolate(pts, 0.5, true);
  if (!st) return false;
  st->setStyle(doc->currentStyle);
  if (closed) st->setSelfLoop(true);
  vi->addStroke(st);
  TUndoManager::manager()->beginBlock();
  if (created) pushCellsUndo(doc, layer, cb, "New drawing");
  pushVectorUndo(doc, layer, drawing, before, vi, "Shape");
  TUndoManager::manager()->endBlock();
  return true;
}

bool vectorFill(Document *doc, const TPointD &rp, bool emptyOnly) {
  Layer *layer;
  int drawing;
  bool created     = false;
  TVectorImageP vi = currentVector(doc, &layer, &drawing, false, &created);
  if (!vi) return false;
  TVectorImageP before = vi->clone();
  TPointD p            = toImage(*doc, rp);
  int old              = vi->fill(p, doc->currentStyle, emptyOnly);
  if (old < 0) {
    // not inside a region: recolour the stroke under the point (ink fill)
    for (int i = (int)vi->getStrokeCount() - 1; i >= 0; i--) {
      TStroke *s = vi->getStroke(i);
      double w, d2;
      if (s->getNearestW(p, w, d2) && std::sqrt(d2) <= s->getThickPoint(w).thick * 0.5 + 2 / vectorPxPerUnit(*doc)) {
        s->setStyle(doc->currentStyle);
        old = 1;
        break;
      }
    }
  }
  if (old < 0) return false;
  pushVectorUndo(doc, layer, drawing, before, vi, "Vector fill");
  return true;
}

int vectorPick(Document *doc, const TPointD &rp) {
  Layer *layer = doc->layer(doc->currentLayer);
  TVectorImageP vi = doc->image(layer, doc->currentRow);
  if (!vi) return -1;
  TPointD p = toImage(*doc, rp);
  for (int i = (int)vi->getStrokeCount() - 1; i >= 0; i--) {
    TStroke *s = vi->getStroke(i);
    double w, d2;
    if (s->getNearestW(p, w, d2) && std::sqrt(d2) <= s->getThickPoint(w).thick * 0.5 + 2 / vectorPxPerUnit(*doc))
      return s->getStyle();
  }
  if (TRegion *r = vi->getRegion(p)) return r->getStyle();
  return -1;
}

// ---------------------------------------------------------------- selection
bool VectorSelection::select(const std::vector<TPointD> &rpoly) {
  if (m_active) commit();
  Layer *layer;
  int drawing;
  bool created     = false;
  TVectorImageP vi = currentVector(m_doc, &layer, &drawing, false, &created);
  if (!vi || rpoly.size() < 3) return false;
  std::vector<TPointD> poly;
  for (auto &p : rpoly) poly.push_back(toImage(*m_doc, p));
  auto inside = [&](const TPointD &q) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
      if ((poly[i].y > q.y) != (poly[j].y > q.y) &&
          q.x < poly[i].x + (q.y - poly[i].y) * (poly[j].x - poly[i].x) / (poly[j].y - poly[i].y))
        in = !in;
    return in;
  };
  m_idx.clear();
  TRectD b;
  for (UINT i = 0; i < vi->getStrokeCount(); i++) {
    TRectD sb = vi->getStroke(i)->getBBox();
    if (inside((sb.getP00() + sb.getP11()) * 0.5)) {
      m_idx.push_back(i);
      b = m_idx.size() == 1 ? sb : b + sb;
    }
  }
  if (m_idx.empty()) return false;
  m_layerUid = layer->uid;
  m_drawing  = drawing;
  m_vi       = vi;
  m_before   = vi->clone();
  m_applied  = TAffine();
  m_centre   = (b.getP00() + b.getP11()) * 0.5;
  m_active   = true;
  return true;
}

void VectorSelection::apply(const TAffine &a) {
  TAffine delta = a * m_applied.inv();
  for (int i : m_idx) m_vi->getStroke(i)->transform(delta, true);
  m_applied = a;
}

void VectorSelection::setTransform(double dx, double dy, double scale, double angle) {
  if (!m_active) return;
  double k = vectorPxPerUnit(*m_doc);  // drag is in canvas pixels
  apply(TTranslation(m_centre + TPointD(dx / k, dy / k)) * TRotation(angle) *
        TScale(std::max(0.05, std::min(20.0, scale))) * TTranslation(-m_centre));
}

TRectD VectorSelection::bounds() const {
  TRectD b;
  bool first = true;
  if (m_active)
    for (int i : m_idx) {
      TRectD sb = m_vi->getStroke(i)->getBBox();
      b         = first ? sb : b + sb;
      first     = false;
    }
  return imageToRaster(*m_doc) * b;
}

void VectorSelection::commit() {
  if (!m_active) return;
  m_active = false;
  m_vi->findRegions();
  if (Layer *l = m_doc->layerByUid(m_layerUid))
    pushVectorUndo(m_doc, l, m_drawing, m_before, m_vi, "Transform");
}

void VectorSelection::cancel() {
  if (!m_active) return;
  apply(TAffine());
  m_active = false;
}

void VectorSelection::erase() {
  if (!m_active) return;
  apply(TAffine());
  std::vector<int> idx = m_idx;
  std::sort(idx.rbegin(), idx.rend());
  for (int i : idx) delete m_vi->removeStroke(i, false);
  m_vi->findRegions();
  m_active = false;
  if (Layer *l = m_doc->layerByUid(m_layerUid))
    pushVectorUndo(m_doc, l, m_drawing, m_before, m_vi, "Delete");
}

}  // namespace otm
