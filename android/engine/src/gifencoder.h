// OpenToonz Mobile - animated GIF89a encoder (no external dependencies).
// Desktop OpenToonz writes GIF through an external ffmpeg executable, which is
// not available on Android, hence this self-contained encoder.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace otm {

class GifEncoder {
public:
  ~GifEncoder();
  bool open(const std::string &path, int w, int h, int fps);
  //! rgba: top-down, premultiplied over an opaque background (alpha ignored)
  bool addFrame(const uint8_t *rgba);
  bool close();
  int frames() const { return m_frames; }

  // exposed for tests
  static void quantize(const uint8_t *rgba, int count,
                       std::vector<uint8_t> &palette /*256*3*/,
                       std::vector<uint8_t> &indices, int maxColors = 256);

private:
  void put(const void *p, size_t n);
  void lzw(const std::vector<uint8_t> &idx, int minCodeSize);
  FILE *m_f   = nullptr;
  int m_w = 0, m_h = 0, m_delay = 8, m_frames = 0;
  bool m_ok = true;
};

}  // namespace otm
