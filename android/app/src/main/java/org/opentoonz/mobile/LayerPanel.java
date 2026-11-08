package org.opentoonz.mobile;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.view.Gravity;
import android.view.View;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Layer panel: visibility, lock, opacity, order, add / delete / rename. */
public class LayerPanel extends LinearLayout {
    public interface Listener { void onLayersChanged(); }

    private final LinearLayout mList;
    private Listener mListener;

    public LayerPanel(Context c) {
        super(c);
        setOrientation(VERTICAL);
        setBackgroundColor(0xF0262A36);
        int p = Ui.dp(c, 6);
        setPadding(p, p, p, p);
        LinearLayout head = new LinearLayout(c);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.addView(Ui.label(c, c.getString(R.string.layers), 16),
                new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        ImageButton addToonz = Ui.iconButton(c, R.drawable.ic_add, c.getString(R.string.add_layer), 40);
        addToonz.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.addLayer(false); changed(); }
        });
        ImageButton addRaster = Ui.iconButton(c, R.drawable.ic_image, c.getString(R.string.add_raster_layer), 40);
        addRaster.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.addLayer(true); changed(); }
        });
        ImageButton addVector = Ui.iconButton(c, R.drawable.ic_vector, c.getString(R.string.add_vector_layer), 40);
        addVector.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.addLayerType(NativeEngine.LAYER_VECTOR); changed(); }
        });
        head.addView(addToonz);
        head.addView(addRaster);
        head.addView(addVector);
        addView(head);
        ScrollView sv = new ScrollView(c);
        mList = new LinearLayout(c);
        mList.setOrientation(VERTICAL);
        sv.addView(mList);
        addView(sv, new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1));
    }

    public void setListener(Listener l) { mListener = l; }

    private void changed() {
        refresh();
        if (mListener != null) mListener.onLayersChanged();
    }

    public void refresh() {
        mList.removeAllViews();
        final Context c = getContext();
        int n = NativeEngine.getLayerCount(), cur = NativeEngine.getCurrentLayer();
        for (int i = n - 1; i >= 0; i--) {   // top layer first
            final int idx = i;
            LinearLayout row = new LinearLayout(c);
            row.setOrientation(VERTICAL);
            row.setBackground(Ui.ripple(c, i == cur ? 0xFF3E4659 : 0xFF2F3442));
            LayoutParams lp = new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT);
            lp.topMargin = Ui.dp(c, 3);
            LinearLayout line = new LinearLayout(c);
            line.setGravity(Gravity.CENTER_VERTICAL);
            final ImageButton eye = Ui.iconButton(c, R.drawable.ic_eye, "visible", 36);
            eye.setAlpha(NativeEngine.isLayerVisible(i) ? 1f : 0.3f);
            eye.setOnClickListener(new OnClickListener() {
                @Override public void onClick(View v) {
                    NativeEngine.setLayerVisible(idx, !NativeEngine.isLayerVisible(idx));
                    changed();
                }
            });
            final ImageButton lock = Ui.iconButton(c, R.drawable.ic_lock, "lock", 36);
            lock.setAlpha(NativeEngine.isLayerLocked(i) ? 1f : 0.3f);
            lock.setOnClickListener(new OnClickListener() {
                @Override public void onClick(View v) {
                    NativeEngine.setLayerLocked(idx, !NativeEngine.isLayerLocked(idx));
                    changed();
                }
            });
            int lt = NativeEngine.getLayerType(i);
            String type = lt == NativeEngine.LAYER_RASTER ? "Raster"
                    : lt == NativeEngine.LAYER_VECTOR ? c.getString(R.string.vector) : "Toonz Raster";
            int par = NativeEngine.getParent(i);
            if (par >= 0) type += "  \u2192 " + NativeEngine.getLayerName(par);
            TextView name = Ui.label(c, NativeEngine.getLayerName(i), 14);
            name.setSingleLine(true);
            TextView badge = Ui.label(c, type, 10);
            badge.setTextColor(Ui.DIM);
            LinearLayout names = new LinearLayout(c);
            names.setOrientation(VERTICAL);
            names.addView(name);
            names.addView(badge);
            line.addView(eye);
            line.addView(lock);
            line.addView(names, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
            row.addView(line);
            row.setOnClickListener(new OnClickListener() {
                @Override public void onClick(View v) {
                    NativeEngine.setCurrentLayer(idx);
                    changed();
                }
            });
            row.setOnLongClickListener(new OnLongClickListener() {
                @Override public boolean onLongClick(View v) { rename(idx); return true; }
            });
            if (i == cur) {
                row.addView(Ui.slider(c, c.getString(R.string.opacity), 0, 100,
                        Math.round(NativeEngine.getLayerOpacity(i) * 100f / 255f), new Ui.IntListener() {
                            @Override public void onValue(int v) {
                                NativeEngine.setLayerOpacity(idx, Math.round(v * 255f / 100f));
                                if (mListener != null) mListener.onLayersChanged();
                            }
                        }));
                LinearLayout ops = new LinearLayout(c);
                ops.setGravity(Gravity.END);
                ImageButton up = Ui.iconButton(c, R.drawable.ic_up, "up", 36);
                up.setOnClickListener(new OnClickListener() {
                    @Override public void onClick(View v) {
                        if (idx + 1 < NativeEngine.getLayerCount()) { NativeEngine.moveLayer(idx, idx + 1); changed(); }
                    }
                });
                ImageButton down = Ui.iconButton(c, R.drawable.ic_down, "down", 36);
                down.setOnClickListener(new OnClickListener() {
                    @Override public void onClick(View v) {
                        if (idx > 0) { NativeEngine.moveLayer(idx, idx - 1); changed(); }
                    }
                });
                ImageButton del = Ui.iconButton(c, R.drawable.ic_delete, c.getString(R.string.delete), 36);
                del.setOnClickListener(new OnClickListener() {
                    @Override public void onClick(View v) { NativeEngine.deleteLayer(idx); changed(); }
                });
                ops.addView(up);
                ops.addView(down);
                ops.addView(del);
                row.addView(ops);
                // peg hierarchy: parent selector + effects
                LinearLayout rig = new LinearLayout(c);
                rig.setGravity(Gravity.CENTER_VERTICAL);
                rig.addView(Ui.label(c, c.getString(R.string.parent), 13));
                final android.widget.Spinner parentSp = new android.widget.Spinner(c);
                final int n2 = NativeEngine.getLayerCount();
                final java.util.List<Integer> choices = new java.util.ArrayList<>();
                java.util.List<String> labels = new java.util.ArrayList<>();
                choices.add(-1);
                labels.add(c.getString(R.string.none));
                for (int k = 0; k < n2; k++)
                    if (k != idx) { choices.add(k); labels.add(NativeEngine.getLayerName(k)); }
                parentSp.setAdapter(new android.widget.ArrayAdapter<>(c,
                        android.R.layout.simple_spinner_dropdown_item, labels));
                parentSp.setSelection(Math.max(0, choices.indexOf(NativeEngine.getParent(idx))));
                parentSp.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
                    @Override public void onItemSelected(android.widget.AdapterView<?> a, View v, int pos, long id) {
                        int want = choices.get(pos);
                        if (want != NativeEngine.getParent(idx)) {
                            if (!NativeEngine.setParent(idx, want))
                                android.widget.Toast.makeText(c, "\u21BB", android.widget.Toast.LENGTH_SHORT).show();
                            changed();
                        }
                    }
                    @Override public void onNothingSelected(android.widget.AdapterView<?> a) {}
                });
                rig.addView(parentSp, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
                android.widget.Button fxb = new android.widget.Button(c);
                fxb.setText(R.string.effects);
                fxb.setContentDescription(c.getString(R.string.effects));
                fxb.setOnClickListener(new OnClickListener() {
                    @Override public void onClick(View v) {
                        FxDialog.show(c, idx, new FxDialog.Done() {
                            @Override public void onApplied() { changed(); }
                        });
                    }
                });
                rig.addView(fxb);
                row.addView(rig);
            }
            mList.addView(row, lp);
        }
    }

    private void rename(final int idx) {
        final Context c = getContext();
        final EditText et = new EditText(c);
        et.setText(NativeEngine.getLayerName(idx));
        new AlertDialog.Builder(c).setTitle(R.string.layer_name).setView(et)
                .setPositiveButton(R.string.ok, new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        NativeEngine.renameLayer(idx, et.getText().toString());
                        changed();
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }
}
