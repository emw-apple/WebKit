/*
 * Copyright (C) 2021, 2022 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if USE(GSTREAMER) && ENABLE(MEDIA_RECORDER)

#include "GRefPtrGStreamer.h"
#include "MediaRecorderPrivate.h"
#include "SharedBuffer.h"
#include "Timer.h"
#include <gst/app/gstappsink.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Condition.h>
#include <wtf/Forward.h>
#include <wtf/Lock.h>
#include <wtf/MediaTime.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class ContentType;
class MediaStreamTrackPrivate;
struct MediaRecorderPrivateOptions;

class MediaRecorderPrivateBackend : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<MediaRecorderPrivateBackend, WTF::DestructionThread::Main> {
    WTF_MAKE_TZONE_ALLOCATED(MediaRecorderPrivateBackend);
public:
    using SelectTracksCallback = Function<void(MediaRecorderPrivate::AudioVideoSelectedTracks)>;
    static RefPtr<MediaRecorderPrivateBackend> create(MediaStreamPrivate& stream, const MediaRecorderPrivateOptions& options)
    {
        return adoptRef(*new MediaRecorderPrivateBackend(stream, options));
    }

    ~MediaRecorderPrivateBackend();

    bool preparePipeline();

    void fetchData(MediaRecorderPrivate::FetchDataCallback&&);
    void startRecording(MediaRecorderPrivate::StartRecordingCallback&&);
    void stopRecording(CompletionHandler<void()>&&);
    void pauseRecording(CompletionHandler<void()>&&);
    void resumeRecording(CompletionHandler<void()>&&);
    const String& mimeType() const { return m_mimeType; }

    void setSelectTracksCallback(SelectTracksCallback&& callback) { m_selectTracksCallback = WTFMove(callback); }

private:
    MediaRecorderPrivateBackend(MediaStreamPrivate&, const MediaRecorderPrivateOptions&);

    void setSource(GstElement*);
    void setSink(GstElement*);
    void configureAudioEncoder(GstElement*);
    void configureVideoEncoder(GstElement*);

    GRefPtr<GstEncodingContainerProfile> containerProfile();
    MediaStreamPrivate& stream() const { return m_stream; }
    void processSample(GRefPtr<GstSample>&&);
    GstFlowReturn handleSample(GstAppSink*, GRefPtr<GstSample>&&);
    void notifyEOS();
    void positionUpdated();

    GRefPtr<GstEncodingProfile> m_audioEncodingProfile;
    String m_videoCodec;
    GRefPtr<GstElement> m_pipeline;
    GRefPtr<GstElement> m_src;
    GRefPtr<GstElement> m_sink;
    Condition m_eosCondition;
    Lock m_eosLock;
    bool m_eos WTF_GUARDED_BY_LOCK(m_eosLock);

    Lock m_dataLock;
    SharedBufferBuilder m_data WTF_GUARDED_BY_LOCK(m_dataLock);
    MediaTime m_position WTF_GUARDED_BY_LOCK(m_dataLock) { MediaTime::invalidTime() };
    double m_timeCode WTF_GUARDED_BY_LOCK(m_dataLock) { 0 };

    MediaStreamPrivate& m_stream;
    const MediaRecorderPrivateOptions& m_options;
    String m_mimeType;
    std::optional<SelectTracksCallback> m_selectTracksCallback;
    Timer m_positionTimer;
};

class MediaRecorderPrivateGStreamer final : public MediaRecorderPrivate {
    WTF_MAKE_TZONE_ALLOCATED(MediaRecorderPrivateGStreamer);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(MediaRecorderPrivateGStreamer);
public:
    static std::unique_ptr<MediaRecorderPrivateGStreamer> create(MediaStreamPrivate&, const MediaRecorderPrivateOptions&);
    explicit MediaRecorderPrivateGStreamer(Ref<MediaRecorderPrivateBackend>&&);
    ~MediaRecorderPrivateGStreamer() = default;

    static bool isTypeSupported(const ContentType&);

private:
    void videoFrameAvailable(VideoFrame&, VideoFrameTimeMetadata) final { };
    void audioSamplesAvailable(const WTF::MediaTime&, const PlatformAudioData&, const AudioStreamDescription&, size_t) final { };

    void fetchData(FetchDataCallback&&) final;
    void startRecording(StartRecordingCallback&&) final;
    void stopRecording(CompletionHandler<void()>&&) final;
    void pauseRecording(CompletionHandler<void()>&&) final;
    void resumeRecording(CompletionHandler<void()>&&) final;
    String mimeType() const final;

    Ref<MediaRecorderPrivateBackend> m_recorder;
};

} // namespace WebCore

#endif // USE(GSTREAMER) && ENABLE(MEDIA_RECORDER)
