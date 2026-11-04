#include "projectio.h"

#include "tconvert.h"
#include "texception.h"
#include "tfilepath.h"
#include "timage_io.h"
#include "tlevel_io.h"
#include "trasterimage.h"
#include "trop.h"
#include "tstream.h"
#include "tsystem.h"
#include "ttoonzimage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <set>
#include <unistd.h>

namespace otm {

static const int kFormatVersion = 2;  // 2: transform keyframes

static void writeKeys(TOStream &os, const KeyMap &keys) {
  for (auto &k : keys)
    os << k.first << k.second.xf.x << k.second.xf.y << k.second.xf.rot
       << k.second.xf.scale << k.second.xf.opacity << (int)k.second.ease;
}

static void readKeys(TIStream &is, KeyMap &keys) {
  while (!is.eos()) {
    int row, ease;
    Key k;
    is >> row >> k.xf.x >> k.xf.y >> k.xf.rot >> k.xf.scale >> k.xf.opacity >> ease;
    k.ease    = (Ease)std::max(0, std::min(2, ease));
    keys[row] = k;
  }
}

static std::string levelBaseName(const Layer &l) {
  std::string safe;
  for (char c : l.name)
    safe += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  if (safe.empty()) safe = "layer";
  return safe + "_" + std::to_string(l.uid);
}

bool savePalette(TPalette *plt, const std::string &path) {
  try {
    TOStream os{TFilePath(path)};
    std::map<std::string, std::string> attr;
    attr["name"] = ::to_string(plt->getGlobalName());
    os.openChild("palette", attr);
    plt->saveData(os);
    os.closeChild();
    return true;
  } catch (...) {
    return false;
  }
}

TPalette *loadPalette(const std::string &path) {
  try {
    TIStream is{TFilePath(path)};
    if (!is) return nullptr;
    std::string tagName;
    if (!is.matchTag(tagName) || tagName != "palette") return nullptr;
    std::string gname;
    is.getTagParam("name", gname);
    TPalette *palette = new TPalette();
    palette->loadData(is);
    palette->setGlobalName(::to_wstring(gname));
    is.matchEndTag();
    return palette;
  } catch (...) {
    return nullptr;
  }
}

// Hard link (cheap, same filesystem) or copy.
static bool linkOrCopy(const QString &src, const QString &dst) {
  if (::link(src.toLocal8Bit().constData(), dst.toLocal8Bit().constData()) == 0)
    return true;
  return QFile::copy(src, dst);
}

// Re-uses the level files of the previous save of an unmodified layer.
static bool reuseLevel(const QString &dir, const QString &tmp, const Layer &l,
                       const std::string &base) {
  QDir levels(dir + "/levels");
  QString b = QString::fromStdString(base);
  QStringList files =
      l.type == LayerType::ToonzRaster
          ? QStringList{b + ".tlv"}  // .tpl is rewritten (palette may change)
          : levels.entryList(QStringList{b + ".*.png"}, QDir::Files);
  if (files.isEmpty()) return false;
  for (const QString &f : files)
    if (!QFile::exists(levels.filePath(f))) return false;
  for (const QString &f : files)
    if (!linkOrCopy(levels.filePath(f), tmp + "/levels/" + f)) return false;
  return true;
}

static bool removeRecursively(const QString &p) {
  QFileInfo fi(p);
  if (!fi.exists()) return true;
  if (fi.isDir()) return QDir(p).removeRecursively();
  return QFile::remove(p);
}

bool saveProject(Document &doc, const std::string &dirStr, std::string *error) {
  auto fail = [&](const std::string &e) {
    if (error) *error = e;
    return false;
  };
  QString dir = QString::fromStdString(dirStr);
  QString tmp = dir + "/.saving";
  removeRecursively(tmp);
  if (!QDir().mkpath(tmp + "/levels")) return fail("cannot create " + dirStr);
  TFilePath tmpFp(tmp.toStdWString());

  try {
    // ---- levels
    for (int i = 0; i < doc.layerCount(); i++) {
      Layer *l = doc.layer(i);
      std::set<int> used;
      for (int d : l->cells)
        if (d > 0 && l->drawings.count(d)) used.insert(d);
      std::string base = levelBaseName(*l);
      if (doc.savedDir == dirStr && l->savedRevision == l->revision &&
          l->savedLevel == base && reuseLevel(dir, tmp, *l, base)) {
        // unchanged since the last save: palette may have changed though
        if (l->type == LayerType::ToonzRaster)
          savePalette(doc.palette(), (tmp + "/levels/" + QString::fromStdString(base) +
                                      ".tpl").toStdString());
        continue;
      }
      if (l->type == LayerType::ToonzRaster) {
        TFilePath fp = tmpFp + TFilePath("levels/" + base + ".tlv");
        if (!used.empty()) {
          TLevelWriterP lw(fp);
          lw->setPalette(doc.palette());
          for (int d : used) {
            TImageP img = doc.drawing(l, d, false);
            TImageWriterP iw = lw->getFrameWriter(TFrameId(d));
            iw->save(img);
          }
        }
        savePalette(doc.palette(),
                    (tmp + "/levels/" + QString::fromStdString(base) + ".tpl")
                        .toStdString());
      } else {
        for (int d : used) {
          TFilePath fp = tmpFp + TFilePath("levels/" + base + "..png");
          TImageWriter::save(fp.withFrame(TFrameId(d)), doc.drawing(l, d, false));
        }
      }
    }
    // ---- palette
    if (!savePalette(doc.palette(), (tmp + "/palette.tpl").toStdString()))
      return fail("cannot write palette");
    // ---- scene
    {
      TOStream os(tmpFp + TFilePath("project.otm"));
      std::map<std::string, std::string> attr;
      attr["version"] = std::to_string(kFormatVersion);
      os.openChild("otmproject", attr);
      os.child("generator") << std::string("OpenToonz Mobile");
      os.child("name") << doc.name;
      os.openChild("canvas");
      os << doc.width() << doc.height() << doc.fps() << doc.minFrameCount();
      os.closeChild();
      os.openChild("current");
      os << doc.currentRow << doc.currentLayer << doc.currentStyle;
      os.closeChild();
      os.openChild("onion");
      os << (int)doc.onion.enabled << doc.onion.back << doc.onion.forward
         << doc.onion.opacity << (int)doc.onion.tint;
      os.closeChild();
      os.child("lastUid") << doc.lastUid();
      if (!doc.cameraKeys.empty()) {
        os.openChild("camera");
        writeKeys(os, doc.cameraKeys);
        os.closeChild();
      }
      os.openChild("layers");
      for (int i = 0; i < doc.layerCount(); i++) {
        Layer *l = doc.layer(i);
        std::map<std::string, std::string> la;
        la["uid"]  = std::to_string(l->uid);
        la["type"] = l->type == LayerType::ToonzRaster ? "tlv" : "png";
        os.openChild("layer", la);
        os.child("name") << l->name;
        os.child("level") << levelBaseName(*l);
        os.openChild("flags");
        os << (int)l->visible << (int)l->locked << l->opacity << l->nextDrawing;
        os.closeChild();
        os.openChild("cells");
        for (int c : l->cells) os << c;
        os.closeChild();
        if (!l->keys.empty()) {
          os.openChild("keys");
          writeKeys(os, l->keys);
          os.closeChild();
        }
        os.closeChild();
      }
      os.closeChild();
      os.closeChild();
    }
  } catch (const TException &e) {
    return fail("save failed: " + ::to_string(e.getMessage()));
  } catch (const std::exception &e) {
    return fail(std::string("save failed: ") + e.what());
  } catch (...) {
    return fail("save failed (OpenToonz writer exception)");
  }

  // ---- swap the new files in (keeps the previous save if anything failed)
  QDir().mkpath(dir);
  removeRecursively(dir + "/levels");
  if (!QDir().rename(tmp + "/levels", dir + "/levels"))
    return fail("cannot move levels into place");
  for (const char *f : {"palette.tpl", "project.otm"}) {
    QString dst = dir + "/" + f;
    QFile::remove(dst);
    if (!QFile::rename(tmp + "/" + f, dst))
      return fail(std::string("cannot move ") + f);
  }
  removeRecursively(tmp);
  for (int i = 0; i < doc.layerCount(); i++) {
    Layer *l         = doc.layer(i);
    l->savedRevision = l->revision;
    l->savedLevel    = levelBaseName(*l);
  }
  doc.savedDir = dirStr;
  return true;
}

std::unique_ptr<Document> loadProject(const std::string &dirStr,
                                      std::string *error) {
  auto fail = [&](const std::string &e) -> std::unique_ptr<Document> {
    if (error) *error = e;
    return nullptr;
  };
  QString dir = QString::fromStdString(dirStr);
  TFilePath dirFp(dir.toStdWString());
  TFilePath sceneFp = dirFp + TFilePath("project.otm");
  if (!TSystem::doesExistFileOrLevel(sceneFp))
    return fail("project.otm not found in " + dirStr);

  struct LayerRec {
    int uid = 0;
    std::string type, name, level;
    int visible = 1, locked = 0, opacity = 255, next = 1;
    std::vector<int> cells;
    KeyMap keys;
  };
  KeyMap cameraKeys;
  std::vector<LayerRec> recs;
  int w = 960, h = 540, fps = 12, minFrames = 1, row = 0, layer = 0, style = 1;
  int onion[5] = {0, 2, 1, 110, 1};
  int lastUid  = 0;
  std::string name = "Untitled";
  try {
    TIStream is(sceneFp);
    std::string tag;
    if (!is.matchTag(tag) || tag != "otmproject")
      return fail("not an OpenToonz Mobile project");
    while (is.matchTag(tag)) {
      if (tag == "canvas") {
        is >> w >> h >> fps;
        if (!is.eos()) is >> minFrames;
      } else if (tag == "current")
        is >> row >> layer >> style;
      else if (tag == "onion")
        for (int &v : onion) is >> v;
      else if (tag == "lastUid")
        is >> lastUid;
      else if (tag == "camera")
        readKeys(is, cameraKeys);
      else if (tag == "name")
        is >> name;
      else if (tag == "layers") {
        while (is.matchTag(tag)) {
          if (tag != "layer") {
            is.skipCurrentTag();
            continue;
          }
          LayerRec r;
          std::string s;
          if (is.getTagParam("uid", s)) r.uid = std::stoi(s);
          is.getTagParam("type", r.type);
          while (is.matchTag(tag)) {
            if (tag == "name")
              is >> r.name;
            else if (tag == "level")
              is >> r.level;
            else if (tag == "keys")
              readKeys(is, r.keys);
            else if (tag == "flags")
              is >> r.visible >> r.locked >> r.opacity >> r.next;
            else if (tag == "cells")
              while (!is.eos()) {
                int c;
                is >> c;
                r.cells.push_back(c);
              }
            else {
              is.skipCurrentTag();
              continue;
            }
            is.matchEndTag();
          }
          is.matchEndTag();
          recs.push_back(r);
        }
      } else {
        is.skipCurrentTag();
        continue;
      }
      is.matchEndTag();
    }
  } catch (...) {
    return fail("corrupted project.otm");
  }

  std::unique_ptr<Document> doc(new Document(w, h, fps));
  doc->name = name;
  doc->setMinFrameCount(minFrames);
  if (TPalette *plt = loadPalette((dir + "/palette.tpl").toStdString()))
    doc->setPalette(plt);

  for (const LayerRec &r : recs) {
    LayerType type = r.type == "png" ? LayerType::Raster : LayerType::ToonzRaster;
    Layer *l       = doc->addLayer(type, r.name);
    l->uid         = r.uid;
    doc->setLastUid(r.uid);
    l->visible = r.visible != 0;
    l->locked  = r.locked != 0;
    l->opacity = r.opacity;
    l->cells   = r.cells;
    l->keys    = r.keys;
    std::set<int> used(r.cells.begin(), r.cells.end());
    used.erase(0);
    try {
      if (type == LayerType::ToonzRaster) {
        TFilePath fp = dirFp + TFilePath("levels/" + r.level + ".tlv");
        if (!used.empty()) {
          TLevelReaderP lr(fp);
          TLevelP lv = lr->loadInfo();
          for (auto it = lv->begin(); it != lv->end(); ++it) {
            TImageP img = lr->getFrameReader(it->first)->load();
            if (TToonzImageP ti = img) {
              ti->setPalette(doc->palette());
              doc->setDrawing(l, it->first.getNumber(), img);
            }
          }
        }
      } else {
        for (int d : used) {
          TFilePath fp = dirFp + TFilePath("levels/" + r.level + "..png");
          TImageP img;
          TImageReader::load(fp.withFrame(TFrameId(d)), img);
          TRasterImageP ri = img;
          if (!ri) continue;
          TRaster32P ras = ri->getRaster();
          if (!ras) {  // PNG without alpha may load as other depth
            ras = TRaster32P(ri->getRaster()->getSize());
            TRop::convert(ras, ri->getRaster());
            ri = TRasterImageP(ras);
          }
          doc->setDrawing(l, d, ri);
        }
      }
    } catch (...) {
      return fail("cannot read level " + r.level);
    }
    // cells pointing to missing drawings are cleared
    for (int &c : l->cells)
      if (c > 0 && !l->drawings.count(c)) c = 0;
    l->nextDrawing = std::max(l->nextDrawing, r.next);
  }
  doc->setLastUid(lastUid);
  doc->cameraKeys = cameraKeys;
  for (int i = 0; i < doc->layerCount() && i < (int)recs.size(); i++) {
    // files on disk are current
    doc->layer(i)->savedRevision = doc->layer(i)->revision;
    doc->layer(i)->savedLevel    = recs[i].level;
  }
  doc->savedDir = dirStr;
  if (doc->layerCount() == 0) doc->addLayer(LayerType::ToonzRaster, "Layer 1");
  doc->currentRow   = std::max(0, row);
  doc->currentLayer = std::max(0, std::min(layer, doc->layerCount() - 1));
  doc->currentStyle = style;
  doc->onion.enabled = onion[0] != 0;
  doc->onion.back    = onion[1];
  doc->onion.forward = onion[2];
  doc->onion.opacity = onion[3];
  doc->onion.tint    = onion[4] != 0;
  return doc;
}

}  // namespace otm
