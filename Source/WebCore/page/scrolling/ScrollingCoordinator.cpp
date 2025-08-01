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

#include "config.h"

#include "ScrollingCoordinator.h"

#include "Document.h"
#include "DocumentClasses.h"
#include "EventNames.h"
#include "GraphicsLayer.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PlatformWheelEvent.h"
#include "PluginViewBase.h"
#include "Region.h"
#include "RenderLayerCompositor.h"
#include "RenderLayerScrollableArea.h"
#include "RenderObjectInlines.h"
#include "RenderView.h"
#include "ScrollAnimator.h"
#include "Settings.h"
#include <wtf/MainThread.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(ScrollingCoordinator);

#if PLATFORM(IOS_FAMILY) || !ENABLE(ASYNC_SCROLLING)
Ref<ScrollingCoordinator> ScrollingCoordinator::create(Page* page)
{
    return adoptRef(*new ScrollingCoordinator(page));
}
#endif

ScrollingCoordinator::ScrollingCoordinator(Page* page)
    : m_page(page)
{
}

ScrollingCoordinator::~ScrollingCoordinator()
{
    ASSERT(!m_page);
}

void ScrollingCoordinator::pageDestroyed()
{
    ASSERT(m_page);
    m_page = nullptr;
}

bool ScrollingCoordinator::coordinatesScrollingForFrameView(const LocalFrameView& frameView) const
{
    ASSERT(isMainThread());
    ASSERT(m_page);

    Ref localFrame = frameView.frame();
    if (!localFrame->isMainFrame() && !m_page->settings().scrollingTreeIncludesFrames()
#if PLATFORM(MAC) || USE(COORDINATED_GRAPHICS)
        && !m_page->settings().asyncFrameScrollingEnabled()
#endif
    )
        return false;

    auto* renderView = localFrame->contentRenderer();
    if (!renderView)
        return false;
    return renderView->usesCompositing();
}

bool ScrollingCoordinator::coordinatesScrollingForOverflowLayer(const RenderLayer& layer) const
{
    ASSERT(isMainThread());
    ASSERT(m_page);

    return layer.hasCompositedScrollableOverflow();
}

std::optional<ScrollingNodeID> ScrollingCoordinator::scrollableContainerNodeID(const RenderObject&) const
{
    return std::nullopt;
}

EventTrackingRegions ScrollingCoordinator::absoluteEventTrackingRegionsForFrame(const LocalFrame& frame) const
{
    auto* renderView = frame.contentRenderer();
    if (!renderView || renderView->renderTreeBeingDestroyed())
        return EventTrackingRegions();

#if ENABLE(IOS_TOUCH_EVENTS)
    // On iOS, we use nonFastScrollableRegion to represent the region covered by elements with touch event handlers.
    ASSERT(frame.isMainFrame());
    auto* document = frame.document();
    if (!document)
        return { };
    return document->eventTrackingRegions();
#else
    auto* frameView = frame.view();
    if (!frameView)
        return EventTrackingRegions();

    Region nonFastScrollableRegion;

    // FIXME: should ASSERT(!frameView->needsLayout()) here, but need to fix DebugPageOverlays
    // to not ask for regions at bad times.

    if (auto* scrollableAreas = frameView->scrollableAreas()) {
        for (auto& area : *scrollableAreas) {
            CheckedPtr<ScrollableArea> scrollableArea(area);
            // Composited scrollable areas can be scrolled off the main thread.
            if (!scrollableArea->isVisibleToHitTesting() || scrollableArea->usesAsyncScrolling())
                continue;

            bool isInsideFixed;
            IntRect box = scrollableArea->scrollableAreaBoundingBox(&isInsideFixed);
            if (isInsideFixed)
                box = IntRect(frameView->fixedScrollableAreaBoundsInflatedForScrolling(LayoutRect(box)));

            nonFastScrollableRegion.unite(box);
        }
    }

    for (auto& widget : frameView->widgetsInRenderTree()) {
        auto* pluginViewBase = dynamicDowncast<PluginViewBase>(widget.get());
        if (!pluginViewBase)
            continue;
        if (!pluginViewBase->wantsWheelEvents())
            continue;
        auto* renderWidget = RenderWidget::find(widget);
        if (!renderWidget)
            continue;
        nonFastScrollableRegion.unite(renderWidget->absoluteBoundingBoxRect());
    }
    
    EventTrackingRegions eventTrackingRegions;

    // FIXME: if we've already accounted for this subframe as a scrollable area, we can avoid recursing into it here.
    for (RefPtr subframe = frame.tree().firstChild(); subframe; subframe = subframe->tree().nextSibling()) {
        auto* localSubframe = dynamicDowncast<LocalFrame>(subframe.get());
        if (!localSubframe)
            continue;
        auto* subframeView = localSubframe->view();
        if (!subframeView)
            continue;

        EventTrackingRegions subframeRegion = absoluteEventTrackingRegionsForFrame(*localSubframe);
        // Map from the frame document to our document.
        // Event regions are integral, and can't represent subpixel frame positions.
        auto offset = subframeView->contentsToContainingViewContents(IntPoint());

        // FIXME: this translation ignores non-trival transforms on the frame.
        subframeRegion.translate(toIntSize(offset));
        eventTrackingRegions.unite(subframeRegion);
    }

#if !ENABLE(WHEEL_EVENT_REGIONS)
    auto wheelHandlerRegion = frame.document()->absoluteRegionForWheelEventTargets();
    bool wheelHandlerInFixedContent = wheelHandlerRegion.second;
    if (wheelHandlerInFixedContent) {
        // FIXME: need to handle position:sticky here too.
        LayoutRect inflatedWheelHandlerBounds = frameView->fixedScrollableAreaBoundsInflatedForScrolling(LayoutRect(wheelHandlerRegion.first.bounds()));
        wheelHandlerRegion.first.unite(enclosingIntRect(inflatedWheelHandlerBounds));
    }
    nonFastScrollableRegion.unite(wheelHandlerRegion.first);
#endif

    // FIXME: If this is not the main frame, we could clip the region to the frame's bounds.
    eventTrackingRegions.uniteSynchronousRegion(EventTrackingRegions::EventType::Wheel, nonFastScrollableRegion);

    return eventTrackingRegions;
#endif
}

EventTrackingRegions ScrollingCoordinator::absoluteEventTrackingRegions() const
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return EventTrackingRegions();
    return absoluteEventTrackingRegionsForFrame(*localMainFrame);
}

void ScrollingCoordinator::frameViewFixedObjectsDidChange(LocalFrameView& frameView)
{
    ASSERT(isMainThread());
    ASSERT(m_page);

    if (!coordinatesScrollingForFrameView(frameView))
        return;

    updateSynchronousScrollingReasons(frameView);
}

GraphicsLayer* ScrollingCoordinator::scrollContainerLayerForFrameView(LocalFrameView& frameView)
{
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().scrollContainerLayer();
    return nullptr;
}

GraphicsLayer* ScrollingCoordinator::scrolledContentsLayerForFrameView(LocalFrameView& frameView)
{
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().scrolledContentsLayer();
    return nullptr;
}

GraphicsLayer* ScrollingCoordinator::headerLayerForFrameView(LocalFrameView& frameView)
{
#if HAVE(RUBBER_BANDING)
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().headerLayer();
    return nullptr;
#else
    UNUSED_PARAM(frameView);
    return nullptr;
#endif
}

GraphicsLayer* ScrollingCoordinator::footerLayerForFrameView(LocalFrameView& frameView)
{
#if HAVE(RUBBER_BANDING)
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().footerLayer();
    return nullptr;
#else
    UNUSED_PARAM(frameView);
    return nullptr;
#endif
}

Page* ScrollingCoordinator::page() const
{
    return m_page.get();
}

RefPtr<Page> ScrollingCoordinator::protectedPage() const
{
    return m_page.get();
}

GraphicsLayer* ScrollingCoordinator::counterScrollingLayerForFrameView(LocalFrameView& frameView)
{
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().fixedRootBackgroundLayer();
    return nullptr;
}

GraphicsLayer* ScrollingCoordinator::insetClipLayerForFrameView(LocalFrameView& frameView)
{
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().clipLayer();
    return nullptr;
}

GraphicsLayer* ScrollingCoordinator::contentShadowLayerForFrameView(LocalFrameView& frameView)
{
#if HAVE(RUBBER_BANDING)
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().layerForContentShadow();
    return nullptr;
#else
    UNUSED_PARAM(frameView);
    return nullptr;
#endif
}

GraphicsLayer* ScrollingCoordinator::rootContentsLayerForFrameView(LocalFrameView& frameView)
{
    if (auto* renderView = frameView.frame().contentRenderer())
        return renderView->compositor().rootContentsLayer();
    return nullptr;
}

void ScrollingCoordinator::frameViewRootLayerDidChange(LocalFrameView& frameView)
{
    ASSERT(isMainThread());
    ASSERT(m_page);

    if (!coordinatesScrollingForFrameView(frameView))
        return;

    frameViewLayoutUpdated(frameView);
    updateSynchronousScrollingReasons(frameView);
}

bool ScrollingCoordinator::hasVisibleSlowRepaintViewportConstrainedObjects(const LocalFrameView& frameView) const
{
    auto* viewportConstrainedObjects = frameView.viewportConstrainedObjects();
    if (!viewportConstrainedObjects)
        return false;

    for (auto& viewportConstrainedObject : *viewportConstrainedObjects) {
        auto* viewportConstrainedBoxModelObject = dynamicDowncast<RenderBoxModelObject>(viewportConstrainedObject);
        if (!viewportConstrainedBoxModelObject || !viewportConstrainedBoxModelObject->hasLayer())
            return true;
        auto& layer = *viewportConstrainedBoxModelObject->layer();
        // Any explicit reason that a fixed position element is not composited shouldn't cause slow scrolling.
        if (!layer.isComposited() && layer.viewportConstrainedNotCompositedReason() == RenderLayer::NoNotCompositedReason)
            return true;
    }
    return false;
}

void ScrollingCoordinator::updateSynchronousScrollingReasons(LocalFrameView& frameView)
{
    ASSERT(coordinatesScrollingForFrameView(frameView));

    OptionSet<SynchronousScrollingReason> newSynchronousScrollingReasons;

    // RenderLayerCompositor::updateSynchronousScrollingReasons maintains this bit, so maintain its current value.
    if (synchronousScrollingReasons(frameView.scrollingNodeID()).contains(SynchronousScrollingReason::HasSlowRepaintObjects))
        newSynchronousScrollingReasons.add(SynchronousScrollingReason::HasSlowRepaintObjects);

    if (m_forceSynchronousScrollLayerPositionUpdates)
        newSynchronousScrollingReasons.add(SynchronousScrollingReason::ForcedOnMainThread);

    if (hasVisibleSlowRepaintViewportConstrainedObjects(frameView))
        newSynchronousScrollingReasons.add(SynchronousScrollingReason::HasNonLayerViewportConstrainedObjects);

    RefPtr page = frameView.frame().page();
    if (page && page->topDocumentHasDocumentClass(DocumentClass::Image))
        newSynchronousScrollingReasons.add(SynchronousScrollingReason::IsImageDocument);

    setSynchronousScrollingReasons(frameView.scrollingNodeID(), newSynchronousScrollingReasons);
}

void ScrollingCoordinator::updateSynchronousScrollingReasonsForAllFrames()
{
    for (RefPtr frame = m_page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        if (RefPtr frameView = localFrame->view()) {
            if (coordinatesScrollingForFrameView(*frameView))
                updateSynchronousScrollingReasons(*frameView);
        }
    }
}

void ScrollingCoordinator::setForceSynchronousScrollLayerPositionUpdates(bool forceSynchronousScrollLayerPositionUpdates)
{
    if (m_forceSynchronousScrollLayerPositionUpdates == forceSynchronousScrollLayerPositionUpdates)
        return;

    m_forceSynchronousScrollLayerPositionUpdates = forceSynchronousScrollLayerPositionUpdates;
    updateSynchronousScrollingReasonsForAllFrames();
}

bool ScrollingCoordinator::shouldUpdateScrollLayerPositionSynchronously(const LocalFrameView& frameView) const
{
    if (&frameView == m_page->mainFrame().virtualView())
        return !synchronousScrollingReasons(frameView.scrollingNodeID()).isEmpty();
    
    return true;
}

ScrollingNodeID ScrollingCoordinator::uniqueScrollingNodeID()
{
    return ScrollingNodeID::generate();
}

void ScrollingCoordinator::receivedWheelEventWithPhases(PlatformWheelEventPhase phase, PlatformWheelEventPhase momentumPhase)
{
    ASSERT(isMainThread());
    if (!m_page)
        return;

    if (auto monitor = m_page->wheelEventTestMonitor())
        monitor->receivedWheelEventWithPhases(phase, momentumPhase);
}

void ScrollingCoordinator::deferWheelEventTestCompletionForReason(ScrollingNodeID nodeID, WheelEventTestMonitor::DeferReason reason)
{
    ASSERT(isMainThread());
    if (!m_page)
        return;

    if (auto monitor = m_page->wheelEventTestMonitor())
        monitor->deferForReason(nodeID, reason);
}

void ScrollingCoordinator::removeWheelEventTestCompletionDeferralForReason(ScrollingNodeID nodeID, WheelEventTestMonitor::DeferReason reason)
{
    ASSERT(isMainThread());
    if (!m_page)
        return;

    if (auto monitor = m_page->wheelEventTestMonitor())
        monitor->removeDeferralForReason(nodeID, reason);
}

String ScrollingCoordinator::scrollingStateTreeAsText(OptionSet<ScrollingStateTreeAsTextBehavior>) const
{
    return emptyString();
}

String ScrollingCoordinator::scrollingTreeAsText(OptionSet<ScrollingStateTreeAsTextBehavior>) const
{
    return emptyString();
}

String ScrollingCoordinator::synchronousScrollingReasonsAsText(OptionSet<SynchronousScrollingReason> reasons)
{
    auto string = makeString(reasons.contains(SynchronousScrollingReason::ForcedOnMainThread) ? "Forced on main thread, "_s : ""_s,
        reasons.contains(SynchronousScrollingReason::HasSlowRepaintObjects) ? "Has slow repaint objects, "_s : ""_s,
        reasons.contains(SynchronousScrollingReason::HasViewportConstrainedObjectsWithoutSupportingFixedLayers) ? "Has viewport constrained objects without supporting fixed layers, "_s : ""_s,
        reasons.contains(SynchronousScrollingReason::HasNonLayerViewportConstrainedObjects) ? "Has non-layer viewport-constrained objects, "_s : ""_s,
        reasons.contains(SynchronousScrollingReason::IsImageDocument) ? "Is image document, "_s : ""_s,
        reasons.contains(SynchronousScrollingReason::DescendantScrollersHaveSynchronousScrolling) ? "Has slow repaint descendant scrollers, "_s : ""_s);
    return string.isEmpty() ? string : string.left(string.length() - 2);
}

String ScrollingCoordinator::synchronousScrollingReasonsAsText() const
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (localMainFrame) {
        if (auto* frameView = localMainFrame->view())
            return synchronousScrollingReasonsAsText(synchronousScrollingReasons(frameView->scrollingNodeID()));
    }

    return String();
}

FrameIdentifier ScrollingCoordinator::mainFrameIdentifier() const
{
    return m_page->mainFrame().frameID();
}

} // namespace WebCore
