package org.opentoonz.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.OverScroller;

/**
 * Xsheet (exposure sheet) laid out like the OpenToonz desktop one: one column
 * per level column (column 1 on the left), frames from top to bottom, a frame
 * number column on the left, column headers with name / camstand toggle /
 * lock, level blocks with their drawing numbers and hold lines, keyframe
 * marks.  Colours are those of the desktop "Default" (dark) theme
 * (stuff/config/qss/Default: XsheetViewer properties).
 */
public class XsheetView extends View {
    public interface Listener {
        void onCellSelected(int layer, int row);
        /** visibility / lock changed from a column header */
        void onColumnChanged();
    }

    // desktop theme colours
    static final int BG = 0xFF484848, EMPTY = 0xFF424242, LINE = 0x73000000, VLINE = 0x99000000,
            TEXT = 0xD9FFFFFF, CUR_ROW = 0x994F7BC7, FOCUS = 0xFF00FFFF, HEAD_SEL = 0x4DFFFFFF,
            MARKER = 0x33FFFFFF, SEC_MARKER = 0x66FFFFFF,
            LEVEL = 0xFF457350, LEVEL_BORDER = 0xFF284332, LEVEL_SEL = 0xFF52A365,
            FULLCOLOR = 0xFF567790, FULLCOLOR_BORDER = 0xFF394F60, FULLCOLOR_SEL = 0xFF6D9DB6,
            VECTOR = 0xFF868650, VECTOR_BORDER = 0xFF565634, VECTOR_SEL = 0xFFB0B063;

    private Listener mListener;
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG), mFill = new Paint(),
            mLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int mColW, mRowH, mHeadH, mFrameW;
    private final GestureDetector mGestures;
    private final OverScroller mScroller;
    private float mScrollX, mScrollY;
    private int[][] mCells = new int[0][0];
    private boolean[][] mKeys = new boolean[0][0];
    private boolean[] mCamKeys = new boolean[0];
    private String[] mNames = new String[0];
    private boolean[] mVisible = new boolean[0], mLocked = new boolean[0];
    private int[] mTypes = new int[0];
    private int mRows = 1, mCurRow, mCurLayer, mFps = 12;

    public XsheetView(Context c) {
        super(c);
        mColW = Ui.dp(c, 66);
        mRowH = Ui.dp(c, 22);
        mHeadH = Ui.dp(c, 46);
        mFrameW = Ui.dp(c, 40);
        mText.setTextSize(Ui.dp(c, 11));
        mScroller = new OverScroller(c);
        mGestures = new GestureDetector(c, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) {
                mScroller.forceFinished(true);
                return true;
            }
            @Override public boolean onScroll(MotionEvent a, MotionEvent b, float dx, float dy) {
                scrollByPx(dx, dy);
                return true;
            }
            @Override public boolean onFling(MotionEvent a, MotionEvent b, float vx, float vy) {
                mScroller.fling((int) mScrollX, (int) mScrollY, (int) -vx, (int) -vy,
                        0, maxScrollX(), 0, maxScrollY());
                postInvalidateOnAnimation();
                return true;
            }
            @Override public boolean onSingleTapUp(MotionEvent e) {
                tap(e.getX(), e.getY());
                return true;
            }
        });
    }

    public void setListener(Listener l) { mListener = l; }

    int columnAt(float x) {
        if (x < mFrameW) return -1;
        int c = (int) ((x - mFrameW + mScrollX) / mColW);
        return c >= 0 && c < mNames.length ? c : -1;
    }

    int rowAt(float y) { return y < mHeadH ? -1 : (int) ((y - mHeadH + mScrollY) / mRowH); }

    /** Tap handling, also used by tests (view coordinates). */
    void tap(float x, float y) {
        int col = columnAt(x), row = rowAt(y);
        if (y < mHeadH) {
            if (col < 0) return;
            float lx = x - mFrameW + mScrollX - col * mColW;
            if (y > mHeadH * 0.5f && lx < mColW * 0.5f) {         // camstand (eye) toggle
                NativeEngine.setLayerVisible(col, !mVisible[col]);
                if (mListener != null) mListener.onColumnChanged();
            } else if (y > mHeadH * 0.5f) {                        // lock toggle
                NativeEngine.setLayerLocked(col, !mLocked[col]);
                if (mListener != null) mListener.onColumnChanged();
            } else if (mListener != null) mListener.onCellSelected(col, mCurRow);
            return;
        }
        if (row < 0) return;
        if (mListener != null) mListener.onCellSelected(col >= 0 ? col : mCurLayer, row);
    }

    /** Pulls the xsheet state from the engine. */
    public void refresh() {
        int layers = NativeEngine.getLayerCount();
        mRows = NativeEngine.getFrameCount();
        mFps = Math.max(1, NativeEngine.getFps());
        int rows = mRows + 24;
        mCells = new int[layers][rows];
        mKeys = new boolean[layers][rows];
        mNames = new String[layers];
        mVisible = new boolean[layers];
        mLocked = new boolean[layers];
        mTypes = new int[layers];
        for (int l = 0; l < layers; l++) {
            mNames[l] = NativeEngine.getLayerName(l);
            mVisible[l] = NativeEngine.isLayerVisible(l);
            mLocked[l] = NativeEngine.isLayerLocked(l);
            mTypes[l] = NativeEngine.getLayerType(l);
            for (int r = 0; r < mRows; r++) mCells[l][r] = NativeEngine.getCell(l, r);
            for (int r : NativeEngine.getKeyRows(l)) if (r >= 0 && r < rows) mKeys[l][r] = true;
        }
        mCamKeys = new boolean[rows];
        for (int r : NativeEngine.getKeyRows(NativeEngine.CAMERA)) if (r >= 0 && r < rows) mCamKeys[r] = true;
        mCurRow = NativeEngine.getCurrentRow();
        mCurLayer = NativeEngine.getCurrentLayer();
        ensureVisible(mCurRow);
        invalidate();
    }

    public void setCurrentRow(int row) {
        mCurRow = row;
        ensureVisible(row);
        invalidate();
    }

    int cellAt(int layer, int row) { return mCells[layer][row]; }

    private void ensureVisible(int row) {
        float y = row * mRowH, visible = getHeight() - mHeadH;
        if (visible <= 0) return;
        if (y < mScrollY) mScrollY = y;
        else if (y + mRowH > mScrollY + visible) mScrollY = y + mRowH - visible;
        mScrollY = Math.max(0, Math.min(maxScrollY(), mScrollY));
    }

    private int rows() { return mRows + 24; }
    private int maxScrollX() { return Math.max(0, mNames.length * mColW - (getWidth() - mFrameW)); }
    private int maxScrollY() { return Math.max(0, rows() * mRowH - (getHeight() - mHeadH)); }

    private void scrollByPx(float dx, float dy) {
        mScrollX = Math.max(0, Math.min(maxScrollX(), mScrollX + dx));
        mScrollY = Math.max(0, Math.min(maxScrollY(), mScrollY + dy));
        invalidate();
    }

    @Override
    public void computeScroll() {
        if (mScroller.computeScrollOffset()) {
            mScrollX = mScroller.getCurrX();
            mScrollY = mScroller.getCurrY();
            postInvalidateOnAnimation();
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        return mGestures.onTouchEvent(e);
    }

    private int[] colours(int type) {
        if (type == NativeEngine.LAYER_VECTOR) return new int[]{VECTOR, VECTOR_BORDER, VECTOR_SEL};
        if (type == NativeEngine.LAYER_RASTER) return new int[]{FULLCOLOR, FULLCOLOR_BORDER, FULLCOLOR_SEL};
        return new int[]{LEVEL, LEVEL_BORDER, LEVEL_SEL};
    }

    private void key(Canvas c, float cx, float cy, float s) {
        Path p = new Path();
        p.moveTo(cx, cy - s);
        p.lineTo(cx + s, cy);
        p.lineTo(cx, cy + s);
        p.lineTo(cx - s, cy);
        p.close();
        mFill.setColor(0xFFF5C342);
        c.drawPath(p, mFill);
    }

    @Override
    protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        c.drawColor(BG);
        float px = Ui.dp(getContext(), 1);
        float base = (mRowH - mText.descent() - mText.ascent()) / 2f;
        int r0 = Math.max(0, (int) (mScrollY / mRowH)), r1 = Math.min(rows(), r0 + (h - mHeadH) / mRowH + 2);
        int c0 = Math.max(0, (int) (mScrollX / mColW)), c1 = Math.min(mNames.length, c0 + (w - mFrameW) / mColW + 2);

        // ---- cells
        c.save();
        c.clipRect(mFrameW, mHeadH, w, h);
        for (int col = c0; col < c1; col++) {
            float x = mFrameW + col * mColW - mScrollX;
            int[] pal = colours(mTypes[col]);
            for (int r = r0; r < r1; r++) {
                float y = mHeadH + r * mRowH - mScrollY;
                int d = mCells[col][r];
                if (d == 0) {
                    mFill.setColor(EMPTY);
                    c.drawRect(x, y, x + mColW, y + mRowH, mFill);
                } else {
                    boolean first = r == 0 || mCells[col][r - 1] != d;
                    boolean blockStart = r == 0 || mCells[col][r - 1] == 0;
                    mFill.setColor(col == mCurLayer ? pal[2] : pal[0]);
                    c.drawRect(x, y, x + mColW, y + mRowH, mFill);
                    mFill.setColor(pal[1]);                       // level side strip
                    c.drawRect(x, y, x + px * 3, y + mRowH, mFill);
                    mText.setColor(TEXT);
                    if (first) {
                        mText.setTextAlign(Paint.Align.RIGHT);
                        c.drawText(String.valueOf(d), x + mColW - px * 12, y + base, mText);
                        if (blockStart) {
                            mText.setTextAlign(Paint.Align.LEFT);
                            String n = mNames[col];
                            int max = mText.breakText(n, true, mColW - px * 34, null);
                            c.drawText(n, 0, max, x + px * 6, y + base, mText);
                        }
                    } else {                                      // hold: continue line
                        mLine.setColor(TEXT);
                        mLine.setStrokeWidth(px);
                        c.drawLine(x + px * 9, y, x + px * 9, y + mRowH, mLine);
                    }
                    boolean last = r + 1 >= mRows || mCells[col][r + 1] == 0;
                    if (last) {                                   // level end mark
                        mLine.setColor(TEXT);
                        mLine.setStrokeWidth(px);
                        c.drawLine(x + px * 5, y + mRowH - px, x + px * 13, y + mRowH - px, mLine);
                    }
                }
                if (mKeys[col][r]) key(c, x + mColW - px * 6, y + mRowH / 2f, px * 4);
                mLine.setColor(LINE);
                mLine.setStrokeWidth(px);
                c.drawLine(x, y + mRowH, x + mColW, y + mRowH, mLine);
            }
            mLine.setColor(VLINE);
            mLine.setStrokeWidth(px);
            c.drawLine(x + mColW, mHeadH, x + mColW, h, mLine);
        }
        // current row band, second markers, cell focus
        for (int r = r0; r < r1; r++) {
            float y = mHeadH + r * mRowH - mScrollY;
            if (r > 0 && r % mFps == 0) {
                mLine.setColor(SEC_MARKER);
                mLine.setStrokeWidth(px);
                c.drawLine(mFrameW, y, w, y, mLine);
            } else if (r > 0 && r % 6 == 0) {
                mLine.setColor(MARKER);
                mLine.setStrokeWidth(px);
                c.drawLine(mFrameW, y, w, y, mLine);
            }
            if (r == mCurRow) {
                mFill.setColor(CUR_ROW);
                c.drawRect(mFrameW, y, w, y + mRowH, mFill);
                if (mCurLayer >= 0 && mCurLayer < mNames.length) {
                    float x = mFrameW + mCurLayer * mColW - mScrollX;
                    mLine.setStyle(Paint.Style.STROKE);
                    mLine.setColor(FOCUS);
                    mLine.setStrokeWidth(px * 1.5f);
                    c.drawRect(x + px, y + px, x + mColW - px, y + mRowH - px, mLine);
                    mLine.setStyle(Paint.Style.FILL);
                }
            }
        }
        c.restore();

        // ---- frame number column
        c.save();
        c.clipRect(0, mHeadH, mFrameW, h);
        mFill.setColor(BG);
        c.drawRect(0, mHeadH, mFrameW, h, mFill);
        mText.setTextAlign(Paint.Align.RIGHT);
        for (int r = r0; r < r1; r++) {
            float y = mHeadH + r * mRowH - mScrollY;
            if (r == mCurRow) {
                mFill.setColor(CUR_ROW);
                c.drawRect(0, y, mFrameW, y + mRowH, mFill);
            }
            mText.setColor(r == mCurRow ? 0xFFFFFFFF : r < mRows ? TEXT : 0x66FFFFFF);
            c.drawText(String.valueOf(r + 1), mFrameW - px * 8, y + base, mText);
            if (mCamKeys[r]) key(c, px * 7, y + mRowH / 2f, px * 3.5f);
            mLine.setColor(LINE);
            mLine.setStrokeWidth(px);
            c.drawLine(0, y + mRowH, mFrameW, y + mRowH, mLine);
        }
        c.restore();

        // ---- column headers
        c.save();
        c.clipRect(mFrameW, 0, w, mHeadH);
        for (int col = c0; col < c1; col++) {
            float x = mFrameW + col * mColW - mScrollX;
            int[] pal = colours(mTypes[col]);
            mFill.setColor(pal[0]);
            c.drawRect(x, 0, x + mColW, mHeadH, mFill);
            if (col == mCurLayer) {
                mFill.setColor(HEAD_SEL);
                c.drawRect(x, 0, x + mColW, mHeadH, mFill);
            }
            mText.setColor(0xFFDFDFDF);
            mText.setTextAlign(Paint.Align.LEFT);
            String n = mNames[col];
            int max = mText.breakText(n, true, mColW - px * 8, null);
            c.drawText(n, 0, max, x + px * 4, mHeadH * 0.25f - (mText.descent() + mText.ascent()) / 2f, mText);
            // camstand toggle (eye) and lock, like the desktop column header
            float cy = mHeadH * 0.75f, ex = x + mColW * 0.25f, lx = x + mColW * 0.75f, s = px * 6;
            mLine.setStrokeWidth(px * 1.5f);
            mLine.setStyle(Paint.Style.STROKE);
            mLine.setColor(mVisible[col] ? 0xFFFFFFFF : 0x55FFFFFF);
            c.drawOval(ex - s * 1.4f, cy - s * 0.8f, ex + s * 1.4f, cy + s * 0.8f, mLine);
            mLine.setStyle(Paint.Style.FILL);
            if (mVisible[col]) c.drawCircle(ex, cy, s * 0.4f, mLine);
            mLine.setColor(mLocked[col] ? 0xFFF5C342 : 0x55FFFFFF);
            c.drawRect(lx - s * 0.8f, cy - s * 0.1f, lx + s * 0.8f, cy + s * 0.9f, mLine);
            mLine.setStyle(Paint.Style.STROKE);
            c.drawArc(lx - s * 0.55f, cy - s * 0.9f, lx + s * 0.55f, cy + s * 0.4f, 180, 180, false, mLine);
            mLine.setStyle(Paint.Style.FILL);
            mLine.setColor(0xFF1A1A1A);
            mLine.setStrokeWidth(px);
            c.drawLine(x + mColW, 0, x + mColW, mHeadH, mLine);
        }
        c.restore();
        mFill.setColor(BG);
        c.drawRect(0, 0, mFrameW, mHeadH, mFill);
        mLine.setColor(0xFF1A1A1A);
        mLine.setStrokeWidth(px);
        c.drawLine(0, mHeadH, w, mHeadH, mLine);
        c.drawLine(mFrameW, 0, mFrameW, h, mLine);
    }
}
