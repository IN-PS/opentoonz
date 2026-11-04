#include "compositor.h"
#include "tools.h"

#include "trop.h"
#include "tropcm.h"
#include "trasterimage.h"
#include "ttoonzimage.h"

#include <cmath>
#include <set>

namespace otm {

namespace {
// TRaster::extract() takes the rect by non-const reference (it clips it).
template <class P>
P sub(const P &ras, TRect r) {
  return ras->extract(r);
}
}  // namespace

TRaster32P Compositor::tempFor(const TDimension &d) {
  if (!m_tmp || m_tmp->getLx() < d.lx || m_tmp->getLy() < d.ly)
    m_tmp = TRaster32P(std::max(d.lx, m_tmp ? m_tmp->getLx() : 0),
                       std::max(d.ly, m_tmp ? m_tmp->getLy() : 0));
  return sub(m_tmp, TRect(0, 0, d.lx - 1, d.ly - 1));
}

static void scaleOpacity(const TRaster32P &ras, int opacity) {
  if (opacity >= 255) return;
  for (int y = 0; y < ras->getLy(); y++) {
    TPixel32 *p = ras->pixels(y), *e = p + ras->getLx();
    for (; p < e; ++p) {
      p->r = p->r * opacity / 255;
      p->g = p->g * opacity / 255;
      p->b = p->b * opacity / 255;
      p->m = p->m * opacity / 255;
    }
  }
}

void Compositor::drawImage(const Document &doc, const TImageP &img,
                           const TRaster32P &outClip, const TRect &clip,
                           int opacity, const TPixel32 *colorScale,
                           const TAffine &aff) {
  if (!img || opacity <= 0) return;
  if (!aff.isIdentity()) {
    // animated layer / camera: whole drawing to RGBM, then resampled
    TRasterP src;
    if (TToonzImageP ti = img) src = ti->getRaster();
    else if (TRasterImageP ri = img) src = ri->getRaster();
    if (!src) return;
    if (!m_full || m_full->getSize() != src->getSize())
      m_full = TRaster32P(src->getSize());
    m_full->clear();
    if (TRasterCM32P cm = src)
      TRop::convert(m_full, cm, TPaletteP(doc.palette()), false);
    else
      m_full->copy(src);
    if (colorScale) TRop::applyColorScale(m_full, *colorScale);
    scaleOpacity(m_full, opacity);
    TRop::over(outClip, m_full, TTranslation(-clip.x0, -clip.y0) * aff,
               TRop::Triangle);
    return;
  }
  TRaster32P tmp = tempFor(clip.getSize());
  if (TToonzImageP ti = img) {
    TRasterCM32P cm = ti->getRaster();
    TRect r         = clip * cm->getBounds();
    if (r.isEmpty()) return;
    tmp->clear();
    // OpenToonz Toonz-Raster -> RGBM conversion through the palette.
    TRop::convert(sub(tmp, r - clip.getP00()), sub(cm, r),
                  TPaletteP(doc.palette()), false);
  } else if (TRasterImageP ri = img) {
    TRaster32P r32 = ri->getRaster();
    if (!r32) return;
    TRect r = clip * r32->getBounds();
    if (r.isEmpty()) return;
    tmp->clear();
    sub(tmp, r - clip.getP00())->copy(sub(r32, r));
  } else
    return;

  if (colorScale) TRop::applyColorScale(tmp, *colorScale);  // onion shading
  scaleOpacity(tmp, opacity);
  TRop::over(outClip, tmp);
}

void Compositor::drawFloating(const Document &doc, const Selection &sel,
                              const TRaster32P &outClip, const TRect &clip,
                              const TAffine &layerAff) {
  TImageP img = sel.floating();
  TAffine aff = TTranslation(-clip.x0, -clip.y0) * layerAff * sel.affine();
  TRaster32P tmp = tempFor(clip.getSize());
  tmp->clear();
  if (TToonzImageP ti = img)
    TRop::resample(tmp, ti->getRaster(), TPaletteP(doc.palette()), aff);
  else if (TRasterImageP ri = img)
    TRop::resample(tmp, ri->getRaster(), aff);
  TRop::over(outClip, tmp);
}

std::vector<std::pair<int, int>> Compositor::onionRows(const Document &doc,
                                                       const Layer *layer,
                                                       int row) {
  std::vector<std::pair<int, int>> res;
  if (!layer) return res;
  int cur = layer->cell(row);
  // Back: previous *different* drawings (holds are skipped).
  int last = cur, found = 0;
  for (int r = row - 1; r >= 0 && found < doc.onion.back; r--) {
    int d = layer->cell(r);
    if (d > 0 && d != last) {
      res.push_back({-(++found), r});
      last = d;
    } else if (d == 0)
      last = 0;
  }
  last  = cur;
  found = 0;
  int n = layer->usedRows();
  for (int r = row + 1; r < n && found < doc.onion.forward; r++) {
    int d = layer->cell(r);
    if (d > 0 && d != last) {
      res.push_back({++found, r});
      last = d;
    } else if (d == 0)
      last = 0;
  }
  return res;
}

void Compositor::render(const Document &doc, int row, const TRaster32P &out,
                        const TRect &clipIn, const RenderOptions &opt) {
  TRect clip = clipIn * out->getBounds();
  if (clip.isEmpty()) return;
  TRaster32P outClip = sub(out, clip);
  if (opt.background)
    outClip->fill(opt.paper);
  else
    outClip->clear();

  for (int i = 0; i < doc.layerCount(); i++) {
    const Layer *layer = doc.layer(i);
    if (!layer->visible || layer->uid == opt.skipLayerUid) continue;

    if (opt.onion && doc.onion.enabled && i == doc.currentLayer) {
      auto rows = onionRows(doc, layer, row);
      // farthest first so that nearer drawings are on top
      std::sort(rows.begin(), rows.end(),
                [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                  return std::abs(a.first) > std::abs(b.first);
                });
      for (auto &o : rows) {
        int dist     = std::abs(o.first);
        int maxDist  = o.first < 0 ? doc.onion.back : doc.onion.forward;
        int opacity  = doc.onion.opacity * (maxDist - dist + 1) / maxDist;
        TPixel32 cs  = o.first < 0 ? opt.backOnion : opt.frontOnion;
        cs.m         = 255;
        drawImage(doc, doc.image(layer, o.second), outClip, clip, opacity,
                  doc.onion.tint ? &cs : nullptr,
                  doc.layerAffine(layer, o.second));
      }
    }
    TAffine aff = doc.layerAffine(layer, row);
    int op      = (int)std::lround(doc.layerOpacity(layer, row) * 255);
    drawImage(doc, doc.image(layer, row), outClip, clip, op, nullptr, aff);
    if (opt.selection && opt.selection->active() &&
        opt.selection->layerUid() == layer->uid)
      drawFloating(doc, *opt.selection, outClip, clip, aff);
  }
}

}  // namespace otm
