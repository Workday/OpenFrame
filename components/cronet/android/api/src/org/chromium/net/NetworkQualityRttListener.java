// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

/**
 * Interface to watch for observations of various round trip times (RTTs) at
 * various layers of the network stack. These include RTT estimates by QUIC
 * and TCP, as well as the time between when a URL request is sent and when
 * the first byte of the response is received.
 * @deprecated not really deprecated but hidden for now as it's a prototype.
 */
@SuppressWarnings("DepAnn")
public interface NetworkQualityRttListener {
    /**
     * Reports a new round trip time observation.
     * @param rttMs the round trip time in milliseconds.
     * @param whenMs milliseconds since the Epoch (January 1st 1970, 00:00:00.000).
     * @param source the observation source from {@link NetworkQualityObservationSource}.
     */
    public void onRttObservation(int rttMs, long whenMs, int source);
}