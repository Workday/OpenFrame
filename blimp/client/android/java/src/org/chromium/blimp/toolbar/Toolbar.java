// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.blimp.toolbar;

import android.content.Context;
import android.graphics.Bitmap;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.view.View;
import android.widget.ImageButton;
import android.widget.LinearLayout;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import org.chromium.blimp.R;

/**
 * A {@link View} that visually represents the Blimp toolbar, which lets users issue navigation
 * commands and displays relevant navigation UI.
 */
@JNINamespace("blimp")
public class Toolbar extends LinearLayout implements UrlBar.UrlBarObserver,
        View.OnClickListener {
    private long mNativeToolbarPtr;

    private UrlBar mUrlBar;
    private ImageButton mReloadButton;

    /**
     * A URL to load when this object is initialized.  This handles the case where there is a URL
     * to load before native is ready to receive any URL.
     * */
    private String mUrlToLoad;

    /**
     * Builds a new {@link Toolbar}.
     * @param context A {@link Context} instance.
     * @param attrs   An {@link AttributeSet} instance.
     */
    public Toolbar(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    /**
     * To be called when the native library is loaded so that this class can initialize its native
     * components.
     */
    public void initialize() {
        assert mNativeToolbarPtr == 0;

        mNativeToolbarPtr = nativeInit();
        sendUrlTextInternal(mUrlToLoad);
    }

    /**
     * To be called when this class should be torn down.  This {@link View} should not be used after
     * this.
     */
    public void destroy() {
        if (mNativeToolbarPtr != 0) {
            nativeDestroy(mNativeToolbarPtr);
            mNativeToolbarPtr = 0;
        }
    }

    /**
     * Loads {@code text} as if it had been typed by the user.  Useful for specifically loading
     * startup URLs or testing.
     * @param text The URL or text to load.
     */
    public void loadUrl(String text) {
        mUrlBar.setText(text);
        sendUrlTextInternal(text);
    }

    /**
     * To be called when the user triggers a back navigation action.
     * @return Whether or not the back event was consumed.
     */
    public boolean onBackPressed() {
        if (mNativeToolbarPtr == 0) return false;
        return nativeOnBackPressed(mNativeToolbarPtr);
    }

    // View overrides.
    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mUrlBar = (UrlBar) findViewById(R.id.toolbar_url_bar);
        mUrlBar.addUrlBarObserver(this);

        mReloadButton = (ImageButton) findViewById(R.id.toolbar_reload_btn);
        mReloadButton.setOnClickListener(this);
    }

    // UrlBar.UrlBarObserver interface.
    @Override
    public void onNewTextEntered(String text) {
        sendUrlTextInternal(text);
    }

    // View.OnClickListener interface.
    @Override
    public void onClick(View view) {
        if (mNativeToolbarPtr == 0) return;
        if (view == mReloadButton) nativeOnReloadPressed(mNativeToolbarPtr);
    }

    private void sendUrlTextInternal(String text) {
        mUrlToLoad = null;
        if (TextUtils.isEmpty(text)) return;

        if (mNativeToolbarPtr == 0) {
            mUrlToLoad = text;
            return;
        }

        nativeOnUrlTextEntered(mNativeToolbarPtr, text);
    }

    // Methods that are called by native via JNI.
    @CalledByNative
    private void onNavigationStateChanged(String url, Bitmap favicon, String title) {
        if (url != null) mUrlBar.setText(url);
        // TODO(dtrainor): Add a UI for favicon and title data and tie it in here.
    }

    private native long nativeInit();
    private native void nativeDestroy(long nativeToolbar);
    private native void nativeOnUrlTextEntered(long nativeToolbar, String text);
    private native void nativeOnReloadPressed(long nativeToolbar);
    private native boolean nativeOnBackPressed(long nativeToolbar);
}
