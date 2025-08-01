/*
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RTCRtpScriptTransformer.h"

#if ENABLE(WEB_RTC)

#include "DedicatedWorkerGlobalScope.h"
#include "EventLoop.h"
#include "FrameRateMonitor.h"
#include "JSDOMPromiseDeferred.h"
#include "JSRTCEncodedAudioFrame.h"
#include "JSRTCEncodedVideoFrame.h"
#include "Logging.h"
#include "MessageWithMessagePorts.h"
#include "RTCRtpTransformableFrame.h"
#include "ReadableStream.h"
#include "ReadableStreamSource.h"
#include "SerializedScriptValue.h"
#include "WorkerThread.h"
#include "WritableStream.h"
#include "WritableStreamSink.h"

namespace WebCore {

ExceptionOr<Ref<RTCRtpScriptTransformer>> RTCRtpScriptTransformer::create(ScriptExecutionContext& context, MessageWithMessagePorts&& options)
{
    if (!context.globalObject())
        return Exception { ExceptionCode::InvalidStateError };

    auto& globalObject = *JSC::jsCast<JSDOMGlobalObject*>(context.globalObject());
    JSC::JSLockHolder lock(globalObject.vm());
    auto readableSource = SimpleReadableStreamSource::create();
    auto readable = ReadableStream::create(globalObject, readableSource.copyRef());
    if (readable.hasException())
        return readable.releaseException();
    if (!options.message)
        return Exception { ExceptionCode::InvalidStateError };

    auto ports = MessagePort::entanglePorts(context, WTFMove(options.transferredPorts));
    auto transformer = adoptRef(*new RTCRtpScriptTransformer(context, options.message.releaseNonNull(), WTFMove(ports), readable.releaseReturnValue(), WTFMove(readableSource)));
    transformer->suspendIfNeeded();
    return transformer;
}

RTCRtpScriptTransformer::RTCRtpScriptTransformer(ScriptExecutionContext& context, Ref<SerializedScriptValue>&& options, Vector<Ref<MessagePort>>&& ports, Ref<ReadableStream>&& readable, Ref<SimpleReadableStreamSource>&& readableSource)
    : ActiveDOMObject(&context)
    , m_options(WTFMove(options))
    , m_ports(WTFMove(ports))
    , m_readableSource(WTFMove(readableSource))
    , m_readable(WTFMove(readable))
#if !RELEASE_LOG_DISABLED
    , m_enableAdditionalLogging(context.settingsValues().webRTCMediaPipelineAdditionalLoggingEnabled)
    , m_identifier(RTCRtpScriptTransformerIdentifier::generate())
#endif
{
}

RTCRtpScriptTransformer::~RTCRtpScriptTransformer() = default;

ExceptionOr<Ref<WritableStream>> RTCRtpScriptTransformer::writable()
{
    if (!m_writable) {
        RefPtr context = downcast<WorkerGlobalScope>(scriptExecutionContext());
        if (!context || !context->globalObject())
            return Exception { ExceptionCode::InvalidStateError };

        auto& globalObject = *JSC::jsCast<JSDOMGlobalObject*>(context->globalObject());
        auto writableOrException = WritableStream::create(globalObject, SimpleWritableStreamSink::create([transformer = Ref { *this }](auto& context, auto value) -> ExceptionOr<void> {
            if (!transformer->m_backend)
                return Exception { ExceptionCode::InvalidStateError };

            auto& globalObject = *context.globalObject();
            Ref vm = globalObject.vm();
            auto scope = DECLARE_THROW_SCOPE(vm);

            auto frameConversionResult = convert<IDLUnion<IDLInterface<RTCEncodedAudioFrame>, IDLInterface<RTCEncodedVideoFrame>>>(globalObject, value);
            if (frameConversionResult.hasException(scope)) [[unlikely]]
                return Exception { ExceptionCode::ExistingExceptionError };

            auto frame = frameConversionResult.releaseReturnValue();
            auto rtcFrame = WTF::switchOn(frame, [&](RefPtr<RTCEncodedAudioFrame>& value) {
                return value->rtcFrame(vm);
            }, [&](RefPtr<RTCEncodedVideoFrame>& value) {
                return value->rtcFrame(vm);
            });

            if (!rtcFrame->isFromTransformer(transformer.get())) {
                RELEASE_LOG_ERROR(WebRTC, "Trying to enqueue a foreign frame");
                return { };
            }

            // If no data, skip the frame since there is nothing to packetize or decode.
            if (rtcFrame->data().data()) {
#if !RELEASE_LOG_DISABLED
                if (transformer->m_enableAdditionalLogging && transformer->m_backend->mediaType() == RTCRtpTransformBackend::MediaType::Video) {
                    if (!transformer->m_writableFrameRateMonitor) {
                        transformer->m_writableFrameRateMonitor = makeUnique<FrameRateMonitor>([identifier = transformer->m_identifier](auto info) {
                            RELEASE_LOG(WebRTC, "RTCRtpScriptTransformer writable %" PRIu64 ", frame at %f, previous frame was at %f, observed frame rate is %f, delay since last frame is %f ms, frame count is %lu", identifier.toUInt64(), info.frameTime.secondsSinceEpoch().value(), info.lastFrameTime.secondsSinceEpoch().value(), info.observedFrameRate, ((info.frameTime - info.lastFrameTime) * 1000).value(), info.frameCount);
                        });
                    }
                    transformer->m_writableFrameRateMonitor->update();
                }
#endif
                transformer->m_backend->processTransformedFrame(rtcFrame.get());
            }
            return { };
        }));
        if (writableOrException.hasException())
            return writableOrException;
        m_writable = writableOrException.releaseReturnValue();
    }
    return Ref { *m_writable };
}

void RTCRtpScriptTransformer::start(Ref<RTCRtpTransformBackend>&& backend)
{
    m_isVideo = backend->mediaType() == RTCRtpTransformBackend::MediaType::Video;
    m_isSender = backend->side() == RTCRtpTransformBackend::Side::Sender;

    Ref context = downcast<WorkerGlobalScope>(*scriptExecutionContext());
    backend->setTransformableFrameCallback([weakThis = WeakPtr { *this }, thread = Ref { context->thread() }](Ref<RTCRtpTransformableFrame>&& frame) mutable {
        thread->runLoop().postTaskForMode([weakThis, frame = WTFMove(frame)](auto& context) mutable {
            RefPtr protectedThis = weakThis.get();
            if (!protectedThis)
                return;

            frame->setTransformer(*protectedThis);
            protectedThis->enqueueFrame(context, WTFMove(frame));
        }, WorkerRunLoop::defaultMode());
    });

    m_backend = WTFMove(backend);
}

void RTCRtpScriptTransformer::clear(ClearCallback clearCallback)
{
    RefPtr backend = std::exchange(m_backend, { });
    if (backend && clearCallback == ClearCallback::Yes)
        backend->clearTransformableFrameCallback();
    m_backend = nullptr;
    stopPendingActivity();
}

void RTCRtpScriptTransformer::enqueueFrame(ScriptExecutionContext& context, Ref<RTCRtpTransformableFrame>&& frame)
{
    if (!m_backend)
        return;

    auto* globalObject = JSC::jsCast<JSDOMGlobalObject*>(context.globalObject());
    if (!globalObject)
        return;

    Ref vm = globalObject->vm();
    JSC::JSLockHolder lock(vm);

    if (m_isVideo && !m_pendingKeyFramePromises.isEmpty() && frame->isKeyFrame()) {
        // FIXME: We should take into account rids to resolve promises.
        for (Ref promise : std::exchange(m_pendingKeyFramePromises, { }))
            promise->resolve<IDLUnsignedLongLong>(frame->timestamp());
    }

#if !RELEASE_LOG_DISABLED
    if (m_enableAdditionalLogging && m_isVideo) {
        if (!m_readableFrameRateMonitor) {
            m_readableFrameRateMonitor = makeUnique<FrameRateMonitor>([identifier = m_identifier](auto info) {
                RELEASE_LOG(WebRTC, "RTCRtpScriptTransformer readable %" PRIu64 ", frame at %f, previous frame was at %f, observed frame rate is %f, delay since last frame is %f ms, frame count is %lu", identifier.toUInt64(), info.frameTime.secondsSinceEpoch().value(), info.lastFrameTime.secondsSinceEpoch().value(), info.observedFrameRate, ((info.frameTime - info.lastFrameTime) * 1000).value(), info.frameCount);
            });
        }
        m_readableFrameRateMonitor->update();
    }
#endif

    auto value = m_isVideo ? toJS(globalObject, globalObject, RTCEncodedVideoFrame::create(WTFMove(frame))) : toJS(globalObject, globalObject, RTCEncodedAudioFrame::create(WTFMove(frame)));
    m_readableSource->enqueue(value);
}

static std::optional<Exception> validateRid(const String& rid)
{
    if (rid.isNull())
        return { };

    if (rid.isEmpty())
        return Exception { ExceptionCode::NotAllowedError, "rid is empty"_s };

    constexpr unsigned maxRidLength = 255;
    if (rid.length() > maxRidLength)
        return Exception { ExceptionCode::NotAllowedError, "rid is too long"_s };

    auto foundBadCharacters = rid.find([](auto character) -> bool {
        return !isASCIIDigit(character) && !isASCIIAlpha(character);
    });
    if (foundBadCharacters != notFound)
        return Exception { ExceptionCode::NotAllowedError, "rid has a character that is not alpha numeric"_s };

    return { };
}

void RTCRtpScriptTransformer::generateKeyFrame(const String& rid, Ref<DeferredPromise>&& promise)
{
    RefPtr context = scriptExecutionContext();
    if (!context || !m_isVideo || !m_isSender) {
        promise->reject(Exception { ExceptionCode::InvalidStateError, "Not attached to a valid video sender"_s });
        return;
    }

    if (auto exception = validateRid(rid)) {
        promise->reject(WTFMove(*exception));
        return;
    }

    RefPtr backend = m_backend;
    if (!backend)
        return;

    if (!m_backend->requestKeyFrame(rid)) {
        context->eventLoop().queueTask(TaskSource::Networking, [promise = WTFMove(promise)]() mutable {
            promise->reject(Exception { ExceptionCode::NotFoundError, "rid was not found or is empty"_s });
        });
        return;
    }

    m_pendingKeyFramePromises.append(WTFMove(promise));
}

void RTCRtpScriptTransformer::sendKeyFrameRequest(Ref<DeferredPromise>&& promise)
{
    RefPtr context = scriptExecutionContext();
    if (!context || !m_isVideo || m_isSender) {
        promise->reject(Exception { ExceptionCode::InvalidStateError, "Not attached to a valid video receiver"_s });
        return;
    }

    RefPtr backend = m_backend;
    if (!backend)
        return;

    // FIXME: We should be able to know when the FIR request is sent to resolve the promise at this exact time.
    backend->requestKeyFrame({ });

    context->eventLoop().queueTask(TaskSource::Networking, [promise = WTFMove(promise)]() mutable {
        promise->resolve();
    });
}

JSC::JSValue RTCRtpScriptTransformer::options(JSC::JSGlobalObject& globalObject)
{
    return m_options->deserialize(globalObject, &globalObject, m_ports);
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
