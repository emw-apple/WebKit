/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#if ENABLE(WEBXR)

#include "XRDeviceIdentifier.h"
#include "XRDeviceInfo.h"
#if USE(OPENXR)
#include "XRDeviceLayer.h"
#endif
#include <WebCore/PlatformXR.h>
#include <wtf/AbstractRefCountedAndCanMakeWeakPtr.h>
#include <wtf/Function.h>

namespace WebCore {
class SecurityOriginData;
}

namespace WebKit {

class WebPageProxy;

class PlatformXRCoordinatorSessionEventClient : public AbstractRefCountedAndCanMakeWeakPtr<PlatformXRCoordinatorSessionEventClient> {
public:
    virtual ~PlatformXRCoordinatorSessionEventClient() = default;

    virtual void sessionDidEnd(XRDeviceIdentifier) = 0;
    virtual void sessionDidUpdateVisibilityState(XRDeviceIdentifier, PlatformXR::VisibilityState) = 0;
};

class PlatformXRCoordinator {
public:
    virtual ~PlatformXRCoordinator() = default;

    // FIXME: Temporary and will be fixed later.
    static PlatformXR::LayerHandle defaultLayerHandle() { return 1; }

    using DeviceInfoCallback = Function<void(std::optional<XRDeviceInfo>)>;
    virtual void getPrimaryDeviceInfo(WebPageProxy&, DeviceInfoCallback&&) = 0;

    using FeatureListCallback = CompletionHandler<void(std::optional<PlatformXR::Device::FeatureList>&&)>;
    virtual void requestPermissionOnSessionFeatures(WebPageProxy&, const WebCore::SecurityOriginData&, PlatformXR::SessionMode, const PlatformXR::Device::FeatureList& granted, const PlatformXR::Device::FeatureList& /* consentRequired */, const PlatformXR::Device::FeatureList& /* consentOptional */, const PlatformXR::Device::FeatureList& /* requiredFeaturesRequested */, const PlatformXR::Device::FeatureList& /* optionalFeaturesRequested */, FeatureListCallback&& completionHandler) { completionHandler(granted); }

#if USE(OPENXR)
    virtual void createLayerProjection(uint32_t width, uint32_t height, bool alpha) = 0;
#endif

    // Session creation/termination.
    virtual void startSession(WebPageProxy&, WeakPtr<PlatformXRCoordinatorSessionEventClient>&&, const WebCore::SecurityOriginData&, PlatformXR::SessionMode, const PlatformXR::Device::FeatureList&) = 0;
    virtual void endSessionIfExists(WebPageProxy&) = 0;

    // Session display loop.
    virtual void scheduleAnimationFrame(WebPageProxy&, std::optional<PlatformXR::RequestData>&&, PlatformXR::Device::RequestFrameCallback&&) = 0;
#if USE(OPENXR)
    virtual void submitFrame(WebPageProxy&, Vector<XRDeviceLayer>&&) = 0;
#else
    virtual void submitFrame(WebPageProxy&) { }
#endif
};

} // namespace WebKit

#endif // ENABLE(WEBXR)
