package org.opentoonz.mobile;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.LruCache;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * MyPaint brush presets shipped with OpenToonz (stuff/library/mypaint brushes,
 * CC0), packaged as APK assets under "mypaint/<group>/<name>.myb" with their
 * "<name>_prev.png" previews.
 */
final class MyPaintPicker {
    private MyPaintPicker() {}

    static final String ROOT = "mypaint";

    interface Listener {
        /** path = "group/name" (without .myb), or null for the OpenToonz brush. */
        void onPicked(String path);
    }

    /** All presets as "group/name", sorted by group then name. */
    static List<String> list(Context c) {
        List<String> out = new ArrayList<>();
        AssetManager am = c.getAssets();
        try {
            String[] groups = am.list(ROOT);
            if (groups == null) return out;
            Arrays.sort(groups);
            for (String g : groups) {
                String[] files = am.list(ROOT + "/" + g);
                if (files == null) continue;
                Arrays.sort(files);
                for (String f : files)
                    if (f.endsWith(".myb")) out.add(g + "/" + f.substring(0, f.length() - 4));
            }
        } catch (IOException ignored) {
        }
        return out;
    }

    /** Contents of a preset (.myb JSON), or null. */
    static String read(Context c, String path) {
        try (InputStream in = c.getAssets().open(ROOT + "/" + path + ".myb")) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
            return bo.toString("UTF-8");
        } catch (IOException e) {
            return null;
        }
    }

    static String displayName(String path) {
        if (path == null) return null;
        return path.replace('_', ' ');
    }

    static void show(final Context c, String current, final Listener l) {
        final List<String> items = new ArrayList<>();
        items.add(null);  // OpenToonz brush engine
        items.addAll(list(c));
        final LruCache<String, Bitmap> cache = new LruCache<>(64);
        final int cell = Ui.dp(c, 96);
        GridView grid = new GridView(c);
        grid.setNumColumns(GridView.AUTO_FIT);
        grid.setColumnWidth(cell);
        grid.setVerticalSpacing(Ui.dp(c, 6));
        grid.setHorizontalSpacing(Ui.dp(c, 6));
        grid.setPadding(Ui.dp(c, 8), Ui.dp(c, 8), Ui.dp(c, 8), Ui.dp(c, 8));
        final String cur = current;
        grid.setAdapter(new BaseAdapter() {
            @Override public int getCount() { return items.size(); }
            @Override public Object getItem(int i) { return items.get(i); }
            @Override public long getItemId(int i) { return i; }
            @Override public View getView(int i, View v, ViewGroup parent) {
                LinearLayout box;
                ImageView img;
                TextView t;
                if (v instanceof LinearLayout) {
                    box = (LinearLayout) v;
                    img = (ImageView) box.getChildAt(0);
                    t = (TextView) box.getChildAt(1);
                } else {
                    box = new LinearLayout(c);
                    box.setOrientation(LinearLayout.VERTICAL);
                    box.setGravity(Gravity.CENTER_HORIZONTAL);
                    img = new ImageView(c);
                    img.setScaleType(ImageView.ScaleType.FIT_CENTER);
                    box.addView(img, new LinearLayout.LayoutParams(cell, Ui.dp(c, 64)));
                    t = Ui.label(c, "", 11);
                    t.setGravity(Gravity.CENTER);
                    t.setMaxLines(2);
                    box.addView(t, new LinearLayout.LayoutParams(cell, ViewGroup.LayoutParams.WRAP_CONTENT));
                }
                String p = items.get(i);
                boolean sel = p == null ? cur == null : p.equals(cur);
                box.setBackground(sel ? Ui.roundRect(0x553D7EFF, Ui.dp(c, 6)) : null);
                if (p == null) {
                    img.setImageResource(R.drawable.ic_brush);
                    t.setText(R.string.mypaint_off);
                } else {
                    t.setText(displayName(p));
                    box.setContentDescription(p);
                    Bitmap b = cache.get(p);
                    if (b == null) {
                        try (InputStream in = c.getAssets().open(ROOT + "/" + p + "_prev.png")) {
                            b = BitmapFactory.decodeStream(in);
                        } catch (IOException ignored) {
                        }
                        if (b != null) cache.put(p, b);
                    }
                    img.setImageBitmap(b);
                }
                return box;
            }
        });
        final AlertDialog d = new AlertDialog.Builder(c).setTitle(R.string.mypaint_brushes)
                .setView(grid).setNegativeButton(android.R.string.cancel, null).create();
        grid.setOnItemClickListener(new android.widget.AdapterView.OnItemClickListener() {
            @Override public void onItemClick(android.widget.AdapterView<?> a, View v, int i, long id) {
                d.dismiss();
                l.onPicked(items.get(i));
            }
        });
        d.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override public void onDismiss(DialogInterface di) { cache.evictAll(); }
        });
        d.show();
    }
}
