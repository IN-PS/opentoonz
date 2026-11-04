#include "document.h"

#include "timagecache.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "tcolorstyles.h"

#include <algorithm>
#include <atomic>
#include <set>

namespace otm {

static std::atomic<int> g_docCounter{0};

void Layer::setCell(int row, int drawing) {
  if (row < 0) return;
  if (row >= (int)cells.size()) {
    if (drawing == 0) return;
    cells.resize(row + 1, 0);
  }
  cells[row] = drawing;
  while (!cells.empty() && cells.back() == 0) cells.pop_back();
}

int Layer::usedRows() const { return (int)cells.size(); }

//-----------------------------------------------------------------------------

TPalette *createDefaultPalette() {
  TPalette *plt = new TPalette();  // OpenToonz default: #0 transparent, #1 black
  plt->setPaletteName(L"OpenToonz Mobile");
  TPalette::Page *page = plt->getPage(0);
  const TPixel32 colors[] = {
      TPixel32(255, 255, 255), TPixel32(230, 40, 40),   TPixel32(250, 140, 30),
      TPixel32(250, 220, 40),  TPixel32(60, 170, 70),   TPixel32(40, 120, 220),
      TPixel32(130, 70, 190),  TPixel32(240, 150, 190), TPixel32(150, 95, 60),
      TPixel32(128, 128, 128), TPixel32(250, 215, 180), TPixel32(120, 200, 230)};
  int n = 2;
  for (const TPixel32 &c : colors) {
    int idx = page->addStyle(c);
    plt->getStyle(page->getStyleId(idx))
        ->setName(L"color_" + std::to_wstring(n++));
  }
  return plt;
}

//-----------------------------------------------------------------------------

Document::Document(int width, int height, int fps)
    : m_w(std::max(16, width))
    , m_h(std::max(16, height))
    , m_fps(std::max(1, fps))
    , m_docId(++g_docCounter) {
  m_palette = createDefaultPalette();
}

Document::~Document() {
  for (auto &l : m_layers)
    for (auto &d : l->drawings) TImageCache::instance()->remove(d.second);
}

std::string Document::cacheId(const Layer *layer, int drawingNo) const {
  return "otm:" + std::to_string(m_docId) + ":" + std::to_string(layer->uid) +
         ":" + std::to_string(drawingNo);
}

Layer *Document::layerByUid(int uid) const {
  for (auto &l : m_layers)
    if (l->uid == uid) return l.get();
  return nullptr;
}

int Document::indexOfUid(int uid) const {
  for (int i = 0; i < (int)m_layers.size(); i++)
    if (m_layers[i]->uid == uid) return i;
  return -1;
}

Layer *Document::addLayer(LayerType type, const std::string &name, int index) {
  auto l  = std::make_shared<Layer>();
  l->uid  = allocUid();
  l->type = type;
  l->name = name.empty() ? (type == LayerType::Raster ? "Image" : "Layer") +
                               std::string(" ") + std::to_string(l->uid)
                         : name;
  attachLayer(l, index);
  return l.get();
}

std::shared_ptr<Layer> Document::detachLayer(int index) {
  if (index < 0 || index >= (int)m_layers.size()) return nullptr;
  auto l = m_layers[index];
  m_layers.erase(m_layers.begin() + index);
  if (currentLayer >= (int)m_layers.size())
    currentLayer = std::max(0, (int)m_layers.size() - 1);
  return l;
}

void Document::attachLayer(std::shared_ptr<Layer> layer, int index) {
  if (index < 0 || index > (int)m_layers.size()) index = (int)m_layers.size();
  m_layers.insert(m_layers.begin() + index, layer);
}

void Document::moveLayer(int from, int to) {
  if (from < 0 || from >= (int)m_layers.size()) return;
  to     = std::max(0, std::min((int)m_layers.size() - 1, to));
  auto l = m_layers[from];
  m_layers.erase(m_layers.begin() + from);
  m_layers.insert(m_layers.begin() + to, l);
}

int Document::frameCount() const {
  int n = m_minFrames;
  for (auto &l : m_layers) n = std::max(n, l->usedRows());
  return std::max(1, n);
}

TImageP Document::newImage(LayerType type) const {
  if (type == LayerType::ToonzRaster) {
    TRasterCM32P ras(m_w, m_h);
    ras->clear();  // TPixelCM32() = ink 0, paint 0, tone 255 (empty)
    TToonzImageP ti(ras, ras->getBounds());
    ti->setDpi(120, 120);
    ti->setPalette(m_palette.getPointer());
    return ti;
  }
  TRaster32P ras(m_w, m_h);
  ras->clear();
  TRasterImageP ri(ras);
  ri->setDpi(120, 120);
  return ri;
}

TImageP Document::drawing(const Layer *layer, int drawingNo,
                          bool toBeModified) const {
  if (!layer || drawingNo <= 0) return TImageP();
  auto it = layer->drawings.find(drawingNo);
  if (it == layer->drawings.end()) return TImageP();
  TImageP img = TImageCache::instance()->get(it->second, toBeModified);
  if (TToonzImageP ti = img) ti->setPalette(m_palette.getPointer());
  return img;
}

TImageP Document::image(const Layer *layer, int row, bool toBeModified) const {
  if (!layer) return TImageP();
  return drawing(layer, layer->cell(row), toBeModified);
}

int Document::addDrawing(Layer *layer, const TImageP &img) {
  int no = layer->nextDrawing++;
  std::string id      = cacheId(layer, no);
  layer->drawings[no] = id;
  TImageCache::instance()->add(id, img, true);
  return no;
}

void Document::setDrawing(Layer *layer, int drawingNo, const TImageP &img) {
  std::string id            = cacheId(layer, drawingNo);
  layer->drawings[drawingNo] = id;
  layer->nextDrawing         = std::max(layer->nextDrawing, drawingNo + 1);
  TImageCache::instance()->add(id, img, true);
}

int Document::createDrawing(Layer *layer) {
  return addDrawing(layer, newImage(layer->type));
}

int Document::duplicateDrawing(Layer *layer, int drawingNo) {
  TImageP src = drawing(layer, drawingNo, false);
  if (!src) return createDrawing(layer);
  return addDrawing(layer, src->cloneImage());
}

int Document::ensureDrawing(Layer *layer, int row, bool *created) {
  int d = layer->cell(row);
  if (created) *created = false;
  if (d > 0 && layer->drawings.count(d)) return d;
  d = createDrawing(layer);
  layer->setCell(row, d);
  if (created) *created = true;
  return d;
}

void Document::purgeUnusedDrawings() {
  for (auto &l : m_layers) {
    std::set<int> used(l->cells.begin(), l->cells.end());
    for (auto it = l->drawings.begin(); it != l->drawings.end();) {
      if (!used.count(it->first)) {
        TImageCache::instance()->remove(it->second);
        it = l->drawings.erase(it);
      } else
        ++it;
    }
  }
}

void Document::compressColdDrawings(int r0, int r1) {
  for (auto &l : m_layers) {
    std::set<int> hot;
    for (int r = std::max(0, r0); r <= r1; r++) hot.insert(l->cell(r));
    for (auto &d : l->drawings)
      if (!hot.count(d.first)) TImageCache::instance()->compress(d.second);
  }
}

}  // namespace otm
