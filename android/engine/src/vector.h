// OpenToonz Mobile - vector layers (OpenToonz TVectorImage / TStroke).
//
// Desktop OpenToonz draws vector images with OpenGL (tglcurves/tglregions);
// Android has no desktop GL, so this module rasterizes strokes and filled
// regions on the CPU (anti-aliased), at any transform: vector drawings stay
// sharp when a layer or the camera is scaled.
#pragma once

#include "document.h"
#include "tools.h"
#include "tvectorimage.h"

#include <vector>

namespace otm {

//! Draws \b vi over \b out (premultiplied BGRM). \b aff maps image space to
//! \b out pixels. \b colorScale tints like OpenToonz onion skin.
void rasterizeVector(const Document &doc, const TVectorImageP &vi,
                     const TRaster32P &out, const TAffine &aff, int opacity,
                     const TPixel32 *colorScale = nullptr);

//! Freehand vector stroke / stroke eraser on the current vector drawing.
class VectorStroke {
public:
  VectorStroke(Document *doc, const ToolSettings &s, Tool tool);
  bool begin(const TPointD &p, double pressure);  // image (raster) coords
  void move(const TPointD &p, double pressure);
  void end(const TPointD &p, double pressure);
  bool valid() const { return m_valid; }

private:
  double thickness(double pressure) const;
  void updatePreview();
  void eraseAt(const TPointD &p);
  Document *m_doc;
  ToolSettings m_s;
  Tool m_tool;
  bool m_valid = false;
  Layer *m_layer = nullptr;
  int m_drawing = 0;
  TVectorImageP m_vi, m_before;
  std::vector<TThickPoint> m_pts;
  int m_preview = -1;  // index of the temporary stroke
  TPointD m_sp;
  double m_spPressure = 1;
  bool m_created = false, m_changed = false;
  std::vector<int> m_cellsBefore;
};

//! Adds a stroke through \b pts (image coords) with the current style.
bool vectorAddShape(Document *doc, const ToolSettings &s,
                    const std::vector<TPointD> &pts, bool closed);
//! Region fill (OpenToonz TVectorImage::fill); true if something changed.
bool vectorFill(Document *doc, const TPointD &p, bool emptyOnly);
int vectorPick(Document *doc, const TPointD &p);

//! Selection of strokes, transformed live, one undo on commit.
class VectorSelection {
public:
  explicit VectorSelection(Document *doc) : m_doc(doc) {}
  bool select(const std::vector<TPointD> &poly);  // image coords
  bool active() const { return m_active; }
  void setTransform(double dx, double dy, double scale, double angleDeg);
  TRectD bounds() const;
  void commit();
  void cancel();
  void erase();

private:
  void apply(const TAffine &a);
  Document *m_doc;
  bool m_active = false;
  int m_layerUid = -1, m_drawing = 0;
  TVectorImageP m_vi, m_before;
  std::vector<int> m_idx;
  TAffine m_applied;
  TPointD m_centre;
};

//! Undo record: whole vector drawing before / after.
void pushVectorUndo(Document *doc, Layer *layer, int drawing,
                    const TVectorImageP &before, const TVectorImageP &after,
                    const std::string &label);

}  // namespace otm
