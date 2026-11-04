package org.opentoonz.mobile;

import android.graphics.Bitmap;

import java.nio.ByteBuffer;

/**
 * JNI binding of the OpenToonz Mobile native engine (libotmengine.so), which
 * is built from the OpenToonz C++ core (tnzcore / toonzlib / image).
 * All coordinates are canvas pixels (origin top-left).
 * Dirty rectangles are returned as {x0, y0, x1, y1} (inclusive; x1 < x0 = none).
 */
public final class NativeEngine {
    private NativeEngine() {}

    public static final int TOOL_BRUSH = 0, TOOL_ERASER = 1, TOOL_FILL = 2,
            TOOL_SELECT = 3, TOOL_PICKER = 4, TOOL_SHAPE = 5, TOOL_TRANSFORM = 6;
    public static final int SHAPE_LINE = 0, SHAPE_RECT = 1, SHAPE_ELLIPSE = 2;
    public static final int EASE_LINEAR = 0, EASE_IN_OUT = 1, EASE_CONSTANT = 2;
    public static final int CAMERA = -1;
    public static final int FILL_AREAS = 0, FILL_LINES = 1, FILL_ALL = 2;
    public static final int ERASE_ALL = 0, ERASE_LINES = 1, ERASE_AREAS = 2;
    public static final int LAYER_TOONZ_RASTER = 0, LAYER_RASTER = 1;

    private static boolean sLoaded;

    /** Loads libotmengine.so (and, as its dependencies, Qt5Core + libc++). */
    public static synchronized void load() {
        if (sLoaded) return;
        System.loadLibrary("otmengine");
        sLoaded = true;
    }

    public static native void init(String cacheDir, String nativeLibDir);

    // document
    public static native void newDocument(int w, int h, int fps);
    public static native int getWidth();
    public static native int getHeight();
    public static native int getFps();
    public static native void setFps(int fps);
    public static native boolean isDirty();
    public static native String getName();
    public static native void setName(String name);

    // rendering
    public static native void render(Bitmap canvasSized, int row, int x0, int y0, int x1, int y1, boolean onion);
    public static native void renderThumb(Bitmap bmp, int row);
    public static native void renderYuv(int row, int w, int h,
                                        ByteBuffer y, int yRowStride, int yPixelStride,
                                        ByteBuffer u, int uRowStride, int uPixelStride,
                                        ByteBuffer v, int vRowStride, int vPixelStride);

    // tools
    public static native void setTool(int tool);
    public static native void setBrush(float minSize, float maxSize, boolean pressure, boolean antialias);
    public static native void setEraser(float size, int mode);
    public static native void setFill(int mode, int depth, boolean emptyOnly);
    public static native int[] pointerDown(float x, float y, float pressure);
    public static native int[] pointerMove(float x, float y, float pressure);
    public static native int[] pointerUp(float x, float y, float pressure);
    public static native int getPickedStyle();

    // selection
    public static native boolean selectRect(int x0, int y0, int x1, int y1);
    public static native void transformSelection(float dx, float dy, float scale, float angleDeg);
    public static native int[] commitSelection();
    public static native int[] cancelSelection();
    public static native int[] deleteSelection();
    public static native boolean hasSelection();
    public static native int[] selectionBounds();

    public static native boolean undo();
    public static native boolean redo();

    // timeline
    public static native int getFrameCount();
    public static native int getCurrentRow();
    public static native void setCurrentRow(int row);
    public static native int getCell(int layer, int row);
    public static native void insertBlankFrame();
    public static native void duplicateFrame();
    public static native void holdFrame();
    public static native void deleteFrame();
    public static native void clearCell();
    public static native void moveCell(int from, int to);

    // layers (index 0 = bottom)
    public static native int getLayerCount();
    public static native int getCurrentLayer();
    public static native void setCurrentLayer(int index);
    public static native String getLayerName(int index);
    public static native int getLayerType(int index);
    public static native boolean isLayerVisible(int index);
    public static native boolean isLayerLocked(int index);
    public static native int getLayerOpacity(int index);
    public static native int addLayer(boolean raster);
    public static native void deleteLayer(int index);
    public static native void moveLayer(int from, int to);
    public static native void setLayerVisible(int index, boolean visible);
    public static native void setLayerLocked(int index, boolean locked);
    public static native void setLayerOpacity(int index, int opacity);
    public static native void renameLayer(int index, String name);

    // onion skin: {enabled, back, forward, opacity}
    public static native void setOnion(boolean enabled, int back, int forward, int opacity);
    public static native int[] getOnion();

    // palette (OpenToonz TPalette styles; colours as ARGB ints)
    public static native int[] getStyleIds();
    public static native int[] getStyleColors();
    public static native int addStyle(int argb);
    public static native void setStyleColor(int id, int argb);
    public static native int getCurrentStyle();
    public static native void setCurrentStyle(int id);

    // files: return null on success, otherwise an error message
    public static native String save(String dir);
    public static native String load(String dir);
    public static native boolean importBitmap(Bitmap bmp, boolean newLayer, int rowOffset, String name);
    public static native boolean importImageFile(String path, boolean newLayer, int rowOffset);
    public static native String exportGif(String path, int maxSide);
    public static native String exportPngSequence(String dir);
    public static native int getMemoryKB();

    // v0.2: professional brush, shapes, lasso, timing, keyframes
    public static native void setBrushPro(int hardness, int opacity, int stabilizer, int eraserHardness);
    public static native int[] drawShape(int type, float x0, float y0, float x1, float y1);
    /** Lasso selection; xy = {x0, y0, x1, y1, ...} in canvas pixels. */
    public static native boolean selectPolygon(float[] xy);
    public static native void reframe(int step);
    public static native void setExposure(int frames);
    /** Transform at a row: {x, y, rotationDeg, scale, opacity}; layer CAMERA = camera. */
    public static native float[] getXform(int layer, int row);
    public static native boolean hasKey(int layer, int row);
    public static native int getKeyEase(int layer, int row);
    public static native void setKey(int layer, int row, float[] xform, int ease);
    public static native void removeKey(int layer, int row);
    public static native void beginKeyEdit(int layer);
    public static native void setKeyLive(int layer, int row, float[] xform, int ease);
    public static native void endKeyEdit();
    public static native int[] getKeyRows(int layer);
}
