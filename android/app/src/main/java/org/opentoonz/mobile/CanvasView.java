package org.opentoonz.mobile;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

/**
 * Central drawing surface.  The document is rendered by the native OpenToonz
 * engine into a canvas-sized bitmap (dirty-rect updates while drawing); this
 * view only maps it to the screen (zoom/pan) and turns touch / stylus input
 * into engine tool calls.
 *
 * Input rules:
 *  - one pointer: current tool (stylus pressure is used when available);
 *  - two pointers: pinch-zoom + pan (or scale/rotate a floating selection);
 *  - once a stylus has been seen, fingers only navigate (palm rejection).
 */
public class CanvasView extends View {
    public interface Listener {
        void onDocumentChanged();          // pixels / cells changed (timeline refresh)
        void onStylePicked(int styleId);
        void onSelectionChanged(boolean active);
        void onStylusDetected();
    }

    private static final int NONE = 0, DRAW_PENDING = 1, DRAWING = 2, NAVIGATE = 3,
            SELECT_RECT = 4, SEL_MOVE = 5, SEL_GESTURE = 6, SHAPE_DRAG = 7, LASSO = 8,
            XF_MOVE = 9, XF_GESTURE = 10;

    // tool modes set by the editor
    private int mShapeType = NativeEngine.SHAPE_LINE;
    private boolean mLasso;
    private boolean mCameraTarget;
    private int mEase = NativeEngine.EASE_IN_OUT;
    // shape / lasso / transform state
    private float mShX0, mShY0, mShX1, mShY1;
    private final java.util.ArrayList<float[]> mLassoPts = new java.util.ArrayList<>();
    private float[] mXfBase;
    private int mXfTarget;
    private float mXfDownX, mXfDownY;

    private Listener mListener;
    private Bitmap mBitmap;
    private final Matrix mView = new Matrix(), mInv = new Matrix();
    private final Paint mBmpPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint mShadow = new Paint(), mSelPaint = new Paint(), mCursor = new Paint();
    private int mTool = NativeEngine.TOOL_BRUSH;
    private boolean mOnion;
    private int mRow;
    private boolean mStylusSeen;
    private boolean mPlaying;
    private int mState = NONE;
    private float mDownX, mDownY;          // screen coords of first pointer
    private long mDownTime;
    private final float mSlop;
    // navigation
    private float mNavLastX, mNavLastY, mNavLastDist, mNavLastAngle;
    // selection
    private float mSelX0, mSelY0, mSelX1, mSelY1;         // rect being dragged (doc)
    private float mSelDx, mSelDy, mSelScale = 1, mSelAngle; // current transform
    private float mGestStartDx, mGestStartDy, mGestStartScale, mGestStartAngle;
    private float mGestStartDist, mGestStartAngleRad, mGestStartCx, mGestStartCy;
    private boolean mFitted;
    private float mHoverX = -1, mHoverY = -1;
    private float mCursorSize = 6;

    public CanvasView(Context c) {
        super(c);
        mSlop = ViewConfiguration.get(c).getScaledTouchSlop() * 0.5f;
        mShadow.setColor(0x60000000);
        mSelPaint.setStyle(Paint.Style.STROKE);
        mSelPaint.setStrokeWidth(Ui.dp(c, 1.5f));
        mSelPaint.setColor(0xFF2D8CFF);
        mSelPaint.setPathEffect(new DashPathEffect(new float[]{Ui.dp(c, 6), Ui.dp(c, 4)}, 0));
        mCursor.setStyle(Paint.Style.STROKE);
        mCursor.setColor(0xA0000000);
        mCursor.setStrokeWidth(Ui.dp(c, 1));
        setBackgroundColor(0xFF3A3F4E);
    }

    public void setListener(Listener l) { mListener = l; }

    /** (Re)creates the canvas bitmap for the currently loaded document. */
    public void documentLoaded() {
        int w = NativeEngine.getWidth(), h = NativeEngine.getHeight();
        if (mBitmap == null || mBitmap.getWidth() != w || mBitmap.getHeight() != h) {
            if (mBitmap != null) mBitmap.recycle();
            mBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        }
        mFitted = false;
        renderAll();
        requestLayout();
    }

    public void setTool(int tool) {
        if (mTool == NativeEngine.TOOL_SELECT && tool != mTool && NativeEngine.hasSelection()) {
            NativeEngine.commitSelection();
            selectionDone();
        }
        mTool = tool;
        NativeEngine.setTool(tool);
        invalidate();
    }

    public void setCursorSize(float s) { mCursorSize = s; }
    public void setShapeType(int t) { mShapeType = t; }
    public void setLasso(boolean lasso) { mLasso = lasso; }
    public void setCameraTarget(boolean camera) { mCameraTarget = camera; }
    public void setEase(int ease) { mEase = ease; }
    private boolean mPivotMode;
    public void setPivotMode(boolean on) { mPivotMode = on; invalidate(); }
    public void setOnion(boolean on) { mOnion = on; renderAll(); }
    public void setPlaying(boolean p) { mPlaying = p; }
    public boolean isStylusMode() { return mStylusSeen; }

    public void showRow(int row) {
        mRow = row;
        renderAll();
    }

    public void renderAll() {
        if (mBitmap == null) return;
        NativeEngine.render(mBitmap, mRow, 0, 0, mBitmap.getWidth() - 1, mBitmap.getHeight() - 1,
                mOnion && !mPlaying);
        invalidate();
    }

    private void renderDirty(int[] r) {
        if (mBitmap == null || r == null || r[2] < r[0] || r[3] < r[1]) return;
        NativeEngine.render(mBitmap, mRow, r[0], r[1], r[2], r[3], mOnion);
        invalidate();
    }

    // ---------------------------------------------------------------- view mapping
    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        if (!mFitted || ow == 0) fit();
        else {
            // keep the document centre in the middle after rotation
            mView.postTranslate((w - ow) / 2f, (h - oh) / 2f);
            mView.invert(mInv);
        }
    }

    public void fit() {
        if (mBitmap == null || getWidth() == 0) return;
        float m = Ui.dp(getContext(), 12);
        float s = Math.min((getWidth() - 2 * m) / mBitmap.getWidth(),
                (getHeight() - 2 * m) / mBitmap.getHeight());
        mView.reset();
        mView.postScale(s, s);
        mView.postTranslate((getWidth() - mBitmap.getWidth() * s) / 2f,
                (getHeight() - mBitmap.getHeight() * s) / 2f);
        mView.invert(mInv);
        mFitted = true;
        invalidate();
    }

    private float[] toDoc(float x, float y) {
        float[] p = {x, y};
        mInv.mapPoints(p);
        return p;
    }

    private float viewScale() {
        float[] v = new float[9];
        mView.getValues(v);
        return (float) Math.hypot(v[Matrix.MSCALE_X], v[Matrix.MSKEW_Y]);
    }

    @Override
    protected void onDraw(Canvas c) {
        if (mBitmap == null) return;
        c.save();
        c.concat(mView);
        float sh = 4 / viewScale();
        c.drawRect(sh, sh, mBitmap.getWidth() + sh, mBitmap.getHeight() + sh, mShadow);
        mBmpPaint.setFilterBitmap(viewScale() < 2f);
        c.drawBitmap(mBitmap, 0, 0, mBmpPaint);
        float lw = Ui.dp(getContext(), 1.5f) / viewScale();
        mSelPaint.setStrokeWidth(lw);
        if (mState == SHAPE_DRAG) {
            android.graphics.Paint sp = new android.graphics.Paint(mSelPaint);
            sp.setPathEffect(null);
            sp.setColor(0xC0000000);
            if (mShapeType == NativeEngine.SHAPE_LINE) c.drawLine(mShX0, mShY0, mShX1, mShY1, sp);
            else {
                RectF r = new RectF(Math.min(mShX0, mShX1), Math.min(mShY0, mShY1),
                        Math.max(mShX0, mShX1), Math.max(mShY0, mShY1));
                if (mShapeType == NativeEngine.SHAPE_RECT) c.drawRect(r, sp);
                else c.drawOval(r, sp);
            }
        } else if (mState == LASSO && mLassoPts.size() > 1) {
            android.graphics.Path path = new android.graphics.Path();
            path.moveTo(mLassoPts.get(0)[0], mLassoPts.get(0)[1]);
            for (float[] q : mLassoPts) path.lineTo(q[0], q[1]);
            c.drawPath(path, mSelPaint);
        } else if (mState == SELECT_RECT) {
            c.drawRect(Math.min(mSelX0, mSelX1), Math.min(mSelY0, mSelY1),
                    Math.max(mSelX0, mSelX1), Math.max(mSelY0, mSelY1), mSelPaint);
        } else if (NativeEngine.hasSelection()) {
            int[] b = NativeEngine.selectionBounds();
            if (b[2] >= b[0]) c.drawRect(b[0], b[1], b[2] + 1, b[3] + 1, mSelPaint);
        }
        if (mTool == NativeEngine.TOOL_TRANSFORM && !mCameraTarget) {  // pivot marker
            float[] pv = NativeEngine.getPivot(NativeEngine.getCurrentLayer());
            android.graphics.Paint pp = new android.graphics.Paint(Paint.ANTI_ALIAS_FLAG);
            pp.setStyle(Paint.Style.STROKE);
            pp.setStrokeWidth(lw * 1.5f);
            pp.setColor(mPivotMode ? 0xFFFF5252 : 0xFF2D8CFF);
            float rr = Ui.dp(getContext(), 7) / viewScale();
            c.drawCircle(pv[0], pv[1], rr, pp);
            c.drawLine(pv[0] - 2 * rr, pv[1], pv[0] + 2 * rr, pv[1], pp);
            c.drawLine(pv[0], pv[1] - 2 * rr, pv[0], pv[1] + 2 * rr, pp);
        }
        if (mHoverX >= 0 && (mTool == NativeEngine.TOOL_BRUSH || mTool == NativeEngine.TOOL_ERASER)) {
            mCursor.setStrokeWidth(lw * 0.7f);
            c.drawCircle(mHoverX, mHoverY, mCursorSize / 2f, mCursor);
        }
        c.restore();
    }

    // ---------------------------------------------------------------- input
    @Override
    public boolean onHoverEvent(MotionEvent e) {
        if (e.getToolType(0) == MotionEvent.TOOL_TYPE_STYLUS) {
            if (e.getActionMasked() == MotionEvent.ACTION_HOVER_EXIT) {
                mHoverX = mHoverY = -1;
            } else {
                float[] p = toDoc(e.getX(), e.getY());
                mHoverX = p[0];
                mHoverY = p[1];
            }
            invalidate();
            return true;
        }
        return super.onHoverEvent(e);
    }

    private static boolean isStylus(MotionEvent e, int idx) {
        int t = e.getToolType(idx);
        return t == MotionEvent.TOOL_TYPE_STYLUS || t == MotionEvent.TOOL_TYPE_ERASER;
    }

    private float pressure(MotionEvent e, int idx, int hist) {
        if (!isStylus(e, idx)) return 1f;
        float p = hist < 0 ? e.getPressure(idx) : e.getHistoricalPressure(idx, hist);
        return Math.max(0.05f, Math.min(1f, p));
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (mBitmap == null || mPlaying) return true;
        int action = e.getActionMasked();
        if (isStylus(e, e.getActionIndex()) && !mStylusSeen) {
            mStylusSeen = true;
            if (mListener != null) mListener.onStylusDetected();
        }
        switch (action) {
            case MotionEvent.ACTION_DOWN: onDown(e); break;
            case MotionEvent.ACTION_POINTER_DOWN: onSecondPointer(e); break;
            case MotionEvent.ACTION_MOVE: onMove(e); break;
            case MotionEvent.ACTION_POINTER_UP:
                if (mState == XF_GESTURE) {
                    // continue as a one finger move from the current key
                    int keep = e.getActionIndex() == 0 ? 1 : 0;
                    mState = XF_MOVE;
                    mXfBase = NativeEngine.getXform(mXfTarget, mRow);
                    float[] k = toDoc(e.getX(keep), e.getY(keep));
                    mXfDownX = k[0];
                    mXfDownY = k[1];
                    break;
                }
                if (mState == NAVIGATE || mState == SEL_GESTURE) {
                    int keep = e.getActionIndex() == 0 ? 1 : 0;
                    mNavLastX = e.getX(keep);
                    mNavLastY = e.getY(keep);
                    mNavLastDist = 0;
                    if (mState == SEL_GESTURE) mState = NONE;
                }
                break;
            case MotionEvent.ACTION_UP: onUp(e); break;
            case MotionEvent.ACTION_CANCEL:
                if (mState == XF_MOVE || mState == XF_GESTURE) NativeEngine.endKeyEdit();
                if (mState == DRAWING) {
                    float[] p = toDoc(e.getX(), e.getY());
                    renderDirty(NativeEngine.pointerUp(p[0], p[1], 1f));
                    changed();
                }
                mState = NONE;
                break;
        }
        return true;
    }

    private boolean fingerOnlyNavigates(MotionEvent e) {
        return mStylusSeen && !isStylus(e, 0);
    }

    private void onDown(MotionEvent e) {
        mDownX = e.getX();
        mDownY = e.getY();
        mDownTime = SystemClock.uptimeMillis();
        mNavLastX = mDownX;
        mNavLastY = mDownY;
        mNavLastDist = 0;
        if (fingerOnlyNavigates(e)) {
            mState = NAVIGATE;
            return;
        }
        float[] p = toDoc(mDownX, mDownY);
        if (mTool == NativeEngine.TOOL_SHAPE) {
            mState = SHAPE_DRAG;
            mShX0 = mShX1 = p[0];
            mShY0 = mShY1 = p[1];
            return;
        }
        if (mTool == NativeEngine.TOOL_TRANSFORM && mPivotMode && !mCameraTarget) {
            NativeEngine.setPivot(NativeEngine.getCurrentLayer(), p[0], p[1]);
            mState = NONE;
            renderAll();
            changed();
            return;
        }
        if (mTool == NativeEngine.TOOL_TRANSFORM) {
            mState = XF_MOVE;
            mXfTarget = mCameraTarget ? NativeEngine.CAMERA : NativeEngine.getCurrentLayer();
            NativeEngine.beginKeyEdit(mXfTarget);
            mXfBase = NativeEngine.getXform(mXfTarget, mRow);
            mXfDownX = p[0];
            mXfDownY = p[1];
            return;
        }
        if (mTool == NativeEngine.TOOL_SELECT && mLasso && !NativeEngine.hasSelection()) {
            mState = LASSO;
            mLassoPts.clear();
            mLassoPts.add(p);
            return;
        }
        if (mTool == NativeEngine.TOOL_SELECT) {
            if (NativeEngine.hasSelection()) {
                int[] b = NativeEngine.selectionBounds();
                if (p[0] >= b[0] && p[0] <= b[2] + 1 && p[1] >= b[1] && p[1] <= b[3] + 1) {
                    mState = SEL_MOVE;
                    mGestStartDx = mSelDx;
                    mGestStartDy = mSelDy;
                    return;
                }
                NativeEngine.commitSelection();
                selectionDone();
            }
            mState = SELECT_RECT;
            mSelX0 = mSelX1 = p[0];
            mSelY0 = mSelY1 = p[1];
            return;
        }
        mState = DRAW_PENDING;
        if (isStylus(e, 0)) startStroke(e);  // stylus: no gesture ambiguity
    }

    private void startStroke(MotionEvent e) {
        if (mTool != NativeEngine.TOOL_BRUSH && mTool != NativeEngine.TOOL_ERASER) return;
        float[] p = toDoc(mDownX, mDownY);
        renderDirty(NativeEngine.pointerDown(p[0], p[1], pressure(e, 0, -1)));
        mState = DRAWING;
    }

    private void onSecondPointer(MotionEvent e) {
        if (mState == DRAW_PENDING || mState == NONE || mState == SELECT_RECT) {
            mState = NAVIGATE;
        } else if (mState == DRAWING) {
            if (SystemClock.uptimeMillis() - mDownTime < 300 && !isStylus(e, 0)) {
                // the "stroke" was the start of a two finger gesture: drop it
                float[] p = toDoc(e.getX(0), e.getY(0));
                NativeEngine.pointerUp(p[0], p[1], 1f);
                NativeEngine.undo();
                renderAll();
                mState = NAVIGATE;
            } else return;
        } else if (mState == XF_MOVE && e.getPointerCount() >= 2) {
            mState = XF_GESTURE;
            mXfBase = NativeEngine.getXform(mXfTarget, mRow);
            float[] a = toDoc(e.getX(0), e.getY(0)), b = toDoc(e.getX(1), e.getY(1));
            mGestStartDist = (float) Math.hypot(a[0] - b[0], a[1] - b[1]);
            mGestStartAngleRad = (float) Math.atan2(b[1] - a[1], b[0] - a[0]);
            mGestStartCx = (a[0] + b[0]) / 2;
            mGestStartCy = (a[1] + b[1]) / 2;
            return;
        } else if (mState == SEL_MOVE) {
            mState = SEL_GESTURE;
            float[] a = toDoc(e.getX(0), e.getY(0)), b = toDoc(e.getX(1), e.getY(1));
            mGestStartDist = (float) Math.hypot(a[0] - b[0], a[1] - b[1]);
            mGestStartAngleRad = (float) Math.atan2(b[1] - a[1], b[0] - a[0]);
            mGestStartCx = (a[0] + b[0]) / 2;
            mGestStartCy = (a[1] + b[1]) / 2;
            mGestStartDx = mSelDx;
            mGestStartDy = mSelDy;
            mGestStartScale = mSelScale;
            mGestStartAngle = mSelAngle;
            return;
        }
        if (mState == NAVIGATE && e.getPointerCount() >= 2) {
            mNavLastX = (e.getX(0) + e.getX(1)) / 2;
            mNavLastY = (e.getY(0) + e.getY(1)) / 2;
            mNavLastDist = (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1));
            mNavLastAngle = (float) Math.atan2(e.getY(1) - e.getY(0), e.getX(1) - e.getX(0));
        }
    }

    private void onMove(MotionEvent e) {
        switch (mState) {
            case DRAW_PENDING:
                if (Math.hypot(e.getX() - mDownX, e.getY() - mDownY) > mSlop) {
                    startStroke(e);
                    if (mState == DRAWING) strokeMove(e);
                }
                break;
            case DRAWING: strokeMove(e); break;
            case NAVIGATE: navigate(e); break;
            case SHAPE_DRAG: {
                float[] p = toDoc(e.getX(), e.getY());
                mShX1 = p[0];
                mShY1 = p[1];
                invalidate();
                break;
            }
            case LASSO: {
                for (int h = 0; h < e.getHistorySize(); h++)
                    mLassoPts.add(toDoc(e.getHistoricalX(0, h), e.getHistoricalY(0, h)));
                mLassoPts.add(toDoc(e.getX(), e.getY()));
                invalidate();
                break;
            }
            case XF_MOVE: {
                float[] p = toDoc(e.getX(), e.getY());
                float dx = p[0] - mXfDownX, dy = p[1] - mXfDownY;
                float[] xf = mXfBase.clone();
                // moving the camera right makes the scene go left: invert
                if (mXfTarget == NativeEngine.CAMERA) {
                    // moving the camera right makes the scene go left: invert
                    xf[0] -= dx;
                    xf[1] -= dy;
                } else {
                    // a child layer moves in its parent's (rotated/scaled) frame
                    float[] d = NativeEngine.canvasDeltaToLocal(mXfTarget, mRow, dx, dy);
                    xf[0] += d[0];
                    xf[1] += d[1];
                }
                NativeEngine.setKeyLive(mXfTarget, mRow, xf, mEase);
                renderAll();
                break;
            }
            case XF_GESTURE: {
                if (e.getPointerCount() < 2) break;
                float[] a = toDoc(e.getX(0), e.getY(0)), b = toDoc(e.getX(1), e.getY(1));
                float dist = (float) Math.hypot(a[0] - b[0], a[1] - b[1]);
                float ang = (float) Math.atan2(b[1] - a[1], b[0] - a[0]);
                float ratio = mGestStartDist > 1 ? dist / mGestStartDist : 1;
                float dAng = (float) Math.toDegrees(ang - mGestStartAngleRad);
                float[] xf = mXfBase.clone();
                boolean cam = mXfTarget == NativeEngine.CAMERA;
                float sign = cam ? -1 : 1;
                float gdx = (a[0] + b[0]) / 2 - mGestStartCx, gdy = (a[1] + b[1]) / 2 - mGestStartCy;
                if (cam) {
                    xf[0] -= gdx;
                    xf[1] -= gdy;
                } else {
                    float[] d = NativeEngine.canvasDeltaToLocal(mXfTarget, mRow, gdx, gdy);
                    xf[0] += d[0];
                    xf[1] += d[1];
                }
                xf[2] += sign * dAng;
                xf[3] = mXfBase[3] * ratio;  // camera: zoom follows the pinch
                NativeEngine.setKeyLive(mXfTarget, mRow, xf, mEase);
                renderAll();
                break;
            }
            case SELECT_RECT: {
                float[] p = toDoc(e.getX(), e.getY());
                mSelX1 = p[0];
                mSelY1 = p[1];
                invalidate();
                break;
            }
            case SEL_MOVE: {
                float[] a = toDoc(mDownX, mDownY), b = toDoc(e.getX(), e.getY());
                mSelDx = mGestStartDx + b[0] - a[0];
                mSelDy = mGestStartDy + b[1] - a[1];
                applySelectionTransform();
                break;
            }
            case SEL_GESTURE: {
                if (e.getPointerCount() < 2) break;
                float[] a = toDoc(e.getX(0), e.getY(0)), b = toDoc(e.getX(1), e.getY(1));
                float dist = (float) Math.hypot(a[0] - b[0], a[1] - b[1]);
                float ang = (float) Math.atan2(b[1] - a[1], b[0] - a[0]);
                mSelScale = mGestStartScale * (mGestStartDist > 1 ? dist / mGestStartDist : 1);
                mSelAngle = mGestStartAngle + (float) Math.toDegrees(ang - mGestStartAngleRad);
                mSelDx = mGestStartDx + (a[0] + b[0]) / 2 - mGestStartCx;
                mSelDy = mGestStartDy + (a[1] + b[1]) / 2 - mGestStartCy;
                applySelectionTransform();
                break;
            }
        }
    }

    private void strokeMove(MotionEvent e) {
        int x0 = Integer.MAX_VALUE, y0 = Integer.MAX_VALUE, x1 = -1, y1 = -1;
        int n = e.getHistorySize();
        for (int h = 0; h <= n; h++) {
            float sx = h < n ? e.getHistoricalX(0, h) : e.getX(0);
            float sy = h < n ? e.getHistoricalY(0, h) : e.getY(0);
            float[] p = toDoc(sx, sy);
            int[] r = NativeEngine.pointerMove(p[0], p[1], pressure(e, 0, h < n ? h : -1));
            if (r[2] >= r[0]) {
                x0 = Math.min(x0, r[0]); y0 = Math.min(y0, r[1]);
                x1 = Math.max(x1, r[2]); y1 = Math.max(y1, r[3]);
            }
        }
        if (x1 >= 0) renderDirty(new int[]{x0, y0, x1, y1});
    }

    private void navigate(MotionEvent e) {
        if (e.getPointerCount() >= 2) {
            float cx = (e.getX(0) + e.getX(1)) / 2, cy = (e.getY(0) + e.getY(1)) / 2;
            float dist = (float) Math.hypot(e.getX(0) - e.getX(1), e.getY(0) - e.getY(1));
            if (mNavLastDist > 0) {
                float s = dist / mNavLastDist;
                float cur = viewScale();
                s = Math.max(0.05f / cur, Math.min(40f / cur, s));
                mView.postTranslate(cx - mNavLastX, cy - mNavLastY);
                mView.postScale(s, s, cx, cy);
            }
            mNavLastX = cx;
            mNavLastY = cy;
            mNavLastDist = dist;
        } else {
            mView.postTranslate(e.getX() - mNavLastX, e.getY() - mNavLastY);
            mNavLastX = e.getX();
            mNavLastY = e.getY();
        }
        mView.invert(mInv);
        invalidate();
    }

    private void onUp(MotionEvent e) {
        // the release position is the final position of a selection drag
        if (mState == SEL_MOVE || mState == XF_MOVE) onMove(e);
        float[] p = toDoc(e.getX(), e.getY());
        boolean tap = Math.hypot(e.getX() - mDownX, e.getY() - mDownY) <= mSlop * 2;
        switch (mState) {
            case DRAW_PENDING:
                if (!tap) break;
                if (mTool == NativeEngine.TOOL_FILL) {
                    renderDirty(NativeEngine.pointerDown(p[0], p[1], 1f));
                    NativeEngine.pointerUp(p[0], p[1], 1f);
                    changed();
                } else if (mTool == NativeEngine.TOOL_PICKER) {
                    NativeEngine.pointerDown(p[0], p[1], 1f);
                    int s = NativeEngine.getPickedStyle();
                    if (s > 0 && mListener != null) mListener.onStylePicked(s);
                } else if (mTool == NativeEngine.TOOL_BRUSH || mTool == NativeEngine.TOOL_ERASER) {
                    renderDirty(NativeEngine.pointerDown(p[0], p[1], pressure(e, 0, -1)));
                    renderDirty(NativeEngine.pointerUp(p[0], p[1], pressure(e, 0, -1)));
                    changed();
                }
                break;
            case DRAWING:
                renderDirty(NativeEngine.pointerUp(p[0], p[1], pressure(e, 0, -1)));
                changed();
                break;
            case SHAPE_DRAG:
                if (Math.hypot(p[0] - mShX0, p[1] - mShY0) > 2) {
                    renderDirty(NativeEngine.drawShape(mShapeType, mShX0, mShY0, p[0], p[1]));
                    changed();
                }
                invalidate();
                break;
            case LASSO: {
                mState = NONE;
                if (mLassoPts.size() >= 3) {
                    float[] xy = new float[mLassoPts.size() * 2];
                    for (int i = 0; i < mLassoPts.size(); i++) {
                        xy[2 * i] = mLassoPts.get(i)[0];
                        xy[2 * i + 1] = mLassoPts.get(i)[1];
                    }
                    if (NativeEngine.selectPolygon(xy)) {
                        mSelDx = mSelDy = mSelAngle = 0;
                        mSelScale = 1;
                        if (mListener != null) mListener.onSelectionChanged(true);
                    }
                }
                mLassoPts.clear();
                renderAll();
                break;
            }
            case XF_MOVE:
            case XF_GESTURE:
                NativeEngine.endKeyEdit();
                renderAll();
                changed();
                break;
            case SELECT_RECT: {
                int x0 = Math.round(Math.min(mSelX0, mSelX1)), y0 = Math.round(Math.min(mSelY0, mSelY1));
                int x1 = Math.round(Math.max(mSelX0, mSelX1)), y1 = Math.round(Math.max(mSelY0, mSelY1));
                mState = NONE;
                if (x1 - x0 > 2 && y1 - y0 > 2 && NativeEngine.selectRect(x0, y0, x1, y1)) {
                    mSelDx = mSelDy = mSelAngle = 0;
                    mSelScale = 1;
                    if (mListener != null) mListener.onSelectionChanged(true);
                }
                renderAll();
                break;
            }
        }
        mState = NONE;
    }

    private void applySelectionTransform() {
        NativeEngine.transformSelection(mSelDx, mSelDy, mSelScale, mSelAngle);
        renderAll();
    }

    /** Called after the selection has been committed / cancelled / deleted. */
    public void selectionDone() {
        mSelDx = mSelDy = mSelAngle = 0;
        mSelScale = 1;
        renderAll();
        changed();
        if (mListener != null) mListener.onSelectionChanged(false);
    }

    private void changed() {
        if (mListener != null) mListener.onDocumentChanged();
    }

    public RectF docRectOnScreen() {
        RectF r = new RectF(0, 0, mBitmap.getWidth(), mBitmap.getHeight());
        mView.mapRect(r);
        return r;
    }
}
