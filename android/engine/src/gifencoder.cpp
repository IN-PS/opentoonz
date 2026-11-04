#include "gifencoder.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace otm {

GifEncoder::~GifEncoder() {
  if (m_f) fclose(m_f);
}

void GifEncoder::put(const void *p, size_t n) {
  if (m_f && fwrite(p, 1, n, m_f) != n) m_ok = false;
}

static void le16(std::vector<uint8_t> &v, int x) {
  v.push_back(x & 255);
  v.push_back((x >> 8) & 255);
}

bool GifEncoder::open(const std::string &path, int w, int h, int fps) {
  m_f = fopen(path.c_str(), "wb");
  if (!m_f) return false;
  m_w = w, m_h = h;
  m_delay = std::max(2, (int)(100.0 / std::max(1, fps) + 0.5));
  std::vector<uint8_t> hd = {'G', 'I', 'F', '8', '9', 'a'};
  le16(hd, w);
  le16(hd, h);
  hd.push_back(0x00);  // no global colour table (local tables per frame)
  hd.push_back(0);
  hd.push_back(0);
  // NETSCAPE2.0 loop forever
  const uint8_t loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                          'E',  '2',  '.',  '0', 0x03, 0x01, 0x00, 0x00, 0x00};
  hd.insert(hd.end(), loop, loop + sizeof(loop));
  put(hd.data(), hd.size());
  return m_ok;
}

// ---------------------------------------------------------------- median cut
namespace {
struct Box {
  int r0, r1, g0, g1, b0, b1;
  std::vector<int> cells;  // indices into histogram (5:5:5)
  long count = 0;
};
inline int ci(int r, int g, int b) { return (r << 10) | (g << 5) | b; }
}  // namespace

void GifEncoder::quantize(const uint8_t *rgba, int count,
                          std::vector<uint8_t> &palette,
                          std::vector<uint8_t> &indices, int maxColors) {
  std::vector<uint32_t> hist(32768, 0);
  for (int i = 0; i < count; i++) {
    const uint8_t *p = rgba + i * 4;
    hist[ci(p[0] >> 3, p[1] >> 3, p[2] >> 3)]++;
  }
  std::vector<Box> boxes(1);
  for (int c = 0; c < 32768; c++)
    if (hist[c]) boxes[0].cells.push_back(c), boxes[0].count += hist[c];
  auto shrink = [&](Box &b) {
    b.r0 = b.g0 = b.b0 = 31;
    b.r1 = b.g1 = b.b1 = 0;
    for (int c : b.cells) {
      int r = c >> 10, g = (c >> 5) & 31, bl = c & 31;
      b.r0 = std::min(b.r0, r), b.r1 = std::max(b.r1, r);
      b.g0 = std::min(b.g0, g), b.g1 = std::max(b.g1, g);
      b.b0 = std::min(b.b0, bl), b.b1 = std::max(b.b1, bl);
    }
  };
  shrink(boxes[0]);
  while ((int)boxes.size() < maxColors) {
    // split the box with most pixels that can still be split
    int best = -1;
    long bestScore = 0;
    for (int i = 0; i < (int)boxes.size(); i++) {
      Box &b = boxes[i];
      if (b.cells.size() < 2) continue;
      int ext = std::max({b.r1 - b.r0, b.g1 - b.g0, b.b1 - b.b0});
      long score = b.count * (ext + 1);
      if (score > bestScore) bestScore = score, best = i;
    }
    if (best < 0) break;
    Box &b  = boxes[best];
    int dr = b.r1 - b.r0, dg = b.g1 - b.g0, db = b.b1 - b.b0;
    int axis = (dr >= dg && dr >= db) ? 0 : (dg >= db ? 1 : 2);
    auto key = [axis](int c) {
      return axis == 0 ? (c >> 10) : axis == 1 ? ((c >> 5) & 31) : (c & 31);
    };
    std::sort(b.cells.begin(), b.cells.end(),
              [&](int a, int c) { return key(a) < key(c); });
    long half = b.count / 2, acc = 0;
    size_t cut = 0;
    for (; cut < b.cells.size() - 2; cut++) {  // both halves non-empty
      acc += hist[b.cells[cut]];
      if (acc >= half) break;
    }
    Box nb;
    nb.cells.assign(b.cells.begin() + cut + 1, b.cells.end());
    b.cells.resize(cut + 1);
    b.count = 0;
    for (int c : b.cells) b.count += hist[c];
    for (int c : nb.cells) nb.count += hist[c];
    shrink(b);
    shrink(nb);
    boxes.push_back(std::move(nb));
  }
  palette.assign(256 * 3, 0);
  std::vector<uint8_t> lut(32768, 0);
  // weighted mean colour of each box (computed on full precision pixels)
  std::vector<double> sr(boxes.size()), sg(boxes.size()), sb(boxes.size()),
      sn(boxes.size());
  for (int i = 0; i < (int)boxes.size(); i++)
    for (int c : boxes[i].cells) lut[c] = (uint8_t)i;
  for (int i = 0; i < count; i++) {
    const uint8_t *p = rgba + i * 4;
    int k            = lut[ci(p[0] >> 3, p[1] >> 3, p[2] >> 3)];
    sr[k] += p[0], sg[k] += p[1], sb[k] += p[2], sn[k] += 1;
  }
  for (int i = 0; i < (int)boxes.size(); i++) {
    if (sn[i] == 0) continue;
    palette[i * 3 + 0] = (uint8_t)(sr[i] / sn[i] + 0.5);
    palette[i * 3 + 1] = (uint8_t)(sg[i] / sn[i] + 0.5);
    palette[i * 3 + 2] = (uint8_t)(sb[i] / sn[i] + 0.5);
  }
  indices.resize(count);
  for (int i = 0; i < count; i++) {
    const uint8_t *p = rgba + i * 4;
    indices[i]       = lut[ci(p[0] >> 3, p[1] >> 3, p[2] >> 3)];
  }
}

// ---------------------------------------------------------------- LZW
void GifEncoder::lzw(const std::vector<uint8_t> &idx, int minCodeSize) {
  std::vector<uint8_t> out;
  uint32_t bitBuf = 0;
  int bitCnt      = 0;
  int clear = 1 << minCodeSize, eoi = clear + 1;
  int codeSize = minCodeSize + 1, next = eoi + 1;
  std::unordered_map<uint32_t, int> dict;
  dict.reserve(8192);
  auto emit = [&](int code) {
    bitBuf |= (uint32_t)code << bitCnt;
    bitCnt += codeSize;
    while (bitCnt >= 8) {
      out.push_back(bitBuf & 255);
      bitBuf >>= 8;
      bitCnt -= 8;
    }
  };
  emit(clear);
  int prefix = -1;
  for (uint8_t k : idx) {
    if (prefix < 0) {
      prefix = k;
      continue;
    }
    uint32_t key = ((uint32_t)prefix << 8) | k;
    auto it      = dict.find(key);
    if (it != dict.end()) {
      prefix = it->second;
      continue;
    }
    emit(prefix);
    if (next < 4096) {
      dict[key] = next++;
      if (next > (1 << codeSize) && codeSize < 12) codeSize++;
    } else {
      emit(clear);
      dict.clear();
      codeSize = minCodeSize + 1;
      next     = eoi + 1;
    }
    prefix = k;
  }
  if (prefix >= 0) emit(prefix);
  emit(eoi);
  if (bitCnt > 0) out.push_back(bitBuf & 255);
  uint8_t mcs = (uint8_t)minCodeSize;
  put(&mcs, 1);
  for (size_t i = 0; i < out.size(); i += 255) {
    uint8_t n = (uint8_t)std::min<size_t>(255, out.size() - i);
    put(&n, 1);
    put(&out[i], n);
  }
  uint8_t z = 0;
  put(&z, 1);
}

bool GifEncoder::addFrame(const uint8_t *rgba) {
  if (!m_f) return false;
  std::vector<uint8_t> pal, idx;
  quantize(rgba, m_w * m_h, pal, idx);
  std::vector<uint8_t> h = {0x21, 0xF9, 0x04, 0x00};  // GCE, no transparency
  le16(h, m_delay);
  h.push_back(0);
  h.push_back(0);
  h.push_back(0x2C);  // image descriptor
  le16(h, 0);
  le16(h, 0);
  le16(h, m_w);
  le16(h, m_h);
  h.push_back(0x80 | 7);  // local colour table, 256 entries
  put(h.data(), h.size());
  put(pal.data(), 256 * 3);
  lzw(idx, 8);
  m_frames++;
  return m_ok;
}

bool GifEncoder::close() {
  if (!m_f) return false;
  uint8_t t = 0x3B;
  put(&t, 1);
  bool ok = m_ok && fclose(m_f) == 0;
  m_f     = nullptr;
  return ok;
}

}  // namespace otm
