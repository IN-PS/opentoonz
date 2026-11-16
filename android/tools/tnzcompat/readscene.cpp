// Compatibility tool: loads a scene with the desktop toonzlib and prints what
// desktop OpenToonz sees: camera, frame rate, columns, level frames, and the
// view matrix (camera^-1 * column placement) of every column on every frame.
// Used to verify the .tnz export of OpenToonz Mobile.
//   readscene <scene.tnz> [matrices-out.txt]
#include "tconvert.h"
#include "tenv.h"
#include "tnzimage.h"
#include "tsystem.h"
#include "tthread.h"
#include "toonz/levelset.h"
#include "toutputproperties.h"
#include "toonz/preferences.h"
#include "toonz/sceneproperties.h"
#include "toonz/tcamera.h"
#include "toonz/toonzfolders.h"
#include "toonz/toonzscene.h"
#include "toonz/tproject.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txshsimplelevel.h"

#include <QApplication>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: readscene <scene.tnz> [matrices-out.txt]\n";
    return 2;
  }
  QApplication app(argc, argv);
  TThread::init();
  TEnv::setRootVarName("TOONZROOT");
  TEnv::setSystemVarPrefix("TOONZ");
  TEnv::setApplicationFileName(argv[0]);
  if (const char *root = getenv("TOONZROOT"))
    TEnv::setStuffDir(TFilePath(QString::fromLocal8Bit(root).toStdWString()));
  TEnv::initUserStuffDir();
  initImageIo();
  Preferences::instance();
  for (auto &f : ToonzFolder::getProjectsFolders())
    TProjectManager::instance()->addProjectsRoot(f);
  TProjectManager::instance()->createSandboxIfNeeded();

  ToonzScene scene;
  try {
    scene.load(TFilePath(QString::fromLocal8Bit(argv[1]).toStdWString()));
  } catch (TException &e) {
    std::cout << "LOAD FAILED: " << ::to_string(e.getMessage()) << "\n";
    return 1;
  } catch (...) {
    std::cout << "LOAD FAILED\n";
    return 1;
  }
  TXsheet *xsh = scene.getXsheet();
  TCamera *cam = scene.getCurrentCamera();
  int frames   = scene.getFrameCount();
  printf("camera %dx%d size %.4fx%.4f fps %.3f frames %d columns %d\n", cam->getRes().lx,
         cam->getRes().ly, cam->getSize().lx, cam->getSize().ly,
         scene.getProperties()->getOutputProperties()->getFrameRate(), frames,
         xsh->getColumnCount());
  for (int c = 0; c < xsh->getColumnCount(); c++) {
    TXshColumn *col = xsh->getColumn(c);
    TStageObject *o = xsh->getStageObject(TStageObjectId::ColumnId(c));
    printf("column %d parent %s visible %d locked %d opacity %d cells", c + 1,
           o->getParent().toString().c_str(), (int)col->isCamstandVisible(),
           (int)col->isLocked(), (int)col->getOpacity());
    TXshSimpleLevel *sl = nullptr;
    for (int r = 0; r < frames; r++) {
      TXshCell cell = xsh->getCell(r, c);
      if (cell.isEmpty())
        printf(" -");
      else {
        printf(" %d", cell.m_frameId.getNumber());
        sl = cell.getSimpleLevel();
      }
    }
    if (sl) {
      // really read the first drawing from disk
      TImageP img = sl->getFrame(sl->getFirstFid(), false);
      printf(" | level %s type %d frames %d firstImage %s", ::to_string(sl->getName()).c_str(),
             sl->getType(), sl->getFrameCount(), img ? "ok" : "MISSING");
    }
    printf("\n");
  }
  if (argc > 2) {
    std::ofstream out(argv[2]);
    char b[256];
    for (int f = 0; f < frames; f++)
      for (int c = 0; c < xsh->getColumnCount(); c++) {
        TAffine a =
            xsh->getCameraAff(f).inv() * xsh->getPlacement(TStageObjectId::ColumnId(c), f);
        snprintf(b, sizeof(b), "%d %d %.6f %.6f %.6f %.6f %.6f %.6f\n", f, c, a.a11, a.a12,
                 a.a13, a.a21, a.a22, a.a23);
        out << b;
      }
  }
  fflush(stdout);
  _exit(0);  // skip global destructors (desktop GL/Qt teardown crashes headless)
}
