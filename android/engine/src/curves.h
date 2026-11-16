// OpenToonz Mobile - function curves.
//
// Layer / camera keys are evaluated and exchanged with the desktop through
// the real OpenToonz TDoubleParam (common/tparam): a mobile key with ease
//   Linear    -> TDoubleKeyframe::Linear
//   Constant  -> TDoubleKeyframe::Constant
//   EaseInOut -> TDoubleKeyframe::EaseInOutPercentage, 50% out / 50% in
// so a scene exported to .tnz animates on desktop exactly as on mobile.
#pragma once

#include "document.h"
#include "tdoubleparam.h"

#include <functional>

namespace otm {

//! Fills a TDoubleParam with one keyframe per key (value picked by `get`).
void keysToParam(const KeyMap &keys, const std::function<double(const Xform &)> &get,
                 TDoubleParam *param);

//! Interpolation weight 0..1 between two keys, computed by TDoubleParam.
double easeWeight(Ease ease, int row0, int row1, double row);

}  // namespace otm
