#include "ik.h"

#include "toonz/ikengine.h"

#include <cmath>

namespace otm {

std::vector<double> solveIkChain(const std::vector<TPointD> &joints, const TPointD &target) {
  int n = (int)joints.size();
  std::vector<double> out(std::max(0, n - 1), 0.0);
  if (n < 2) return out;
  auto dir = [](const TPointD &a, const TPointD &b) { return std::atan2(b.y - a.y, b.x - a.x); };
  if (n == 2) {  // a single bone simply aims at the target
    out[0] = dir(joints[0], target) - dir(joints[0], joints[1]);
    return out;
  }
  // IKEngine::drag advances by a clamped step (the desktop tool calls it on
  // every mouse move): iterate until the end effector settles.
  std::vector<TPointD> cur = joints;
  double lastDist = tdistance(cur.back(), target);
  for (int pass = 0; pass < 200 && lastDist > 0.25; pass++) {
    IKEngine engine;
    engine.setRoot(cur[0]);
    for (int i = 1; i < n; i++) engine.addJoint(cur[i], i - 1);
    TPointD t = target;
    engine.drag(t);
    for (int i = 0; i < n; i++) cur[i] = engine.getJoint(i);
    double dist = tdistance(cur.back(), target);
    if (lastDist - dist < 0.01) break;
    lastDist = dist;
  }
  for (int i = 0; i + 1 < n; i++) {
    double d = dir(cur[i], cur[i + 1]) - dir(joints[i], joints[i + 1]);
    while (d > M_PI) d -= 2 * M_PI;
    while (d < -M_PI) d += 2 * M_PI;
    out[i] = d;
  }
  return out;
}

}  // namespace otm
