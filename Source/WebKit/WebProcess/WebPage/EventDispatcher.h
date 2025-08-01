/*
 * Copyright (C) 2011-2025 Apple Inc. All rights reserved.
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

#include "DisplayLinkObserverID.h"
#include "MessageReceiver.h"
#include "MomentumEventDispatcher.h"
#include "WebEvent.h"
#include <WebCore/FrameIdentifier.h>
#include <WebCore/PageIdentifier.h>
#include <WebCore/PlatformWheelEvent.h>
#include <WebCore/RectEdges.h>
#include <WebCore/ScrollingCoordinatorTypes.h>
#include <WebCore/WheelEventDeltaFilter.h>
#include <memory>
#include <wtf/CheckedRef.h>
#include <wtf/HashMap.h>
#include <wtf/Lock.h>
#include <wtf/Noncopyable.h>
#include <wtf/RefPtr.h>
#include <wtf/ThreadingPrimitives.h>
#include <wtf/WorkQueue.h>

#if ENABLE(MAC_GESTURE_EVENTS)
#include "WebGestureEvent.h"
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
#include "WebTouchEvent.h"
#include <wtf/CompletionHandler.h>
#endif

namespace WebCore {
class ThreadedScrollingTree;
struct DisplayUpdate;
struct RemoteUserInputEventData;
using PlatformDisplayID = uint32_t;
}

namespace WebKit {

class MomentumEventDispatcher;
class ScrollingAccelerationCurve;
class WebPage;
class WebProcess;
class WebWheelEvent;
struct RemoteWebTouchEvent;

#if ENABLE(IOS_TOUCH_EVENTS)
struct TouchEventData {
    TouchEventData(WebCore::FrameIdentifier, const WebTouchEvent&, CompletionHandler<void(bool, std::optional<RemoteWebTouchEvent>)>&&);
    TouchEventData(TouchEventData&&);
    ~TouchEventData();
    TouchEventData& operator=(TouchEventData&&);

    WebCore::FrameIdentifier frameID;
    WebTouchEvent event;
    CompletionHandler<void(bool, std::optional<RemoteWebTouchEvent>)> completionHandler;
};
#endif

class EventDispatcher final :
#if ENABLE(MOMENTUM_EVENT_DISPATCHER)
    public MomentumEventDispatcher::Client,
#endif
    public IPC::MessageReceiver {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(EventDispatcher);
#if ENABLE(MOMENTUM_EVENT_DISPATCHER)
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(EventDispatcher);
#endif
public:
    explicit EventDispatcher(WebProcess&);
    ~EventDispatcher();

    void ref() const final;
    void deref() const final;

    enum class WheelEventOrigin : bool { UIProcess, MomentumEventDispatcher };

    WorkQueue& queue() { return m_queue.get(); }

#if ENABLE(ASYNC_SCROLLING) && ENABLE(SCROLLING_THREAD)
    void addScrollingTreeForPage(WebPage&);
    void removeScrollingTreeForPage(WebPage&);
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
    using TouchEventQueue = Vector<TouchEventData, 1>;
    void takeQueuedTouchEventsForPage(const WebPage&, UniqueRef<TouchEventQueue>&);
#endif

    void initializeConnection(IPC::Connection&);

    void notifyScrollingTreesDisplayDidRefresh(WebCore::PlatformDisplayID);

private:
    // IPC::MessageReceiver overrides.
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;

    // Message handlers
    void wheelEvent(WebCore::PageIdentifier, const WebWheelEvent&, WebCore::RectEdges<WebCore::RubberBandingBehavior> rubberBandableEdges);
#if ENABLE(MOMENTUM_EVENT_DISPATCHER)
    void setScrollingAccelerationCurve(WebCore::PageIdentifier, std::optional<ScrollingAccelerationCurve>);
#endif
#if ENABLE(IOS_TOUCH_EVENTS)
    void touchEvent(WebCore::PageIdentifier, WebCore::FrameIdentifier, const WebTouchEvent&, CompletionHandler<void(bool, std::optional<RemoteWebTouchEvent>)>&&);
#endif
#if ENABLE(MAC_GESTURE_EVENTS)
    void gestureEvent(WebCore::FrameIdentifier, WebCore::PageIdentifier, const WebGestureEvent&, CompletionHandler<void(std::optional<WebEventType>, bool, std::optional<WebCore::RemoteUserInputEventData>)>&&);
#endif

    // This is called on the main thread.
    void dispatchWheelEvent(WebCore::PageIdentifier, const WebWheelEvent&, OptionSet<WebCore::WheelEventProcessingSteps>, WheelEventOrigin);
    void dispatchWheelEventViaMainThread(WebCore::PageIdentifier, const WebWheelEvent&, OptionSet<WebCore::WheelEventProcessingSteps>, WheelEventOrigin);

    void internalWheelEvent(WebCore::PageIdentifier, const WebWheelEvent&, WebCore::RectEdges<WebCore::RubberBandingBehavior> rubberBandableEdges, WheelEventOrigin);

#if ENABLE(IOS_TOUCH_EVENTS)
    void dispatchTouchEvents();
#endif
#if ENABLE(MAC_GESTURE_EVENTS)
    void dispatchGestureEvent(WebCore::FrameIdentifier, WebCore::PageIdentifier, const WebGestureEvent&, CompletionHandler<void(std::optional<WebEventType>, bool, std::optional<WebCore::RemoteUserInputEventData>)>&&);
#endif

    static void sendDidReceiveEvent(WebCore::PageIdentifier, WebEventType, bool didHandleEvent);

#if HAVE(DISPLAY_LINK)
    void displayDidRefresh(WebCore::PlatformDisplayID, const WebCore::DisplayUpdate&, bool sendToMainThread);
#endif

#if ENABLE(SCROLLING_THREAD)
    void displayDidRefreshOnScrollingThread(WebCore::PlatformDisplayID);
#endif

#if ENABLE(MOMENTUM_EVENT_DISPATCHER)
    // EventDispatcher::Client
    void handleSyntheticWheelEvent(WebCore::PageIdentifier, const WebWheelEvent&, WebCore::RectEdges<WebCore::RubberBandingBehavior> rubberBandableEdges) override;
    void startDisplayDidRefreshCallbacks(WebCore::PlatformDisplayID) override;
    void stopDisplayDidRefreshCallbacks(WebCore::PlatformDisplayID) override;

#if ENABLE(MOMENTUM_EVENT_DISPATCHER_TEMPORARY_LOGGING)
    void flushMomentumEventLoggingSoon() override;
#endif
#endif

    void pageScreenDidChange(WebCore::PageIdentifier, WebCore::PlatformDisplayID, std::optional<unsigned> nominalFramesPerSecond);

    const CheckedRef<WebProcess> m_process;
    const Ref<WorkQueue> m_queue;

#if ENABLE(ASYNC_SCROLLING) && ENABLE(SCROLLING_THREAD)
    Lock m_scrollingTreesLock;
    HashMap<WebCore::PageIdentifier, RefPtr<WebCore::ThreadedScrollingTree>> m_scrollingTrees WTF_GUARDED_BY_LOCK(m_scrollingTreesLock);
#endif
    std::unique_ptr<WebCore::WheelEventDeltaFilter> m_recentWheelEventDeltaFilter;
#if ENABLE(IOS_TOUCH_EVENTS)
    Lock m_touchEventsLock;
    HashMap<WebCore::PageIdentifier, UniqueRef<TouchEventQueue>> m_touchEvents WTF_GUARDED_BY_LOCK(m_touchEventsLock);
#endif

#if ENABLE(MOMENTUM_EVENT_DISPATCHER)
    std::unique_ptr<MomentumEventDispatcher> m_momentumEventDispatcher;
    DisplayLinkObserverID m_observerID;
#endif
};

} // namespace WebKit
