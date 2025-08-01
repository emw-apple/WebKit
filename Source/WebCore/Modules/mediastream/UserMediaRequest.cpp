/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013-2018 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "UserMediaRequest.h"

#if ENABLE(MEDIA_STREAM)

#include "AudioSession.h"
#include "DocumentInlines.h"
#include "ExceptionCode.h"
#include "JSDOMPromiseDeferred.h"
#include "JSMediaStream.h"
#include "JSOverconstrainedError.h"
#include "LocalFrame.h"
#include "Logging.h"
#include "MediaConstraints.h"
#include "MediaDevices.h"
#include "Navigator.h"
#include "NavigatorMediaDevices.h"
#include "PermissionsPolicy.h"
#include "PlatformMediaSessionManager.h"
#include "RTCController.h"
#include "RealtimeMediaSourceCenter.h"
#include "Settings.h"
#include "UserMediaController.h"
#include "WindowEventLoop.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <algorithm>
#include <wtf/Scope.h>

namespace WebCore {

Ref<UserMediaRequest> UserMediaRequest::create(Document& document, MediaStreamRequest&& request, TrackConstraints&& audioConstraints, TrackConstraints&& videoConstraints, DOMPromiseDeferred<IDLInterface<MediaStream>>&& promise)
{
    auto result = adoptRef(*new UserMediaRequest(document, WTFMove(request), WTFMove(audioConstraints), WTFMove(videoConstraints), WTFMove(promise)));
    result->suspendIfNeeded();
    return result;
}

UserMediaRequest::UserMediaRequest(Document& document, MediaStreamRequest&& request, TrackConstraints&& audioConstraints, TrackConstraints&& videoConstraints, DOMPromiseDeferred<IDLInterface<MediaStream>>&& promise)
    : ActiveDOMObject(document)
    , m_promise(makeUniqueRef<DOMPromiseDeferred<IDLInterface<MediaStream>>>(WTFMove(promise)))
    , m_request(WTFMove(request))
    , m_audioConstraints(WTFMove(audioConstraints))
    , m_videoConstraints(WTFMove(videoConstraints))
{
}

UserMediaRequest::~UserMediaRequest()
{
    if (auto completionHandler = std::exchange(m_allowCompletionHandler, { }))
        completionHandler();
}

SecurityOrigin* UserMediaRequest::userMediaDocumentOrigin() const
{
    RefPtr context = scriptExecutionContext();
    return context ? context->securityOrigin() : nullptr;
}

SecurityOrigin* UserMediaRequest::topLevelDocumentOrigin() const
{
    RefPtr context = scriptExecutionContext();
    return context ? &context->topOrigin() : nullptr;
}

void UserMediaRequest::start()
{
    RefPtr context = scriptExecutionContext();
    ASSERT(context);
    if (!context) {
        deny(MediaAccessDenialReason::UserMediaDisabled);
        return;
    }

    // 4. If the current settings object's responsible document is NOT allowed to use the feature indicated by
    //    attribute name allowusermedia, return a promise rejected with a DOMException object whose name
    //    attribute has the value SecurityError.
    auto& document = downcast<Document>(*context);
    auto* controller = UserMediaController::from(document.page());
    if (!controller) {
        deny(MediaAccessDenialReason::UserMediaDisabled);
        return;
    }

    // 6.3 Optionally, e.g., based on a previously-established user preference, for security reasons,
    //     or due to platform limitations, jump to the step labeled Permission Failure below.
    // ...
    // 6.10 Permission Failure: Reject p with a new DOMException object whose name attribute has
    //      the value NotAllowedError.

    switch (m_request.type) {
    case MediaStreamRequest::Type::DisplayMedia:
    case MediaStreamRequest::Type::DisplayMediaWithAudio:
        if (!PermissionsPolicy::isFeatureEnabled(PermissionsPolicy::Feature::DisplayCapture, document)) {
            deny(MediaAccessDenialReason::PermissionDenied);
            controller->logGetDisplayMediaDenial(document);
            return;
        }
        break;
    case MediaStreamRequest::Type::UserMedia:
        if (m_request.audioConstraints.isValid && !PermissionsPolicy::isFeatureEnabled(PermissionsPolicy::Feature::Microphone, document)) {
            deny(MediaAccessDenialReason::PermissionDenied);
            controller->logGetUserMediaDenial(document);
            return;
        }
        if (m_request.videoConstraints.isValid && !PermissionsPolicy::isFeatureEnabled(PermissionsPolicy::Feature::Camera, document)) {
            deny(MediaAccessDenialReason::PermissionDenied);
            controller->logGetUserMediaDenial(document);
            return;
        }
        break;
    }

    ASSERT(document.page());
    if (RefPtr page = document.page())
        page->mediaSessionManager().prepareToSendUserMediaPermissionRequestForPage(*page);
    controller->requestUserMediaAccess(*this);
}

static inline bool isMediaStreamCorrectlyStarted(const MediaStream& stream)
{
    if (stream.getTracks().isEmpty())
        return false;

    return std::ranges::all_of(stream.getTracks(), [](auto& track) {
        return !track->source().captureDidFail();
    });
}

void UserMediaRequest::allow(CaptureDevice&& audioDevice, CaptureDevice&& videoDevice, MediaDeviceHashSalts&& deviceIdentifierHashSalt, CompletionHandler<void()>&& completionHandler)
{
    RELEASE_LOG(MediaStream, "UserMediaRequest::allow %s %s", audioDevice ? audioDevice.persistentId().utf8().data() : "", videoDevice ? videoDevice.persistentId().utf8().data() : "");

    Ref document = downcast<Document>(*scriptExecutionContext());
    RefPtr localWindow = document->window();
    RefPtr mediaDevices = localWindow ? NavigatorMediaDevices::mediaDevices(localWindow->protectedNavigator()) : nullptr;
    if (mediaDevices)
        mediaDevices->willStartMediaCapture(!!audioDevice, !!videoDevice);

    m_allowCompletionHandler = WTFMove(completionHandler);
    queueTaskKeepingObjectAlive(*this, TaskSource::UserInteraction, [audioDevice = WTFMove(audioDevice), videoDevice = WTFMove(videoDevice), deviceIdentifierHashSalt = WTFMove(deviceIdentifierHashSalt)](auto& request) mutable {
        auto callback = [protectedThis = Ref { request }, protector = request.makePendingActivity(request)](auto privateStreamOrError) mutable {
            auto scopeExit = makeScopeExit([completionHandler = WTFMove(protectedThis->m_allowCompletionHandler)]() mutable {
                completionHandler();
            });
            if (protectedThis->isContextStopped()) {
                if (!!privateStreamOrError) {
                    RELEASE_LOG(MediaStream, "UserMediaRequest::allow, context is stopped");
                    privateStreamOrError.value()->forEachTrack([](auto& track) {
                        track.endTrack();
                    });
                }
                return;
            }

            if (!privateStreamOrError) {
                RELEASE_LOG(MediaStream, "UserMediaRequest::allow failed to create media stream!");
                auto error = privateStreamOrError.error();
                protectedThis->scriptExecutionContext()->addConsoleMessage(MessageSource::JS, MessageLevel::Error, error.errorMessage);
                protectedThis->deny(error.denialReason, error.errorMessage, error.invalidConstraint);
                return;
            }
            auto privateStream = WTFMove(privateStreamOrError).value();

            auto& document = downcast<Document>(*protectedThis->scriptExecutionContext());
            privateStream->monitorOrientation(document.orientationNotifier());

            Ref stream = MediaStream::create(document, WTFMove(privateStream));
            stream->startProducingData();

            if (!isMediaStreamCorrectlyStarted(stream)) {
                protectedThis->deny(MediaAccessDenialReason::HardwareError);
                return;
            }

            if (RefPtr audioTrack = stream->getFirstAudioTrack()) {
#if USE(AUDIO_SESSION)
                AudioSession::singleton().tryToSetActive(true);
#endif
                if (std::holds_alternative<MediaTrackConstraints>(protectedThis->m_audioConstraints))
                    audioTrack->setConstraints(std::get<MediaTrackConstraints>(WTFMove(protectedThis->m_audioConstraints)));
            }
            if (RefPtr videoTrack = stream->getFirstVideoTrack()) {
                if (std::holds_alternative<MediaTrackConstraints>(protectedThis->m_videoConstraints))
                    videoTrack->setConstraints(std::get<MediaTrackConstraints>(WTFMove(protectedThis->m_videoConstraints)));
            }

            ASSERT(document.isCapturing());
            document.setHasCaptureMediaStreamTrack();
            protectedThis->m_promise->resolve(WTFMove(stream));
        };

        auto& document = downcast<Document>(*request.scriptExecutionContext());
        RealtimeMediaSourceCenter::singleton().createMediaStream(document.logger(), WTFMove(callback), WTFMove(deviceIdentifierHashSalt), WTFMove(audioDevice), WTFMove(videoDevice), request.m_request);

        if (!request.scriptExecutionContext())
            return;

#if ENABLE(WEB_RTC)
        if (RefPtr page = document.page())
            page->rtcController().disableICECandidateFilteringForDocument(document);
#endif
    });
}

void UserMediaRequest::deny(MediaAccessDenialReason reason, const String& message, MediaConstraintType invalidConstraint)
{
    if (!scriptExecutionContext())
        return;

    ExceptionCode code;
    switch (reason) {
    case MediaAccessDenialReason::NoReason:
        ASSERT_NOT_REACHED();
        code = ExceptionCode::AbortError;
        break;
    case MediaAccessDenialReason::NoConstraints:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - no constraints");
        code = ExceptionCode::TypeError;
        break;
    case MediaAccessDenialReason::UserMediaDisabled:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - user media disabled");
        code = ExceptionCode::SecurityError;
        break;
    case MediaAccessDenialReason::NoCaptureDevices:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - no capture devices");
        code = ExceptionCode::NotFoundError;
        break;
    case MediaAccessDenialReason::InvalidConstraint:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - invalid constraint - %d", (int)invalidConstraint);
        m_promise->rejectType<IDLInterface<OverconstrainedError>>(OverconstrainedError::create(invalidConstraint, "Invalid constraint"_s).get());
        return;
    case MediaAccessDenialReason::HardwareError:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - hardware error");
        code = ExceptionCode::NotReadableError;
        break;
    case MediaAccessDenialReason::OtherFailure:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - other failure");
        code = ExceptionCode::AbortError;
        break;
    case MediaAccessDenialReason::PermissionDenied:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - permission denied");
        code = ExceptionCode::NotAllowedError;
        break;
    case MediaAccessDenialReason::InvalidAccess:
        RELEASE_LOG(MediaStream, "UserMediaRequest::deny - invalid access");
        code = ExceptionCode::InvalidAccessError;
        break;
    }

    if (!message.isEmpty())
        m_promise->reject(code, message);
    else
        m_promise->reject(code);
}

void UserMediaRequest::stop()
{
    Ref document = downcast<Document>(*scriptExecutionContext());
    if (auto* controller = UserMediaController::from(document->page()))
        controller->cancelUserMediaAccessRequest(*this);
}

Document* UserMediaRequest::document() const
{
    return downcast<Document>(scriptExecutionContext());
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
