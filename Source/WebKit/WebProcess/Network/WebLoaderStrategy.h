/*
 * Copyright (C) 2012, 2015 Apple Inc. All rights reserved.
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

#include "NetworkResourceLoadIdentifier.h"
#include "WebResourceLoader.h"
#include <WebCore/LoaderStrategy.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceLoader.h>
#include <WebCore/ResourceResponse.h>
#include <wtf/CheckedPtr.h>
#include <wtf/HashSet.h>
#include <wtf/RunLoop.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakRef.h>

namespace WebCore {
struct FetchOptions;
}

namespace WebKit {

class NetworkProcessConnection;
class WebFrame;
class WebPage;
class WebProcess;
class WebURLSchemeTaskProxy;

class WebLoaderStrategy final : public WebCore::LoaderStrategy {
    WTF_MAKE_TZONE_ALLOCATED(WebLoaderStrategy);
    WTF_MAKE_NONCOPYABLE(WebLoaderStrategy);
public:
    explicit WebLoaderStrategy(WebProcess&);
    ~WebLoaderStrategy() final;

    void ref() const;
    void deref() const;
    
    void loadResource(WebCore::LocalFrame&, WebCore::CachedResource&, WebCore::ResourceRequest&&, const WebCore::ResourceLoaderOptions&, CompletionHandler<void(RefPtr<WebCore::SubresourceLoader>&&)>&&) final;
    void loadResourceSynchronously(WebCore::FrameLoader&, WebCore::ResourceLoaderIdentifier, const WebCore::ResourceRequest&, WebCore::ClientCredentialPolicy, const WebCore::FetchOptions&, const WebCore::HTTPHeaderMap&, WebCore::ResourceError&, WebCore::ResourceResponse&, Vector<uint8_t>& data) final;
    void pageLoadCompleted(WebCore::Page&) final;
    void browsingContextRemoved(WebCore::LocalFrame&) final;

    void remove(WebCore::ResourceLoader*) final;
    void setDefersLoading(WebCore::ResourceLoader&, bool) final;
    void crossOriginRedirectReceived(WebCore::ResourceLoader*, const URL& redirectURL) final;
    
    void servePendingRequests(WebCore::ResourceLoadPriority minimumPriority) final;

    void suspendPendingRequests() final;
    void resumePendingRequests() final;

    bool usePingLoad() const final { return false; }
    void startPingLoad(WebCore::LocalFrame&, WebCore::ResourceRequest&, const WebCore::HTTPHeaderMap& originalRequestHeaders, const WebCore::FetchOptions&, WebCore::ContentSecurityPolicyImposition, PingLoadCompletionHandler&&) final;
    void didFinishPingLoad(WebCore::ResourceLoaderIdentifier pingLoadIdentifier, WebCore::ResourceError&&, WebCore::ResourceResponse&&);

    void preconnectTo(WebCore::ResourceRequest&&, WebPage&, WebFrame&, WebCore::StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& = nullptr);
    void preconnectTo(WebCore::FrameLoader&, WebCore::ResourceRequest&&, WebCore::StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&&) final;
    void didFinishPreconnection(WebCore::ResourceLoaderIdentifier preconnectionIdentifier, WebCore::ResourceError&&);

    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final;

    WebResourceLoader* webResourceLoaderForIdentifier(WebCore::ResourceLoaderIdentifier identifier) const { return m_webResourceLoaders.get(identifier); }
    void schedulePluginStreamLoad(WebCore::LocalFrame&, WebCore::NetscapePlugInStreamLoaderClient&, WebCore::ResourceRequest&&, CompletionHandler<void(RefPtr<WebCore::NetscapePlugInStreamLoader>&&)>&&);

    void networkProcessCrashed();

    void addURLSchemeTaskProxy(WebURLSchemeTaskProxy&);
    void removeURLSchemeTaskProxy(WebURLSchemeTaskProxy&);

    void scheduleLoadFromNetworkProcess(WebCore::ResourceLoader&, const WebCore::ResourceRequest&, const WebResourceLoader::TrackingParameters&, bool shouldClearReferrerOnHTTPSToHTTPRedirect, Seconds maximumBufferingTime);

    bool isOnLine() const final;
    void addOnlineStateChangeListener(Function<void(bool)>&&) final;
    void setOnLineState(bool);

    void setExistingNetworkResourceLoadIdentifierToResume(std::optional<NetworkResourceLoadIdentifier> existingNetworkResourceLoadIdentifierToResume) { m_existingNetworkResourceLoadIdentifierToResume = existingNetworkResourceLoadIdentifierToResume; }

    static constexpr Seconds mediaMaximumBufferingTime { 50_ms };

private:
    void scheduleLoad(WebCore::ResourceLoader&, WebCore::CachedResource*, bool shouldClearReferrerOnHTTPSToHTTPRedirect);
    void scheduleInternallyFailedLoad(WebCore::ResourceLoader&);
    void internallyFailedLoadTimerFired();
    void startLocalLoad(WebCore::ResourceLoader&);
    bool tryLoadingUsingURLSchemeHandler(WebCore::ResourceLoader&, const std::optional<WebResourceLoader::TrackingParameters>&);
#if ENABLE(PDFJS)
    bool tryLoadingUsingPDFJSHandler(WebCore::ResourceLoader&, const std::optional<WebResourceLoader::TrackingParameters>&);
#endif

    WebCore::ResourceError cancelledError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError blockedError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError blockedByContentBlockerError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError cannotShowURLError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError interruptedForPolicyChangeError(const WebCore::ResourceRequest&) const final;
#if ENABLE(CONTENT_FILTERING)
    WebCore::ResourceError blockedByContentFilterError(const WebCore::ResourceRequest&) const final;
#endif

    WebCore::ResourceError cannotShowMIMETypeError(const WebCore::ResourceResponse&) const final;
    WebCore::ResourceError fileDoesNotExistError(const WebCore::ResourceResponse&) const final;
    WebCore::ResourceError httpsUpgradeRedirectLoopError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError httpNavigationWithHTTPSOnlyError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError pluginWillHandleLoadError(const WebCore::ResourceResponse&) const final;

    struct SyncLoadResult {
        WebCore::ResourceResponse response;
        WebCore::ResourceError error;
        Vector<uint8_t> data;
    };
    std::optional<SyncLoadResult> tryLoadingSynchronouslyUsingURLSchemeHandler(WebCore::FrameLoader&, WebCore::ResourceLoaderIdentifier, const WebCore::ResourceRequest&);
    SyncLoadResult loadDataURLSynchronously(const WebCore::ResourceRequest&);

    WebCore::ResourceResponse responseFromResourceLoadIdentifier(WebCore::ResourceLoaderIdentifier) final;
    Vector<WebCore::NetworkTransactionInformation> intermediateLoadInformationFromResourceLoadIdentifier(WebCore::ResourceLoaderIdentifier) final;
    WebCore::NetworkLoadMetrics networkMetricsFromResourceLoadIdentifier(WebCore::ResourceLoaderIdentifier) final;

    bool shouldPerformSecurityChecks() const final;
    bool havePerformedSecurityChecks(const WebCore::ResourceResponse&) const final;

    void isResourceLoadFinished(WebCore::CachedResource&, CompletionHandler<void(bool)>&&) final;

    void setResourceLoadSchedulingMode(WebCore::Page&, WebCore::LoadSchedulingMode) final;
    void prioritizeResourceLoads(const Vector<WebCore::SubresourceLoader*>&) final;

    Vector<WebCore::ResourceLoaderIdentifier> ongoingLoads() const final
    {
        return WTF::map(m_webResourceLoaders, [](auto&& keyValue) -> WebCore::ResourceLoaderIdentifier {
            return keyValue.key;
        });
    }

    WeakRef<WebProcess> m_webProcess;
    HashSet<RefPtr<WebCore::ResourceLoader>> m_internallyFailedResourceLoaders;
    RunLoop::Timer m_internallyFailedLoadTimer;
    
    HashMap<WebCore::ResourceLoaderIdentifier, RefPtr<WebResourceLoader>> m_webResourceLoaders;
    HashMap<WebCore::ResourceLoaderIdentifier, WeakRef<WebURLSchemeTaskProxy>> m_urlSchemeTasks;
    HashMap<WebCore::ResourceLoaderIdentifier, PingLoadCompletionHandler> m_pingLoadCompletionHandlers;
    HashMap<WebCore::ResourceLoaderIdentifier, PreconnectCompletionHandler> m_preconnectCompletionHandlers;
    Vector<Function<void(bool)>> m_onlineStateChangeListeners;
    std::optional<NetworkResourceLoadIdentifier> m_existingNetworkResourceLoadIdentifierToResume;
    bool m_isOnLine { true };
};

} // namespace WebKit
