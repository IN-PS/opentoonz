#include "tnzimport.h"

#include "vector.h"

#include "tcolorstyles.h"
#include "timage_io.h"
#include "tlevel_io.h"
#include "tpalette.h"
#include "tpixelcm.h"
#include "trasterimage.h"
#include "tregion.h"
#include "trop.h"
#include "tstroke.h"
#include "tsystem.h"
#include "ttoonzimage.h"
#include "tvectorimage.h"
#include "tdoublekeyframe.h"
#include "tdoubleparam.h"
#include "tconvert.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace otm {

// ---------------------------------------------------------------- mini XML
namespace {

struct XNode {
  std::string tag;                           // empty = text token
  std::string text;                          // token (quotes removed)
  std::map<std::string, std::string> attr;
  std::vector<std::unique_ptr<XNode>> kids;  // in document order

  const XNode *child(const std::string &t) const {
    for (auto &k : kids)
      if (k->tag == t) return k.get();
    return nullptr;
  }
  std::vector<const XNode *> children(const std::string &t = "") const {
    std::vector<const XNode *> r;
    for (auto &k : kids)
      if (!k->tag.empty() && (t.empty() || k->tag == t)) r.push_back(k.get());
    return r;
  }
  std::vector<std::string> tokens() const {
    std::vector<std::string> r;
    for (auto &k : kids)
      if (k->tag.empty()) r.push_back(k->text);
    return r;
  }
};

class XParser {
  const std::string &s;
  size_t i = 0;

  void ws() {
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
  }
  std::string ident() {
    size_t b = i;
    while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '>' && s[i] != '/' &&
           s[i] != '=')
      i++;
    return s.substr(b, i - b);
  }
  std::string quoted() {  // TOStream escapes \" and \\ inside strings
    char q = s[i++];
    std::string r;
    while (i < s.size() && s[i] != q) {
      if (s[i] == '\\' && i + 1 < s.size()) i++;
      r += s[i++];
    }
    i++;
    return r;
  }

public:
  explicit XParser(const std::string &src) : s(src) {}

  // parses children of `parent` until </endTag> (or EOF)
  bool parse(XNode &parent, const std::string &endTag) {
    for (;;) {
      ws();
      if (i >= s.size()) return endTag.empty();
      if (s[i] == '<') {
        if (s.compare(i, 4, "<!--") == 0) {
          size_t e = s.find("-->", i);
          i        = e == std::string::npos ? s.size() : e + 3;
          continue;
        }
        if (s.compare(i, 2, "<?") == 0) {
          size_t e = s.find("?>", i);
          i        = e == std::string::npos ? s.size() : e + 2;
          continue;
        }
        if (s[i + 1] == '/') {  // end tag
          i += 2;
          std::string t = ident();
          ws();
          if (i < s.size() && s[i] == '>') i++;
          return t == endTag;
        }
        i++;
        auto n = std::make_unique<XNode>();
        n->tag = ident();
        for (;;) {
          ws();
          if (i >= s.size()) return false;
          if (s[i] == '/') {  // <tag ... />
            i += 2;
            parent.kids.push_back(std::move(n));
            break;
          }
          if (s[i] == '>') {
            i++;
            if (!parse(*n, n->tag)) return false;
            parent.kids.push_back(std::move(n));
            break;
          }
          std::string a = ident();
          ws();
          std::string v;
          if (i < s.size() && s[i] == '=') {
            i++;
            ws();
            v = (s[i] == '"' || s[i] == '\'') ? quoted() : ident();
          }
          n->attr[a] = v;
        }
      } else {
        auto t = std::make_unique<XNode>();
        if (s[i] == '"')
          t->text = quoted();
        else {
          size_t b = i;
          while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '<') i++;
          t->text = s.substr(b, i - b);
        }
        parent.kids.push_back(std::move(t));
      }
    }
  }
};

int toInt(const std::string &s, int d = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return d;
  }
}
double toDouble(const std::string &s, double d = 0) {
  try {
    return std::stod(s);
  } catch (...) {
    return d;
  }
}

struct LevelRec {
  std::string id, name, path, kind;  // kind: level / childLevel / ...
  double dpix = 0, dpiy = 0;
};

// Resolves "+drawings/A.tlv", "$scenefolder/A.tlv", absolute or relative
// paths against the scene's folder / project.
QString resolveLevelPath(const QString &sceneDir, const std::string &p0) {
  QString p = QString::fromStdString(p0);
  QStringList cands;
  if (p.startsWith("+")) {
    int slash     = p.indexOf('/');
    QString alias = p.mid(1, slash - 1), rest = p.mid(slash + 1);
    for (QString base : {sceneDir + "/../" + alias, sceneDir + "/" + alias, sceneDir + "/..",
                         sceneDir})
      cands << base + "/" + rest;
  } else if (p.startsWith("$scenefolder")) {
    cands << sceneDir + p.mid(QString("$scenefolder").size());
  } else if (QFileInfo(p).isAbsolute()) {
    cands << p << sceneDir + "/" + QFileInfo(p).fileName();
  } else
    cands << sceneDir + "/" + p;
  for (const QString &c : cands) {
    QString clean = QDir::cleanPath(c);
    // sequence levels "A..png" exist as "A.0001.png": test the folder
    if (clean.contains("..")) {
      QFileInfo fi(clean);
      QString stem = fi.fileName().section("..", 0, 0);
      if (QDir(fi.absolutePath()).entryList({stem + ".*"}).size() > 0) return clean;
    } else if (QFile::exists(clean))
      return clean;
  }
  return QString();
}

// Adds the styles of a level palette to the document palette, returning the
// id remapping (same id + same colour = shared).
std::map<int, int> mergePalette(TPalette *doc, TPalette *lev, bool first) {
  std::map<int, int> map;
  if (!lev) return map;
  for (int p = 0; p < lev->getPageCount(); p++) {
    TPalette::Page *page = lev->getPage(p);
    for (int k = 0; k < page->getStyleCount(); k++) {
      int id          = page->getStyleId(k);
      TColorStyle *ls = lev->getStyle(id);
      if (id == 0 || !ls) {
        map[id] = id;
        continue;
      }
      TColorStyle *ds = doc->getStyle(id);
      bool exists     = id < doc->getStyleCount() && ds && ds->getMainColor() == ls->getMainColor();
      if (first && id < doc->getStyleCount() && ds) {
        ds->setMainColor(ls->getMainColor());
        ds->setName(ls->getName());
        exists = true;
      }
      if (exists) {
        map[id] = id;
        continue;
      }
      int idx = doc->getPage(0)->addStyle(ls->getMainColor());
      int nid = doc->getPage(0)->getStyleId(idx);
      doc->getStyle(nid)->setName(ls->getName());
      map[id] = nid;
    }
  }
  return map;
}

void remapCM32(const TRasterCM32P &r, const std::map<int, int> &m) {
  for (int y = 0; y < r->getLy(); y++) {
    TPixelCM32 *p = r->pixels(y);
    for (int x = 0; x < r->getLx(); x++, p++) {
      int ink = p->getInk(), paint = p->getPaint();
      auto a = m.find(ink), b = m.find(paint);
      int ni = a != m.end() ? a->second : ink, np = b != m.end() ? b->second : paint;
      if (ni != ink || np != paint) *p = TPixelCM32(ni, np, p->getTone());
    }
  }
}

void remapRegions(TRegion *r, const std::map<int, int> &m) {
  auto it = m.find(r->getStyle());
  if (it != m.end() && it->second != r->getStyle()) r->setStyle(it->second);
  for (UINT i = 0; i < r->getSubregionCount(); i++) remapRegions(r->getSubregion(i), m);
}

}  // namespace

// ---------------------------------------------------------------- pegbars
// A stage object ("pegbar") of the scene: its channels are real OpenToonz
// TDoubleParam objects rebuilt from the file (tdoublekeyframe.cpp format),
// so every desktop interpolation type is evaluated by the original code.
struct Peg {
  std::string id, parent;
  TPointD center, offset;  // inches
  TDoubleParamP x, y, sx, sy, sc, rot;
  bool unsupported = false;  // expression / file / path / shear
  bool has(const TDoubleParamP &p) const { return p && p->getKeyframeCount() > 0; }
  bool animated() const { return has(x) || has(y) || has(sx) || has(sy) || has(sc) || has(rot); }
  bool isDefault() const {
    return !animated() && std::abs(x->getDefaultValue()) < 1e-9 &&
           std::abs(y->getDefaultValue()) < 1e-9 && std::abs(rot->getDefaultValue()) < 1e-9 &&
           std::abs(sx->getDefaultValue() - 1) < 1e-9 && std::abs(sy->getDefaultValue() - 1) < 1e-9 &&
           std::abs(sc->getDefaultValue() - 1) < 1e-9 && norm(center) < 1e-9 && norm(offset) < 1e-9;
  }
  // TStageObject::computeLocalPlacement, in pixels (camera centred, y up)
  TAffine local(double f, double dpi) const {
    double s = sc->getValue(f);
    return TTranslation((TPointD(x->getValue(f), y->getValue(f)) + offset) * dpi) *
           TRotation(rot->getValue(f)) * TScale(s * sx->getValue(f), s * sy->getValue(f)) *
           TTranslation(-(center * dpi));
  }
  void frames(std::set<int> &out) const {
    for (const TDoubleParamP &p : {x, y, sx, sy, sc, rot})
      for (int i = 0; i < p->getKeyframeCount(); i++)
        out.insert((int)std::lround(p->getKeyframe(i).m_frame));
  }
  TDoubleKeyframe::Type typeAt(int frame) const {
    for (const TDoubleParamP &p : {x, y, rot, sc, sx, sy})
      for (int i = p->getKeyframeCount() - 1; i >= 0; i--)
        if (p->getKeyframe(i).m_frame <= frame + 1e-6) return p->getKeyframe(i).m_type;
    return TDoubleKeyframe::Linear;
  }
};

TDoubleParamP readChannel(const XNode *n, double def, bool *unsupported) {
  TDoubleParamP p(new TDoubleParam(def));
  if (!n) return p;
  if (const XNode *d = n->child("default")) {
    auto t = d->tokens();
    if (!t.empty()) p->setDefaultValue(toDouble(t[0], def));
  }
  const XNode *kf = n->child("keyframes");
  if (!kf) return p;
  for (const XNode *k : kf->children()) {
    TDoubleKeyframe key;
    key.m_isKeyframe = true;
    auto t = k->tokens();
    if (const XNode *prev = k->child("prev")) {
      auto pt = prev->tokens();
      if (pt.size() > 1) key.m_speedIn.x = toDouble(pt[1]);
      if (pt.size() > 2) key.m_speedIn.y = toDouble(pt[2]);
    }
    if (k->attr.count("lnk") && k->attr.at("lnk") == "no") key.m_linkedHandles = false;
    if (t.size() < 2) continue;
    key.m_frame = toDouble(t[0]);
    key.m_value = toDouble(t[1]);
    if (k->tag == "L")
      key.m_type = TDoubleKeyframe::Linear;
    else if (k->tag == "C")
      key.m_type = TDoubleKeyframe::Constant;
    else if (k->tag == "Exp")
      key.m_type = TDoubleKeyframe::Exponential;
    else if (k->tag == "S") {
      key.m_type = TDoubleKeyframe::SpeedInOut;
      if (t.size() > 3) key.m_speedOut = TPointD(toDouble(t[2]), toDouble(t[3]));
    } else if (k->tag == "E" || k->tag == "Ep") {
      key.m_type = k->tag == "E" ? TDoubleKeyframe::EaseInOut
                                 : TDoubleKeyframe::EaseInOutPercentage;
      if (t.size() > 2) key.m_speedOut.x = toDouble(t[2]);
    } else {  // expression, file, similar shape
      *unsupported = true;
      continue;
    }
    p->setKeyframe(key);
  }
  return p;
}

// T(pos) R(ang) S(s) T(-centre) -> mobile transform about that centre.
// Returns false when the matrix is not a rotation + uniform scale.
bool decompose(const TAffine &m, const TPointD &centerPx, Xform &out) {
  double s1 = std::sqrt(m.a11 * m.a11 + m.a21 * m.a21);
  double s2 = std::sqrt(m.a12 * m.a12 + m.a22 * m.a22);
  double det = m.a11 * m.a22 - m.a12 * m.a21;
  bool ok = det > 0 && std::abs(s1 - s2) < 1e-3 * std::max(1.0, s1) &&
            std::abs(m.a11 * m.a12 + m.a21 * m.a22) < 1e-3 * std::max(1.0, s1 * s2);
  TPointD pos = m * centerPx;
  out.x       = pos.x - centerPx.x;
  out.y       = -(pos.y - centerPx.y);
  out.scale   = std::sqrt(std::abs(det));
  out.rot     = -std::atan2(m.a21, m.a11) * 180.0 / M_PI;
  return ok;
}

// ---------------------------------------------------------------- import
TnzImportResult importTnz(const std::string &tnzPath) {
  TnzImportResult res;
  QFile f(QString::fromStdString(tnzPath));
  if (!f.open(QIODevice::ReadOnly)) {
    res.error = "cannot open " + tnzPath;
    return res;
  }
  std::string src = f.readAll().toStdString();
  XNode root;
  if (!XParser(src).parse(root, "") || !root.child("tnz")) {
    res.error = "not an OpenToonz scene (.tnz)";
    return res;
  }
  const XNode *tnz = root.child("tnz");
  QString sceneDir = QFileInfo(QString::fromStdString(tnzPath)).absolutePath();

  // camera / fps
  int W = 1920, H = 1080, fps = 24;
  double camInchW = 16;
  if (const XNode *props = tnz->child("properties")) {
    if (const XNode *cams = props->child("cameras"))
      if (const XNode *cam = cams->child("camera")) {
        if (const XNode *r = cam->child("cameraRes")) {
          auto t = r->tokens();
          if (t.size() >= 2) W = toInt(t[0], W), H = toInt(t[1], H);
        }
        if (const XNode *sz = cam->child("cameraSize")) {
          auto t = sz->tokens();
          if (!t.empty()) camInchW = toDouble(t[0], 16);
        }
      }
    if (const XNode *outs = props->child("outputs"))
      for (const XNode *o : outs->children("output"))
        if (o->attr.count("name") && o->attr.at("name") == "main")
          if (const XNode *fp = o->child("fps")) {
            auto t = fp->tokens();
            if (!t.empty()) fps = (int)std::lround(toDouble(t[0], 24));
          }
  }
  double camDpi = W / std::max(0.1, camInchW);

  // level set
  std::map<std::string, LevelRec> levels;
  if (const XNode *ls = tnz->child("levelSet"))
    if (const XNode *lv = ls->child("levels"))
      for (const XNode *l : lv->children()) {
        LevelRec r;
        r.kind = l->tag;
        r.id   = l->attr.count("id") ? l->attr.at("id") : "";
        auto tk = l->tokens();
        if (!tk.empty()) r.name = tk[0];
        if (const XNode *info = l->child("info")) {
          if (info->attr.count("dpix")) r.dpix = toDouble(info->attr.at("dpix"));
          if (info->attr.count("dpiy")) r.dpiy = toDouble(info->attr.at("dpiy"));
        }
        if (const XNode *p = l->child("path")) {
          auto t = p->tokens();
          if (!t.empty()) r.path = t[0];
        }
        levels[r.id] = r;
      }

  std::unique_ptr<Document> doc(new Document(W, H, fps));
  doc->name = QFileInfo(QString::fromStdString(tnzPath)).completeBaseName().toStdString();
  bool firstPalette = true;
  std::map<std::string, std::map<int, int>> levelStyleMap;
  std::map<std::string, std::map<int, TImageP>> loaded;  // level id -> fid -> image
  std::map<std::string, LayerType> levelType;
  std::set<std::string> failed;

  auto loadLevel = [&](const LevelRec &lr) -> bool {
    if (loaded.count(lr.id)) return true;
    if (failed.count(lr.id)) return false;
    auto fail = [&](const std::string &why) {
      res.warnings.push_back("level \"" + lr.name + "\" (" + lr.path + "): " + why);
      failed.insert(lr.id);
      return false;
    };
    if (lr.kind != "level") return fail("unsupported level kind <" + lr.kind + ">");
    QString path = resolveLevelPath(sceneDir, lr.path);
    if (path.isEmpty()) return fail("file not found");
    QString ext = QFileInfo(path).suffix().toLower();
    std::map<int, TImageP> &frames = loaded[lr.id];
    try {
      if (ext == "tlv" || ext == "pli") {
        TLevelReaderP rd(TFilePath(path.toStdWString()));
        TLevelP lev       = rd->loadInfo();
        TPalette *plt     = lev->getPalette();
        auto map          = mergePalette(doc->palette(), plt, firstPalette);
        firstPalette      = false;
        levelType[lr.id]  = ext == "tlv" ? LayerType::ToonzRaster : LayerType::Vector;
        for (auto it = lev->begin(); it != lev->end(); ++it) {
          TImageP img = rd->getFrameReader(it->first)->load();
          if (TToonzImageP ti = img) {
            TRasterCM32P ras = ti->getRaster();
            remapCM32(ras, map);
            // place at camera DPI (desktop: level dpi vs camera dpi)
            double dx = 0, dy = 0;
            ti->getDpi(dx, dy);
            double sc = dx > 0 ? camDpi / dx : 1.0;
            if (ras->getLx() != W || ras->getLy() != H || std::abs(sc - 1) > 1e-3) {
              TRasterCM32P out(W, H);
              out->clear();
              TAffine aff = TTranslation(W * 0.5, H * 0.5) * TScale(sc) *
                            TTranslation(-ras->getLx() * 0.5, -ras->getLy() * 0.5);
              TRop::resample(out, ras, aff, TRop::ClosestPixel);
              ras = out;
            }
            TToonzImageP nti(ras, ras->getBounds());
            nti->setDpi(camDpi, camDpi);
            nti->setPalette(doc->palette());
            frames[it->first.getNumber()] = nti;
          } else if (TVectorImageP vi = img) {
            for (UINT s = 0; s < vi->getStrokeCount(); s++) {
              TStroke *st = vi->getStroke(s);
              auto m      = map.find(st->getStyle());
              if (m != map.end()) st->setStyle(m->second);
            }
            for (UINT r = 0; r < vi->getRegionCount(); r++) remapRegions(vi->getRegion(r), map);
            if (std::abs(camInchW - 16) > 1e-3)  // mobile canvas = 16" camera
              vi->transform(TScale(16.0 / camInchW), true);
            vi->setPalette(doc->palette());
            frames[it->first.getNumber()] = vi;
          }
        }
      } else if (ext == "png") {
        levelType[lr.id] = LayerType::Raster;
        QFileInfo fi(path);
        QString stem = fi.fileName().section("..", 0, 0);
        bool seq     = fi.fileName().contains("..");
        QStringList files = seq ? QDir(fi.absolutePath()).entryList({stem + ".*.png"})
                                : QStringList{fi.fileName()};
        for (const QString &fn : files) {
          int num = seq ? toInt(fn.section('.', -2, -2).toStdString(), 1) : 1;
          TImageP img;
          TImageReader::load(TFilePath((fi.absolutePath() + "/" + fn).toStdWString()), img);
          TRasterImageP ri = img;
          if (!ri) continue;
          TRaster32P src = ri->getRaster();  // already RGBA32 (8 bit PNG)
          if (!src) {                         // 16 bit / grey PNG
            src = TRaster32P(ri->getRaster()->getSize());
            TRop::convert(src, ri->getRaster());
          }
          TRaster32P out(W, H);
          out->clear();
          double sc = lr.dpix > 0 ? camDpi / lr.dpix : 1.0;
          TRop::over(out, src, TTranslation(W * 0.5, H * 0.5) * TScale(sc) *
                                   TTranslation(-src->getLx() * 0.5, -src->getLy() * 0.5));
          frames[num] = TRasterImageP(out);
        }
      } else
        return fail("format ." + ext.toStdString() + " not supported on mobile");
    } catch (TException &e) {
      return fail("read error: " + ::to_string(e.getMessage()));
    } catch (std::exception &e) {
      return fail(std::string("read error: ") + e.what());
    } catch (...) {
      return fail("read error");
    }
    if (frames.empty()) return fail("no frames");
    return true;
  };

  // xsheet columns
  const XNode *xsh  = tnz->child("xsheet");
  const XNode *cols = xsh ? xsh->child("columns") : nullptr;
  int colIndex      = 0;
  std::map<int, std::vector<int>> colLayers;  // column number (1-based) -> layer uids
  if (cols)
    for (const XNode *col : cols->children()) {
      colIndex++;
      if (col->tag != "levelColumn") {
        res.warnings.push_back("column " + std::to_string(colIndex) + ": <" + col->tag +
                               "> not imported");
        continue;
      }
      int status = 0, opacity = 255;
      if (const XNode *st = col->child("status")) {
        auto t = st->tokens();
        if (!t.empty()) status = toInt(t[0]);
      }
      if (const XNode *op = col->child("camerastand_opacity")) {
        auto t = op->tokens();
        if (!t.empty()) opacity = toInt(t[0], 255);
      }
      // cells: "row count <level id/> fid increment"
      std::map<std::string, std::vector<std::pair<int, int>>> cellsByLevel;  // id -> (row,fid)
      if (const XNode *cells = col->child("cells"))
        for (const XNode *c : cells->children("cell")) {
          std::vector<std::string> tok;
          std::string lid;
          for (auto &k : c->kids) {
            if (k->tag.empty())
              tok.push_back(k->text);
            else if (k->tag == "level" && k->attr.count("id"))
              lid = k->attr.at("id");
          }
          if (tok.size() < 4 || lid.empty()) continue;
          int row = toInt(tok[0]), n = toInt(tok[1]), inc = toInt(tok[3]);
          int fid = toInt(tok[2]);  // letters (e.g. "1a") are not supported
          for (int k = 0; k < n; k++) cellsByLevel[lid].push_back({row + k, fid + k * inc});
        }
      for (auto &cl : cellsByLevel) {
        auto lit = levels.find(cl.first);
        if (lit == levels.end()) {
          res.warnings.push_back("column " + std::to_string(colIndex) + ": unknown level id " +
                                 cl.first);
          continue;
        }
        if (!loadLevel(lit->second)) continue;
        Layer *l   = doc->addLayer(levelType[cl.first], lit->second.name);
        colLayers[colIndex].push_back(l->uid);
        l->visible = (status & 0x1) == 0;  // eCamstandVisible bit set = hidden
        l->locked  = (status & 0x8) != 0;
        l->opacity = opacity;
        for (auto &fr : loaded[cl.first]) doc->setDrawing(l, fr.first, fr.second->cloneImage());
        for (auto &rc : cl.second)
          if (loaded[cl.first].count(rc.second)) l->setCell(rc.first, rc.second);
      }
    }
  if (xsh && xsh->child("pegbars")) {
    std::map<std::string, Peg> pegs;
    std::string cameraId;
    for (const XNode *p : xsh->child("pegbars")->children("pegbar")) {
      Peg g;
      g.id = p->attr.count("id") ? p->attr.at("id") : "";
      if (p->attr.count("activeboth") || p->attr.count("active")) cameraId = g.id;
      if (const XNode *par = p->child("parent")) {
        g.parent = par->attr.count("id") ? par->attr.at("id") : "None";
        for (const char *h : {"handle", "parentHandle"})
          if (par->attr.count(h) && par->attr.at(h) != "B" && !par->attr.at(h).empty())
            g.unsupported = true;
      }
      if (const XNode *c = p->child("center")) {
        auto t = c->tokens();
        if (t.size() >= 4) {
          g.center = TPointD(toDouble(t[0]), toDouble(t[1]));
          g.offset = TPointD(-toDouble(t[2]), -toDouble(t[3]));  // saved negated
        }
      }
      if (const XNode *st = p->child("status")) {
        auto t = st->tokens();
        if (!t.empty() && (toInt(t[0]) & 0x3) != 0) g.unsupported = true;  // motion path / IK
      }
      g.x   = readChannel(p->child("x"), 0, &g.unsupported);
      g.y   = readChannel(p->child("y"), 0, &g.unsupported);
      g.sx  = readChannel(p->child("sx"), 1, &g.unsupported);
      g.sy  = readChannel(p->child("sy"), 1, &g.unsupported);
      g.sc  = readChannel(p->child("sc"), 1, &g.unsupported);
      g.rot = readChannel(p->child("rot"), 0, &g.unsupported);
      for (const char *ch : {"shx", "shy", "plasticSD"})
        if (p->child(ch)) g.unsupported = true;
      pegs[g.id] = g;
    }
    auto isCol = [](const std::string &id) { return id.compare(0, 3, "Col") == 0; };
    // chain of an object up to (excluding) its nearest column ancestor
    auto chainOf = [&](const std::string &id, std::string *colParent) {
      std::vector<const Peg *> chain;  // object first
      std::string cur = id;
      for (int depth = 0; depth < 64 && pegs.count(cur); depth++) {
        const Peg &g = pegs[cur];
        chain.push_back(&g);
        if (isCol(g.parent)) {
          if (colParent) *colParent = g.parent;
          break;
        }
        cur = g.parent;
      }
      return chain;
    };
    // Builds mobile keys for a chain; pivot = centre of the object itself.
    auto buildKeys = [&](const std::vector<const Peg *> &chain, const std::string &what,
                         bool camera, KeyMap &keys, TPointD &centerPx) {
      const Peg &own = *chain[0];
      centerPx       = own.center * camDpi;
      bool unsupported = false, extra = false;
      std::set<int> fr;
      for (size_t i = 0; i < chain.size(); i++) {
        if (chain[i]->unsupported) unsupported = true;
        if (i > 0 && !chain[i]->isDefault()) extra = true;
        chain[i]->frames(fr);
      }
      if (unsupported)
        res.warnings.push_back(what + ": expressions, motion paths, shear or handles are not "
                                      "imported (animation may differ)");
      bool allDefault = true;
      for (auto *g : chain)
        if (!g->isDefault()) allDefault = false;
      if (allDefault) return;
      auto at = [&](double f) {
        TAffine m;
        for (auto *g : chain) m = g->local(f, camDpi) * m;
        return m;
      };
      auto toXform = [&](double f, Xform &xf) {
        bool ok = decompose(at(f), camera ? TPointD() : centerPx, xf);
        if (!extra && !camera) {  // keep the channel values (no angle wrapping)
          xf.rot   = -own.rot->getValue(f);
          xf.scale = own.sc->getValue(f) * own.sx->getValue(f);
        } else if (!extra && camera)
          xf.rot = -own.rot->getValue(f);
        if (camera) xf.scale = xf.scale > 1e-6 ? 1.0 / xf.scale : 1.0;  // stored as zoom
        return ok;
      };
      bool similarity = true;
      if (fr.empty()) fr.insert(0);
      // 1st attempt: one mobile key per desktop keyframe ...
      KeyMap simple;
      for (int f : fr) {
        Key k;
        similarity &= toXform(f, k.xf);
        TDoubleKeyframe::Type t = own.typeAt(f);
        k.ease = t == TDoubleKeyframe::Constant ? Ease::Constant
                 : t == TDoubleKeyframe::Linear ? Ease::Linear
                                                : Ease::EaseInOut;
        simple[std::max(0, f)] = k;
      }
      // ... kept only if it reproduces the desktop curves on every frame
      bool exact = true;
      int f0 = *fr.begin(), f1 = *fr.rbegin();
      for (int f = std::max(0, f0); f <= f1 && exact; f++) {
        Xform want, got = xformAt(simple, f);
        similarity &= toXform(f, want);
        if (std::abs(want.x - got.x) > 0.02 || std::abs(want.y - got.y) > 0.02 ||
            std::abs(want.rot - got.rot) > 0.005 ||
            std::abs(want.scale - got.scale) > 1e-4 * std::max(1.0, want.scale))
          exact = false;
      }
      if (exact)
        keys = simple;
      else {  // baked: one linear key per frame
        keys.clear();
        double prevRot = 0;
        bool first     = true;
        for (int f = std::max(0, f0); f <= f1; f++) {
          Key k;
          toXform(f, k.xf);
          if (!first)  // unwrap atan2 so that in-between frames do not spin
            while (k.xf.rot - prevRot > 180) k.xf.rot -= 360;
          if (!first)
            while (k.xf.rot - prevRot < -180) k.xf.rot += 360;
          prevRot = k.xf.rot;
          first   = false;
          k.ease  = Ease::Linear;
          keys[f] = k;
        }
      }
      if (!similarity)
        res.warnings.push_back(what + ": non-uniform scale is imported as uniform");
    };
    for (auto &cl : colLayers) {
      std::string id = "Col" + std::to_string(cl.first);
      if (!pegs.count(id)) continue;
      std::string colParent;
      auto chain = chainOf(id, &colParent);
      KeyMap keys;
      TPointD c;
      buildKeys(chain, "column " + std::to_string(cl.first), false, keys, c);
      int parentUid = 0;
      if (!colParent.empty()) {
        int pc = toInt(colParent.substr(3));
        if (colLayers.count(pc) && !colLayers[pc].empty()) parentUid = colLayers[pc][0];
      }
      for (int uid : cl.second) {
        Layer *l = doc->layerByUid(uid);
        if (!l) continue;
        l->keys      = keys;
        l->parentUid = parentUid;
        if (!keys.empty() && norm(c) > 1e-6) {
          l->pivotX = W * 0.5 + c.x;
          l->pivotY = H * 0.5 - c.y;
        }
      }
    }
    if (!cameraId.empty() && pegs.count(cameraId)) {
      auto chain = chainOf(cameraId, nullptr);
      TPointD c;
      buildKeys(chain, "camera", true, doc->cameraKeys, c);
    }
  }
  if (xsh && xsh->child("fxnodes") && !xsh->child("fxnodes")->children().empty())
    res.warnings.push_back("FX schematic nodes are not imported");
  if (doc->layerCount() == 0) {
    res.error = "no importable level columns in the scene";
    return res;
  }
  doc->currentLayer = 0;
  doc->dirty        = true;
  res.doc           = std::move(doc);
  return res;
}

}  // namespace otm
