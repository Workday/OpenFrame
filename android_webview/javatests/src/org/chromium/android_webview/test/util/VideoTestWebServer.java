// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test.util;

import android.content.Context;
import android.util.Pair;

import org.chromium.net.test.util.TestWebServer;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/**
 * This class is a WebServer provide video data.
 */
public class VideoTestWebServer {

    public static final String ONE_PIXEL_ONE_FRAME_WEBM_FILENAME = "one_pixel_one_frame.webm";
    public static final String ONE_PIXEL_ONE_FRAME_WEBM_BASE64 =
            "GkXfo0AgQoaBAUL3gQFC8oEEQvOBCEKCQAR3ZWJtQoeBAkKFgQIYU4BnQN8VSalmQCgq17FAAw9C" +
            "QE2AQAZ3aGFtbXlXQUAGd2hhbW15RIlACECPQAAAAAAAFlSua0AxrkAu14EBY8WBAZyBACK1nEAD" +
            "dW5khkAFVl9WUDglhohAA1ZQOIOBAeBABrCBlrqBlh9DtnVAdOeBAKNAboEAAIDyCACdASqWAJYA" +
            "Pk0ci0WD+IBAAJiWlu4XdQTSq2H4MW0+sMO0gz8HMRe+0jRo0aNGjRo0aNGjRo0aNGjRo0aNGjRo" +
            "0aNGjRo0aNGjRo0VAAD+/729RWRzH4mOZ9/O8Dl319afX4gsgAAA";

    private String mOnePixelOneFrameWebmURL;
    private String mFullScreenVideoTestURL;
    private TestWebServer mTestWebServer;

    public VideoTestWebServer(Context context) throws Exception {
        mTestWebServer = new TestWebServer(false);
        List<Pair<String, String>> headers = getWebmHeaders(true);
        mOnePixelOneFrameWebmURL = mTestWebServer.setResponseBase64("/" +
                ONE_PIXEL_ONE_FRAME_WEBM_FILENAME,
                ONE_PIXEL_ONE_FRAME_WEBM_BASE64, headers);
        initFullScreenVideoTest(context);
    }

    /**
     * @return the mOnePixelOneFrameWebmURL
     */
    public String getOnePixelOneFrameWebmURL() {
        return mOnePixelOneFrameWebmURL;
    }

    public String getFullScreenVideoTestURL() {
        return mFullScreenVideoTestURL;
    }

    public TestWebServer getTestWebServer() {
        return mTestWebServer;
    }

    private static List<Pair<String, String>> getWebmHeaders(boolean disableCache) {
        return CommonResources.getContentTypeAndCacheHeaders("video/webm", disableCache);
    }

    private static List<Pair<String, String>> getHTMLHeaders(boolean disableCache) {
        return CommonResources.getContentTypeAndCacheHeaders("text/html", disableCache);
    }

    private void initFullScreenVideoTest(Context context) throws IOException {
        final String FULL_SCREEN_VIDEO_PATH = "full_screen_video_test.html";
        String data = loadAssetData(context, FULL_SCREEN_VIDEO_PATH);
        mFullScreenVideoTestURL = mTestWebServer.setResponse("/" + FULL_SCREEN_VIDEO_PATH,
                data.replace("VIDEO_FILE_URL", getOnePixelOneFrameWebmURL()),
                getHTMLHeaders(false));
    }

    private String loadAssetData(Context context, String asset) throws IOException {
        InputStream in = context.getAssets().open(asset);
        ByteArrayOutputStream os = new ByteArrayOutputStream();
        int buflen = 128;
        byte[] buffer = new byte[buflen];
        int len = in.read(buffer, 0, buflen);
        while (len != -1) {
            os.write(buffer, 0, len);
            if (len < buflen) break;
            len = in.read(buffer, 0, buflen);
        }
        return os.toString();
  }
}
