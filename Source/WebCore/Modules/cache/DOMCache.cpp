/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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
#include "DOMCache.h"

#include "CacheQueryOptions.h"
#include "CachedResourceRequestInitiatorTypes.h"
#include "EventLoop.h"
#include "FetchResponse.h"
#include "HTTPParsers.h"
#include "JSDOMPromiseDeferred.h"
#include "JSFetchRequest.h"
#include "JSFetchResponse.h"
#include "ScriptExecutionContextInlines.h"
#include <wtf/CompletionHandler.h>
#include <wtf/URL.h>

namespace WebCore {
using namespace WebCore::DOMCacheEngine;

Ref<DOMCache> DOMCache::create(ScriptExecutionContext& context, String&& name, DOMCacheIdentifier identifier, Ref<CacheStorageConnection>&& connection)
{
    auto cache = adoptRef(*new DOMCache(context, WTFMove(name), identifier, WTFMove(connection)));
    cache->suspendIfNeeded();
    return cache;
}

DOMCache::DOMCache(ScriptExecutionContext& context, String&& name, DOMCacheIdentifier identifier, Ref<CacheStorageConnection>&& connection)
    : ActiveDOMObject(&context)
    , m_name(WTFMove(name))
    , m_identifier(identifier)
    , m_connection(WTFMove(connection))
{
    m_connection->reference(m_identifier);
}

DOMCache::~DOMCache()
{
    if (!m_isStopped)
        m_connection->dereference(m_identifier);
}

void DOMCache::match(RequestInfo&& info, CacheQueryOptions&& options, Ref<DeferredPromise>&& promise)
{
    doMatch(WTFMove(info), WTFMove(options), [this, protectedThis = Ref { *this }, promise = WTFMove(promise)](ExceptionOr<RefPtr<FetchResponse>>&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto&) mutable {
            if (result.hasException()) {
                promise->reject(result.releaseException());
                return;
            }
            RefPtr value = result.returnValue();
            if (!value) {
                promise->resolve();
                return;
            }
            promise->resolve<IDLInterface<FetchResponse>>(*value);
        });
    });
}

static Ref<FetchResponse> createResponse(ScriptExecutionContext& context, const DOMCacheEngine::Record& record, MonotonicTime requestStart)
{
    auto resourceResponse = record.response;
    resourceResponse.setSource(ResourceResponse::Source::DOMCache);

    auto metrics = Box<NetworkLoadMetrics>::create();
    metrics->requestStart = requestStart;
    metrics->responseStart = MonotonicTime::now();
    resourceResponse.setDeprecatedNetworkLoadMetrics(WTFMove(metrics));

    auto response = FetchResponse::create(&context, std::nullopt, record.responseHeadersGuard, WTFMove(resourceResponse));
    response->setBodyData(copyResponseBody(record.responseBody), record.responseBodySize);
    return response;
}

void DOMCache::doMatch(RequestInfo&& info, CacheQueryOptions&& options, MatchCallback&& callback)
{
    if (!scriptExecutionContext()) [[unlikely]]
        return;

    bool requestValidationFailed = false;
    auto requestOrException = requestFromInfo(WTFMove(info), options.ignoreMethod, &requestValidationFailed);
    if (requestOrException.hasException()) {
        if (requestValidationFailed)
            callback(nullptr);
        else
            callback(requestOrException.releaseException());
        return;
    }

    auto request = requestOrException.releaseReturnValue()->resourceRequest();
    auto requestStart = MonotonicTime::now();
    queryCache(WTFMove(request), options, ShouldRetrieveResponses::Yes, [this, callback = WTFMove(callback), requestStart](auto&& result) mutable {
        if (result.hasException()) {
            callback(result.releaseException());
            return;
        }

        RefPtr<FetchResponse> response;
        if (!result.returnValue().isEmpty())
            response = createResponse(*scriptExecutionContext(), result.returnValue()[0], requestStart);

        callback(WTFMove(response));
    });
}

Vector<Ref<FetchResponse>> DOMCache::cloneResponses(const Vector<DOMCacheEngine::Record>& records, MonotonicTime requestStart)
{
    return WTF::map(records, [this, requestStart] (const auto& record) {
        return createResponse(*scriptExecutionContext(), record, requestStart);
    });
}

void DOMCache::matchAll(std::optional<RequestInfo>&& info, CacheQueryOptions&& options, MatchAllPromise&& promise)
{
    if (!scriptExecutionContext()) [[unlikely]]
        return;

    ResourceRequest resourceRequest;
    if (info) {
        bool requestValidationFailed = false;
        auto requestOrException = requestFromInfo(WTFMove(info.value()), options.ignoreMethod, &requestValidationFailed);
        if (requestOrException.hasException()) {
            if (requestValidationFailed)
                promise.resolve({ });
            else
                promise.reject(requestOrException.releaseException());
            return;
        }
        resourceRequest = requestOrException.releaseReturnValue()->resourceRequest();
    }

    auto requestStart = MonotonicTime::now();
    queryCache(WTFMove(resourceRequest), options, ShouldRetrieveResponses::Yes, [this, promise = WTFMove(promise), requestStart]<typename Result> (Result&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = std::forward<Result>(result), requestStart](auto& cache) mutable {
            if (result.hasException()) {
                promise.reject(result.releaseException());
                return;
            }
            promise.resolve(cache.cloneResponses(result.releaseReturnValue(), requestStart));
        });
    });
}

void DOMCache::add(RequestInfo&& info, DOMPromiseDeferred<void>&& promise)
{
    addAll(Vector<RequestInfo> { WTFMove(info) }, WTFMove(promise));
}

static inline bool hasResponseVaryStarHeaderValue(const FetchResponse& response)
{
    auto varyValue = response.headers().internalHeaders().get(WebCore::HTTPHeaderName::Vary);
    bool hasStar = false;
    varyValue.split(',', [&](StringView view) {
        if (!hasStar && view.trim(isASCIIWhitespaceWithoutFF<char16_t>) == "*"_s)
            hasStar = true;
    });
    return hasStar;
}

class FetchTasksHandler : public RefCounted<FetchTasksHandler> {
public:
    static Ref<FetchTasksHandler> create(Ref<DOMCache>&& domCache, CompletionHandler<void(ExceptionOr<Vector<Record>>&&)>&& callback) { return adoptRef(*new FetchTasksHandler(WTFMove(domCache), WTFMove(callback))); }

    ~FetchTasksHandler()
    {
        if (m_callback)
            m_callback(WTFMove(m_records));
    }

    const Vector<Record>& records() const { return m_records; }

    size_t addRecord(Record&& record)
    {
        ASSERT(!isDone());
        m_records.append(WTFMove(record));
        return m_records.size() - 1;
    }

    void addResponseBody(size_t position, FetchResponse& response, DOMCacheEngine::ResponseBody&& data)
    {
        ASSERT(!isDone());
        auto& record = m_records[position];
        record.responseBodySize = m_domCache->connection().computeRecordBodySize(response, data);
        record.responseBody = WTFMove(data);
    }

    bool isDone() const { return !m_callback; }

    void error(Exception&& exception)
    {
        if (auto callback = WTFMove(m_callback))
            callback(WTFMove(exception));
    }

private:
    FetchTasksHandler(Ref<DOMCache>&& domCache, CompletionHandler<void(ExceptionOr<Vector<Record>>&&)>&& callback)
        : m_domCache(WTFMove(domCache))
        , m_callback(WTFMove(callback))
    {
    }

    const Ref<DOMCache> m_domCache;
    Vector<Record> m_records;
    CompletionHandler<void(ExceptionOr<Vector<Record>>&&)> m_callback;
};

ExceptionOr<Ref<FetchRequest>> DOMCache::requestFromInfo(RequestInfo&& info, bool ignoreMethod, bool* requestValidationFailed)
{
    RefPtr<FetchRequest> request;
    if (std::holds_alternative<RefPtr<FetchRequest>>(info)) {
        request = std::get<RefPtr<FetchRequest>>(info).releaseNonNull();
        if (request->method() != "GET"_s && !ignoreMethod) {
            if (requestValidationFailed)
                *requestValidationFailed = true;
            return Exception { ExceptionCode::TypeError, "Request method is not GET"_s };
        }
    } else {
        auto result = FetchRequest::create(*protectedScriptExecutionContext(), WTFMove(info), { });
        if (result.hasException())
            return result.releaseException();
        request = result.releaseReturnValue();
    }

    if (!request->url().protocolIsInHTTPFamily()) {
        if (requestValidationFailed)
            *requestValidationFailed = true;
        return Exception { ExceptionCode::TypeError, "Request url is not HTTP/HTTPS"_s };
    }

    return request.releaseNonNull();
}

void DOMCache::addAll(Vector<RequestInfo>&& infos, DOMPromiseDeferred<void>&& promise)
{
    RefPtr scriptExecutionContext = this->scriptExecutionContext();
    if (!scriptExecutionContext) [[unlikely]]
        return;

    Vector<Ref<FetchRequest>> requests;
    requests.reserveInitialCapacity(infos.size());
    for (auto& info : infos) {
        bool ignoreMethod = false;
        auto requestOrException = requestFromInfo(WTFMove(info), ignoreMethod);
        if (requestOrException.hasException()) {
            promise.reject(requestOrException.releaseException());
            return;
        }
        requests.append(requestOrException.releaseReturnValue());
    }

    auto taskHandler = FetchTasksHandler::create(*this, [this, protectedThis = Ref { *this }, promise = WTFMove(promise)](ExceptionOr<Vector<Record>>&& result) mutable {
        if (result.hasException()) {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = result.releaseException()](auto&) mutable {
                promise.reject(WTFMove(exception));
            });
            return;
        }
        batchPutOperation(result.releaseReturnValue(), [this, protectedThis = WTFMove(protectedThis), promise = WTFMove(promise)](ExceptionOr<void>&& result) mutable {
            queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto&) mutable {
                promise.settle(WTFMove(result));
            });
        });
    });

    for (Ref request : requests) {
        if (request->signal().aborted()) {
            taskHandler->error(Exception { ExceptionCode::AbortError, "Request signal is aborted"_s });
            return;
        }
        FetchResponse::fetch(*scriptExecutionContext, request.get(), [this, request, taskHandler](auto&& result) mutable {

            if (taskHandler->isDone())
                return;

            if (result.hasException()) {
                taskHandler->error(result.releaseException());
                return;
            }

            auto protectedResponse = result.releaseReturnValue();
            auto& response = protectedResponse.get();

            if (!response.ok()) {
                taskHandler->error(Exception { ExceptionCode::TypeError, "Response is not OK"_s });
                return;
            }

            if (hasResponseVaryStarHeaderValue(response)) {
                taskHandler->error(Exception { ExceptionCode::TypeError, "Response has a '*' Vary header value"_s });
                return;
            }

            if (response.status() == 206) {
                taskHandler->error(Exception { ExceptionCode::TypeError, "Response is a 206 partial"_s });
                return;
            }

            CacheQueryOptions options;
            for (const auto& record : taskHandler->records()) {
                if (DOMCacheEngine::queryCacheMatch(request->resourceRequest(), record.request, record.response, options)) {
                    taskHandler->error(Exception { ExceptionCode::InvalidStateError, "addAll cannot store several matching requests"_s });
                    return;
                }
            }
            size_t recordPosition = taskHandler->addRecord(toConnectionRecord(request.get(), response, nullptr));

            response.consumeBodyReceivedByChunk([taskHandler = WTFMove(taskHandler), recordPosition, data = SharedBufferBuilder(), response = WTFMove(protectedResponse)] (auto&& result) mutable {
                if (taskHandler->isDone())
                    return;

                if (result.hasException()) {
                    taskHandler->error(result.releaseException());
                    return;
                }

                if (auto* chunk = result.returnValue())
                    data.append(*chunk);
                else
                    taskHandler->addResponseBody(recordPosition, response, data.takeAsContiguous());
            });
        }, cachedResourceRequestInitiatorTypes().fetch);
    }
}

void DOMCache::putWithResponseData(DOMPromiseDeferred<void>&& promise, Ref<FetchRequest>&& request, Ref<FetchResponse>&& response, ExceptionOr<RefPtr<SharedBuffer>>&& responseBody)
{
    if (responseBody.hasException()) {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), exception = responseBody.releaseException()](auto&) mutable {
            promise.reject(WTFMove(exception));
        });
        return;
    }

    DOMCacheEngine::ResponseBody body;
    if (auto buffer = responseBody.releaseReturnValue())
        body = buffer->makeContiguous();
    batchPutOperation(request.get(), response.get(), WTFMove(body), [this, protectedThis = Ref { *this }, promise = WTFMove(promise)](ExceptionOr<void>&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto&) mutable {
            promise.settle(WTFMove(result));
        });
    });
}

void DOMCache::put(RequestInfo&& info, Ref<FetchResponse>&& response, DOMPromiseDeferred<void>&& promise)
{
    if (isContextStopped())
        return;

    bool ignoreMethod = false;
    auto requestOrException = requestFromInfo(WTFMove(info), ignoreMethod);
    if (requestOrException.hasException()) {
        promise.reject(requestOrException.releaseException());
        return;
    }
    auto request = requestOrException.releaseReturnValue();

    if (auto exception = response->loadingException()) {
        promise.reject(*exception);
        return;
    }

    if (hasResponseVaryStarHeaderValue(response.get())) {
        promise.reject(Exception { ExceptionCode::TypeError, "Response has a '*' Vary header value"_s });
        return;
    }

    if (response->status() == 206) {
        promise.reject(Exception { ExceptionCode::TypeError, "Response is a 206 partial"_s });
        return;
    }

    if (response->isDisturbedOrLocked()) {
        promise.reject(Exception { ExceptionCode::TypeError, "Response is disturbed or locked"_s });
        return;
    }

    // FIXME: for efficiency, we should load blobs/form data directly instead of going through the readableStream path.
    if (response->isBlobBody() || response->isBlobFormData()) {
        auto streamOrException = response->readableStream(*protectedScriptExecutionContext()->globalObject());
        if (streamOrException.hasException()) [[unlikely]] {
            promise.reject(streamOrException.releaseException());
            return;
        }
    }

    if (response->isBodyReceivedByChunk()) {
        auto& responseRef = response.get();
        responseRef.consumeBodyReceivedByChunk([promise = WTFMove(promise), request = WTFMove(request), response = WTFMove(response), data = SharedBufferBuilder(), pendingActivity = makePendingActivity(*this)](auto&& result) mutable {

            if (result.hasException()) {
                pendingActivity->object().putWithResponseData(WTFMove(promise), WTFMove(request), WTFMove(response), result.releaseException().isolatedCopy());
                return;
            }

            if (auto* chunk = result.returnValue())
                data.append(*chunk);
            else
                pendingActivity->object().putWithResponseData(WTFMove(promise), WTFMove(request), WTFMove(response), RefPtr<SharedBuffer> { data.takeAsContiguous() });
        });
        return;
    }

    batchPutOperation(request.get(), response.get(), response->consumeBody(), [this, protectedThis = Ref { *this }, promise = WTFMove(promise)](ExceptionOr<void>&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto&) mutable {
            promise.settle(WTFMove(result));
        });
    });
}

void DOMCache::remove(RequestInfo&& info, CacheQueryOptions&& options, DOMPromiseDeferred<IDLBoolean>&& promise)
{
    if (!scriptExecutionContext()) [[unlikely]]
        return;

    auto requestOrException = requestFromInfo(WTFMove(info), options.ignoreMethod);
    if (requestOrException.hasException()) {
        promise.resolve(false);
        return;
    }

    batchDeleteOperation(requestOrException.releaseReturnValue(), WTFMove(options), [this, protectedThis = Ref { *this }, promise = WTFMove(promise)](ExceptionOr<bool>&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto&) mutable {
            promise.settle(WTFMove(result));
        });
    });
}

static Ref<FetchRequest> createRequest(ScriptExecutionContext& context, const DOMCacheEngine::Record& record)
{
    auto requestHeaders = FetchHeaders::create(record.requestHeadersGuard, HTTPHeaderMap { record.request.httpHeaderFields() });
    return FetchRequest::create(context, std::nullopt, WTFMove(requestHeaders),  ResourceRequest { record.request }, FetchOptions { record.options }, String { record.referrer });
}

void DOMCache::keys(std::optional<RequestInfo>&& info, CacheQueryOptions&& options, KeysPromise&& promise)
{
    if (!scriptExecutionContext()) [[unlikely]]
        return;

    ResourceRequest resourceRequest;
    if (info) {
        auto requestOrException = requestFromInfo(WTFMove(info.value()), options.ignoreMethod);
        if (requestOrException.hasException()) {
            promise.resolve(Vector<Ref<FetchRequest>> { });
            return;
        }
        resourceRequest = requestOrException.releaseReturnValue()->resourceRequest();
    }

    queryCache(WTFMove(resourceRequest), options, ShouldRetrieveResponses::No, [this, promise = WTFMove(promise)](auto&& result) mutable {
        queueTaskKeepingObjectAlive(*this, TaskSource::DOMManipulation, [promise = WTFMove(promise), result = WTFMove(result)](auto& cache) mutable {
            if (result.hasException()) {
                promise.reject(result.releaseException());
                return;
            }

            auto records = result.releaseReturnValue();
            promise.resolve(WTF::map(records, [&](auto& record) {
                return createRequest(*cache.scriptExecutionContext(), record);
            }));
        });
    });
}

void DOMCache::queryCache(ResourceRequest&& request, const CacheQueryOptions& options, ShouldRetrieveResponses shouldRetrieveResponses, RecordsCallback&& callback)
{
    RefPtr context = scriptExecutionContext();
    if (!context) {
        callback(DOMCacheEngine::convertToException(DOMCacheEngine::Error::Stopped));
        return;
    }

    RetrieveRecordsOptions retrieveOptions { WTFMove(request), scriptExecutionContext()->crossOriginEmbedderPolicy(), *scriptExecutionContext()->securityOrigin(), options.ignoreSearch, options.ignoreMethod, options.ignoreVary, shouldRetrieveResponses == ShouldRetrieveResponses::Yes };

    context->enqueueTaskWhenSettled(m_connection->retrieveRecords(m_identifier, WTFMove(retrieveOptions)), TaskSource::DOMManipulation, [pendingActivity = makePendingActivity(*this), callback = WTFMove(callback)] (auto&& result) mutable {
        RefPtr scriptExecutionContext = pendingActivity->object().scriptExecutionContext();
        if (pendingActivity->object().m_isStopped) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), DOMCacheEngine::Error::Stopped));
            return;
        }

        if (!result) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), result.error()));
            return;
        }

        auto records = WTF::map(WTFMove(result).value(), [](CrossThreadRecord&& record) {
            return fromCrossThreadRecord(WTFMove(record));
        });
        callback(WTFMove(records));
    }, [] (auto&& callback) {
        callback(makeUnexpected(DOMCacheEngine::Error::Stopped));
    });
}

void DOMCache::batchDeleteOperation(const FetchRequest& request, CacheQueryOptions&& options, CompletionHandler<void(ExceptionOr<bool>&&)>&& callback)
{
    RefPtr context = scriptExecutionContext();
    if (!context) {
        callback(DOMCacheEngine::convertToException(DOMCacheEngine::Error::Stopped));
        return;
    }

    context->enqueueTaskWhenSettled(m_connection->batchDeleteOperation(m_identifier, request.internalRequest(), WTFMove(options)), TaskSource::DOMManipulation, [pendingActivity = makePendingActivity(*this), callback = WTFMove(callback)] (auto&& result) mutable {
        RefPtr scriptExecutionContext = pendingActivity->object().scriptExecutionContext();
        if (pendingActivity->object().m_isStopped) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), DOMCacheEngine::Error::Stopped));
            return;
        }

        if (!result) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), result.error()));
            return;
        }
        callback(!result.value().isEmpty());
    }, [] (auto&& callback) {
        callback(makeUnexpected(DOMCacheEngine::Error::Stopped));
    });
}

Record DOMCache::toConnectionRecord(const FetchRequest& request, FetchResponse& response, DOMCacheEngine::ResponseBody&& responseBody)
{
    auto cachedResponse = response.resourceResponse();
    ResourceRequest cachedRequest = request.internalRequest();
    cachedRequest.setHTTPHeaderFields(request.headers().internalHeaders());

    ASSERT(!cachedRequest.isNull());
    ASSERT(!cachedResponse.isNull());

    auto sizeWithPadding = response.bodySizeWithPadding();
    if (!sizeWithPadding) {
        sizeWithPadding = m_connection->computeRecordBodySize(response, responseBody);
        response.setBodySizeWithPadding(sizeWithPadding);
    }

    return { 0, 0,
        request.headers().guard(), WTFMove(cachedRequest), request.fetchOptions(), request.internalRequestReferrer(),
        response.headers().guard(), WTFMove(cachedResponse), WTFMove(responseBody), sizeWithPadding
    };
}

void DOMCache::batchPutOperation(const FetchRequest& request, FetchResponse& response, DOMCacheEngine::ResponseBody&& responseBody, CompletionHandler<void(ExceptionOr<void>&&)>&& callback)
{
    auto record = toConnectionRecord(request, response, WTFMove(responseBody));
    batchPutOperation({ WTFMove(record) }, WTFMove(callback));
}

void DOMCache::batchPutOperation(Vector<Record>&& records, CompletionHandler<void(ExceptionOr<void>&&)>&& callback)
{
    RefPtr context = scriptExecutionContext();
    if (!context) {
        callback(DOMCacheEngine::convertToException(DOMCacheEngine::Error::Stopped));
        return;
    }

    auto crossThreadRecords = WTF::map(WTFMove(records), [](Record&& record) {
        return toCrossThreadRecord(WTFMove(record));
    });
    context->enqueueTaskWhenSettled(m_connection->batchPutOperation(m_identifier, WTFMove(crossThreadRecords)), TaskSource::DOMManipulation, [pendingActivity = makePendingActivity(*this), callback = WTFMove(callback)] (auto&& result) mutable {
        RefPtr scriptExecutionContext = pendingActivity->object().scriptExecutionContext();
        if (pendingActivity->object().m_isStopped) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), DOMCacheEngine::Error::Stopped));
            return;
        }

        if (!result) {
            callback(DOMCacheEngine::convertToExceptionAndLog(scriptExecutionContext.get(), result.error()));
            return;
        }
        callback({ });
    }, [] (auto&& callback) {
        callback(makeUnexpected(DOMCacheEngine::Error::Stopped));
    });
}

void DOMCache::stop()
{
    if (m_isStopped)
        return;
    m_isStopped = true;
    m_connection->dereference(m_identifier);
}

} // namespace WebCore
