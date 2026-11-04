// OpenToonz Mobile - drawing tools.
//
// The tools reuse the OpenToonz algorithms directly:
//   * Brush / Eraser on Toonz Raster: RasterStrokeGenerator (toonzlib), the
//     same engine used by the desktop "Brush" (pencil / hardness 100) and the
//     "Eraser" (normal) tools;
//   * Fill: fill() / inkFill() from toonzlib/fill.cpp (desktop Fill tool);
//   * Undo: TTileSaverCM32 / TTileSetCM32 (+ FullColor variants) and the
//     TUndoManager, as in the desktop raster tools;
//   * Selection transform: TRop::resample + TRop::over (CM32 aware).
#pragma once

#include "document.h"
#include "tgeometry.h"

#include <memory>

class RasterStrokeGenerator;
class TTileSetCM32;
class TTileSaverCM32;
class TTileSetFullColor;
class TTileSaverFullColor;

namespace otm {

enum class Tool { Brush = 0, Eraser = 1, Fill = 2, Select = 3, Picker = 4 };
enum class FillMode { Areas = 0, Lines = 1, LinesAndAreas = 2 };
enum class EraseMode { LinesAndAreas = 0, Lines = 1, Areas = 2 };

struct ToolSettings {
  double brushMin    = 2.0;   // thickness at zero pressure (pixels)
  double brushMax    = 6.0;   // thickness at full pressure
  bool pressure      = true;
  bool antialias     = true;  // false = "pencil" mode (aliased)
  double eraserSize  = 24.0;
  EraseMode eraseMode = EraseMode::LinesAndAreas;
  FillMode fillMode  = FillMode::Areas;
  int fillDepth      = 10;    // 0..15 "Maximum Gap" like desktop fill depth
  bool fillEmptyOnly = false;
};

//! Records the pixels of one drawing that a tool is about to change, and
//! turns them into an undo/redo record.  Works for CM32 and 32-bit images.
class RasterChange {
public:
  RasterChange(Document *doc, Layer *layer, int drawingNo, const TImageP &img);
  ~RasterChange();
  void save(const TRect &r);   //!< call before modifying pixels inside r
  void commit(const std::string &label);  //!< pushes the undo

private:
  Document *m_doc;
  int m_layerUid, m_drawing;
  TImageP m_img;
  std::unique_ptr<TTileSetCM32> m_cmSet;
  std::unique_ptr<TTileSaverCM32> m_cmSaver;
  std::unique_ptr<TTileSetFullColor> m_fcSet;
  std::unique_ptr<TTileSaverFullColor> m_fcSaver;
};

class StrokeSession {
public:
  StrokeSession(Document *doc, const ToolSettings &s, Tool tool);
  ~StrokeSession();
  //! Coordinates in raster space. Returns the dirty rect (raster coords).
  TRect begin(const TPointD &p, double pressure);
  TRect move(const TPointD &p, double pressure);
  TRect end(const TPointD &p, double pressure);
  bool valid() const { return m_valid; }

private:
  TRect paintPiece(bool last);
  double thickness(double pressure) const;

  Document *m_doc;
  ToolSettings m_s;
  Tool m_tool;
  bool m_valid = false;
  Layer *m_layer = nullptr;
  int m_drawing  = 0;
  TImageP m_img;
  std::unique_ptr<RasterChange> m_change;
  std::unique_ptr<RasterStrokeGenerator> m_gen;
  // full colour layers: stroke rendered as CM32 mask over a snapshot
  TRasterCM32P m_mask;
  TRaster32P m_base;
  TRect m_total;
  bool m_cellCreated = false;
  std::vector<int> m_cellsBefore;
};

//! Fill at raster point; returns dirty rect (empty if nothing happened).
TRect fillAt(Document *doc, const ToolSettings &s, const TPointD &p);

//! Style id at raster point of the current layer/frame (-1 if none).
int pickStyleAt(Document *doc, const TPointD &p);

//! Rectangular selection with a floating, transformable copy of the pixels.
class Selection {
public:
  explicit Selection(Document *doc) : m_doc(doc) {}
  //! Lifts the pixels in rect (raster coords) of the current drawing.
  bool lift(const TRect &rect);
  bool active() const { return m_active; }
  //! Transform relative to the original selection centre.
  void setTransform(double dx, double dy, double scale, double angleDeg);
  TAffine affine() const;
  TRectD bounds() const;  //!< transformed bounds (raster coords)
  TRect originalRect() const { return m_rect; }
  TImageP floating() const { return m_floating; }
  int layerUid() const { return m_layerUid; }
  TRect commit();          //!< pastes & records undo; returns dirty rect
  TRect cancel();          //!< restores the original pixels
  TRect erase();           //!< deletes the selected pixels (undoable)
  double dx = 0, dy = 0, scale = 1, angle = 0;

private:
  Document *m_doc;
  bool m_active = false;
  TRect m_rect;
  int m_layerUid = -1, m_drawing = 0;
  TImageP m_img, m_floating;
  std::unique_ptr<RasterChange> m_change;
};

//! Undo of cell / drawing-structure edits of one layer.
void pushCellsUndo(Document *doc, Layer *layer,
                   const std::vector<int> &before, const std::string &label);

}  // namespace otm
