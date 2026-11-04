package org.opentoonz.mobile;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.view.View;
import android.widget.LinearLayout;

/** HSV colour picker used to create / edit palette styles. */
final class ColorPickerDialog {
    interface Callback { void onColor(int argb); }

    static void show(Context c, String title, int initial, final Callback cb) {
        final float[] hsv = new float[3];
        Color.colorToHSV(initial, hsv);
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(c, 16);
        l.setPadding(p, p, p, 0);
        final View preview = new View(c);
        preview.setBackground(Ui.roundRect(initial | 0xFF000000, Ui.dp(c, 8)));
        l.addView(preview, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Ui.dp(c, 56)));
        final Runnable update = new Runnable() {
            @Override public void run() {
                preview.setBackground(Ui.roundRect(Color.HSVToColor(hsv), Ui.dp(preview.getContext(), 8)));
            }
        };
        l.addView(Ui.slider(c, c.getString(R.string.hue), 0, 360, Math.round(hsv[0]), new Ui.IntListener() {
            @Override public void onValue(int v) { hsv[0] = v; update.run(); }
        }));
        l.addView(Ui.slider(c, c.getString(R.string.saturation), 0, 100, Math.round(hsv[1] * 100), new Ui.IntListener() {
            @Override public void onValue(int v) { hsv[1] = v / 100f; update.run(); }
        }));
        l.addView(Ui.slider(c, c.getString(R.string.value), 0, 100, Math.round(hsv[2] * 100), new Ui.IntListener() {
            @Override public void onValue(int v) { hsv[2] = v / 100f; update.run(); }
        }));
        new AlertDialog.Builder(c).setTitle(title).setView(l)
                .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) { cb.onColor(Color.HSVToColor(hsv)); }
                }).setNegativeButton(R.string.cancel, null).show();
    }
}
