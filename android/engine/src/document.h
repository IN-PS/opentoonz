// OpenToonz Mobile - document model.
//
// The model follows OpenToonz's xsheet concepts in a reduced form:
//   * a Layer is an xsheet column holding exactly one level;
//   * Layer::cells[row] is the drawing number (TFrameId) exposed at that
//     frame, 0 = empty cell; repeated numbers are "holds" exactly as in an
//     OpenToonz xsheet;
//   * drawings are OpenToonz images: TToonzImage (Toonz Raster, CM32 + shared
//     TPalette) or TRasterImage (full colour, for imported pictures);
//   * images live in OpenToonz's TImageCache, which keeps cold drawings
//     LZ4-compressed in RAM (important on low-memory phones).
#pragma once

#include "tcommon.h"
#include "timage.h"
#include "tpalette.h"
#include "traster.h"
#include "trastercm.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace otm {

enum class LayerType { ToonzRaster = 0, Raster = 1 };

class Layer {
public:
  int uid = 0;
  std::string name;
  LayerType type = LayerType::ToonzRaster;
  bool visible   = true;
  bool locked    = false;
  int opacity    = 255;  // 0..255

  std::vector<int> cells;           // row -> drawing number (0 = empty)
  std::map<int, std::string> drawings;  // drawing number -> TImageCache id
  int nextDrawing = 1;

  int cell(int row) const {
    return (row >= 0 && row < (int)cells.size()) ? cells[row] : 0;
  }
  void setCell(int row, int drawing);
  int usedRows() const;  // index of last non-empty cell + 1
};

struct OnionSettings {
  bool enabled  = false;
  int back      = 2;   // previous drawings shown
  int forward   = 1;   // next drawings shown
  int opacity   = 110;  // 0..255 for the nearest onion frame
  bool tint     = true; // OpenToonz-like red (back) / green (front) tinting
};

class Document {
public:
  Document(int width, int height, int fps);
  ~Document();

  int width() const { return m_w; }
  int height() const { return m_h; }
  TDimension size() const { return TDimension(m_w, m_h); }
  int fps() const { return m_fps; }
  void setFps(int fps) { m_fps = std::max(1, std::min(60, fps)); }

  TPalette *palette() const { return m_palette.getPointer(); }
  void setPalette(TPalette *plt) { m_palette = plt; }

  // layers ------------------------------------------------------------
  int layerCount() const { return (int)m_layers.size(); }
  Layer *layer(int i) const {
    return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i].get() : nullptr;
  }
  std::shared_ptr<Layer> layerShared(int i) const {
    return (i >= 0 && i < (int)m_layers.size()) ? m_layers[i] : nullptr;
  }
  Layer *layerByUid(int uid) const;
  int indexOfUid(int uid) const;
  Layer *addLayer(LayerType type, const std::string &name, int index = -1);
  std::shared_ptr<Layer> detachLayer(int index);
  void attachLayer(std::shared_ptr<Layer> layer, int index);
  void moveLayer(int from, int to);

  // frames ------------------------------------------------------------
  int frameCount() const;  // max used rows over all layers, at least 1
  int minFrameCount() const { return m_minFrames; }
  void setMinFrameCount(int n) { m_minFrames = std::max(1, n); }

  // drawings ----------------------------------------------------------
  //! Image exposed at (layer,row) or empty.  toBeModified=true keeps the
  //! image uncompressed in the cache.
  TImageP image(const Layer *layer, int row, bool toBeModified = false) const;
  TImageP drawing(const Layer *layer, int drawingNo,
                  bool toBeModified = false) const;
  //! Creates a new empty drawing for the layer and returns its number.
  int createDrawing(Layer *layer);
  //! Registers an image as new drawing of the layer (takes ownership).
  int addDrawing(Layer *layer, const TImageP &img);
  void setDrawing(Layer *layer, int drawingNo, const TImageP &img);
  //! Copy of a drawing as a new drawing number.
  int duplicateDrawing(Layer *layer, int drawingNo);
  //! Ensures the cell at (layer,row) holds a drawing that can be painted,
  //! creating one if empty.  Returns the drawing number.
  int ensureDrawing(Layer *layer, int row, bool *created = nullptr);
  //! Removes drawings no longer referenced by any cell (called on save).
  void purgeUnusedDrawings();
  //! Compress every drawing not exposed at rows in [r0,r1].
  void compressColdDrawings(int r0, int r1);

  TImageP newImage(LayerType type) const;

  // state -------------------------------------------------------------
  int currentRow     = 0;
  int currentLayer   = 0;
  int currentStyle   = 1;
  OnionSettings onion;
  bool dirty         = false;
  std::string name   = "Untitled";

  std::string cacheId(const Layer *layer, int drawingNo) const;
  int docId() const { return m_docId; }
  int allocUid() { return ++m_lastUid; }
  int lastUid() const { return m_lastUid; }
  void setLastUid(int u) { m_lastUid = std::max(m_lastUid, u); }

private:
  int m_w, m_h, m_fps;
  int m_minFrames = 1;
  int m_docId;
  int m_lastUid = 0;
  TPaletteP m_palette;
  std::vector<std::shared_ptr<Layer>> m_layers;
};

//! The default palette of a new document: OpenToonz default palette
//! (style 0 transparent, style 1 black) plus a basic colour set.
TPalette *createDefaultPalette();

//! Converts canvas coordinates (origin top-left, y down; as used by the
//! Android UI) to OpenToonz raster coordinates (origin bottom-left, y up).
inline TPointD canvasToRaster(const Document &d, double x, double y) {
  return TPointD(x, d.height() - y);
}

}  // namespace otm
