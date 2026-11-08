#include "compositor.h"
#include "tools.h"
#include "vector.h"
#include "tvectorimage.h"

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
  if (TVectorImageP vi = img) {  // resolution independent, at any transform
    rasterizeVector(doc, vi, outClip, TTranslation(-clip.x0, -clip.y0) * aff, opacity,
                    colorScale);
    return;
  }
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

// ---------------------------------------------------------------- effects
namespace {
inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void colorAdjust(const TRaster32P &r, const LayerFx &fx) {
  double br = fx.brightness * 2.55, ct = (100.0 + fx.contrast) / 100.0;
  double sat = (100.0 + fx.saturation) / 100.0, h = -fx.hue * M_PI / 180.0;  // +hue: red->green
  // hue rotation matrix in YIQ-like space (luminance preserving)
  double cs = cos(h), sn = sin(h);
  double m[3][3] = {
      {0.299 + 0.701 * cs + 0.168 * sn, 0.587 - 0.587 * cs + 0.330 * sn, 0.114 - 0.114 * cs - 0.497 * sn},
      {0.299 - 0.299 * cs - 0.328 * sn, 0.587 + 0.413 * cs + 0.035 * sn, 0.114 - 0.114 * cs + 0.292 * sn},
      {0.299 - 0.300 * cs + 1.250 * sn, 0.587 - 0.588 * cs - 1.050 * sn, 0.114 + 0.886 * cs - 0.203 * sn}};
  for (int y = 0; y < r->getLy(); y++) {
    TPixel32 *p = r->pixels(y), *e = p + r->getLx();
    for (; p < e; ++p) {
      if (!p->m) continue;
      double a = p->m / 255.0;
      double c[3] = {p->r / a, p->g / a, p->b / a};  // depremultiply
      if (fx.hue) {
        double n[3];
        for (int i = 0; i < 3; i++) n[i] = m[i][0] * c[0] + m[i][1] * c[1] + m[i][2] * c[2];
        c[0] = n[0], c[1] = n[1], c[2] = n[2];
      }
      double lum = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
      for (int i = 0; i < 3; i++) {
        double v = lum + (c[i] - lum) * sat;
        v        = (v - 127.5) * ct + 127.5 + br;
        c[i]     = std::max(0.0, std::min(255.0, v));
      }
      p->r = (int)(c[0] * a + 0.5), p->g = (int)(c[1] * a + 0.5), p->b = (int)(c[2] * a + 0.5);
    }
  }
}

TRaster32P blurred(const TRaster32P &src, int radius) {
  TRaster32P out(src->getSize());
  out->clear();
  TRop::blur(out, src, radius, 0, 0);  // OpenToonz box/gaussian blur
  return out;
}

// silhouette of src (alpha) coloured, shifted by (dx,dy) raster px
TRaster32P silhouette(const TRaster32P &src, TPixel32 col, int opacity, int dx, int dy) {
  TRaster32P out(src->getSize());
  out->clear();
  for (int y = 0; y < src->getLy(); y++) {
    int sy = y - dy;
    if (sy < 0 || sy >= src->getLy()) continue;
    TPixel32 *d = out->pixels(y);
    const TPixel32 *s = src->pixels(sy);
    for (int x = 0; x < src->getLx(); x++) {
      int sx = x - dx;
      if (sx < 0 || sx >= src->getLx()) continue;
      int m = s[sx].m * opacity / 100;
      d[x]  = TPixel32(col.r * m / 255, col.g * m / 255, col.b * m / 255, m);
    }
  }
  return out;
}
}  // namespace

TRaster32P Compositor::fxLayer(const Document &doc, const Layer *layer, int row) {
  TImageP img = doc.image(layer, row);
  TAffine aff = doc.layerAffine(layer, row);
  int op      = (int)std::lround(doc.layerOpacity(layer, row) * 255);
  char k[512];
  snprintf(k, sizeof(k), "%d|%d|%d|%d|%d|%.6f %.6f %.6f %.6f %.6f %.6f|%s", row,
           layer->cell(row), layer->revision, op, doc.paletteSerial, aff.a11, aff.a12,
           aff.a13, aff.a21, aff.a22, aff.a23, layer->fx.key().c_str());
  FxCache &c = m_fxCache[layer->uid];
  if (c.ras && c.key == k) return c.ras;
  TRaster32P buf(doc.size());
  buf->clear();
  if (img) {
    TRect all = buf->getBounds();
    drawImage(doc, img, buf, all, op, nullptr, aff);
  }
  const LayerFx &fx = layer->fx;
  if (fx.brightness || fx.contrast || fx.saturation || fx.hue) colorAdjust(buf, fx);
  if (fx.blur > 0) buf = blurred(buf, fx.blur);
  if (fx.shadowOpacity > 0) {
    TRaster32P sh = silhouette(buf, fx.shadowColor, fx.shadowOpacity, fx.shadowDx, -fx.shadowDy);
    if (fx.shadowBlur > 0) sh = blurred(sh, fx.shadowBlur);
    TRop::over(sh, buf);  // layer over its shadow
    buf = sh;
  }
  if (fx.glow > 0) {
    TRaster32P g = blurred(silhouette(buf, fx.glowColor, 100, 0, 0), fx.glow);
    for (int y = 0; y < buf->getLy(); y++) {  // additive glow
      TPixel32 *d = buf->pixels(y);
      const TPixel32 *s = g->pixels(y);
      for (int x = 0; x < buf->getLx(); x++) {
        int k2 = fx.glowIntensity;
        d[x].r = clamp255(d[x].r + s[x].r * k2 / 100);
        d[x].g = clamp255(d[x].g + s[x].g * k2 / 100);
        d[x].b = clamp255(d[x].b + s[x].b * k2 / 100);
        d[x].m = std::max<int>(d[x].m, clamp255(s[x].m * k2 / 100));
        d[x].r = std::min(d[x].r, d[x].m), d[x].g = std::min(d[x].g, d[x].m),
        d[x].b = std::min(d[x].b, d[x].m);
      }
    }
  }
  c.key = k;
  c.ras = buf;
  return buf;
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
    if (layer->fx.active()) {
      TRaster32P fxr = fxLayer(doc, layer, row);
      TRop::over(outClip, sub(fxr, clip));
    } else
      drawImage(doc, doc.image(layer, row), outClip, clip, op, nullptr, aff);
    if (opt.selection && opt.selection->active() &&
        opt.selection->layerUid() == layer->uid)
      drawFloating(doc, *opt.selection, outClip, clip, aff);
  }
}

}  // namespace otm
