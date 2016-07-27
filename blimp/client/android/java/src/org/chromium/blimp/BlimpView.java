// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.blimp;

import android.content.Context;
import android.graphics.Point;
import android.os.Build;
import android.util.AttributeSet;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import org.chromium.base.annotations.JNINamespace;

/**
 * A {@link View} that will visually represent the Blimp rendered content.  This {@link View} starts
 * a native compositor.
 */
@JNINamespace("blimp")
public class BlimpView extends SurfaceView implements SurfaceHolder.Callback2 {
    private long mNativeBlimpViewPtr;

    /**
     * Builds a new {@link BlimpView}.
     * @param context A {@link Context} instance.
     * @param attrs   An {@link AttributeSet} instance.
     */
    public BlimpView(Context context, AttributeSet attrs) {
        super(context, attrs);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /**
     * Starts up rendering for this {@link View}.  This will start up the native compositor and will
     * display it's contents.
     */
    public void initializeRenderer() {
        assert mNativeBlimpViewPtr == 0;

        WindowManager windowManager =
                (WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE);
        Point displaySize = new Point();
        windowManager.getDefaultDisplay().getSize(displaySize);
        Point physicalSize = new Point();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            windowManager.getDefaultDisplay().getRealSize(physicalSize);
        }
        // TODO(dtrainor): Change 1.f to dpToPx once native fully supports dp.
        float compositorDensity = 1.f;
        mNativeBlimpViewPtr = nativeInit(
                physicalSize.x, physicalSize.y, displaySize.x, displaySize.y, compositorDensity);
        getHolder().addCallback(this);
        setVisibility(VISIBLE);
    }

    /**
     * Stops rendering for this {@link View} and destroys all internal state.  This {@link View}
     * should not be used after this.
     */
    public void destroyRenderer() {
        getHolder().removeCallback(this);
        if (mNativeBlimpViewPtr != 0) {
            nativeDestroy(mNativeBlimpViewPtr);
            mNativeBlimpViewPtr = 0;
        }
    }

    /**
     * Triggers a redraw of the native compositor, pushing a new frame.
     */
    public void setNeedsComposite() {
        if (mNativeBlimpViewPtr == 0) return;
        nativeSetNeedsComposite(mNativeBlimpViewPtr);
    }

    /**
     * Toggles whether or not the native compositor draws to this {@link View} or not.
     * @param visible Whether or not the compositor should draw or not.
     */
    public void setCompositorVisibility(boolean visible) {
        if (mNativeBlimpViewPtr == 0) return;
        nativeSetVisibility(mNativeBlimpViewPtr, visible);
    }

    // SurfaceView overrides.
    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        setZOrderMediaOverlay(true);
        setVisibility(GONE);
    }

    // SurfaceHolder.Callback2 interface.
    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (mNativeBlimpViewPtr == 0) return;
        nativeOnSurfaceChanged(mNativeBlimpViewPtr, format, width, height, holder.getSurface());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (mNativeBlimpViewPtr == 0) return;
        nativeOnSurfaceCreated(mNativeBlimpViewPtr);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mNativeBlimpViewPtr == 0) return;
        nativeOnSurfaceDestroyed(mNativeBlimpViewPtr);
    }

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {}

    // Native Methods
    private native long nativeInit(int physicalWidth, int physicalHeight, int displayWidth,
            int displayHeight, float dpToPixel);
    private native void nativeDestroy(long nativeBlimpView);
    private native void nativeSetNeedsComposite(long nativeBlimpView);
    private native void nativeOnSurfaceChanged(
            long nativeBlimpView, int format, int width, int height, Surface surface);
    private native void nativeOnSurfaceCreated(long nativeBlimpView);
    private native void nativeOnSurfaceDestroyed(long nativeBlimpView);
    private native void nativeSetVisibility(long nativeBlimpView, boolean visible);
}
