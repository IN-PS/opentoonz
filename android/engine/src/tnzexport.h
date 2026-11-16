// OpenToonz Mobile - export of a project as a desktop OpenToonz scene.
//
// Writes a standard OpenToonz project folder:
//   <dir>/<dir>_otprj.xml, scenes/<name>.tnz, scenes/scenes.xml,
//   drawings/<level>.tlv + .tpl (Toonz Raster), drawings/<level>.pli (vector),
//   extras/<level>.NNNN.png (raster)
// with camera, frame rate, columns (cells with holds, visibility, lock,
// opacity), column parents (peg hierarchy), centres (pivots) and the
// animation of columns and camera as TDoubleParam keyframes.
// Not exported (reported as warnings): layer effects, opacity keys, sound.
#pragma once

#include "document.h"

#include <string>
#include <vector>

namespace otm {

bool exportTnz(Document &doc, const std::string &dir, const std::string &sceneName,
               std::vector<std::string> *warnings, std::string *error);

}  // namespace otm
