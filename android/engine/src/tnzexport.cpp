#include "tnzexport.h"

#include "projectio.h"

#include "timage_io.h"
#include "tlevel_io.h"
#include "tpalette.h"
#include "trasterimage.h"
#include "tsystem.h"
#include "ttoonzimage.h"
#include "tvectorimage.h"

#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>

namespace otm {

namespace {

std::string num(double v) {
  if (std::abs(v) < 1e-12) v = 0;
  char b[64];
  snprintf(b, sizeof(b), "%.10g", v);
  return b;
}

std::string safeName(const Layer &l, std::set<std::string> &used) {
  std::string s;
  for (char c : l.name) s += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  if (s.empty()) s = "level";
  std::string base = s;
  for (int n = 2; used.count(s); n++) s = base + "_" + std::to_string(n);
  used.insert(s);
  return s;
}

// One animation channel in the TDoubleParam stream format
// (common/tparam/tdoubleparam.cpp, tdoublekeyframe.cpp ::saveData).
struct ChanKey {
  int frame;
  double value;
  Ease ease;
};

void writeChannel(std::ostream &os, const char *tag, double def, double channelDefault,
                  const std::vector<ChanKey> &keys) {
  if (keys.empty() && std::abs(def - channelDefault) < 1e-12) return;
  os << "<" << tag << "><default>" << num(def) << "</default>";
  if (!keys.empty()) {
    os << "<keyframes>";
    bool prevEase = false;
    for (auto &k : keys) {
      const char *t = k.ease == Ease::Constant ? "C" : k.ease == Ease::EaseInOut ? "Ep" : "L";
      os << "<" << t << ">";
      if (prevEase) os << "<prev>" << num(k.value) << " -50</prev>";
      os << k.frame << " " << num(k.value);
      if (k.ease == Ease::EaseInOut) os << " 50";
      os << "</" << t << ">";
      prevEase = k.ease == Ease::EaseInOut;
    }
    os << "</keyframes>";
  }
  os << "</" << tag << ">\n";
}

std::vector<ChanKey> channel(const KeyMap &keys, const std::function<double(const Xform &)> &get) {
  std::vector<ChanKey> r;
  for (auto &kv : keys) r.push_back({kv.first, get(kv.second.xf), kv.second.ease});
  return r;
}

const char *kOutput =
    "<range>0 -1</range><step>1</step><shrink>1</shrink>"
    "<applyShrinkToViewer>0</applyShrinkToViewer>";

}  // namespace

bool exportTnz(Document &doc, const std::string &dirStr, const std::string &sceneName,
               std::vector<std::string> *warnings, std::string *error) {
  auto fail = [&](const std::string &e) {
    if (error) *error = e;
    return false;
  };
  auto warn = [&](const std::string &w) {
    if (warnings) warnings->push_back(w);
  };
  QString dir = QString::fromStdString(dirStr);
  for (const char *sub : {"scenes", "drawings", "extras", "inputs", "outputs", "palettes"})
    if (!QDir().mkpath(dir + "/" + sub)) return fail("cannot create " + dirStr);
  TFilePath dirFp(dir.toStdWString());

  const int W = doc.width(), H = doc.height();
  // Mobile vector layers use Stage units with a 16 inch wide camera.
  const double camW = 16.0, camH = 16.0 * H / W, dpi = W / camW;
  const int frames = doc.frameCount();

  struct Col {
    Layer *layer;
    std::string level, path;
    std::set<int> used;
  };
  std::vector<Col> cols;
  std::set<std::string> names;
  try {
    for (int i = 0; i < doc.layerCount(); i++) {
      Layer *l = doc.layer(i);
      Col c{l, safeName(*l, names), "", {}};
      for (int d : l->cells)
        if (d > 0 && l->drawings.count(d)) c.used.insert(d);
      if (l->type == LayerType::Vector) {
        c.path = "+drawings/" + c.level + ".pli";
        if (!c.used.empty()) {
          TLevelWriterP lw(dirFp + TFilePath("drawings/" + c.level + ".pli"));
          lw->setPalette(doc.palette());
          for (int d : c.used) lw->getFrameWriter(TFrameId(d))->save(doc.drawing(l, d, false));
        }
      } else if (l->type == LayerType::ToonzRaster) {
        c.path = "+drawings/" + c.level + ".tlv";
        if (!c.used.empty()) {
          TLevelWriterP lw(dirFp + TFilePath("drawings/" + c.level + ".tlv"));
          lw->setPalette(doc.palette());
          for (int d : c.used) {
            TToonzImageP ti = doc.drawing(l, d, false);
            if (!ti) continue;
            ti->setDpi(dpi, dpi);
            lw->getFrameWriter(TFrameId(d))->save(ti);
          }
        }
        savePalette(doc.palette(), (dir + "/drawings/" + QString::fromStdString(c.level) + ".tpl")
                                       .toStdString());
      } else {
        c.path = "+extras/" + c.level + "..png";
        for (int d : c.used) {
          TFilePath fp = dirFp + TFilePath("extras/" + c.level + "..png");
          TImageWriter::save(fp.withFrame(TFrameId(d)), doc.drawing(l, d, false));
        }
      }
      cols.push_back(c);
    }
  } catch (const TException &e) {
    return fail("export failed: " + ::to_string(e.getMessage()));
  } catch (...) {
    return fail("export failed (OpenToonz writer exception)");
  }

  std::ostringstream os;
  os << "<tnz framecount=\"" << frames << "\" version=\"71.1\">\n";
  os << "<generator>\"OpenToonz Mobile\"</generator>\n<properties>\n";
  os << "<cameras><camera><cameraSize>" << num(camW) << " " << num(camH) << "</cameraSize>"
     << "<cameraRes>" << W << " " << H << "</cameraRes>"
     << "<cameraXPrevalence>1</cameraXPrevalence><interestRect>0 0 -1 -1</interestRect>"
     << "</camera></cameras>\n";
  os << "<outputs>";
  for (const char *o : {"main", "preview"})
    os << "<output name=\"" << o << "\">" << kOutput << "<fps>" << doc.fps() << "</fps>"
       << "<path>\"+outputs/" << sceneName << ".tif\"</path><bpp>32</bpp></output>";
  os << "</outputs>\n<bgColor>255 255 255 255</bgColor>\n</properties>\n";

  // ids: levels 1..n, columns and their fx after
  int nextId = (int)cols.size() + 1;
  os << "<levelSet><levels>\n";
  for (size_t i = 0; i < cols.size(); i++) {
    const Col &c = cols[i];
    os << "<level id='" << (i + 1) << "'>" << c.level << " ";
    if (c.layer->type == LayerType::Vector)
      os << "<info dpiType=\"image\"/>";
    else
      os << "<info dpix=\"" << num(dpi) << "\" dpiy=\"" << num(dpi) << "\"/>";
    os << "<path>\"" << c.path << "\"</path></level>\n";
  }
  os << "</levels><folder name=\"Cast\" type=\"default\"><levels>";
  for (size_t i = 0; i < cols.size(); i++) os << "<level id='" << (i + 1) << "'/>";
  os << "</levels></folder><folder name=\"Audio\"></folder></levelSet>\n";

  os << "<xsheet><columns>\n";
  std::vector<int> fxIds;
  for (size_t i = 0; i < cols.size(); i++) {
    const Col &c = cols[i];
    Layer *l     = c.layer;
    int colId = nextId++, fxId = nextId++;
    fxIds.push_back(fxId);
    int status = (l->visible ? 0 : 0x1 | 0x2) | (l->locked ? 0x8 : 0);
    os << "<levelColumn id='" << colId << "'><status>" << status << "</status>";
    if (l->opacity < 255) os << "<camerastand_opacity>" << l->opacity << "</camerastand_opacity>";
    os << "<cells>";
    int n = (int)l->cells.size();
    for (int r = 0; r < n;) {
      int d = l->cells[r];
      if (d <= 0 || !c.used.count(d)) {
        r++;
        continue;
      }
      // run with a constant increment (0 = hold), like TXshLevelColumn::saveData
      int cnt = 1, inc = 0;
      if (r + 1 < n && l->cells[r + 1] > 0 && c.used.count(l->cells[r + 1])) {
        inc = l->cells[r + 1] - d;
        cnt = 2;
        while (r + cnt < n && l->cells[r + cnt] == d + cnt * inc && c.used.count(l->cells[r + cnt]))
          cnt++;
      }
      char fid[16];
      snprintf(fid, sizeof(fid), "%04d", d);
      os << "<cell>" << r << " " << cnt << " <level id='" << (i + 1) << "'/>" << fid << " " << inc
         << "</cell>";
      r += cnt;
    }
    os << "</cells>";
    os << "<fx><Toonz_columnFx id='" << fxId << "'><params></params><ports></ports>"
       << "<numberId>0</numberId><name>LevelColumn</name><fxId>\"\"</fxId><opened>0</opened>"
       << "</Toonz_columnFx></fx></levelColumn>\n";
    if (l->fx.active()) warn("layer \"" + l->name + "\": effects are not exported");
  }
  os << "</columns>\n<pegbars>\n";

  // ---- camera (the view is the inverse of its placement, see Document::layerAffine)
  os << "<pegbar activeboth=\"yes\" id=\"Camera1\"><parent handle=\"B\" id=\"Table\" "
        "parentHandle=\"B\"></parent><isOpened>0</isOpened><center>0 0 0 0</center>"
        "<status>0</status>\n";
  {
    const KeyMap &ck = doc.cameraKeys;
    writeChannel(os, "x", 0, 0, channel(ck, [&](const Xform &f) { return f.x / dpi; }));
    writeChannel(os, "y", 0, 0, channel(ck, [&](const Xform &f) { return -f.y / dpi; }));
    writeChannel(os, "sx", 1, 0, {});
    writeChannel(os, "sy", 1, 0, {});
    auto inv = [](double z) { return z > 1e-3 ? 1.0 / z : 1.0; };
    bool zoomAnimated = false;
    for (auto &kv : ck)
      if (std::abs(kv.second.xf.scale - ck.begin()->second.xf.scale) > 1e-9) zoomAnimated = true;
    std::vector<ChanKey> sc;
    if (zoomAnimated) {
      // mobile interpolates the zoom, desktop the camera scale (1/zoom):
      // baked per frame so that both match on every frame
      for (int f = ck.begin()->first; f <= ck.rbegin()->first; f++)
        sc.push_back({f, inv(xformAt(ck, f).scale), Ease::Linear});
    } else
      sc = channel(ck, [&](const Xform &f) { return inv(f.scale); });
    writeChannel(os, "sc", 1, 0, sc);
    writeChannel(os, "rot", 0, 0, channel(ck, [&](const Xform &f) { return -f.rot; }));
  }
  os << "<nodePos>1.234e+09 5.678e+09</nodePos><camera><cameraSize>" << num(camW) << " "
     << num(camH) << "</cameraSize><cameraRes>" << W << " " << H << "</cameraRes>"
     << "<cameraXPrevalence>1</cameraXPrevalence><interestRect>0 0 -1 -1</interestRect>"
     << "</camera></pegbar>\n";
  os << "<pegbar id=\"Table\"><parent handle=\"B\" id=\"None\" parentHandle=\"B\"></parent>"
        "<isOpened>0</isOpened><center>0 0 0 0</center><status>0</status>";
  writeChannel(os, "sx", 1, 0, {});
  writeChannel(os, "sy", 1, 0, {});
  writeChannel(os, "sc", 1, 0, {});
  os << "<nodePos>1.234e+09 5.678e+09</nodePos></pegbar>\n";

  // ---- columns: T(pos) R(ang) S T(-center) in inches, y up, camera centred
  for (size_t i = 0; i < cols.size(); i++) {
    Layer *l = cols[i].layer;
    std::string parent = "Table";
    if (l->parentUid)
      for (size_t j = 0; j < cols.size(); j++)
        if (cols[j].layer->uid == l->parentUid) parent = "Col" + std::to_string(j + 1);
    // the mobile transform (and so its pivot) only exists when there are keys
    bool hasKeys = !l->keys.empty();
    double cx = 0, cy = 0;
    if (hasKeys && l->pivotX >= 0) {
      cx = (l->pivotX - W * 0.5) / dpi;
      cy = (H * 0.5 - l->pivotY) / dpi;
    }
    os << "<pegbar id=\"Col" << (i + 1) << "\"><parent handle=\"B\" id=\"" << parent
       << "\" parentHandle=\"B\"></parent><name>" << cols[i].level << "</name>"
       << "<isOpened>0</isOpened><center>" << num(cx) << " " << num(cy) << " 0 0</center>"
       << "<status>0</status>\n";
    const KeyMap &k = l->keys;
    writeChannel(os, "x", cx, 0, channel(k, [&](const Xform &f) { return cx + f.x / dpi; }));
    writeChannel(os, "y", cy, 0, channel(k, [&](const Xform &f) { return cy - f.y / dpi; }));
    writeChannel(os, "sx", 1, 0, {});
    writeChannel(os, "sy", 1, 0, {});
    writeChannel(os, "sc", 1, 0, channel(k, [&](const Xform &f) { return f.scale; }));
    writeChannel(os, "rot", 0, 0, channel(k, [&](const Xform &f) { return -f.rot; }));
    os << "<nodePos>1.234e+09 5.678e+09</nodePos></pegbar>\n";
    for (auto &kv : k)
      if (std::abs(kv.second.xf.opacity - 1) > 1e-6) {
        warn("layer \"" + l->name + "\": opacity keys are not exported (desktop needs an FX)");
        break;
      }
  }
  os << "</pegbars>\n<fxnodes><terminal>";
  for (int id : fxIds) os << "<fxnode><Toonz_columnFx id='" << id << "'/></fxnode>";
  int xshFx = nextId++, outFx = nextId++;
  os << "</terminal>\n<xsheet><Toonz_xsheetFx id='" << xshFx
     << "'><params></params><ports></ports><numberId>0</numberId><name>Xsheet</name>"
        "<fxId>\"\"</fxId><opened>0</opened></Toonz_xsheetFx></xsheet>\n"
     << "<output><Toonz_outputFx id='" << outFx << "'><params></params><ports><source>"
     << "<Toonz_xsheetFx id='" << xshFx << "'/></source></ports><numberId>0</numberId>"
        "<name>Output</name><fxId>\"\"</fxId><opened>0</opened></Toonz_outputFx></output>\n"
     << "<grid_dimension>1</grid_dimension></fxnodes>\n</xsheet>\n</tnz>\n";

  auto writeFile = [&](const QString &path, const std::string &data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data.data(), (qint64)data.size()) == (qint64)data.size();
  };
  QString scene = dir + "/scenes/" + QString::fromStdString(sceneName) + ".tnz";
  if (!writeFile(scene, os.str())) return fail("cannot write the scene file");
  // project files, so that desktop resolves +drawings / +extras in this folder
  writeFile(dir + "/scenes/scenes.xml",
            "<parentProject type=\"projectFolder\">\n  \"..\"\n</parentProject>\n");
  QString prj = QDir(dir).dirName();
  std::ostringstream pj;
  pj << "<project><version>70 1</version><folders>";
  for (const char *f : {"inputs", "drawings", "scenes", "extras", "outputs", "palettes"})
    pj << "<folder name=\"" << f << "\" path=\"" << f << "\"/>";
  pj << "</folders></project>\n";
  writeFile(dir + "/" + prj + "_otprj.xml", pj.str());
  return true;
}

}  // namespace otm
