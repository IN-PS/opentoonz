package org.opentoonz.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.DateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** The animation editor. */
public class EditorActivity extends Activity implements CanvasView.Listener,
        TimelineView.Listener, LayerPanel.Listener {
    static final String EXTRA_DIR = "dir", EXTRA_NEW = "new";
    private static final int REQ_IMPORT = 1, REQ_IMPORT_SEQ = 2, REQ_SAVE_GIF = 3, REQ_SAVE_MP4 = 4,
            REQ_IMPORT_AUDIO = 5;
    private static final long AUTOSAVE_MS = 30_000;

    private File mDir;
    private CanvasView mCanvas;
    private TimelineView mTimeline;
    private LayerPanel mLayers;
    private LinearLayout mSwatches, mToolOptions, mSelBar;
    private ImageButton[] mToolButtons = new ImageButton[7];
    private ImageButton mOnionBtn, mPlayBtn, mLayersBtn;
    private TextView mStatus, mFrameLabel;
    private final ExecutorService mIo = Executors.newSingleThreadExecutor();
    private final Handler mUi = new Handler(Looper.getMainLooper());
    private boolean mLoaded, mPlaying, mSaving;
    private int mPlayRow;
    private int mTool = NativeEngine.TOOL_BRUSH;
    // tool settings
    private int mBrushMax = 8, mBrushMin = 3, mEraserSize = 24, mEraseMode = NativeEngine.ERASE_ALL,
            mFillMode = NativeEngine.FILL_AREAS, mFillDepth = 10;
    private boolean mPressure = true, mAntialias = true, mFillEmpty;
    // v0.2 tool settings
    private int mHardness = 100, mOpacity = 100, mStabilizer = 2, mEraserHardness = 100;
    private int mShapeType = NativeEngine.SHAPE_LINE, mEase = NativeEngine.EASE_IN_OUT;
    private boolean mLasso, mCameraTarget, mPivotMode;
    private int mGrain, mScatter;
    private java.util.List<ProjectAudio> mAudios = new java.util.ArrayList<>();
    private File mPendingExport;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        App.ensureEngine(getApplication());
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        mDir = new File(getIntent().getStringExtra(EXTRA_DIR));
        buildUi();
        final ProgressDialog pd = ProgressDialog.show(this, null, mDir.getName(), true, false);
        mIo.execute(new Runnable() {
            @Override public void run() {
                final String err = NativeEngine.load(mDir.getAbsolutePath());
                mUi.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        if (err != null) {
                            new AlertDialog.Builder(EditorActivity.this)
                                    .setMessage(getString(R.string.load_failed, err))
                                    .setOnDismissListener(new android.content.DialogInterface.OnDismissListener() {
                                        @Override public void onDismiss(android.content.DialogInterface d) { finish(); }
                                    }).show();
                            return;
                        }
                        mLoaded = true;
                        int[] onion = NativeEngine.getOnion();
                        mCanvas.setOnion(onion[0] != 0);
                        Ui.setActive(mOnionBtn, onion[0] != 0);
                        mCanvas.documentLoaded();
                        loadAudio();
                        applyToolSettings();
                        selectTool(NativeEngine.TOOL_BRUSH);
                        refreshAll();
                        mUi.postDelayed(mAutosave, AUTOSAVE_MS);
                    }
                });
            }
        });
    }

    // ------------------------------------------------------------------ UI
    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Ui.BG);

        // top bar ----------------------------------------------------------
        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setBackgroundColor(Ui.PANEL);
        ImageButton back = Ui.iconButton(this, R.drawable.ic_back, "back", 44);
        back.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { onBackPressed(); }
        });
        top.addView(back);
        TextView title = Ui.label(this, mDir.getName(), 15);
        title.setSingleLine(true);
        title.setMaxWidth(Ui.dp(this, 140));
        top.addView(title);
        mStatus = Ui.label(this, "", 11);
        mStatus.setTextColor(Ui.DIM);
        mStatus.setPadding(Ui.dp(this, 8), 0, Ui.dp(this, 8), 0);
        top.addView(mStatus);
        ImageButton undo = Ui.iconButton(this, R.drawable.ic_undo, getString(R.string.undo), 44);
        undo.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doUndo(); }
        });
        ImageButton redo = Ui.iconButton(this, R.drawable.ic_redo, getString(R.string.redo), 44);
        redo.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doRedo(); }
        });
        top.addView(undo);
        top.addView(redo);
        HorizontalScrollView sw = new HorizontalScrollView(this);
        sw.setHorizontalScrollBarEnabled(false);
        mSwatches = new LinearLayout(this);
        mSwatches.setGravity(Gravity.CENTER_VERTICAL);
        sw.addView(mSwatches);
        top.addView(sw, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        ImageButton addColor = Ui.iconButton(this, R.drawable.ic_add, getString(R.string.new_color), 44);
        addColor.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                ColorPickerDialog.show(EditorActivity.this, getString(R.string.new_color), 0xFF3366CC,
                        new ColorPickerDialog.Callback() {
                            @Override public void onColor(int argb) {
                                NativeEngine.addStyle(argb);
                                refreshSwatches();
                            }
                        });
            }
        });
        top.addView(addColor);
        mLayersBtn = Ui.iconButton(this, R.drawable.ic_layers, getString(R.string.layers), 44);
        mLayersBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { toggleLayers(); }
        });
        top.addView(mLayersBtn);
        final ImageButton menu = Ui.iconButton(this, R.drawable.ic_menu, "menu", 44);
        menu.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { showMenu(menu); }
        });
        top.addView(menu);
        root.addView(top);

        // tool options -----------------------------------------------------
        HorizontalScrollView optScroll = new HorizontalScrollView(this);
        optScroll.setBackgroundColor(0xFF22252F);
        mToolOptions = new LinearLayout(this);
        mToolOptions.setGravity(Gravity.CENTER_VERTICAL);
        mToolOptions.setPadding(Ui.dp(this, 8), 0, Ui.dp(this, 8), 0);
        mToolOptions.setMinimumHeight(Ui.dp(this, 40));
        optScroll.addView(mToolOptions);
        root.addView(optScroll);

        // middle: toolbar | canvas, layer drawer, selection bar -------------
        FrameLayout middle = new FrameLayout(this);
        LinearLayout work = new LinearLayout(this);
        ScrollView toolScroll = new ScrollView(this);
        toolScroll.setBackgroundColor(Ui.PANEL);
        LinearLayout tools = new LinearLayout(this);
        tools.setOrientation(LinearLayout.VERTICAL);
        tools.setPadding(Ui.dp(this, 2), Ui.dp(this, 4), Ui.dp(this, 2), Ui.dp(this, 4));
        int[] icons = {R.drawable.ic_brush, R.drawable.ic_eraser, R.drawable.ic_fill,
                R.drawable.ic_select, R.drawable.ic_picker, R.drawable.ic_shape, R.drawable.ic_transform};
        int[] names = {R.string.brush, R.string.eraser, R.string.fill, R.string.select, R.string.picker,
                R.string.shape, R.string.transform};
        for (int i = 0; i < icons.length; i++) {
            final int tool = i;
            mToolButtons[i] = Ui.iconButton(this, icons[i], getString(names[i]), 50);
            mToolButtons[i].setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { selectTool(tool); }
            });
            tools.addView(mToolButtons[i]);
        }
        tools.addView(Ui.spacer(this, 10));
        mOnionBtn = Ui.iconButton(this, R.drawable.ic_onion, getString(R.string.onion), 50);
        mOnionBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { toggleOnion(); }
        });
        mOnionBtn.setOnLongClickListener(new View.OnLongClickListener() {
            @Override public boolean onLongClick(View v) { onionSettings(); return true; }
        });
        tools.addView(mOnionBtn);
        toolScroll.addView(tools);
        work.addView(toolScroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        mCanvas = new CanvasView(this);
        mCanvas.setListener(this);
        work.addView(mCanvas, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        middle.addView(work);

        mLayers = new LayerPanel(this);
        mLayers.setListener(this);
        mLayers.setVisibility(View.GONE);
        middle.addView(mLayers, new FrameLayout.LayoutParams(Ui.dp(this, 260),
                ViewGroup.LayoutParams.MATCH_PARENT, Gravity.END));

        mSelBar = new LinearLayout(this);
        mSelBar.setBackground(Ui.roundRect(0xF0262A36, Ui.dp(this, 22)));
        mSelBar.setPadding(Ui.dp(this, 6), 0, Ui.dp(this, 6), 0);
        ImageButton apply = Ui.iconButton(this, R.drawable.ic_check, getString(R.string.apply), 44);
        apply.setColorFilter(0xFF7CD992);
        apply.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.commitSelection(); mCanvas.selectionDone(); }
        });
        ImageButton cancel = Ui.iconButton(this, R.drawable.ic_close, getString(R.string.cancel), 44);
        cancel.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.cancelSelection(); mCanvas.selectionDone(); }
        });
        ImageButton del = Ui.iconButton(this, R.drawable.ic_delete, getString(R.string.delete), 44);
        del.setColorFilter(0xFFFF8080);
        del.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { NativeEngine.deleteSelection(); mCanvas.selectionDone(); }
        });
        mSelBar.addView(apply);
        mSelBar.addView(cancel);
        mSelBar.addView(del);
        mSelBar.setVisibility(View.GONE);
        FrameLayout.LayoutParams sbl = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        sbl.bottomMargin = Ui.dp(this, 12);
        middle.addView(mSelBar, sbl);
        root.addView(middle, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        // frame bar -------------------------------------------------------
        HorizontalScrollView fbScroll = new HorizontalScrollView(this);
        fbScroll.setBackgroundColor(0xFF22252F);
        LinearLayout fb = new LinearLayout(this);
        fb.setGravity(Gravity.CENTER_VERTICAL);
        ImageButton prev = Ui.iconButton(this, R.drawable.ic_prev, "previous", 44);
        prev.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { gotoRow(NativeEngine.getCurrentRow() - 1); }
        });
        mPlayBtn = Ui.iconButton(this, R.drawable.ic_play, getString(R.string.play), 44);
        mPlayBtn.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { togglePlay(); }
        });
        ImageButton next = Ui.iconButton(this, R.drawable.ic_next, "next", 44);
        next.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { gotoRow(NativeEngine.getCurrentRow() + 1); }
        });
        fb.addView(prev);
        fb.addView(mPlayBtn);
        fb.addView(next);
        mFrameLabel = Ui.label(this, "1 / 1", 13);
        mFrameLabel.setMinWidth(Ui.dp(this, 64));
        mFrameLabel.setGravity(Gravity.CENTER);
        fb.addView(mFrameLabel);
        addFrameButton(fb, R.drawable.ic_add, R.string.frame_add, new Runnable() {
            @Override public void run() { NativeEngine.insertBlankFrame(); }
        });
        addFrameButton(fb, R.drawable.ic_copy, R.string.frame_dup, new Runnable() {
            @Override public void run() { NativeEngine.duplicateFrame(); }
        });
        addFrameButton(fb, R.drawable.ic_hold, R.string.frame_hold, new Runnable() {
            @Override public void run() { NativeEngine.holdFrame(); }
        });
        addFrameButton(fb, R.drawable.ic_close, R.string.frame_clear, new Runnable() {
            @Override public void run() { NativeEngine.clearCell(); }
        });
        addFrameButton(fb, R.drawable.ic_delete, R.string.frame_del, new Runnable() {
            @Override public void run() { NativeEngine.deleteFrame(); }
        });
        ImageButton timing = Ui.iconButton(this, R.drawable.ic_timing, getString(R.string.timing), 44);
        timing.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { timingDialog(); }
        });
        fb.addView(timing);
        fbScroll.addView(fb);
        root.addView(fbScroll);

        mTimeline = new TimelineView(this);
        mTimeline.setListener(this);
        root.addView(mTimeline, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(root);
    }

    private void addFrameButton(LinearLayout bar, int icon, int name, final Runnable op) {
        ImageButton b = Ui.iconButton(this, icon, getString(name), 44);
        b.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (!mLoaded || mPlaying) return;
                op.run();
                refreshAll();
            }
        });
        bar.addView(b);
    }

    // ------------------------------------------------------------------ tools
    private void selectTool(int tool) {
        if (!mLoaded) return;
        mTool = tool;
        for (int i = 0; i < mToolButtons.length; i++) Ui.setActive(mToolButtons[i], i == tool);
        mCanvas.setTool(tool);
        buildToolOptions();
        applyToolSettings();
    }

    private void applyToolSettings() {
        NativeEngine.setBrush(mBrushMin, mBrushMax, mPressure, mAntialias);
        NativeEngine.setEraser(mEraserSize, mEraseMode);
        NativeEngine.setFill(mFillMode, mFillDepth, mFillEmpty);
        mCanvas.setCursorSize(mTool == NativeEngine.TOOL_ERASER ? mEraserSize : mBrushMax);
        NativeEngine.setBrushPro(mHardness, mOpacity, mStabilizer, mEraserHardness);
        mCanvas.setShapeType(mShapeType);
        mCanvas.setLasso(mLasso);
        mCanvas.setCameraTarget(mCameraTarget);
        mCanvas.setEase(mEase);
        mCanvas.setPivotMode(mPivotMode && mTool == NativeEngine.TOOL_TRANSFORM);
        NativeEngine.setBrushTexture(mGrain, mScatter);
    }

    private void buildToolOptions() {
        mToolOptions.removeAllViews();
        switch (mTool) {
            case NativeEngine.TOOL_BRUSH:
                addSlider(getString(R.string.size), 1, 100, mBrushMax, new Ui.IntListener() {
                    @Override public void onValue(int v) { mBrushMax = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.min_size), 1, 100, mBrushMin, new Ui.IntListener() {
                    @Override public void onValue(int v) { mBrushMin = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.hardness), 0, 100, mHardness, new Ui.IntListener() {
                    @Override public void onValue(int v) { mHardness = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.opacity), 1, 100, mOpacity, new Ui.IntListener() {
                    @Override public void onValue(int v) { mOpacity = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.stabilizer), 0, 10, mStabilizer, new Ui.IntListener() {
                    @Override public void onValue(int v) { mStabilizer = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.grain), 0, 100, mGrain, new Ui.IntListener() {
                    @Override public void onValue(int v) { mGrain = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.scatter), 0, 100, mScatter, new Ui.IntListener() {
                    @Override public void onValue(int v) { mScatter = v; applyToolSettings(); }
                });
                addCheck(getString(R.string.pressure), mPressure, new CompoundButton.OnCheckedChangeListener() {
                    @Override public void onCheckedChanged(CompoundButton b, boolean c) { mPressure = c; applyToolSettings(); }
                });
                addCheck(getString(R.string.antialias), mAntialias, new CompoundButton.OnCheckedChangeListener() {
                    @Override public void onCheckedChanged(CompoundButton b, boolean c) { mAntialias = c; applyToolSettings(); }
                });
                break;
            case NativeEngine.TOOL_ERASER:
                addSlider(getString(R.string.size), 1, 200, mEraserSize, new Ui.IntListener() {
                    @Override public void onValue(int v) { mEraserSize = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.hardness), 0, 100, mEraserHardness, new Ui.IntListener() {
                    @Override public void onValue(int v) { mEraserHardness = v; applyToolSettings(); }
                });
                addSpinner(new String[]{getString(R.string.lines_areas), getString(R.string.lines),
                        getString(R.string.areas)}, mEraseMode, new Ui.IntListener() {
                    @Override public void onValue(int v) { mEraseMode = v; applyToolSettings(); }
                });
                break;
            case NativeEngine.TOOL_FILL:
                addSpinner(new String[]{getString(R.string.areas), getString(R.string.lines),
                        getString(R.string.lines_areas)}, mFillMode, new Ui.IntListener() {
                    @Override public void onValue(int v) { mFillMode = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.gap), 0, 15, mFillDepth, new Ui.IntListener() {
                    @Override public void onValue(int v) { mFillDepth = v; applyToolSettings(); }
                });
                addCheck(getString(R.string.empty_only), mFillEmpty, new CompoundButton.OnCheckedChangeListener() {
                    @Override public void onCheckedChanged(CompoundButton b, boolean c) { mFillEmpty = c; applyToolSettings(); }
                });
                break;
            case NativeEngine.TOOL_SELECT:
                addSpinner(new String[]{getString(R.string.rect_select), getString(R.string.lasso)},
                        mLasso ? 1 : 0, new Ui.IntListener() {
                    @Override public void onValue(int v) { mLasso = v == 1; applyToolSettings(); }
                });
                break;
            case NativeEngine.TOOL_SHAPE:
                addSpinner(new String[]{getString(R.string.line), getString(R.string.rectangle),
                        getString(R.string.ellipse)}, mShapeType, new Ui.IntListener() {
                    @Override public void onValue(int v) { mShapeType = v; applyToolSettings(); }
                });
                addSlider(getString(R.string.size), 1, 100, mBrushMax, new Ui.IntListener() {
                    @Override public void onValue(int v) { mBrushMax = v; applyToolSettings(); }
                });
                break;
            case NativeEngine.TOOL_TRANSFORM:
                addSpinner(new String[]{getString(R.string.layer), getString(R.string.camera)},
                        mCameraTarget ? 1 : 0, new Ui.IntListener() {
                    @Override public void onValue(int v) { mCameraTarget = v == 1; applyToolSettings(); }
                });
                addSpinner(new String[]{getString(R.string.linear), getString(R.string.ease_in_out),
                        getString(R.string.constant)}, mEase, new Ui.IntListener() {
                    @Override public void onValue(int v) {
                        mEase = v;
                        applyToolSettings();
                        int target = mCameraTarget ? NativeEngine.CAMERA : NativeEngine.getCurrentLayer();
                        int row = NativeEngine.getCurrentRow();
                        if (NativeEngine.hasKey(target, row)
                                && NativeEngine.getKeyEase(target, row) != v)
                            NativeEngine.setKey(target, row, NativeEngine.getXform(target, row), v);
                    }
                });
                android.widget.Button rm = new android.widget.Button(this);
                rm.setText(R.string.remove_key);
                rm.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View v) {
                        int target = mCameraTarget ? NativeEngine.CAMERA : NativeEngine.getCurrentLayer();
                        NativeEngine.removeKey(target, NativeEngine.getCurrentRow());
                        refreshAll();
                    }
                });
                mToolOptions.addView(rm);
                addCheck(getString(R.string.set_pivot), mPivotMode, new CompoundButton.OnCheckedChangeListener() {
                    @Override public void onCheckedChanged(CompoundButton b, boolean c) { mPivotMode = c; applyToolSettings(); }
                });
                TextView hint = Ui.label(this, getString(R.string.key_hint), 12);
                hint.setTextColor(Ui.DIM);
                mToolOptions.addView(hint);
                break;
            default:
                mToolOptions.addView(Ui.label(this, getString(R.string.picker), 13));
        }
    }

    private void addSlider(String label, int min, int max, int value, Ui.IntListener l) {
        LinearLayout s = Ui.slider(this, label, min, max, value, l);
        mToolOptions.addView(s, new LinearLayout.LayoutParams(Ui.dp(this, 300), ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    private void addCheck(String label, boolean value, CompoundButton.OnCheckedChangeListener l) {
        CheckBox cb = new CheckBox(this);
        cb.setText(label);
        cb.setTextColor(Ui.TEXT);
        cb.setChecked(value);
        cb.setOnCheckedChangeListener(l);
        mToolOptions.addView(cb);
    }

    private void addSpinner(String[] items, int sel, final Ui.IntListener l) {
        Spinner sp = new Spinner(this);
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, items);
        sp.setAdapter(a);
        sp.setSelection(sel);
        sp.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> p, View v, int pos, long id) { l.onValue(pos); }
            @Override public void onNothingSelected(AdapterView<?> p) {}
        });
        mToolOptions.addView(sp);
    }

    // ------------------------------------------------------------------ refresh
    private void refreshAll() {
        if (!mLoaded) return;
        int row = NativeEngine.getCurrentRow();
        mCanvas.showRow(row);
        mTimeline.refresh();
        if (mLayers.getVisibility() == View.VISIBLE) mLayers.refresh();
        refreshSwatches();
        updateFrameLabel(row);
    }

    private void updateFrameLabel(int row) {
        mFrameLabel.setText((row + 1) + " / " + NativeEngine.getFrameCount());
    }

    private void refreshSwatches() {
        mSwatches.removeAllViews();
        int[] ids = NativeEngine.getStyleIds();
        int[] colors = NativeEngine.getStyleColors();
        int cur = NativeEngine.getCurrentStyle();
        int s = Ui.dp(this, 30), m = Ui.dp(this, 3);
        for (int i = 0; i < ids.length; i++) {
            final int id = ids[i];
            final int color = colors[i];
            View v = new View(this);
            android.graphics.drawable.GradientDrawable g = new android.graphics.drawable.GradientDrawable();
            g.setColor(color | 0xFF000000);
            g.setCornerRadius(Ui.dp(this, 6));
            g.setStroke(Ui.dp(this, id == cur ? 3 : 1), id == cur ? Ui.ACCENT : 0xFF555A68);
            v.setBackground(g);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(s, s);
            lp.setMargins(m, m, m, m);
            v.setContentDescription("style " + id);
            v.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View x) {
                    NativeEngine.setCurrentStyle(id);
                    refreshSwatches();
                }
            });
            v.setOnLongClickListener(new View.OnLongClickListener() {
                @Override public boolean onLongClick(View x) {
                    ColorPickerDialog.show(EditorActivity.this, getString(R.string.edit_color), color,
                            new ColorPickerDialog.Callback() {
                                @Override public void onColor(int argb) {
                                    // Toonz Raster: every drawing using the style is recoloured
                                    NativeEngine.setStyleColor(id, argb);
                                    refreshAll();
                                }
                            });
                    return true;
                }
            });
            mSwatches.addView(v, lp);
        }
    }

    private void gotoRow(int row) {
        if (!mLoaded || mPlaying) return;
        row = Math.max(0, row);
        NativeEngine.setCurrentRow(row);
        mCanvas.showRow(row);
        mTimeline.refresh();
        updateFrameLabel(row);
        for (ProjectAudio a : mAudios) a.scrub(row, NativeEngine.getFps(), mUi);  // lip-sync scrub
    }

    // ------------------------------------------------------------------ listeners
    @Override public void onDocumentChanged() {
        mTimeline.refresh();
        updateFrameLabel(NativeEngine.getCurrentRow());
    }

    @Override public void onStylePicked(int styleId) {
        NativeEngine.setCurrentStyle(styleId);
        refreshSwatches();
    }

    @Override public void onSelectionChanged(boolean active) {
        mSelBar.setVisibility(active ? View.VISIBLE : View.GONE);
    }

    @Override public void onStylusDetected() {
        Toast.makeText(this, R.string.stylus_mode, Toast.LENGTH_SHORT).show();
    }

    @Override public void onCellMoved(int layer, int from, int to) {
        if (mPlaying) return;
        NativeEngine.setCurrentLayer(layer);
        NativeEngine.moveCell(from, to);
        refreshAll();
    }

    @Override public void onCellSelected(int layer, int row) {
        if (mPlaying) return;
        NativeEngine.setCurrentLayer(layer);
        gotoRow(row);
        if (mLayers.getVisibility() == View.VISIBLE) mLayers.refresh();
    }

    @Override public void onLayersChanged() {
        mCanvas.renderAll();
        mTimeline.refresh();
    }

    private void toggleLayers() {
        boolean show = mLayers.getVisibility() != View.VISIBLE;
        mLayers.setVisibility(show ? View.VISIBLE : View.GONE);
        Ui.setActive(mLayersBtn, show);
        if (show) mLayers.refresh();
    }

    private void toggleOnion() {
        int[] o = NativeEngine.getOnion();
        boolean en = o[0] == 0;
        NativeEngine.setOnion(en, o[1], o[2], o[3]);
        Ui.setActive(mOnionBtn, en);
        mCanvas.setOnion(en);
    }

    private void onionSettings() {
        final int[] o = NativeEngine.getOnion();
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 16);
        l.setPadding(p, p, p, 0);
        l.addView(Ui.slider(this, getString(R.string.frames_back), 0, 5, o[1], new Ui.IntListener() {
            @Override public void onValue(int v) { o[1] = v; }
        }));
        l.addView(Ui.slider(this, getString(R.string.frames_forward), 0, 5, o[2], new Ui.IntListener() {
            @Override public void onValue(int v) { o[2] = v; }
        }));
        l.addView(Ui.slider(this, getString(R.string.opacity), 10, 255, o[3], new Ui.IntListener() {
            @Override public void onValue(int v) { o[3] = v; }
        }));
        new AlertDialog.Builder(this).setTitle(R.string.onion_settings).setView(l)
                .setPositiveButton(R.string.ok, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int w) {
                        NativeEngine.setOnion(true, o[1], o[2], o[3]);
                        Ui.setActive(mOnionBtn, true);
                        mCanvas.setOnion(true);
                    }
                }).show();
    }

    // ------------------------------------------------------------------ playback
    private final Runnable mPlayTick = new Runnable() {
        @Override public void run() {
            if (!mPlaying) return;
            int frames = NativeEngine.getFrameCount();
            mPlayRow = (mPlayRow + 1) % Math.max(1, frames);
            if (mPlayRow == 0)  // loop: restart the sound
                for (ProjectAudio a : mAudios) { a.pause(); a.play(0, NativeEngine.getFps()); }
            mCanvas.showRow(mPlayRow);
            mTimeline.setCurrentRow(mPlayRow);
            updateFrameLabel(mPlayRow);
            mUi.postDelayed(this, 1000 / Math.max(1, NativeEngine.getFps()));
        }
    };

    private void togglePlay() {
        if (!mLoaded) return;
        mPlaying = !mPlaying;
        mCanvas.setPlaying(mPlaying);
        mPlayBtn.setImageResource(mPlaying ? R.drawable.ic_pause : R.drawable.ic_play);
        if (mPlaying) {
            if (NativeEngine.hasSelection()) { NativeEngine.commitSelection(); mCanvas.selectionDone(); }
            mPlayRow = NativeEngine.getCurrentRow();
            for (ProjectAudio a : mAudios) a.play(mPlayRow, NativeEngine.getFps());
            mUi.post(mPlayTick);
        } else {
            mUi.removeCallbacks(mPlayTick);
            for (ProjectAudio a : mAudios) a.pause();
            NativeEngine.setCurrentRow(mPlayRow);
            refreshAll();
        }
    }

    // ------------------------------------------------------------------ undo
    private void doUndo() {
        if (!mLoaded || mPlaying) return;
        boolean hadSel = NativeEngine.hasSelection();
        NativeEngine.undo();
        if (hadSel) mCanvas.selectionDone();
        refreshAll();
    }

    private void doRedo() {
        if (!mLoaded || mPlaying) return;
        NativeEngine.redo();
        refreshAll();
    }

    // ------------------------------------------------------------------ menu
    private void showMenu(View anchor) {
        final int[] ids = {R.string.save, R.string.import_images, R.string.import_sequence,
                R.string.export_gif, R.string.export_mp4, R.string.export_png,
                R.string.onion_settings, R.string.scene_settings, R.string.import_audio,
                R.string.audio_settings, R.string.memory_info};
        String[] items = new String[ids.length];
        for (int i = 0; i < ids.length; i++) items[i] = getString(ids[i]);
        new AlertDialog.Builder(this).setItems(items, new android.content.DialogInterface.OnClickListener() {
            @Override public void onClick(android.content.DialogInterface d, int which) {
                switch (ids[which]) {
                    case R.string.save: save(false); break;
                    case R.string.import_images: pickImages(REQ_IMPORT); break;
                    case R.string.import_sequence: pickImages(REQ_IMPORT_SEQ); break;
                    case R.string.export_gif: exportGif(); break;
                    case R.string.export_mp4: exportMp4(); break;
                    case R.string.export_png: exportPng(); break;
                    case R.string.onion_settings: onionSettings(); break;
                    case R.string.scene_settings: sceneSettings(); break;
                    case R.string.memory_info: about(); break;
                    case R.string.import_audio: pickAudio(); break;
                    case R.string.audio_settings: audioSettings(); break;
                }
            }
        }).show();
    }

    private void sceneSettings() {
        final int[] fps = {NativeEngine.getFps()};
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 16);
        l.setPadding(p, p, p, 0);
        l.addView(Ui.slider(this, getString(R.string.fps), 1, 60, fps[0], new Ui.IntListener() {
            @Override public void onValue(int v) { fps[0] = v; }
        }));
        l.addView(Ui.label(this, getString(R.string.canvas_size) + ": " + NativeEngine.getWidth()
                + "×" + NativeEngine.getHeight(), 14));
        new AlertDialog.Builder(this).setTitle(R.string.scene_settings).setView(l)
                .setPositiveButton(R.string.ok, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int w) {
                        NativeEngine.setFps(fps[0]);
                    }
                }).show();
    }

    private void about() {
        new AlertDialog.Builder(this).setTitle(R.string.app_name)
                .setMessage(getString(R.string.about_text, NativeEngine.getMemoryKB() / 1024,
                        NativeEngine.getWidth(), NativeEngine.getHeight(), NativeEngine.getFps()))
                .setPositiveButton(R.string.ok, null).show();
    }

    // ------------------------------------------------------------------ save
    private final Runnable mAutosave = new Runnable() {
        @Override public void run() {
            if (mLoaded && !mPlaying && NativeEngine.isDirty()) save(true);
            mUi.postDelayed(this, AUTOSAVE_MS);
        }
    };

    private void save(final boolean auto) {
        if (!mLoaded || mSaving) return;
        mSaving = true;
        mStatus.setText("…");
        mIo.execute(new Runnable() {
            @Override public void run() {
                final String err = NativeEngine.save(mDir.getAbsolutePath());
                if (err == null) ProjectStore.writeThumbnail(mDir);
                mUi.post(new Runnable() {
                    @Override public void run() {
                        mSaving = false;
                        if (err != null) {
                            mStatus.setText("!");
                            Toast.makeText(EditorActivity.this, getString(R.string.save_failed, err),
                                    Toast.LENGTH_LONG).show();
                        } else {
                            mStatus.setText(getString(auto ? R.string.autosaved : R.string.saved) + " "
                                    + DateFormat.getTimeInstance(DateFormat.SHORT).format(new Date()));
                        }
                    }
                });
            }
        });
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mPlaying) togglePlay();
        if (mLoaded && NativeEngine.isDirty()) save(true);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        mUi.removeCallbacks(mAutosave);
        for (ProjectAudio a : mAudios) a.release();
        mIo.shutdown();
    }

    @Override
    public void onBackPressed() {
        if (mLoaded && NativeEngine.isDirty()) save(false);
        super.onBackPressed();
    }

    // ------------------------------------------------------------------ import
    private void pickImages(int req) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("image/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"image/png", "image/jpeg"});
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, req);
    }

    private String displayName(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (RuntimeException ignored) {
        }
        return u.getLastPathSegment();
    }

    private Bitmap decode(Uri u) throws java.io.IOException {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        try (InputStream in = getContentResolver().openInputStream(u)) { BitmapFactory.decodeStream(in, null, o); }
        // keep memory bounded on low-end phones: never decode more than ~2x the canvas
        int maxSide = 2 * Math.max(NativeEngine.getWidth(), NativeEngine.getHeight());
        int sample = 1;
        while (Math.max(o.outWidth, o.outHeight) / sample > maxSide) sample *= 2;
        BitmapFactory.Options d = new BitmapFactory.Options();
        d.inSampleSize = sample;
        d.inPreferredConfig = Bitmap.Config.ARGB_8888;
        try (InputStream in = getContentResolver().openInputStream(u)) { return BitmapFactory.decodeStream(in, null, d); }
    }

    private void importUris(final List<Uri> uris, final boolean sequence) {
        final ProgressDialog pd = ProgressDialog.show(this, null, getString(R.string.import_images), true, false);
        mIo.execute(new Runnable() {
            @Override public void run() {
                int ok = 0;
                for (int i = 0; i < uris.size(); i++) {
                    try {
                        Bitmap b = decode(uris.get(i));
                        if (b == null) continue;
                        String name = displayName(uris.get(i));
                        if (name != null && name.lastIndexOf('.') > 0) name = name.substring(0, name.lastIndexOf('.'));
                        // sequence: one new layer, consecutive frames; else one layer per image
                        boolean newLayer = !sequence || i == 0;
                        if (NativeEngine.importBitmap(b, newLayer, sequence ? i : 0, name)) ok++;
                        b.recycle();
                    } catch (Exception e) {
                        // skip unreadable file
                    }
                }
                final int imported = ok;
                mUi.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        Toast.makeText(EditorActivity.this, imported + " / " + uris.size(), Toast.LENGTH_SHORT).show();
                        refreshAll();
                    }
                });
            }
        });
    }

    // ------------------------------------------------------------------ export
    private void exportGif() {
        runExport(new File(getCacheDir(), mDir.getName() + ".gif"), "image/gif", REQ_SAVE_GIF, new ExportJob() {
            @Override public String run(File out, ProgressDialog pd) {
                return NativeEngine.exportGif(out.getAbsolutePath(), 960);
            }
        });
    }

    private void exportMp4() {
        runExport(new File(getCacheDir(), mDir.getName() + ".mp4"), "video/mp4", REQ_SAVE_MP4, new ExportJob() {
            @Override public String run(File out, final ProgressDialog pd) {
                ProjectAudio mix = null;
                try {
                    mix = ProjectAudio.mix(mAudios, NativeEngine.getFrameCount(), NativeEngine.getFps(), getCacheDir());
                } catch (java.io.IOException | RuntimeException ignored) {
                }
                return Mp4Exporter.export(out, 1920, new Mp4Exporter.Progress() {
                    @Override public void onFrame(final int done, final int total) {
                        mUi.post(new Runnable() {
                            @Override public void run() { pd.setMessage(getString(R.string.exporting) + " " + done + "/" + total); }
                        });
                    }
                }, mix);
            }
        });
    }

    private void exportPng() {
        final File dir = new File(getExternalFilesDir("exports"), mDir.getName() + "_png");
        final ProgressDialog pd = ProgressDialog.show(this, null, getString(R.string.exporting), true, false);
        mIo.execute(new Runnable() {
            @Override public void run() {
                dir.mkdirs();
                final String err = NativeEngine.exportPngSequence(dir.getAbsolutePath());
                mUi.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        new AlertDialog.Builder(EditorActivity.this).setMessage(err == null
                                ? getString(R.string.export_done, dir.getAbsolutePath())
                                : getString(R.string.export_failed, err)).setPositiveButton(R.string.ok, null).show();
                    }
                });
            }
        });
    }

    private interface ExportJob { String run(File out, ProgressDialog pd); }

    private void runExport(final File out, final String mime, final int req, final ExportJob job) {
        if (NativeEngine.hasSelection()) { NativeEngine.commitSelection(); mCanvas.selectionDone(); }
        final ProgressDialog pd = ProgressDialog.show(this, null, getString(R.string.exporting), true, false);
        mIo.execute(new Runnable() {
            @Override public void run() {
                final String err = job.run(out, pd);
                mUi.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        if (err != null) {
                            new AlertDialog.Builder(EditorActivity.this)
                                    .setMessage(getString(R.string.export_failed, err))
                                    .setPositiveButton(R.string.ok, null).show();
                            return;
                        }
                        mPendingExport = out;
                        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                        i.addCategory(Intent.CATEGORY_OPENABLE);
                        i.setType(mime);
                        i.putExtra(Intent.EXTRA_TITLE, out.getName());
                        startActivityForResult(i, req);
                    }
                });
            }
        });
    }

    private void copyTo(File src, Uri dst) {
        try (InputStream in = new FileInputStream(src);
             OutputStream os = getContentResolver().openOutputStream(dst)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            Toast.makeText(this, getString(R.string.export_done, displayName(dst)), Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            Toast.makeText(this, getString(R.string.export_failed, e.toString()), Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null) return;
        if (req == REQ_IMPORT || req == REQ_IMPORT_SEQ) {
            final List<Uri> uris = new ArrayList<>();
            ClipData cd = data.getClipData();
            if (cd != null) for (int i = 0; i < cd.getItemCount(); i++) uris.add(cd.getItemAt(i).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            if (req == REQ_IMPORT_SEQ) {
                // order frames by file name (img_001.png, img_002.png ...)
                final List<String> names = new ArrayList<>();
                for (Uri u : uris) names.add(String.valueOf(displayName(u)));
                List<Integer> idx = new ArrayList<>();
                for (int i = 0; i < uris.size(); i++) idx.add(i);
                Collections.sort(idx, new Comparator<Integer>() {
                    @Override public int compare(Integer a, Integer b) { return names.get(a).compareTo(names.get(b)); }
                });
                List<Uri> sorted = new ArrayList<>();
                for (int i : idx) sorted.add(uris.get(i));
                importUris(sorted, true);
            } else importUris(uris, false);
        } else if (req == REQ_IMPORT_AUDIO) {
            importAudio(data.getData());
        } else if ((req == REQ_SAVE_GIF || req == REQ_SAVE_MP4) && mPendingExport != null) {
            copyTo(mPendingExport, data.getData());
        }
    }

    // ------------------------------------------------------------------ timing
    private void timingDialog() {
        if (!mLoaded || mPlaying) return;
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 16);
        l.setPadding(p, p, p, 0);
        l.addView(Ui.label(this, getString(R.string.reframe), 15));
        LinearLayout row = new LinearLayout(this);
        final AlertDialog[] dlg = new AlertDialog[1];
        for (int n = 1; n <= 3; n++) {
            final int step = n;
            android.widget.Button b = new android.widget.Button(this);
            b.setText(getString(R.string.reframe_n, n));
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    NativeEngine.reframe(step);
                    refreshAll();
                    if (dlg[0] != null) dlg[0].dismiss();
                }
            });
            row.addView(b);
        }
        l.addView(row);
        final int[] exp = {1};
        int r = NativeEngine.getCurrentRow(), layer = NativeEngine.getCurrentLayer();
        int d = NativeEngine.getCell(layer, r);
        if (d > 0) {
            int a = r, b = r;
            while (a > 0 && NativeEngine.getCell(layer, a - 1) == d) a--;
            while (NativeEngine.getCell(layer, b + 1) == d) b++;
            exp[0] = b - a + 1;
        }
        l.addView(Ui.label(this, getString(R.string.exposure), 15));
        l.addView(Ui.slider(this, getString(R.string.exposure_frames), 1, 24, exp[0], new Ui.IntListener() {
            @Override public void onValue(int v) { exp[0] = v; }
        }));
        dlg[0] = new AlertDialog.Builder(this).setTitle(R.string.timing).setView(l)
                .setPositiveButton(R.string.apply, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface di, int w) {
                        NativeEngine.setExposure(exp[0]);
                        refreshAll();
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }

    // ------------------------------------------------------------------ audio
    private void loadAudio() {
        for (ProjectAudio a : mAudios) a.release();
        mAudios = ProjectAudio.loadAll(mDir);
        java.util.List<float[]> peaks = new java.util.ArrayList<>();
        java.util.List<Integer> offsets = new java.util.ArrayList<>();
        for (ProjectAudio a : mAudios) {
            try {
                peaks.add(a.peaks(NativeEngine.getFps()));
                offsets.add(a.offsetFrames);
            } catch (java.io.IOException ignored) {
            }
        }
        mTimeline.setAudioTracks(peaks, offsets);
    }

    private void pickAudio() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("audio/*");
        startActivityForResult(i, REQ_IMPORT_AUDIO);
    }

    private void importAudio(final Uri uri) {
        if (uri == null) return;
        final ProgressDialog pd = ProgressDialog.show(this, null, getString(R.string.import_audio), true, false);
        mIo.execute(new Runnable() {
            @Override public void run() {
                final String err = ProjectAudio.importFrom(EditorActivity.this, uri, mDir);
                mUi.post(new Runnable() {
                    @Override public void run() {
                        pd.dismiss();
                        loadAudio();
                        Toast.makeText(EditorActivity.this, err == null
                                ? getString(R.string.audio_imported,
                                        mAudios.isEmpty() ? 0 : mAudios.get(mAudios.size() - 1).durationSec())
                                : getString(R.string.audio_failed, err), Toast.LENGTH_LONG).show();
                    }
                });
            }
        });
    }

    private void audioSettings() {
        if (mAudios.isEmpty()) { pickAudio(); return; }
        if (mAudios.size() == 1) { trackSettings(mAudios.get(0)); return; }
        String[] names = new String[mAudios.size()];
        for (int i = 0; i < names.length; i++) names[i] = "Audio " + (i + 1);
        new AlertDialog.Builder(this).setTitle(R.string.audio_settings)
                .setItems(names, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int which) {
                        trackSettings(mAudios.get(which));
                    }
                }).show();
    }

    private void trackSettings(final ProjectAudio track) {
        final int[] off = {track.offsetFrames};
        final int[] vol = {Math.round(track.volume * 100)};
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        int p = Ui.dp(this, 16);
        l.setPadding(p, p, p, 0);
        l.addView(Ui.slider(this, getString(R.string.audio_offset), 0, 600, off[0], new Ui.IntListener() {
            @Override public void onValue(int v) { off[0] = v; }
        }));
        l.addView(Ui.slider(this, getString(R.string.audio_volume), 0, 100, vol[0], new Ui.IntListener() {
            @Override public void onValue(int v) { vol[0] = v; }
        }));
        new AlertDialog.Builder(this).setTitle(R.string.audio_settings).setView(l)
                .setPositiveButton(R.string.ok, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int w) {
                        track.offsetFrames = off[0];
                        track.volume = vol[0] / 100f;
                        try { track.saveProps(); } catch (java.io.IOException ignored) {}
                        loadAudio();
                    }
                })
                .setNeutralButton(R.string.remove_audio, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface d, int w) {
                        track.removeFiles();
                        loadAudio();
                    }
                }).setNegativeButton(R.string.cancel, null).show();
    }

    // ------------------------------------------------------------------ keyboard
    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (!mLoaded) return super.onKeyDown(code, e);
        if (e.isCtrlPressed()) {
            if (code == KeyEvent.KEYCODE_Z) { if (e.isShiftPressed()) doRedo(); else doUndo(); return true; }
            if (code == KeyEvent.KEYCODE_Y) { doRedo(); return true; }
            if (code == KeyEvent.KEYCODE_S) { save(false); return true; }
        }
        switch (code) {
            case KeyEvent.KEYCODE_B: selectTool(NativeEngine.TOOL_BRUSH); return true;
            case KeyEvent.KEYCODE_E: selectTool(NativeEngine.TOOL_ERASER); return true;
            case KeyEvent.KEYCODE_F: selectTool(NativeEngine.TOOL_FILL); return true;
            case KeyEvent.KEYCODE_S: selectTool(NativeEngine.TOOL_SELECT); return true;
            case KeyEvent.KEYCODE_O: toggleOnion(); return true;
            case KeyEvent.KEYCODE_SPACE: togglePlay(); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: case KeyEvent.KEYCODE_COMMA:
                gotoRow(NativeEngine.getCurrentRow() - 1); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: case KeyEvent.KEYCODE_PERIOD:
                gotoRow(NativeEngine.getCurrentRow() + 1); return true;
        }
        return super.onKeyDown(code, e);
    }
}
