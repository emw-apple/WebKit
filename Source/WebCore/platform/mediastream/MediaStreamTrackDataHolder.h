/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(MEDIA_STREAM)

#include "CaptureDevice.h"
#include "MediaStreamTrackHintValue.h"
#include "RealtimeMediaSource.h"
#include <wtf/FastMalloc.h>

namespace WebCore {

class PreventSourceFromEndingObserverWrapper;

struct MediaStreamTrackDataHolder {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(MediaStreamTrackDataHolder);

    WEBCORE_EXPORT MediaStreamTrackDataHolder(String&& trackId, String&& label, RealtimeMediaSource::Type, CaptureDevice::DeviceType, bool isEnabled, bool isEnded, MediaStreamTrackHintValue, bool isProducingData, bool isMuted, bool isInterrupted, RealtimeMediaSourceSettings, RealtimeMediaSourceCapabilities, Ref<RealtimeMediaSource>&&);
    WEBCORE_EXPORT ~MediaStreamTrackDataHolder();

    MediaStreamTrackDataHolder(const MediaStreamTrackDataHolder &) = delete;
    MediaStreamTrackDataHolder &operator=(const MediaStreamTrackDataHolder &) = delete;

    String trackId;
    String label;
    RealtimeMediaSource::Type type;
    CaptureDevice::DeviceType deviceType;
    bool isEnabled { true };
    bool isEnded { false };
    MediaStreamTrackHintValue contentHint { MediaStreamTrackHintValue::Empty };
    bool isProducingData { false };
    bool isMuted { false };
    bool isInterrupted { false };
    RealtimeMediaSourceSettings settings;
    RealtimeMediaSourceCapabilities capabilities;
    Ref<RealtimeMediaSource> source;

    Ref<PreventSourceFromEndingObserverWrapper> preventSourceFromEndingObserverWrapper;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
