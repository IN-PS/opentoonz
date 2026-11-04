// OpenToonz Mobile - engine facade used by the JNI bridge and by the tests.
// All coordinates passed to the Engine are *canvas* coordinates (pixels of
// the document, origin top-left, y down); they are converted to OpenToonz
// raster coordinates internally.
#pragma once

#include "compositor.h"
#include "document.h"
#include "tools.h"

#include <memory>
#include <mutex>
#include <string>

namespace otm {

struct StyleInfo {
  int id;
  TPixel32 color;
  std::string name;
};

class Engine {
public:
  Engine();
  ~Engine();

  static void initOpenToonz();  //!< registers TLV/PNG codecs etc. (once)

  // document -----------------------------------------------------------
  void newDocument(int w, int h, int fps);
  Document *doc() const { return m_doc.get(); }
  bool save(const std::string &dir, std::string *error = nullptr);
  bool load(const std::string &dir, std::string *error = nullptr);
  std::string projectDir() const { return m_projectDir; }

  // rendering ----------------------------------------------------------
  //! Renders frame \b row into a top-down RGBA (premultiplied) buffer of
  //! the document size with row stride \b stride bytes; only the canvas rect
  //! [x0,y0,x1,y1] (inclusive) is updated.
  void renderRGBA(int row, uint8_t *dst, int stride, int x0, int y0, int x1,
                  int y1, bool onion, bool background = true);
  //! Same, at arbitrary output size (thumbnail / export scaling).
  void renderScaledRGBA(int row, uint8_t *dst, int w, int h, int stride,
                        bool onion);
  TRaster32P renderFrame(int row, bool onion, bool background = true);

  // tools ----------------------------------------------------------------
  ToolSettings settings;
  Tool tool = Tool::Brush;
  //! returns dirty canvas rect as {x0,y0,x1,y1} (x1<x0 when nothing).
  struct DirtyRect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool empty() const { return x1 < x0 || y1 < y0; }
  };
  DirtyRect pointerDown(double x, double y, double pressure);
  DirtyRect pointerMove(double x, double y, double pressure);
  DirtyRect pointerUp(double x, double y, double pressure = 1.0);
  int pickedStyle() const { return m_picked; }

  Selection &selection() { return *m_selection; }
  bool selectRect(int x0, int y0, int x1, int y1);  // canvas coords
  //! Lasso selection; polygon in canvas coords.
  bool selectPolygon(const std::vector<std::pair<double, double>> &pts);
  //! Line / rectangle / ellipse with the current brush (canvas coords).
  DirtyRect drawShape(int type, double x0, double y0, double x1, double y1);
  void transformSelection(double dx, double dy, double scale, double angle);
  DirtyRect commitSelection();
  DirtyRect cancelSelection();
  DirtyRect deleteSelection();
  //! selection bounds in canvas coords: {x0,y0,x1,y1}; empty if inactive
  DirtyRect selectionBounds() const;

  // undo -----------------------------------------------------------------
  bool undo();
  bool redo();

  // frames ---------------------------------------------------------------
  void setCurrentRow(int row);
  void setCurrentLayer(int index);
  void insertBlankFrame();   //!< new empty drawing after current row
  void duplicateFrame();     //!< copy of current drawing after current row
  void holdFrame();          //!< repeats current drawing on the next row
  void deleteFrame();        //!< removes the current cell, shifts up
  void clearCell();          //!< empties the current cell
  void moveCell(int from, int to);

  // layers ---------------------------------------------------------------
  int addLayer(bool raster = false);
  void deleteLayer(int index);
  void moveLayer(int from, int to);
  void setLayerVisible(int index, bool v);
  void setLayerLocked(int index, bool v);
  void setLayerOpacity(int index, int opacity);
  void renameLayer(int index, const std::string &name);

  // palette -------------------------------------------------------------
  std::vector<StyleInfo> styles() const;
  int addStyle(TPixel32 color);
  void setStyleColor(int id, TPixel32 color);

  // import / export ----------------------------------------------------
  //! Adds an RGBA (non premultiplied, top-down) picture as a drawing of a
  //! full colour layer (new layer when newLayer, else current raster layer)
  //! at the current row (or row+index for sequences). The picture is
  //! scaled to fit the canvas when fit=true.
  bool importRGBA(const uint8_t *rgba, int w, int h, bool newLayer, bool fit,
                  int rowOffset = 0, const std::string &name = "");
  bool importImageFile(const std::string &path, bool newLayer, int rowOffset);
  bool exportGif(const std::string &path, int maxSide, std::string *error);
  bool exportPngSequence(const std::string &dir, std::string *error);

  std::recursive_mutex &mutex() { return m_mutex; }
  unsigned memoryKB() const;  //!< process resident memory (VmRSS)

  // keyframes (layer index, or -1 for the camera) --------------------
  Xform xformAt(int layer, int row) const;
  bool hasKey(int layer, int row) const;
  void setKey(int layer, int row, const Xform &xf, Ease ease);
  void removeKey(int layer, int row);
  //! Interactive editing (transform tool): many live updates, one undo.
  void beginKeyEdit(int layer);
  void setKeyLive(int layer, int row, const Xform &xf, Ease ease);
  void endKeyEdit();
  std::vector<int> keyRows(int layer) const;
  Ease keyEase(int layer, int row) const;

  // timing ---------------------------------------------------------------
  //! Re-times the current layer so that every drawing is exposed \b step
  //! frames ("animate on ones / twos / threes").
  void reframe(int step);
  //! Sets how many frames the drawing at the current row is exposed.
  void setExposure(int frames);

private:
  int m_keyEditLayer = -2;
  KeyMap m_keyEditBefore;
  KeyMap *keysOf(int layer) const;
  TAffine curAff() const;
  TPointD toLayer(double x, double y) const;
  DirtyRect layerToCanvas(const TRect &r) const;
  TPointD toRaster(double x, double y) const;
  DirtyRect toCanvas(const TRect &r) const;

  std::unique_ptr<Document> m_doc;
  Compositor m_compositor;
  std::unique_ptr<StrokeSession> m_stroke;
  std::unique_ptr<Selection> m_selection;
  TRaster32P m_frame;
  int m_picked = -1;
  std::string m_projectDir;
  std::recursive_mutex m_mutex;
};

}  // namespace otm
