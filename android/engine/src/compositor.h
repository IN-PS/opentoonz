// OpenToonz Mobile - CPU frame compositor.
#pragma once

#include "document.h"

#include <map>
#include <string>

namespace otm {

class Selection;

struct RenderOptions {
  bool onion          = false;  // draw onion skin of the current layer
  bool background     = true;   // fill with paper colour (else transparent)
  TPixel32 paper      = TPixel32(255, 255, 255, 255);
  int skipLayerUid    = -1;     // layer not drawn (used while floating)
  TPixel32 backOnion  = TPixel32(255, 70, 70, 255);   // OpenToonz-like
  TPixel32 frontOnion = TPixel32(60, 190, 80, 255);
  const Selection *selection = nullptr;  // floating pixels drawn on top
};

class Compositor {
public:
  //! Renders frame \b row of the document into \b out (size of the document,
  //! OpenToonz orientation: y up, premultiplied BGRM pixels), limited to
  //! \b clip (raster coordinates).
  void render(const Document &doc, int row, const TRaster32P &out,
              const TRect &clip, const RenderOptions &opt);

  //! Draws one image (TToonzImage or TRasterImage) over \b outClip, where
  //! \b outClip is \b out restricted to \b clip.
  void drawImage(const Document &doc, const TImageP &img,
                 const TRaster32P &outClip, const TRect &clip, int opacity,
                 const TPixel32 *colorScale = nullptr,
                 const TAffine &aff = TAffine());

  //! Rows of the drawings used as onion skin around \b row for \b layer:
  //! negative distance = back, positive = forward.
  static std::vector<std::pair<int, int>> onionRows(const Document &doc,
                                                    const Layer *layer,
                                                    int row);

private:
  void drawFloating(const Document &doc, const Selection &sel,
                    const TRaster32P &outClip, const TRect &clip,
                    const TAffine &layerAff);
  TRaster32P m_full;  // full-size buffer for transformed layers
  //! layer with effects: rendered in full, effects applied, cached
  TRaster32P fxLayer(const Document &doc, const Layer *layer, int row);
  struct FxCache {
    std::string key;
    TRaster32P ras;
  };
  std::map<int, FxCache> m_fxCache;  // by layer uid
  TRaster32P tempFor(const TDimension &d);
  TRaster32P m_tmp;
};

}  // namespace otm
