// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextualsearch;

import android.content.Context;
import android.text.TextUtils;

import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchSelectionController.SelectionType;
import org.chromium.chrome.browser.preferences.ChromePreferenceManager;
import org.chromium.chrome.browser.preferences.NetworkPredictionOptions;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.content.browser.ContentViewCore;

import java.net.URL;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

import javax.annotation.Nullable;


/**
 * Handles policy decisions for the {@code ContextualSearchManager}.
 */
class ContextualSearchPolicy {
    private static final Pattern CONTAINS_WHITESPACE_PATTERN = Pattern.compile("\\s");
    private static final int REMAINING_NOT_APPLICABLE = -1;
    private static final int ONE_DAY_IN_MILLIS = 24 * 60 * 60 * 1000;
    private static final int TAP_TRIGGERED_PROMO_LIMIT = 50;
    private static final int TAP_RESOLVE_LIMIT_FOR_DECIDED = 50;
    private static final int TAP_PREFETCH_LIMIT_FOR_DECIDED = 50;
    private static final int TAP_RESOLVE_LIMIT_FOR_UNDECIDED = 20;
    private static final int TAP_PREFETCH_LIMIT_FOR_UNDECIDED = 20;

    private static ContextualSearchPolicy sInstance;

    private final ChromePreferenceManager mPreferenceManager;

    // Members used only for testing purposes.
    private boolean mDidOverrideDecidedStateForTesting;
    private boolean mDecidedStateForTesting;
    private boolean mDidResetCounters;
    private Integer mTapTriggeredPromoLimitForTesting;
    private Integer mTapResolveLimitForDecided;
    private Integer mTapPrefetchLimitForDecided;
    private Integer mTapResolveLimitForUndecided;
    private Integer mTapPrefetchLimitForUndecided;

    public static ContextualSearchPolicy getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new ContextualSearchPolicy(context);
        }
        return sInstance;
    }

    /**
     * Private constructor -- use {@link #getInstance} to get the singleton instance.
     * @param context The Android Context.
     */
    private ContextualSearchPolicy(Context context) {
        mPreferenceManager = ChromePreferenceManager.getInstance(context);
    }

    // TODO(donnd): Consider adding a test-only constructor that uses dependency injection of a
    // preference manager and PrefServiceBridge.  Currently this is not possible because the
    // PrefServiceBridge is final.

    /**
     * @return The number of additional times to show the promo on tap, 0 if it should not be shown,
     *         or a negative value if the counter has been disabled or the user has accepted
     *         the promo.
     */
    int getPromoTapsRemaining() {
        if (!isUserUndecided()) return REMAINING_NOT_APPLICABLE;

        // Return a non-negative value if opt-out promo counter is enabled, and there's a limit.
        DisableablePromoTapCounter counter = getPromoTapCounter();
        if (counter.isEnabled()) {
            int limit = getPromoTapTriggeredLimit();
            if (limit >= 0) return Math.max(0, limit - counter.getCount());
        }

        return REMAINING_NOT_APPLICABLE;
    }

    private int getPromoTapTriggeredLimit() {
        if (mTapTriggeredPromoLimitForTesting != null) {
            return mTapTriggeredPromoLimitForTesting.intValue();
        }
        return TAP_TRIGGERED_PROMO_LIMIT;
    }

    /**
     * @return the {@link DisableablePromoTapCounter}.
     */
    DisableablePromoTapCounter getPromoTapCounter() {
        return DisableablePromoTapCounter.getInstance(mPreferenceManager);
    }

    /**
     * @return Whether a Tap gesture is currently supported as a trigger for the feature.
     */
    boolean isTapSupported() {
        if (!isUserUndecided()) return true;
        return getPromoTapsRemaining() != 0;
    }

    /**
     * @return whether or not the Contextual Search Result should be preloaded before the user
     *         explicitly interacts with the feature.
     */
    boolean shouldPrefetchSearchResult(boolean isTapTriggered) {
        if (PrefServiceBridge.getInstance().getNetworkPredictionOptions()
                == NetworkPredictionOptions.NETWORK_PREDICTION_NEVER) {
            return false;
        }

        if (isTapPrefetchBeyondTheLimit()) return false;

        // If we're not resolving the tap due to the tap limit, we should not preload either.
        if (isTapResolveBeyondTheLimit()) return false;

        // We never preload on long-press so users can cut & paste without hitting the servers.
        return isTapTriggered;
    }

    /**
     * Returns whether the previous tap (the tap last counted) should resolve.
     * @return Whether the previous tap should resolve.
     */
    boolean shouldPreviousTapResolve(@Nullable URL url) {
        if (isTapResolveBeyondTheLimit()) {
            return false;
        }

        if (isPromoAvailable()) {
            return isBasePageHTTP(url);
        }

        return true;
    }

    /**
     * Returns whether surrounding context can be accessed by other systems or not.
     * @baseContentViewUrl The URL of the base page.
     * @return Whether surroundings are available.
     */
    boolean canSendSurroundings(@Nullable URL baseContentViewUrl) {
        if (isUserUndecided()) return false;

        if (isPromoAvailable()) {
            return isBasePageHTTP(baseContentViewUrl);
        }

        return true;
    }

    /**
     * @return Whether the Opt-out promo is available to be shown in any panel.
     */
    boolean isPromoAvailable() {
        return isUserUndecided();
    }

    /**
     * @param controller The {@link ContextualSearchSelectionController} instance.
     * @return Whether the Peek promo is available to be shown above the Search Bar.
     */
    public boolean isPeekPromoAvailable(ContextualSearchSelectionController controller) {
        // Allow Promo to be forcefully enabled for testing.
        if (ContextualSearchFieldTrial.isPeekPromoForced()) return true;

        // Enabled by Finch.
        if (!ContextualSearchFieldTrial.isPeekPromoEnabled()) return false;

        return isPeekPromoConditionSatisfied(controller);
    }

    /**
     * @param controller The {@link ContextualSearchSelectionController} instance.
     * @return Whether the condition to show the Peek promo is satisfied.
     */
    public boolean isPeekPromoConditionSatisfied(ContextualSearchSelectionController controller) {
        // Check for several conditions to determine whether the Peek Promo can be shown.

        // 1) If the Panel was never opened.
        if (getPromoOpenCount() > 0) return false;

        // 2) User has not opted in.
        if (!isUserUndecided()) return false;

        // 3) Selection was caused by a long press.
        if (controller.getSelectionType() != SelectionType.LONG_PRESS) return false;

        // 4) Promo was not shown more than the maximum number of times defined by Finch.
        final int maxShowCount = ContextualSearchFieldTrial.getPeekPromoMaxShowCount();
        final int peekPromoShowCount = mPreferenceManager.getContextualSearchPeekPromoShowCount();
        if (peekPromoShowCount >= maxShowCount) return false;

        // 5) Only then, show the promo.
        return true;
    }

    /**
     * Register that the Peek Promo was seen.
     */
    public void registerPeekPromoSeen() {
        final int peekPromoShowCount = mPreferenceManager.getContextualSearchPeekPromoShowCount();
        mPreferenceManager.setContextualSearchPeekPromoShowCount(peekPromoShowCount + 1);
    }

    /**
     * Logs metrics related to the Peek Promo.
     * @param wasPromoSeen Whether the Peek Promo was seen.
     * @param wouldHaveShownPromo Whether the Promo would have shown.
     */
    public void logPeekPromoMetrics(boolean wasPromoSeen, boolean wouldHaveShownPromo) {
        final boolean hasOpenedPanel = getPromoOpenCount() > 0;
        ContextualSearchUma.logPeekPromoOutcome(wasPromoSeen, wouldHaveShownPromo, hasOpenedPanel);

        if (wasPromoSeen) {
            final int showCount = mPreferenceManager.getContextualSearchPeekPromoShowCount();
            ContextualSearchUma.logPeekPromoShowCount(showCount, hasOpenedPanel);
        }
    }

    /**
     * Registers that a tap has taken place by incrementing tap-tracking counters.
     */
    void registerTap() {
        if (isPromoAvailable()) {
            DisableablePromoTapCounter promoTapCounter = getPromoTapCounter();
            // Bump the counter only when it is still enabled.
            if (promoTapCounter.isEnabled()) {
                promoTapCounter.increment();
            }
        }
        int tapsSinceOpen = mPreferenceManager.getContextualSearchTapCount();
        mPreferenceManager.setContextualSearchTapCount(++tapsSinceOpen);
        if (isUserUndecided()) {
            ContextualSearchUma.logTapsSinceOpenForUndecided(tapsSinceOpen);
        } else {
            ContextualSearchUma.logTapsSinceOpenForDecided(tapsSinceOpen);
        }
    }

    /**
     * Updates all the counters to account for an open-action on the panel.
     */
    void updateCountersForOpen() {
        // Always completely reset the tap counter, since it just counts taps
        // since the last open.
        mPreferenceManager.setContextualSearchTapCount(0);

        // Disable the "promo tap" counter, but only if we're using the Opt-out onboarding.
        // For Opt-in, we never disable the promo tap counter.
        if (isPromoAvailable()) {
            getPromoTapCounter().disable();

            // Bump the total-promo-opens counter.
            int count = mPreferenceManager.getContextualSearchPromoOpenCount();
            mPreferenceManager.setContextualSearchPromoOpenCount(++count);
            ContextualSearchUma.logPromoOpenCount(count);
        }
    }

    /**
     * @return Whether a verbatim request should be made for the given base page, assuming there
     *         is no exiting request.
     */
    boolean shouldCreateVerbatimRequest(ContextualSearchSelectionController controller,
            @Nullable URL basePageUrl) {
        // TODO(donnd): refactor to make the controller a member of this class?
        return (controller.getSelectedText() != null
                && (controller.getSelectionType() == SelectionType.LONG_PRESS
                || (controller.getSelectionType() == SelectionType.TAP
                        && !shouldPreviousTapResolve(basePageUrl))));
    }

    /**
     * Determines whether an error from a search term resolution request should
     * be shown to the user, or not.
     */
    boolean shouldShowErrorCodeInBar() {
        // Builds with lots of real users should not see raw error codes.
        return !(ChromeVersionInfo.isStableBuild() || ChromeVersionInfo.isBetaBuild());
    }

    /**
     * Logs the current user's state, including preference, tap and open counters, etc.
     */
    void logCurrentState(@Nullable ContentViewCore cvc) {
        if (cvc == null || !ContextualSearchFieldTrial.isEnabled(cvc.getContext())) {
            return;
        }

        ContextualSearchUma.logPreferenceState();

        // Log the number of promo taps remaining.
        int promoTapsRemaining = getPromoTapsRemaining();
        if (promoTapsRemaining >= 0) ContextualSearchUma.logPromoTapsRemaining(promoTapsRemaining);

        // Also log the total number of taps before opening the promo, even for those
        // that are no longer tap limited. That way we'll know the distribution of the
        // number of taps needed before opening the promo.
        DisableablePromoTapCounter promoTapCounter = getPromoTapCounter();
        boolean wasOpened = !promoTapCounter.isEnabled();
        int count = promoTapCounter.getCount();
        if (wasOpened) {
            ContextualSearchUma.logPromoTapsBeforeFirstOpen(count);
        } else {
            ContextualSearchUma.logPromoTapsForNeverOpened(count);
        }
    }

    /**
     * Logs details about the Search Term Resolution.
     * Should only be called when a search term has been resolved.
     * @param searchTerm The Resolved Search Term.
     * @param basePageUrl The URL of the base page.
     */
    void logSearchTermResolutionDetails(String searchTerm, @Nullable URL basePageUrl) {
        // Only log for decided users so the data reflect fully-enabled behavior.
        // Otherwise we'll get skewed data; more HTTP pages than HTTPS (since those don't resolve),
        // and it's also possible that public pages, e.g. news, have more searches for multi-word
        // entities like people.
        if (!isUserUndecided()) {
            ContextualSearchUma.logBasePageProtocol(isBasePageHTTP(basePageUrl));
            boolean isSingleWord = !CONTAINS_WHITESPACE_PATTERN.matcher(searchTerm.trim()).find();
            ContextualSearchUma.logSearchTermResolvedWords(isSingleWord);
        }
    }

    /**
     * Whether sending the URL of the base page to the server may be done for policy reasons.
     * NOTE: There may be additional privacy reasons why the base page URL should not be sent.
     * TODO(donnd): Update this API to definitively determine if it's OK to send the URL,
     * by merging the checks in the native contextual_search_delegate here.
     * @return {@code true} if the URL may be sent for policy reasons.
     *         Note that a return value of {@code true} may still require additional checks
     *         to see if all privacy-related conditions are met to send the base page URL.
     */
    boolean maySendBasePageUrl() {
        return !isUserUndecided();
    }

    /**
     * The search provider icon is animated every time on long press if the user has never opened
     * the panel before and once a day on tap.
     *
     * @param selectionType The type of selection made by the user.
     * @param isShowing Whether the panel is showing.
     * @return Whether the search provider icon should be animated.
     */
    boolean shouldAnimateSearchProviderIcon(SelectionType selectionType, boolean isShowing) {
        if (isShowing) {
            return false;
        }

        if (selectionType == SelectionType.TAP) {
            long currentTimeMillis = System.currentTimeMillis();
            long lastAnimatedTimeMillis =
                    mPreferenceManager.getContextualSearchLastAnimationTime();
            if (Math.abs(currentTimeMillis - lastAnimatedTimeMillis) > ONE_DAY_IN_MILLIS) {
                mPreferenceManager.setContextualSearchLastAnimationTime(currentTimeMillis);
                return true;
            } else {
                return false;
            }
        } else if (selectionType == SelectionType.LONG_PRESS) {
            // If the panel has never been opened before, getPromoOpenCount() will be 0.
            // Once the panel has been opened, regardless of whether or not the user has opted-in or
            // opted-out, the promo open count will be greater than zero.
            return isUserUndecided() && getPromoOpenCount() == 0;
        }

        return false;
    }

    // --------------------------------------------------------------------------------------------
    // Testing support.
    // --------------------------------------------------------------------------------------------

    /**
     * Resets all policy counters.
     */
    @VisibleForTesting
    void resetCounters() {
        updateCountersForOpen();

        mPreferenceManager.setContextualSearchPromoOpenCount(0);
        mDidResetCounters = true;
    }

    /**
     * Overrides the decided/undecided state for the user preference.
     * @param decidedState Whether the user has decided or not.
     */
    @VisibleForTesting
    void overrideDecidedStateForTesting(boolean decidedState) {
        mDidOverrideDecidedStateForTesting = true;
        mDecidedStateForTesting = decidedState;
    }

    /**
     * @return Whether counters have been reset yet (by resetCounters) or not.
     */
    @VisibleForTesting
    boolean didResetCounters() {
        return mDidResetCounters;
    }

    /**
     * @return count of times the panel with the promo has been opened.
     */
    @VisibleForTesting
    int getPromoOpenCount() {
        return mPreferenceManager.getContextualSearchPromoOpenCount();
    }

    /**
     * @return The number of times the user has tapped since the last panel open.
     */
    @VisibleForTesting
    int getTapCount() {
        return mPreferenceManager.getContextualSearchTapCount();
    }

    /**
     * Determines whether translation is needed between the given languages.
     * @param sourceLanguage The source language code; language we're translating from.
     * @param targetLanguages A list of target language codes; languages we might translate to.
     * @return Whether translation is needed or not.
     */
    boolean needsTranslation(String sourceLanguage, List<String> targetLanguages) {
        // For now, we just look for a language match.
        for (String targetLanguage : targetLanguages) {
            if (TextUtils.equals(sourceLanguage, targetLanguage)) {
                return false;
            }
        }
        return true;
    }

    /**
     * Determines the best target language.
     */
    String bestTargetLanguage(List<String> targetLanguages) {
        // For now, we just return the first language, unless it's English
        // (due to over-usage).
        // TODO(donnd): Improve this logic. Determining the right language seems non-trivial.
        // E.g. If this language doesn't match the user's server preferences, they might see a page
        // in one language and the one box translation in another, which might be confusing.
        // Also this logic should only apply on Android, where English setup is over used.
        if (TextUtils.equals(targetLanguages.get(0), Locale.ENGLISH.getLanguage())
                && targetLanguages.size() > 1) {
            return targetLanguages.get(1);
        } else {
            return targetLanguages.get(0);
        }
    }

    /**
     * @return Whether forcing a translation Onebox is disabled.
     */
    boolean disableForceTranslationOnebox() {
        return ContextualSearchFieldTrial.disableForceTranslationOnebox();
    }

    /**
     * @return Whether forcing a translation Onebox based on auto-detection of the source language
     *         is disabled.
     */
    boolean disableAutoDetectTranslationOnebox() {
        if (disableForceTranslationOnebox()) return true;

        return ContextualSearchFieldTrial.disableAutoDetectTranslationOnebox();
    }

    /**
     * Sets the limit for the tap triggered promo.
     */
    @VisibleForTesting
    void setPromoTapTriggeredLimitForTesting(int limit) {
        mTapTriggeredPromoLimitForTesting = limit;
    }

    @VisibleForTesting
    void setTapResolveLimitForDecidedForTesting(int limit) {
        mTapResolveLimitForDecided = limit;
    }

    @VisibleForTesting
    void setTapPrefetchLimitForDecidedForTesting(int limit) {
        mTapPrefetchLimitForDecided = limit;
    }

    @VisibleForTesting
    void setTapPrefetchLimitForUndecidedForTesting(int limit) {
        mTapPrefetchLimitForUndecided = limit;
    }

    @VisibleForTesting
    void setTapResolveLimitForUndecidedForTesting(int limit) {
        mTapResolveLimitForUndecided = limit;
    }

    // --------------------------------------------------------------------------------------------
    // Private helpers.
    // --------------------------------------------------------------------------------------------

    /**
     * @return Whether a promo is needed because the user is still undecided
     *         on enabling or disabling the feature.
     */
    private boolean isUserUndecided() {
        // TODO(donnd) use dependency injection for the PrefServiceBridge instead!
        if (mDidOverrideDecidedStateForTesting) return !mDecidedStateForTesting;

        return PrefServiceBridge.getInstance().isContextualSearchUninitialized();
    }

    /**
     * @param url The URL of the base page.
     * @return Whether the given content view is for an HTTP page.
     */
    private boolean isBasePageHTTP(@Nullable URL url) {
        return url != null && "http".equals(url.getProtocol());
    }

    /**
     * @return Whether the tap resolve limit has been exceeded.
     */
    private boolean isTapResolveBeyondTheLimit() {
        return getTapCount() > getTapResolveLimit();
    }

    /**
     * @return Whether the tap resolve limit has been exceeded.
     */
    private boolean isTapPrefetchBeyondTheLimit() {
        return getTapCount() > getTapPrefetchLimit();
    }

    /**
     * @return The limit of the number of taps to prefetch.
     */
    private int getTapPrefetchLimit() {
        return isUserUndecided()
                ? getTapPrefetchLimitForUndecided()
                : getTapPrefetchLimitForDecided();
    }

    /**
     * @return The limit of the number of taps to resolve using search term resolution.
     */
    private int getTapResolveLimit() {
        return isUserUndecided()
                ? getTapResolveLimitForUndecided()
                : getTapResolveLimitForDecided();
    }

    private int getTapPrefetchLimitForDecided() {
        if (mTapPrefetchLimitForDecided != null) return mTapPrefetchLimitForDecided.intValue();
        return TAP_PREFETCH_LIMIT_FOR_DECIDED;
    }

    private int getTapResolveLimitForDecided() {
        if (mTapResolveLimitForDecided != null) return mTapResolveLimitForDecided.intValue();
        return TAP_RESOLVE_LIMIT_FOR_DECIDED;
    }

    private int getTapPrefetchLimitForUndecided() {
        if (mTapPrefetchLimitForUndecided != null) return mTapPrefetchLimitForUndecided.intValue();
        return TAP_PREFETCH_LIMIT_FOR_UNDECIDED;
    }

    private int getTapResolveLimitForUndecided() {
        if (mTapResolveLimitForUndecided != null) return mTapResolveLimitForUndecided.intValue();
        return TAP_RESOLVE_LIMIT_FOR_UNDECIDED;
    }
}
