// OpenToonz Mobile - JNI bridge for org.opentoonz.mobile.NativeEngine.
// Also builds on the host (with the JDK's jni.h) so that the symbol table
// can be checked against the Java declarations.
#include "engine.h"

#include <jni.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifdef __ANDROID__
#include <android/bitmap.h>
#include <android/log.h>
#include <QtCore/private/qjnihelpers_p.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "otm", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "otm", __VA_ARGS__)
#else
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

#include <QCoreApplication>
#include "timagecache.h"
#include "tfilepath.h"

using namespace otm;

namespace {
std::unique_ptr<Engine> g_engine;
QCoreApplication *g_app = nullptr;

Engine &E() {
  if (!g_engine) g_engine.reset(new Engine());
  return *g_engine;
}

std::string str(JNIEnv *env, jstring s) {
  if (!s) return std::string();
  const char *c = env->GetStringUTFChars(s, nullptr);
  std::string r(c);
  env->ReleaseStringUTFChars(s, c);
  return r;
}

jintArray rect(JNIEnv *env, const Engine::DirtyRect &d) {
  jint v[4] = {d.x0, d.y0, d.x1, d.y1};
  jintArray a = env->NewIntArray(4);
  env->SetIntArrayRegion(a, 0, 4, v);
  return a;
}

jstring errOrNull(JNIEnv *env, bool ok, const std::string &err) {
  if (ok) return nullptr;
  return env->NewStringUTF(err.empty() ? "unknown error" : err.c_str());
}

inline jint argb(const TPixel32 &c) {
  return (jint)(((uint32_t)c.m << 24) | ((uint32_t)c.r << 16) |
                ((uint32_t)c.g << 8) | (uint32_t)c.b);
}
inline TPixel32 fromArgb(jint v) {
  uint32_t u = (uint32_t)v;
  return TPixel32((u >> 16) & 255, (u >> 8) & 255, u & 255, (u >> 24) & 255);
}
}  // namespace

#define JF(ret, name) \
  extern "C" JNIEXPORT ret JNICALL Java_org_opentoonz_mobile_NativeEngine_##name

#ifdef __ANDROID__
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  JNIEnv *env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
    return JNI_ERR;
  // Qt5Core keeps a JavaVM pointer for its Android helpers; the app ships a
  // small org.qtproject.qt5.android.QtNative compatible class for this.
  if (QtAndroidPrivate::initJNI(vm, env) != JNI_OK) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    LOGE("Qt initJNI failed (continuing without Qt JNI helpers)");
  }
  return JNI_VERSION_1_6;
}
#endif

JF(void, init)(JNIEnv *env, jclass, jstring cacheDir, jstring nativeLibDir) {
  std::string libDir = str(env, nativeLibDir);
  // OpenToonz's TLV codec runs LZO in helper processes; on Android they are
  // installed with the native libraries (lib<name>.so naming).
  std::string comp   = libDir + "/liblzocompress.so";
  std::string decomp = libDir + "/liblzodecompress.so";
#ifdef __ANDROID__
  setenv("OTM_LZOCOMPRESS", comp.c_str(), 1);
  setenv("OTM_LZODECOMPRESS", decomp.c_str(), 1);
#else
  // host (JVM tests): keep helper paths given by the test runner
  setenv("OTM_LZOCOMPRESS", comp.c_str(), 0);
  setenv("OTM_LZODECOMPRESS", decomp.c_str(), 0);
#endif
  if (!QCoreApplication::instance()) {
    static int argc    = 1;
    static char arg0[] = "opentoonz-mobile";
    static char *argv[] = {arg0, nullptr};
    g_app               = new QCoreApplication(argc, argv);
  }
  std::string cache = str(env, cacheDir);
  if (!cache.empty())
    TImageCache::instance()->setRootDir(TFilePath(cache + "/imagecache"));
  E();
  LOGI("OpenToonz Mobile engine initialised\n");
}

JF(void, newDocument)(JNIEnv *, jclass, jint w, jint h, jint fps) {
  E().newDocument(w, h, fps);
}
JF(jint, getWidth)(JNIEnv *, jclass) { return E().doc()->width(); }
JF(jint, getHeight)(JNIEnv *, jclass) { return E().doc()->height(); }
JF(jint, getFps)(JNIEnv *, jclass) { return E().doc()->fps(); }
JF(void, setFps)(JNIEnv *, jclass, jint fps) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().doc()->setFps(fps);
  E().doc()->dirty = true;
}
JF(jboolean, isDirty)(JNIEnv *, jclass) { return E().doc()->dirty; }
JF(jstring, getName)(JNIEnv *env, jclass) {
  return env->NewStringUTF(E().doc()->name.c_str());
}
JF(void, setName)(JNIEnv *env, jclass, jstring n) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().doc()->name  = str(env, n);
  E().doc()->dirty = true;
}

// ------------------------------------------------------------------ render
#ifdef __ANDROID__
JF(void, render)(JNIEnv *env, jclass, jobject bitmap, jint row, jint x0,
                 jint y0, jint x1, jint y1, jboolean onion) {
  AndroidBitmapInfo info;
  if (AndroidBitmap_getInfo(env, bitmap, &info) < 0 ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
    return;
  if ((int)info.width != E().doc()->width() ||
      (int)info.height != E().doc()->height())
    return;
  void *px = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &px) < 0) return;
  E().renderRGBA(row, (uint8_t *)px, info.stride, x0, y0, x1, y1, onion);
  AndroidBitmap_unlockPixels(env, bitmap);
}

JF(void, renderThumb)(JNIEnv *env, jclass, jobject bitmap, jint row) {
  AndroidBitmapInfo info;
  if (AndroidBitmap_getInfo(env, bitmap, &info) < 0 ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
    return;
  void *px = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &px) < 0) return;
  E().renderScaledRGBA(row, (uint8_t *)px, info.width, info.height,
                       info.stride, false);
  AndroidBitmap_unlockPixels(env, bitmap);
}

JF(jboolean, importBitmap)(JNIEnv *env, jclass, jobject bitmap,
                           jboolean newLayer, jint rowOffset, jstring name) {
  AndroidBitmapInfo info;
  if (AndroidBitmap_getInfo(env, bitmap, &info) < 0 ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
    return false;
  void *px = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &px) < 0) return false;
  // Android bitmaps are premultiplied; importRGBA wants straight alpha.
  std::vector<uint8_t> rgba((size_t)info.width * info.height * 4);
  for (uint32_t y = 0; y < info.height; y++) {
    const uint8_t *s = (const uint8_t *)px + (size_t)y * info.stride;
    uint8_t *d       = &rgba[(size_t)y * info.width * 4];
    for (uint32_t x = 0; x < info.width; x++, s += 4, d += 4) {
      int a = s[3];
      if (a == 0) {
        d[0] = d[1] = d[2] = d[3] = 0;
      } else if (a == 255) {
        memcpy(d, s, 4);
      } else {
        d[0] = std::min(255, s[0] * 255 / a);
        d[1] = std::min(255, s[1] * 255 / a);
        d[2] = std::min(255, s[2] * 255 / a);
        d[3] = a;
      }
    }
  }
  AndroidBitmap_unlockPixels(env, bitmap);
  return E().importRGBA(rgba.data(), info.width, info.height, newLayer, true,
                        rowOffset, str(env, name));
}
#else  // ---------------------------------------------------------- host JVM
// Host versions (Robolectric tests on the JVM): pixels go through the
// android.graphics.Bitmap Java API instead of <android/bitmap.h>.
namespace {
struct BmpApi {
  jmethodID w, h, set, get;
  explicit BmpApi(JNIEnv *env) {
    jclass c = env->FindClass("android/graphics/Bitmap");
    w   = env->GetMethodID(c, "getWidth", "()I");
    h   = env->GetMethodID(c, "getHeight", "()I");
    set = env->GetMethodID(c, "setPixels", "([IIIIIII)V");
    get = env->GetMethodID(c, "getPixels", "([IIIIIII)V");
  }
};
void putRgba(JNIEnv *env, jobject bmp, const uint8_t *rgba, int x0, int y0,
             int w, int h, int stride) {
  BmpApi api(env);
  std::vector<jint> px((size_t)w * h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint8_t *p = rgba + (size_t)(y + y0) * stride + (size_t)(x + x0) * 4;
      int a = p[3], r = p[0], g = p[1], b = p[2];
      if (a > 0 && a < 255) r = r * 255 / a, g = g * 255 / a, b = b * 255 / a;
      px[(size_t)y * w + x] = (jint)(((uint32_t)a << 24) | (r << 16) | (g << 8) | b);
    }
  jintArray arr = env->NewIntArray(w * h);
  env->SetIntArrayRegion(arr, 0, w * h, px.data());
  env->CallVoidMethod(bmp, api.set, arr, 0, w, x0, y0, w, h);
  env->DeleteLocalRef(arr);
}
}  // namespace

JF(void, render)(JNIEnv *env, jclass, jobject bitmap, jint row, jint x0,
                 jint y0, jint x1, jint y1, jboolean onion) {
  int w = E().doc()->width(), h = E().doc()->height();
  x0 = std::max(0, (int)x0), y0 = std::max(0, (int)y0);
  x1 = std::min(w - 1, (int)x1), y1 = std::min(h - 1, (int)y1);
  if (x1 < x0 || y1 < y0) return;
  std::vector<uint8_t> buf((size_t)w * h * 4);
  E().renderRGBA(row, buf.data(), w * 4, x0, y0, x1, y1, onion);
  putRgba(env, bitmap, buf.data(), x0, y0, x1 - x0 + 1, y1 - y0 + 1, w * 4);
}

JF(void, renderThumb)(JNIEnv *env, jclass, jobject bitmap, jint row) {
  BmpApi api(env);
  int w = env->CallIntMethod(bitmap, api.w), h = env->CallIntMethod(bitmap, api.h);
  std::vector<uint8_t> buf((size_t)w * h * 4);
  E().renderScaledRGBA(row, buf.data(), w, h, w * 4, false);
  putRgba(env, bitmap, buf.data(), 0, 0, w, h, w * 4);
}

JF(jboolean, importBitmap)(JNIEnv *env, jclass, jobject bitmap,
                           jboolean newLayer, jint rowOffset, jstring name) {
  BmpApi api(env);
  int w = env->CallIntMethod(bitmap, api.w), h = env->CallIntMethod(bitmap, api.h);
  if (w <= 0 || h <= 0) return false;
  jintArray arr = env->NewIntArray(w * h);
  env->CallVoidMethod(bitmap, api.get, arr, 0, w, 0, 0, w, h);
  std::vector<jint> px((size_t)w * h);
  env->GetIntArrayRegion(arr, 0, w * h, px.data());
  env->DeleteLocalRef(arr);
  std::vector<uint8_t> rgba((size_t)w * h * 4);
  for (size_t i = 0; i < px.size(); i++) {
    uint32_t c = (uint32_t)px[i];
    rgba[i * 4 + 0] = (c >> 16) & 255, rgba[i * 4 + 1] = (c >> 8) & 255;
    rgba[i * 4 + 2] = c & 255, rgba[i * 4 + 3] = c >> 24;
  }
  return E().importRGBA(rgba.data(), w, h, newLayer, true, rowOffset,
                        str(env, name));
}
#endif

//! Renders a frame scaled to w x h into a YUV420 encoder input image
//! (MediaCodec flexible YUV planes), BT.601 limited range.
JF(void, renderYuv)(JNIEnv *env, jclass, jint row, jint w, jint h,
                    jobject yBuf, jint yRow, jint yPix, jobject uBuf, jint uRow,
                    jint uPix, jobject vBuf, jint vRow, jint vPix) {
  uint8_t *Y = (uint8_t *)env->GetDirectBufferAddress(yBuf);
  uint8_t *U = (uint8_t *)env->GetDirectBufferAddress(uBuf);
  uint8_t *V = (uint8_t *)env->GetDirectBufferAddress(vBuf);
  if (!Y || !U || !V) return;
  std::vector<uint8_t> rgba((size_t)w * h * 4);
  E().renderScaledRGBA(row, rgba.data(), w, h, w * 4, false);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint8_t *p = &rgba[((size_t)y * w + x) * 4];
      int r = p[0], g = p[1], b = p[2];
      Y[y * yRow + x * yPix] =
          (uint8_t)std::min(255, std::max(0, ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16));
      if (!(x & 1) && !(y & 1)) {
        int cu = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        int cv = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        U[(y / 2) * uRow + (x / 2) * uPix] = (uint8_t)std::min(255, std::max(0, cu));
        V[(y / 2) * vRow + (x / 2) * vPix] = (uint8_t)std::min(255, std::max(0, cv));
      }
    }
}

// ------------------------------------------------------------------ tools
JF(void, setTool)(JNIEnv *, jclass, jint t) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().tool = (Tool)t;
}
JF(void, setBrush)(JNIEnv *, jclass, jfloat minSize, jfloat maxSize,
                   jboolean pressure, jboolean antialias) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().settings.brushMin  = std::max(1.0f, std::min(minSize, maxSize));
  E().settings.brushMax  = std::max(1.0f, maxSize);
  E().settings.pressure  = pressure;
  E().settings.antialias = antialias;
}
JF(void, setEraser)(JNIEnv *, jclass, jfloat size, jint mode) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().settings.eraserSize = std::max(1.0f, size);
  E().settings.eraseMode  = (EraseMode)mode;
}
JF(void, setFill)(JNIEnv *, jclass, jint mode, jint depth, jboolean emptyOnly) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().settings.fillMode      = (FillMode)mode;
  E().settings.fillDepth     = depth;
  E().settings.fillEmptyOnly = emptyOnly;
}
JF(jintArray, pointerDown)(JNIEnv *env, jclass, jfloat x, jfloat y, jfloat p) {
  return rect(env, E().pointerDown(x, y, p));
}
JF(jintArray, pointerMove)(JNIEnv *env, jclass, jfloat x, jfloat y, jfloat p) {
  return rect(env, E().pointerMove(x, y, p));
}
JF(jintArray, pointerUp)(JNIEnv *env, jclass, jfloat x, jfloat y, jfloat p) {
  return rect(env, E().pointerUp(x, y, p));
}
JF(jint, getPickedStyle)(JNIEnv *, jclass) { return E().pickedStyle(); }

JF(jboolean, selectRect)(JNIEnv *, jclass, jint x0, jint y0, jint x1, jint y1) {
  return E().selectRect(x0, y0, x1, y1);
}
JF(void, transformSelection)(JNIEnv *, jclass, jfloat dx, jfloat dy,
                             jfloat scale, jfloat angle) {
  E().transformSelection(dx, dy, scale, angle);
}
JF(jintArray, commitSelection)(JNIEnv *env, jclass) {
  return rect(env, E().commitSelection());
}
JF(jintArray, cancelSelection)(JNIEnv *env, jclass) {
  return rect(env, E().cancelSelection());
}
JF(jintArray, deleteSelection)(JNIEnv *env, jclass) {
  return rect(env, E().deleteSelection());
}
JF(jboolean, hasSelection)(JNIEnv *, jclass) {
  return E().selection().active();
}
JF(jintArray, selectionBounds)(JNIEnv *env, jclass) {
  return rect(env, E().selectionBounds());
}

JF(jboolean, undo)(JNIEnv *, jclass) { return E().undo(); }
JF(jboolean, redo)(JNIEnv *, jclass) { return E().redo(); }

// ------------------------------------------------------------------ frames
JF(jint, getFrameCount)(JNIEnv *, jclass) { return E().doc()->frameCount(); }
JF(jint, getCurrentRow)(JNIEnv *, jclass) { return E().doc()->currentRow; }
JF(void, setCurrentRow)(JNIEnv *, jclass, jint r) { E().setCurrentRow(r); }
JF(jint, getCell)(JNIEnv *, jclass, jint layer, jint row) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  Layer *l = E().doc()->layer(layer);
  return l ? l->cell(row) : 0;
}
JF(void, insertBlankFrame)(JNIEnv *, jclass) { E().insertBlankFrame(); }
JF(void, duplicateFrame)(JNIEnv *, jclass) { E().duplicateFrame(); }
JF(void, holdFrame)(JNIEnv *, jclass) { E().holdFrame(); }
JF(void, deleteFrame)(JNIEnv *, jclass) { E().deleteFrame(); }
JF(void, clearCell)(JNIEnv *, jclass) { E().clearCell(); }
JF(void, moveCell)(JNIEnv *, jclass, jint from, jint to) {
  E().moveCell(from, to);
}

// ------------------------------------------------------------------ layers
JF(jint, getLayerCount)(JNIEnv *, jclass) { return E().doc()->layerCount(); }
JF(jint, getCurrentLayer)(JNIEnv *, jclass) { return E().doc()->currentLayer; }
JF(void, setCurrentLayer)(JNIEnv *, jclass, jint i) { E().setCurrentLayer(i); }
JF(jstring, getLayerName)(JNIEnv *env, jclass, jint i) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  Layer *l = E().doc()->layer(i);
  return env->NewStringUTF(l ? l->name.c_str() : "");
}
JF(jint, getLayerType)(JNIEnv *, jclass, jint i) {
  Layer *l = E().doc()->layer(i);
  return l ? (jint)l->type : 0;
}
JF(jboolean, isLayerVisible)(JNIEnv *, jclass, jint i) {
  Layer *l = E().doc()->layer(i);
  return l && l->visible;
}
JF(jboolean, isLayerLocked)(JNIEnv *, jclass, jint i) {
  Layer *l = E().doc()->layer(i);
  return l && l->locked;
}
JF(jint, getLayerOpacity)(JNIEnv *, jclass, jint i) {
  Layer *l = E().doc()->layer(i);
  return l ? l->opacity : 255;
}
JF(jint, addLayer)(JNIEnv *, jclass, jboolean raster) {
  return E().addLayer(raster);
}
JF(void, deleteLayer)(JNIEnv *, jclass, jint i) { E().deleteLayer(i); }
JF(void, moveLayer)(JNIEnv *, jclass, jint f, jint t) { E().moveLayer(f, t); }
JF(void, setLayerVisible)(JNIEnv *, jclass, jint i, jboolean v) {
  E().setLayerVisible(i, v);
}
JF(void, setLayerLocked)(JNIEnv *, jclass, jint i, jboolean v) {
  E().setLayerLocked(i, v);
}
JF(void, setLayerOpacity)(JNIEnv *, jclass, jint i, jint o) {
  E().setLayerOpacity(i, o);
}
JF(void, renameLayer)(JNIEnv *env, jclass, jint i, jstring n) {
  E().renameLayer(i, str(env, n));
}

// ------------------------------------------------------------------ onion
JF(void, setOnion)(JNIEnv *, jclass, jboolean en, jint back, jint fwd,
                   jint opacity) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  OnionSettings &o = E().doc()->onion;
  o.enabled = en;
  o.back    = std::max(0, std::min(10, (int)back));
  o.forward = std::max(0, std::min(10, (int)fwd));
  o.opacity = std::max(10, std::min(255, (int)opacity));
}
JF(jintArray, getOnion)(JNIEnv *env, jclass) {
  OnionSettings &o = E().doc()->onion;
  jint v[4] = {o.enabled, o.back, o.forward, o.opacity};
  jintArray a = env->NewIntArray(4);
  env->SetIntArrayRegion(a, 0, 4, v);
  return a;
}

// ------------------------------------------------------------------ palette
JF(jintArray, getStyleIds)(JNIEnv *env, jclass) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  auto s = E().styles();
  std::vector<jint> v;
  for (auto &st : s) v.push_back(st.id);
  jintArray a = env->NewIntArray((jsize)v.size());
  env->SetIntArrayRegion(a, 0, (jsize)v.size(), v.data());
  return a;
}
JF(jintArray, getStyleColors)(JNIEnv *env, jclass) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  auto s = E().styles();
  std::vector<jint> v;
  for (auto &st : s) v.push_back(argb(st.color));
  jintArray a = env->NewIntArray((jsize)v.size());
  env->SetIntArrayRegion(a, 0, (jsize)v.size(), v.data());
  return a;
}
JF(jint, addStyle)(JNIEnv *, jclass, jint c) { return E().addStyle(fromArgb(c)); }
JF(void, setStyleColor)(JNIEnv *, jclass, jint id, jint c) {
  E().setStyleColor(id, fromArgb(c));
}
JF(jint, getCurrentStyle)(JNIEnv *, jclass) { return E().doc()->currentStyle; }
JF(void, setCurrentStyle)(JNIEnv *, jclass, jint id) {
  std::lock_guard<std::recursive_mutex> lk(E().mutex());
  E().doc()->currentStyle = id;
}

// ------------------------------------------------------------------ files
JF(jstring, save)(JNIEnv *env, jclass, jstring dir) {
  std::string err;
  bool ok = E().save(str(env, dir), &err);
  return errOrNull(env, ok, err);
}
JF(jstring, load)(JNIEnv *env, jclass, jstring dir) {
  std::string err;
  bool ok = E().load(str(env, dir), &err);
  return errOrNull(env, ok, err);
}
JF(jboolean, importImageFile)(JNIEnv *env, jclass, jstring path,
                              jboolean newLayer, jint rowOffset) {
  return E().importImageFile(str(env, path), newLayer, rowOffset);
}
JF(jstring, exportGif)(JNIEnv *env, jclass, jstring path, jint maxSide) {
  std::string err;
  bool ok = E().exportGif(str(env, path), maxSide, &err);
  return errOrNull(env, ok, err);
}
JF(jstring, exportPngSequence)(JNIEnv *env, jclass, jstring dir) {
  std::string err;
  bool ok = E().exportPngSequence(str(env, dir), &err);
  return errOrNull(env, ok, err);
}
JF(jint, getMemoryKB)(JNIEnv *, jclass) { return (jint)E().memoryKB(); }
