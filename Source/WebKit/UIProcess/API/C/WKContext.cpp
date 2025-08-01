/*
 * Copyright (C) 2010, 2011, 2012 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WKContextPrivate.h"

#include "APIArray.h"
#include "APIClient.h"
#include "APIDownloadClient.h"
#include "APILegacyContextHistoryClient.h"
#include "APINavigationData.h"
#include "APIProcessPoolConfiguration.h"
#include "APIURLRequest.h"
#include "AuthenticationChallengeProxy.h"
#include "DownloadProxy.h"
#include "GPUProcessProxy.h"
#include "LegacyGlobalSettings.h"
#include "WKAPICast.h"
#include "WKArray.h"
#include "WKContextConfigurationRef.h"
#include "WKRetainPtr.h"
#include "WKString.h"
#include "WKWebsiteDataStoreRef.h"
#include "WebContextInjectedBundleClient.h"
#include "WebFrameProxy.h"
#include "WebPageProxy.h"
#include "WebProcessPool.h"
#include <WebCore/GamepadProvider.h>
#include <wtf/RefPtr.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/WTFString.h>

// Supplements
#include "WebGeolocationManagerProxy.h"
#include "WebNotificationManagerProxy.h"

namespace API {
template<> struct ClientTraits<WKContextDownloadClientBase> {
    typedef std::tuple<WKContextDownloadClientV0, WKContextDownloadClientV1> Versions;
};
template<> struct ClientTraits<WKContextHistoryClientBase> {
    typedef std::tuple<WKContextHistoryClientV0> Versions;
};
}

WKTypeID WKContextGetTypeID()
{
    return WebKit::toAPI(WebKit::WebProcessPool::APIType);
}

WKContextRef WKContextCreate()
{
    auto configuration = API::ProcessPoolConfiguration::create();
    return WebKit::toAPILeakingRef(WebKit::WebProcessPool::create(configuration));
}

WKContextRef WKContextCreateWithInjectedBundlePath(WKStringRef pathRef)
{
    auto configuration = API::ProcessPoolConfiguration::create();
    configuration->setInjectedBundlePath(WebKit::toWTFString(pathRef));

    return WebKit::toAPILeakingRef(WebKit::WebProcessPool::create(configuration));
}

WKContextRef WKContextCreateWithConfiguration(WKContextConfigurationRef configuration)
{
    RefPtr<API::ProcessPoolConfiguration> apiConfiguration = WebKit::toImpl(configuration);
    if (!apiConfiguration)
        apiConfiguration = API::ProcessPoolConfiguration::create();
    return WebKit::toAPILeakingRef(WebKit::WebProcessPool::create(*apiConfiguration));
}

void WKContextSetClient(WKContextRef contextRef, const WKContextClientBase* wkClient)
{
    WebKit::toProtectedImpl(contextRef)->initializeClient(wkClient);
}

void WKContextSetInjectedBundleClient(WKContextRef contextRef, const WKContextInjectedBundleClientBase* wkClient)
{
    WebKit::toProtectedImpl(contextRef)->setInjectedBundleClient(makeUnique<WebKit::WebContextInjectedBundleClient>(wkClient));
}

void WKContextSetHistoryClient(WKContextRef contextRef, const WKContextHistoryClientBase* wkClient)
{
    class HistoryClient final : public API::Client<WKContextHistoryClientBase>, public API::LegacyContextHistoryClient {
        WTF_MAKE_TZONE_ALLOCATED_INLINE(HistoryClient);
    public:
        explicit HistoryClient(const WKContextHistoryClientBase* client)
        {
            initialize(client);
        }

    private:
        void didNavigateWithNavigationData(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const WebKit::WebNavigationDataStore& navigationDataStore, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didNavigateWithNavigationData)
                return;

            RefPtr<API::NavigationData> navigationData = API::NavigationData::create(navigationDataStore);
            m_client.didNavigateWithNavigationData(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toAPI(navigationData.get()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didPerformClientRedirect(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& sourceURL, const String& destinationURL, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didPerformClientRedirect)
                return;

            m_client.didPerformClientRedirect(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toURLRef(sourceURL.impl()), WebKit::toURLRef(destinationURL.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didPerformServerRedirect(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& sourceURL, const String& destinationURL, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didPerformServerRedirect)
                return;

            m_client.didPerformServerRedirect(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toURLRef(sourceURL.impl()), WebKit::toURLRef(destinationURL.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void didUpdateHistoryTitle(WebKit::WebProcessPool& processPool, WebKit::WebPageProxy& page, const String& title, const String& url, WebKit::WebFrameProxy& frame) override
        {
            if (!m_client.didUpdateHistoryTitle)
                return;

            m_client.didUpdateHistoryTitle(WebKit::toAPI(&processPool), WebKit::toAPI(&page), WebKit::toAPI(title.impl()), WebKit::toURLRef(url.impl()), WebKit::toAPI(&frame), m_client.base.clientInfo);
        }

        void populateVisitedLinks(WebKit::WebProcessPool& processPool) override
        {
            if (!m_client.populateVisitedLinks)
                return;

            m_client.populateVisitedLinks(WebKit::toAPI(&processPool), m_client.base.clientInfo);
        }

        bool addsVisitedLinks() const override
        {
            return m_client.populateVisitedLinks;
        }
    };

    Ref processPool = *WebKit::toImpl(contextRef);
    processPool->setHistoryClient(makeUnique<HistoryClient>(wkClient));

    bool addsVisitedLinks = processPool->historyClient().addsVisitedLinks();

    for (Ref process : processPool->processes()) {
        for (Ref page : process->pages())
            page->setAddsVisitedLinks(addsVisitedLinks);
    }
}

// FIXME: Remove when rdar://133503931 is complete.
void WKContextSetDownloadClient(WKContextRef context, const WKContextDownloadClientBase* wkClient)
{
    class LegacyDownloadClient final : public API::Client<WKContextDownloadClientBase>, public API::DownloadClient {
    public:
        explicit LegacyDownloadClient(const WKContextDownloadClientBase* client, WKContextRef context)
            : m_context(context)
        {
            initialize(client);
        }
    private:
        void legacyDidStart(WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didStart)
                return;
            m_client.didStart(m_context, WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }
        void didReceiveAuthenticationChallenge(WebKit::DownloadProxy& downloadProxy, WebKit::AuthenticationChallengeProxy& authenticationChallengeProxy) final
        {
            if (!m_client.didReceiveAuthenticationChallenge)
                return;
            m_client.didReceiveAuthenticationChallenge(m_context, WebKit::toAPI(&downloadProxy), WebKit::toAPI(&authenticationChallengeProxy), m_client.base.clientInfo);
        }
        void didReceiveResponse(WebKit::DownloadProxy& downloadProxy, const WebCore::ResourceResponse& response)
        {
            if (!m_client.didReceiveResponse)
                return;
            m_client.didReceiveResponse(m_context, WebKit::toAPI(&downloadProxy), WebKit::toAPI(API::URLResponse::create(response).ptr()), m_client.base.clientInfo);
        }
        void didReceiveData(WebKit::DownloadProxy& downloadProxy, uint64_t length, uint64_t, uint64_t) final
        {
            if (!m_client.didReceiveData)
                return;
            m_client.didReceiveData(m_context, WebKit::toAPI(&downloadProxy), length, m_client.base.clientInfo);
        }
        void decideDestinationWithSuggestedFilename(WebKit::DownloadProxy& downloadProxy, const WebCore::ResourceResponse& response, const String& filename, CompletionHandler<void(WebKit::AllowOverwrite, WTF::String)>&& completionHandler) final
        {
            didReceiveResponse(downloadProxy, response);
            if (!m_client.decideDestinationWithSuggestedFilename)
                return completionHandler(WebKit::AllowOverwrite::No, { });
            bool allowOverwrite = false;
            auto destination = adoptWK(m_client.decideDestinationWithSuggestedFilename(m_context, WebKit::toAPI(&downloadProxy), WebKit::toAPI(filename.impl()), &allowOverwrite, m_client.base.clientInfo));
            completionHandler(allowOverwrite ? WebKit::AllowOverwrite::Yes : WebKit::AllowOverwrite::No, WebKit::toWTFString(destination.get()));
        }
        void didCreateDestination(WebKit::DownloadProxy& downloadProxy, const String& path) final
        {
            if (!m_client.didCreateDestination)
                return;
            m_client.didCreateDestination(m_context, WebKit::toAPI(&downloadProxy), WebKit::toAPI(path.impl()), m_client.base.clientInfo);
        }
        void didFinish(WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didFinish)
                return;
            m_client.didFinish(m_context, WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }
        void didFail(WebKit::DownloadProxy& downloadProxy, const WebCore::ResourceError& error, API::Data*) final
        {
            if (!m_client.didFail)
                return;
            m_client.didFail(m_context, WebKit::toAPI(&downloadProxy), WebKit::toAPI(error), m_client.base.clientInfo);
        }
        void legacyDidCancel(WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.didCancel)
                return;
            m_client.didCancel(m_context, WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }
        void processDidCrash(WebKit::DownloadProxy& downloadProxy) final
        {
            if (!m_client.processDidCrash)
                return;
            m_client.processDidCrash(m_context, WebKit::toAPI(&downloadProxy), m_client.base.clientInfo);
        }
        void willSendRequest(WebKit::DownloadProxy& downloadProxy, WebCore::ResourceRequest&& request, const WebCore::ResourceResponse&, CompletionHandler<void(WebCore::ResourceRequest&&)>&& completionHandler) final
        {
            if (m_client.didReceiveServerRedirect)
                m_client.didReceiveServerRedirect(m_context, WebKit::toAPI(&downloadProxy), WebKit::toURLRef(request.url().string().impl()), m_client.base.clientInfo);
            completionHandler(WTFMove(request));
        }
        WKContextRef m_context;
    };
    WebKit::toProtectedImpl(context)->setLegacyDownloadClient(adoptRef(*new LegacyDownloadClient(wkClient, context)));
}

void WKContextSetInitializationUserDataForInjectedBundle(WKContextRef contextRef,  WKTypeRef userDataRef)
{
    WebKit::toImpl(contextRef)->setInjectedBundleInitializationUserData(WebKit::toImpl(userDataRef));
}

void WKContextPostMessageToInjectedBundle(WKContextRef contextRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef)
{
    WebKit::toProtectedImpl(contextRef)->postMessageToInjectedBundle(WebKit::toProtectedImpl(messageNameRef)->string(), WebKit::toProtectedImpl(messageBodyRef).get());
}

void WKContextGetGlobalStatistics(WKContextStatistics* statistics)
{
    const WebKit::WebProcessPool::Statistics& webContextStatistics = WebKit::WebProcessPool::statistics();

    statistics->wkViewCount = webContextStatistics.wkViewCount;
    statistics->wkPageCount = webContextStatistics.wkPageCount;
    statistics->wkFrameCount = webContextStatistics.wkFrameCount;
}

void WKContextAddVisitedLink(WKContextRef contextRef, WKStringRef visitedURL)
{
    String visitedURLString = WebKit::toProtectedImpl(visitedURL)->string();
    if (visitedURLString.isEmpty())
        return;

    WebKit::toImpl(contextRef)->visitedLinkStore().addVisitedLinkHash(WebCore::computeSharedStringHash(visitedURLString));
}

void WKContextClearVisitedLinks(WKContextRef contextRef)
{
    WebKit::toImpl(contextRef)->visitedLinkStore().removeAll();
}

void WKContextSetCacheModel(WKContextRef contextRef, WKCacheModel cacheModel)
{
    WebKit::LegacyGlobalSettings::singleton().setCacheModel(WebKit::toCacheModel(cacheModel));
}

WKCacheModel WKContextGetCacheModel(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::LegacyGlobalSettings::singleton().cacheModel());
}

void WKContextSetMaximumNumberOfProcesses(WKContextRef, unsigned)
{
    // Deprecated.
}

unsigned WKContextGetMaximumNumberOfProcesses(WKContextRef)
{
    // Deprecated.
    return std::numeric_limits<unsigned>::max();
}

void WKContextSetAlwaysUsesComplexTextCodePath(WKContextRef contextRef, bool alwaysUseComplexTextCodePath)
{
    WebKit::toProtectedImpl(contextRef)->setAlwaysUsesComplexTextCodePath(alwaysUseComplexTextCodePath);
}

void WKContextSetDisableFontSubpixelAntialiasingForTesting(WKContextRef contextRef, bool disable)
{
    WebKit::toProtectedImpl(contextRef)->setDisableFontSubpixelAntialiasingForTesting(disable);
}

void WKContextSetAdditionalPluginsDirectory(WKContextRef contextRef, WKStringRef pluginsDirectory)
{
    UNUSED_PARAM(contextRef);
    UNUSED_PARAM(pluginsDirectory);
}

void WKContextRefreshPlugIns(WKContextRef context)
{
    UNUSED_PARAM(context);
}

void WKContextRegisterURLSchemeAsEmptyDocument(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->registerURLSchemeAsEmptyDocument(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsSecure(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->registerURLSchemeAsSecure(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsBypassingContentSecurityPolicy(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->registerURLSchemeAsBypassingContentSecurityPolicy(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsCachePartitioned(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->registerURLSchemeAsCachePartitioned(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextRegisterURLSchemeAsCanDisplayOnlyIfCanRequest(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->registerURLSchemeAsCanDisplayOnlyIfCanRequest(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextSetDomainRelaxationForbiddenForURLScheme(WKContextRef contextRef, WKStringRef urlScheme)
{
    WebKit::toProtectedImpl(contextRef)->setDomainRelaxationForbiddenForURLScheme(WebKit::toProtectedImpl(urlScheme)->string());
}

void WKContextSetCanHandleHTTPSServerTrustEvaluation(WKContextRef contextRef, bool value)
{
}

void WKContextSetPrewarmsProcessesAutomatically(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->configuration().setIsAutomaticProcessWarmingEnabled(value);
}

void WKContextSetUsesSingleWebProcess(WKContextRef contextRef, bool value)
{
    WebKit::toImpl(contextRef)->configuration().setUsesSingleWebProcess(value);
}

bool WKContextGetUsesSingleWebProcess(WKContextRef contextRef)
{
    return WebKit::toImpl(contextRef)->configuration().usesSingleWebProcess();
}

void WKContextSetCustomWebContentServiceBundleIdentifier(WKContextRef, WKStringRef)
{
}

void WKContextSetDiskCacheSpeculativeValidationEnabled(WKContextRef, bool)
{
}

void WKContextPreconnectToServer(WKContextRef, WKURLRef)
{
}

WKWebsiteDataStoreRef WKContextGetWebsiteDataStore(WKContextRef)
{
    return WKWebsiteDataStoreGetDefaultDataStore();
}

WKApplicationCacheManagerRef WKContextGetApplicationCacheManager(WKContextRef context)
{
    return reinterpret_cast<WKApplicationCacheManagerRef>(WKWebsiteDataStoreGetDefaultDataStore());
}

WKGeolocationManagerRef WKContextGetGeolocationManager(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toProtectedImpl(contextRef)->protectedSupplement<WebKit::WebGeolocationManagerProxy>().get());
}

WKIconDatabaseRef WKContextGetIconDatabase(WKContextRef)
{
    return nullptr;
}

WKKeyValueStorageManagerRef WKContextGetKeyValueStorageManager(WKContextRef context)
{
    return reinterpret_cast<WKKeyValueStorageManagerRef>(WKWebsiteDataStoreGetDefaultDataStore());
}

WKNotificationManagerRef WKContextGetNotificationManager(WKContextRef contextRef)
{
    return WebKit::toAPI(WebKit::toProtectedImpl(contextRef)->protectedSupplement<WebKit::WebNotificationManagerProxy>().get());
}

WKResourceCacheManagerRef WKContextGetResourceCacheManager(WKContextRef context)
{
    return reinterpret_cast<WKResourceCacheManagerRef>(WKWebsiteDataStoreGetDefaultDataStore());
}

void WKContextStartMemorySampler(WKContextRef contextRef, WKDoubleRef interval)
{
    WebKit::toProtectedImpl(contextRef)->startMemorySampler(WebKit::toImpl(interval)->value());
}

void WKContextStopMemorySampler(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->stopMemorySampler();
}

void WKContextSetIconDatabasePath(WKContextRef, WKStringRef)
{
}

void WKContextAllowSpecificHTTPSCertificateForHost(WKContextRef, WKCertificateInfoRef, WKStringRef)
{
}

void WKContextDisableProcessTermination(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->disableProcessTermination();
}

void WKContextEnableProcessTermination(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->enableProcessTermination();
}

void WKContextSetHTTPPipeliningEnabled(WKContextRef contextRef, bool enabled)
{
    WebKit::toProtectedImpl(contextRef)->setHTTPPipeliningEnabled(enabled);
}

void WKContextWarmInitialProcess(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->prewarmProcess();
}

void WKContextGetStatistics(WKContextRef contextRef, void* context, WKContextGetStatisticsFunction callback)
{
}

void WKContextGetStatisticsWithOptions(WKContextRef contextRef, WKStatisticsOptions optionsMask, void* context, WKContextGetStatisticsFunction callback)
{
}

bool WKContextJavaScriptConfigurationFileEnabled(WKContextRef contextRef)
{
    return WebKit::toImpl(contextRef)->javaScriptConfigurationFileEnabled();
}

void WKContextSetJavaScriptConfigurationFileEnabled(WKContextRef contextRef, bool enable)
{
    WebKit::toProtectedImpl(contextRef)->setJavaScriptConfigurationFileEnabled(enable);
}

void WKContextGarbageCollectJavaScriptObjects(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->garbageCollectJavaScriptObjects();
}

void WKContextSetJavaScriptGarbageCollectorTimerEnabled(WKContextRef contextRef, bool enable)
{
    WebKit::toProtectedImpl(contextRef)->setJavaScriptGarbageCollectorTimerEnabled(enable);
}

WKDictionaryRef WKContextCopyPlugInAutoStartOriginHashes(WKContextRef)
{
    return nullptr;
}

void WKContextSetPlugInAutoStartOriginHashes(WKContextRef, WKDictionaryRef)
{
}

void WKContextSetPlugInAutoStartOriginsFilteringOutEntriesAddedAfterTime(WKContextRef, WKDictionaryRef, double)
{
}

void WKContextSetPlugInAutoStartOrigins(WKContextRef, WKArrayRef)
{
}

void WKContextSetInvalidMessageFunction(WKContextInvalidMessageFunction invalidMessageFunction)
{
    WebKit::WebProcessPool::setInvalidMessageCallback(invalidMessageFunction);
}

void WKContextSetMemoryCacheDisabled(WKContextRef contextRef, bool disabled)
{
    WebKit::toProtectedImpl(contextRef)->setMemoryCacheDisabled(disabled);
}

void WKContextSetFontAllowList(WKContextRef contextRef, WKArrayRef arrayRef)
{
    WebKit::toProtectedImpl(contextRef)->setFontAllowList(WebKit::toProtectedImpl(arrayRef).get());
}

void WKContextTerminateGPUProcess(WKContextRef)
{
#if ENABLE(GPU_PROCESS)
    if (RefPtr gpuProcess = WebKit::GPUProcessProxy::singletonIfCreated())
        gpuProcess->terminateForTesting();
#endif
}

void WKContextTerminateServiceWorkers(WKContextRef context)
{
    WebKit::toProtectedImpl(context)->terminateServiceWorkers();
}

void WKContextAddSupportedPlugin(WKContextRef contextRef, WKStringRef domainRef, WKStringRef nameRef, WKArrayRef mimeTypesRef, WKArrayRef extensionsRef)
{
}

void WKContextClearSupportedPlugins(WKContextRef contextRef)
{
}

void WKContextClearCurrentModifierStateForTesting(WKContextRef contextRef)
{
    WebKit::toProtectedImpl(contextRef)->clearCurrentModifierStateForTesting();
}

void WKContextSetUseSeparateServiceWorkerProcess(WKContextRef, bool useSeparateServiceWorkerProcess)
{
    WebKit::WebProcessPool::setUseSeparateServiceWorkerProcess(useSeparateServiceWorkerProcess);
}

void WKContextSetPrimaryWebsiteDataStore(WKContextRef, WKWebsiteDataStoreRef)
{
}

WKArrayRef WKContextCopyLocalhostAliases(WKContextRef)
{
    return WebKit::toAPILeakingRef(API::Array::createStringArray(copyToVector(WebKit::LegacyGlobalSettings::singleton().hostnamesToRegisterAsLocal())));
}

void WKContextSetLocalhostAliases(WKContextRef, WKArrayRef localhostAliases)
{
    for (const auto& hostname : WebKit::toProtectedImpl(localhostAliases)->toStringVector())
        WebKit::LegacyGlobalSettings::singleton().registerHostnameAsLocal(hostname);
}

void WKContextClearMockGamepadsForTesting(WKContextRef)
{
#if ENABLE(GAMEPAD)
    if (WebCore::GamepadProvider::singleton().isMockGamepadProvider())
        WebCore::GamepadProvider::singleton().clearGamepadsForTesting();
#endif
}

void WKContextSetResourceMonitorURLsForTesting(WKContextRef contextRef, WKStringRef rulesText, void* context, WKContextSetResourceMonitorURLsFunction callback)
{
#if ENABLE(CONTENT_EXTENSIONS) && PLATFORM(COCOA)
    WebKit::toProtectedImpl(contextRef)->setResourceMonitorURLsForTesting(WebKit::toWTFString(rulesText), [context, callback] {
        if (callback)
            callback(context);
    });
#else
    if (callback)
        callback(context);
#endif
}
