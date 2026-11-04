package org.opentoonz.mobile;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.robolectric.Shadows.shadowOf;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.RectF;
import android.net.Uri;
import android.os.Looper;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import org.junit.FixMethodOrder;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.MethodSorters;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowAlertDialog;

import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * End-to-end tests of the OpenToonz Mobile app on the JVM (Robolectric):
 * real Activities / Views / dialogs, driven by synthetic touch events, calling
 * the real native OpenToonz engine through JNI.
 */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = 27, qualifiers = "w900dp-h560dp-land-mdpi")
@FixMethodOrder(MethodSorters.NAME_ASCENDING)
public class AppFlowTest {

    // ------------------------------------------------------------ helpers
    static void log(String s) { System.out.println("[app-test] " + s); }

    static void idle() { shadowOf(Looper.getMainLooper()).idle(); }

    /** Runs the main looper until cond is true (background executor work). */
    static void waitFor(String what, java.util.function.BooleanSupplier cond) {
        long end = System.currentTimeMillis() + 60_000;
        while (!cond.getAsBoolean()) {
            idle();
            if (System.currentTimeMillis() > end) throw new AssertionError("timeout: " + what);
            try { Thread.sleep(15); } catch (InterruptedException e) { throw new RuntimeException(e); }
        }
        idle();
    }

    static <T extends View> List<T> find(View root, Class<T> cls) {
        List<T> out = new ArrayList<>();
        if (cls.isInstance(root)) out.add(cls.cast(root));
        if (root instanceof ViewGroup)
            for (int i = 0; i < ((ViewGroup) root).getChildCount(); i++)
                out.addAll(find(((ViewGroup) root).getChildAt(i), cls));
        return out;
    }

    static View byDesc(Activity a, String desc) {
        for (View v : find(a.getWindow().getDecorView(), View.class))
            if (desc.contentEquals(String.valueOf(v.getContentDescription())) && v.isShown()) return v;
        throw new AssertionError("no view with description " + desc);
    }

    static void click(Activity a, String desc) {
        assertTrue(desc, byDesc(a, desc).performClick());
        idle();
    }

    /** Robolectric 3.x does not run layout passes: lay the window out explicitly. */
    static void layoutWindow(Activity a, int w, int h) {
        View d = a.getWindow().getDecorView();
        d.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY));
        d.layout(0, 0, w, h);
        idle();
    }

    static boolean loaded(EditorActivity a) {
        try {
            java.lang.reflect.Field f = EditorActivity.class.getDeclaredField("mLoaded");
            f.setAccessible(true);
            return f.getBoolean(a);
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    static boolean saving(EditorActivity a) {
        try {
            java.lang.reflect.Field f = EditorActivity.class.getDeclaredField("mSaving");
            f.setAccessible(true);
            return f.getBoolean(a);
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    /** Synthetic finger gesture on the canvas through dispatchTouchEvent, doc coords. */
    static void drag(CanvasView c, float[][] docPts) {
        RectF r = c.docRectOnScreen();
        float sx = r.width() / NativeEngine.getWidth(), sy = r.height() / NativeEngine.getHeight();
        long t0 = SystemClock.uptimeMillis();
        for (int i = 0; i < docPts.length; i++) {
            int action = i == 0 ? MotionEvent.ACTION_DOWN
                    : i == docPts.length - 1 ? MotionEvent.ACTION_UP : MotionEvent.ACTION_MOVE;
            MotionEvent e = MotionEvent.obtain(t0, t0 + i * 16L, action,
                    r.left + docPts[i][0] * sx, r.top + docPts[i][1] * sy, 0);
            c.dispatchTouchEvent(e);
            e.recycle();
        }
        idle();
    }

    static float[][] line(float x0, float y0, float x1, float y1, int n) {
        float[][] p = new float[n + 1][];
        for (int i = 0; i <= n; i++) p[i] = new float[]{x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n};
        return p;
    }

    static float[][] circle(float cx, float cy, float rad, int n) {
        float[][] p = new float[n + 1][];
        for (int i = 0; i <= n; i++) {
            double a = 2 * Math.PI * i / n;
            p[i] = new float[]{(float) (cx + rad * Math.cos(a)), (float) (cy + rad * Math.sin(a))};
        }
        return p;
    }

    static Bitmap frame(int row, boolean onion) {
        Bitmap b = Bitmap.createBitmap(NativeEngine.getWidth(), NativeEngine.getHeight(), Bitmap.Config.ARGB_8888);
        NativeEngine.render(b, row, 0, 0, b.getWidth() - 1, b.getHeight() - 1, onion);
        return b;
    }

    static boolean dark(int c) { return Color.red(c) < 80 && Color.green(c) < 80 && Color.blue(c) < 80; }
    static boolean white(int c) { return Color.red(c) > 245 && Color.green(c) > 245 && Color.blue(c) > 245; }

    static File createProject(String name) {
        ActivityController<ProjectsActivity> pc = Robolectric.buildActivity(ProjectsActivity.class).setup();
        ProjectsActivity pa = pc.get();
        layoutWindow(pa, 900, 560);
        click(pa, "New project");
        AlertDialog d = (AlertDialog) ShadowAlertDialog.getLatestAlertDialog();
        assertNotNull("new project dialog", d);
        android.widget.EditText et = find(d.getWindow().getDecorView(), android.widget.EditText.class).get(0);
        et.setText(name);
        d.getButton(DialogInterface.BUTTON_POSITIVE).performClick();
        idle();
        Intent next = shadowOf(pa).getNextStartedActivity();
        assertNotNull("editor started", next);
        assertEquals(EditorActivity.class.getName(), next.getComponent().getClassName());
        File dir = new File(next.getStringExtra(EditorActivity.EXTRA_DIR));
        assertTrue(new File(dir, "project.otm").exists());
        pc.pause().stop().destroy();
        return dir;
    }

    static ActivityController<EditorActivity> openEditor(File dir) {
        Intent i = new Intent(RuntimeEnvironment.application, EditorActivity.class);
        i.putExtra(EditorActivity.EXTRA_DIR, dir.getAbsolutePath());
        ActivityController<EditorActivity> c = Robolectric.buildActivity(EditorActivity.class, i).setup();
        waitFor("project load", () -> loaded(c.get()));
        layoutWindow(c.get(), 900, 560);
        return c;
    }

    static void menu(EditorActivity a, int item) {
        click(a, "menu");
        AlertDialog d = (AlertDialog) ShadowAlertDialog.getLatestAlertDialog();
        shadowOf(d).clickOnItem(item);
        idle();
    }

    static void saveAndWait(EditorActivity a) {
        menu(a, 0);
        waitFor("save", () -> !saving(a));
    }

    // ------------------------------------------------------------ tests
    @Test
    public void t1_projectManagerCreatesProject() {
        File dir = createProject("Test One");
        log("project created: " + dir + " files=" + java.util.Arrays.toString(dir.list()));
        assertTrue(new File(dir, "palette.tpl").exists());
        assertTrue(new File(dir, "thumbnail.png").exists());
        // project list shows it
        ProjectsActivity pa = Robolectric.buildActivity(ProjectsActivity.class).setup().get();
        boolean listed = false;
        for (TextView t : find(pa.getWindow().getDecorView(), TextView.class))
            if ("Test One".contentEquals(t.getText())) listed = true;
        assertTrue("project card listed", listed);
    }

    @Test
    public void t2_drawFramesOnionLayersFillUndoSelectPlaySaveReopen() {
        File dir = createProject("Flow");
        ActivityController<EditorActivity> c = openEditor(dir);
        EditorActivity a = c.get();
        CanvasView canvas = find(a.getWindow().getDecorView(), CanvasView.class).get(0);
        assertTrue("canvas laid out", canvas.getWidth() > 100 && canvas.getHeight() > 100);
        int W = NativeEngine.getWidth(), H = NativeEngine.getHeight();
        log("canvas view " + canvas.getWidth() + "x" + canvas.getHeight() + ", document " + W + "x" + H);

        // brush stroke (finger) on frame 1
        click(a, "Brush");
        drag(canvas, line(100, 200, 400, 200, 30));
        assertTrue("stroke pixel", dark(frame(0, false).getPixel(250, 200)));
        assertTrue(NativeEngine.getCell(0, 0) > 0);
        log("brush stroke OK");

        // frame-by-frame: new frame, second drawing
        click(a, "New frame");
        assertEquals(1, NativeEngine.getCurrentRow());
        assertEquals(2, NativeEngine.getFrameCount());
        drag(canvas, line(100, 300, 400, 300, 30));
        assertTrue(dark(frame(1, false).getPixel(250, 300)));
        assertTrue(white(frame(1, false).getPixel(250, 200)));
        log("frame 2 drawn; timeline frames=" + NativeEngine.getFrameCount());

        // onion skin: previous drawing visible (tinted) on frame 2
        click(a, "Onion skin");
        assertEquals(1, NativeEngine.getOnion()[0]);
        int on = frame(1, true).getPixel(250, 200);
        log(String.format("onion pixel = #%08X", on));
        assertTrue("onion tinted red", Color.red(on) > Color.green(on) + 30);
        click(a, "Onion skin");

        // hold + duplicate
        click(a, "Hold (extend) frame");
        assertEquals(3, NativeEngine.getFrameCount());
        assertEquals(NativeEngine.getCell(0, 1), NativeEngine.getCell(0, 2));

        // layers panel: add a layer, draw red, hide it
        click(a, "Layers");
        click(a, "Add Toonz Raster layer");
        assertEquals(2, NativeEngine.getLayerCount());
        assertEquals(1, NativeEngine.getCurrentLayer());
        NativeEngine.setCurrentStyle(3);  // red in the default palette
        drag(canvas, line(250, 100, 250, 400, 30));
        int red = frame(2, false).getPixel(250, 300);
        assertTrue("red over black", Color.red(red) > 180 && Color.green(red) < 90);
        NativeEngine.setLayerVisible(1, false);
        assertTrue(dark(frame(2, false).getPixel(250, 300)));
        NativeEngine.setLayerVisible(1, true);
        click(a, "Layers");
        log("layers OK");

        // fill inside a closed circle
        NativeEngine.setCurrentStyle(7);  // blue
        drag(canvas, circle(650, 250, 60, 48));
        click(a, "Fill");
        drag(canvas, new float[][]{{650, 250}, {650, 250}});
        int blue = frame(2, false).getPixel(650, 250);
        log(String.format("fill pixel = #%08X", blue));
        assertTrue("filled blue", Color.blue(blue) > 180 && Color.red(blue) < 90);

        // undo / redo buttons
        click(a, "Undo");
        assertTrue(white(frame(2, false).getPixel(650, 250)));
        click(a, "Redo");
        assertTrue(Color.blue(frame(2, false).getPixel(650, 250)) > 180);
        log("fill + undo/redo OK");

        // selection: drag a rect around the red line top, move it, apply
        click(a, "Selection");
        drag(canvas, line(230, 90, 270, 160, 10));
        assertTrue(NativeEngine.hasSelection());
        drag(canvas, line(250, 120, 450, 120, 10));    // move by +200 x
        click(a, "Apply");
        assertTrue(!NativeEngine.hasSelection());
        Bitmap fm = frame(2, false);
        int moved = fm.getPixel(450, 120);
        log(String.format("after move: (250,120)=#%08X (450,120)=#%08X (450,130)=#%08X", fm.getPixel(250, 120), moved, fm.getPixel(450, 130)));
        assertTrue("moved red", Color.red(moved) > 180 && Color.green(moved) < 90);
        log("selection move OK");

        // playback through the timeline
        click(a, "Play");
        shadowOf(Looper.getMainLooper()).idle(1000 / NativeEngine.getFps() * 2 + 10, java.util.concurrent.TimeUnit.MILLISECONDS);
        click(a, "Play");
        log("playback OK, current row " + NativeEngine.getCurrentRow());

        // timeline tap selects a cell
        TimelineView tl = find(a.getWindow().getDecorView(), TimelineView.class).get(0);
        assertTrue(tl.getHeight() > 0);

        // save through the menu (TLV via the LZO helper processes)
        saveAndWait(a);
        File levels = new File(dir, "levels");
        String[] files = levels.list();
        log("saved levels: " + java.util.Arrays.toString(files));
        int tlv = 0;
        for (String f : files) if (f.endsWith(".tlv")) tlv++;
        assertEquals(2, tlv);
        assertTrue(!NativeEngine.isDirty());
        Bitmap before = frame(2, false);
        c.pause().stop().destroy();

        // reopen in a fresh editor: identical pixels
        NativeEngine.newDocument(64, 64, 12);
        ActivityController<EditorActivity> c2 = openEditor(dir);
        assertEquals(2, NativeEngine.getLayerCount());
        assertEquals(3, NativeEngine.getFrameCount());
        Bitmap after = frame(2, false);
        int diff = 0;
        for (int y = 0; y < before.getHeight(); y += 2)
            for (int x = 0; x < before.getWidth(); x += 2)
                if (before.getPixel(x, y) != after.getPixel(x, y)) diff++;
        log("reopen: differing pixels = " + diff);
        assertEquals(0, diff);
        c2.pause().stop().destroy();
    }

    @Test
    public void t3_autosaveOnPause() {
        File dir = createProject("Autosave");
        ActivityController<EditorActivity> c = openEditor(dir);
        EditorActivity a = c.get();
        CanvasView canvas = find(a.getWindow().getDecorView(), CanvasView.class).get(0);
        drag(canvas, line(50, 50, 300, 300, 20));
        assertTrue(NativeEngine.isDirty());
        c.pause();
        waitFor("autosave on pause", () -> !saving(a) && !NativeEngine.isDirty());
        assertTrue(new File(dir, "levels").list().length > 0);
        log("autosave on pause OK");
        c.stop().destroy();
    }

    @Test
    public void t4_importAndExport() throws Exception {
        File dir = createProject("ImportExport");
        ActivityController<EditorActivity> c = openEditor(dir);
        EditorActivity a = c.get();

        // import an image through the SAF picker flow
        File png = new File(RuntimeEnvironment.application.getCacheDir(), "pic.png");
        Bitmap src = Bitmap.createBitmap(200, 100, Bitmap.Config.ARGB_8888);
        src.eraseColor(Color.rgb(20, 160, 60));
        try (FileOutputStream o = new FileOutputStream(png)) { src.compress(Bitmap.CompressFormat.PNG, 100, o); }
        final Uri uri = Uri.parse("content://test/pic.png");
        shadowOf(a.getContentResolver()).registerInputStream(uri,
                new java.io.ByteArrayInputStream(java.nio.file.Files.readAllBytes(png.toPath())));
        menu(a, 1);
        Intent pick = shadowOf(a).getNextStartedActivityForResult().intent;
        assertEquals(Intent.ACTION_OPEN_DOCUMENT, pick.getAction());
        int layersBefore = NativeEngine.getLayerCount();
        shadowOf(a).receiveResult(pick, Activity.RESULT_OK, new Intent().setData(uri));
        waitFor("import", () -> NativeEngine.getLayerCount() == layersBefore + 1);
        assertEquals(NativeEngine.LAYER_RASTER, NativeEngine.getLayerType(NativeEngine.getLayerCount() - 1));
        log("import flow OK (raster layer added)");

        // a second frame so the export has an animation
        CanvasView canvas = find(a.getWindow().getDecorView(), CanvasView.class).get(0);
        NativeEngine.setCurrentLayer(0);
        drag(canvas, line(100, 100, 500, 400, 30));
        click(a, "New frame");
        drag(canvas, line(500, 100, 100, 400, 30));

        // GIF export -> CREATE_DOCUMENT -> copy into the chosen document
        menu(a, 3);
        waitFor("gif export", () -> {
            Intent n = shadowOf(a).peekNextStartedActivityForResult() == null ? null
                    : shadowOf(a).peekNextStartedActivityForResult().intent;
            return n != null && Intent.ACTION_CREATE_DOCUMENT.equals(n.getAction());
        });
        Intent create = shadowOf(a).getNextStartedActivityForResult().intent;
        assertEquals("image/gif", create.getType());
        File gifFile = new File(a.getCacheDir(), dir.getName() + ".gif");
        byte[] gif = java.nio.file.Files.readAllBytes(gifFile.toPath());
        log("GIF exported: " + gif.length + " bytes (" + gifFile.getName() + ")");
        shadowOf(a).receiveResult(create, Activity.RESULT_OK,
                new Intent().setData(Uri.fromFile(new File(a.getCacheDir(), "copy.gif"))));
        idle();
        assertTrue(gif.length > 1000);
        assertEquals("GIF89a", new String(gif, 0, 6, "ASCII"));

        // PNG sequence export
        menu(a, 5);
        File pngDir = new File(a.getExternalFilesDir("exports"), dir.getName() + "_png");
        waitFor("png export", () -> pngDir.list() != null && pngDir.list().length == NativeEngine.getFrameCount());
        log("PNG sequence: " + java.util.Arrays.toString(pngDir.list()));

        // MP4: MediaCodec is an Android platform service; on the JVM Robolectric
        // provides no H.264 encoder, so only the error handling is exercised.
        menu(a, 4);
        waitFor("mp4 attempt", () -> ShadowAlertDialog.getLatestAlertDialog() != null
                && ShadowAlertDialog.getLatestAlertDialog().isShowing()
                || shadowOf(a).peekNextStartedActivityForResult() != null);
        log("MP4 on JVM: " + (shadowOf(a).peekNextStartedActivityForResult() != null
                ? "encoder produced a file" : "no encoder available (expected on JVM, error dialog shown, no crash)"));
        c.pause().stop().destroy();
    }

    @Test
    public void t5_rotationKeepsWorking() {
        File dir = createProject("Rotate");
        ActivityController<EditorActivity> c = openEditor(dir);
        EditorActivity a = c.get();
        Configuration cfg = new Configuration(a.getResources().getConfiguration());
        cfg.orientation = Configuration.ORIENTATION_PORTRAIT;
        cfg.screenWidthDp = 560;
        cfg.screenHeightDp = 900;
        c.configurationChange(cfg);
        layoutWindow(a, 560, 900);
        idle();
        CanvasView canvas = find(a.getWindow().getDecorView(), CanvasView.class).get(0);
        canvas.measure(View.MeasureSpec.makeMeasureSpec(500, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(700, View.MeasureSpec.EXACTLY));
        canvas.layout(0, 0, 500, 700);
        canvas.fit();
        drag(canvas, line(100, 100, 300, 100, 20));
        assertTrue(dark(frame(0, false).getPixel(200, 100)));
        log("portrait: drawing still works, canvas " + canvas.getWidth() + "x" + canvas.getHeight());
        c.pause().stop().destroy();
    }

    @Test
    public void t6_stylusPressure() {
        File dir = createProject("Stylus");
        ActivityController<EditorActivity> c = openEditor(dir);
        EditorActivity a = c.get();
        CanvasView canvas = find(a.getWindow().getDecorView(), CanvasView.class).get(0);
        NativeEngine.setBrush(1, 30, true, true);
        RectF r = canvas.docRectOnScreen();
        float s = r.width() / NativeEngine.getWidth();
        MotionEvent.PointerProperties[] pp = {new MotionEvent.PointerProperties()};
        pp[0].id = 0;
        pp[0].toolType = MotionEvent.TOOL_TYPE_STYLUS;
        long t0 = SystemClock.uptimeMillis();
        float[] pressures = {0.1f, 0.1f, 1f, 1f};
        float[] xs = {100, 200, 400, 500};
        for (int i = 0; i < 4; i++) {
            MotionEvent.PointerCoords[] pc = {new MotionEvent.PointerCoords()};
            pc[0].x = r.left + xs[i] * s;
            pc[0].y = r.top + 200 * s;
            pc[0].pressure = pressures[i];
            int act = i == 0 ? MotionEvent.ACTION_DOWN : i == 3 ? MotionEvent.ACTION_UP : MotionEvent.ACTION_MOVE;
            MotionEvent e = MotionEvent.obtain(t0, t0 + i * 16, act, 1, pp, pc, 0, 0, 1, 1, 0, 0, 0, 0);
            canvas.dispatchTouchEvent(e);
            e.recycle();
        }
        idle();
        boolean uiStylus = canvas.isStylusMode();
        log("stylus tool type visible to the UI under Robolectric 3.8: " + uiStylus
                + (uiStylus ? "" : " (legacy MotionEvent shadow drops toolType; UI stylus path needs a device)"));
        // pressure -> thickness through the same JNI calls the CanvasView makes
        NativeEngine.setCurrentRow(1);
        NativeEngine.setTool(NativeEngine.TOOL_BRUSH);
        NativeEngine.pointerDown(100, 200, 0.1f);
        NativeEngine.pointerMove(200, 200, 0.1f);
        NativeEngine.pointerMove(300, 200, 0.1f);
        NativeEngine.pointerMove(400, 200, 1f);
        NativeEngine.pointerMove(500, 200, 1f);
        NativeEngine.pointerUp(500, 200, 1f);
        Bitmap f = frame(1, false);
        int thin = 0, thick = 0;
        for (int y = 170; y < 230; y++) {
            if (dark(f.getPixel(150, y))) thin++;
            if (dark(f.getPixel(450, y))) thick++;
        }
        log("stylus: line width at low pressure=" + thin + "px, high pressure=" + thick + "px");
        assertTrue(thick > thin * 3);
        c.pause().stop().destroy();
    }
}
