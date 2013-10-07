// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.app.Instrumentation;
import android.content.Context;
import android.test.ActivityInstrumentationTestCase2;

import org.chromium.android_webview.AwBrowserContext;
import org.chromium.android_webview.AwBrowserProcess;
import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.AwContentsClient;
import org.chromium.android_webview.AwLayoutSizer;
import org.chromium.android_webview.AwSettings;
import org.chromium.android_webview.test.util.JSUtils;
import org.chromium.base.test.util.InMemorySharedPreferences;
import org.chromium.content.browser.ContentSettings;
import org.chromium.content.browser.LoadUrlParams;
import org.chromium.content.browser.test.util.CallbackHelper;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * A base class for android_webview tests.
 */
public class AwTestBase
        extends ActivityInstrumentationTestCase2<AwTestRunnerActivity> {
    protected static final int WAIT_TIMEOUT_SECONDS = 15;
    protected static final int CHECK_INTERVAL = 100;

    public AwTestBase() {
        super(AwTestRunnerActivity.class);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        final Context context = getActivity();
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                AwBrowserProcess.start(context);
             }
        });
    }

    /**
     * Runs a {@link Callable} on the main thread, blocking until it is
     * complete, and returns the result. Calls
     * {@link Instrumentation#waitForIdleSync()} first to help avoid certain
     * race conditions.
     *
     * @param <R> Type of result to return
     */
    public <R> R runTestOnUiThreadAndGetResult(Callable<R> callable)
            throws Exception {
        FutureTask<R> task = new FutureTask<R>(callable);
        getInstrumentation().waitForIdleSync();
        getInstrumentation().runOnMainSync(task);
        return task.get();
    }

    protected void enableJavaScriptOnUiThread(final AwContents awContents) {
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                awContents.getSettings().setJavaScriptEnabled(true);
            }
        });
    }

    /**
     * Loads url on the UI thread and blocks until onPageFinished is called.
     */
    protected void loadUrlSync(final AwContents awContents,
                               CallbackHelper onPageFinishedHelper,
                               final String url) throws Exception {
        int currentCallCount = onPageFinishedHelper.getCallCount();
        loadUrlAsync(awContents, url);
        onPageFinishedHelper.waitForCallback(currentCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
    }

    protected void loadUrlSyncAndExpectError(final AwContents awContents,
            CallbackHelper onPageFinishedHelper,
            CallbackHelper onReceivedErrorHelper,
            final String url) throws Exception {
        int onErrorCallCount = onReceivedErrorHelper.getCallCount();
        int onFinishedCallCount = onPageFinishedHelper.getCallCount();
        loadUrlAsync(awContents, url);
        onReceivedErrorHelper.waitForCallback(onErrorCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
        onPageFinishedHelper.waitForCallback(onFinishedCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
    }

    /**
     * Loads url on the UI thread but does not block.
     */
    protected void loadUrlAsync(final AwContents awContents,
                                final String url) throws Exception {
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                awContents.loadUrl(new LoadUrlParams(url));
            }
        });
    }

    /**
     * Posts url on the UI thread and blocks until onPageFinished is called.
     */
    protected void postUrlSync(final AwContents awContents,
            CallbackHelper onPageFinishedHelper, final String url,
            byte[] postData) throws Exception {
        int currentCallCount = onPageFinishedHelper.getCallCount();
        postUrlAsync(awContents, url, postData);
        onPageFinishedHelper.waitForCallback(currentCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
    }

    /**
     * Loads url on the UI thread but does not block.
     */
    protected void postUrlAsync(final AwContents awContents,
            final String url, byte[] postData) throws Exception {
        class PostUrl implements Runnable {
            byte[] mPostData;
            public PostUrl(byte[] postData) {
                mPostData = postData;
            }
            @Override
            public void run() {
                awContents.loadUrl(LoadUrlParams.createLoadHttpPostParams(url,
                        mPostData));
            }
        }
        getInstrumentation().runOnMainSync(new PostUrl(postData));
    }

    /**
     * Loads data on the UI thread and blocks until onPageFinished is called.
     */
    protected void loadDataSync(final AwContents awContents,
                                CallbackHelper onPageFinishedHelper,
                                final String data, final String mimeType,
                                final boolean isBase64Encoded) throws Exception {
        int currentCallCount = onPageFinishedHelper.getCallCount();
        loadDataAsync(awContents, data, mimeType, isBase64Encoded);
        onPageFinishedHelper.waitForCallback(currentCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
    }

    protected void loadDataSyncWithCharset(final AwContents awContents,
                                           CallbackHelper onPageFinishedHelper,
                                           final String data, final String mimeType,
                                           final boolean isBase64Encoded, final String charset)
            throws Exception {
        int currentCallCount = onPageFinishedHelper.getCallCount();
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                awContents.loadUrl(LoadUrlParams.createLoadDataParams(
                        data, mimeType, isBase64Encoded, charset));
            }
        });
        onPageFinishedHelper.waitForCallback(currentCallCount, 1, WAIT_TIMEOUT_SECONDS,
                TimeUnit.SECONDS);
    }

    /**
     * Loads data on the UI thread but does not block.
     */
    protected void loadDataAsync(final AwContents awContents, final String data,
                                 final String mimeType, final boolean isBase64Encoded)
            throws Exception {
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                awContents.loadUrl(LoadUrlParams.createLoadDataParams(
                        data, mimeType, isBase64Encoded));
            }
        });
    }

    /**
     * Factory class used in creation of test AwContents instances.
     *
     * Test cases can provide subclass instances to the createAwTest* methods in order to create an
     * AwContents instance with injected test dependencies.
     */
    public static class TestDependencyFactory {
        public AwLayoutSizer createLayoutSizer() {
            return new AwLayoutSizer();
        }
        public AwTestContainerView createAwTestContainerView(AwTestRunnerActivity activity) {
            return new AwTestContainerView(activity);
        }
    }

    protected TestDependencyFactory createTestDependencyFactory() {
        return new TestDependencyFactory();
    }

    protected AwTestContainerView createAwTestContainerView(
            final AwContentsClient awContentsClient) {
        AwTestContainerView testContainerView = createDetachedAwTestContainerView(awContentsClient);
        getActivity().addView(testContainerView);
        testContainerView.requestFocus();
        return testContainerView;
    }

    // The browser context needs to be a process-wide singleton.
    private AwBrowserContext mBrowserContext =
            new AwBrowserContext(new InMemorySharedPreferences());

    protected AwTestContainerView createDetachedAwTestContainerView(
            final AwContentsClient awContentsClient) {
        final TestDependencyFactory testDependencyFactory = createTestDependencyFactory();
        final AwTestContainerView testContainerView =
            testDependencyFactory.createAwTestContainerView(getActivity());
        testContainerView.initialize(new AwContents(
                mBrowserContext, testContainerView, testContainerView.getInternalAccessDelegate(),
                awContentsClient, false, testDependencyFactory.createLayoutSizer()));
        return testContainerView;
    }

    protected AwTestContainerView createAwTestContainerViewOnMainSync(
            final AwContentsClient client) throws Exception {
        final AtomicReference<AwTestContainerView> testContainerView =
                new AtomicReference<AwTestContainerView>();
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                testContainerView.set(createAwTestContainerView(client));
            }
        });
        return testContainerView.get();
    }

    protected void destroyAwContentsOnMainSync(final AwContents awContents) {
        if (awContents == null) return;
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                awContents.destroy();
            }
        });
    }

    protected String getTitleOnUiThread(final AwContents awContents) throws Exception {
        return runTestOnUiThreadAndGetResult(new Callable<String>() {
            @Override
            public String call() throws Exception {
                return awContents.getContentViewCore().getTitle();
            }
        });
    }

    protected ContentSettings getContentSettingsOnUiThread(
            final AwContents awContents) throws Exception {
        return runTestOnUiThreadAndGetResult(new Callable<ContentSettings>() {
            @Override
            public ContentSettings call() throws Exception {
                return awContents.getContentViewCore().getContentSettings();
            }
        });
    }

    protected AwSettings getAwSettingsOnUiThread(
            final AwContents awContents) throws Exception {
        return runTestOnUiThreadAndGetResult(new Callable<AwSettings>() {
            @Override
            public AwSettings call() throws Exception {
                return awContents.getSettings();
            }
        });
    }

    /**
     * Executes the given snippet of JavaScript code within the given ContentView. Returns the
     * result of its execution in JSON format.
     */
    protected String executeJavaScriptAndWaitForResult(final AwContents awContents,
            TestAwContentsClient viewClient, final String code) throws Exception {
        return JSUtils.executeJavaScriptAndWaitForResult(this, awContents,
                viewClient.getOnEvaluateJavaScriptResultHelper(),
                code);
    }

    /**
     * Similar to CriteriaHelper.pollForCriteria but runs the callable on the UI thread.
     * Note that exceptions are treated as failure.
     */
    protected boolean pollOnUiThread(final Callable<Boolean> callable) throws Exception {
        return CriteriaHelper.pollForCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                try {
                    return runTestOnUiThreadAndGetResult(callable);
                } catch (Throwable e) {
                    return false;
                }
            }
        });
    }

    /**
     * Clears the resource cache. Note that the cache is per-application, so this will clear the
     * cache for all WebViews used.
     */
    protected void clearCacheOnUiThread(
            final AwContents awContents,
            final boolean includeDiskFiles) throws Exception {
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
              awContents.clearCache(includeDiskFiles);
            }
        });
    }
}
