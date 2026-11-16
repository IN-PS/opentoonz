// OpenToonz Mobile - inverse kinematics on the layer (peg) hierarchy, solved
// by the OpenToonz IKEngine (toonzlib/ikengine.cpp, Jacobian DLS solver used
// by the desktop Skeleton tool).
#pragma once

#include "tgeometry.h"

#include <vector>

namespace otm {

//! joints: root first, end effector last (>= 2 points). The root stays in
//! place and the end effector is dragged towards target. Returns, for each
//! bone i (joints[i] -> joints[i+1]), the change of its world direction in
//! radians (counter-clockwise, y up).
std::vector<double> solveIkChain(const std::vector<TPointD> &joints, const TPointD &target);

}  // namespace otm
