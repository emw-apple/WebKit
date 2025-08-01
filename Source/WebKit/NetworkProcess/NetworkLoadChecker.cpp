/*
 * Copyright (C) 2018-2025 Apple Inc. All rights reserved.
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
#include "NetworkLoadChecker.h"

#include "Download.h"
#include "Logging.h"
#include "NetworkCORSPreflightChecker.h"
#include "NetworkOriginAccessPatterns.h"
#include "NetworkProcess.h"
#include "NetworkResourceLoader.h"
#include "NetworkSchemeRegistry.h"
#include "WebPageMessages.h"
#include <WebCore/ContentRuleListResults.h>
#include <WebCore/ContentSecurityPolicy.h>
#include <WebCore/CrossOriginAccessControl.h>
#include <WebCore/CrossOriginEmbedderPolicy.h>
#include <WebCore/CrossOriginPreflightResultCache.h>
#include <WebCore/HTTPStatusCodes.h>
#include <WebCore/LegacySchemeRegistry.h>
#include <WebCore/OriginAccessPatterns.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

#define LOAD_CHECKER_RELEASE_LOG(fmt, ...) RELEASE_LOG(Network, "%p - NetworkLoadChecker::" fmt, this, ##__VA_ARGS__)
#define LOAD_CHECKER_RELEASE_LOG_WITH_THIS(thisPtr, fmt, ...) RELEASE_LOG(Network, "%p - NetworkLoadChecker::" fmt, thisPtr, ##__VA_ARGS__)

namespace WebKit {

using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(NetworkLoadChecker);

NetworkLoadChecker::NetworkLoadChecker(NetworkProcess& networkProcess, NetworkResourceLoader* networkResourceLoader, NetworkSchemeRegistry* schemeRegistry, FetchOptions&& options, PAL::SessionID sessionID, std::optional<WebPageProxyIdentifier> webPageProxyID, HTTPHeaderMap&& originalRequestHeaders, URL&& url, DocumentURL&& documentURL, RefPtr<SecurityOrigin>&& sourceOrigin, RefPtr<SecurityOrigin>&& topOrigin, RefPtr<SecurityOrigin>&& parentOrigin, PreflightPolicy preflightPolicy, String&& referrer, bool allowPrivacyProxy, OptionSet<AdvancedPrivacyProtections> advancedPrivacyProtections, bool shouldCaptureExtraNetworkLoadMetrics, LoadType requestLoadType)
    : m_options(WTFMove(options))
    , m_allowPrivacyProxy(allowPrivacyProxy)
    , m_advancedPrivacyProtections(advancedPrivacyProtections)
    , m_sessionID(sessionID)
    , m_networkProcess(networkProcess)
    , m_webPageProxyID(webPageProxyID)
    , m_originalRequestHeaders(WTFMove(originalRequestHeaders))
    , m_url(WTFMove(url))
    , m_documentURL(WTFMove(documentURL))
    , m_origin(WTFMove(sourceOrigin))
    , m_topOrigin(WTFMove(topOrigin))
    , m_parentOrigin(WTFMove(parentOrigin))
    , m_preflightPolicy(preflightPolicy)
    , m_referrer(WTFMove(referrer))
    , m_shouldCaptureExtraNetworkLoadMetrics(shouldCaptureExtraNetworkLoadMetrics)
    , m_requestLoadType(requestLoadType)
    , m_schemeRegistry(schemeRegistry)
    , m_networkResourceLoader(networkResourceLoader)
{
    if (m_requestLoadType == LoadType::MainFrame)
        m_origin = m_topOrigin;

    m_isSameOriginRequest = isSameOrigin(m_url, m_origin.get());
    switch (options.credentials) {
    case FetchOptions::Credentials::Include:
        m_storedCredentialsPolicy = StoredCredentialsPolicy::Use;
        break;
    case FetchOptions::Credentials::SameOrigin:
        m_storedCredentialsPolicy = m_isSameOriginRequest ? StoredCredentialsPolicy::Use : StoredCredentialsPolicy::DoNotUse;
        break;
    case FetchOptions::Credentials::Omit:
        m_storedCredentialsPolicy = StoredCredentialsPolicy::DoNotUse;
        break;
    }
}

NetworkLoadChecker::~NetworkLoadChecker() = default;

RefPtr<NetworkCORSPreflightChecker> NetworkLoadChecker::protectedCORSPreflightChecker() const
{
    return m_corsPreflightChecker;
}

bool NetworkLoadChecker::isSameOrigin(const URL& url, const SecurityOrigin* origin) const
{
    return url.protocolIsData()
        || url.protocolIsBlob()
        || !origin
        || origin->canRequest(url, originAccessPatterns());
}

const WebCore::OriginAccessPatterns& NetworkLoadChecker::originAccessPatterns() const
{
    if (m_networkResourceLoader)
        return m_networkResourceLoader->connectionToWebProcess().originAccessPatterns();
    return WebCore::EmptyOriginAccessPatterns::singleton();
}

void NetworkLoadChecker::check(ResourceRequest&& request, ContentSecurityPolicyClient* client, ValidationHandler&& handler)
{
    ASSERT(!isChecking());

    if (m_shouldCaptureExtraNetworkLoadMetrics)
        m_loadInformation.request = request;

    m_firstRequestHeaders = request.httpHeaderFields();
    checkRequest(WTFMove(request), client, WTFMove(handler));
}

static inline NetworkLoadChecker::RedirectionRequestOrError redirectionError(const ResourceResponse& redirectResponse, String&& errorMessage)
{
    return makeUnexpected(ResourceError { String { }, 0, redirectResponse.url(), WTFMove(errorMessage), ResourceError::Type::AccessControl });
}

void NetworkLoadChecker::checkRedirection(ResourceRequest&& request, ResourceRequest&& redirectRequest, ResourceResponse&& redirectResponse, ContentSecurityPolicyClient* client, RedirectionValidationHandler&& handler)
{
    ASSERT(!isChecking());

    auto error = validateResponse(request, redirectResponse);
    if (!error.isNull()) {
        handler(redirectionError(redirectResponse, makeString("Cross-origin redirection to "_s, redirectRequest.url().string(), " denied by Cross-Origin Resource Sharing policy: "_s, error.localizedDescription())));
        return;
    }

    if (m_options.redirect == FetchOptions::Redirect::Error) {
        handler(redirectionError(redirectResponse, makeString("Not allowed to follow a redirection while loading "_s, redirectResponse.url().string())));
        return;
    }
    if (m_options.redirect == FetchOptions::Redirect::Manual) {
        handler(RedirectionTriplet { WTFMove(request), WTFMove(redirectRequest), WTFMove(redirectResponse) });
        return;
    }

    // FIXME: We should check that redirections are only HTTP(s) as per fetch spec.
    // See https://github.com/whatwg/fetch/issues/393

    if (m_options.mode == FetchOptions::Mode::Cors && (!m_isSameOriginRequest || !isSameOrigin(request.url(), m_origin.get()))) {
        auto location = URL(redirectResponse.url(), redirectResponse.httpHeaderField(HTTPHeaderName::Location));
        if (m_schemeRegistry && !m_schemeRegistry->shouldTreatURLSchemeAsCORSEnabled(location.protocol())) {
            handler(redirectionError(redirectResponse, makeString("Cross-origin redirection to "_s, redirectRequest.url().string(), " denied by Cross-Origin Resource Sharing policy: not allowed to follow a cross-origin CORS redirection with non CORS scheme"_s)));
            return;
        }
        if (location.hasCredentials()) {
            handler(redirectionError(redirectResponse, makeString("Cross-origin redirection to "_s, redirectRequest.url().string(), " denied by Cross-Origin Resource Sharing policy: redirection URL "_s, location.string(), " has credentials"_s)));
            return;
        }
    }

    if (++m_redirectCount > 20) {
        handler(redirectionError(redirectResponse, "Load cannot follow more than 20 redirections"_s));
        return;
    }

    m_previousURL = WTFMove(m_url);
    m_url = redirectRequest.url();

    checkRequest(WTFMove(redirectRequest), client, [handler = WTFMove(handler), request = WTFMove(request), redirectResponse](auto&& result) mutable {
        WTF::switchOn(result,
            [&handler] (ResourceError& error) mutable {
                handler(makeUnexpected(WTFMove(error)));
            },
            [&handler, &request, &redirectResponse] (RedirectionTriplet& triplet) mutable {
                // FIXME: if checkRequest returns a RedirectionTriplet, it means the requested URL has changed and we should update the redirectResponse to match.
                handler(RedirectionTriplet { WTFMove(request), WTFMove(triplet.redirectRequest), WTFMove(redirectResponse) });
            },
            [&handler, &request, &redirectResponse] (ResourceRequest& redirectRequest) mutable {
                handler(RedirectionTriplet { WTFMove(request), WTFMove(redirectRequest), WTFMove(redirectResponse) });
            }
        );
    });
}

static URL contextURLforCORPViolation(NetworkResourceLoader& loader)
{
    auto& url = loader.isMainResource() ? loader.parameters().parentFrameURL : loader.parameters().frameURL;
    return url.isValid() ? url : aboutBlankURL();
}

// https://fetch.spec.whatwg.org/#cross-origin-resource-policy-check
static std::optional<ResourceError> performCORPCheck(const CrossOriginEmbedderPolicy& embedderCOEP, const SecurityOrigin& embedderOrigin, const URL& url, ResourceResponse& response, ForNavigation forNavigation, NetworkResourceLoader* loader, const WebCore::OriginAccessPatterns& patterns)
{
    if (auto error = validateCrossOriginResourcePolicy(CrossOriginEmbedderPolicyValue::UnsafeNone, embedderOrigin, url, response, forNavigation, patterns))
        return error;

    if (embedderCOEP.reportOnlyValue == CrossOriginEmbedderPolicyValue::RequireCORP && loader) {
        if (auto error = validateCrossOriginResourcePolicy(embedderCOEP.reportOnlyValue, embedderOrigin, url, response, forNavigation, patterns))
            sendCOEPCORPViolation(*loader, contextURLforCORPViolation(*loader), embedderCOEP.reportOnlyReportingEndpoint, COEPDisposition::Reporting, loader->parameters().options.destination, loader->firstResponseURL());
    }

    if (embedderCOEP.value == CrossOriginEmbedderPolicyValue::RequireCORP) {
        if (auto error = validateCrossOriginResourcePolicy(embedderCOEP.value, embedderOrigin, url, response, forNavigation, patterns)) {
            if (loader)
                sendCOEPCORPViolation(*loader, contextURLforCORPViolation(*loader), embedderCOEP.reportingEndpoint, COEPDisposition::Enforce, loader->parameters().options.destination, loader->firstResponseURL());
            return error;
        }
    }
    return std::nullopt;
}

ResourceError NetworkLoadChecker::validateResponse(const ResourceRequest& request, ResourceResponse& response)
{
    if (response.containsInvalidHTTPHeaders())
        return badResponseHeadersError(request.url());

    auto scope = makeScopeExit([this, protectedThis = Ref { *this }, &response] {
        if (!checkTAO(response)) {
            if (auto metrics = response.takeNetworkLoadMetrics()) {
                metrics->failsTAOCheck = true;
                response.setDeprecatedNetworkLoadMetrics(WTFMove(metrics));
            }
        }
    });

    if (m_redirectCount)
        response.setRedirected(true);

    if (response.type() == ResourceResponse::Type::Opaqueredirect) {
        response.setTainting(ResourceResponse::Tainting::Opaqueredirect);
        return { };
    }

    if (m_options.mode == FetchOptions::Mode::Navigate || m_isSameOriginRequest) {
        if (RefPtr parentOrigin = m_parentOrigin; parentOrigin && m_options.mode == FetchOptions::Mode::Navigate) {
            if (auto error = performCORPCheck(m_parentCrossOriginEmbedderPolicy, *parentOrigin, m_url, response, ForNavigation::Yes, RefPtr { m_networkResourceLoader.get() }.get(), originAccessPatterns()))
                return WTFMove(*error);
        }
        response.setTainting(ResourceResponse::Tainting::Basic);
        return { };
    }

    if (request.hasHTTPHeaderField(HTTPHeaderName::Range))
        response.setAsRangeRequested();

    if (m_options.mode == FetchOptions::Mode::NoCors) {
        if (auto error = performCORPCheck(m_crossOriginEmbedderPolicy, *origin(), m_url, response, ForNavigation::No, RefPtr { m_networkResourceLoader.get() }.get(), originAccessPatterns()))
            return WTFMove(*error);

        response.setTainting(ResourceResponse::Tainting::Opaque);
        return { };
    }

    ASSERT(m_options.mode == FetchOptions::Mode::Cors);

    // If we have a 304, the cached response is in WebProcess so we let WebProcess do the CORS check on the cached response.
    if (response.httpStatusCode() == httpStatus304NotModified) {
        response.setTainting(ResourceResponse::Tainting::Cors);
        return { };
    }

    auto result = passesAccessControlCheck(response, m_storedCredentialsPolicy, *origin(), m_networkResourceLoader.get());
    if (!result)
        return ResourceError { String { }, 0, m_url, WTFMove(result.error()), ResourceError::Type::AccessControl };

    response.setTainting(ResourceResponse::Tainting::Cors);
    return { };
}

// https://fetch.spec.whatwg.org/#concept-tao-check
bool NetworkLoadChecker::checkTAO(const ResourceResponse& response)
{
    if (m_timingAllowFailedFlag)
        return false;

    if (m_origin) {
        const auto& timingAllowOriginString = response.httpHeaderField(HTTPHeaderName::TimingAllowOrigin);
        for (auto originWithSpace : StringView(timingAllowOriginString).split(',')) {
            auto origin = originWithSpace.trim(isASCIIWhitespaceWithoutFF<char16_t>);
            if (origin == "*"_s || origin == protectedOrigin()->toString())
                return true;
        }
    }

    if (m_options.mode == FetchOptions::Mode::Navigate && !m_isSameOriginRequest) {
        m_timingAllowFailedFlag = true;
        return false;
    }

    m_timingAllowFailedFlag = response.tainting() != ResourceResponse::Tainting::Basic;
    return !m_timingAllowFailedFlag;
}

auto NetworkLoadChecker::accessControlErrorForValidationHandler(String&& message) -> RequestOrRedirectionTripletOrError
{
    return ResourceError { String { }, 0, m_url, WTFMove(message), ResourceError::Type::AccessControl };
}

void NetworkLoadChecker::checkRequest(ResourceRequest&& request, ContentSecurityPolicyClient* client, ValidationHandler&& handler)
{
    ResourceRequest originalRequest = request;

    if (CheckedPtr contentSecurityPolicy = this->contentSecurityPolicy()) {
        if (this->isRedirected()) {
            auto type = m_options.mode == FetchOptions::Mode::Navigate ? ContentSecurityPolicy::InsecureRequestType::Navigation : ContentSecurityPolicy::InsecureRequestType::Load;
            contentSecurityPolicy->upgradeInsecureRequestIfNeeded(request, type);
        }

        if (!this->isAllowedByContentSecurityPolicy(request, client)) {
            contentSecurityPolicy = nullptr;
            handler(this->accessControlErrorForValidationHandler("Blocked by Content Security Policy."_s));
            return;
        }
    }

#if ENABLE(CONTENT_EXTENSIONS)
    this->processContentRuleListsForLoad(WTFMove(request), [weakThis = WeakPtr { *this }, handler = WTFMove(handler), originalRequest = WTFMove(originalRequest)](auto&& result) mutable {
        if (!result.has_value()) {
            ASSERT(result.error().isCancellation());
            handler(WTFMove(result.error()));
            return;
        }

        if (!weakThis)
            return handler({ ResourceError { ResourceError::Type::Cancellation }});

        if (result.value().results.shouldBlock()) {
            handler(weakThis->accessControlErrorForValidationHandler("Blocked by content extension"_s));
            return;
        }

        weakThis->continueCheckingRequestOrDoSyntheticRedirect(WTFMove(originalRequest), WTFMove(result.value().request), WTFMove(handler));
    });
#else
    this->continueCheckingRequestOrDoSyntheticRedirect(WTFMove(originalRequest), WTFMove(request), WTFMove(handler));
#endif
}

void NetworkLoadChecker::continueCheckingRequestOrDoSyntheticRedirect(ResourceRequest&& originalRequest, ResourceRequest&& currentRequest, ValidationHandler&& handler)
{
    // If main frame load and request has been modified, trigger a synthetic redirect.
    if (m_requestLoadType == LoadType::MainFrame && currentRequest.url() != originalRequest.url()) {
        ResourceResponse redirectResponse = ResourceResponse::syntheticRedirectResponse(originalRequest.url(), currentRequest.url());
        handler(RedirectionTriplet { WTFMove(originalRequest), WTFMove(currentRequest), WTFMove(redirectResponse) });
        return;
    }
    this->continueCheckingRequest(WTFMove(currentRequest), WTFMove(handler));
}

bool NetworkLoadChecker::isAllowedByContentSecurityPolicy(const ResourceRequest& request, WebCore::ContentSecurityPolicyClient* client)
{
    CheckedPtr contentSecurityPolicy = this->contentSecurityPolicy();
    contentSecurityPolicy->setClient(client);
    auto clearContentSecurityPolicyClient = makeScopeExit([&] {
        contentSecurityPolicy->setClient(nullptr);
    });

    auto preRedirectURL = m_networkResourceLoader ? m_networkResourceLoader->originalRequest().url() : URL();
    auto redirectResponseReceived = isRedirected() ? ContentSecurityPolicy::RedirectResponseReceived::Yes : ContentSecurityPolicy::RedirectResponseReceived::No;
    switch (m_options.destination) {
    case FetchOptions::Destination::Audioworklet:
    case FetchOptions::Destination::Paintworklet:
    case FetchOptions::Destination::Worker:
    case FetchOptions::Destination::Serviceworker:
    case FetchOptions::Destination::Sharedworker:
        return contentSecurityPolicy->allowWorkerFromSource(request.url(), redirectResponseReceived, preRedirectURL);
    case FetchOptions::Destination::Script:
        if (request.requester() == ResourceRequestRequester::ImportScripts && !contentSecurityPolicy->allowScriptFromSource(request.url(), redirectResponseReceived, preRedirectURL))
            return false;
        // FIXME: Check CSP for non-importScripts() initiated loads.
        return true;
    case FetchOptions::Destination::EmptyString:
        return contentSecurityPolicy->allowConnectToSource(request.url(), redirectResponseReceived, preRedirectURL);
    case FetchOptions::Destination::Audio:
    case FetchOptions::Destination::Document:
    case FetchOptions::Destination::Embed:
    case FetchOptions::Destination::Environmentmap:
    case FetchOptions::Destination::Font:
    case FetchOptions::Destination::Image:
    case FetchOptions::Destination::Iframe:
    case FetchOptions::Destination::Manifest:
    case FetchOptions::Destination::Model:
    case FetchOptions::Destination::Object:
    case FetchOptions::Destination::Report:
    case FetchOptions::Destination::Style:
    case FetchOptions::Destination::Track:
    case FetchOptions::Destination::Video:
    case FetchOptions::Destination::Xslt:
        // FIXME: Check CSP for these destinations.
        return true;
    }
    ASSERT_NOT_REACHED();
    return true;
}

void NetworkLoadChecker::continueCheckingRequest(ResourceRequest&& request, ValidationHandler&& handler)
{
    if (m_options.credentials == FetchOptions::Credentials::SameOrigin)
        m_storedCredentialsPolicy = m_isSameOriginRequest && protectedOrigin()->canRequest(request.url(), originAccessPatterns()) ? StoredCredentialsPolicy::Use : StoredCredentialsPolicy::DoNotUse;

    m_isSameOriginRequest = m_isSameOriginRequest && isSameOrigin(request.url(), m_origin.get());

    if (doesNotNeedCORSCheck(request.url())) {
        handler(WTFMove(request));
        return;
    }

    if (m_options.mode == FetchOptions::Mode::SameOrigin) {
        handler(accessControlErrorForValidationHandler(makeString("Unsafe attempt to load URL "_s, request.url().stringCenterEllipsizedToLength(), " from origin "_s, protectedOrigin()->toString(), ". Domains, protocols and ports must match.\n"_s)));
        return;
    }

    if (isRedirected()) {
        LOAD_CHECKER_RELEASE_LOG("checkRequest - Redirect requires CORS checks");
        checkCORSRedirectedRequest(WTFMove(request), WTFMove(handler));
        return;
    }

    checkCORSRequest(WTFMove(request), WTFMove(handler));
}

void NetworkLoadChecker::checkCORSRequest(ResourceRequest&& request, ValidationHandler&& handler)
{
    ASSERT(m_options.mode == FetchOptions::Mode::Cors);

    // Except in case where preflight is needed, loading should be able to continue on its own.
    switch (m_preflightPolicy) {
    case PreflightPolicy::Force:
        checkCORSRequestWithPreflight(WTFMove(request), WTFMove(handler));
        break;
    case PreflightPolicy::Consider:
        if (!m_isSimpleRequest || !isSimpleCrossOriginAccessRequest(request.httpMethod(), m_originalRequestHeaders)) {
            checkCORSRequestWithPreflight(WTFMove(request), WTFMove(handler));
            return;
        }
        [[fallthrough]];
    case PreflightPolicy::Prevent:
        updateRequestForAccessControl(request, *origin(), m_storedCredentialsPolicy);
        handler(WTFMove(request));
        break;
    }
}

void NetworkLoadChecker::checkCORSRedirectedRequest(ResourceRequest&& request, ValidationHandler&& handler)
{
    ASSERT(m_options.mode == FetchOptions::Mode::Cors);
    ASSERT(isRedirected());

    // Force any subsequent request to use these checks.
    m_isSameOriginRequest = false;

    if (!protectedOrigin()->canRequest(m_previousURL, originAccessPatterns()) && !protocolHostAndPortAreEqual(m_previousURL, request.url())) {
        // Use an opaque origin for subsequent loads if needed.
        // https://fetch.spec.whatwg.org/#concept-http-redirect-fetch (Step 10).
        if (!m_origin || !m_origin->isOpaque())
            m_origin = SecurityOrigin::createOpaque();
    }

    // FIXME: We should set the request referrer according the referrer policy.

    // Let's fetch the request with the original headers (equivalent to request cloning specified by fetch algorithm).
    if (!request.httpHeaderFields().contains(HTTPHeaderName::Authorization))
        m_firstRequestHeaders.remove(HTTPHeaderName::Authorization);
    request.setHTTPHeaderFields(m_firstRequestHeaders);

    checkCORSRequest(WTFMove(request), WTFMove(handler));
}

void NetworkLoadChecker::checkCORSRequestWithPreflight(ResourceRequest&& request, ValidationHandler&& handler)
{
    ASSERT(m_options.mode == FetchOptions::Mode::Cors);

    m_isSimpleRequest = false;
    if (CrossOriginPreflightResultCache::singleton().canSkipPreflight(m_sessionID, { m_topOrigin->data(), m_origin->data() }, request.url(), m_storedCredentialsPolicy, request.httpMethod(), m_originalRequestHeaders)) {
        LOAD_CHECKER_RELEASE_LOG("checkCORSRequestWithPreflight - preflight can be skipped thanks to cached result");
        updateRequestForAccessControl(request, *origin(), m_storedCredentialsPolicy);
        handler(WTFMove(request));
        return;
    }

    auto requestForPreflight = request;
    // We need to set header fields to m_originalRequestHeaders to correctly compute Access-Control-Request-Headers header value.
    requestForPreflight.setHTTPHeaderFields(m_originalRequestHeaders);
    NetworkCORSPreflightChecker::Parameters parameters = {
        WTFMove(requestForPreflight),
        *m_origin,
        *m_topOrigin,
        request.httpReferrer(),
        request.httpUserAgent(),
        m_sessionID,
        m_webPageProxyID,
        m_storedCredentialsPolicy,
        m_allowPrivacyProxy,
        m_advancedPrivacyProtections,
        request.hasHTTPHeaderField(HTTPHeaderName::SecFetchSite)
    };
    RefPtr networkResourceLoader = m_networkResourceLoader.get();
    m_corsPreflightChecker = NetworkCORSPreflightChecker::create(m_networkProcess.get(), networkResourceLoader.get(), WTFMove(parameters), m_shouldCaptureExtraNetworkLoadMetrics, [weakThis = WeakPtr { *this }, request = WTFMove(request), handler = WTFMove(handler), isRedirected = isRedirected()](auto&& error) mutable {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis) {
            handler(WTFMove(request));
            return;
        }

        LOAD_CHECKER_RELEASE_LOG_WITH_THIS(protectedThis.get(), "checkCORSRequestWithPreflight - makeCrossOriginAccessRequestWithPreflight preflight complete, success=%d forRedirect=%d", error.isNull(), isRedirected);

        if (!error.isNull()) {
            handler(WTFMove(error));
            return;
        }

        if (protectedThis->m_shouldCaptureExtraNetworkLoadMetrics)
            protectedThis->m_loadInformation.transactions.append(protectedThis->protectedCORSPreflightChecker()->takeInformation());

        auto corsPreflightChecker = std::exchange(protectedThis->m_corsPreflightChecker, nullptr);
        updateRequestForAccessControl(request, *protectedThis->origin(), protectedThis->m_storedCredentialsPolicy);
        handler(WTFMove(request));
    });
    protectedCORSPreflightChecker()->startPreflight();
}

bool NetworkLoadChecker::doesNotNeedCORSCheck(const URL& url) const
{
    if (m_options.mode == FetchOptions::Mode::NoCors || m_options.mode == FetchOptions::Mode::Navigate)
        return true;

    if (m_schemeRegistry && !m_schemeRegistry->shouldTreatURLSchemeAsCORSEnabled(url.protocol()))
        return true;

    return m_isSameOriginRequest;
}

ContentSecurityPolicy* NetworkLoadChecker::contentSecurityPolicy()
{
    if (!m_contentSecurityPolicy && m_cspResponseHeaders) {
        // FIXME: Pass the URL of the protected resource instead of its origin.
        m_contentSecurityPolicy = makeUnique<ContentSecurityPolicy>(URL { protectedOrigin()->toRawString() }, nullptr, m_networkResourceLoader.get());
        CheckedPtr { m_contentSecurityPolicy.get() }->didReceiveHeaders(*m_cspResponseHeaders, String { m_referrer }, ContentSecurityPolicy::ReportParsingErrors::No);
        if (!m_documentURL.isEmpty())
            m_contentSecurityPolicy->setDocumentURL(m_documentURL);
    }
    return m_contentSecurityPolicy.get();
}

#if ENABLE(CONTENT_EXTENSIONS)
void NetworkLoadChecker::processContentRuleListsForLoad(ResourceRequest&& request, ContentExtensionCallback&& callback)
{
    // FIXME: Enable content blockers for navigation loads.
    if (!m_checkContentExtensions || !m_userContentControllerIdentifier || m_options.mode == FetchOptions::Mode::Navigate) {
        ContentRuleListResults results;
        callback(ContentExtensionResult { WTFMove(request), results });
        return;
    }

    m_networkProcess->protectedNetworkContentRuleListManager()->contentExtensionsBackend(*m_userContentControllerIdentifier, [weakThis = WeakPtr { *this }, request = WTFMove(request), callback = WTFMove(callback)](auto& backend) mutable {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis) {
            callback(makeUnexpected(ResourceError { ResourceError::Type::Cancellation }));
            return;
        }

        auto results = backend.processContentRuleListsForPingLoad(request.url(), protectedThis->m_mainDocumentURL, protectedThis->m_frameURL, request.httpMethod());
        WebCore::ContentExtensions::applyResultsToRequest(ContentRuleListResults { results }, nullptr, request);
        callback(ContentExtensionResult { WTFMove(request), results });
    });
}
#endif // ENABLE(CONTENT_EXTENSIONS)

void NetworkLoadChecker::storeRedirectionIfNeeded(const ResourceRequest& request, const ResourceResponse& response)
{
    if (!m_shouldCaptureExtraNetworkLoadMetrics)
        return;
    m_loadInformation.transactions.append(NetworkTransactionInformation { NetworkTransactionInformation::Type::Redirection, ResourceRequest { request }, ResourceResponse { response }, { } });
}

Ref<NetworkProcess> NetworkLoadChecker::protectedNetworkProcess()
{
    return m_networkProcess;
}

} // namespace WebKit

#undef LOAD_CHECKER_RELEASE_LOG
