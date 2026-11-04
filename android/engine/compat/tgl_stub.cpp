// OpenToonz Mobile: replacements for the parts of tnzcore's tgl.cpp and
// tofflinegl.cpp that are referenced by the compiled core modules.  Android
// has no desktop OpenGL; the mobile engine renders on the CPU, so these GL
// helpers are no-ops and TOfflineGL is a null back-end producing empty
// rasters (only reached by vector-style preview code paths, which the
// mobile app does not use).
#include "tgl.h"
#include "tofflinegl.h"
#include "tvectorimage.h"
#include "trasterimage.h"

double tglGetPixelSize2() { return 1.0; }
void tglRgbOnlyColorMask() {}
void tglEnableBlending(GLenum, GLenum) {}
void tglEnableLineSmooth(bool, double) {}
void tglGetColorMask(GLboolean &r, GLboolean &g, GLboolean &b, GLboolean &a) {
  r = g = b = a = 1;
}
void tglMultColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}

namespace {
class NullOfflineImp final : public TOfflineGL::Imp {
public:
  NullOfflineImp(int lx, int ly) : Imp(lx, ly) {}
  void makeCurrent() override {}
  void doneCurrent() override {}
  void createContext(TDimension, std::shared_ptr<Imp>) override {}
  void getRaster(TRaster32P r) override {
    if (r) r->clear();
  }
};
}  // namespace

TOfflineGL::TOfflineGL(TDimension dim, const TOfflineGL *)
    : m_imp(std::make_shared<NullOfflineImp>(dim.lx, dim.ly)) {}
TOfflineGL::TOfflineGL(const TRaster32P &raster, const TOfflineGL *)
    : m_imp(std::make_shared<NullOfflineImp>(raster->getLx(),
                                             raster->getLy())) {}
TOfflineGL::~TOfflineGL() {}
void TOfflineGL::makeCurrent() {}
void TOfflineGL::doneCurrent() {}
void TOfflineGL::initMatrix() {}
void TOfflineGL::clear(TPixel32) {}
void TOfflineGL::draw(TVectorImageP, const TVectorRenderData &, bool) {}
void TOfflineGL::draw(TRasterImageP, const TAffine &, bool) {}
void TOfflineGL::flush() {}
TRaster32P TOfflineGL::getRaster() {
  TRaster32P r(getLx(), getLy());
  r->clear();
  return r;
}
void TOfflineGL::getRaster(TRaster32P r) {
  if (r) r->clear();
}
void TOfflineGL::getRaster(TRasterP r) {
  if (r) r->clear();
}
int TOfflineGL::getLx() const { return m_imp->getLx(); }
int TOfflineGL::getLy() const { return m_imp->getLy(); }
TOfflineGL *TOfflineGL::getStock(TDimension dim) {
  static thread_local TOfflineGL *stock = nullptr;
  if (!stock || stock->getLx() != dim.lx || stock->getLy() != dim.ly) {
    delete stock;
    stock = new TOfflineGL(dim);
  }
  return stock;
}
