package org.opentoonz.mobile;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.widget.LinearLayout;
import android.widget.ScrollView;

/** Layer effects editor (blur, glow, drop shadow, colour adjustment). */
final class FxDialog {
    interface Done { void onApplied(); }

    static void show(final Context c, final int layer, final Done done) {
        final int[] fx = NativeEngine.getLayerFx(layer);
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(c, 16);
        l.setPadding(p, p, p, 0);
        int[][] rows = {   // index, label, min, max
                {0, R.string.blur, 0, 50}, {1, R.string.glow, 0, 50}, {2, R.string.glow_intensity, 0, 300},
                {6, R.string.shadow_x, -100, 100}, {7, R.string.shadow_y, -100, 100},
                {8, R.string.shadow_blur, 0, 50}, {9, R.string.shadow_opacity, 0, 100},
                {13, R.string.brightness, -100, 100}, {14, R.string.contrast, -100, 100},
                {15, R.string.saturation, -100, 100}, {16, R.string.hue, -180, 180}};
        for (final int[] r : rows)
            l.addView(Ui.slider(c, c.getString(r[1]), r[2], r[3], fx[r[0]], new Ui.IntListener() {
                @Override public void onValue(int v) { fx[r[0]] = v; }
            }));
        ScrollView sv = new ScrollView(c);
        sv.addView(l);
        new AlertDialog.Builder(c).setTitle(R.string.effects).setView(sv)
                .setPositiveButton(R.string.apply, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        NativeEngine.setLayerFx(layer, fx);
                        if (done != null) done.onApplied();
                    }
                })
                .setNeutralButton(R.string.none,
                        new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        int[] zero = new int[17];
                        zero[2] = 100;
                        zero[3] = 255; zero[4] = 236; zero[5] = 150;
                        NativeEngine.setLayerFx(layer, zero);
                        if (done != null) done.onApplied();
                    }
                })
                .setNegativeButton(R.string.cancel, null).show();
    }
}
