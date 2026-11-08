// Compatibility tool: creates a REAL OpenToonz scene with the desktop
// toonzlib (built from this repository), used to test the mobile .tnz import.
//   mkscene <output-scene.tnz>
// Scene: camera 1920x1080 (16x9 in), 12 fps;
//   column 1: Toonz Raster level "A" (frames 1,1,2,3,3 -> holds)
//   column 2: vector level "B" (2 frames; frame 2 has a filled region)
//   column 3: raster PNG level "C" (hidden column)
#include "tconvert.h"
#include "tenv.h"
#include "tnzimage.h"
#include "tpalette.h"
#include "tpixelcm.h"
#include "trasterimage.h"
#include "tstroke.h"
#include "tsystem.h"
#include "tthread.h"
#include "ttoonzimage.h"
#include "tvectorimage.h"
#include "toonz/levelset.h"
#include "toutputproperties.h"
#include "toonz/preferences.h"
#include "toonz/sceneproperties.h"
#include "toonz/tcamera.h"
#include "toonz/toonzfolders.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"

#include <QApplication>
#include <iostream>
#include <unistd.h>

static void hline(const TRasterCM32P &r, int x0, int x1, int y, int w, int ink) {
  for (int yy = y - w / 2; yy <= y + w / 2; yy++)
    for (int x = x0; x <= x1; x++)
      if (yy >= 0 && yy < r->getLy()) r->pixels(yy)[x] = TPixelCM32(ink, 0, 0);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: mkscene <scene.tnz>\n";
    return 2;
  }
  QApplication app(argc, argv);
  TThread::init();
  TEnv::setRootVarName("TOONZROOT");
  TEnv::setSystemVarPrefix("TOONZ");
  TEnv::setApplicationFileName(argv[0]);
  if (const char *root = getenv("TOONZROOT"))  // explicit stuff folder
    TEnv::setStuffDir(TFilePath(QString::fromLocal8Bit(root).toStdWString()));
  TEnv::initUserStuffDir();
  initImageIo();
  Preferences::instance();
  for (auto &f : ToonzFolder::getProjectsFolders())
    TProjectManager::instance()->addProjectsRoot(f);
  TProjectManager *pm = TProjectManager::instance();
  pm->createSandboxIfNeeded();
  pm->setCurrentProjectPath(pm->getSandboxProjectPath());

  TFilePath scenePath(QString::fromLocal8Bit(argv[1]).toStdWString());
  ToonzScene scene;
  pm->initializeScene(&scene);
  scene.setScenePath(scenePath);
  TCamera *cam = scene.getCurrentCamera();
  cam->setSize(TDimensionD(16, 9));
  cam->setRes(TDimension(1920, 1080));
  scene.getProperties()->getOutputProperties()->setFrameRate(12);
  TXsheet *xsh = scene.getXsheet();

  // ---- Toonz Raster level A
  TXshSimpleLevel *a = scene.createNewLevel(TZP_XSHLEVEL, L"A", TDimension(1920, 1080), 120)
                           ->getSimpleLevel();
  TPalette *pa = a->getPalette();
  int red      = pa->getPage(0)->getStyleId(pa->getPage(0)->addStyle(TPixel32(230, 30, 30)));
  for (int f = 1; f <= 3; f++) {
    TRasterCM32P ras(1920, 1080);
    ras->clear();
    hline(ras, 300, 1600, 900 - f * 200, 12, f == 3 ? red : 1);  // y-up raster
    TToonzImageP ti(ras, ras->getBounds());
    ti->setDpi(120, 120);
    ti->setPalette(pa);
    a->setFrame(TFrameId(f), ti);
    a->touchFrame(TFrameId(f));
  }
  int seq[] = {1, 1, 2, 3, 3};
  for (int r = 0; r < 5; r++) xsh->setCell(r, 0, TXshCell(a, TFrameId(seq[r])));

  // ---- vector level B
  TXshSimpleLevel *b = scene.createNewLevel(PLI_XSHLEVEL, L"B")->getSimpleLevel();
  TPalette *pb = b->getPalette();
  int blue     = pb->getPage(0)->getStyleId(pb->getPage(0)->addStyle(TPixel32(30, 60, 230)));
  for (int f = 1; f <= 2; f++) {
    TVectorImageP vi = new TVectorImage();
    vi->setPalette(pb);
    // closed square of 300x300 stage units (1 unit = 2.25 px at 1920/16")
    std::vector<TThickPoint> pts = {{-150, -150, 4}, {150, -150, 4}, {150, 150, 4},
                                    {-150, 150, 4}, {-150, -150, 4}};
    std::vector<TThickPoint> cps;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
      cps.push_back(pts[i]);
      cps.push_back((pts[i] + pts[i + 1]) * 0.5);
    }
    cps.push_back(pts.back());
    TStroke *s = new TStroke(cps);
    s->setStyle(1);
    s->setSelfLoop(true);
    vi->addStroke(s);
    if (f == 2) vi->fill(TPointD(0, 0), blue);
    b->setFrame(TFrameId(f), vi);
    b->touchFrame(TFrameId(f));
  }
  for (int r = 0; r < 5; r++) xsh->setCell(r, 1, TXshCell(b, TFrameId(r < 2 ? 1 : 2)));

  // ---- raster PNG level C, hidden column
  TXshSimpleLevel *c =
      scene.createNewLevel(OVL_XSHLEVEL, L"C", TDimension(400, 200), 120,
                           TFilePath("+extras/C..png"))
          ->getSimpleLevel();
  TRaster32P cr(400, 200);
  cr->fill(TPixel32(30, 200, 60, 255));
  TRasterImageP ci(cr);
  ci->setDpi(120, 120);
  c->setFrame(TFrameId(1), ci);
  c->touchFrame(TFrameId(1));
  xsh->setCell(0, 2, TXshCell(c, TFrameId(1)));
  xsh->getColumn(2)->setCamstandVisible(false);

  for (TXshSimpleLevel *l : {a, b, c}) {  // frames set in memory: force save
    l->setDirtyFlag(true);
  }
  try {
    a->save();
    b->save();
    c->save();
    scene.save(scenePath);
  } catch (TException &e) {
    std::cerr << "save failed: " << ::to_string(e.getMessage()) << "\n";
    return 1;
  }
  std::cout << "saved " << ::to_string(scenePath) << "\n";
  std::cout.flush();
  _exit(0);  // skip global destructors (desktop GL/Qt teardown crashes headless)
}
