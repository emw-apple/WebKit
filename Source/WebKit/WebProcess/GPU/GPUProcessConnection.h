/*
 * Copyright (C) 2019-2025 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS)

#include "AudioMediaStreamTrackRendererInternalUnitIdentifier.h"
#include "Connection.h"
#include "GPUProcessConnectionIdentifier.h"
#include "GraphicsContextGLIdentifier.h"
#include "MediaOverridesForTesting.h"
#include "MessageReceiverMap.h"
#include "RenderingBackendIdentifier.h"
#include "StreamServerConnection.h"
#include "WebGPUIdentifier.h"
#include <WebCore/AudioSession.h>
#include <WebCore/PlatformMediaSession.h>
#include <WebCore/SharedMemory.h>
#include <wtf/AbstractThreadSafeRefCountedAndCanMakeWeakPtr.h>
#include <wtf/Forward.h>
#include <wtf/RefCounted.h>
#include <wtf/ThreadSafeWeakHashSet.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class CAAudioStreamDescription;
struct GraphicsContextGLAttributes;
struct PageIdentifierType;
using PageIdentifier = ObjectIdentifier<PageIdentifierType>;
}

namespace IPC {
class Semaphore;
}

namespace WebKit {
class RemoteAudioSourceProviderManager;
class RemoteMediaPlayerManager;
class RemoteSharedResourceCacheProxy;
class SampleBufferDisplayLayerManager;
class WebPage;
struct GPUProcessConnectionInfo;
struct OverrideScreenDataForTesting;
struct WebPageCreationParameters;

#if ENABLE(VIDEO)
class RemoteVideoFrameObjectHeapProxy;
#endif

class GPUProcessConnection : public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<GPUProcessConnection>, public IPC::Connection::Client {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(GPUProcessConnection);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(GPUProcessConnection);
public:
    static Ref<GPUProcessConnection> create(Ref<IPC::Connection>&&);
    ~GPUProcessConnection();
    GPUProcessConnectionIdentifier identifier() const { return m_identifier; }

    void ref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::ref(); }
    void deref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::deref(); }

    IPC::Connection& connection() { return m_connection.get(); }
    IPC::MessageReceiverMap& messageReceiverMap() { return m_messageReceiverMap; }

    void didBecomeUnresponsive();
#if HAVE(AUDIT_TOKEN)
    std::optional<audit_token_t> auditToken();
#endif
    Ref<RemoteSharedResourceCacheProxy> sharedResourceCache();
#if PLATFORM(COCOA) && ENABLE(MEDIA_STREAM)
    SampleBufferDisplayLayerManager& sampleBufferDisplayLayerManager();
    Ref<SampleBufferDisplayLayerManager> protectedSampleBufferDisplayLayerManager();
    void resetAudioMediaStreamTrackRendererInternalUnit(AudioMediaStreamTrackRendererInternalUnitIdentifier);
#endif
#if ENABLE(VIDEO)
    RemoteVideoFrameObjectHeapProxy& videoFrameObjectHeapProxy();
    Ref<RemoteVideoFrameObjectHeapProxy> protectedVideoFrameObjectHeapProxy();
    RemoteMediaPlayerManager& mediaPlayerManager();
    Ref<RemoteMediaPlayerManager> protectedMediaPlayerManager();
#endif

#if PLATFORM(COCOA) && ENABLE(WEB_AUDIO)
    RemoteAudioSourceProviderManager& audioSourceProviderManager();
    Ref<RemoteAudioSourceProviderManager> protectedAudioSourceProviderManager();
#endif

    void updateMediaConfiguration(bool forceUpdate);

#if HAVE(VISIBILITY_PROPAGATION_VIEW)
    void createVisibilityPropagationContextForPage(WebPage&);
    void destroyVisibilityPropagationContextForPage(WebPage&);
#endif

#if ENABLE(EXTENSION_CAPABILITIES)
    void setMediaEnvironment(WebCore::PageIdentifier, const String&);
#endif

    void configureLoggingChannel(const String&, WTFLogChannelState, WTFLogLevel);

    void createRenderingBackend(RenderingBackendIdentifier, IPC::StreamServerConnection::Handle&&);
    void releaseRenderingBackend(RenderingBackendIdentifier);
#if ENABLE(WEBGL)
    void createGraphicsContextGL(GraphicsContextGLIdentifier, const WebCore::GraphicsContextGLAttributes&, RenderingBackendIdentifier, IPC::StreamServerConnection::Handle&&);
    void releaseGraphicsContextGL(GraphicsContextGLIdentifier);
#endif
    void createGPU(WebGPUIdentifier, RenderingBackendIdentifier, IPC::StreamServerConnection::Handle&&);
    void releaseGPU(WebGPUIdentifier);

    class Client : public AbstractThreadSafeRefCountedAndCanMakeWeakPtr {
    public:
        virtual ~Client() = default;

        virtual void gpuProcessConnectionDidClose(GPUProcessConnection&) { }
    };
    void addClient(const Client& client) { m_clients.add(client); }

    static constexpr Seconds defaultTimeout = 3_s;
private:
    GPUProcessConnection(Ref<IPC::Connection>&&);
    bool waitForDidInitialize();
    void invalidate();

    // IPC::Connection::Client
    void didClose(IPC::Connection&) override;
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&) final;
    void didReceiveInvalidMessage(IPC::Connection&, IPC::MessageName, const Vector<uint32_t>& indicesOfObjectsFailingDecoding) override;

    bool dispatchMessage(IPC::Connection&, IPC::Decoder&);
    bool dispatchSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&);

    // Messages.
    void didReceiveRemoteCommand(WebCore::PlatformMediaSession::RemoteControlCommandType, const WebCore::PlatformMediaSession::RemoteCommandArgument&);
    void didInitialize(std::optional<GPUProcessConnectionInfo>&&);

#if ENABLE(ROUTING_ARBITRATION)
    void beginRoutingArbitrationWithCategory(WebCore::AudioSession::CategoryType, WebCore::AudioSessionRoutingArbitrationClient::ArbitrationCallback&&);
    void endRoutingArbitration();
#endif

    // The connection from the web process to the GPU process.
    const Ref<IPC::Connection> m_connection;
    IPC::MessageReceiverMap m_messageReceiverMap;
    GPUProcessConnectionIdentifier m_identifier { GPUProcessConnectionIdentifier::generate() };
    bool m_hasInitialized { false };
    RefPtr<RemoteSharedResourceCacheProxy> m_sharedResourceCache;
#if HAVE(AUDIT_TOKEN)
    std::optional<audit_token_t> m_auditToken;
#endif
#if PLATFORM(COCOA) && ENABLE(MEDIA_STREAM)
    const std::unique_ptr<SampleBufferDisplayLayerManager> m_sampleBufferDisplayLayerManager;
#endif
#if ENABLE(VIDEO)
    RefPtr<RemoteVideoFrameObjectHeapProxy> m_videoFrameObjectHeapProxy;
#endif
#if PLATFORM(COCOA) && ENABLE(WEB_AUDIO)
    RefPtr<RemoteAudioSourceProviderManager> m_audioSourceProviderManager;
#endif

#if PLATFORM(COCOA)
    MediaOverridesForTesting m_mediaOverridesForTesting;
#endif

    ThreadSafeWeakHashSet<Client> m_clients;
};

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS)
