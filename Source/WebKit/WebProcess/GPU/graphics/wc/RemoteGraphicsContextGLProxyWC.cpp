/*
 * Copyright (C) 2021 Sony Interactive Entertainment Inc.
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
#include "RemoteGraphicsContextGLProxy.h"

#if ENABLE(GPU_PROCESS) && ENABLE(WEBGL) && USE(GRAPHICS_LAYER_WC)

#include "GPUConnectionToWebProcess.h"
#include "GPUProcessConnection.h"
#include "RemoteGraphicsContextGLMessages.h"
#include "RemoteRenderingBackendProxy.h"
#include "WCPlatformLayerGCGL.h"
#include "WebProcess.h"
#include <WebCore/GraphicsLayerContentsDisplayDelegate.h>
#include <WebCore/TextureMapperPlatformLayer.h>

namespace WebKit {

namespace {

class PlatformLayerDisplayDelegate final : public WebCore::GraphicsLayerContentsDisplayDelegate {
public:
    static Ref<PlatformLayerDisplayDelegate> create(PlatformLayerContainer&& platformLayer)
    {
        return adoptRef(*new PlatformLayerDisplayDelegate(WTFMove(platformLayer)));
    }

    PlatformLayer* platformLayer() const final
    {
        return m_platformLayer.get();
    }

private:
    PlatformLayerDisplayDelegate(PlatformLayerContainer&& platformLayer)
        : m_platformLayer(WTFMove(platformLayer))
    {
    }

    PlatformLayerContainer m_platformLayer;
};

class RemoteGraphicsContextGLProxyWC final : public RemoteGraphicsContextGLProxy {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(RemoteGraphicsContextGLProxyWC);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RemoteGraphicsContextGLProxyWC);
public:
    // RemoteGraphicsContextGLProxy overrides.
    void prepareForDisplay() final;
    RefPtr<WebCore::GraphicsLayerContentsDisplayDelegate> layerContentsDisplayDelegate() final { return m_layerContentsDisplayDelegate.ptr(); }
private:
    explicit RemoteGraphicsContextGLProxyWC(const WebCore::GraphicsContextGLAttributes& attributes)
        : RemoteGraphicsContextGLProxy(attributes)
        , m_layerContentsDisplayDelegate(PlatformLayerDisplayDelegate::create(makeUnique<WCPlatformLayerGCGL>()))
    {
    }

    const Ref<PlatformLayerDisplayDelegate> m_layerContentsDisplayDelegate;
    friend class RemoteGraphicsContextGLProxy;
};

void RemoteGraphicsContextGLProxyWC::prepareForDisplay()
{
    if (isContextLost())
        return;
    auto sendResult = sendSync(Messages::RemoteGraphicsContextGL::PrepareForDisplay());
    if (!sendResult.succeeded()) {
        markContextLost();
        return;
    }
    auto& [contentBuffer] = sendResult.reply();
    if (contentBuffer)
        static_cast<WCPlatformLayerGCGL*>(m_layerContentsDisplayDelegate->platformLayer())->addContentBufferIdentifier(*contentBuffer);
}

}

Ref<RemoteGraphicsContextGLProxy> RemoteGraphicsContextGLProxy::platformCreate(const WebCore::GraphicsContextGLAttributes& attributes)
{
    return adoptRef(*new RemoteGraphicsContextGLProxyWC(attributes));
}

}

#endif
