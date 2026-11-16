#include "curves.h"

#include "tdoublekeyframe.h"

namespace otm {

static TDoubleKeyframe::Type typeOf(Ease e) {
  switch (e) {
  case Ease::Constant: return TDoubleKeyframe::Constant;
  case Ease::EaseInOut: return TDoubleKeyframe::EaseInOutPercentage;
  default: return TDoubleKeyframe::Linear;
  }
}

static void addKey(TDoubleParam *p, double frame, double value, Ease ease, bool prevEase) {
  TDoubleKeyframe k;
  k.m_frame      = frame;
  k.m_value      = value;
  k.m_isKeyframe = true;
  k.m_type       = typeOf(ease);
  if (ease == Ease::EaseInOut) k.m_speedOut = TPointD(50, 0);  // ease out 50%
  if (prevEase) k.m_speedIn = TPointD(-50, 0);                 // ease in 50%
  p->setKeyframe(k);
}

void keysToParam(const KeyMap &keys, const std::function<double(const Xform &)> &get,
                 TDoubleParam *param) {
  param->clearKeyframes();
  bool prevEase = false;
  for (auto &kv : keys) {
    addKey(param, kv.first, get(kv.second.xf), kv.second.ease, prevEase);
    prevEase = kv.second.ease == Ease::EaseInOut;
  }
}

double easeWeight(Ease ease, int row0, int row1, double row) {
  if (row1 <= row0) return 0;
  if (ease == Ease::Linear) return (row - row0) / double(row1 - row0);
  TDoubleParamP p(new TDoubleParam());
  addKey(p.getPointer(), row0, 0.0, ease, false);
  addKey(p.getPointer(), row1, 1.0, Ease::Linear, ease == Ease::EaseInOut);
  return p->getValue(row);
}

}  // namespace otm
