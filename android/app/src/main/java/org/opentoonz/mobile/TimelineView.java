package org.opentoonz.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.widget.OverScroller;

/**
 * Timeline (OpenToonz "Timeline" orientation of the xsheet): one row per layer
 * (top layer on top), one column per frame.  A cell shows its drawing number;
 * a repeated drawing (hold) is drawn as a continuation bar, like in OpenToonz.
 * Tap = select frame + layer.  Drag = scroll.
 */
public class TimelineView extends View {
    public interface Listener {
        void onCellSelected(int layer, int row);
    }

    private Listener mListener;
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG), mFill = new Paint(),
            mLine = new Paint();
    private final int mCellW, mCellH, mHeaderH, mNameW;
    private final GestureDetector mGestures;
    private final OverScroller mScroller;
    private float mScrollX, mScrollY;
    private int[][] mCells = new int[0][0];  // [layer][row]
    private String[] mNames = new String[0];
    private boolean[] mVisible = new boolean[0];
    private int[] mTypes = new int[0];
    private int mRows, mCurRow, mCurLayer;

    public TimelineView(Context c) {
        super(c);
        mCellW = Ui.dp(c, 40);
        mCellH = Ui.dp(c, 30);
        mHeaderH = Ui.dp(c, 22);
        mNameW = Ui.dp(c, 92);
        mText.setTextSize(Ui.dp(c, 12));
        mText.setColor(Ui.TEXT);
        mLine.setColor(0xFF3A3F4E);
        mLine.setStrokeWidth(1);
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
        setBackgroundColor(Ui.PANEL);
    }

    public void setListener(Listener l) { mListener = l; }

    @Override
    protected void onMeasure(int wSpec, int hSpec) {
        int rows = Math.max(1, Math.min(4, mNames.length));
        int h = mHeaderH + rows * mCellH + Ui.dp(getContext(), 2);
        setMeasuredDimension(MeasureSpec.getSize(wSpec), resolveSize(h, hSpec));
    }

    /** Pulls the xsheet state from the engine. */
    public void refresh() {
        int layers = NativeEngine.getLayerCount();
        mRows = NativeEngine.getFrameCount();
        int cols = mRows + 24;  // room to draw beyond the last frame
        mCells = new int[layers][cols];
        mNames = new String[layers];
        mVisible = new boolean[layers];
        mTypes = new int[layers];
        for (int l = 0; l < layers; l++) {
            mNames[l] = NativeEngine.getLayerName(l);
            mVisible[l] = NativeEngine.isLayerVisible(l);
            mTypes[l] = NativeEngine.getLayerType(l);
            for (int r = 0; r < cols; r++) mCells[l][r] = r < mRows ? NativeEngine.getCell(l, r) : 0;
        }
        mCurRow = NativeEngine.getCurrentRow();
        mCurLayer = NativeEngine.getCurrentLayer();
        requestLayout();
        ensureVisible(mCurRow);
        invalidate();
    }

    public void setCurrentRow(int row) {
        mCurRow = row;
        ensureVisible(row);
        invalidate();
    }

    private void ensureVisible(int row) {
        float x = row * mCellW;
        float visible = getWidth() - mNameW;
        if (visible <= 0) return;
        if (x < mScrollX) mScrollX = x;
        else if (x + mCellW > mScrollX + visible) mScrollX = x + mCellW - visible;
        mScrollX = Math.max(0, Math.min(maxScrollX(), mScrollX));
    }

    private int cols() { return mCells.length > 0 ? mCells[0].length : mRows + 24; }
    private int maxScrollX() { return Math.max(0, cols() * mCellW - (getWidth() - mNameW)); }
    private int maxScrollY() { return Math.max(0, mNames.length * mCellH - (getHeight() - mHeaderH)); }

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
        getParent().requestDisallowInterceptTouchEvent(true);
        return mGestures.onTouchEvent(e) || super.onTouchEvent(e);
    }

    private void tap(float x, float y) {
        int row = x < mNameW ? mCurRow : (int) ((x - mNameW + mScrollX) / mCellW);
        int layer = mCurLayer;
        if (y > mHeaderH) {
            int visualRow = (int) ((y - mHeaderH + mScrollY) / mCellH);
            int l = mNames.length - 1 - visualRow;  // top row = top layer
            if (l >= 0 && l < mNames.length) layer = l;
        }
        if (row < 0) return;
        if (mListener != null) mListener.onCellSelected(layer, row);
    }

    @Override
    protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        int cols = cols();
        int firstCol = (int) (mScrollX / mCellW), lastCol = Math.min(cols - 1, firstCol + (w - mNameW) / mCellW + 1);
        // header with frame numbers
        mFill.setColor(0xFF20232D);
        c.drawRect(0, 0, w, mHeaderH, mFill);
        c.save();
        c.clipRect(mNameW, 0, w, h);
        for (int col = firstCol; col <= lastCol; col++) {
            float x = mNameW + col * mCellW - mScrollX;
            if (col == mCurRow) {
                mFill.setColor(0x40F5C342);
                c.drawRect(x, 0, x + mCellW, h, mFill);
            }
            mText.setColor(col < mRows ? Ui.TEXT : Ui.DIM);
            mText.setTextAlign(Paint.Align.CENTER);
            c.drawText(String.valueOf(col + 1), x + mCellW / 2f, mHeaderH * 0.72f, mText);
        }
        c.restore();
        // layer rows
        RectF r = new RectF();
        for (int l = mNames.length - 1, vr = 0; l >= 0; l--, vr++) {
            float y = mHeaderH + vr * mCellH - mScrollY;
            if (y + mCellH < mHeaderH || y > h) continue;
            c.save();
            c.clipRect(0, mHeaderH, w, h);
            mFill.setColor(l == mCurLayer ? 0xFF3A4152 : Ui.PANEL);
            c.drawRect(0, y, w, y + mCellH, mFill);
            // name column
            mText.setTextAlign(Paint.Align.LEFT);
            mText.setColor(mVisible[l] ? Ui.TEXT : Ui.DIM);
            String nm = (mTypes[l] == NativeEngine.LAYER_RASTER ? "▣ " : "✎ ") + mNames[l];
            c.drawText(ellipsize(nm, mNameW - Ui.dp(getContext(), 8)), Ui.dp(getContext(), 6),
                    y + mCellH * 0.65f, mText);
            c.clipRect(mNameW, mHeaderH, w, h);
            for (int col = firstCol; col <= lastCol; col++) {
                float x = mNameW + col * mCellW - mScrollX;
                int d = mCells[l][col];
                if (d > 0) {
                    boolean hold = col > 0 && mCells[l][col - 1] == d;
                    boolean holdNext = col + 1 < cols && mCells[l][col + 1] == d;
                    int base = mTypes[l] == NativeEngine.LAYER_RASTER ? 0xFF4F7A5A : 0xFF4E6A9C;
                    if (!mVisible[l]) base = 0xFF4A4D57;
                    mFill.setColor(base);
                    r.set(x + (hold ? 0 : 2), y + 3, x + mCellW - (holdNext ? 0 : 2), y + mCellH - 3);
                    c.drawRect(r, mFill);
                    mText.setTextAlign(Paint.Align.CENTER);
                    mText.setColor(Ui.TEXT);
                    if (!hold) c.drawText(String.valueOf(d), x + mCellW / 2f, y + mCellH * 0.65f, mText);
                    else {
                        mFill.setColor(0xC0FFFFFF);
                        c.drawRect(x + mCellW / 2f - 1, y + 7, x + mCellW / 2f + 1, y + mCellH - 7, mFill);
                    }
                }
                if (l == mCurLayer && col == mCurRow) {
                    Paint p = new Paint();
                    p.setStyle(Paint.Style.STROKE);
                    p.setColor(Ui.ACCENT);
                    p.setStrokeWidth(Ui.dp(getContext(), 2));
                    c.drawRect(x + 1, y + 1, x + mCellW - 1, y + mCellH - 1, p);
                }
                c.drawLine(x + mCellW, y, x + mCellW, y + mCellH, mLine);
            }
            c.restore();
            c.drawLine(0, y + mCellH, w, y + mCellH, mLine);
        }
        c.drawLine(mNameW, 0, mNameW, h, mLine);
    }

    private String ellipsize(String s, float maxW) {
        if (mText.measureText(s) <= maxW) return s;
        while (s.length() > 1 && mText.measureText(s + "…") > maxW) s = s.substring(0, s.length() - 1);
        return s + "…";
    }
}
