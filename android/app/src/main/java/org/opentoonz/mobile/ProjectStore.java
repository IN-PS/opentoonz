package org.opentoonz.mobile;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * Projects live in <filesDir>/projects/<folder>/ (project.otm, palette.tpl,
 * levels/*.tlv|png written by the native engine) plus a thumbnail.png.
 */
final class ProjectStore {
    private ProjectStore() {}

    static File root(Context c) {
        File r = new File(c.getFilesDir(), "projects");
        if (!r.exists()) r.mkdirs();
        return r;
    }

    static List<File> list(Context c) {
        File[] dirs = root(c).listFiles();
        List<File> res = new ArrayList<>();
        if (dirs == null) return res;
        for (File d : dirs)
            if (d.isDirectory() && new File(d, "project.otm").exists()) res.add(d);
        File[] arr = res.toArray(new File[0]);
        Arrays.sort(arr, new Comparator<File>() {
            @Override public int compare(File a, File b) {
                return Long.compare(lastModified(b), lastModified(a));
            }
        });
        return new ArrayList<>(Arrays.asList(arr));
    }

    static long lastModified(File projectDir) {
        return new File(projectDir, "project.otm").lastModified();
    }

    static File newFolder(Context c, String name) {
        String base = name.replaceAll("[^A-Za-z0-9_\\- ]", "_").trim();
        if (base.isEmpty()) base = "project";
        File f = new File(root(c), base);
        int i = 2;
        while (f.exists()) f = new File(root(c), base + " " + (i++));
        return f;
    }

    static Bitmap thumbnail(File projectDir) {
        File t = new File(projectDir, "thumbnail.png");
        if (!t.exists()) return null;
        return BitmapFactory.decodeFile(t.getAbsolutePath());
    }

    /** Renders frame 0 of the open document as the project thumbnail. */
    static void writeThumbnail(File projectDir) {
        int w = NativeEngine.getWidth(), h = NativeEngine.getHeight();
        float s = 320f / Math.max(w, h);
        Bitmap b = Bitmap.createBitmap(Math.max(1, Math.round(w * s)),
                Math.max(1, Math.round(h * s)), Bitmap.Config.ARGB_8888);
        NativeEngine.renderThumb(b, 0);
        try (FileOutputStream out = new FileOutputStream(new File(projectDir, "thumbnail.png"))) {
            b.compress(Bitmap.CompressFormat.PNG, 90, out);
        } catch (IOException ignored) {
        }
        b.recycle();
    }

    static boolean deleteRecursive(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteRecursive(k);
        return f.delete();
    }
}
