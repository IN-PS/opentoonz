// OpenToonz Mobile - project persistence.
//
// A project is a directory:
//   project.otm          scene description (OpenToonz TOStream XML format)
//   palette.tpl          the shared palette, OpenToonz studio-palette format
//   levels/<n>_<uid>.tlv Toonz Raster layers as genuine OpenToonz TLV levels
//                        (+ <n>_<uid>.tpl next to it, as desktop expects)
//   levels/<n>_<uid>.0001.png ...  full colour layers (OpenToonz sequence
//                        naming), one file per drawing number
// TLV/TPL/PNG files can be imported by desktop OpenToonz.
#pragma once

#include "document.h"

#include <memory>
#include <string>

namespace otm {

bool saveProject(Document &doc, const std::string &dir, std::string *error);
std::unique_ptr<Document> loadProject(const std::string &dir,
                                      std::string *error);

//! Writes / reads a palette in the OpenToonz .tpl format.
bool savePalette(TPalette *plt, const std::string &path);
TPalette *loadPalette(const std::string &path);

}  // namespace otm
