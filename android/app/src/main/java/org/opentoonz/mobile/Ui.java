package org.opentoonz.mobile;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** Small helpers to build the touch UI in code (no AndroidX dependency). */
final class Ui {
    private Ui() {}

    static final int BG = 0xFF1B1E27, PANEL = 0xFF262A36, PANEL2 = 0xFF2F3442,
            ACCENT = 0xFFF5C342, TEXT = 0xFFE8EAF0, DIM = 0xFF8A90A0;

    static int dp(Context c, float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                c.getResources().getDisplayMetrics()));
    }

    static Drawable roundRect(int color, float radiusPx) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(radiusPx);
        return g;
    }

    static Drawable ripple(Context c, int color) {
        return new RippleDrawable(ColorStateList.valueOf(0x40FFFFFF),
                roundRect(color, dp(c, 8)), roundRect(Color.WHITE, dp(c, 8)));
    }

    static ImageButton iconButton(Context c, int iconRes, String desc, int sizeDp) {
        ImageButton b = new ImageButton(c);
        b.setImageResource(iconRes);
        b.setContentDescription(desc);
        b.setBackground(ripple(c, 0x00000000));
        int p = dp(c, 8);
        b.setPadding(p, p, p, p);
        b.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        b.setLayoutParams(new LinearLayout.LayoutParams(dp(c, sizeDp), dp(c, sizeDp)));
        return b;
    }

    static void setActive(ImageButton b, boolean active) {
        Context c = b.getContext();
        b.setBackground(ripple(c, active ? 0xFF4A5368 : 0x00000000));
        b.setColorFilter(active ? ACCENT : TEXT);
    }

    static TextView label(Context c, String text, float sp) {
        TextView t = new TextView(c);
        t.setText(text);
        t.setTextColor(TEXT);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setGravity(Gravity.CENTER_VERTICAL);
        return t;
    }

    interface IntListener { void onValue(int v); }

    /** Labelled slider row: "<label>  [=====o---]  value". */
    static LinearLayout slider(Context c, String label, int min, int max, int value,
                               final IntListener l) {
        LinearLayout row = new LinearLayout(c);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final TextView name = label(c, label, 14);
        name.setMinWidth(dp(c, 110));
        final TextView val = label(c, String.valueOf(value), 14);
        val.setMinWidth(dp(c, 36));
        val.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        final int fMin = min;
        SeekBar sb = new SeekBar(c);
        sb.setMax(max - min);
        sb.setProgress(value - min);
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean u) {
                val.setText(String.valueOf(p + fMin));
                if (u) l.onValue(p + fMin);
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        row.addView(name);
        row.addView(sb, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        row.addView(val);
        return row;
    }

    static View spacer(Context c, int dp) {
        View v = new View(c);
        v.setLayoutParams(new LinearLayout.LayoutParams(dp(c, dp), dp(c, dp)));
        return v;
    }
}
