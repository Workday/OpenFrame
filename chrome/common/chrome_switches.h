// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the shared command-line switches used by code in the Chrome
// directory that don't have anywhere more specific to go.

#ifndef CHROME_COMMON_CHROME_SWITCHES_H_
#define CHROME_COMMON_CHROME_SWITCHES_H_

#include "build/build_config.h"

// Don't add more switch files here. This is linked into some places like the
// installer where dependencies should be limited. Instead, have files
// directly include your switch file.

namespace switches {

// -----------------------------------------------------------------------------
// Can't find the switch you are looking for? Try looking in
// media/base/media_switches.cc or ui/gl/gl_switches.cc or one of the
// .cc files corresponding to the *_switches.h files included above
// instead.
// -----------------------------------------------------------------------------

// All switches in alphabetical order. The switches should be documented
// alongside the definition of their values in the .cc file.
extern const char kAllowCrossOriginAuthPrompt[];
extern const char kAllowFileAccess[];
extern const char kAllowHttpScreenCapture[];
extern const char kAllowInsecureLocalhost[];
extern const char kAllowOutdatedPlugins[];
extern const char kAllowRunningInsecureContent[];
extern const char kAlternativeServiceProbabilityThreshold[];
extern const char kAlwaysAuthorizePlugins[];
extern const char kAppId[];
extern const char kApp[];
extern const char kAppsGalleryDownloadURL[];
extern const char kAppsGalleryURL[];
extern const char kAppsGalleryUpdateURL[];
extern const char kAppModeAuthCode[];
extern const char kAppModeOAuth2Token[];
extern const char kAuthExtensionPath[];
extern const char kAuthServerWhitelist[];
extern const char kAutoSelectDesktopCaptureSource[];
extern const char kBypassAppBannerEngagementChecks[];
extern const char kAutomationClientChannelID[];
extern const char kCertificateTransparencyLog[];
extern const char kCheckForUpdateIntervalSec[];
extern const char kChromeFrame[];
extern const char kChromeVersion[];
extern const char kCipherSuiteBlacklist[];
extern const char kCloudPrintFile[];
extern const char kCloudPrintJobTitle[];
extern const char kCloudPrintFileType[];
extern const char kCloudPrintPrintTicket[];
extern const char kCloudPrintSetupProxy[];
extern const char kCrashOnHangThreads[];
extern const char kCreateBrowserOnStartupForTests[];
extern const char kDebugEnableFrameToggle[];
extern const char kDebugPackedApps[];
extern const char kDiagnostics[];
extern const char kDiagnosticsFormat[];
extern const char kDiagnosticsRecovery[];
extern const char kDisableAboutInSettings[];
extern const char kDisableAddToShelf[];
extern const char kDisableAsyncDns[];
extern const char kDisableBackgroundNetworking[];
extern const char kDisableBundledPpapiFlash[];
extern const char kDisableCastStreamingHWEncoding[];
extern const char kDisableChildAccountDetection[];
extern const char kDisableClearBrowsingDataCounters[];
extern const char kDisableClientSidePhishingDetection[];
extern const char kDisableComponentExtensionsWithBackgroundPages[];
extern const char kDisableComponentUpdate[];
extern const char kDisableDefaultApps[];
extern const char kDisableDeviceDiscoveryNotifications[];
extern const char kDisableDinosaurEasterEgg[];
extern const char kDisableDomainReliability[];
extern const char kDisableExtensionsFileAccessCheck[];
extern const char kDisableExtensionsHttpThrottling[];
extern const char kDisableExtensions[];
extern const char kDisableFieldTrialTestingConfig[];
extern const char kDisableJavaScriptHarmonyShipping[];
extern const char kDisableMaterialDesignDownloads[];
extern const char kDisableMinimizeOnSecondLauncherItemClick[];
extern const char kDisableNewBookmarkApps[];
extern const char kDisableNTPPopularSites[];
extern const char kDisableOfflineAutoReload[];
extern const char kDisableOfflineAutoReloadVisibleOnly[];
extern const char kDisableOutOfProcessPac[];
extern const char kDisablePasswordManagerReauthentication[];
extern const char kDisablePdfMaterialUI[];
extern const char kDisablePermissionsBlacklist[];
extern const char kDisablePopupBlocking[];
extern const char kDisablePreconnect[];
extern const char kDisablePrintPreview[];
extern const char kDisablePromptOnRepost[];
extern const char kDisablePushApiBackgroundMode[];
extern const char kDisableQuic[];
extern const char kDisableQuicPortSelection[];
extern const char kDisableSessionCrashedBubble[];
extern const char kDisableSiteEngagementService[];
extern const char kDisableWebNotificationCustomLayouts[];
extern const char kDisableWebResources[];
extern const char kDisableZeroBrowsersOpenForTests[];
extern const char kDiskCacheDir[];
extern const char kDiskCacheSize[];
extern const char kDnsLogDetails[];
extern const char kDnsPrefetchDisable[];
extern const char kDumpBrowserHistograms[];
extern const char kEasyUnlockAppPath[];
extern const char kEnableAddToShelf[];
extern const char kEnableAppsFileAssociations[];
extern const char kEnableBenchmarking[];
extern const char kEnableBookmarkUndo[];
extern const char kEnableChildAccountDetection[];
extern const char kEnableClearBrowsingDataCounters[];
extern const char kEnableCloudPrintProxy[];
extern const char kEnableDevToolsExperiments[];
extern const char kEnableDeviceDiscoveryNotifications[];
extern const char kEnableDownloadNotification[];
extern const char kEnableDomainReliability[];
extern const char kEnableExperimentalHotwordHardware[];
extern const char kEnableExtensionActivityLogging[];
extern const char kEnableExtensionActivityLogTesting[];
extern const char kEnableFastUnload[];
extern const char kEnableMaterialDesignDownloads[];
extern const char kEnableMaterialDesignExtensions[];
extern const char kEnableMaterialDesignHistory[];
extern const char kEnableMaterialDesignPolicyPage[];
extern const char kEnableNaCl[];
extern const char kEnableNavigationTracing[];
extern const char kEnableNetBenchmarking[];
extern const char kEnableNewBookmarkApps[];
extern const char kEnableNTPPopularSites[];
extern const char kEnableNTPSearchEngineCountryDetection[];
extern const char kEnableOfflineAutoReload[];
extern const char kEnableOfflineAutoReloadVisibleOnly[];
extern const char kEnablePanels[];
extern const char kEnablePdfMaterialUI[];
extern const char kEnablePermissionsBlacklist[];
extern const char kEnablePotentiallyAnnoyingSecurityFeatures[];
extern const char kEnablePowerOverlay[];
extern const char kEnablePrintPreviewRegisterPromos[];
extern const char kEnableProfiling[];
extern const char kEnablePushApiBackgroundMode[];
extern const char kEnableQuic[];
extern const char kEnableQuicPortSelection[];
extern const char kEnableAlternativeServices[];
extern const char kEnableSessionCrashedBubble[];
extern const char kEnableSettingsWindow[];
extern const char kDisableSettingsWindow[];
extern const char kEnableSimplifiedFullscreenUI[];
extern const char kDisableSimplifiedFullscreenUI[];
extern const char kEnableSiteEngagementEvictionPolicy[];
extern const char kEnableSiteEngagementService[];
extern const char kEnableSupervisedUserManagedBookmarksFolder[];
extern const char kEnableTabAudioMuting[];
extern const char kEnableThumbnailRetargeting[];
extern const char kEnableUserAlternateProtocolPorts[];
extern const char kEnableWebAppFrame[];
extern const char kEnableWebNotificationCustomLayouts[];
extern const char kEnableWebUsbOnAnyOrigin[];
extern const char kExtensionContentVerificationBootstrap[];
extern const char kExtensionContentVerificationEnforceStrict[];
extern const char kExtensionContentVerificationEnforce[];
extern const char kExtensionContentVerification[];
extern const char kExtensionsInstallVerification[];
extern const char kExtensionsNotWebstore[];
extern const char kExtensionsUpdateFrequency[];
extern const char kFastStart[];
extern const char kForceAppMode[];
extern const char kForceFieldTrialParams[];
extern const char kForceFirstRun[];
extern const char kForceVariationIds[];
extern const char kForceLocalNtp[];
extern const char kHistoryEnableGroupByDomain[];
extern const char kHomePage[];
extern const char kHostRules[];
extern const char kHostResolverRetryAttempts[];
extern const char kIgnoreUrlFetcherCertRequests[];
extern const char kIncognito[];
extern const char kInstallChromeApp[];
extern const char kInstallSupervisedUserWhitelists[];
extern const char kInstantProcess[];
extern const char kInterestsURL[];
extern const char kIpcDumpDirectory[];
extern const char kJavaScriptHarmony[];
extern const char kKeepAliveForTest[];
extern const char kKioskMode[];
extern const char kKioskModePrinting[];
extern const char kLoadComponentExtension[];
extern const char kLoadExtension[];
#if !defined(GOOGLE_CHROME_BUILD)
extern const char kLocalNtpReload[];
#endif
extern const char kMakeDefaultBrowser[];
extern const char kManualEnhancedBookmarks[];
extern const char kManualEnhancedBookmarksOptout[];
extern const char kMarkNonSecureAs[];
extern const char kMarkNonSecureAsNeutral[];
extern const char kMarkNonSecureAsNonSecure[];
extern const char kMediaCacheSize[];
extern const char kMediaRouter[];
extern const char kMessageLoopHistogrammer[];
extern const char kMetricsRecordingOnly[];
extern const char kMonitoringDestinationID[];
extern const char kNetLogCaptureMode[];
extern const char kNoDefaultBrowserCheck[];
extern const char kNoDisplayingInsecureContent[];
extern const char kNoExperiments[];
extern const char kNoFirstRun[];
extern const char kNoProxyServer[];
extern const char kNoPings[];
extern const char kNoServiceAutorun[];
extern const char kNoStartupWindow[];
extern const char kNoSupervisedUserAcknowledgmentCheck[];
extern const char kNumPacThreads[];
extern const char kOpenInNewWindow[];
extern const char kOriginToForceQuicOn[];
extern const char kOriginalProcessStartTime[];
extern const char kPackExtension[];
extern const char kPackExtensionKey[];
extern const char kParentProfile[];
extern const char kPermissionRequestApiScope[];
extern const char kPermissionRequestApiUrl[];
extern const char kPpapiFlashPath[];
extern const char kPpapiFlashVersion[];
extern const char kPrerenderFromOmnibox[];
extern const char kPrerenderFromOmniboxSwitchValueAuto[];
extern const char kPrerenderFromOmniboxSwitchValueDisabled[];
extern const char kPrerenderFromOmniboxSwitchValueEnabled[];
extern const char kPrerenderMode[];
extern const char kPrerenderModeSwitchValueDisabled[];
extern const char kPrerenderModeSwitchValueEnabled[];
extern const char kPrivetIPv6Only[];
extern const char kProductVersion[];
extern const char kProfileDirectory[];
extern const char kProfilingAtStart[];
extern const char kProfilingFile[];
extern const char kProfilingFlush[];
extern const char kProxyAutoDetect[];
extern const char kProxyBypassList[];
extern const char kProxyPacUrl[];
extern const char kProxyServer[];
extern const char kQuicConnectionOptions[];
extern const char kQuicMaxPacketLength[];
extern const char kQuicVersion[];
extern const char kRemoteDebuggingTargets[];
extern const char kRestoreLastSession[];
extern const char kRestrictIFramePermissions[];
extern const char kSavePageAsMHTML[];
extern const char kSbDisableAutoUpdate[];
extern const char kSbDisableDownloadProtection[];
extern const char kSbDisableExtensionBlacklist[];
extern const char kSbManualDownloadBlacklist[];
extern const char kServiceProcess[];
extern const char kSilentDebuggerExtensionAPI[];
extern const char kSilentLaunch[];
extern const char kShowAppList[];
extern const char kShowSavedCopy[];
extern const char kSSLKeyLogFile[];
extern const char kEnableShowSavedCopyPrimary[];
extern const char kEnableShowSavedCopySecondary[];
extern const char kDisableShowSavedCopy[];
extern const char kSimulateElevatedRecovery[];
extern const char kSimulateUpgrade[];
extern const char kSimulateCriticalUpdate[];
extern const char kSimulateOutdated[];
extern const char kSimulateOutdatedNoAU[];
extern const char kSpeculativeResourcePrefetching[];
extern const char kSpeculativeResourcePrefetchingDisabled[];
extern const char kSpeculativeResourcePrefetchingEnabled[];
extern const char kSpeculativeResourcePrefetchingLearning[];
#if defined(ENABLE_SPELLCHECK)
#if defined(OS_ANDROID)
extern const char kEnableAndroidSpellChecker[];
#endif
extern const char kDisableMultilingualSpellChecker[];
extern const char kEnableMultilingualSpellChecker[];
extern const char kEnableSpellingFeedbackFieldTrial[];
extern const char kSpellingServiceFeedbackUrl[];
extern const char kSpellingServiceFeedbackIntervalSeconds[];
#endif
extern const char kStartMaximized[];
extern const char kSupervisedUserId[];
extern const char kSupervisedUserSafeSites[];
extern const char kSupervisedUserSyncToken[];
extern const char kSystemLogUploadFrequency[];
extern const char kTestName[];
extern const char kTestingChannelID[];
extern const char kTrustedSpdyProxy[];
extern const char kTryChromeAgain[];
extern const char kUnlimitedStorage[];
extern const char kUnsafelyTreatInsecureOriginAsSecure[];
extern const char kUseSimpleCacheBackend[];
extern const char kUseSpdy[];
extern const char kUserAgent[];
extern const char kUserDataDir[];
extern const char kV8PacMojoInProcess[];
extern const char kV8PacMojoOutOfProcess[];
extern const char kValidateCrx[];
extern const char kVersion[];
extern const char kWindowPosition[];
extern const char kWindowSize[];
extern const char kWinHttpProxyResolver[];
extern const char kWinJumplistAction[];

#if defined(OS_ANDROID)
extern const char kAuthAndroidNegotiateAccountType[];
extern const char kDisableContextualSearch[];
extern const char kEnableAccessibilityTabSwitcher[];
extern const char kEnableContextualSearch[];
extern const char kEnableDomDistillerButtonAnimation[];
extern const char kEnableHostedMode[];
extern const char kEnableHungRendererInfoBar[];
extern const char kEnablePhysicalWeb[];
extern const char kProgressBarAnimation[];
extern const char kEnableThemeColorInTabbedMode[];
extern const char kDisableAutoHidingToolbarThreshold[];
extern const char kEnableTabSwitcherInDocumentMode[];
#endif  // defined(OS_ANDROID)

#if defined(USE_ASH)
extern const char kOpenAsh[];
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_CHROMEOS)
extern const char kHelp[];
extern const char kHelpShort[];
extern const char kPasswordStore[];
#endif

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
extern const char kMigrateDataDirForSxS[];
#endif

#if defined(OS_MACOSX)
extern const char kAppsKeepChromeAliveInTests[];
extern const char kHostedAppQuitNotification[];
extern const char kDisableAppInfoDialogMac[];
extern const char kDisableAppWindowCycling[];
extern const char kDisableHostedAppShimCreation[];
extern const char kDisableHostedAppsInWindows[];
extern const char kDisableMacViewsNativeAppWindows[];
extern const char kDisableSavePasswordBubble[];
extern const char kDisableTranslateNewUX[];
extern const char kEnableAppInfoDialogMac[];
extern const char kEnableAppWindowCycling[];
extern const char kEnableHostedAppsInWindows[];
extern const char kEnableMacViewsNativeAppWindows[];
extern const char kEnableMacViewsDialogs[];
extern const char kEnableSavePasswordBubble[];
extern const char kEnableTranslateNewUX[];
extern const char kMetricsClientID[];
extern const char kRelauncherProcess[];
extern const char kRelauncherProcessDMGDevice[];
#endif  // defined(OS_MACOSX)

#if defined(OS_WIN)
extern const char kCrashpadHandler[];
extern const char kEnableCloudPrintXps[];
extern const char kEnableProfileShortcutManager[];
extern const char kForceDesktop[];
extern const char kForceImmersive[];
extern const char kHideIcons[];
extern const char kNoNetworkProfileWarning[];
extern const char kRelaunchShortcut[];
extern const char kShowIcons[];
extern const char kUninstall[];
extern const char kViewerLaunchViaAppId[];
extern const char kWaitForMutex[];
extern const char kWindows8Search[];
#endif  // defined(OS_WIN)

#if defined(ENABLE_IPC_FUZZER)
extern const char kIpcFuzzerTestcase[];
#endif

#if defined(ENABLE_PRINT_PREVIEW) && !defined(OFFICIAL_BUILD)
extern const char kDebugPrint[];
#endif

#if defined(ENABLE_PLUGINS)
extern const char kAllowNaClCrxFsAPI[];
extern const char kAllowNaClFileHandleAPI[];
extern const char kAllowNaClSocketAPI[];
#endif

#if defined(FULL_SAFE_BROWSING)
extern const char kAllowUncheckedDangerousDownloads[];
extern const char kDisallowUncheckedDangerousDownloads[];
#endif

#if defined(ENABLE_TASK_MANAGER)
extern const char kDisableNewTaskManager[];
#endif  // defined(ENABLE_TASK_MANAGER)

#if defined(ENABLE_WAYLAND_SERVER)
extern const char kEnableWaylandServer[];
#endif

bool AboutInSettingsEnabled();
bool MdExtensionsEnabled();
bool MdHistoryEnabled();
bool MdPolicyPageEnabled();
bool PdfMaterialUIEnabled();
bool SettingsWindowEnabled();

#if defined(OS_CHROMEOS)
bool PowerOverlayEnabled();
#endif

#if defined(ENABLE_TASK_MANAGER)
bool NewTaskManagerEnabled();
#endif  // defined(ENABLE_TASK_MANAGER)

// DON'T ADD RANDOM STUFF HERE. Put it in the main section above in
// alphabetical order, or in one of the ifdefs (also in order in each section).

}  // namespace switches

#endif  // CHROME_COMMON_CHROME_SWITCHES_H_
