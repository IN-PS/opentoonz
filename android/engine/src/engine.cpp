#include "engine.h"

#include "gifencoder.h"
#include "projectio.h"
#include "vector.h"
#include "tnzimport.h"
#include "tiio_pli.h"

#include "tfiletype.h"
#include "timage_io.h"
#include "timagecache.h"
#include "tiio.h"
#include "tlevel_io.h"
#include "trasterimage.h"
#include "trop.h"
#include "tsystem.h"
#include "ttoonzimage.h"
#include "tundo.h"
#include "tcolorstyles.h"

#include "tiio_tzl.h"
#include "tiio_png.h"

#include <cmath>
#include <cstdio>
#include <mutex>

namespace otm {

namespace {

class LayerUndo final : public TUndo {
  Document *m_doc;
  std::shared_ptr<Layer> m_layer;
  int m_index;
  bool m_added;

public:
  LayerUndo(Document *doc, std::shared_ptr<Layer> l, int index, bool added)
      : m_doc(doc), m_layer(l), m_index(index), m_added(added) {}
  void remove() const {
    int i = m_doc->indexOfUid(m_layer->uid);
    if (i >= 0) m_doc->detachLayer(i);
  }
  void insert() const {
    m_doc->attachLayer(m_layer, m_index);
    m_doc->currentLayer = m_doc->indexOfUid(m_layer->uid);
  }
  void undo() const override {
    m_added ? remove() : insert();
    m_doc->dirty = true;
  }
  void redo() const override {
    m_added ? insert() : remove();
    m_doc->dirty = true;
  }
  int getSize() const override { return sizeof(*this) + 1024; }
};

class LayerMoveUndo final : public TUndo {
  Document *m_doc;
  int m_from, m_to;

public:
  LayerMoveUndo(Document *d, int f, int t) : m_doc(d), m_from(f), m_to(t) {}
  void undo() const override {
    m_doc->moveLayer(m_to, m_from);
    m_doc->currentLayer = m_from;
  }
  void redo() const override {
    m_doc->moveLayer(m_from, m_to);
    m_doc->currentLayer = m_to;
  }
  int getSize() const override { return sizeof(*this); }
};

class StyleColorUndo final : public TUndo {
  Document *m_doc;
  int m_id;
  TPixel32 m_before, m_after;

public:
  StyleColorUndo(Document *d, int id, TPixel32 b, TPixel32 a)
      : m_doc(d), m_id(id), m_before(b), m_after(a) {}
  void set(TPixel32 c) const {
    if (TColorStyle *s = m_doc->palette()->getStyle(m_id)) s->setMainColor(c);
    m_doc->paletteSerial++;
    m_doc->dirty = true;
  }
  void undo() const override { set(m_before); }
  void redo() const override { set(m_after); }
  int getSize() const override { return sizeof(*this); }
};

TRasterP rasterOf(const TImageP &img) {
  if (TToonzImageP ti = img) return ti->getRaster();
  if (TRasterImageP ri = img) return ri->getRaster();
  return TRasterP();
}

}  // namespace

//=============================================================================

void Engine::initOpenToonz() {
  static std::once_flag once;
  std::call_once(once, [] {
    // Same registrations as toonz/sources/image/tiio.cpp (initImageIo) for
    // the formats the mobile engine uses.
    TLevelWriter::define("pli", TLevelWriterPli::create, false);
    TLevelReader::define("pli", TLevelReaderPli::create);
    TFileType::declare("pli", TFileType::VECTOR_LEVEL);
    TLevelWriter::define("tlv", TLevelWriterTzl::create, false);
    TLevelReader::define("tlv", TLevelReaderTzl::create);
    TFileType::declare("tlv", TFileType::CMAPPED_LEVEL);
    Tiio::defineReaderMaker("png", Tiio::makePngReader);
    Tiio::defineWriterMaker("png", Tiio::makePngWriter, true);
    TFileType::declare("png", TFileType::RASTER_IMAGE);
    Tiio::defineWriterProperties("png", new Tiio::PngWriterProperties());
    TUndoManager::manager()->setUndoMemorySize(64);  // MB, low-RAM devices
  });
}

Engine::Engine() {
  initOpenToonz();
  newDocument(960, 540, 12);
}

Engine::~Engine() {
  TUndoManager::manager()->reset();
}

void Engine::newDocument(int w, int h, int fps) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TUndoManager::manager()->reset();
  m_stroke.reset();
  m_selection.reset();
  m_doc.reset(new Document(w, h, fps));
  m_doc->addLayer(LayerType::ToonzRaster, "Layer 1");
  m_doc->ensureDrawing(m_doc->layer(0), 0);
  m_doc->setMinFrameCount(1);
  m_doc->dirty = false;
  m_selection.reset(new Selection(m_doc.get()));
  m_vsel.reset(new VectorSelection(m_doc.get()));
  m_frame = TRaster32P(m_doc->width(), m_doc->height());
  m_projectDir.clear();
}

bool Engine::save(const std::string &dir, std::string *error) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (hasSelection()) commitSelection();
  bool ok = saveProject(*m_doc, dir, error);
  if (ok) {
    m_doc->dirty = false;
    m_projectDir = dir;
  }
  return ok;
}

bool Engine::load(const std::string &dir, std::string *error) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  std::unique_ptr<Document> d = loadProject(dir, error);
  if (!d) return false;
  TUndoManager::manager()->reset();
  m_stroke.reset();
  m_doc = std::move(d);
  m_selection.reset(new Selection(m_doc.get()));
  m_vsel.reset(new VectorSelection(m_doc.get()));
  m_frame      = TRaster32P(m_doc->width(), m_doc->height());
  m_projectDir = dir;
  m_doc->dirty = false;
  return true;
}

bool Engine::importTnz(const std::string &tnzPath, std::vector<std::string> *warnings,
                       std::string *error) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TnzImportResult r = otm::importTnz(tnzPath);
  if (warnings) *warnings = r.warnings;
  if (!r.doc) {
    if (error) *error = r.error;
    return false;
  }
  TUndoManager::manager()->reset();
  m_stroke.reset();
  m_vstroke.reset();
  m_doc = std::move(r.doc);
  m_selection.reset(new Selection(m_doc.get()));
  m_vsel.reset(new VectorSelection(m_doc.get()));
  m_frame = TRaster32P(m_doc->width(), m_doc->height());
  m_projectDir.clear();
  return true;
}

TPointD Engine::toRaster(double x, double y) const {
  return canvasToRaster(*m_doc, x, y);
}

Engine::DirtyRect Engine::toCanvas(const TRect &r0) const {
  DirtyRect d;
  TRect r = r0 * TRect(m_doc->size());
  if (r.isEmpty()) return d;
  int h = m_doc->height();
  d.x0  = r.x0;
  d.x1  = r.x1;
  d.y0  = h - 1 - r.y1;
  d.y1  = h - 1 - r.y0;
  return d;
}

//-----------------------------------------------------------------------------

TRaster32P Engine::renderFrame(int row, bool onion, bool background) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TRaster32P out(m_doc->width(), m_doc->height());
  RenderOptions opt;
  opt.onion      = onion;
  opt.background = background;
  opt.selection  = m_selection.get();
  m_compositor.render(*m_doc, row, out, out->getBounds(), opt);
  return out;
}

static void copyFlippedRGBA(const TRaster32P &src, uint8_t *dst, int stride,
                            int x0, int y0, int x1, int y1) {
  int h = src->getLy();
  for (int cy = y0; cy <= y1; cy++) {
    const TPixel32 *s = src->pixels(h - 1 - cy) + x0;
    uint8_t *d        = dst + (size_t)cy * stride + (size_t)x0 * 4;
    for (int x = x0; x <= x1; x++, s++, d += 4) {
      d[0] = s->r;
      d[1] = s->g;
      d[2] = s->b;
      d[3] = s->m;
    }
  }
}

void Engine::renderRGBA(int row, uint8_t *dst, int stride, int x0, int y0,
                        int x1, int y1, bool onion, bool background) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  int w = m_doc->width(), h = m_doc->height();
  x0 = std::max(0, x0), y0 = std::max(0, y0);
  x1 = std::min(w - 1, x1), y1 = std::min(h - 1, y1);
  if (x1 < x0 || y1 < y0) return;
  TRect clip(x0, h - 1 - y1, x1, h - 1 - y0);
  RenderOptions opt;
  opt.onion      = onion;
  opt.background = background;
  opt.selection  = m_selection.get();
  m_compositor.render(*m_doc, row, m_frame, clip, opt);
  copyFlippedRGBA(m_frame, dst, stride, x0, y0, x1, y1);
}

void Engine::renderScaledRGBA(int row, uint8_t *dst, int w, int h, int stride,
                              bool onion) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TRaster32P full = renderFrame(row, onion, true);
  TRaster32P out(w, h);
  if (w == full->getLx() && h == full->getLy())
    out = full;
  else {
    out->fill(TPixel32::White);
    double sx = (double)w / full->getLx(), sy = (double)h / full->getLy();
    TRop::resample(out, full, TScale(sx, sy), TRop::Triangle);
  }
  copyFlippedRGBA(out, dst, stride, 0, 0, w - 1, h - 1);
}

//-----------------------------------------------------------------------------

Engine::DirtyRect Engine::pointerDown(double x, double y, double pressure) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TPointD p = toLayer(x, y);
  if (isVectorLayer() && tool != Tool::Select && tool != Tool::Shape &&
      tool != Tool::Transform) {
    if (vselActive()) commitSelection();
    if (tool == Tool::Brush || tool == Tool::Eraser) {
      m_vstroke.reset(new VectorStroke(m_doc.get(), settings, tool));
      if (!m_vstroke->begin(p, pressure)) {
        m_vstroke.reset();
        return DirtyRect();
      }
      return fullCanvas();
    }
    if (tool == Tool::Fill)
      return vectorFill(m_doc.get(), p, settings.fillEmptyOnly) ? fullCanvas() : DirtyRect();
    if (tool == Tool::Picker) {
      m_picked = vectorPick(m_doc.get(), p);
      if (m_picked > 0) m_doc->currentStyle = m_picked;
    }
    return DirtyRect();
  }
  switch (tool) {
  case Tool::Brush:
  case Tool::Eraser: {
    if (hasSelection()) commitSelection();
    m_stroke.reset(new StrokeSession(m_doc.get(), settings, tool));
    TRect r = m_stroke->begin(p, pressure);
    if (!m_stroke->valid()) {
      m_stroke.reset();
      return DirtyRect();
    }
    return layerToCanvas(r);
  }
  case Tool::Fill: {
    if (hasSelection()) commitSelection();
    return layerToCanvas(fillAt(m_doc.get(), settings, p));
  }
  case Tool::Picker: {
    m_picked = pickStyleAt(m_doc.get(), p);
    if (m_picked > 0) m_doc->currentStyle = m_picked;
    return DirtyRect();
  }
  case Tool::Select:
  case Tool::Shape:
  case Tool::Transform:
    return DirtyRect();  // driven by dedicated calls (selection/shape/keys)
  }
  return DirtyRect();
}

Engine::DirtyRect Engine::pointerMove(double x, double y, double pressure) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_vstroke) {
    m_vstroke->move(toLayer(x, y), pressure);
    return fullCanvas();
  }
  if (!m_stroke) return DirtyRect();
  return layerToCanvas(m_stroke->move(toLayer(x, y), pressure));
}

Engine::DirtyRect Engine::pointerUp(double x, double y, double pressure) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_vstroke) {
    m_vstroke->end(toLayer(x, y), pressure);
    m_vstroke.reset();
    return fullCanvas();
  }
  if (!m_stroke) return DirtyRect();
  TRect r = m_stroke->end(toLayer(x, y), pressure);
  m_stroke.reset();
  return layerToCanvas(r);
}

bool Engine::selectRect(int x0, int y0, int x1, int y1) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  if (!curAff().isIdentity() || isVectorLayer())
    return selectPolygon({{(double)x0, (double)y0}, {(double)x1 + 1, (double)y0},
                          {(double)x1 + 1, (double)y1 + 1}, {(double)x0, (double)y1 + 1}});
  int h = m_doc->height();
  return m_selection->lift(TRect(x0, h - 1 - y1, x1, h - 1 - y0));
}

bool Engine::selectPolygon(const std::vector<std::pair<double, double>> &pts) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  std::vector<TPointD> poly;
  for (auto &p : pts) poly.push_back(toLayer(p.first, p.second));
  if (isVectorLayer()) return m_vsel->select(poly);
  return m_selection->liftPolygon(poly);
}

Engine::DirtyRect Engine::drawShape(int type, double x0, double y0, double x1,
                                    double y1) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (hasSelection()) commitSelection();
  auto pts = shapePoints((ShapeType)type, toLayer(x0, y0), toLayer(x1, y1));
  if (pts.size() < 2) return DirtyRect();
  if (isVectorLayer())
    return vectorAddShape(m_doc.get(), settings, pts, type != (int)ShapeType::Line)
               ? fullCanvas() : DirtyRect();
  ToolSettings s = settings;
  s.stabilizer   = 0;
  s.pressure     = false;
  StrokeSession st(m_doc.get(), s, Tool::Brush);
  TRect r = st.begin(pts[0], 1.0);
  if (!st.valid()) return DirtyRect();
  for (size_t i = 1; i + 1 < pts.size(); i++) r += st.move(pts[i], 1.0);
  r += st.end(pts.back(), 1.0);
  return layerToCanvas(r);
}

TAffine Engine::curAff() const {
  return m_doc->layerAffine(m_doc->layer(m_doc->currentLayer), m_doc->currentRow);
}

TPointD Engine::toLayer(double x, double y) const {
  TAffine a = curAff();
  TPointD p = toRaster(x, y);
  return a.isIdentity() ? p : a.inv() * p;
}

Engine::DirtyRect Engine::layerToCanvas(const TRect &r) const {
  TAffine a = curAff();
  if (a.isIdentity() || r.isEmpty()) return toCanvas(r);
  TRectD b = a * TRectD(r.x0, r.y0, r.x1 + 1, r.y1 + 1);
  return toCanvas(TRect(tfloor(b.x0) - 1, tfloor(b.y0) - 1, tceil(b.x1) + 1,
                        tceil(b.y1) + 1));
}

void Engine::transformSelection(double dx, double dy, double scale,
                                double angle) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  // canvas y-down -> raster y-up; canvas clockwise angle -> raster ccw;
  // the drag vector is expressed in the (possibly animated) layer space
  TAffine inv = curAff().inv();
  TPointD d   = inv * TPointD(dx, -dy) - inv * TPointD(0, 0);
  if (m_vsel->active()) {
    m_vsel->setTransform(d.x, d.y, scale, -angle);
    return;
  }
  m_selection->setTransform(d.x, d.y, scale, -angle);
}

Engine::DirtyRect Engine::commitSelection() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_vsel->active()) return m_vsel->commit(), fullCanvas();
  return layerToCanvas(m_selection->commit());
}
Engine::DirtyRect Engine::cancelSelection() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_vsel->active()) return m_vsel->cancel(), fullCanvas();
  return layerToCanvas(m_selection->cancel());
}
Engine::DirtyRect Engine::deleteSelection() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_vsel->active()) return m_vsel->erase(), fullCanvas();
  return layerToCanvas(m_selection->erase());
}

Engine::DirtyRect Engine::selectionBounds() const {
  if (m_vsel->active()) {
    TRectD b = m_vsel->bounds();
    return layerToCanvas(TRect(tfloor(b.x0), tfloor(b.y0), tceil(b.x1), tceil(b.y1)));
  }
  if (!m_selection->active()) return DirtyRect();
  TRectD b = m_selection->bounds();
  return layerToCanvas(TRect(tfloor(b.x0), tfloor(b.y0), tceil(b.x1) - 1,
                             tceil(b.y1) - 1));
}

bool Engine::hasSelection() const { return m_selection->active() || m_vsel->active(); }
bool Engine::vselActive() const { return m_vsel && m_vsel->active(); }

bool Engine::isVectorLayer() const {
  Layer *l = m_doc->layer(m_doc->currentLayer);
  return l && l->type == LayerType::Vector;
}

Engine::DirtyRect Engine::fullCanvas() const {
  DirtyRect d;
  d.x1 = m_doc->width() - 1, d.y1 = m_doc->height() - 1;
  return d;
}

//-----------------------------------------------------------------------------

bool Engine::undo() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_stroke) return false;
  if (hasSelection()) {
    cancelSelection();
    return true;
  }
  bool ok = TUndoManager::manager()->undo();
  m_doc->currentLayer =
      std::max(0, std::min(m_doc->currentLayer, m_doc->layerCount() - 1));
  return ok;
}

bool Engine::redo() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_stroke || m_vstroke || hasSelection()) return false;
  bool ok = TUndoManager::manager()->redo();
  m_doc->currentLayer =
      std::max(0, std::min(m_doc->currentLayer, m_doc->layerCount() - 1));
  return ok;
}

//-----------------------------------------------------------------------------

void Engine::setCurrentRow(int row) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (hasSelection()) commitSelection();
  m_doc->currentRow = std::max(0, row);
  // keep RAM low: drawings far from the current frame are LZ4-compressed
  // inside OpenToonz's TImageCache.
  int margin = std::max(m_doc->onion.back, m_doc->onion.forward) + 2;
  m_doc->compressColdDrawings(m_doc->currentRow - margin,
                              m_doc->currentRow + margin);
}

void Engine::setCurrentLayer(int index) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (hasSelection()) commitSelection();
  if (index >= 0 && index < m_doc->layerCount()) m_doc->currentLayer = index;
}

static Layer *currentLayerOf(Document *d) { return d->layer(d->currentLayer); }

void Engine::insertBlankFrame() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked) return;
  auto before = l->cells;
  int row     = m_doc->currentRow + 1;
  int d       = m_doc->createDrawing(l);
  if (row > (int)l->cells.size()) l->cells.resize(row, 0);
  l->cells.insert(l->cells.begin() + row, d);
  pushCellsUndo(m_doc.get(), l, before, "Insert frame");
  m_doc->currentRow = row;
  m_doc->dirty      = true;
}

void Engine::duplicateFrame() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked) return;
  int cur = l->cell(m_doc->currentRow);
  if (cur == 0) return insertBlankFrame();
  auto before = l->cells;
  int row     = m_doc->currentRow + 1;
  int d       = m_doc->duplicateDrawing(l, cur);
  if (row > (int)l->cells.size()) l->cells.resize(row, 0);
  l->cells.insert(l->cells.begin() + row, d);
  pushCellsUndo(m_doc.get(), l, before, "Duplicate frame");
  m_doc->currentRow = row;
  m_doc->dirty      = true;
}

void Engine::holdFrame() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked) return;
  int cur = l->cell(m_doc->currentRow);
  if (cur == 0) return;
  auto before = l->cells;
  int row     = m_doc->currentRow + 1;
  if (row > (int)l->cells.size()) l->cells.resize(row, 0);
  l->cells.insert(l->cells.begin() + row, cur);
  pushCellsUndo(m_doc.get(), l, before, "Hold frame");
  m_doc->currentRow = row;
  m_doc->dirty      = true;
}

void Engine::deleteFrame() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked) return;
  int row = m_doc->currentRow;
  if (row >= (int)l->cells.size()) return;
  auto before = l->cells;
  l->cells.erase(l->cells.begin() + row);
  while (!l->cells.empty() && l->cells.back() == 0) l->cells.pop_back();
  pushCellsUndo(m_doc.get(), l, before, "Delete frame");
  if (m_doc->currentRow > 0 && m_doc->currentRow >= m_doc->frameCount())
    m_doc->currentRow = m_doc->frameCount() - 1;
  m_doc->dirty = true;
}

void Engine::clearCell() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked) return;
  auto before = l->cells;
  l->setCell(m_doc->currentRow, 0);
  pushCellsUndo(m_doc.get(), l, before, "Clear cell");
  m_doc->dirty = true;
}

void Engine::moveCell(int from, int to) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked || from == to || from < 0 || to < 0) return;
  auto before = l->cells;
  int d       = l->cell(from);
  if (from < (int)l->cells.size()) l->cells.erase(l->cells.begin() + from);
  if (to > (int)l->cells.size()) l->cells.resize(to, 0);
  l->cells.insert(l->cells.begin() + to, d);
  while (!l->cells.empty() && l->cells.back() == 0) l->cells.pop_back();
  pushCellsUndo(m_doc.get(), l, before, "Move frame");
  m_doc->currentRow = to;
  m_doc->dirty      = true;
}

//-----------------------------------------------------------------------------

int Engine::addLayer(bool raster) { return addLayerOfType(raster ? 1 : 0); }

int Engine::addLayerOfType(int type) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  bool raster = type == 1;
  int index = m_doc->currentLayer + 1;
  Layer *l  = m_doc->addLayer((LayerType)std::max(0, std::min(2, type)), "", index);
  index     = m_doc->indexOfUid(l->uid);
  TUndoManager::manager()->beginBlock();
  TUndoManager::manager()->add(
      new LayerUndo(m_doc.get(), m_doc->layerShared(index), index, true));
  if (!raster) {
    auto before = l->cells;
    m_doc->ensureDrawing(l, m_doc->currentRow);
    pushCellsUndo(m_doc.get(), l, before, "New drawing");
  }
  TUndoManager::manager()->endBlock();
  m_doc->currentLayer = index;
  m_doc->dirty        = true;
  return index;
}

void Engine::deleteLayer(int index) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_doc->layerCount() <= 1) return;
  if (hasSelection()) cancelSelection();
  auto l = m_doc->detachLayer(index);
  if (!l) return;
  TUndoManager::manager()->add(new LayerUndo(m_doc.get(), l, index, false));
  m_doc->currentLayer =
      std::max(0, std::min(index, m_doc->layerCount() - 1));
  m_doc->dirty = true;
}

void Engine::moveLayer(int from, int to) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (from == to || from < 0 || to < 0 || from >= m_doc->layerCount() ||
      to >= m_doc->layerCount())
    return;
  m_doc->moveLayer(from, to);
  TUndoManager::manager()->add(new LayerMoveUndo(m_doc.get(), from, to));
  m_doc->currentLayer = to;
  m_doc->dirty        = true;
}

void Engine::setLayerVisible(int i, bool v) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (Layer *l = m_doc->layer(i)) l->visible = v, m_doc->dirty = true;
}
void Engine::setLayerLocked(int i, bool v) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (Layer *l = m_doc->layer(i)) l->locked = v, m_doc->dirty = true;
}
void Engine::setLayerOpacity(int i, int o) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (Layer *l = m_doc->layer(i))
    l->opacity = std::max(0, std::min(255, o)), m_doc->dirty = true;
}
void Engine::renameLayer(int i, const std::string &name) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (Layer *l = m_doc->layer(i)) l->name = name, m_doc->dirty = true;
}

//-----------------------------------------------------------------------------

std::vector<StyleInfo> Engine::styles() const {
  std::vector<StyleInfo> res;
  TPalette *plt = m_doc->palette();
  for (int p = 0; p < plt->getPageCount(); p++) {
    TPalette::Page *page = plt->getPage(p);
    for (int i = 0; i < page->getStyleCount(); i++) {
      int id = page->getStyleId(i);
      if (id == 0) continue;  // transparent "none" style
      TColorStyle *s = plt->getStyle(id);
      res.push_back({id, s->getMainColor(),
                     QString::fromStdWString(s->getName()).toStdString()});
    }
  }
  return res;
}

int Engine::addStyle(TPixel32 color) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TPalette *plt = m_doc->palette();
  int idx       = plt->getPage(0)->addStyle(color);
  int id        = plt->getPage(0)->getStyleId(idx);
  plt->getStyle(id)->setName(L"color_" + std::to_wstring(id));
  m_doc->currentStyle = id;
  m_doc->dirty        = true;
  return id;
}

void Engine::setStyleColor(int id, TPixel32 color) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  TColorStyle *s = m_doc->palette()->getStyle(id);
  if (!s || id == 0) return;
  TPixel32 before = s->getMainColor();
  s->setMainColor(color);
  m_doc->paletteSerial++;
  TUndoManager::manager()->add(
      new StyleColorUndo(m_doc.get(), id, before, color));
  m_doc->dirty = true;
}

//-----------------------------------------------------------------------------

bool Engine::importRGBA(const uint8_t *rgba, int w, int h, bool newLayer,
                        bool fit, int rowOffset, const std::string &name) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (!rgba || w <= 0 || h <= 0) return false;
  TRaster32P src(w, h);
  for (int y = 0; y < h; y++) {
    const uint8_t *s = rgba + (size_t)(h - 1 - y) * w * 4;
    TPixel32 *d      = src->pixels(y);
    for (int x = 0; x < w; x++, s += 4, d++) *d = TPixel32(s[0], s[1], s[2], s[3]);
  }
  TRop::premultiply(src);

  TRaster32P dst(m_doc->width(), m_doc->height());
  dst->clear();
  double sc = 1.0;
  if (fit)
    sc = std::min((double)dst->getLx() / w, (double)dst->getLy() / h);
  TAffine aff = TTranslation((dst->getLx() - w * sc) * 0.5,
                             (dst->getLy() - h * sc) * 0.5) *
                TScale(sc);
  if (sc == 1.0 && w == dst->getLx() && h == dst->getLy())
    dst->copy(src);
  else
    TRop::resample(dst, src, aff, TRop::Triangle);

  TUndoManager::manager()->beginBlock();
  Layer *l = currentLayerOf(m_doc.get());
  if (newLayer || !l || l->type != LayerType::Raster) {
    int index = m_doc->currentLayer + 1;
    l         = m_doc->addLayer(LayerType::Raster, name, index);
    int i = m_doc->indexOfUid(l->uid);
    TUndoManager::manager()->add(
        new LayerUndo(m_doc.get(), m_doc->layerShared(i), i, true));
    m_doc->currentLayer = i;
  }
  auto before = l->cells;
  int d       = m_doc->addDrawing(l, TRasterImageP(dst));
  l->setCell(m_doc->currentRow + rowOffset, d);
  pushCellsUndo(m_doc.get(), l, before, "Import image");
  TUndoManager::manager()->endBlock();
  m_doc->dirty = true;
  return true;
}

bool Engine::importImageFile(const std::string &path, bool newLayer,
                             int rowOffset) {
  TImageP img;
  try {
    TImageReader::load(TFilePath(path), img);
  } catch (...) {
    return false;
  }
  TRasterImageP ri = img;
  if (!ri) return false;
  TRaster32P r = ri->getRaster();
  if (!r) return false;
  // to RGBA top-down, non premultiplied (as Android delivers it)
  std::vector<uint8_t> rgba((size_t)r->getLx() * r->getLy() * 4);
  for (int y = 0; y < r->getLy(); y++) {
    const TPixel32 *s = r->pixels(r->getLy() - 1 - y);
    uint8_t *d        = &rgba[(size_t)y * r->getLx() * 4];
    for (int x = 0; x < r->getLx(); x++, s++, d += 4) {
      d[0] = s->r, d[1] = s->g, d[2] = s->b, d[3] = s->m;
    }
  }
  return importRGBA(rgba.data(), r->getLx(), r->getLy(), newLayer, true,
                    rowOffset, TFilePath(path).getName());
}

bool Engine::exportGif(const std::string &path, int maxSide,
                       std::string *error) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  int w = m_doc->width(), h = m_doc->height();
  double sc = 1.0;
  if (maxSide > 0 && std::max(w, h) > maxSide)
    sc = (double)maxSide / std::max(w, h);
  int ow = std::max(1, (int)std::lround(w * sc)),
      oh = std::max(1, (int)std::lround(h * sc));
  GifEncoder gif;
  if (!gif.open(path, ow, oh, m_doc->fps())) {
    if (error) *error = "cannot write " + path;
    return false;
  }
  std::vector<uint8_t> buf((size_t)ow * oh * 4);
  for (int row = 0; row < m_doc->frameCount(); row++) {
    renderScaledRGBA(row, buf.data(), ow, oh, ow * 4, false);
    gif.addFrame(buf.data());
  }
  return gif.close();
}

bool Engine::exportPngSequence(const std::string &dir, std::string *error) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  try {
    TSystem::mkDir(TFilePath(dir));
  } catch (...) {
  }
  for (int row = 0; row < m_doc->frameCount(); row++) {
    TRaster32P r = renderFrame(row, false, true);
    TFilePath fp = TFilePath(dir) + TFilePath(m_doc->name + "..png");
    fp           = fp.withFrame(TFrameId(row + 1));
    try {
      TImageWriter::save(fp, TRasterImageP(r));
    } catch (...) {
      if (error) *error = "cannot write " + fp.getQString().toStdString();
      return false;
    }
  }
  return true;
}

unsigned Engine::memoryKB() const {
  // resident set size of the process (Linux / Android)
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return 0;
  char line[256];
  unsigned kb = 0;
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "VmRSS: %u kB", &kb) == 1) break;
  fclose(f);
  return kb;
}

//-----------------------------------------------------------------------------
// keyframes

namespace {
class KeysUndo final : public TUndo {
  Document *m_doc;
  int m_uid;  // -1 = camera
  KeyMap m_before, m_after;

public:
  KeysUndo(Document *d, int uid, const KeyMap &b, const KeyMap &a)
      : m_doc(d), m_uid(uid), m_before(b), m_after(a) {}
  void set(const KeyMap &k) const {
    if (m_uid < 0)
      m_doc->cameraKeys = k;
    else if (Layer *l = m_doc->layerByUid(m_uid))
      l->keys = k;
    m_doc->dirty = true;
  }
  void undo() const override { set(m_before); }
  void redo() const override { set(m_after); }
  int getSize() const override {
    return sizeof(*this) + (int)(m_before.size() + m_after.size()) * 64;
  }
};
}  // namespace

KeyMap *Engine::keysOf(int layer) const {
  if (layer < 0) return &m_doc->cameraKeys;
  Layer *l = m_doc->layer(layer);
  return l ? &l->keys : nullptr;
}

Xform Engine::xformAt(int layer, int row) const {
  KeyMap *k = keysOf(layer);
  return k ? otm::xformAt(*k, row) : Xform();
}

bool Engine::hasKey(int layer, int row) const {
  KeyMap *k = keysOf(layer);
  return k && k->count(row);
}

Ease Engine::keyEase(int layer, int row) const {
  KeyMap *k = keysOf(layer);
  if (!k || !k->count(row)) return Ease::EaseInOut;
  return k->at(row).ease;
}

void Engine::setKey(int layer, int row, const Xform &xf, Ease ease) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  KeyMap *k = keysOf(layer);
  if (!k || row < 0) return;
  KeyMap before = *k;
  Xform f       = xf;
  f.scale       = std::max(0.01, std::min(100.0, f.scale));
  f.opacity     = std::max(0.0, std::min(1.0, f.opacity));
  (*k)[row]     = Key{f, ease};
  int uid       = layer < 0 ? -1 : m_doc->layer(layer)->uid;
  TUndoManager::manager()->add(new KeysUndo(m_doc.get(), uid, before, *k));
  m_doc->dirty = true;
}

void Engine::removeKey(int layer, int row) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  KeyMap *k = keysOf(layer);
  if (!k || !k->count(row)) return;
  KeyMap before = *k;
  k->erase(row);
  int uid = layer < 0 ? -1 : m_doc->layer(layer)->uid;
  TUndoManager::manager()->add(new KeysUndo(m_doc.get(), uid, before, *k));
  m_doc->dirty = true;
}

void Engine::beginKeyEdit(int layer) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  KeyMap *k = keysOf(layer);
  if (!k) return;
  m_keyEditLayer  = layer;
  m_keyEditBefore = *k;
}

void Engine::setKeyLive(int layer, int row, const Xform &xf, Ease ease) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  KeyMap *k = keysOf(layer);
  if (!k || row < 0) return;
  Xform f   = xf;
  f.scale   = std::max(0.01, std::min(100.0, f.scale));
  f.opacity = std::max(0.0, std::min(1.0, f.opacity));
  (*k)[row] = Key{f, ease};
  m_doc->dirty = true;
}

void Engine::endKeyEdit() {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  if (m_keyEditLayer == -2) return;
  KeyMap *k = keysOf(m_keyEditLayer);
  int uid   = m_keyEditLayer < 0 ? -1 : m_doc->layer(m_keyEditLayer)->uid;
  if (k) TUndoManager::manager()->add(new KeysUndo(m_doc.get(), uid, m_keyEditBefore, *k));
  m_keyEditLayer = -2;
}

std::vector<int> Engine::keyRows(int layer) const {
  std::vector<int> r;
  if (KeyMap *k = keysOf(layer))
    for (auto &e : *k) r.push_back(e.first);
  return r;
}

//-----------------------------------------------------------------------------
// timing

void Engine::reframe(int step) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  if (!l || l->locked || step < 1 || step > 12) return;
  auto before = l->cells;
  std::vector<int> runs;  // distinct consecutive drawings (0 = blank run)
  for (size_t i = 0; i < l->cells.size(); i++)
    if (i == 0 || l->cells[i] != l->cells[i - 1]) runs.push_back(l->cells[i]);
  std::vector<int> out;
  for (int d : runs) out.insert(out.end(), step, d);
  while (!out.empty() && out.back() == 0) out.pop_back();
  l->cells = out;
  pushCellsUndo(m_doc.get(), l, before, "Reframe");
}

void Engine::setExposure(int frames) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = currentLayerOf(m_doc.get());
  int row  = m_doc->currentRow;
  if (!l || l->locked || frames < 1 || row >= (int)l->cells.size()) return;
  int d = l->cells[row];
  if (d == 0) return;
  int start = row, end = row;
  while (start > 0 && l->cells[start - 1] == d) start--;
  while (end + 1 < (int)l->cells.size() && l->cells[end + 1] == d) end++;
  auto before = l->cells;
  l->cells.erase(l->cells.begin() + start, l->cells.begin() + end + 1);
  l->cells.insert(l->cells.begin() + start, frames, d);
  pushCellsUndo(m_doc.get(), l, before, "Exposure");
  m_doc->currentRow = start;
}

//-----------------------------------------------------------------------------
// peg hierarchy

namespace {
class RigUndo final : public TUndo {
  Document *m_doc;
  int m_uid, m_p0, m_p1;
  double m_x0, m_y0, m_x1, m_y1;

public:
  RigUndo(Document *d, const Layer *l, int p0, double x0, double y0)
      : m_doc(d), m_uid(l->uid), m_p0(p0), m_p1(l->parentUid), m_x0(x0), m_y0(y0),
        m_x1(l->pivotX), m_y1(l->pivotY) {}
  void set(int p, double x, double y) const {
    if (Layer *l = m_doc->layerByUid(m_uid)) l->parentUid = p, l->pivotX = x, l->pivotY = y;
    m_doc->dirty = true;
  }
  void undo() const override { set(m_p0, m_x0, m_y0); }
  void redo() const override { set(m_p1, m_x1, m_y1); }
  int getSize() const override { return sizeof(*this); }
};
}  // namespace

bool Engine::setParent(int layer, int parent) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = m_doc->layer(layer);
  if (!l) return false;
  int puid = 0;
  if (parent >= 0) {
    Layer *p = m_doc->layer(parent);
    if (!p || p == l || m_doc->wouldCycle(l, p->uid)) return false;
    puid = p->uid;
  }
  int p0 = l->parentUid;
  l->parentUid = puid;
  TUndoManager::manager()->add(new RigUndo(m_doc.get(), l, p0, l->pivotX, l->pivotY));
  m_doc->dirty = true;
  return true;
}

int Engine::parentOf(int layer) const {
  Layer *l = m_doc->layer(layer);
  return l && l->parentUid ? m_doc->indexOfUid(l->parentUid) : -1;
}

void Engine::setPivot(int layer, double x, double y) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = m_doc->layer(layer);
  if (!l) return;
  double x0 = l->pivotX, y0 = l->pivotY;
  l->pivotX = x, l->pivotY = y;
  TUndoManager::manager()->add(new RigUndo(m_doc.get(), l, l->parentUid, x0, y0));
  m_doc->dirty = true;
}

TPointD Engine::pivotOf(int layer) const {
  Layer *l = m_doc->layer(layer);
  if (!l || l->pivotX < 0) return TPointD(m_doc->width() * 0.5, m_doc->height() * 0.5);
  return TPointD(l->pivotX, l->pivotY);
}

TPointD Engine::canvasDeltaToLocal(int layer, int row, double dx, double dy) const {
  // frame in which the layer's own key x/y are applied = camera * parent chain
  Layer *l      = m_doc->layer(layer);
  Layer *parent = l && l->parentUid ? m_doc->layerByUid(l->parentUid) : nullptr;
  TAffine frame = m_doc->layerAffine(parent, row);  // parent==null: camera only
  TAffine inv   = frame.inv();
  TPointD d     = inv * TPointD(dx, -dy) - inv * TPointD(0, 0);  // canvas y down
  return TPointD(d.x, -d.y);
}

//-----------------------------------------------------------------------------
// layer effects

namespace {
class FxUndo final : public TUndo {
  Document *m_doc;
  int m_uid;
  LayerFx m_b, m_a;

public:
  FxUndo(Document *d, int uid, const LayerFx &b, const LayerFx &a)
      : m_doc(d), m_uid(uid), m_b(b), m_a(a) {}
  void set(const LayerFx &f) const {
    if (Layer *l = m_doc->layerByUid(m_uid)) l->fx = f;
    m_doc->dirty = true;
  }
  void undo() const override { set(m_b); }
  void redo() const override { set(m_a); }
  int getSize() const override { return sizeof(*this); }
};
}  // namespace

void Engine::setLayerFx(int layer, const LayerFx &fx) {
  std::lock_guard<std::recursive_mutex> lk(m_mutex);
  Layer *l = m_doc->layer(layer);
  if (!l) return;
  LayerFx b = l->fx;
  l->fx     = fx;
  TUndoManager::manager()->add(new FxUndo(m_doc.get(), l->uid, b, fx));
  m_doc->dirty = true;
}

LayerFx Engine::layerFx(int layer) const {
  Layer *l = m_doc->layer(layer);
  return l ? l->fx : LayerFx();
}

}  // namespace otm
