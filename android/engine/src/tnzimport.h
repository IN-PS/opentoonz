// OpenToonz Mobile - import of desktop OpenToonz scenes (.tnz).
//
// Reads the scene XML written by ToonzScene::save (TOStream format):
// camera resolution/size, output fps, the level set (Toonz Raster .tlv with
// its .tpl palette, vector .pli, .png raster levels) and the xsheet level
// columns with their cells (holds, increments, visibility, lock, opacity).
// Each (column, level) pair becomes a mobile layer; level palettes are merged
// into the document palette with style-id remapping.
//
// Not imported (reported as warnings): sub-xsheets, sound / palette /
// zerary-fx columns, FX schematic, pegbar animation, jpg/tif/psd levels.
#pragma once

#include "document.h"

#include <memory>
#include <string>
#include <vector>

namespace otm {

struct TnzImportResult {
  std::unique_ptr<Document> doc;
  std::vector<std::string> warnings;
  std::string error;  // non-empty on failure
};

TnzImportResult importTnz(const std::string &tnzPath);

}  // namespace otm
