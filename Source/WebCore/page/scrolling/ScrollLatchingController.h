/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

#include "FloatSize.h"
#include "ScrollTypes.h"
#include "Timer.h"
#include <wtf/RefPtr.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

#if ENABLE(WHEEL_EVENT_LATCHING)

namespace WTF {
class TextStream;
}

namespace WebCore {

class Element;
class LocalFrame;
class Page;
class PlatformWheelEvent;
class ScrollableArea;
class WeakPtrImplWithEventTargetData;

class ScrollLatchingController {
    WTF_MAKE_TZONE_ALLOCATED(ScrollLatchingController);
public:
    explicit ScrollLatchingController(Page&);
    ~ScrollLatchingController();

    void clear();

    void ref() const;
    void deref() const;

    void receivedWheelEvent(const PlatformWheelEvent&);
    FloatSize cumulativeEventDelta() const { return m_cumulativeEventDelta; }

    // Frame containing latched scroller (may be the frame or some sub-scroller).
    LocalFrame* latchedFrame() const;

    // Returns true if no frame is latched, or latching is in the given frame (in which case latchedScroller will be non-null).
    bool latchingAllowsScrollingInFrame(const LocalFrame&, WeakPtr<ScrollableArea>& latchedScroller) const;

    void updateAndFetchLatchingStateForFrame(LocalFrame&, const PlatformWheelEvent&, RefPtr<Element>& latchedElement, WeakPtr<ScrollableArea>&, bool& isOverWidget);

    void removeLatchingStateForTarget(const Element&);
    void removeLatchingStateForFrame(const LocalFrame&);

    void dump(WTF::TextStream&) const;

private:
    struct FrameState {
        WeakPtr<Element, WeakPtrImplWithEventTargetData> wheelEventElement;
        WeakPtr<ScrollableArea> scrollableArea;
        LocalFrame* frame { nullptr };
        bool isOverWidget { false };
    };

    void clearOrScheduleClearIfNeeded(const PlatformWheelEvent&);
    void clearTimerFired();

    bool hasStateForFrame(const LocalFrame&) const;
    FrameState* stateForFrame(const LocalFrame&);
    const FrameState* stateForFrame(const LocalFrame&) const;

    bool shouldLatchToScrollableArea(const LocalFrame&, ScrollableArea*, FloatSize) const;

    WeakRef<Page> m_page;
    FloatSize m_cumulativeEventDelta;
    Vector<FrameState> m_frameStateStack;
    Timer m_clearLatchingStateTimer;
};

WTF::TextStream& operator<<(WTF::TextStream&, const ScrollLatchingController&);

} // namespace WebCore

#endif // ENABLE(WHEEL_EVENT_LATCHING)

