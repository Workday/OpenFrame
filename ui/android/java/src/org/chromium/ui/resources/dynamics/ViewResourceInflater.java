// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.resources.dynamics;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;

/**
 * ViewResourceInflater is a utility class that facilitates using an Android View as a dynamic
 * resource, which can be later used as a compositor layer. This class assumes that the View
 * is defined declaratively, using a XML Layout file, and that the View that is going to be
 * inflated is the single top-level View of the layout (its root).
 *
 * By default, the View is inflated without being attached to the hierarchy, which allows
 * subclasses to read/modify the View "offscreen", via the method {@link #onFinishInflate()}.
 * Only when a new snapshot of the View is required, which happens when the method
 * {@link #invalidate()} is called, and the View is automatically detached from the
 * hierarchy after the snapshot is captured.
 *
 * There's also an option to not attach to the hierarchy at all, by overriding the method
 * {@link #shouldAttachView()} and making it return false (the default is yes). In this case
 * the changes to the View will always be "offscreen". By default, an unspecified value of
 * {@link View.MeasureSpec} will de used to determine the width and height of the View.
 * It's possible to specify custom size constraints by overriding the methods
 * {@link #getWidthMeasureSpec()} and {@link #getHeightMeasureSpec()}.
 */
public class ViewResourceInflater {

    /**
     * The id of the XML Layout that describes the View.
     */
    private int mLayoutId;

    /**
     * The id of the View being inflated, which must be the root of the given Layout.
     */
    private int mViewId;

    /**
     * The Context used to inflate the View.
     */
    private Context mContext;

    /**
     * The ViewGroup container used to inflate the View.
     */
    private ViewGroup mContainer;

    /**
     * The DynamicResourceLoader used to manage resources generated dynamically.
     */
    private DynamicResourceLoader mResourceLoader;

    /**
     * The ViewResourceAdapter used to capture snapshots of the View.
     */
    private ViewResourceAdapter mResourceAdapter;

    /**
     * The inflated View.
     */
    private View mView;

    /**
     * Whether the View is invalided.
     */
    private boolean mIsInvalidated;

    /**
     * Whether the View is attached to the hierarchy.
     */
    private boolean mIsAttached;

    /**
     * The ViewInflaterOnDrawListener used to track changes in the View when attached.
     */
    private ViewInflaterOnDrawListener mOnDrawListener;

    /**
     * The invalid ID.
     */
    private static final int INVALID_ID = -1;

    /**
     * @param layoutId          The XML Layout that declares the View.
     * @param viewId            The id of the root View of the Layout.
     * @param context           The Android Context used to inflate the View.
     * @param container         The container View used to inflate the View.
     * @param resourceLoader    The resource loader that will handle the snapshot capturing.
     */
    public ViewResourceInflater(int layoutId,
                                int viewId,
                                Context context,
                                ViewGroup container,
                                DynamicResourceLoader resourceLoader) {
        mLayoutId = layoutId;
        mViewId = viewId;
        mContext = context;
        mContainer = container;
        mResourceLoader = resourceLoader;
    }

    /**
     * Inflate the layout.
     */
    public void inflate() {
        if (mView != null) return;

        // Inflate the View without attaching to hierarchy (attachToRoot param is false).
        mView = LayoutInflater.from(mContext).inflate(mLayoutId, mContainer, false);

        // Make sure the View we just inflated is the right one.
        assert mView.getId() == mViewId;

        // Allow subclasses to access/modify the View before it's attached
        // to the hierarchy (if allowed) or snapshots are captured.
        onFinishInflate();

        registerResource();
    }

    /**
     * Invalidate the inflated View, causing a snapshot of the View to be captured.
     */
    public void invalidate() {
        // View must be inflated at this point. If it's not, do it now.
        if (mView == null) {
            inflate();
        }

        mIsInvalidated = true;

        // If the View is already attached, we don't need to do anything because the
        // snapshot will be captured automatically when the View is drawn.
        if (!mIsAttached) {
            if (shouldAttachView()) {
                // TODO(pedrosimonetti): investigate if complex views can be rendered offline.
                // NOTE(pedrosimonetti): it seems that complex views don't get rendered
                // properly if not attached to the hierarchy. The problem seem to be related
                // to the use of the property "layout_gravity: end", possibly in combination
                // of other things like elastic views (layout_weight: 1) and/or fading edges.
                attachView();
            } else {
                // When the View is not attached, we need to manually layout the View
                // and invalidate the resource in order to capture a new snapshot.
                layout();
                invalidateResource();
            }
        }
    }

    /**
     * Destroy the instance.
     */
    public void destroy() {
        if (mView == null) return;

        unregisterResource();

        detachView();
        mView = null;

        mLayoutId = INVALID_ID;
        mViewId = INVALID_ID;

        mContext = null;
        mContainer = null;
        mResourceLoader = null;
    }

    /**
     * @return The measured width of the inflated View.
     */
    public int getMeasuredWidth() {
        // View must be inflated at this point.
        assert mView != null;

        return mView.getMeasuredWidth();
    }

    /**
     * @return The measured height of the inflated View.
     */
    public int getMeasuredHeight() {
        // View must be inflated at this point.
        assert mView != null;

        return mView.getMeasuredHeight();
    }

    /**
     * @return The id of View, which is used as an identifier for the resource loader.
     */
    public int getViewId() {
        return mViewId;
    }

    /**
     * The callback called after inflating the View, allowing subclasses to access/modify
     * the View before it's attached to the hierarchy (if allowed) or snapshots are captured.
     */
    protected void onFinishInflate() {}

    /**
     * NOTE(pedrosimonetti): Complex views don't fully work when not attached to the hierarchy.
     * @return Whether the View should be attached to the hierarchy after being inflated.
     *         Subclasses should override this method to change the default behavior.
     */
    protected boolean shouldAttachView() {
        return true;
    }

    /**
     * @return Whether the View should be detached from the hierarchy after being captured.
     *         Subclasses should override this method to change the default behavior.
     */
    protected boolean shouldDetachViewAfterCapturing() {
        return true;
    }

    /**
     * @return The MeasureSpec used for calculating the width of the offscreen View.
     *         Subclasses should override this method to specify measurements.
     *         By default, this method returns an unspecified MeasureSpec.
     */
    protected int getWidthMeasureSpec() {
        return getUnspecifiedMeasureSpec();
    }

    /**
     * @return The MeasureSpec used for calculating the height of the offscreen View.
     *         Subclasses should override this method to specify measurements.
     *         By default, this method returns an unspecified MeasureSpec.
     */
    protected int getHeightMeasureSpec() {
        return getUnspecifiedMeasureSpec();
    }

    /**
     * @return The View resource.
     */
    protected View getView() {
        return mView;
    }

    /**
     * Attach the View to the hierarchy.
     */
    private void attachView() {
        if (!mIsAttached) {
            assert mView.getParent() == null;
            mContainer.addView(mView);
            mIsAttached = true;

            if (mOnDrawListener == null) {
                // Add a draw listener. For now on, changes in the View will cause a
                // new snapshot to be captured, if the ViewResourceInflater was invalidated.
                mOnDrawListener = new ViewInflaterOnDrawListener();
                mView.getViewTreeObserver().addOnDrawListener(mOnDrawListener);
            }
        }
    }

    /**
     * Detach the View from the hierarchy.
     */
    private void detachView() {
        if (mIsAttached) {
            if (mOnDrawListener != null) {
                mView.getViewTreeObserver().removeOnDrawListener(mOnDrawListener);
                mOnDrawListener = null;
            }

            assert mView.getParent() != null;
            mContainer.removeView(mView);
            mIsAttached = false;
        }
    }

    /**
     * Layout the View. This is to be used when the View is not attached to the hierarchy.
     */
    private void layout() {
        // View must be inflated at this point.
        assert mView != null;

        mView.measure(getWidthMeasureSpec(), getHeightMeasureSpec());
        mView.layout(0, 0, getMeasuredWidth(), getMeasuredHeight());
    }

    /**
     * @return An unspecified MeasureSpec value.
     */
    private int getUnspecifiedMeasureSpec() {
        return View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
    }

    /**
     * Register the resource and creates an adapter for it.
     */
    private void registerResource() {
        if (mResourceAdapter == null) {
            mResourceAdapter = new ViewInflaterAdapter(mView.findViewById(mViewId));
        }

        if (mResourceLoader != null) {
            mResourceLoader.registerResource(mViewId, mResourceAdapter);
        }
    }

    /**
     * Unregister the resource and destroys the adapter.
     */
    private void unregisterResource() {
        if (mResourceLoader != null) {
            mResourceLoader.unregisterResource(mViewId);
        }

        mResourceAdapter = null;
    }

    /**
     * Invalidate the resource, which will cause a new snapshot to be captured.
     */
    private void invalidateResource() {
        if (mIsInvalidated && mView != null && mResourceAdapter != null) {
            mIsInvalidated = false;
            mResourceAdapter.invalidate(null);
        }
    }

    /**
     * A custom {@link ViewResourceAdapter} that calls the method {@link #onCaptureEnd()}.
     */
    private class ViewInflaterAdapter extends ViewResourceAdapter {
        public ViewInflaterAdapter(View view) {
            super(view);
        }

        @Override
        protected void onCaptureEnd() {
            ViewResourceInflater.this.onCaptureEnd();
        }
    }

    /**
     * Called when a snapshot is captured.
     */
    private void onCaptureEnd() {
        if (shouldDetachViewAfterCapturing()) {
            detachView();
        }
    }

    /**
     * A custom {@link ViewTreeObserver.OnDrawListener} that calls the method {@link #onDraw()}.
     */
    private class ViewInflaterOnDrawListener implements ViewTreeObserver.OnDrawListener {
        @Override
        public void onDraw() {
            ViewResourceInflater.this.onDraw();
        }
    }

    /**
     * Called when the View is drawn,
     */
    private void onDraw() {
        invalidateResource();
    }
}
