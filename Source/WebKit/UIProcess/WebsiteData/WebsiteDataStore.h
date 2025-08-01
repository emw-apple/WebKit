/*
 * Copyright (C) 2014-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "FrameInfoData.h"
#include "NetworkSessionCreationParameters.h"
#include "WebDeviceOrientationAndMotionAccessController.h"
#include "WebFramePolicyListenerProxy.h"
#include "WebPageProxyIdentifier.h"
#include "WebPreferences.h"
#include "WebResourceLoadStatisticsStore.h"
#include "WebsiteDataStoreClient.h"
#include "WebsiteDataStoreConfiguration.h"
#include <WebCore/Cookie.h>
#include <WebCore/DeviceOrientationOrMotionPermissionState.h>
#include <WebCore/PageIdentifier.h>
#include <WebCore/RegistrableDomain.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/SecurityOriginHash.h>
#include <pal/SessionID.h>
#include <wtf/CheckedRef.h>
#include <wtf/Function.h>
#include <wtf/HashSet.h>
#include <wtf/OptionSet.h>
#include <wtf/RefCounted.h>
#include <wtf/RefCounter.h>
#include <wtf/RefPtr.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakHashSet.h>
#include <wtf/WeakPtr.h>
#include <wtf/WorkQueue.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(COCOA)
#include <pal/spi/cf/CFNetworkSPI.h>
#include <wtf/OSObjectPtr.h>
#include <wtf/spi/darwin/XPCSPI.h>
#endif

#if USE(CURL)
#include <WebCore/CurlProxySettings.h>
#endif

#if USE(SOUP)
#include "SoupCookiePersistentStorageType.h"
#include <WebCore/HTTPCookieAcceptPolicy.h>
#include <WebCore/SoupNetworkProxySettings.h>
#endif

namespace API {
class Data;
class DownloadClient;
class HTTPCookieStore;
}

namespace WebCore {
class CertificateInfo;
class RegistrableDomain;
class ResourceRequest;
class SecurityOrigin;
class LocalWebLockRegistry;
class PrivateClickMeasurement;
struct RecentSearch;

struct MockWebAuthenticationConfiguration;
struct NotificationData;
}

namespace WebKit {

class AuthenticatorManager;
class AuxiliaryProcessProxy;
class SecKeyProxyStore;
class DeviceIdHashSaltStorage;
class DownloadProxy;
class NetworkProcessProxy;
class SOAuthorizationCoordinator;
class VirtualAuthenticatorManager;
class WebPageProxy;
class WebProcessPool;
class WebProcessProxy;
class WebResourceLoadStatisticsStore;
enum class CacheModel : uint8_t;
enum class CallDownloadDidStart : bool;
enum class RestrictedOpenerType : uint8_t;
enum class ShouldGrandfatherStatistics : bool;
enum class StorageAccessStatus : uint8_t;
enum class StorageAccessPromptStatus;
enum class UnifiedOriginStorageLevel : uint8_t;
enum class WebsiteDataFetchOption : uint8_t;
enum class WebsiteDataType : uint32_t;

struct ITPThirdPartyData;
struct NetworkProcessConnectionInfo;
struct WebPushMessage;
struct WebsiteDataRecord;
struct WebsiteDataStoreParameters;

enum RemoveDataTaskCounterType { };
using RemoveDataTaskCounter = RefCounter<RemoveDataTaskCounterType>;

class WebsiteDataStore : public API::ObjectImpl<API::Object::Type::WebsiteDataStore>, public CanMakeWeakPtr<WebsiteDataStore> {
public:
    static Ref<WebsiteDataStore> defaultDataStore();
    static bool defaultDataStoreExists();
    static void deleteDefaultDataStoreForTesting();
    static RefPtr<WebsiteDataStore> existingDataStoreForIdentifier(const WTF::UUID&);
    
    static Ref<WebsiteDataStore> createNonPersistent();
    static Ref<WebsiteDataStore> create(Ref<WebsiteDataStoreConfiguration>&&, PAL::SessionID);
#if PLATFORM(COCOA)
    static Ref<WebsiteDataStore> dataStoreForIdentifier(const WTF::UUID&);
#endif

    WebsiteDataStore(Ref<WebsiteDataStoreConfiguration>&&, PAL::SessionID);
    ~WebsiteDataStore();

    static void forEachWebsiteDataStore(NOESCAPE Function<void(WebsiteDataStore&)>&&);
    
    NetworkProcessProxy& networkProcess() const;
    NetworkProcessProxy& networkProcess();
    Ref<NetworkProcessProxy> protectedNetworkProcess() const;
    NetworkProcessProxy* networkProcessIfExists() { return m_networkProcess.get(); }
    void setNetworkProcess(NetworkProcessProxy&);
    
    static WebsiteDataStore* existingDataStoreForSessionID(PAL::SessionID);

    bool isPersistent() const { return !m_sessionID.isEphemeral(); }
    PAL::SessionID sessionID() const { return m_sessionID; }

    enum class ProcessAccessType : uint8_t { None, OnlyIfLaunched, Launch };
    static ProcessAccessType computeWebProcessAccessTypeForDataRemoval(OptionSet<WebsiteDataType> dataTypes, bool /* isNonPersistentStore */);
    
    void registerProcess(WebProcessProxy&);
    void unregisterProcess(WebProcessProxy&);
    
    const WeakHashSet<WebProcessProxy>& processes() const { return m_processes; }

    enum class ShouldRetryOnFailure : bool { No, Yes };
    void getNetworkProcessConnection(WebProcessProxy&, CompletionHandler<void(NetworkProcessConnectionInfo&&)>&&, ShouldRetryOnFailure = ShouldRetryOnFailure::Yes);
    void terminateNetworkProcess();
    void sendNetworkProcessPrepareToSuspendForTesting(CompletionHandler<void()>&&);
    void sendNetworkProcessWillSuspendImminentlyForTesting();
    void sendNetworkProcessDidResume();
    void networkProcessDidTerminate(NetworkProcessProxy&);
    static void makeNextNetworkProcessLaunchFailForTesting();
    static bool shouldMakeNextNetworkProcessLaunchFailForTesting();

    bool trackingPreventionEnabled() const;
    void setTrackingPreventionEnabled(bool);
    bool resourceLoadStatisticsDebugMode() const;
    void setResourceLoadStatisticsDebugMode(bool);
    void setResourceLoadStatisticsDebugMode(bool, CompletionHandler<void()>&&);
    void isResourceLoadStatisticsEphemeral(CompletionHandler<void(bool)>&&) const;

    void setPrivateClickMeasurementDebugMode(bool);
    void storePrivateClickMeasurement(const WebCore::PrivateClickMeasurement&);

    bool storageSiteValidationEnabled() const { return m_storageSiteValidationEnabled; }
    void setStorageSiteValidationEnabled(bool);

    uint64_t perOriginStorageQuota() const { return m_configuration->perOriginStorageQuota(); }
    std::optional<double> originQuotaRatio() { return m_configuration->originQuotaRatio(); }

    void didAllowPrivateTokenUsageByThirdPartyForTesting(bool wasAllowed, URL&& resourceURL);

    bool isBlobRegistryPartitioningEnabled() const;
    bool isOptInCookiePartitioningEnabled() const;
    void propagateSettingUpdates();

#if PLATFORM(IOS_FAMILY)
    String resolvedCookieStorageDirectory();
    String resolvedContainerTemporaryDirectory();
    static String defaultResolvedContainerTemporaryDirectory();
    static String cacheDirectoryInContainerOrHomeDirectory(const String& subpath);
#endif

    void clearResourceLoadStatisticsInWebProcesses(CompletionHandler<void()>&&);
    void setUserAgentStringQuirkForTesting(const String& domain, const String& userAgentString, CompletionHandler<void()>&&);
    void setPrivateTokenIPCForTesting(bool enabled);

    void fetchData(OptionSet<WebsiteDataType>, OptionSet<WebsiteDataFetchOption>, Function<void(Vector<WebsiteDataRecord>)>&& completionHandler);
    void removeData(OptionSet<WebsiteDataType>, WallTime modifiedSince, Function<void()>&& completionHandler);
    void removeData(OptionSet<WebsiteDataType>, const Vector<WebsiteDataRecord>&, Function<void()>&& completionHandler);

    void setCacheModelSynchronouslyForTesting(CacheModel);
    void setServiceWorkerTimeoutForTesting(Seconds);
    void resetServiceWorkerTimeoutForTesting();
    bool hasServiceWorkerBackgroundActivityForTesting() const;
    void runningOrTerminatingServiceWorkerCountForTesting(CompletionHandler<void(unsigned)>&&);

    void fetchDataForRegistrableDomains(OptionSet<WebsiteDataType>, OptionSet<WebsiteDataFetchOption>, Vector<WebCore::RegistrableDomain>&&, CompletionHandler<void(Vector<WebsiteDataRecord>&&, HashSet<WebCore::RegistrableDomain>&&)>&&);
    void clearPrevalentResource(const URL&, CompletionHandler<void()>&&);
    void clearUserInteraction(const URL&, CompletionHandler<void()>&&);
    void dumpResourceLoadStatistics(CompletionHandler<void(const String&)>&&);
    void logTestingEvent(const String&);
    void logUserInteraction(const URL&, CompletionHandler<void()>&&);
    void getAllStorageAccessEntries(WebPageProxyIdentifier, CompletionHandler<void(Vector<String>&& domains)>&&);
    void hasHadUserInteraction(const URL&, CompletionHandler<void(bool)>&&);
    void isRelationshipOnlyInDatabaseOnce(const URL& subUrl, const URL& topUrl, CompletionHandler<void(bool)>&&);
    void isPrevalentResource(const URL&, CompletionHandler<void(bool)>&&);
    void isRegisteredAsRedirectingTo(const URL& hostRedirectedFrom, const URL& hostRedirectedTo, CompletionHandler<void(bool)>&&);
    void isRegisteredAsSubresourceUnder(const URL& subresource, const URL& topFrame, CompletionHandler<void(bool)>&&);
    void isRegisteredAsSubFrameUnder(const URL& subFrame, const URL& topFrame, CompletionHandler<void(bool)>&&);
    void isVeryPrevalentResource(const URL&, CompletionHandler<void(bool)>&&);
    void resetParametersToDefaultValues(CompletionHandler<void()>&&);
    void scheduleCookieBlockingUpdate(CompletionHandler<void()>&&);
    void scheduleClearInMemoryAndPersistent(WallTime modifiedSince, ShouldGrandfatherStatistics, CompletionHandler<void()>&&);
    void scheduleClearInMemoryAndPersistent(ShouldGrandfatherStatistics, CompletionHandler<void()>&&);
    void getResourceLoadStatisticsDataSummary(CompletionHandler<void(Vector<ITPThirdPartyData>&&)>&&);
    void scheduleStatisticsAndDataRecordsProcessing(CompletionHandler<void()>&&);
    void setGrandfathered(const URL&, bool, CompletionHandler<void()>&&);
    void isGrandfathered(const URL&, CompletionHandler<void(bool)>&&);
    void setGrandfatheringTime(Seconds, CompletionHandler<void()>&&);
    void setLastSeen(const URL&, Seconds, CompletionHandler<void()>&&);
    void domainIDExistsInDatabase(int domainID, CompletionHandler<void(bool)>&&);
    void statisticsDatabaseHasAllTables(CompletionHandler<void(bool)>&&);
    void mergeStatisticForTesting(const URL&, const URL& topFrameUrl1, const URL& topFrameUrl2, Seconds lastSeen, bool hadUserInteraction, Seconds mostRecentUserInteraction, bool isGrandfathered, bool isPrevalent, bool isVeryPrevalent, unsigned dataRecordsRemoved, CompletionHandler<void()>&&);
    void insertExpiredStatisticForTesting(const URL&, unsigned numberOfOperatingDaysPassed, bool hadUserInteraction, bool isScheduledForAllButCookieDataRemoval, bool isPrevalent, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsTimeAdvanceForTesting(Seconds, CompletionHandler<void()>&&);
    void setStorageAccessPromptQuirkForTesting(String&& topFrameDomain, Vector<String>&& subFrameDomains, Vector<String>&& triggerPages, CompletionHandler<void()>&&);
    void grantStorageAccessForTesting(String&& topFrameDomain, Vector<String>&& subFrameDomains, CompletionHandler<void()>&&);
    void setIsRunningResourceLoadStatisticsTest(bool, CompletionHandler<void()>&&);
    void setPruneEntriesDownTo(size_t, CompletionHandler<void()>&&);
    void setSubframeUnderTopFrameDomain(const URL& subframe, const URL& topFrame, CompletionHandler<void()>&&);
    void setSubresourceUnderTopFrameDomain(const URL& subresource, const URL& topFrame, CompletionHandler<void()>&&);
    void setSubresourceUniqueRedirectTo(const URL& subresource, const URL& hostNameRedirectedTo, CompletionHandler<void()>&&);
    void setSubresourceUniqueRedirectFrom(const URL& subresource, const URL& hostNameRedirectedFrom, CompletionHandler<void()>&&);
    void setTimeToLiveUserInteraction(Seconds, CompletionHandler<void()>&&);
    void setTopFrameUniqueRedirectTo(const URL& topFrameHostName, const URL& hostNameRedirectedTo, CompletionHandler<void()>&&);
    void setTopFrameUniqueRedirectFrom(const URL& topFrameHostName, const URL& hostNameRedirectedFrom, CompletionHandler<void()>&&);
    void setMaxStatisticsEntries(size_t, CompletionHandler<void()>&&);
    void setMinimumTimeBetweenDataRecordsRemoval(Seconds, CompletionHandler<void()>&&);
    void setPrevalentResource(const URL&, CompletionHandler<void()>&&);
    void setPrevalentResourceForDebugMode(const URL&, CompletionHandler<void()>&&);
    void setShouldClassifyResourcesBeforeDataRecordsRemoval(bool, CompletionHandler<void()>&&);
    void setStatisticsTestingCallback(Function<void(const String&)>&&);
    bool hasStatisticsTestingCallback() const { return !!m_statisticsTestingCallback; }
    void setVeryPrevalentResource(const URL&, CompletionHandler<void()>&&);
    void setSubframeUnderTopFrameDomain(const URL& subframe, const URL& topFrame);
    void setCrossSiteLoadWithLinkDecorationForTesting(const URL& fromURL, const URL& toURL, bool wasFiltered, CompletionHandler<void()>&&);
    void resetCrossSiteLoadsWithLinkDecorationForTesting(CompletionHandler<void()>&&);
    void deleteCookiesForTesting(const URL&, bool includeHttpOnlyCookies, CompletionHandler<void()>&&);
    void hasLocalStorageForTesting(const URL&, CompletionHandler<void(bool)>&&) const;
    void hasIsolatedSessionForTesting(const URL&, CompletionHandler<void(bool)>&&) const;
    void setResourceLoadStatisticsShouldDowngradeReferrerForTesting(bool, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsShouldBlockThirdPartyCookiesForTesting(bool enabled, WebCore::ThirdPartyCookieBlockingMode, CompletionHandler<void()>&&);
    void setThirdPartyCookieBlockingMode(WebCore::ThirdPartyCookieBlockingMode, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsShouldEnbleSameSiteStrictEnforcementForTesting(bool enabled, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsFirstPartyWebsiteDataRemovalModeForTesting(bool enabled, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsToSameSiteStrictCookiesForTesting(const URL&, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsFirstPartyHostCNAMEDomainForTesting(const URL& firstPartyURL, const URL& cnameURL, CompletionHandler<void()>&&);
    void setResourceLoadStatisticsThirdPartyCNAMEDomainForTesting(const URL&, CompletionHandler<void()>&&);
    WebCore::ThirdPartyCookieBlockingMode thirdPartyCookieBlockingMode() const;
    void closeDatabases(CompletionHandler<void()>&&);
    void syncLocalStorage(CompletionHandler<void()>&&);
    void storeServiceWorkerRegistrations(CompletionHandler<void()>&&);
    void setCacheMaxAgeCapForPrevalentResources(Seconds, CompletionHandler<void()>&&);
    void resetCacheMaxAgeCapForPrevalentResources(CompletionHandler<void()>&&);
    const WebsiteDataStoreConfiguration::Directories& resolvedDirectories() const;
    FileSystem::Salt mediaKeysStorageSalt() const;
#if ENABLE(SCREEN_TIME)
    void removeScreenTimeData(const HashSet<URL>& websitesToRemove);
    void removeScreenTimeDataWithInterval(WallTime);
#endif

    static void setCachedProcessSuspensionDelayForTesting(Seconds);

#if !PLATFORM(COCOA)
    void allowSpecificHTTPSCertificateForHost(const WebCore::CertificateInfo&, const String& host);
#endif
    void allowTLSCertificateChainForLocalPCMTesting(const WebCore::CertificateInfo&);

    DeviceIdHashSaltStorage& ensureDeviceIdHashSaltStorage();
    Ref<DeviceIdHashSaltStorage> ensureProtectedDeviceIdHashSaltStorage();

#if ENABLE(ENCRYPTED_MEDIA)
    DeviceIdHashSaltStorage& ensureMediaKeysHashSaltStorage();
    Ref<DeviceIdHashSaltStorage> ensureProtectedMediaKeysHashSaltStorage();
#endif

    WebsiteDataStoreParameters parameters();
    static Vector<WebsiteDataStoreParameters> parametersFromEachWebsiteDataStore();

    void flushCookies(CompletionHandler<void()>&&);

    void dispatchOnQueue(Function<void()>&&);

#if PLATFORM(COCOA)
    static std::optional<bool> useNetworkLoader();
#endif

#if USE(CURL)
    void setNetworkProxySettings(WebCore::CurlProxySettings&&);
    const WebCore::CurlProxySettings& networkProxySettings() const { return m_proxySettings; }
#endif

#if USE(SOUP)
    void setPersistentCredentialStorageEnabled(bool);
    bool persistentCredentialStorageEnabled() const { return m_persistentCredentialStorageEnabled && isPersistent(); }
    void setIgnoreTLSErrors(bool);
    bool ignoreTLSErrors() const { return m_ignoreTLSErrors; }
    void setNetworkProxySettings(WebCore::SoupNetworkProxySettings&&);
    const WebCore::SoupNetworkProxySettings& networkProxySettings() const { return m_networkProxySettings; }
    void setCookiePersistentStorage(const String&, SoupCookiePersistentStorageType);
    void setHTTPCookieAcceptPolicy(WebCore::HTTPCookieAcceptPolicy);
#endif

    static void allowWebsiteDataRecordsForAllOrigins();

#if HAVE(SEC_KEY_PROXY)
    void addSecKeyProxyStore(Ref<SecKeyProxyStore>&&);
#endif

#if ENABLE(WEB_AUTHN)
    AuthenticatorManager& authenticatorManager() { return m_authenticatorManager.get(); }
    Ref<AuthenticatorManager> protectedAuthenticatorManager();
    void setMockWebAuthenticationConfiguration(WebCore::MockWebAuthenticationConfiguration&&);
    VirtualAuthenticatorManager& virtualAuthenticatorManager();
    Ref<VirtualAuthenticatorManager> protectedVirtualAuthenticatorManager();
#endif

    const WebsiteDataStoreConfiguration& configuration() const { return m_configuration.get(); }

    WebsiteDataStoreClient& client() { return m_client.get(); }
    void setClient(UniqueRef<WebsiteDataStoreClient>&& client) { m_client = WTFMove(client); }

    API::HTTPCookieStore& cookieStore();
    Ref<API::HTTPCookieStore> protectedCookieStore();
    WebCore::LocalWebLockRegistry& webLockRegistry() { return m_webLockRegistry.get(); }

    void renameOriginInWebsiteData(WebCore::SecurityOriginData&&, WebCore::SecurityOriginData&&, OptionSet<WebsiteDataType>, CompletionHandler<void()>&&);
    void originDirectoryForTesting(WebCore::ClientOrigin&&, OptionSet<WebsiteDataType>, CompletionHandler<void(const String&)>&&);

    bool networkProcessHasEntitlementForTesting(const String&);

#if ENABLE(DEVICE_ORIENTATION)
    WebDeviceOrientationAndMotionAccessController& deviceOrientationAndMotionAccessController() { return m_deviceOrientationAndMotionAccessController; }
    Ref<WebDeviceOrientationAndMotionAccessController> protectedDeviceOrientationAndMotionAccessController() { return m_deviceOrientationAndMotionAccessController; }
#endif

#if HAVE(APP_SSO)
    SOAuthorizationCoordinator& soAuthorizationCoordinator(const WebPageProxy&);
#endif

#if PLATFORM(COCOA)
    static void fetchAllDataStoreIdentifiers(CompletionHandler<void(Vector<WTF::UUID>&&)>&&);
    static void removeDataStoreWithIdentifier(const WTF::UUID& identifier, CompletionHandler<void(const String&)>&&);
    static void removeDataStoreWithIdentifierImpl(const WTF::UUID& identifier, CompletionHandler<void(const String&)>&&);
    static String defaultWebsiteDataStoreDirectory(const WTF::UUID& identifier);
    static String defaultCookieStorageFile(const String& baseDataDirectory = nullString());
    static String defaultSearchFieldHistoryDirectory(const String& baseDataDirectory = nullString());
#endif
    static String defaultServiceWorkerRegistrationDirectory(const String& baseDataDirectory = nullString());
    static String defaultLocalStorageDirectory(const String& baseDataDirectory = nullString());
    static String defaultResourceLoadStatisticsDirectory(const String& baseDataDirectory = nullString());
    static String defaultNetworkCacheDirectory(const String& baseCacheDirectory = nullString());
    static String defaultAlternativeServicesDirectory(const String& baseCacheDirectory = nullString());
    static String defaultApplicationCacheDirectory(const String& baseCacheDirectory = nullString());
    static String defaultWebSQLDatabaseDirectory(const String& baseDataDirectory = nullString());
    static String defaultHSTSStorageDirectory(const String& baseCacheDirectory = nullString());
#if ENABLE(ARKIT_INLINE_PREVIEW)
    static String defaultModelElementCacheDirectory(const String& baseCacheDirectory = nullString());
#endif
    static String defaultIndexedDBDatabaseDirectory(const String& baseDataDirectory = nullString());
    static String defaultCacheStorageDirectory(const String& baseCacheDirectory = nullString());
    static String defaultGeneralStorageDirectory(const String& baseDataDirectory = nullString());
    static String defaultMediaCacheDirectory(const String& baseCacheDirectory = nullString());
    static String defaultMediaKeysStorageDirectory(const String& baseDataDirectory = nullString());
    static String defaultDeviceIdHashSaltsStorageDirectory(const String& baseDataDirectory = nullString());
#if ENABLE(ENCRYPTED_MEDIA)
    static String defaultMediaKeysHashSaltsStorageDirectory(const String& baseDataDirectory = nullString());
#endif
    static String defaultJavaScriptConfigurationDirectory(const String& baseDataDirectory = nullString());

#if ENABLE(CONTENT_EXTENSIONS)
    static String defaultResourceMonitorThrottlerDirectory(const String& baseDataDirectory = nullString());
#endif

    static constexpr uint64_t defaultPerOriginQuota() { return 1000 * MB; }
    static constexpr uint64_t defaultStandardVolumeCapacity() {
#if PLATFORM(MAC)
        return 128 * GB;
#elif PLATFORM(IOS) || PLATFORM(VISION)
        return 64 * GB;
#else
        return 16 * GB;
#endif
    }
    static std::optional<double> defaultOriginQuotaRatio();
    static std::optional<double> defaultTotalQuotaRatio();
    static UnifiedOriginStorageLevel defaultUnifiedOriginStorageLevel();

#if USE(GLIB)
    static const String& defaultBaseCacheDirectory();
    static const String& defaultBaseDataDirectory();
#endif

    void resetQuota(CompletionHandler<void()>&&);
    void resetStoragePersistedState(CompletionHandler<void()>&&);
#if PLATFORM(IOS_FAMILY)
    void setBackupExclusionPeriodForTesting(Seconds, CompletionHandler<void()>&&);
#endif

#if ENABLE(APP_BOUND_DOMAINS)
    void hasAppBoundSession(CompletionHandler<void(bool)>&&) const;
    void clearAppBoundSession(CompletionHandler<void()>&&);
    void beginAppBoundDomainCheck(const String& host, const String& protocol, WebFramePolicyListenerProxy&);
    void getAppBoundDomains(CompletionHandler<void(const HashSet<WebCore::RegistrableDomain>&)>&&) const;
    void getAppBoundSchemes(CompletionHandler<void(const HashSet<String>&)>&&) const;
    void ensureAppBoundDomains(CompletionHandler<void(const HashSet<WebCore::RegistrableDomain>&, const HashSet<String>&)>&&) const;
    void reinitializeAppBoundDomains();
    static void setAppBoundDomainsForTesting(HashSet<WebCore::RegistrableDomain>&&, CompletionHandler<void()>&&);
#endif
#if ENABLE(MANAGED_DOMAINS)
    void ensureManagedDomains(CompletionHandler<void(const HashSet<WebCore::RegistrableDomain>&)>&&) const;
    void getManagedDomains(CompletionHandler<void(const HashSet<WebCore::RegistrableDomain>&)>&&) const;
    void reinitializeManagedDomains();
    static void setManagedDomainsForTesting(HashSet<WebCore::RegistrableDomain>&&, CompletionHandler<void()>&&);
#endif

    void updateBundleIdentifierInNetworkProcess(const String&, CompletionHandler<void()>&&);
    void clearBundleIdentifierInNetworkProcess(CompletionHandler<void()>&&);

    void countNonDefaultSessionSets(CompletionHandler<void(uint64_t)>&&);

    bool showPersistentNotification(IPC::Connection*, const WebCore::NotificationData&);
    void cancelServiceWorkerNotification(const WTF::UUID& notificationID);
    void clearServiceWorkerNotification(const WTF::UUID& notificationID);
    void didDestroyServiceWorkerNotification(const WTF::UUID& notificationID);

    bool hasClientGetDisplayedNotifications() const;
    void getNotifications(const URL& registrationalURL, CompletionHandler<void(Vector<WebCore::NotificationData>&&)>&&);

    void openWindowFromServiceWorker(const String& urlString, const WebCore::SecurityOriginData& serviceWorkerOrigin, CompletionHandler<void(std::optional<WebCore::PageIdentifier>)>&&);
    void reportServiceWorkerConsoleMessage(const URL&, const WebCore::SecurityOriginData&, MessageSource,  MessageLevel, const String& message, unsigned long requestIdentifier);

    void workerUpdatedAppBadge(const WebCore::SecurityOriginData&, std::optional<uint64_t>);

#if ENABLE(INSPECTOR_NETWORK_THROTTLING)
    void setEmulatedConditions(std::optional<int64_t>&& bytesPerSecondLimit);
#endif

    void addPage(WebPageProxy&);
    void removePage(WebPageProxy&);

    void updateServiceWorkerInspectability();

    HashSet<RefPtr<WebProcessPool>> processPools(size_t limit = std::numeric_limits<size_t>::max()) const;

    void setServiceWorkerOverridePreferences(WebPreferences* preferences) { m_serviceWorkerOverridePreferences = preferences; }
    WebPreferences* serviceWorkerOverridePreferences() const { return m_serviceWorkerOverridePreferences.get(); }

    Ref<DownloadProxy> createDownloadProxy(Ref<API::DownloadClient>&&, const WebCore::ResourceRequest&, WebPageProxy* originatingPage, const std::optional<FrameInfoData>&);
    void download(const DownloadProxy&, const String& suggestedFilename);
    void resumeDownload(const DownloadProxy&, const API::Data&, const String& path, CallDownloadDidStart);

    void saveRecentSearches(const String& name, const Vector<WebCore::RecentSearch>&);
    void loadRecentSearches(const String& name, CompletionHandler<void(Vector<WebCore::RecentSearch>&&)>&&);

#if HAVE(NW_PROXY_CONFIG)
    void clearProxyConfigData();
    void setProxyConfigData(Vector<std::pair<Vector<uint8_t>, std::optional<WTF::UUID>>>&&);
#endif
    void setCompletionHandlerForRemovalFromNetworkProcess(CompletionHandler<void(String&&)>&&);

    void processPushMessage(WebPushMessage&&, CompletionHandler<void(bool)>&&);

    void setOriginQuotaRatioEnabledForTesting(bool enabled, CompletionHandler<void()>&&);

    RestrictedOpenerType openerTypeForDomain(const WebCore::RegistrableDomain&) const;
    void setRestrictedOpenerTypeForDomainForTesting(const WebCore::RegistrableDomain&, RestrictedOpenerType);

    bool operator==(const WebsiteDataStore& other) const { return (m_sessionID == other.sessionID()); }
    void resolveDirectoriesAsynchronously();

    const HashSet<URL>& persistedSiteURLs() const { return m_persistedSiteURLs; }
    void setPersistedSiteURLs(HashSet<URL>&&);

    void getAppBadgeForTesting(CompletionHandler<void(std::optional<uint64_t>)>&&);

    void fetchLocalStorage(CompletionHandler<void(std::optional<HashMap<WebCore::ClientOrigin, HashMap<String, String>>>&&)>&&);
    void restoreLocalStorage(HashMap<WebCore::ClientOrigin, HashMap<String, String>>&&, CompletionHandler<void(bool)>&&);

#if ENABLE(WEB_PUSH_NOTIFICATIONS)
    bool builtInNotificationsEnabled() const;
#endif

#if ENABLE(CONTENT_EXTENSIONS)
    void resetResourceMonitorThrottlerForTesting(CompletionHandler<void()>&&);
#endif

    bool isRemovingData() const { return!!m_removeDataTaskCounter.value(); }
    uint64_t cookiesVersion() const { return m_cookiesVersion; }
    void setCookies(Vector<WebCore::Cookie>&&, CompletionHandler<void()>&&);

private:
    enum class ForceReinitialization : bool { No, Yes };
#if ENABLE(APP_BOUND_DOMAINS)
    void initializeAppBoundDomains(ForceReinitialization = ForceReinitialization::No);
    void addTestDomains() const;
#endif
    void initializeManagedDomains(ForceReinitialization = ForceReinitialization::No);

    void fetchDataAndApply(OptionSet<WebsiteDataType>, OptionSet<WebsiteDataFetchOption>, Ref<WorkQueue>&&, Function<void(Vector<WebsiteDataRecord>)>&& apply);

    void platformInitialize();
    void platformDestroy();
    void platformSetNetworkParameters(WebsiteDataStoreParameters&);
    void removeRecentSearches(WallTime, CompletionHandler<void()>&&);

    WebsiteDataStore();
    static WorkQueue& websiteDataStoreIOQueueSingleton();

    // FIXME: Only Cocoa ports respect ShouldCreateDirectory, so you cannot rely on it to create
    // directories. This is confusing.
    enum class ShouldCreateDirectory : bool { No, Yes };
    static String tempDirectoryFileSystemRepresentation(const String& directoryName, ShouldCreateDirectory = ShouldCreateDirectory::Yes);
    static String cacheDirectoryFileSystemRepresentation(const String& directoryName, const String& baseCacheDirectory = nullString(), ShouldCreateDirectory = ShouldCreateDirectory::Yes);
    static String websiteDataDirectoryFileSystemRepresentation(const String& directoryName, const String& baseDataDirectory = nullString(), ShouldCreateDirectory = ShouldCreateDirectory::Yes);
    void createHandleFromResolvedPathIfPossible(const String& resolvedPath, SandboxExtension::Handle&, SandboxExtension::Type = SandboxExtension::Type::ReadWrite);

    // Will create a temporary process pool is none exists yet.
    HashSet<RefPtr<WebProcessPool>> ensureProcessPools() const;

    static Vector<WebCore::SecurityOriginData> mediaKeysStorageOrigins(const String& mediaKeysStorageDirectory);
    static void removeMediaKeysStorage(const String& mediaKeysStorageDirectory, WallTime modifiedSince);
    static void removeMediaKeysStorage(const String& mediaKeysStorageDirectory, const HashSet<WebCore::SecurityOriginData>&, const FileSystem::Salt&);

    void registerWithSessionIDMap();
    bool hasActivePages();
    bool defaultTrackingPreventionEnabled() const;

#if ENABLE(APP_BOUND_DOMAINS)
    static std::optional<HashSet<WebCore::RegistrableDomain>> appBoundDomainsIfInitialized();
    constexpr static const std::atomic<bool> isAppBoundITPRelaxationEnabled = false;
    static void forwardAppBoundDomainsToITPIfInitialized(CompletionHandler<void()>&&);
    void setAppBoundDomainsForITP(const HashSet<WebCore::RegistrableDomain>&, CompletionHandler<void()>&&);
#endif

#if ENABLE(MANAGED_DOMAINS)
    static const HashSet<WebCore::RegistrableDomain>* managedDomainsIfInitialized();
    static void forwardManagedDomainsToITPIfInitialized(CompletionHandler<void()>&&);
    void setManagedDomainsForITP(const HashSet<WebCore::RegistrableDomain>&, CompletionHandler<void()>&&);
#endif

#if PLATFORM(IOS_FAMILY)
    String resolvedContainerCachesNetworkingDirectory();
    String parentBundleDirectory() const;
#endif

    void handleResolvedDirectoriesAsynchronously(const WebsiteDataStoreConfiguration::Directories&, bool);

    HashSet<WebCore::ProcessIdentifier> activeWebProcesses() const;
    void removeDataInNetworkProcess(WebsiteDataStore::ProcessAccessType, OptionSet<WebsiteDataType>, WallTime, CompletionHandler<void()>&&);

    const PAL::SessionID m_sessionID;

    mutable Lock m_resolveDirectoriesLock;
    mutable Condition m_resolveDirectoriesCondition;
    bool m_hasDispatchedResolveDirectories { false };
    std::optional<WebsiteDataStoreConfiguration::Directories> m_resolvedDirectories WTF_GUARDED_BY_LOCK(m_resolveDirectoriesLock);
    FileSystem::Salt m_mediaKeysStorageSalt WTF_GUARDED_BY_LOCK(m_resolveDirectoriesLock);
    const Ref<const WebsiteDataStoreConfiguration> m_configuration;
    bool m_hasResolvedDirectories { false };
    const RefPtr<DeviceIdHashSaltStorage> m_deviceIdHashSaltStorage;
#if ENABLE(ENCRYPTED_MEDIA)
    const RefPtr<DeviceIdHashSaltStorage> m_mediaKeysHashSaltStorage;
#endif
#if PLATFORM(IOS_FAMILY)
    String m_resolvedContainerCachesWebContentDirectory;
    String m_resolvedContainerCachesNetworkingDirectory;
    String m_resolvedContainerTemporaryDirectory;
    String m_resolvedCookieStorageDirectory;
#endif

    bool m_trackingPreventionDebugMode { false };
    enum class TrackingPreventionEnabled : uint8_t { Default, No, Yes };
    TrackingPreventionEnabled m_trackingPreventionEnabled { TrackingPreventionEnabled::Default };
    Function<void(const String&)> m_statisticsTestingCallback;

    const Ref<WorkQueue> m_queue;

#if PLATFORM(COCOA)
    Vector<uint8_t> m_uiProcessCookieStorageIdentifier;
#endif

#if USE(CURL)
    WebCore::CurlProxySettings m_proxySettings;
#endif

#if USE(SOUP)
    bool m_persistentCredentialStorageEnabled { true };
    bool m_ignoreTLSErrors { true };
    WebCore::SoupNetworkProxySettings m_networkProxySettings;
    String m_cookiePersistentStoragePath;
    SoupCookiePersistentStorageType m_cookiePersistentStorageType { SoupCookiePersistentStorageType::SQLite };
    WebCore::HTTPCookieAcceptPolicy m_cookieAcceptPolicy { WebCore::HTTPCookieAcceptPolicy::ExclusivelyFromMainDocumentDomain };
#endif

    WeakHashSet<WebProcessProxy> m_processes;
    WeakHashSet<WebPageProxy> m_pages;

#if HAVE(SEC_KEY_PROXY)
    Vector<Ref<SecKeyProxyStore>> m_secKeyProxyStores;
#endif

#if ENABLE(WEB_AUTHN)
    Ref<AuthenticatorManager> m_authenticatorManager;
#endif

#if ENABLE(DEVICE_ORIENTATION)
    WebDeviceOrientationAndMotionAccessController m_deviceOrientationAndMotionAccessController;
#endif

    UniqueRef<WebsiteDataStoreClient> m_client;

    const RefPtr<API::HTTPCookieStore> m_cookieStore;
    RefPtr<NetworkProcessProxy> m_networkProcess;

#if HAVE(APP_SSO)
    const std::unique_ptr<SOAuthorizationCoordinator> m_soAuthorizationCoordinator;
#endif
    mutable std::optional<WebCore::ThirdPartyCookieBlockingMode> m_thirdPartyCookieBlockingMode; // Lazily computed.
    const Ref<WebCore::LocalWebLockRegistry> m_webLockRegistry;

    RefPtr<WebPreferences> m_serviceWorkerOverridePreferences;
    CompletionHandler<void(String&&)> m_completionHandlerForRemovalFromNetworkProcess;

    bool m_inspectionForServiceWorkersAllowed { true };
    bool m_isBlobRegistryPartitioningEnabled { false };
    bool m_isOptInCookiePartitioningEnabled { false };

    HashMap<WebCore::RegistrableDomain, RestrictedOpenerType> m_restrictedOpenerTypesForTesting;

#if HAVE(NW_PROXY_CONFIG)
    std::optional<Vector<std::pair<Vector<uint8_t>, std::optional<WTF::UUID>>>> m_proxyConfigData;
#endif
    bool m_storageSiteValidationEnabled { false };
    HashSet<URL> m_persistedSiteURLs;

    RemoveDataTaskCounter m_removeDataTaskCounter;
    uint64_t m_cookiesVersion { 0 };
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::WebsiteDataStore)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::WebsiteDataStore; }
SPECIALIZE_TYPE_TRAITS_END()
