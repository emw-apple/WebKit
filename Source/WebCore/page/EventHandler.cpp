/*
 * Copyright (C) 2006-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 * Copyright (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2012 Digia Plc. and/or its subsidiary(-ies)
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
#include "EventHandler.h"

#include "AutoscrollController.h"
#include "BackForwardController.h"
#include "CachedImage.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "CloseWatcherManager.h"
#include "ComposedTreeAncestorIterator.h"
#include "ComposedTreeIterator.h"
#include "ContainerNodeInlines.h"
#include "DocumentFullscreen.h"
#include "DocumentInlines.h"
#include "DocumentMarkerController.h"
#include "DragController.h"
#include "DragEvent.h"
#include "DragState.h"
#include "Editing.h"
#include "Editor.h"
#include "EditorClient.h"
#include "ElementInlines.h"
#include "EventNames.h"
#include "EventTargetInlines.h"
#include "FileList.h"
#include "FloatPoint.h"
#include "FloatRect.h"
#include "FocusController.h"
#include "FocusOptions.h"
#include "FrameLoader.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "HTMLAreaElement.h"
#include "HTMLDialogElement.h"
#include "HTMLDocument.h"
#include "HTMLFrameElement.h"
#include "HTMLFrameSetElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLPlugInElement.h"
#include "HTMLVideoElement.h"
#include "HandleUserInputEventResult.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "Image.h"
#include "ImageOverlay.h"
#include "ImageOverlayController.h"
#include "InspectorInstrumentation.h"
#include "KeyboardEvent.h"
#include "KeyboardScrollingAnimator.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "MouseEvent.h"
#include "MouseEventWithHitTestResults.h"
#include "NodeInlines.h"
#include "NotImplemented.h"
#include "PageInlines.h"
#include "PageOverlayController.h"
#include "Pasteboard.h"
#include "PlatformEvent.h"
#include "PlatformKeyboardEvent.h"
#include "PlatformMouseEvent.h"
#include "PlatformWheelEvent.h"
#include "PluginDocument.h"
#include "PointerCaptureController.h"
#include "PointerEventTypeNames.h"
#include "PseudoClassChangeInvalidation.h"
#include "Quirks.h"
#include "Range.h"
#include "RemoteFrame.h"
#include "RemoteFrameGeometryTransformer.h"
#include "RemoteFrameView.h"
#include "RemoteUserInputEventData.h"
#include "RenderFrameSet.h"
#include "RenderImage.h"
#include "RenderLayer.h"
#include "RenderLayerScrollableArea.h"
#include "RenderListBox.h"
#include "RenderTextControlSingleLine.h"
#include "RenderView.h"
#include "RenderWidget.h"
#include "ResourceLoadObserver.h"
#include "SVGDocument.h"
#include "SVGElementTypeHelpers.h"
#include "SVGNames.h"
#include "ScrollAnimator.h"
#include "ScrollLatchingController.h"
#include "Scrollbar.h"
#include "ScrollingCoordinator.h"
#include "ScrollingEffectsController.h"
#include "SelectionRestorationMode.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "StaticPasteboard.h"
#include "StyleCachedImage.h"
#include "TextEvent.h"
#include "TextIterator.h"
#include "TextRecognitionOptions.h"
#include "UserGestureIndicator.h"
#include "UserTypingGestureIndicator.h"
#include "ValidationMessageClient.h"
#include "VisibleUnits.h"
#include "WheelEvent.h"
#include "WheelEventDeltaFilter.h"
#include "WheelEventTestMonitor.h"
#include "WindowsKeyboardCodes.h"
#include <wtf/Assertions.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RuntimeApplicationChecks.h>
#include <wtf/Scope.h>
#include <wtf/SetForScope.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(IOS_TOUCH_EVENTS)
#include "PlatformTouchEventIOS.h"
#endif

#if ENABLE(CONTENT_CHANGE_OBSERVER)
#include "DOMTimerHoldingTank.h"
#endif

#if ENABLE(TOUCH_EVENTS)
#include "TouchEvent.h"
#include "TouchList.h"
#endif

#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
#include "PlatformTouchEvent.h"
#endif

#if ENABLE(MAC_GESTURE_EVENTS)
#include "PlatformGestureEventMac.h"
#endif

#if ENABLE(POINTER_LOCK)
#include "PointerLockController.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(EventHandler);

using namespace HTMLNames;

#if ENABLE(DRAG_SUPPORT)
// The link drag hysteresis is much larger than the others because there
// needs to be enough space to cancel the link press without starting a link drag,
// and because dragging links is rare.
const int LinkDragHysteresis = 40;
const int ImageDragHysteresis = 5;
const int TextDragHysteresis = 3;
const int ColorDragHystersis = 3;
const int GeneralDragHysteresis = 3;
#if PLATFORM(MAC)
const Seconds EventHandler::TextDragDelay { 150_ms };
#else
const Seconds EventHandler::TextDragDelay { 0_s };
#endif
#endif // ENABLE(DRAG_SUPPORT)

#if ENABLE(IOS_GESTURE_EVENTS) || ENABLE(MAC_GESTURE_EVENTS)
const float GestureUnknown = 0;
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
// FIXME: Share this constant with EventHandler and SliderThumbElement.
const unsigned InvalidTouchIdentifier = 0;
#endif

// Match key code of composition keydown event on windows.
// IE sends VK_PROCESSKEY which has value 229;
const int CompositionEventKeyCode = 229;

using namespace SVGNames;

#if !ENABLE(IOS_TOUCH_EVENTS)
// The amount of time to wait before sending a fake mouse event, triggered
// during a scroll. The short interval is used if the content responds to the mouse events
// in fakeMouseMoveDurationThreshold or less, otherwise the long interval is used.
const double fakeMouseMoveDurationThreshold = 0.01;
const Seconds fakeMouseMoveShortInterval = { 100_ms };
const Seconds fakeMouseMoveLongInterval = { 250_ms };
#endif

const int maximumCursorSize = 128;

#if ENABLE(MOUSE_CURSOR_SCALE)
// It's pretty unlikely that a scale of less than one would ever be used. But all we really
// need to ensure here is that the scale isn't so small that integer overflow can occur when
// dividing cursor sizes (limited above) by the scale.
const double minimumCursorScale = 0.001;
#endif

class MaximumDurationTracker {
public:
    explicit MaximumDurationTracker(double *maxDuration)
        : m_maxDuration(maxDuration)
        , m_start(MonotonicTime::now())
    {
    }

    ~MaximumDurationTracker()
    {
        *m_maxDuration = std::max(*m_maxDuration, (MonotonicTime::now() - m_start).seconds());
    }

private:
    double* m_maxDuration;
    MonotonicTime m_start;
};

static UserGestureType userGestureTypeForPlatformEvent(const PlatformKeyboardEvent& keyEvent)
{
    // https://html.spec.whatwg.org/multipage/interaction.html#activation-triggering-input-event
    // An activation triggering input event is any event whose isTrusted attribute is true and whose type is one of:
    // * "keydown", provided the key is neither the Esc key nor a shortcut key reserved by the user agent.
    if (keyEvent.windowsVirtualKeyCode() == VK_ESCAPE)
        return UserGestureType::EscapeKey;
    if (keyEvent.type() == PlatformEventType::KeyDown)
        return UserGestureType::ActivationTriggering;

    // FIXME: This check does not yet handle whether the event represents a "shortcut key reserved by the user agent".
    return UserGestureType::Other;
}

static UserGestureType userGestureTypeForPlatformEvent(const PlatformMouseEvent& mouseEvent)
{
    // ...
    // * "mousedown".
    // * "pointerdown", provided the event's pointerType is "mouse".
    if (mouseEvent.type() == PlatformEventType::MousePressed)
        return UserGestureType::ActivationTriggering;
    return UserGestureType::Other;
}

#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
static UserGestureType userGestureTypeForPlatformEvent(const PlatformTouchEvent& touchEvent)
{
    // ...
    // * "pointerup", provided the event's pointerType is not "mouse".
    // * "touchend".
    if (touchEvent.type() == PlatformEventType::TouchEnd)
        return UserGestureType::ActivationTriggering;
    return UserGestureType::Other;
}
#endif

#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
class SyntheticTouchPoint : public PlatformTouchPoint {
public:

    // The default values are based on http://dvcs.w3.org/hg/webevents/raw-file/tip/touchevents.html
    explicit SyntheticTouchPoint(const PlatformMouseEvent& event)
    {
        static constexpr int idDefaultValue = 0;
        static constexpr int radiusYDefaultValue = 1;
        static constexpr int radiusXDefaultValue = 1;
        static constexpr float rotationAngleDefaultValue = 0.0f;
        static constexpr float forceDefaultValue = 1.0f;

        m_id = idDefaultValue; // There is only one active TouchPoint.
        m_screenPos = event.globalPosition();
        m_pos = event.position();
        m_radiusY = radiusYDefaultValue;
        m_radiusX = radiusXDefaultValue;
        m_rotationAngle = rotationAngleDefaultValue;
        m_force = forceDefaultValue;

        PlatformEvent::Type type = event.type();
        ASSERT(type == PlatformEvent::Type::MouseMoved || type == PlatformEvent::Type::MousePressed || type == PlatformEvent::Type::MouseReleased);

        switch (type) {
        case PlatformEvent::Type::MouseMoved:
            m_state = TouchMoved;
            break;
        case PlatformEvent::Type::MousePressed:
            m_state = TouchPressed;
            break;
        case PlatformEvent::Type::MouseReleased:
            m_state = TouchReleased;
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    }
};

class SyntheticSingleTouchEvent : public PlatformTouchEvent {
public:
    explicit SyntheticSingleTouchEvent(const PlatformMouseEvent& event)
    {
        switch (event.type()) {
        case PlatformEvent::Type::MouseMoved:
            m_type = Type::TouchMove;
            break;
        case PlatformEvent::Type::MousePressed:
            m_type = Type::TouchStart;
            break;
        case PlatformEvent::Type::MouseReleased:
            m_type = Type::TouchEnd;
            break;
        default:
            ASSERT_NOT_REACHED();
            m_type = Type::NoType;
            break;
        }
        m_timestamp = event.timestamp();
        m_modifiers = event.modifiers();
        m_touchPoints.append(SyntheticTouchPoint(event));
    }
};
#endif // ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)

static inline ScrollGranularity wheelGranularityToScrollGranularity(unsigned deltaMode)
{
    switch (deltaMode) {
    case WheelEvent::DOM_DELTA_PAGE:
        return ScrollGranularity::Page;
    case WheelEvent::DOM_DELTA_LINE:
        return ScrollGranularity::Line;
    case WheelEvent::DOM_DELTA_PIXEL:
        return ScrollGranularity::Pixel;
    default:
        return ScrollGranularity::Pixel;
    }
}

#if (ENABLE(TOUCH_EVENTS) && !PLATFORM(IOS_FAMILY))
static bool shouldGesturesTriggerActive()
{
    // If the platform we're on supports GestureTapDown and GestureTapCancel then we'll
    // rely on them to set the active state. Unfortunately there's no generic way to
    // know in advance what event types are supported.
    return false;
}
#endif

#if !PLATFORM(COCOA)

bool EventHandler::eventLoopHandleMouseUp(const MouseEventWithHitTestResults&)
{
    return false;
}

#if ENABLE(DRAG_SUPPORT)
bool EventHandler::eventLoopHandleMouseDragged(const MouseEventWithHitTestResults&)
{
    return false;
}
#endif

#endif

// Refetch the event target node if it is removed or currently is the shadow node inside an <input> element.
// If a mouse event handler changes the input element type to one that has a widget associated,
// we'd like to EventHandler::handleMousePressEvent to pass the event to the widget and thus the
// event target node can't still be the shadow node.
static inline bool shouldRefetchEventTarget(const MouseEventWithHitTestResults& mouseEvent)
{
    RefPtr targetNode = mouseEvent.targetNode();
    ASSERT(targetNode);
    if (!targetNode->parentNode())
        return true;
    RefPtr shadowRoot = dynamicDowncast<ShadowRoot>(*targetNode);
    return shadowRoot && is<HTMLInputElement>(shadowRoot->host());
}

EventHandler::EventHandler(LocalFrame& frame)
    : m_frame(frame)
    , m_hoverTimer(*this, &EventHandler::hoverTimerFired)
#if ENABLE(IMAGE_ANALYSIS)
    , m_textRecognitionHoverTimer(*this, &EventHandler::textRecognitionHoverTimerFired, 250_ms)
#endif
    , m_autoscrollController(makeUniqueRef<AutoscrollController>())
#if !ENABLE(IOS_TOUCH_EVENTS)
    , m_fakeMouseMoveEventTimer(*this, &EventHandler::fakeMouseMoveEventTimerFired)
#endif
#if ENABLE(CURSOR_VISIBILITY)
    , m_autoHideCursorTimer(*this, &EventHandler::autoHideCursorTimerFired)
#endif
{
}

EventHandler::~EventHandler()
{
#if !ENABLE(IOS_TOUCH_EVENTS)
    ASSERT(!m_fakeMouseMoveEventTimer.isActive());
#endif
#if ENABLE(CURSOR_VISIBILITY)
    ASSERT(!m_autoHideCursorTimer.isActive());
#endif
}
    
#if ENABLE(DRAG_SUPPORT)

DragState& EventHandler::dragState()
{
    static NeverDestroyed<DragState> state;
    return state;
}

Element* EventHandler::draggedElement()
{
    return dragState().source.get();
}

RefPtr<Element> EventHandler::protectedDraggedElement()
{
    return dragState().source;
}

#endif
    
void EventHandler::clear()
{
    m_hoverTimer.stop();
    m_hasScheduledCursorUpdate = false;
#if !ENABLE(IOS_TOUCH_EVENTS)
    m_fakeMouseMoveEventTimer.stop();
#endif
#if ENABLE(CURSOR_VISIBILITY)
    cancelAutoHideCursorTimer();
#endif
#if ENABLE(IMAGE_ANALYSIS)
    m_textRecognitionHoverTimer.stop();
#endif
    m_resizeLayer = nullptr;
    clearElementUnderMouse();
    m_lastElementUnderMouse = nullptr;
    m_lastMouseMoveEventSubframe = nullptr;
    m_lastScrollbarUnderMouse = nullptr;
    m_clickCount = 0;
    m_clickNode = nullptr;
#if ENABLE(IOS_GESTURE_EVENTS)
    m_gestureInitialDiameter = GestureUnknown;
    m_gestureInitialRotation = GestureUnknown;
#endif
#if ENABLE(IOS_GESTURE_EVENTS) || ENABLE(MAC_GESTURE_EVENTS)
    m_gestureLastDiameter = GestureUnknown;
    m_gestureLastRotation = GestureUnknown;
    m_gestureTargets.clear();
#endif
#if ENABLE(IOS_TOUCH_EVENTS)
    m_touches.clear();
    m_touchLastGlobalPositionAndDeltaMap.clear();
    m_firstTouchID = InvalidTouchIdentifier;
    m_touchEventTargetSubframe = nullptr;
#endif
    m_frameSetBeingResized = nullptr;
#if ENABLE(DRAG_SUPPORT)
    m_dragTarget = nullptr;
    m_shouldOnlyFireDragOverEvent = false;
#endif
    m_lastKnownMousePosition = std::nullopt;
    m_lastKnownMouseGlobalPosition = { };
    m_mousePressNode = nullptr;
    m_mousePressed = false;
    m_capturesDragging = false;
    resetCapturingMouseEventsElement();
    clearLatchedState();
#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
    m_originatingTouchPointTargets.clear();
    m_originatingTouchPointDocument = nullptr;
    m_originatingTouchPointTargetKey = 0;
    m_touchPressed = false;
#endif
    m_maxMouseMovedDuration = 0;
    m_didStartDrag = false;
}

void EventHandler::nodeWillBeRemoved(Node& nodeToBeRemoved)
{
    if (nodeToBeRemoved.isShadowIncludingInclusiveAncestorOf(RefPtr { m_clickNode }.get()))
        m_clickNode = nullptr;

    if (nodeToBeRemoved.isShadowIncludingInclusiveAncestorOf(RefPtr { m_lastElementUnderMouse }.get()))
        m_lastElementUnderMouse = nullptr;
}

static void setSelectionIfNeeded(FrameSelection& selection, const VisibleSelection& newSelection)
{
    if (selection.selection() != newSelection && selection.shouldChangeSelection(newSelection))
        selection.setSelection(newSelection, FrameSelection::defaultSetSelectionOptions(UserTriggered::Yes));
}

static inline bool dispatchSelectStart(Node* node)
{
    if (!node || !node->renderer())
        return true;

    auto event = Event::create(eventNames().selectstartEvent, Event::CanBubble::Yes, Event::IsCancelable::Yes);
    node->dispatchEvent(event);
    return !event->defaultPrevented();
}

static Node* nodeToSelectOnMouseDownForNode(Node& targetNode)
{
    if (ImageOverlay::isInsideOverlay(targetNode))
        return nullptr;

    if (RefPtr rootUserSelectAll = Position::rootUserSelectAllForNode(&targetNode))
        return rootUserSelectAll.get();

    if (targetNode.shouldSelectOnMouseDown())
        return &targetNode;

    return nullptr;
}

static VisibleSelection expandSelectionToRespectSelectOnMouseDown(Node& targetNode, const VisibleSelection& selection)
{
    RefPtr nodeToSelect = nodeToSelectOnMouseDownForNode(targetNode);
    if (!nodeToSelect)
        return selection;

    VisibleSelection newSelection(selection);
    newSelection.setBase(positionBeforeNode(nodeToSelect.get()).upstream(CanCrossEditingBoundary));
    newSelection.setExtent(positionAfterNode(nodeToSelect.get()).downstream(CanCrossEditingBoundary));

    return newSelection;
}

static bool shouldAvoidExtendingSelectionOnClick(const Node& targetNode, const VisibleSelection& selection)
{
    if (is<Text>(targetNode))
        return false;

    if (selection.isContentEditable())
        return false;

    auto range = selection.toNormalizedRange();
    if (!range)
        return false;

    if (range->collapsed())
        return false;

    static constexpr OptionSet plainTextOptions {
        TextIteratorBehavior::EmitsObjectReplacementCharacters,
        TextIteratorBehavior::EntersTextControls,
    };

    if (hasAnyPlainText(*range, plainTextOptions, IgnoreCollapsedRanges::Yes))
        return false;

    return true;
}

bool EventHandler::expandAndUpdateSelectionForMouseDownIfNeeded(Node& targetNode, const VisibleSelection& selection, TextGranularity granularity)
{
    auto expandedSelection = expandSelectionToRespectSelectOnMouseDown(targetNode, selection);
    if (shouldAvoidExtendingSelectionOnClick(targetNode, expandedSelection))
        return false;

    return updateSelectionForMouseDownDispatchingSelectStart(&targetNode, expandedSelection, granularity);
}

bool EventHandler::updateSelectionForMouseDownDispatchingSelectStart(Node* targetNode, const VisibleSelection& selection, TextGranularity granularity)
{
    if (Position::nodeIsUserSelectNone(targetNode))
        return false;

    if (!dispatchSelectStart(targetNode)) {
        m_mouseDownMayStartSelect = false;
        return false;
    }

    if (selection.isOrphan()) {
        m_mouseDownMayStartSelect = false;
        return false;
    }

    if (selection.isRange()) {
        m_selectionInitiationState = ExtendedSelection;
#if ENABLE(DRAG_SUPPORT)
        m_dragStartSelection = getWeakSimpleRangeFromSelection(selection);
#endif
    } else {
        granularity = TextGranularity::CharacterGranularity;
        m_selectionInitiationState = PlacedCaret;
    }

    protectedFrame()->selection().setSelectionByMouseIfDifferent(selection, granularity);

    return true;
}

void EventHandler::selectClosestWordFromHitTestResult(const HitTestResult& result, AppendTrailingWhitespace appendTrailingWhitespace)
{
    RefPtr targetNode = result.targetNode();
    VisibleSelection newSelection;

    if (targetNode && targetNode->renderer()) {
        VisiblePosition pos(targetNode->renderer()->positionForPoint(result.localPoint(), HitTestSource::User, nullptr));
        if (pos.isNotNull()) {
            newSelection = VisibleSelection(pos);
            newSelection.expandUsingGranularity(TextGranularity::WordGranularity);
        }

        if (appendTrailingWhitespace == ShouldAppendTrailingWhitespace && newSelection.isRange())
            newSelection.appendTrailingWhitespace();

        expandAndUpdateSelectionForMouseDownIfNeeded(*targetNode, newSelection, TextGranularity::WordGranularity);
    }
}

static AppendTrailingWhitespace shouldAppendTrailingWhitespace(const MouseEventWithHitTestResults& result, const LocalFrame& frame)
{
    return (result.event().clickCount() == 2 && frame.editor().isSelectTrailingWhitespaceEnabled()) ? ShouldAppendTrailingWhitespace : DontAppendTrailingWhitespace;
}

#if !PLATFORM(COCOA)
VisibleSelection EventHandler::selectClosestWordFromHitTestResultBasedOnLookup(const HitTestResult&)
{
    return VisibleSelection();
}
#endif
    
void EventHandler::selectClosestContextualWordFromHitTestResult(const HitTestResult& result, AppendTrailingWhitespace appendTrailingWhitespace)
{
    RefPtr targetNode = result.targetNode();
    VisibleSelection newSelection;
    
    if (targetNode && targetNode->renderer()) {
        newSelection = selectClosestWordFromHitTestResultBasedOnLookup(result);
        if (newSelection.isNone()) {
            VisiblePosition pos(targetNode->renderer()->positionForPoint(result.localPoint(), HitTestSource::User, nullptr));
            if (pos.isNotNull()) {
                newSelection = VisibleSelection(pos);
                newSelection.expandUsingGranularity(TextGranularity::WordGranularity);
            }
        }
        
        if (appendTrailingWhitespace == ShouldAppendTrailingWhitespace && newSelection.isRange())
            newSelection.appendTrailingWhitespace();
        
        updateSelectionForMouseDownDispatchingSelectStart(targetNode.get(), expandSelectionToRespectSelectOnMouseDown(*targetNode, newSelection), TextGranularity::WordGranularity);
    }
}
    
void EventHandler::selectClosestContextualWordOrLinkFromHitTestResult(const HitTestResult& result, AppendTrailingWhitespace appendTrailingWhitespace)
{
    // FIXME: In the editable case, word selection sometimes selects content that isn't underneath the mouse.
    // If the selection is non-editable, we do word selection to make it easier to use the contextual menu items
    // available for text selections. But only if we're above text.
    if (!m_frame->selection().selection().isContentEditable() && !is<Text>(result.targetNode()))
        return;

    if (!m_frame->settings().textInteractionEnabled())
        return;

    RefPtr urlElement = result.URLElement();
    if (!urlElement || !isDraggableLink(*urlElement)) {
        if (RefPtr targetNode = result.targetNode(); targetNode && isEditableNode(*targetNode)) {
            selectClosestWordFromHitTestResult(result, appendTrailingWhitespace);
            return;
        }

        return selectClosestContextualWordFromHitTestResult(result, appendTrailingWhitespace);
    }

    if (RefPtr targetNode = result.targetNode(); targetNode && targetNode->renderer()) {
        VisibleSelection newSelection;
        VisiblePosition pos(targetNode->renderer()->positionForPoint(result.localPoint(), HitTestSource::User, nullptr));
        if (pos.isNotNull() && pos.deepEquivalent().deprecatedNode()->isDescendantOf(*urlElement))
            newSelection = VisibleSelection::selectionFromContentsOfNode(urlElement.get());

        updateSelectionForMouseDownDispatchingSelectStart(targetNode.get(), expandSelectionToRespectSelectOnMouseDown(*targetNode, newSelection), TextGranularity::WordGranularity);
    }
}

bool EventHandler::handleMousePressEventDoubleClick(const MouseEventWithHitTestResults& event)
{
    if (event.event().button() != MouseButton::Left)
        return false;

    if (m_frame->selection().isRange()) {
        // A double-click when range is already selected
        // should not change the selection.  So, do not call
        // selectClosestWordFromHitTestResult, but do set
        // m_beganSelectingText to prevent handleMouseReleaseEvent
        // from setting caret selection.
        m_selectionInitiationState = ExtendedSelection;
#if ENABLE(DRAG_SUPPORT)
        m_dragStartSelection = getWeakSimpleRangeFromSelection(m_frame->selection().selection());
#endif
    } else if (mouseDownMayStartSelect())
        selectClosestWordFromHitTestResult(event.hitTestResult(), shouldAppendTrailingWhitespace(event, protectedFrame()));

    return true;
}

bool EventHandler::handleMousePressEventTripleClick(const MouseEventWithHitTestResults& event)
{
    if (event.event().button() != MouseButton::Left)
        return false;
    
    RefPtr targetNode = event.targetNode();
    if (!(targetNode && targetNode->renderer() && mouseDownMayStartSelect()))
        return false;

    VisibleSelection newSelection;
    VisiblePosition pos(targetNode->renderer()->positionForPoint(event.localPoint(), HitTestSource::User, nullptr));
    if (pos.isNotNull()) {
        newSelection = VisibleSelection(pos);
        newSelection.expandUsingGranularity(TextGranularity::ParagraphGranularity);
    }

    return expandAndUpdateSelectionForMouseDownIfNeeded(*targetNode, newSelection, TextGranularity::ParagraphGranularity);
}

static uint64_t textDistance(const Position& start, const Position& end)
{
    auto range = makeSimpleRange(start, end);
    if (!range)
        return 0;
    return characterCount(*range, TextIteratorBehavior::EmitsCharactersBetweenAllVisiblePositions);
}

bool EventHandler::handleMousePressEventSingleClick(const MouseEventWithHitTestResults& event)
{
    Ref frame = m_frame.get();
    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();
    RefPtr targetNode = event.targetNode();
    if (!targetNode || !targetNode->renderer() || !mouseDownMayStartSelect() || m_mouseDownDelegatedFocus)
        return false;

    // Extend the selection if the Shift key is down, unless the click is in a link.
    bool extendSelection = event.event().shiftKey() && !event.isOverLink();

    // Don't restart the selection when the mouse is pressed on an
    // existing selection so we can allow for text dragging.
    if (RefPtr view = frame->view()) {
        LayoutPoint vPoint = view->windowToContents(event.event().position());
        if (!extendSelection && frame->selection().contains(vPoint)) {
            m_mouseDownWasSingleClickInSelection = true;
            return false;
        }
    }

    VisiblePosition visiblePosition(targetNode->renderer()->positionForPoint(event.localPoint(), HitTestSource::User, nullptr));
    if (visiblePosition.isNull())
        visiblePosition = VisiblePosition(firstPositionInOrBeforeNode(targetNode.get()));
    Position pos = visiblePosition.deepEquivalent();

    VisibleSelection newSelection = frame->selection().selection();
    TextGranularity granularity = TextGranularity::CharacterGranularity;

    if (!frame->editor().client()->shouldAllowSingleClickToChangeSelection(*targetNode, newSelection))
        return true;

    if (extendSelection && newSelection.isCaretOrRange()) {
        VisibleSelection selectionInUserSelectAll = expandSelectionToRespectSelectOnMouseDown(*targetNode, VisibleSelection(pos));
        if (selectionInUserSelectAll.isRange()) {
            if (selectionInUserSelectAll.start() < newSelection.start())
                pos = selectionInUserSelectAll.start();
            else if (newSelection.end() < selectionInUserSelectAll.end())
                pos = selectionInUserSelectAll.end();
        }

        if (!frame->editor().behavior().shouldConsiderSelectionAsDirectional() && pos.isNotNull()) {
            // See <rdar://problem/3668157> REGRESSION (Mail): shift-click deselects when selection
            // was created right-to-left
            Position start = newSelection.start();
            Position end = newSelection.end();
            int distanceToStart = textDistance(start, pos);
            int distanceToEnd = textDistance(pos, end);
            if (distanceToStart <= distanceToEnd)
                newSelection = VisibleSelection(end, pos);
            else
                newSelection = VisibleSelection(start, pos);
        } else {
            if (newSelection.directionality() == Directionality::Strong) {
                RefPtr baseNode = newSelection.isBaseFirst() ? newSelection.base().computeNodeAfterPosition() : newSelection.base().computeNodeBeforePosition();
                if (!baseNode)
                    baseNode = newSelection.base().containerNode();
                if (baseNode) {
                    auto expandedBaseSelection = expandSelectionToRespectSelectOnMouseDown(*baseNode, VisibleSelection { newSelection.visibleBase() });
                    expandedBaseSelection.expandUsingGranularity(frame->selection().granularity());
                    if (expandedBaseSelection.isRange()) {
                        if (newSelection.isBaseFirst() && pos < newSelection.start())
                            newSelection.setBase(expandedBaseSelection.end());
                        else if (!newSelection.isBaseFirst() && newSelection.end() < pos)
                            newSelection.setBase(expandedBaseSelection.start());
                    }
                }
            }
            newSelection.setExtent(pos);
        }

        if (frame->selection().granularity() != TextGranularity::CharacterGranularity) {
            granularity = frame->selection().granularity();
            newSelection.expandUsingGranularity(frame->selection().granularity());
        }
    } else {
        if (event.event().syntheticClickType() != SyntheticClickType::NoTap) {
            auto adjustedVisiblePosition = wordBoundaryForPositionWithoutCrossingLine(visiblePosition).first;
            if (adjustedVisiblePosition.isNotNull())
                visiblePosition = WTFMove(adjustedVisiblePosition);
        }
        newSelection = expandSelectionToRespectSelectOnMouseDown(*targetNode, visiblePosition);
    }

    return updateSelectionForMouseDownDispatchingSelectStart(targetNode.get(), newSelection, granularity);
}

bool EventHandler::canMouseDownStartSelect(const MouseEventWithHitTestResults& event)
{
    RefPtr node = event.targetNode();

    if (RefPtr page = m_frame->page()) {
        if (!page->chrome().client().shouldUseMouseEventForSelection(event.event()))
            return false;
    }

    if (!node || !node->renderer())
        return true;

    if (node->protectedDocument()->quirks().shouldAvoidStartingSelectionOnMouseDownOverPointerCursor(*node))
        return false;

    if (ImageOverlay::isOverlayText(*node))
        return node->renderer()->style().usedUserSelect() != UserSelect::None;

    return node->canStartSelection() || Position::nodeIsUserSelectAll(node.get());
}

bool EventHandler::mouseDownMayStartSelect() const
{
    if (!m_frame->settings().textInteractionEnabled())
        return false;

    return m_mouseDownMayStartSelect;
}

bool EventHandler::handleMousePressEvent(const MouseEventWithHitTestResults& event)
{
    Ref frame = m_frame.get();

#if ENABLE(DRAG_SUPPORT)
    // Reset drag state.
    setDragStateSource(nullptr);
#endif

#if !ENABLE(IOS_TOUCH_EVENTS)
    cancelFakeMouseMoveEvent();
#endif

    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();

    RefPtr view = frame->view();
    if (view && view->isPointInScrollbarCorner(event.event().position()))
        return false;

    bool singleClick = event.event().clickCount() <= 1;

    // If we got the event back, that must mean it wasn't prevented,
    // so it's allowed to start a drag or selection if it wasn't in a scrollbar.
    m_mouseDownMayStartSelect = canMouseDownStartSelect(event) && !event.scrollbar();
    
#if ENABLE(DRAG_SUPPORT)
    // Careful that the drag starting logic stays in sync with eventMayStartDrag()
    // FIXME: eventMayStartDrag() does not check for shift key press, link or image event targets.
    // Bug: https://bugs.webkit.org/show_bug.cgi?id=155390

    // Single mouse down on links or images can always trigger drag-n-drop.
    bool isImageOverlayText = ImageOverlay::isOverlayText(event.protectedTargetNode().get());
    bool isMouseDownOnLinkOrImage = event.isOverLink() || (event.hitTestResult().image() && !isImageOverlayText);
    m_mouseDownMayStartDrag = singleClick && (!event.event().shiftKey() || isMouseDownOnLinkOrImage) && shouldAllowMouseDownToStartDrag();
#endif

    m_mouseDownWasSingleClickInSelection = false;

    m_mouseDownEvent = event.event();

    if (m_immediateActionStage != ImmediateActionStage::PerformedHitTest)
        m_immediateActionStage = ImmediateActionStage::None;

    if (event.isOverWidget() && passWidgetMouseDownEventToWidget(event))
        return true;

    if (RefPtr svgDocument = dynamicDowncast<SVGDocument>(*frame->protectedDocument()); svgDocument && svgDocument->zoomAndPanEnabled()) {
        if (event.event().shiftKey() && singleClick) {
            m_svgPan = true;
            svgDocument->startPan(frame->protectedView()->windowToContents(event.event().position()));
            return true;
        }
    }

    // We don't do this at the start of mouse down handling,
    // because we don't want to do it until we know we didn't hit a widget.
    if (singleClick)
        focusDocumentView();

    m_mousePressNode = event.targetNode();
    frame->protectedDocument()->setFocusNavigationStartingNode(event.protectedTargetNode().get());

#if ENABLE(DRAG_SUPPORT)
    m_dragStartPosition = event.event().position();
#endif

    m_mousePressed = true;
    m_selectionInitiationState = HaveNotStartedSelection;

    bool swallowEvent = false;
    if (event.event().clickCount() == 2)
        swallowEvent = handleMousePressEventDoubleClick(event);
    else if (event.event().clickCount() >= 3)
        swallowEvent = handleMousePressEventTripleClick(event);
    else
        swallowEvent = handleMousePressEventSingleClick(event);

    m_mouseDownMayStartAutoscroll = [&] {
        if (view) {
            auto absolutePosition = view->windowToContents(event.event().position());
            if (!view->visualViewportRect().contains(LayoutPoint { view->absoluteToDocumentPoint(absolutePosition) }))
                return false;
        }

        if (mouseDownMayStartSelect())
            return true;

        if (m_mousePressNode && m_mousePressNode->renderBox() && m_mousePressNode->renderBox()->canBeProgramaticallyScrolled())
            return true;

        return false;
    }();

    return swallowEvent;
}

VisiblePosition EventHandler::selectionExtentRespectingEditingBoundary(const VisibleSelection& selection, const LayoutPoint& localPoint, Node* targetNode)
{
    FloatPoint selectionEndPoint = localPoint;
    RefPtr editableElement = selection.rootEditableElement();

    if (!targetNode || !targetNode->renderer())
        return VisiblePosition();

    RefPtr adjustedTarget = targetNode;
    if (editableElement && !editableElement->contains(targetNode)) {
        if (!editableElement->renderer())
            return VisiblePosition();

        FloatPoint absolutePoint = targetNode->renderer()->localToAbsolute(FloatPoint(selectionEndPoint));
        selectionEndPoint = editableElement->renderer()->absoluteToLocal(absolutePoint);
        adjustedTarget = editableElement;
    }

    return adjustedTarget->renderer()->positionForPoint(LayoutPoint(selectionEndPoint), HitTestSource::User, nullptr);
}

#if ENABLE(DRAG_SUPPORT)

#if !PLATFORM(IOS_FAMILY)

bool EventHandler::supportsSelectionUpdatesOnMouseDrag() const
{
    return true;
}

bool EventHandler::shouldAllowMouseDownToStartDrag() const
{
    return true;
}

#endif

bool EventHandler::handleMouseDraggedEvent(const MouseEventWithHitTestResults& event, CheckDragHysteresis checkDragHysteresis)
{
    if (!m_mousePressed)
        return false;

    Ref frame = m_frame.get();

    if (handleDrag(event, checkDragHysteresis))
        return true;

    RefPtr targetNode = event.targetNode();
    if (event.event().button() != MouseButton::Left || !targetNode)
        return false;

    RenderObject* renderer = targetNode->renderer();
    if (!renderer) {
        RefPtr parent = targetNode->parentOrShadowHostElement();
        if (!parent)
            return false;

        renderer = parent->renderer();
        if (!renderer || !renderer->isRenderListBox())
            return false;
    }

#if PLATFORM(COCOA) // FIXME: Why does this assertion fire on other platforms?
    ASSERT(mouseDownMayStartSelect() || m_mouseDownMayStartAutoscroll);
#endif

    m_mouseDownMayStartDrag = false;

    if (m_mouseDownMayStartAutoscroll && !panScrollInProgress()) {
        m_autoscrollController->startAutoscrollForSelection(renderer);
        m_mouseDownMayStartAutoscroll = false;
    }

    if (m_selectionInitiationState != ExtendedSelection) {
        HitTestResult result(m_mouseDownContentsPosition);
        frame->protectedDocument()->hitTest(HitTestRequest(), result);

        updateSelectionForMouseDrag(result);
    } else
        event.targetNode()->protectedDocument()->updateStyleIfNeeded();
    updateSelectionForMouseDrag(event.hitTestResult());
    return true;
}
    
bool EventHandler::eventMayStartDrag(const PlatformMouseEvent& event) const
{
    // This is a pre-flight check of whether the event might lead to a drag being started.  Be careful
    // that its logic needs to stay in sync with handleMouseMoveEvent() and the way we setMouseDownMayStartDrag
    // in handleMousePressEvent
    Ref frame = m_frame.get();
    RefPtr document = frame->document();
    if (!document)
        return false;

    if (event.button() != MouseButton::Left || event.clickCount() != 1)
        return false;

    RefPtr view = frame->view();
    if (!view)
        return false;

    RefPtr page = frame->page();
    if (!page)
        return false;

    updateDragSourceActionsAllowed();
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::DisallowUserAgentShadowContent };
    HitTestResult result(view->windowToContents(event.position()));
    document->hitTest(hitType, result);
    DragState state;
    RefPtr targetElement = result.targetElement();
    return targetElement && page->dragController().draggableElement(frame.ptr(), targetElement.get(), result.roundedPointInInnerNodeFrame(), state);
}

void EventHandler::updateSelectionForMouseDrag()
{
    if (!supportsSelectionUpdatesOnMouseDrag())
        return;

    RefPtr view = m_frame->view();
    if (!view)
        return;
    RefPtr document = m_frame->document();
    if (!document)
        return;

    constexpr OptionSet<HitTestRequest::Type> hitType {  HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::Move, HitTestRequest::Type::DisallowUserAgentShadowContent };
    HitTestResult result(view->windowToContents(valueOrDefault(m_lastKnownMousePosition)));
    document->hitTest(hitType, result);
    updateSelectionForMouseDrag(result);
}

void EventHandler::updateSelectionForMouseDrag(const HitTestResult& hitTestResult)
{
    if (!supportsSelectionUpdatesOnMouseDrag())
        return;

    if (!mouseDownMayStartSelect())
        return;

    RefPtr target = hitTestResult.targetNode();
    if (!target)
        return;

    if (!HTMLElement::shouldExtendSelectionToTargetNode(*target, m_frame->selection().selection()))
        return;

    VisiblePosition targetPosition = selectionExtentRespectingEditingBoundary(m_frame->selection().selection(), hitTestResult.localPoint(), target.get());

    // Don't modify the selection if we're not on a node.
    if (targetPosition.isNull())
        return;

    // Restart the selection if this is the first mouse move. This work is usually
    // done in handleMousePressEvent, but not if the mouse press was on an existing selection.
    VisibleSelection oldSelection = m_frame->selection().selection();
    auto newSelection = oldSelection;

    // Special case to limit selection to the containing block for SVG text.
    // FIXME: Isn't there a better non-SVG-specific way to do this?
    if (RefPtr selectionBaseNode = newSelection.base().deprecatedNode()) {
        if (RenderObject* selectionBaseRenderer = selectionBaseNode->renderer()) {
            if (selectionBaseRenderer->isRenderSVGText()) {
                if (target->renderer()->containingBlock() != selectionBaseRenderer->containingBlock())
                    return;
            }
        }
    }


    if (m_selectionInitiationState == HaveNotStartedSelection && !dispatchSelectStart(target.get())) {
        m_mouseDownMayStartSelect = false;
        return;
    }

    bool shouldSetDragStartSelection = false;
    if (m_selectionInitiationState != ExtendedSelection) {
        // Always extend selection here because it's caused by a mouse drag
        m_selectionInitiationState = ExtendedSelection;
        newSelection = VisibleSelection(targetPosition);
        shouldSetDragStartSelection = true;
    }

    RefPtr rootUserSelectAllForMousePressNode = Position::rootUserSelectAllForNode(m_mousePressNode.get());
    if (rootUserSelectAllForMousePressNode && rootUserSelectAllForMousePressNode == Position::rootUserSelectAllForNode(target.get())) {
        newSelection.setBase(positionBeforeNode(rootUserSelectAllForMousePressNode.get()).upstream(CanCrossEditingBoundary));
        newSelection.setExtent(positionAfterNode(rootUserSelectAllForMousePressNode.get()).downstream(CanCrossEditingBoundary));
    } else {
        // Reset base for user select all when base is inside user-select-all area and extent < base.
        if (rootUserSelectAllForMousePressNode && target->renderer()->positionForPoint(hitTestResult.localPoint(), HitTestSource::User, nullptr) < m_mousePressNode->renderer()->positionForPoint(m_dragStartPosition, HitTestSource::User, nullptr))
            newSelection.setBase(positionAfterNode(rootUserSelectAllForMousePressNode.get()).downstream(CanCrossEditingBoundary));
        
        RefPtr rootUserSelectAllForTarget = Position::rootUserSelectAllForNode(target.get());
        if (rootUserSelectAllForTarget && m_mousePressNode->renderer() && target->renderer()->positionForPoint(hitTestResult.localPoint(), HitTestSource::User, nullptr) < m_mousePressNode->renderer()->positionForPoint(m_dragStartPosition, HitTestSource::User, nullptr))
            newSelection.setExtent(positionBeforeNode(rootUserSelectAllForTarget.get()).upstream(CanCrossEditingBoundary));
        else if (rootUserSelectAllForTarget && m_mousePressNode->renderer())
            newSelection.setExtent(positionAfterNode(rootUserSelectAllForTarget.get()).downstream(CanCrossEditingBoundary));
        else
            newSelection.setExtent(targetPosition);
    }

    if (m_frame->selection().granularity() != TextGranularity::CharacterGranularity) {
        newSelection.expandUsingGranularity(m_frame->selection().granularity());
        if (!newSelection.isBaseFirst() && !oldSelection.isBaseFirst() && oldSelection.end() < newSelection.end())
            newSelection.setBase(oldSelection.end());
        else if (newSelection.isBaseFirst() && !oldSelection.isBaseFirst() && oldSelection.start() < newSelection.start() && m_dragStartSelection && m_dragStartSelection->start.container && m_dragStartSelection->end.container) {
            VisibleSelection dragStartSelection { createSimpleRangeFromDragStartSelection() };
            dragStartSelection.expandUsingGranularity(m_frame->selection().granularity());
            if (!dragStartSelection.isNoneOrOrphaned())
                newSelection.setBase(dragStartSelection.start());
        }
    }

    if (shouldSetDragStartSelection)
        m_dragStartSelection = getWeakSimpleRangeFromSelection(newSelection);

    m_frame->selection().setSelectionByMouseIfDifferent(newSelection, m_frame->selection().granularity(),
        FrameSelection::EndPointsAdjustmentMode::AdjustAtBidiBoundary);

    if (oldSelection != newSelection && ImageOverlay::isOverlayText(newSelection.start().protectedContainerNode().get()) && ImageOverlay::isOverlayText(newSelection.end().protectedContainerNode().get()))
        invalidateClick();
}

SimpleRange EventHandler::createSimpleRangeFromDragStartSelection() const
{
    const WeakSimpleRange& range = m_dragStartSelection.value();
    return { BoundaryPoint(*(range.start.container), range.start.offset), BoundaryPoint(*(range.end.container), range.end.offset) };
}

std::optional<WeakSimpleRange> EventHandler::getWeakSimpleRangeFromSelection(const VisibleSelection& selection) const
{
    if (auto range = selection.range())
        return range->makeWeakSimpleRange();
    return std::nullopt;
}
#endif // ENABLE(DRAG_SUPPORT)

void EventHandler::lostMouseCapture()
{
    protectedFrame()->selection().setCaretBlinkingSuspended(false);
}

bool EventHandler::handleMouseUp(const MouseEventWithHitTestResults& event)
{
    if (eventLoopHandleMouseUp(event))
        return true;
    
    // If this was the first click in the window, we don't even want to clear the selection.
    // This case occurs when the user clicks on a draggable element, since we have to process
    // the mouse down and drag events to see if we might start a drag.  For other first clicks
    // in a window, we just don't acceptFirstMouse, and the whole down-drag-up sequence gets
    // ignored upstream of this layer.
    return eventActivatedView(event.event());
}

bool EventHandler::handleMouseReleaseEvent(const MouseEventWithHitTestResults& event)
{
    if (autoscrollInProgress())
        stopAutoscrollTimer();

    Ref frame = m_frame.get();

    if (handleMouseUp(event))
        return true;

    // Used to prevent mouseMoveEvent from initiating a drag before
    // the mouse is pressed again.
    m_mousePressed = false;
    m_capturesDragging = false;
#if ENABLE(DRAG_SUPPORT)
    m_mouseDownMayStartDrag = false;
#endif
    m_mouseDownMayStartSelect = false;
    m_mouseDownMayStartAutoscroll = false;
    m_mouseDownWasInSubframe = false;
  
    bool handled = false;

    // Clear the selection if the mouse didn't move after the last mouse
    // press and it's not a context menu click.  We do this so when clicking
    // on the selection, the selection goes away.  However, if we are
    // editing, place the caret.
    if (m_mouseDownWasSingleClickInSelection && m_selectionInitiationState != ExtendedSelection
#if ENABLE(DRAG_SUPPORT)
            && m_dragStartPosition == event.event().position()
#endif
            && frame->selection().isRange()
            && event.event().button() != MouseButton::Right) {
        VisibleSelection newSelection;
        RefPtr node = event.targetNode();
        bool caretBrowsing = frame->settings().caretBrowsingEnabled();
        bool allowSelectionChanges = true;
        if (node && node->renderer() && (caretBrowsing || node->hasEditableStyle())) {
            VisiblePosition pos = node->renderer()->positionForPoint(event.localPoint(), HitTestSource::User, nullptr);
            newSelection = VisibleSelection(pos);
#if PLATFORM(IOS_FAMILY)
            // On iOS, selection changes are triggered using platform-specific text interaction gestures rather than
            // default behavior on click or mouseup. As such, the only time we should allow click events to change the
            // selection on iOS is when we focus a different editable element, in which case the text interaction
            // gestures will fail.
            allowSelectionChanges = frame->selection().selection().rootEditableElement() != newSelection.rootEditableElement();
#endif
        }

        if (allowSelectionChanges)
            setSelectionIfNeeded(frame->selection(), newSelection);

        handled = true;
    }

    // If the event was a middle click, attempt to copy global selection in after
    // the newly set caret position.
    //
    // There is some debate about when the global selection is pasted:
    //   xterm: pastes on up.
    //   GTK: pastes on down.
    //   Qt: pastes on up.
    //   Firefox: pastes on up.
    //   Chromium: pastes on up.
    //
    // However, WebKitGTK actually needs to paste on up to avoid clashing with
    // mouse gestures, https://gitlab.gnome.org/GNOME/epiphany/-/issues/1814. So
    // let's always paste on up, and forget about matching GTK.
    //
    // There is something of a webcompat angle to this well, as highlighted by
    // crbug.com/14608. Pages can clear text boxes 'onclick' and, if we paste on
    // down then the text is pasted just before the onclick handler runs and
    // clears the text box. So it's important this happens after the event
    // handlers have been fired.
    if (event.event().button() == MouseButton::Middle) {
        // Ignore handled, since we want to paste to where the caret was placed anyway.
        handled = handlePasteGlobalSelection() || handled;
    }

    return handled;
}

#if ENABLE(PAN_SCROLLING)

void EventHandler::didPanScrollStart()
{
    m_autoscrollController->didPanScrollStart();
}

void EventHandler::didPanScrollStop()
{
    m_autoscrollController->didPanScrollStop();
}

void EventHandler::startPanScrolling(RenderElement& renderer)
{
    CheckedPtr renderBox = dynamicDowncast<RenderBox>(renderer);
    if (!renderBox)
        return;
    m_autoscrollController->startPanScrolling(*renderBox, lastKnownMousePosition());
    invalidateClick();
}

#endif // ENABLE(PAN_SCROLLING)

RenderBox* EventHandler::autoscrollRenderer() const
{
    return m_autoscrollController->autoscrollRenderer();
}

void EventHandler::updateAutoscrollRenderer()
{
    m_autoscrollController->updateAutoscrollRenderer();
}

bool EventHandler::autoscrollInProgress() const
{
    return m_autoscrollController->autoscrollInProgress();
}

bool EventHandler::panScrollInProgress() const
{
    return m_autoscrollController->panScrollInProgress();
}

#if ENABLE(DRAG_SUPPORT)
OptionSet<DragSourceAction> EventHandler::updateDragSourceActionsAllowed() const
{
    RefPtr page = m_frame->page();
    if (!page)
        return { };

    RefPtr view = m_frame->view();
    if (!view)
        return { };

    return page->dragController().delegateDragSourceAction(view->contentsToRootView(m_mouseDownContentsPosition));
}
#endif // ENABLE(DRAG_SUPPORT)

HitTestResult EventHandler::hitTestResultAtPoint(const LayoutPoint& point, OptionSet<HitTestRequest::Type> hitType) const
{
    Ref frame = m_frame.get();

    // We always send hitTestResultAtPoint to the main frame if we have one,
    // otherwise we might hit areas that are obscured by higher frames.
    if (!frame->isMainFrame()) {
        if (RefPtr mainFrame = dynamicDowncast<LocalFrame>(frame->mainFrame())) {
            if (RefPtr frameView = frame->view(), mainView = mainFrame->view(); frameView && mainView) {
                IntPoint mainFramePoint = mainView->rootViewToContents(frameView->contentsToRootView(roundedIntPoint(point)));
                return mainFrame->eventHandler().hitTestResultAtPoint(mainFramePoint, hitType);
            }
        }
    }

    // We should always start hit testing a clean tree.
    if (RefPtr frameView = frame->view())
        frameView->updateLayoutAndStyleIfNeededRecursive();

    auto result = HitTestResult { point };
    RefPtr document = frame->document();
    if (!document)
        return result;

    HitTestRequest request(hitType);
    document->hitTest(request, result);
    if (!request.readOnly())
        frame->protectedDocument()->updateHoverActiveState(request, result.protectedTargetElement().get());

    RefPtr innerNode = result.innerNode();
    if (request.disallowsUserAgentShadowContent()
        || (request.disallowsUserAgentShadowContentExceptForImageOverlays() && innerNode && !ImageOverlay::isInsideOverlay(*innerNode)))
        result.setToNonUserAgentShadowAncestor();

    return result;
}

void EventHandler::stopAutoscrollTimer(bool rendererIsBeingDestroyed)
{
    m_autoscrollController->stopAutoscrollTimer(rendererIsBeingDestroyed);
}

bool EventHandler::scrollOverflow(ScrollDirection direction, ScrollGranularity granularity, Node* startingNode)
{
    RefPtr node = startingNode;

    if (!node)
        node = m_frame->document()->focusedElement();

    if (!node)
        node = m_mousePressNode.get();
    
    if (node) {
        auto r = node->renderer();
        if (r && !r->isRenderListBox() && r->enclosingBox().scroll(direction, granularity)) {
            setFrameWasScrolledByUser();
            return true;
        }
    }

    return false;
}

bool EventHandler::logicalScrollOverflow(ScrollLogicalDirection direction, ScrollGranularity granularity, Node* startingNode)
{
    RefPtr node = startingNode;

    if (!node)
        node = m_frame->document()->focusedElement();

    if (!node)
        node = m_mousePressNode.get();
    
    if (node) {
        auto r = node->renderer();
        if (r && !r->isRenderListBox() && r->enclosingBox().logicalScroll(direction, granularity)) {
            setFrameWasScrolledByUser();
            return true;
        }
    }

    return false;
}

bool EventHandler::scrollRecursively(ScrollDirection direction, ScrollGranularity granularity, Node* startingNode)
{
    // The layout needs to be up to date to determine if we can scroll. We may be
    // here because of an onLoad event, in which case the final layout hasn't been performed yet.
    Ref frame = m_frame.get();
    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();
    if (scrollOverflow(direction, granularity, startingNode))
        return true;

    RefPtr view = frame->view();
    if (view && view->scroll(direction, granularity))
        return true;
    RefPtr parent = frame->tree().parent();
    if (!parent)
        return false;
    RefPtr localParent = dynamicDowncast<LocalFrame>(parent.get());
    if (!localParent)
        return false;
    return localParent->eventHandler().scrollRecursively(direction, granularity, frame->protectedOwnerElement().get());
}

bool EventHandler::logicalScrollRecursively(ScrollLogicalDirection direction, ScrollGranularity granularity, Node* startingNode)
{
    Ref frame = m_frame.get();

    // The layout needs to be up to date to determine if we can scroll. We may be
    // here because of an onLoad event, in which case the final layout hasn't been performed yet.
    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();
    if (logicalScrollOverflow(direction, granularity, startingNode))
        return true;    

    RefPtr view = frame->view();
    
    bool scrolled = false;
#if PLATFORM(COCOA)
    // Mac also resets the scroll position in the inline direction.
    if (granularity == ScrollGranularity::Document && view && view->logicalScroll(ScrollInlineDirectionBackward, ScrollGranularity::Document))
        scrolled = true;
#endif
    if (view && view->logicalScroll(direction, granularity))
        scrolled = true;
    
    if (scrolled)
        return true;

    RefPtr parent = frame->tree().parent();
    if (!parent)
        return false;
    RefPtr localParent = dynamicDowncast<LocalFrame>(parent.get());
    if (!localParent)
        return false;

    return localParent->eventHandler().logicalScrollRecursively(direction, granularity, frame->protectedOwnerElement().get());
}

IntPoint EventHandler::lastKnownMousePosition() const
{
    return valueOrDefault(m_lastKnownMousePosition);
}

RefPtr<Frame> EventHandler::subframeForHitTestResult(const MouseEventWithHitTestResults& hitTestResult)
{
    if (!hitTestResult.isOverWidget())
        return nullptr;
    return subframeForTargetNode(hitTestResult.protectedTargetNode().get());
}

RefPtr<Frame> EventHandler::subframeForTargetNode(Node* node)
{
    if (!node)
        return nullptr;

    CheckedPtr renderWidget = dynamicDowncast<RenderWidget>(node->renderer());
    if (!renderWidget)
        return nullptr;

    auto* frameView = dynamicDowncast<FrameView>(renderWidget->widget());
    if (!frameView)
        return nullptr;

    return &frameView->frame();
}

static bool isSubmitImage(Node* node)
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(node);
    return input && input->isImageButton();
}

// Returns true if the node's editable block is not current focused for editing
static bool nodeIsNotBeingEdited(const Node& node, const LocalFrame& frame)
{
    return frame.selection().selection().rootEditableElement() != node.rootEditableElement();
}

bool EventHandler::useHandCursor(Node* node, bool isOverLink, bool shiftKey)
{
    if (!node)
        return false;

    bool editable = node->hasEditableStyle();
    bool editableLinkEnabled = false;

    // If the link is editable, then we need to check the settings to see whether or not the link should be followed
    if (editable) {
        switch (m_frame->settings().editableLinkBehavior()) {
        case EditableLinkBehavior::Default:
        case EditableLinkBehavior::AlwaysLive:
            editableLinkEnabled = true;
            break;

        case EditableLinkBehavior::NeverLive:
            editableLinkEnabled = false;
            break;

        case EditableLinkBehavior::LiveWhenNotFocused:
            editableLinkEnabled = nodeIsNotBeingEdited(*node, protectedFrame()) || shiftKey;
            break;

        case EditableLinkBehavior::OnlyLiveWithShiftKey:
            editableLinkEnabled = shiftKey;
            break;
        }
    }

    return ((isOverLink || isSubmitImage(node)) && (!editable || editableLinkEnabled));
}

void EventHandler::updateCursorIfNeeded()
{
    if (std::exchange(m_hasScheduledCursorUpdate, false))
        updateCursor();
}

void EventHandler::updateCursor()
{
    if (!m_lastKnownMousePosition)
        return;

    if (Page* page = m_frame->page()) {
        if (!page->chrome().client().supportsSettingCursor())
            return;
    }

    RefPtr view = m_frame->view();
    if (!view)
        return;

    RefPtr document = m_frame->document();
    if (!document)
        return;

    if (!view->shouldSetCursor())
        return;

    bool shiftKey;
    bool ctrlKey;
    bool altKey;
    bool metaKey;
    PlatformKeyboardEvent::getCurrentModifierState(shiftKey, ctrlKey, altKey, metaKey);

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::AllowFrameScrollbars };
    HitTestResult result(view->windowToContents(*m_lastKnownMousePosition));
    document->hitTest(hitType, result);

    updateCursor(*view, result, shiftKey);
}

void EventHandler::updateCursor(LocalFrameView& view, const HitTestResult& result, bool shiftKey)
{
    if (auto optionalCursor = selectCursor(result, shiftKey)) {
        m_currentMouseCursor = WTFMove(optionalCursor.value());
        view.setCursor(m_currentMouseCursor);
    }
}

std::optional<Cursor> EventHandler::selectCursor(const HitTestResult& result, bool shiftKey)
{
    if (m_resizeLayer && m_resizeLayer->inResizeMode())
        return std::nullopt;

    if (!m_frame->page())
        return std::nullopt;

#if ENABLE(PAN_SCROLLING)
    auto* localFrame = dynamicDowncast<LocalFrame>(m_frame->mainFrame());
    if (!localFrame)
        return std::nullopt;

    if (localFrame->eventHandler().panScrollInProgress())
        return std::nullopt;
#endif

    Ref frame = m_frame.get();

    // Use always pointer cursor for scrollbars.
    if (result.scrollbar()) {
#if ENABLE(CURSOR_VISIBILITY)
        cancelAutoHideCursorTimer();
#endif
        return pointerCursor();
    }

    RefPtr node = result.targetNode();
    if (!node)
        return std::nullopt;

    auto renderer = node->renderer();
    auto* style = renderer ? &renderer->style() : nullptr;
    bool horizontalText = !style || style->writingMode().isHorizontal();
    const Cursor& iBeam = horizontalText ? iBeamCursor() : verticalTextCursor();

    // area element has display: none set by default, should use node to get style instead of renderer.
    if (is<HTMLAreaElement>(node))
        style = node->computedStyle();

#if ENABLE(CURSOR_VISIBILITY)
    if (style && style->cursorVisibility() == CursorVisibility::AutoHide)
        startAutoHideCursorTimer();
    else
        cancelAutoHideCursorTimer();
#endif

    if (renderer) {
        Cursor overrideCursor;
        switch (renderer->getCursor(roundedIntPoint(result.localPoint()), overrideCursor)) {
        case SetCursorBasedOnStyle:
            break;
        case SetCursor:
            return overrideCursor;
        case DoNotSetCursor:
            return std::nullopt;
        }
    }

    auto styleCursor = style ? style->cursor() : Style::Cursor { CSS::Keyword::Auto { } };
    if (styleCursor.images) {
        for (auto& styleCursorImage : *styleCursor.images) {
            Ref styleImage = styleCursorImage.image;
            CachedImage* cachedImage = styleImage->cachedImage();
            if (!cachedImage)
                continue;
            float scale = styleImage->imageScaleFactor();
            // Get hotspot and convert from logical pixels to physical pixels.
            auto hotSpot = styleCursorImage.hotSpot;
            FloatSize size = cachedImage->imageForRenderer(renderer)->size();
            if (cachedImage->errorOccurred())
                continue;
            // Limit the size of cursors (in UI pixels) so that they cannot be
            // used to cover UI elements in chrome.
            size.scale(1 / scale);
            if (size.width() > maximumCursorSize || size.height() > maximumCursorSize)
                continue;

            RefPtr localMainFrame = dynamicDowncast<LocalFrame>(frame->mainFrame());
            if (!localMainFrame)
                continue;
            IntRect visibleContentRect = localMainFrame->view()->visibleContentRect();
            IntRect cursorRect = { roundedIntPoint(result.pointInMainFrame()), expandedIntSize(size) };
            cursorRect.moveBy(-hotSpot);

            if (!visibleContentRect.contains(cursorRect))
                continue;

            Image* image = cachedImage->imageForRenderer(renderer);
#if ENABLE(MOUSE_CURSOR_SCALE)
            // Ensure no overflow possible in calculations above.
            if (scale < minimumCursorScale)
                continue;
            return Cursor(image, hotSpot, scale);
#else
            ASSERT(scale == 1);
            return Cursor(image, hotSpot);
#endif // ENABLE(MOUSE_CURSOR_SCALE)
        }
    }

    switch (styleCursor.predefined) {
    case CursorType::Auto: {
        if (ImageOverlay::isOverlayText(node.get())) {
            if (renderer && renderer->style().usedUserSelect() != UserSelect::None)
                return iBeam;
        }

        bool editable = node->hasEditableStyle();

        if (useHandCursor(node.get(), result.isOverLink(), shiftKey))
            return handCursor();

        bool inResizer = false;
        auto resizerRenderer = renderer;

        if (is<RenderText>(resizerRenderer))
            resizerRenderer = resizerRenderer->parent();

        if (resizerRenderer && resizerRenderer->hasLayer()) {
            auto& layerRenderer = downcast<RenderLayerModelObject>(*resizerRenderer);
            inResizer = layerRenderer.layer()->isPointInResizeControl(roundedIntPoint(result.localPoint()));
            if (inResizer)
                return layerRenderer.shouldPlaceVerticalScrollbarOnLeft() ? southWestResizeCursor() : southEastResizeCursor();
        }

        // During selection, use an I-beam regardless of the content beneath the cursor.
        // If a drag may be starting or we're capturing mouse events for a particular node, don't treat this as a selection.
        if (m_mousePressed
            && mouseDownMayStartSelect()
#if ENABLE(DRAG_SUPPORT)
            && !m_mouseDownMayStartDrag
#endif
            && frame->selection().isCaretOrRange()
            && !m_capturingMouseEventsElement && renderer && renderer->style().usedUserSelect() != UserSelect::None)
                return iBeam;

        if ((editable || (renderer && renderer->isRenderText() && node->canStartSelection() && renderer->style().usedUserSelect() != UserSelect::None)) && !inResizer && !result.scrollbar())
            return iBeam;
        return pointerCursor();
    }
    case CursorType::Default:
        return pointerCursor();
    case CursorType::None:
        return noneCursor();
    case CursorType::ContextMenu:
        return contextMenuCursor();
    case CursorType::Help:
        return helpCursor();
    case CursorType::Pointer:
        return handCursor();
    case CursorType::Progress:
        return progressCursor();
    case CursorType::Wait:
        return waitCursor();
    case CursorType::Cell:
        return cellCursor();
    case CursorType::Crosshair:
        return crossCursor();
    case CursorType::Text:
        return iBeamCursor();
    case CursorType::VerticalText:
        return verticalTextCursor();
    case CursorType::Alias:
        return aliasCursor();
    case CursorType::Copy:
        return copyCursor();
    case CursorType::Move:
        return moveCursor();
    case CursorType::NoDrop:
        return noDropCursor();
    case CursorType::NotAllowed:
        return notAllowedCursor();
    case CursorType::Grab:
        return grabCursor();
    case CursorType::Grabbing:
        return grabbingCursor();
    case CursorType::EResize:
        return eastResizeCursor();
    case CursorType::NResize:
        return northResizeCursor();
    case CursorType::NEResize:
        return northEastResizeCursor();
    case CursorType::NWResize:
        return northWestResizeCursor();
    case CursorType::SResize:
        return southResizeCursor();
    case CursorType::SEResize:
        return southEastResizeCursor();
    case CursorType::SWResize:
        return southWestResizeCursor();
    case CursorType::WResize:
        return westResizeCursor();
    case CursorType::EWResize:
        return eastWestResizeCursor();
    case CursorType::NSResize:
        return northSouthResizeCursor();
    case CursorType::NESWResize:
        return northEastSouthWestResizeCursor();
    case CursorType::NWSEResize:
        return northWestSouthEastResizeCursor();
    case CursorType::ColumnResize:
        return columnResizeCursor();
    case CursorType::RowResize:
        return rowResizeCursor();
    case CursorType::AllScroll:
        return moveCursor();
    case CursorType::ZoomIn:
        return zoomInCursor();
    case CursorType::ZoomOut:
        return zoomOutCursor();
    }
    return pointerCursor();
}

#if ENABLE(CURSOR_VISIBILITY)
void EventHandler::startAutoHideCursorTimer()
{
    RefPtr page = m_frame->page();
    if (!page)
        return;

    m_autoHideCursorTimer.startOneShot(page->settings().timeWithoutMouseMovementBeforeHidingControls());

#if !ENABLE(IOS_TOUCH_EVENTS)
    // The fake mouse move event screws up the auto-hide feature (by resetting the auto-hide timer)
    // so cancel any pending fake mouse moves.
    if (m_fakeMouseMoveEventTimer.isActive())
        m_fakeMouseMoveEventTimer.stop();
#endif
}

void EventHandler::cancelAutoHideCursorTimer()
{
    if (m_autoHideCursorTimer.isActive())
        m_autoHideCursorTimer.stop();
}

void EventHandler::autoHideCursorTimerFired()
{
    RefPtr view = m_frame->view();
    if (!view || !view->isActive())
        return;

    if (RefPtr page = m_frame->page())
        page->chrome().setCursorHiddenUntilMouseMoves(true);
}
#endif

static LayoutPoint documentPointForWindowPoint(LocalFrame& frame, const IntPoint& windowPoint)
{
    RefPtr view = frame.view();
    if (!view) {
        // FIXME: Is it really OK to use the wrong coordinates here when view is 0?
        // Historically the code would just crash; this is clearly no worse than that.
        return windowPoint;
    }

    auto result = view->windowToContents(FloatPoint { windowPoint });
    return LayoutPoint { result };
}

std::optional<RemoteUserInputEventData> EventHandler::userInputEventDataForRemoteFrame(const RemoteFrame* remoteFrame, const IntPoint& pointInFrame)
{
    if (!remoteFrame)
        return std::nullopt;

    RefPtr frameView = m_frame->view();
    if (!frameView)
        return std::nullopt;

    RefPtr remoteFrameView = remoteFrame->view();
    if (!remoteFrameView)
        return std::nullopt;

    return RemoteUserInputEventData {
        remoteFrame->frameID(),
        remoteFrameView->rootViewToContents(frameView->contentsToRootView(pointInFrame))
    };
}

std::optional<RemoteFrameGeometryTransformer> EventHandler::geometryTransformerForRemoteFrame(RemoteFrame* remoteFrame)
{
    if (!remoteFrame)
        return std::nullopt;

    RefPtr frameView = m_frame->view();
    if (!frameView)
        return std::nullopt;

    RefPtr remoteFrameView = remoteFrame->view();
    if (!remoteFrameView)
        return std::nullopt;

    return RemoteFrameGeometryTransformer(remoteFrameView.releaseNonNull(), frameView.releaseNonNull(), remoteFrame->frameID());
}

static Scrollbar* scrollbarForMouseEvent(const MouseEventWithHitTestResults& mouseEvent, LocalFrameView* view)
{
    if (view) {
        if (auto* scrollbar = view->scrollbarAtPoint(mouseEvent.event().position()))
            return scrollbar;
    }
    return mouseEvent.scrollbar();

}

HandleUserInputEventResult EventHandler::handleMousePressEvent(const PlatformMouseEvent& platformMouseEvent)
{
    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };

    if (InspectorInstrumentation::handleMousePress(frame)) {
        invalidateClick();
        return true;
    }

    RefPtr page = frame->page();
    if (!page)
        return false;

#if ENABLE(POINTER_LOCK)
    if (auto& pointerLockController = page->pointerLockController(); pointerLockController.isLocked()) {
        pointerLockController.dispatchLockedMouseEvent(platformMouseEvent, eventNames().mousedownEvent);
        return true;
    }
#endif

    if (page->pageOverlayController().handleMouseEvent(platformMouseEvent))
        return true;

#if ENABLE(TOUCH_EVENTS)
    bool defaultPrevented = dispatchSyntheticTouchEventIfEnabled(platformMouseEvent);
    if (defaultPrevented)
        return true;
#endif

    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, frame->protectedDocument().get(), userGestureTypeForPlatformEvent(platformMouseEvent), UserGestureIndicator::ProcessInteractionStyle::Immediate, platformMouseEvent.authorizationToken());

    // FIXME (bug 68185): this call should be made at another abstraction layer
    frame->loader().resetMultipleFormSubmissionProtection();

#if !ENABLE(IOS_TOUCH_EVENTS)
    cancelFakeMouseMoveEvent();
#endif
    if (m_eventHandlerWillResetCapturingMouseEventsElement)
        resetCapturingMouseEventsElement();

    m_mousePressed = true;
    m_capturesDragging = true;
    setLastKnownMousePosition(platformMouseEvent.position(), platformMouseEvent.globalPosition());
    m_mouseDownTimestamp = platformMouseEvent.timestamp();
#if ENABLE(DRAG_SUPPORT)
    m_mouseDownMayStartDrag = false;
#endif
    m_mouseDownMayStartSelect = false;
    m_mouseDownMayStartAutoscroll = false;
    if (RefPtr view = frame->view())
        m_mouseDownContentsPosition = view->windowToContents(platformMouseEvent.position());
    else {
        invalidateClick();
        return false;
    }
    m_mouseDownWasInSubframe = false;

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent };
    // Save the document point we generate in case the window coordinate is invalidated by what happens
    // when we dispatch the event.
    LayoutPoint documentPoint = documentPointForWindowPoint(frame, platformMouseEvent.position());
    MouseEventWithHitTestResults mouseEvent = frame->protectedDocument()->prepareMouseEvent(hitType, documentPoint, platformMouseEvent);

    if (!mouseEvent.targetNode()) {
        invalidateClick();
        return false;
    }

    m_mousePressNode = mouseEvent.targetNode();
    frame->protectedDocument()->setFocusNavigationStartingNode(mouseEvent.protectedTargetNode().get());

    Scrollbar* scrollbar = scrollbarForMouseEvent(mouseEvent, frame->view());
    updateLastScrollbarUnderMouse(scrollbar, SetOrClearLastScrollbar::Set);
    bool passedToScrollbar = scrollbar && passMousePressEventToScrollbar(mouseEvent, scrollbar);

    if (!passedToScrollbar) {
        auto subframe = subframeForHitTestResult(mouseEvent);
        if (auto remoteMouseEventData = userInputEventDataForRemoteFrame(dynamicDowncast<RemoteFrame>(subframe).get(), mouseEvent.hitTestResult().roundedPointInInnerNodeFrame()))
            return *remoteMouseEventData;

        if (RefPtr localSubframe = dynamicDowncast<LocalFrame>(subframe)) {
            auto result = passMousePressEventToSubframe(mouseEvent, *localSubframe);
            if (auto remoteMouseEventData = result.remoteUserInputEventData())
                return *remoteMouseEventData;
            if (result.wasHandled()) {
                // Start capturing future events for this frame. We only do this if we didn't clear
                // the m_mousePressed flag, which may happen if an AppKit widget entered a modal event loop.
                m_capturesDragging = localSubframe->eventHandler().capturesDragging();
                if (m_mousePressed) {
                    m_capturingMouseEventsElement = localSubframe->ownerElement();
                    m_eventHandlerWillResetCapturingMouseEventsElement = true;
                    if (!m_capturingMouseEventsElement)
                        m_isCapturingRootElementForMouseEvents = true;
                }
                invalidateClick();
                return true;
            }
        }
    }

#if ENABLE(PAN_SCROLLING)
    // We store whether pan scrolling is in progress before calling stopAutoscrollTimer()
    // because it will set m_autoscrollType to NoAutoscroll on return.
    auto* localFrame = dynamicDowncast<LocalFrame>(frame->mainFrame());
    if (!localFrame)
        return false;

    bool isPanScrollInProgress = Ref(*localFrame)->eventHandler().panScrollInProgress();
    stopAutoscrollTimer();
    if (isPanScrollInProgress) {
        // We invalidate the click when exiting pan scrolling so that we don't inadvertently navigate
        // away from the current page (e.g. the click was on a hyperlink). See <rdar://problem/6095023>.
        invalidateClick();
        return true;
    }
#endif

    m_clickCount = platformMouseEvent.clickCount();
    m_clickNode = mouseEvent.targetNode();

    if (!m_clickNode) {
        invalidateClick();
        return false;
    }

    RenderLayer* layer = m_clickNode->renderer() ? m_clickNode->renderer()->enclosingLayer() : nullptr;
    auto localPoint = roundedIntPoint(mouseEvent.hitTestResult().localPoint());
    if (layer && layer->isPointInResizeControl(localPoint)) {
        layer->setInResizeMode(true);
        m_resizeLayer = *layer;
        m_offsetFromResizeCorner = layer->offsetFromResizeCorner(localPoint);
        dispatchMouseEvent(eventNames().mousedownEvent, mouseEvent.protectedTargetNode().get(), m_clickCount, platformMouseEvent, FireMouseOverOut::Yes);
        return true;
    }

    frame->selection().setCaretBlinkingSuspended(true);

    bool swallowEvent = !dispatchMouseEvent(eventNames().mousedownEvent, mouseEvent.protectedTargetNode().get(), m_clickCount, platformMouseEvent, FireMouseOverOut::Yes);
    if (!swallowEvent || mouseEvent.scrollbar())
        m_capturesDragging = true;
    else
        m_capturesDragging = m_capturesDragging ? CapturesDragging::InabilityReason::Unknown : m_capturesDragging.inabilityReason();

    // If the hit testing originally determined the event was in a scrollbar, refetch the MouseEventWithHitTestResults
    // in case the scrollbar widget was destroyed when the mouse event was handled.
    if (mouseEvent.scrollbar()) {
        const bool wasLastScrollBar = mouseEvent.scrollbar() == m_lastScrollbarUnderMouse;
        mouseEvent = frame->protectedDocument()->prepareMouseEvent(HitTestRequest(), documentPoint, platformMouseEvent);
        if (wasLastScrollBar && mouseEvent.scrollbar() != m_lastScrollbarUnderMouse)
            m_lastScrollbarUnderMouse = nullptr;
    }

    if (!swallowEvent) {
        if (shouldRefetchEventTarget(mouseEvent))
            mouseEvent = frame->protectedDocument()->prepareMouseEvent(HitTestRequest(), documentPoint, platformMouseEvent);
    }

    if (!swallowEvent) {
        if (passedToScrollbar)
            swallowEvent = true;
        else
            swallowEvent = handleMousePressEvent(mouseEvent);
    }
    return swallowEvent;
}

// This method only exists for platforms that don't know how to deliver 
bool EventHandler::handleMouseDoubleClickEvent(const PlatformMouseEvent& platformMouseEvent)
{
    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };

    frame->selection().setCaretBlinkingSuspended(false);

    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, frame->protectedDocument().get(), userGestureTypeForPlatformEvent(platformMouseEvent));

#if ENABLE(POINTER_LOCK)
    if (frame->page()->pointerLockController().isLocked()) {
        frame->page()->pointerLockController().dispatchLockedMouseEvent(platformMouseEvent, eventNames().mouseupEvent);
        return true;
    }
#endif

    // We get this instead of a second mouse-up 
    m_mousePressed = false;
    setLastKnownMousePosition(platformMouseEvent.position(), platformMouseEvent.globalPosition());

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Release, HitTestRequest::Type::DisallowUserAgentShadowContent };
    MouseEventWithHitTestResults mouseEvent = prepareMouseEvent(hitType, platformMouseEvent);
    auto subframe = dynamicDowncast<LocalFrame>(subframeForHitTestResult(mouseEvent));

    if (m_eventHandlerWillResetCapturingMouseEventsElement)
        resetCapturingMouseEventsElement();

    if (subframe && passMousePressEventToSubframe(mouseEvent, *subframe).wasHandled())
        return true;

    m_clickCount = platformMouseEvent.clickCount();
    bool swallowMouseUpEvent = !dispatchMouseEvent(eventNames().mouseupEvent, mouseEvent.protectedTargetNode().get(), m_clickCount, platformMouseEvent, FireMouseOverOut::No);
    bool swallowClickEvent = swallowAnyClickEvent(platformMouseEvent, mouseEvent, IgnoreAncestorNodesForClickEvent::Yes);

    if (m_lastScrollbarUnderMouse)
        swallowMouseUpEvent = m_lastScrollbarUnderMouse->mouseUp(platformMouseEvent);

    bool swallowMouseReleaseEvent = !swallowMouseUpEvent && handleMouseReleaseEvent(mouseEvent);

    invalidateClick();

    return swallowMouseUpEvent || swallowClickEvent || swallowMouseReleaseEvent;
}

ScrollableArea* EventHandler::enclosingScrollableArea(Node* node) const
{
    for (auto ancestor = node; ancestor; ancestor = ancestor->parentOrShadowHostNode()) {
        if (is<HTMLIFrameElement>(*ancestor))
            return nullptr;

        if (is<HTMLHtmlElement>(*ancestor) || is<HTMLDocument>(*ancestor))
            break;

        auto renderer = ancestor->renderer();
        if (!renderer)
            continue;

        if (auto* renderListBox = dynamicDowncast<RenderListBox>(*renderer)) {
            auto* scrollableArea = static_cast<ScrollableArea*>(renderListBox);
            if (scrollableArea->isScrollableOrRubberbandable())
                return scrollableArea;
        }

        if (RefPtr plugin = dynamicDowncast<RenderEmbeddedObject>(renderer)) {
            if (auto* scrollableArea = plugin->scrollableArea()) {
                Ref frame = m_frame.get();
                RefPtr page = frame->page();
                if (!page || page->chrome().client().usePluginRendererScrollableArea(frame))
                    return scrollableArea;
            }
        }

        auto* layer = renderer->enclosingLayer();
        if (!layer)
            return nullptr;

        if (auto* scrollableLayer = layer->enclosingScrollableLayer(IncludeSelfOrNot::IncludeSelf, CrossFrameBoundaries::No)) {
            if (!scrollableLayer->isRenderViewLayer())
                return scrollableLayer->scrollableArea();
        }
    }

    return m_frame->view();
}

HandleUserInputEventResult EventHandler::mouseMoved(const PlatformMouseEvent& event)
{
    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };
    MaximumDurationTracker maxDurationTracker(&m_maxMouseMovedDuration);

    if (frame->page() && frame->protectedPage()->pageOverlayController().handleMouseEvent(event))
        return true;

    HitTestResult hitTestResult;
    auto result = handleMouseMoveEvent(event, &hitTestResult);

    RefPtr page = m_frame->page();
    if (!page)
        return result;

    hitTestResult.setToNonUserAgentShadowAncestor();
    page->chrome().mouseDidMoveOverElement(hitTestResult, event.modifiers());

#if ENABLE(IMAGE_ANALYSIS)
    if (event.syntheticClickType() == SyntheticClickType::NoTap && m_textRecognitionHoverTimer.isActive())
        m_textRecognitionHoverTimer.restart();
#endif

    return result;
}

bool EventHandler::passMouseMovedEventToScrollbars(const PlatformMouseEvent& event)
{
    HitTestResult hitTestResult;
    return handleMouseMoveEvent(event, &hitTestResult, true).wasHandled();
}

OptionSet<HitTestRequest::Type> EventHandler::getHitTypeForMouseMoveEvent(const PlatformMouseEvent& platformMouseEvent, bool onlyUpdateScrollbars)
{
    OptionSet hitType { HitTestRequest::Type::Move, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowFrameScrollbars };
    if (m_mousePressed)
        hitType.add(HitTestRequest::Type::Active);
    else if (onlyUpdateScrollbars) {
        // Mouse events should be treated as "read-only" if we're updating only scrollbars. This
        // means that :hover and :active freeze in the state they were in, rather than updating
        // for nodes the mouse moves while the window is not key (which will be the case if
        // onlyUpdateScrollbars is true).
        hitType.add(HitTestRequest::Type::ReadOnly);
    }

#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
    // Treat any mouse move events as readonly if the user is currently touching the screen.
    if (m_touchPressed) {
        hitType.add(HitTestRequest::Type::Active);
        hitType.add(HitTestRequest::Type::ReadOnly);
    }
#endif

#if ENABLE(PENCIL_HOVER)
    if (platformMouseEvent.pointerType() == WebCore::penPointerEventType())
        hitType.add(WebCore::HitTestRequest::Type::PenEvent);
#else
    UNUSED_PARAM(platformMouseEvent);
#endif
    return hitType;
}

HitTestResult EventHandler::getHitTestResultForMouseEvent(const PlatformMouseEvent& platformMouseEvent)
{
    HitTestRequest request(getHitTypeForMouseMoveEvent(platformMouseEvent));
    return prepareMouseEvent(request, platformMouseEvent).hitTestResult();
}

HandleUserInputEventResult EventHandler::handleMouseMoveEvent(const PlatformMouseEvent& platformMouseEvent, HitTestResult* hitTestResult, bool onlyUpdateScrollbars)
{
#if ENABLE(TOUCH_EVENTS)
    bool defaultPrevented = dispatchSyntheticTouchEventIfEnabled(platformMouseEvent);
    if (defaultPrevented)
        return true;
#endif

    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };

#if ENABLE(POINTER_LOCK)
    if (frame->page()->pointerLockController().isLocked()) {
        frame->protectedPage()->pointerLockController().dispatchLockedMouseEvent(platformMouseEvent, eventNames().mousemoveEvent);
        return true;
    }
#endif

    setLastKnownMousePosition(platformMouseEvent.position(), platformMouseEvent.globalPosition());

    if (m_hoverTimer.isActive())
        m_hoverTimer.stop();

    m_hasScheduledCursorUpdate = false;

#if !ENABLE(IOS_TOUCH_EVENTS)
    cancelFakeMouseMoveEvent();
#endif

    if (m_svgPan) {
        downcast<SVGDocument>(*frame->protectedDocument()).updatePan(frame->protectedView()->windowToContents(valueOrDefault(m_lastKnownMousePosition)));
        return true;
    }

    if (m_frameSetBeingResized)
        return !dispatchMouseEvent(eventNames().mousemoveEvent, m_frameSetBeingResized.get(), 0, platformMouseEvent, FireMouseOverOut::No);

    // On iOS, our scrollbars are managed by UIKit.
#if !PLATFORM(IOS_FAMILY)
    // Send events right to a scrollbar if the mouse is pressed.
    if (m_lastScrollbarUnderMouse && m_mousePressed)
        return m_lastScrollbarUnderMouse->mouseMoved(platformMouseEvent);
#endif

    HitTestRequest request(getHitTypeForMouseMoveEvent(platformMouseEvent, onlyUpdateScrollbars));
    MouseEventWithHitTestResults mouseEvent = prepareMouseEvent(request, platformMouseEvent);
    if (hitTestResult)
        *hitTestResult = mouseEvent.hitTestResult();

    if (m_resizeLayer && m_resizeLayer->inResizeMode()) {
        m_resizeLayer->resize(platformMouseEvent, m_offsetFromResizeCorner);

        if (m_resizeLayer->renderer().shouldPlaceVerticalScrollbarOnLeft()) {
            auto absolutePoint = frame->protectedView()->windowToContents(platformMouseEvent.position());
            auto localPoint = roundedIntPoint(m_resizeLayer->absoluteToContents(absolutePoint));
            m_offsetFromResizeCorner.setWidth(m_resizeLayer->offsetFromResizeCorner(localPoint).width());
        }
    } else {
        RefPtr scrollbar = mouseEvent.scrollbar();
        updateLastScrollbarUnderMouse(scrollbar.get(), m_mousePressed ? SetOrClearLastScrollbar::Clear : SetOrClearLastScrollbar::Set);

        // On iOS, our scrollbars are managed by UIKit.
#if !PLATFORM(IOS_FAMILY)
        if (!m_mousePressed && scrollbar)
            scrollbar->mouseMoved(platformMouseEvent); // Handle hover effects on platforms that support visual feedback on scrollbar hovering.
#endif
        if (onlyUpdateScrollbars) {
            if (shouldSendMouseEventsToInactiveWindows())
                updateMouseEventTargetNode(eventNames().mousemoveEvent, mouseEvent.protectedTargetNode().get(), platformMouseEvent, FireMouseOverOut::Yes);

            return true;
        }
    }

    bool swallowEvent = false;
    auto subframe = isCapturingMouseEventsElement() ? subframeForTargetNode(m_capturingMouseEventsElement.get()) : subframeForHitTestResult(mouseEvent);
    if (auto remoteMouseEventData = userInputEventDataForRemoteFrame(dynamicDowncast<RemoteFrame>(subframe).get(), mouseEvent.hitTestResult().roundedPointInInnerNodeFrame()))
        return *remoteMouseEventData;

    RefPtr localSubframe = dynamicDowncast<LocalFrame>(subframe.get());
 
    // We want mouseouts to happen first, from the inside out.  First send a move event to the last subframe so that it will fire mouseouts.
    if (RefPtr lastMouseMoveEventSubframe = m_lastMouseMoveEventSubframe; lastMouseMoveEventSubframe && lastMouseMoveEventSubframe->tree().isDescendantOf(frame.ptr()) && lastMouseMoveEventSubframe != localSubframe)
        passMouseMoveEventToSubframe(mouseEvent, *lastMouseMoveEventSubframe);

    if (localSubframe) {
        // Update over/out state before passing the event to the subframe.
        updateMouseEventTargetNode(eventNames().mousemoveEvent, mouseEvent.protectedTargetNode().get(), platformMouseEvent, FireMouseOverOut::Yes);

        // Event dispatch in updateMouseEventTargetNode may have caused the subframe of the target
        // node to be detached from its FrameView, in which case the event should not be passed.
        if (localSubframe->view()) {
            auto result = passMouseMoveEventToSubframe(mouseEvent, *localSubframe, hitTestResult);
            if (auto remoteMouseEventData = result.remoteUserInputEventData())
                return *remoteMouseEventData;
            swallowEvent |= result.wasHandled();
        }
    }

    if (!localSubframe || mouseEvent.scrollbar()) {
        if (RefPtr view = frame->view())
            updateCursor(*view, mouseEvent.hitTestResult(), platformMouseEvent.shiftKey());
    }

    m_lastMouseMoveEventSubframe = localSubframe;

    if (swallowEvent)
        return true;

    swallowEvent = !dispatchMouseEvent(eventNames().mousemoveEvent, mouseEvent.protectedTargetNode().get(), 0, platformMouseEvent, FireMouseOverOut::Yes);

#if ENABLE(DRAG_SUPPORT)
    if (!swallowEvent || m_capturesDragging.inabilityReason() == CapturesDragging::InabilityReason::MouseMoveIsCancelled)
        swallowEvent = handleMouseDraggedEvent(mouseEvent);
#endif

    return swallowEvent;
}

bool EventHandler::shouldSendMouseEventsToInactiveWindows() const
{
#if PLATFORM(GTK)
    return true;
#endif
    return false;
}

void EventHandler::invalidateClick()
{
    m_clickCount = 0;
    m_clickNode = nullptr;
}

static RefPtr<Node> targetNodeForClickEvent(Node* mousePressNode, Node* mouseReleaseNode)
{
    if (!mousePressNode || !mouseReleaseNode)
        return nullptr;

    if (mousePressNode == mouseReleaseNode)
        return mouseReleaseNode;

    // If mousePressNode and mouseReleaseNode differ, we should fire the event at their common ancestor if there is one.
    if (&mousePressNode->document() == &mouseReleaseNode->document()) {
        if (auto commonAncestor = commonInclusiveAncestor<ComposedTree>(*mousePressNode, *mouseReleaseNode))
            return commonAncestor;
    }

    auto mouseReleaseShadowHost = mouseReleaseNode->shadowHost();
    if (mouseReleaseShadowHost && mouseReleaseShadowHost == mousePressNode->shadowHost()) {
        // We want to dispatch the click to the shadow tree host element to give listeners the illusion that the
        // shadom tree is a single element. For example, we want to give the illusion that <input type="range">
        // is a single element even though it is a composition of multiple shadom tree elements.
        return mouseReleaseShadowHost;
    }

    return nullptr;
}

bool EventHandler::swallowAnyClickEvent(const PlatformMouseEvent& platformMouseEvent, const MouseEventWithHitTestResults& mouseEvent, IgnoreAncestorNodesForClickEvent ignoreAncestorNodesForClickEvent)
{
    if (!m_clickCount)
        return false;

    auto nodeToClick = [&] -> RefPtr<Node> {
        if (ignoreAncestorNodesForClickEvent == IgnoreAncestorNodesForClickEvent::Yes) {
            if (mouseEvent.targetNode() != m_clickNode)
                return nullptr;

            return m_clickNode;
        }

        return targetNodeForClickEvent(RefPtr { m_clickNode }.get(), mouseEvent.protectedTargetNode().get());
    }();

    if (!nodeToClick)
        return false;

    bool isPrimaryPointerButton = platformMouseEvent.button() == MouseButton::Left;
    if (!isPrimaryPointerButton && !protectedFrame()->settings().auxclickEventEnabled())
        return false;

    // The auxclick event should only be fired for the non-primary pointer buttons.
    // In the case of right button, the auxclick event is dispatched after any contextmenu event.
    //
    // The click event should only be fired for the primary pointer button.

    auto& eventName = isPrimaryPointerButton ? eventNames().clickEvent : eventNames().auxclickEvent;
    bool swallowed = !dispatchMouseEvent(eventName, nodeToClick.get(), m_clickCount, platformMouseEvent, FireMouseOverOut::Yes);

    if (RefPtr page = m_frame->page())
        page->chrome().client().didDispatchClickEvent(platformMouseEvent, *nodeToClick);

    return swallowed;
}

HandleUserInputEventResult EventHandler::handleMouseReleaseEvent(const PlatformMouseEvent& platformMouseEvent)
{
    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };

    frame->selection().setCaretBlinkingSuspended(false);

    RefPtr page = frame->page();
    if (!page)
        return false;

#if ENABLE(POINTER_LOCK)
    if (auto& pointerLockController = page->pointerLockController(); pointerLockController.isLocked()) {
        pointerLockController.dispatchLockedMouseEvent(platformMouseEvent, eventNames().mouseupEvent);
        return true;
    }
#endif

    if (page->pageOverlayController().handleMouseEvent(platformMouseEvent))
        return true;

#if ENABLE(TOUCH_EVENTS)
    bool defaultPrevented = dispatchSyntheticTouchEventIfEnabled(platformMouseEvent);
    if (defaultPrevented)
        return true;
#endif

    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, frame->protectedDocument().get(), userGestureTypeForPlatformEvent(platformMouseEvent), UserGestureIndicator::ProcessInteractionStyle::Immediate, platformMouseEvent.authorizationToken());

#if ENABLE(PAN_SCROLLING)
    m_autoscrollController->handleMouseReleaseEvent(platformMouseEvent);
#endif

    m_mousePressed = false;
    setLastKnownMousePosition(platformMouseEvent.position(), platformMouseEvent.globalPosition());

    if (m_svgPan) {
        m_svgPan = false;
        downcast<SVGDocument>(*frame->protectedDocument()).updatePan(frame->protectedView()->windowToContents(valueOrDefault(m_lastKnownMousePosition)));
        return true;
    }

    if (m_frameSetBeingResized)
        return !dispatchMouseEvent(eventNames().mouseupEvent, m_frameSetBeingResized.get(), m_clickCount, platformMouseEvent, FireMouseOverOut::No);

    // If an immediate action began or was completed using this series of mouse events, then we should send mouseup to
    // the DOM and return now so that we don't perform our own default behaviors.
    if (immediateActionBeganOrWasCompleted(m_immediateActionStage)) {
        // We reset the immediate action stage after event dispatch, and not before, so that DOM event handling can query for the stage if needed.
        auto resetImmediateActionStageAfterMouseEventDispatch = makeScopeExit([&] {
            m_immediateActionStage = ImmediateActionStage::None;
        });
        return !dispatchMouseEvent(eventNames().mouseupEvent, m_lastElementUnderMouse.get(), m_clickCount, platformMouseEvent, FireMouseOverOut::No);
    }
    m_immediateActionStage = ImmediateActionStage::None;

    if (m_lastScrollbarUnderMouse) {
        invalidateClick();
        m_lastScrollbarUnderMouse->mouseUp(platformMouseEvent);
        return !dispatchMouseEvent(eventNames().mouseupEvent, m_lastElementUnderMouse.get(), m_clickCount, platformMouseEvent, FireMouseOverOut::No);
    }

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Release, HitTestRequest::Type::DisallowUserAgentShadowContent };
    MouseEventWithHitTestResults mouseEvent = prepareMouseEvent(hitType, platformMouseEvent);
    auto subframe = isCapturingMouseEventsElement() ? subframeForTargetNode(m_capturingMouseEventsElement.get()) : subframeForHitTestResult(mouseEvent);
    if (m_eventHandlerWillResetCapturingMouseEventsElement)
        resetCapturingMouseEventsElement();

    if (auto remoteMouseEventData = userInputEventDataForRemoteFrame(dynamicDowncast<RemoteFrame>(subframe).get(), mouseEvent.hitTestResult().roundedPointInInnerNodeFrame()))
        return *remoteMouseEventData;

    if (RefPtr localSubframe = dynamicDowncast<LocalFrame>(subframe)) {
        auto result = passMouseReleaseEventToSubframe(mouseEvent, *localSubframe);
        if (auto remoteMouseEventData = result.remoteUserInputEventData())
            return *remoteMouseEventData;
        if (result.wasHandled())
            return true;
    }

    bool swallowMouseUpEvent = !dispatchMouseEvent(eventNames().mouseupEvent, mouseEvent.protectedTargetNode().get(), m_clickCount, platformMouseEvent, FireMouseOverOut::No);

    bool swallowClickEvent = swallowAnyClickEvent(platformMouseEvent, mouseEvent, IgnoreAncestorNodesForClickEvent::No);

    if (m_resizeLayer) {
        m_resizeLayer->setInResizeMode(false);
        m_resizeLayer = nullptr;
    }

    bool swallowMouseReleaseEvent = false;
    if (!swallowMouseUpEvent)
        swallowMouseReleaseEvent = handleMouseReleaseEvent(mouseEvent);

    invalidateClick();

    return swallowMouseUpEvent || swallowClickEvent || swallowMouseReleaseEvent;
}

bool EventHandler::handleMouseForceEvent(const PlatformMouseEvent& event)
{
    Ref frame = m_frame.get();
    RefPtr protectedView(frame->view());

#if ENABLE(POINTER_LOCK)
    if (frame->page()->pointerLockController().isLocked()) {
        m_frame->page()->pointerLockController().dispatchLockedMouseEvent(event, eventNames().webkitmouseforcechangedEvent);
        if (event.type() == PlatformEvent::Type::MouseForceDown)
            m_frame->page()->pointerLockController().dispatchLockedMouseEvent(event, eventNames().webkitmouseforcedownEvent);
        if (event.type() == PlatformEvent::Type::MouseForceUp)
            m_frame->page()->pointerLockController().dispatchLockedMouseEvent(event, eventNames().webkitmouseforceupEvent);
        return true;
    }
#endif

    setLastKnownMousePosition(event.position(), event.globalPosition());

    OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::DisallowUserAgentShadowContent };

    if (event.force())
        hitType.add(HitTestRequest::Type::Active);

    auto mouseEvent = prepareMouseEvent(hitType, event);

    bool swallowedEvent = !dispatchMouseEvent(eventNames().webkitmouseforcechangedEvent, mouseEvent.protectedTargetNode().get(), 0, event, FireMouseOverOut::No);
    if (event.type() == PlatformEvent::Type::MouseForceDown)
        swallowedEvent |= !dispatchMouseEvent(eventNames().webkitmouseforcedownEvent, mouseEvent.protectedTargetNode().get(), 0, event, FireMouseOverOut::No);
    if (event.type() == PlatformEvent::Type::MouseForceUp)
        swallowedEvent |= !dispatchMouseEvent(eventNames().webkitmouseforceupEvent, mouseEvent.protectedTargetNode().get(), 0, event, FireMouseOverOut::No);

    return swallowedEvent;
}

bool EventHandler::handlePasteGlobalSelection()
{
    if (!m_frame->page())
        return false;
    RefPtr focusFrame = m_frame->page()->focusController().focusedOrMainFrame();
    // Do not paste here if the focus was moved somewhere else.
    if (m_frame.ptr() == focusFrame.get() && m_frame->editor().client()->supportsGlobalSelection())
        return protectedFrame()->editor().command("PasteGlobalSelection"_s).execute();

    return false;
}

#if ENABLE(DRAG_SUPPORT)

bool EventHandler::dispatchDragEvent(const AtomString& eventType, Element& dragTarget, const PlatformMouseEvent& event, DataTransfer& dataTransfer)
{
    Ref frame = m_frame.get();
    RefPtr view = frame->view();

    // FIXME: We might want to dispatch a dragleave even if the view is gone.
    if (!view)
        return false;

    auto dragEvent = DragEvent::create(eventType, Event::CanBubble::Yes, Event::IsCancelable::Yes, Event::IsComposed::Yes,
        event.timestamp().approximateMonotonicTime(), &frame->windowProxy(), 0,
        event.globalPosition(), event.position(), event.movementDelta().x(), event.movementDelta().y(),
        event.modifiers(), MouseButton::Left, 0, nullptr, event.force(), SyntheticClickType::NoTap, &dataTransfer);

    dragTarget.dispatchEvent(dragEvent);

    if (CheckedPtr cache = frame->document()->existingAXObjectCache()) {
        auto& eventNames = WebCore::eventNames();
        if (eventType == eventNames.dragstartEvent)
            cache->postNotification(&dragTarget, AXNotification::DraggingStarted);
        else if (eventType == eventNames.dragendEvent)
            cache->postNotification(&dragTarget, AXNotification::DraggingEnded);
        else if (eventType == eventNames.dragenterEvent)
            cache->postNotification(&dragTarget, AXNotification::DraggingEnteredDropZone);
        else if (eventType == eventNames.dragleaveEvent)
            cache->postNotification(&dragTarget, AXNotification::DraggingExitedDropZone);
        else if (eventType == eventNames.dropEvent)
            cache->postNotification(&dragTarget, AXNotification::DraggingDropped);
    }

    return dragEvent->defaultPrevented();
}

Element* EventHandler::draggingElement() const
{
    return dragState().source.get();
}

void EventHandler::setDragStateSource(Element* element) const
{
    RefPtr document = m_frame->document();
    if (CheckedPtr cache = document ? document->existingAXObjectCache() : nullptr)
        cache->onDragElementChanged(dragState().source.get(), element);

    dragState().source = element;
}

bool EventHandler::canDropCurrentlyDraggedImageAsFile() const
{
    auto sourceOrigin = dragState().restrictedOriginForImageData;
    return !sourceOrigin || m_frame->document()->protectedSecurityOrigin()->canReceiveDragData(*sourceOrigin);
}

static std::pair<bool, RefPtr<LocalFrame>> contentFrameForNode(Node* target)
{
    RefPtr frameElement = dynamicDowncast<HTMLFrameElementBase>(target);
    if (!frameElement)
        return { false, nullptr };

    return { true, dynamicDowncast<LocalFrame>(frameElement->contentFrame()) };
}

static std::optional<DragOperation> convertDropZoneOperationToDragOperation(const String& dragOperation)
{
    if (dragOperation == "copy"_s)
        return DragOperation::Copy;
    if (dragOperation == "move"_s)
        return DragOperation::Move;
    if (dragOperation == "link"_s)
        return DragOperation::Link;
    return std::nullopt;
}

static String convertDragOperationToDropZoneOperation(std::optional<DragOperation> operation)
{
    if (operation) {
        switch (*operation) {
        case DragOperation::Move:
            return "move"_s;
        case DragOperation::Link:
            return "link"_s;
        default:
            break;
        }
    }
    return "copy"_s;
}

static bool hasDropZoneType(Document& document, DataTransfer& dataTransfer, const String& keyword)
{
    if (keyword.startsWith("file:"_s))
        return dataTransfer.hasFileOfType(keyword.substring(5));

    if (keyword.startsWith("string:"_s))
        return dataTransfer.hasStringOfType(document, keyword.substring(7));

    return false;
}

static bool findDropZone(Node& target, DataTransfer& dataTransfer)
{
    RefPtr element = dynamicDowncast<Element>(target);
    if (!element)
        element = target.parentElement();
    for (; element; element = element->parentElement()) {
        SpaceSplitString keywords(element->attributeWithoutSynchronization(webkitdropzoneAttr), SpaceSplitString::ShouldFoldCase::Yes);
        bool matched = false;
        std::optional<DragOperation> dragOperation;
        for (auto& keyword : keywords) {
            if (auto operationFromKeyword = convertDropZoneOperationToDragOperation(keyword)) {
                if (!dragOperation)
                    dragOperation = operationFromKeyword;
            } else
                matched = matched || hasDropZoneType(target.protectedDocument(), dataTransfer, keyword.string());
            if (matched && dragOperation)
                break;
        }
        if (matched) {
            dataTransfer.setDropEffect(convertDragOperationToDropZoneOperation(dragOperation));
            return true;
        }
    }
    return false;
}

EventHandler::DragTargetResponse EventHandler::dispatchDragEnterOrDragOverEvent(const AtomString& eventType, Element& target, const PlatformMouseEvent& event, std::unique_ptr<Pasteboard>&& pasteboard, OptionSet<DragOperation> sourceOperationMask, bool draggingFiles)
{
    auto dataTransfer = DataTransfer::createForUpdatingDropTarget(target.protectedDocument(), WTFMove(pasteboard), sourceOperationMask, draggingFiles);
    bool accept = dispatchDragEvent(eventType, target, event, dataTransfer.get());
    if (!accept)
        accept = findDropZone(target, dataTransfer);
    dataTransfer->makeInvalidForSecurity();
    if (accept && !dataTransfer->dropEffectIsUninitialized())
        return { true, dataTransfer->destinationOperationMask() };
    return { accept, std::nullopt };
}

EventHandler::DragTargetResponse EventHandler::updateDragAndDrop(const PlatformMouseEvent& event, const std::function<std::unique_ptr<Pasteboard>()>& makePasteboard, OptionSet<DragOperation> sourceOperationMask, bool draggingFiles)
{
    Ref frame = m_frame.get();
    if (!frame->view())
        return { };

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::DisallowUserAgentShadowContent };
    MouseEventWithHitTestResults mouseEvent = prepareMouseEvent(hitType, event);

    RefPtr<Element> newTarget;
    if (RefPtr targetNode = mouseEvent.targetNode()) {
        // Drag events should never go to non-element nodes (following IE, and proper mouseover/out dispatch)
        if (!is<Element>(*targetNode))
            newTarget = targetNode->parentOrShadowHostElement();
        else
            newTarget = static_pointer_cast<Element>(WTFMove(targetNode));
    }

    m_autoscrollController->updateDragAndDrop(newTarget.get(), event.position(), event.timestamp());

    DragTargetResponse response;
    if (m_dragTarget != newTarget) {
        // FIXME: this ordering was explicitly chosen to match WinIE. However,
        // it is sometimes incorrect when dragging within subframes, as seen with
        // LayoutTests/fast/events/drag-in-frames.html.
        //
        // Moreover, this ordering conforms to section 7.9.4 of the HTML 5 spec. <http://dev.w3.org/html5/spec/Overview.html#drag-and-drop-processing-model>.
        if (auto [isFrameOwner, targetFrame] = contentFrameForNode(newTarget.get()); isFrameOwner) {
            if (targetFrame)
                response = targetFrame->eventHandler().updateDragAndDrop(event, makePasteboard, sourceOperationMask, draggingFiles);
        } else if (newTarget) {
            // As per section 7.9.4 of the HTML 5 spec., we must always fire a drag event before firing a dragenter, dragleave, or dragover event.
            dispatchEventToDragSourceElement(eventNames().dragEvent, event);
            response = dispatchDragEnterOrDragOverEvent(eventNames().dragenterEvent, *newTarget, event, makePasteboard(), sourceOperationMask, draggingFiles);
        }

        if (auto [isFrameOwner, targetFrame] = contentFrameForNode(m_dragTarget.copyRef().get()); isFrameOwner) {
            // FIXME: Recursing again here doesn't make sense if the newTarget and m_dragTarget were in the same frame.
            if (targetFrame)
                response = targetFrame->eventHandler().updateDragAndDrop(event, makePasteboard, sourceOperationMask, draggingFiles);
        } else if (RefPtr dragTarget = m_dragTarget) {
            auto dataTransfer = DataTransfer::createForUpdatingDropTarget(dragTarget->protectedDocument(), makePasteboard(), sourceOperationMask, draggingFiles);
            dispatchDragEvent(eventNames().dragleaveEvent, *dragTarget, event, dataTransfer.get());
            dataTransfer->makeInvalidForSecurity();
        }

        if (newTarget) {
            // We do not explicitly call dispatchDragEvent here because it could ultimately result in the appearance that
            // two dragover events fired. So, we mark that we should only fire a dragover event on the next call to this function.
            m_shouldOnlyFireDragOverEvent = true;
        }
    } else {
        if (auto [isFrameOwner, targetFrame] = contentFrameForNode(newTarget.get()); isFrameOwner) {
            if (targetFrame)
                response = targetFrame->eventHandler().updateDragAndDrop(event, makePasteboard, sourceOperationMask, draggingFiles);
        } else if (newTarget) {
            // Note, when dealing with sub-frames, we may need to fire only a dragover event as a drag event may have been fired earlier.
            if (!m_shouldOnlyFireDragOverEvent)
                dispatchEventToDragSourceElement(eventNames().dragEvent, event);
            response = dispatchDragEnterOrDragOverEvent(eventNames().dragoverEvent, *newTarget, event, makePasteboard(), sourceOperationMask, draggingFiles);
            m_shouldOnlyFireDragOverEvent = false;
        }
    }
    m_dragTarget = WTFMove(newTarget);
    return response;
}

void EventHandler::cancelDragAndDrop(const PlatformMouseEvent& event, std::unique_ptr<Pasteboard>&& pasteboard, OptionSet<DragOperation> sourceOperationMask, bool draggingFiles)
{
    Ref frame = m_frame.get();

    if (auto [isFrameOwner, targetFrame] = contentFrameForNode(m_dragTarget.copyRef().get()); isFrameOwner) {
        if (targetFrame)
            targetFrame->eventHandler().cancelDragAndDrop(event, WTFMove(pasteboard), sourceOperationMask, draggingFiles);
    } else if (RefPtr dragTarget = m_dragTarget) {
        dispatchEventToDragSourceElement(eventNames().dragEvent, event);

        auto dataTransfer = DataTransfer::createForUpdatingDropTarget(dragTarget->protectedDocument(), WTFMove(pasteboard), sourceOperationMask, draggingFiles);
        dispatchDragEvent(eventNames().dragleaveEvent, *dragTarget, event, dataTransfer.get());
        dataTransfer->makeInvalidForSecurity();
    }
    clearDragState();
}

bool EventHandler::performDragAndDrop(const PlatformMouseEvent& event, std::unique_ptr<Pasteboard>&& pasteboard, OptionSet<DragOperation> sourceOperationMask, bool draggingFiles)
{
    Ref frame = m_frame.get();

    bool preventedDefault = false;
    if (auto [isFrameOwner, targetFrame] = contentFrameForNode(m_dragTarget.copyRef().get()); isFrameOwner) {
        if (targetFrame)
            preventedDefault = targetFrame->eventHandler().performDragAndDrop(event, WTFMove(pasteboard), sourceOperationMask, draggingFiles);
    } else if (RefPtr dragTarget = m_dragTarget) {
        Ref dataTransfer = DataTransfer::createForDrop(dragTarget->protectedDocument(), WTFMove(pasteboard), sourceOperationMask, draggingFiles);
        preventedDefault = dispatchDragEvent(eventNames().dropEvent, *dragTarget, event, dataTransfer);
        dataTransfer->makeInvalidForSecurity();
    }
    clearDragState();
    return preventedDefault;
}

void EventHandler::clearDragState()
{
    stopAutoscrollTimer();
    m_dragStartSelection = std::nullopt;
    m_dragTarget = nullptr;
    resetCapturingMouseEventsElement();
    m_shouldOnlyFireDragOverEvent = false;
#if PLATFORM(COCOA)
    m_sendingEventToSubview = false;
#endif
}

#endif // ENABLE(DRAG_SUPPORT)

void EventHandler::setCapturingMouseEventsElement(RefPtr<Element>&& element)
{
    m_capturingMouseEventsElement = WTFMove(element);
    m_isCapturingRootElementForMouseEvents = false;
    m_eventHandlerWillResetCapturingMouseEventsElement = false;
}

void EventHandler::pointerCaptureElementDidChange(Element* element)
{
    if (m_capturingMouseEventsElement == element)
        return;

    setCapturingMouseEventsElement(element);

    // Now that we have a new capture element, we need to dispatch boundary mouse events.
    updateMouseEventTargetNode(eventNames().gotpointercaptureEvent, element, m_lastPlatformMouseEvent, FireMouseOverOut::Yes);
}

MouseEventWithHitTestResults EventHandler::prepareMouseEvent(const HitTestRequest& request, const PlatformMouseEvent& mouseEvent)
{
    m_lastPlatformMouseEvent = mouseEvent;
    Ref frame = m_frame.get();
    ASSERT(frame->document());
    return frame->protectedDocument()->prepareMouseEvent(request, documentPointForWindowPoint(frame, mouseEvent.position()), mouseEvent);
}

static bool hierarchyHasCapturingEventListeners(Element* element, const AtomString& pointerEventName, const AtomString& compatibilityMouseEventName)
{
    for (RefPtr<ContainerNode> curr = element; curr; curr = curr->parentInComposedTree()) {
        if (curr->hasCapturingEventListeners(pointerEventName) || curr->hasCapturingEventListeners(compatibilityMouseEventName))
            return true;
    }
    return false;
}

#if ENABLE(IMAGE_ANALYSIS)

RefPtr<Element> EventHandler::textRecognitionCandidateElement() const
{
    RefPtr candidateElement = m_elementUnderMouse;
    if (candidateElement) {
        if (auto shadowHost = candidateElement->shadowHost())
            candidateElement = shadowHost;
    }

    if (!candidateElement)
        return nullptr;

    if (candidateElement->hasEditableStyle())
        return nullptr;

    auto renderer = candidateElement->renderer();
    if (!is<RenderImage>(renderer))
        return nullptr;

    if (candidateElement->document().settings().textRecognitionInVideosEnabled()) {
        if (auto video = dynamicDowncast<HTMLVideoElement>(*candidateElement); video && video->paused())
            return candidateElement;
    }

#if ENABLE(VIDEO)
    if (is<HTMLVideoElement>(*candidateElement))
        return nullptr;
#endif // ENABLE(VIDEO)

    return candidateElement;
}

#endif // ENABLE(IMAGE_ANALYSIS)

void EventHandler::updateMouseEventTargetNode(const AtomString& eventType, Node* targetNode, const PlatformMouseEvent& platformMouseEvent, FireMouseOverOut fireMouseOverOut)
{
    Ref frame = m_frame.get();
    RefPtr<Element> targetElement;
    
    // If we're capturing, we always go right to that element.
    if (m_capturingMouseEventsElement)
        targetElement = m_capturingMouseEventsElement.get();
    else {
        // If the target node is a non-element, dispatch on the parent. <rdar://problem/4196646>
        while (targetNode) {
            if (auto* asElement = dynamicDowncast<Element>(*targetNode)) {
                targetElement = asElement;
                break;
            }
            targetNode = targetNode->parentInComposedTree();
        }
    }

    m_elementUnderMouse = targetElement;

#if ENABLE(IMAGE_ANALYSIS)
    if (!textRecognitionCandidateElement())
        m_textRecognitionHoverTimer.stop();
    else if (!platformMouseEvent.movementDelta().isZero())
        m_textRecognitionHoverTimer.restart();
#endif // ENABLE(IMAGE_ANALYSIS)

    if (RefPtr page = frame->page())
        page->imageOverlayController().elementUnderMouseDidChange(frame, m_elementUnderMouse.get());

    ASSERT_IMPLIES(m_elementUnderMouse, &m_elementUnderMouse->document() == frame->document());
    ASSERT_IMPLIES(m_lastElementUnderMouse, &m_lastElementUnderMouse->document() == frame->document());

    // Fire mouseout/mouseover if the mouse has shifted to a different node.
    if (fireMouseOverOut == FireMouseOverOut::Yes) {
        notifyScrollableAreasOfMouseEvents(eventType, m_lastElementUnderMouse.get(), m_elementUnderMouse.get());

        if (m_lastElementUnderMouse && &m_lastElementUnderMouse->document() != frame->document()) {
            m_lastElementUnderMouse = nullptr;
            m_lastScrollbarUnderMouse = nullptr;
        }

        if (m_lastElementUnderMouse != m_elementUnderMouse) {
            // mouseenter and mouseleave events are only dispatched if there is a capturing eventhandler on an ancestor
            // or a normal eventhandler on the element itself (they don't bubble).
            // This optimization is necessary since these events can cause O(n^2) capturing event-handler checks.
            auto& eventNames = WebCore::eventNames();
            bool hasCapturingMouseEnterListener = hierarchyHasCapturingEventListeners(m_elementUnderMouse.get(), eventNames.pointerenterEvent, eventNames.mouseenterEvent);
            bool hasCapturingMouseLeaveListener = hierarchyHasCapturingEventListeners(m_lastElementUnderMouse.get(), eventNames.pointerleaveEvent, eventNames.mouseleaveEvent);

            Vector<Ref<Element>, 32> leftElementsChain;
            for (Element* element = m_lastElementUnderMouse.get(); element; element = element->parentElementInComposedTree())
                leftElementsChain.append(*element);
            Vector<Ref<Element>, 32> enteredElementsChain;
            for (Element* element = m_elementUnderMouse.get(); element; element = element->parentElementInComposedTree())
                enteredElementsChain.append(*element);

            if (!leftElementsChain.isEmpty() && !enteredElementsChain.isEmpty() && leftElementsChain.last().ptr() == enteredElementsChain.last().ptr()) {
                size_t minHeight = std::min(leftElementsChain.size(), enteredElementsChain.size());
                size_t i;
                for (i = 0; i < minHeight; ++i) {
                    if (leftElementsChain[leftElementsChain.size() - i - 1].ptr() != enteredElementsChain[enteredElementsChain.size() - i - 1].ptr())
                        break;
                }
                leftElementsChain.shrink(leftElementsChain.size() - i);
                enteredElementsChain.shrink(enteredElementsChain.size() - i);
            }

            if (auto lastElementUnderMouse = m_lastElementUnderMouse)
                lastElementUnderMouse->dispatchMouseEvent(platformMouseEvent, eventNames.mouseoutEvent, 0, m_elementUnderMouse.get());

            for (auto& chain : leftElementsChain) {
                if (hasCapturingMouseLeaveListener || chain->hasEventListeners(eventNames.pointerleaveEvent) || chain->hasEventListeners(eventNames.mouseleaveEvent))
                    chain->dispatchMouseEvent(platformMouseEvent, eventNames.mouseleaveEvent, 0, m_elementUnderMouse.get());
            }

            if (auto elementUnderMouse = m_elementUnderMouse)
                elementUnderMouse->dispatchMouseEvent(platformMouseEvent, eventNames.mouseoverEvent, 0, m_lastElementUnderMouse.get());

            for (auto& chain : makeReversedRange(enteredElementsChain)) {
                if (hasCapturingMouseEnterListener || chain->hasEventListeners(eventNames.pointerenterEvent) || chain->hasEventListeners(eventNames.mouseenterEvent))
                    chain->dispatchMouseEvent(platformMouseEvent, eventNames.mouseenterEvent, 0, m_lastElementUnderMouse.get());
            }
        }

        // Event handling may have moved the element to a different document.
        if (m_elementUnderMouse && &m_elementUnderMouse->document() != frame->document()) {
#if ENABLE(IMAGE_ANALYSIS)
            m_textRecognitionHoverTimer.stop();
#endif
            clearElementUnderMouse();
        }

        m_lastElementUnderMouse = m_elementUnderMouse;
    }
}

void EventHandler::clearElementUnderMouse()
{
    if (!m_elementUnderMouse)
        return;

    m_elementUnderMouse = nullptr;

    RefPtr page = m_frame->page();
    if (!page)
        return;

    auto* imageOverlayController = page->imageOverlayControllerIfExists();
    if (!imageOverlayController)
        return;

    imageOverlayController->elementUnderMouseDidChange(protectedFrame(), nullptr);
}

void EventHandler::notifyScrollableAreasOfMouseEvents(const AtomString& eventType, Element* lastElementUnderMouse, Element* elementUnderMouse)
{
    Ref frame = m_frame.get();
    RefPtr frameView = frame->view();
    if (!frameView)
        return;

    auto scrollableAreaForLastNode = enclosingScrollableArea(lastElementUnderMouse);
    auto scrollableAreaForNodeUnderMouse = enclosingScrollableArea(elementUnderMouse);

    if (!!lastElementUnderMouse != !!elementUnderMouse) {
        if (elementUnderMouse) {
            if (scrollableAreaForNodeUnderMouse != frameView)
                frameView->mouseEnteredContentArea();
            if (scrollableAreaForNodeUnderMouse)
                scrollableAreaForNodeUnderMouse->mouseEnteredContentArea();
        } else {
            if (scrollableAreaForLastNode)
                scrollableAreaForLastNode->mouseExitedContentArea();

            if (scrollableAreaForLastNode != frameView)
                frameView->mouseExitedContentArea();
        }
        return;
    }

    if (!scrollableAreaForLastNode && !scrollableAreaForNodeUnderMouse)
        return;

    // FIXME: This does doesn't handle nested ScrollableAreas well. It really needs to know
    // the hierarchical relationship between scrollableAreaForLastNode and scrollableAreaForNodeUnderMouse.
    bool movedBetweenScrollableaAreas = scrollableAreaForLastNode && scrollableAreaForNodeUnderMouse && (scrollableAreaForLastNode != scrollableAreaForNodeUnderMouse);
    if (eventType == eventNames().mousemoveEvent) {
        frameView->mouseMovedInContentArea();

        if (!movedBetweenScrollableaAreas && scrollableAreaForNodeUnderMouse && scrollableAreaForNodeUnderMouse != frameView)
            scrollableAreaForNodeUnderMouse->mouseMovedInContentArea();
    }

    if (!movedBetweenScrollableaAreas)
        return;

    if (scrollableAreaForLastNode && scrollableAreaForLastNode != frameView)
        scrollableAreaForLastNode->mouseExitedContentArea();

    if (scrollableAreaForNodeUnderMouse && scrollableAreaForNodeUnderMouse != frameView)
        scrollableAreaForNodeUnderMouse->mouseEnteredContentArea();
}

bool EventHandler::dispatchMouseEvent(const AtomString& eventType, Node* targetNode, int clickCount, const PlatformMouseEvent& platformMouseEvent, FireMouseOverOut fireMouseOverOut)
{
    Ref frame = m_frame.get();

    updateMouseEventTargetNode(eventType, targetNode, platformMouseEvent, fireMouseOverOut);

    bool isMouseDownEvent = eventType == eventNames().mousedownEvent;

    if (auto elementUnderMouse = m_elementUnderMouse) {
        auto [eventIsDispatched, eventIsDefaultPrevented] = elementUnderMouse->dispatchMouseEvent(platformMouseEvent, eventType, clickCount, nullptr, IsSyntheticClick::No);
        m_capturesDragging = CapturesDragging::InabilityReason::Unknown;
        if (eventIsDefaultPrevented == Element::EventIsDefaultPrevented::Yes) {
            if (isMouseDownEvent)
                m_capturesDragging = CapturesDragging::InabilityReason::MousePressIsCancelled;
            else if (eventType == eventNames().mousemoveEvent)
                m_capturesDragging = CapturesDragging::InabilityReason::MouseMoveIsCancelled;
        }
        if (eventIsDispatched == Element::EventIsDispatched::No)
            return false;
    }

    if (!isMouseDownEvent)
        return true;

    m_mouseDownDelegatedFocus = false;

    // If clicking on a frame scrollbar, do not make any change to which element is focused.
    RefPtr view = frame->view();
    if (view && view->scrollbarAtPoint(platformMouseEvent.position()))
        return true;

    // The layout needs to be up to date to determine if an element is focusable.
    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();

    // Remove focus from the currently focused element when a link or button is clicked.
    // This is expected by some sites that rely on change event handlers running
    // from form fields before the button click is processed, behavior that was inherited
    // from the user interface of Windows, where pushing a button moves focus to the button.

    // Walk up the DOM tree to search for an element to focus.
    RefPtr<Element> element;
    for (element = m_elementUnderMouse.get(); element; element = element->parentElementInComposedTree()) {
        if (RefPtr shadowRoot = element->shadowRoot()) {
            if (shadowRoot->delegatesFocus()) {
                element = Element::findFocusDelegateForTarget(*shadowRoot, FocusTrigger::Click);
                m_mouseDownDelegatedFocus = true;
                break;
            }
        }
        if (element->isMouseFocusable())
            break;
    }

    // To fix <rdar://problem/4895428> Can't drag selected ToDo, we don't focus an
    // element on mouse down if it's selected and inside a focused element. It will be
    // focused if the user does a mouseup over it, however, because the mouseup
    // will set a selection inside it, which will also set the focused element.
    if (element && frame->selection().isRange()) {
        if (auto range = frame->selection().selection().toNormalizedRange()) {
            if (contains<ComposedTree>(*range, *element) && element->isDescendantOf(frame->document()->protectedFocusedElement().get()))
                return true;
        }
    }

    // Only change the focus when clicking scrollbars if it can be transferred to a mouse focusable node.
    if (!element && isInsideScrollbar(platformMouseEvent.position()))
        return false;

#if (!PLATFORM(GTK) && !PLATFORM(WPE))
    // This is a workaround related to :focus-visible (see webkit.org/b/236782).
    // Form control elements are not mouse focusable on some platforms (see HTMLFormControlElement::isMouseFocusable())
    // which makes us behave differently than other browsers when a button is clicked,
    // because the button is not actually focused so we don't set the latest FocusTrigger.
    if (m_elementUnderMouse && !m_elementUnderMouse->isMouseFocusable() && is<HTMLFormControlElement>(m_elementUnderMouse))
        frame->protectedDocument()->setLatestFocusTrigger(FocusTrigger::Click);
#endif

    // If focus shift is blocked, we eat the event.
    RefPtr page = frame->page();
    if (page && !page->focusController().setFocusedElement(element.get(), protectedFrame(), { { }, { }, { }, FocusTrigger::Click, { } }))
        return false;

    if (element && m_mouseDownDelegatedFocus)
        element->findTargetAndUpdateFocusAppearance(SelectionRestorationMode::SelectAll);

    return true;
}

bool EventHandler::isInsideScrollbar(const IntPoint& windowPoint) const
{
    if (RefPtr document = m_frame->document()) {
        HitTestResult result { windowPoint };
        document->hitTest(OptionSet<HitTestRequest::Type> { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::DisallowUserAgentShadowContent }, result);
        return result.scrollbar();
    }

    return false;
}

#if !PLATFORM(MAC)

void EventHandler::determineWheelEventTarget(const PlatformWheelEvent&, RefPtr<Element>&, WeakPtr<ScrollableArea>&, bool&)
{
}

bool EventHandler::processWheelEventForScrolling(const PlatformWheelEvent& event, const WeakPtr<ScrollableArea>&, OptionSet<EventHandling> eventHandling)
{
    Ref frame = m_frame.get();

    // We do another check on the frame view because the event handler can run JS which results in the frame getting destroyed.
    RefPtr view = frame->view();
    
    bool didHandleEvent = view ? handleWheelEventInScrollableArea(event, *view, eventHandling) : false;
    m_isHandlingWheelEvent = false;
    return didHandleEvent;
}

void EventHandler::wheelEventWasProcessedByMainThread(const PlatformWheelEvent& wheelEvent, OptionSet<EventHandling> eventHandling)
{
    updateWheelGestureState(wheelEvent, eventHandling);

#if ENABLE(ASYNC_SCROLLING)

    if (!m_frame->page())
        return;

    RefPtr view = m_frame->view();
    if (RefPtr scrollingCoordinator = m_frame->page()->scrollingCoordinator()) {
        if (scrollingCoordinator->coordinatesScrollingForFrameView(*view))
            scrollingCoordinator->wheelEventWasProcessedByMainThread(wheelEvent, m_wheelScrollGestureState);
    }
#endif
}

bool EventHandler::platformCompletePlatformWidgetWheelEvent(const PlatformWheelEvent&, const Widget&, const WeakPtr<ScrollableArea>&)
{
    return true;
}

void EventHandler::processWheelEventForScrollSnap(const PlatformWheelEvent&, const WeakPtr<ScrollableArea>&)
{
}
    
#if !PLATFORM(IOS_FAMILY)
    
IntPoint EventHandler::targetPositionInWindowForSelectionAutoscroll() const
{
    return valueOrDefault(m_lastKnownMousePosition);
}
    
#endif // !PLATFORM(IOS_FAMILY)
    
#endif // !PLATFORM(MAC)
    
#if !PLATFORM(IOS_FAMILY)
    
bool EventHandler::shouldUpdateAutoscroll()
{
    return mousePressed();
}
    
#endif // !PLATFORM(IOS_FAMILY)

Widget* EventHandler::widgetForEventTarget(Element* eventTarget)
{
    if (!eventTarget)
        return nullptr;

    auto* renderWidget = dynamicDowncast<RenderWidget>(eventTarget->renderer());
    if (!renderWidget)
        return nullptr;

    return renderWidget->widget();
}

static RefPtr<Widget> widgetForElement(const Element& element)
{
    auto* renderWidget = dynamicDowncast<RenderWidget>(element.renderer());
    if (!renderWidget || !renderWidget->widget())
        return { };

    return renderWidget->widget();
}

bool EventHandler::completeWidgetWheelEvent(const PlatformWheelEvent& event, const SingleThreadWeakPtr<Widget>& widget, const WeakPtr<ScrollableArea>& scrollableArea)
{
    m_isHandlingWheelEvent = false;
    
    // We do another check on the widget because the event handler can run JS which results in the frame getting destroyed.
    if (!widget)
        return false;
    
    if (scrollableArea)
        scrollableArea->setScrollShouldClearLatchedState(false);

    processWheelEventForScrollSnap(event, scrollableArea);

    if (!widget->platformWidget())
        return true;

    return platformCompletePlatformWidgetWheelEvent(event, *widget.get(), scrollableArea);
}

std::pair<HandleUserInputEventResult, OptionSet<EventHandling>> EventHandler::handleWheelEvent(const PlatformWheelEvent& wheelEvent, OptionSet<WheelEventProcessingSteps> processingSteps)
{
    Ref frame = m_frame.get();
#if ENABLE(KINETIC_SCROLLING)
    if (wheelEvent.isGestureStart())
        m_wheelScrollGestureState = std::nullopt;
#endif

    OptionSet<EventHandling> handling;
    auto handleWheelEventResult = handleWheelEventInternal(wheelEvent, processingSteps, handling);
    // wheelEventWasProcessedByMainThread() may have already been called via performDefaultWheelEventHandling(), but this ensures that it's always called if that code path doesn't run.
    wheelEventWasProcessedByMainThread(wheelEvent, handling);
    return { handleWheelEventResult, handling };
}

HandleUserInputEventResult EventHandler::handleWheelEventInternal(const PlatformWheelEvent& event, OptionSet<WheelEventProcessingSteps> processingSteps, OptionSet<EventHandling>& handling)
{
    Ref frame = m_frame.get();
    RefPtr document = frame->document();
    if (!document)
        return false;

    RefPtr view = frame->view();
    if (!view)
        return false;

    if (!frame->page())
        return false;

#if ENABLE(POINTER_LOCK)
    if (frame->page()->pointerLockController().isLocked()) {
        frame->protectedPage()->pointerLockController().dispatchLockedWheelEvent(event);
        return true;
    }
#endif

#if PLATFORM(COCOA) || PLATFORM(WIN)
    LOG_WITH_STREAM(Scrolling, stream << "EventHandler::handleWheelEvent " << event << " processing steps " << processingSteps);
    auto monitor = frame->page()->wheelEventTestMonitor();
    if (monitor)
        monitor->receivedWheelEventWithPhases(event.phase(), event.momentumPhase());
#endif

    m_isHandlingWheelEvent = true;
    auto allowsScrollingState = SetForScope(m_currentWheelEventAllowsScrolling, processingSteps.contains(WheelEventProcessingSteps::SynchronousScrolling));
    
    setFrameWasScrolledByUser();
    setLastKnownMousePosition(event.position(), event.globalPosition());

    if (m_frame->isMainFrame()) {
        RefPtr page = m_frame->page();
#if ENABLE(WHEEL_EVENT_LATCHING)
        page->scrollLatchingController().receivedWheelEvent(event);
#endif
        page->wheelEventDeltaFilter()->updateFromEvent(event);
    }

    HitTestRequest request;
    HitTestResult result(view->windowToContents(event.position()));
    document->hitTest(request, result);

    RefPtr<Element> element = result.targetElement();
    WeakPtr<ScrollableArea> scrollableArea;
    bool isOverWidget = result.isOverWidget();

    // FIXME: Despite doing this up-front search for the correct scrollable area, we dispatch events via elements which
    // itself finds and tries to scroll overflow scrollers.
    determineWheelEventTarget(event, element, scrollableArea, isOverWidget);

#if PLATFORM(COCOA) || PLATFORM(WIN)
    std::unique_ptr<WheelEventTestMonitorCompletionDeferrer> deferrer;
    if (scrollableArea)
        deferrer = makeUnique<WheelEventTestMonitorCompletionDeferrer>(monitor.get(), scrollableArea->scrollingNodeIDForTesting(), WheelEventTestMonitor::DeferReason::HandlingWheelEventOnMainThread);
#endif

    if (element) {
        if (isOverWidget) {
            if (RefPtr remoteSubframe = dynamicDowncast<RemoteFrame>(subframeForTargetNode(result.protectedTargetNode().get()))) {
                if (auto wheelEventDataForRemoteFrame = userInputEventDataForRemoteFrame(remoteSubframe.get(), result.roundedPointInInnerNodeFrame()))
                    return *wheelEventDataForRemoteFrame;
            } else if (RefPtr widget = widgetForElement(*element)) {
                if (passWheelEventToWidget(event, *widget, processingSteps))
                    return completeWidgetWheelEvent(event, widget, scrollableArea);
            }
        }

        auto isCancelable = processingSteps.contains(WheelEventProcessingSteps::BlockingDOMEventDispatch) ? Event::IsCancelable::Yes : Event::IsCancelable::No;
        if (!element->dispatchWheelEvent(event, handling, isCancelable)) {
            m_isHandlingWheelEvent = false;
            if (scrollableArea && scrollableArea->scrollShouldClearLatchedState()) {
                // Web developer is controlling scrolling, so don't attempt to latch.
                if (handling.containsAll({ EventHandling::DispatchedToDOM, EventHandling::DefaultPrevented }))
                    clearLatchedState();
                scrollableArea->setScrollShouldClearLatchedState(false);
            }

            processWheelEventForScrollSnap(event, scrollableArea);
            return true;
        }
    }

    if (scrollableArea)
        scrollableArea->setScrollShouldClearLatchedState(false);

    // Event handling may have disconnected m_frame.
    if (!m_frame->page())
        return false;

    bool handledEvent = false;
    bool allowScrolling = m_currentWheelEventAllowsScrolling;

#if ENABLE(WHEEL_EVENT_LATCHING)
    if (allowScrolling)
        allowScrolling = m_frame->page()->scrollLatchingController().latchingAllowsScrollingInFrame(protectedFrame(), scrollableArea);
#endif
    auto adjustedWheelEvent = event;
    auto filteredDelta = adjustedWheelEvent.delta();
    filteredDelta = view->deltaForPropagation(filteredDelta);
    if (view->shouldBlockScrollPropagation(filteredDelta))
        return true;

    if (allowScrolling) {
        // FIXME: processWheelEventForScrolling() is only called for FrameView scrolling, not overflow scrolling, which is confusing.
        adjustedWheelEvent = adjustedWheelEvent.copyWithDeltaAndVelocity(filteredDelta, adjustedWheelEvent.scrollingVelocity());
        handledEvent = processWheelEventForScrolling(adjustedWheelEvent, scrollableArea, handling);
        processWheelEventForScrollSnap(adjustedWheelEvent, scrollableArea);
    }

    return handledEvent;
}

static void handleWheelEventPhaseInScrollableArea(ScrollableArea& scrollableArea, const WheelEvent& wheelEvent)
{
#if PLATFORM(MAC)
    if (wheelEvent.phase() == PlatformWheelEventPhase::MayBegin || wheelEvent.phase() == PlatformWheelEventPhase::Cancelled)
        scrollableArea.scrollAnimator().handleWheelEventPhase(wheelEvent.phase());
#else
    UNUSED_PARAM(scrollableArea);
    UNUSED_PARAM(wheelEvent);
#endif
}

static bool scrollViaNonPlatformEvent(ScrollableArea& scrollableArea, const WheelEvent& wheelEvent)
{
    auto filteredDelta = FloatSize(wheelEvent.deltaX(), wheelEvent.deltaY());
    filteredDelta = scrollableArea.deltaForPropagation(filteredDelta);
    ScrollGranularity scrollGranularity = wheelGranularityToScrollGranularity(wheelEvent.deltaMode());
    bool didHandleWheelEvent = false;
    if (float absoluteDelta = std::abs(filteredDelta.width()))
        didHandleWheelEvent |= scrollableArea.scroll(filteredDelta.width() > 0 ? ScrollDirection::ScrollRight : ScrollDirection::ScrollLeft, scrollGranularity, absoluteDelta);

    if (float absoluteDelta = std::abs(filteredDelta.height()))
        didHandleWheelEvent |= scrollableArea.scroll(filteredDelta.height() > 0 ? ScrollDirection::ScrollDown : ScrollDirection::ScrollUp, scrollGranularity, absoluteDelta);
    return didHandleWheelEvent;
}

bool EventHandler::handleWheelEventInAppropriateEnclosingBox(Node* startNode, const WheelEvent& wheelEvent, FloatSize& filteredPlatformDelta, const FloatSize& filteredVelocity, OptionSet<EventHandling> eventHandling)
{
    bool shouldHandleEvent = wheelEvent.deltaX() || wheelEvent.deltaY();
#if ENABLE(WHEEL_EVENT_LATCHING)
    shouldHandleEvent |= wheelEvent.phase() == PlatformWheelEventPhase::Ended;
    shouldHandleEvent |= wheelEvent.momentumPhase() == PlatformWheelEventPhase::Ended;
#endif
    if (!startNode->renderer())
        return false;

    RenderBox& initialEnclosingBox = startNode->renderer()->enclosingBox();

    // RenderListBox is special because it's a ScrollableArea that the scrolling tree doesn't know about.
    if (CheckedPtr renderListBox = dynamicDowncast<RenderListBox>(initialEnclosingBox))
        handleWheelEventPhaseInScrollableArea(*renderListBox, wheelEvent);

    if (!shouldHandleEvent)
        return false;

    auto scrollableAreaForBox = [](RenderBox& box) -> ScrollableArea* {
        if (auto* renderListBox = dynamicDowncast<RenderListBox>(box))
            return renderListBox;

        if (box.hasLayer())
            return box.layer()->scrollableArea();

        return nullptr;
    };

    RenderBox* currentEnclosingBox = &initialEnclosingBox;
#if PLATFORM(MAC)
    auto biasedDelta = ScrollingEffectsController::wheelDeltaBiasingTowardsVertical(FloatSize(wheelEvent.deltaX(), wheelEvent.deltaY()));
#else
    auto biasedDelta = FloatSize(wheelEvent.deltaX(), wheelEvent.deltaY());
#endif
    
    while (currentEnclosingBox) {
        if (auto* boxScrollableArea = scrollableAreaForBox(*currentEnclosingBox)) {
            auto platformEvent = wheelEvent.underlyingPlatformEvent();
            bool scrollingWasHandled;
            if (platformEvent) {
                auto copiedEvent = platformEvent->copyWithDeltaAndVelocity(filteredPlatformDelta, filteredVelocity);
                scrollingWasHandled = scrollableAreaCanHandleEvent(copiedEvent, *boxScrollableArea) && handleWheelEventInScrollableArea(copiedEvent, *boxScrollableArea, eventHandling);
            } else
                scrollingWasHandled = scrollViaNonPlatformEvent(*boxScrollableArea, wheelEvent);

            if (scrollingWasHandled)
                return true;
            
            biasedDelta = boxScrollableArea->deltaForPropagation(biasedDelta);
            if (boxScrollableArea->shouldBlockScrollPropagation(biasedDelta))
                return true;
        }

        currentEnclosingBox = currentEnclosingBox->containingBlock();
        if (!currentEnclosingBox || currentEnclosingBox->isRenderView())
            return false;
    }
    return false;
}

bool EventHandler::scrollableAreaCanHandleEvent(const PlatformWheelEvent& wheelEvent, ScrollableArea& scrollableArea)
{
#if PLATFORM(MAC)
    auto biasedDelta = ScrollingEffectsController::wheelDeltaBiasingTowardsVertical(wheelEvent.delta());
#else
    auto biasedDelta = wheelEvent.delta();
#endif

    auto verticalSide = ScrollableArea::targetSideForScrollDelta(-biasedDelta, ScrollEventAxis::Vertical);
    if (verticalSide && !scrollableArea.isPinnedOnSide(*verticalSide))
        return true;

    auto horizontalSide = ScrollableArea::targetSideForScrollDelta(-biasedDelta, ScrollEventAxis::Horizontal);
    if (horizontalSide && !scrollableArea.isPinnedOnSide(*horizontalSide))
        return true;
    if (scrollableArea.shouldBlockScrollPropagation(biasedDelta) && scrollableArea.overscrollBehaviorAllowsRubberBand())
        return true;

    return false;
}

bool EventHandler::handleWheelEventInScrollableArea(const PlatformWheelEvent& wheelEvent, ScrollableArea& scrollableArea, OptionSet<EventHandling> eventHandling)
{
    auto gestureState = updateWheelGestureState(wheelEvent, eventHandling);
    LOG_WITH_STREAM(Scrolling, stream << "EventHandler::handleWheelEventInScrollableArea() " << scrollableArea << " - eventHandling " << eventHandling << " -> gesture state " << gestureState);
    return scrollableArea.handleWheelEventForScrolling(wheelEvent, gestureState);
}

std::optional<WheelScrollGestureState> EventHandler::updateWheelGestureState(const PlatformWheelEvent& wheelEvent, OptionSet<EventHandling> eventHandling)
{
#if ENABLE(KINETIC_SCROLLING)
    if (!m_wheelScrollGestureState && wheelEvent.isGestureStart() && eventHandling.contains(EventHandling::DispatchedToDOM))
        m_wheelScrollGestureState = eventHandling.contains(EventHandling::DefaultPrevented) ? WheelScrollGestureState::Blocking : WheelScrollGestureState::NonBlocking;

    return m_wheelScrollGestureState;
#else
    UNUSED_PARAM(wheelEvent);
    UNUSED_PARAM(eventHandling);
    return std::nullopt;
#endif
}

void EventHandler::clearLatchedState()
{
    RefPtr<Page> page = m_frame->page();
    if (!page)
        return;

#if ENABLE(WHEEL_EVENT_LATCHING)
    LOG_WITH_STREAM(ScrollLatching, stream << "EventHandler::clearLatchedState()");
    if (auto* scrollLatchingController = page->scrollLatchingControllerIfExists())
        scrollLatchingController->removeLatchingStateForFrame(Ref<LocalFrame> { m_frame.get() });
#endif
}

void EventHandler::defaultWheelEventHandler(Node* startNode, WheelEvent& wheelEvent)
{
    if (!startNode)
        return;
    
    if (!m_frame->page())
        return;

    auto platformEvent = wheelEvent.underlyingPlatformEvent();
    bool isUserEvent = platformEvent.has_value();

    if (isUserEvent && !m_currentWheelEventAllowsScrolling)
        return;

    Ref frame = m_frame.get();

    FloatSize filteredPlatformDelta(wheelEvent.deltaX(), wheelEvent.deltaY());
    FloatSize filteredVelocity;
    if (platformEvent)
        filteredPlatformDelta = platformEvent->delta();

    OptionSet<EventHandling> eventHandling = { EventHandling::DispatchedToDOM };
    if (wheelEvent.defaultPrevented())
        eventHandling.add(EventHandling::DefaultPrevented);

    auto* deltaFilter = frame->page()->wheelEventDeltaFilter();
    if (platformEvent && deltaFilter && WheelEventDeltaFilter::shouldApplyFilteringForEvent(*platformEvent)) {
        filteredPlatformDelta = deltaFilter->filteredDelta();
        filteredVelocity = deltaFilter->filteredVelocity();
    }

#if ENABLE(WHEEL_EVENT_LATCHING)
    WeakPtr<ScrollableArea> latchedScroller;
    if (!frame->page()->scrollLatchingController().latchingAllowsScrollingInFrame(frame, latchedScroller))
        return;

    if (isUserEvent && latchedScroller) {
        if (latchedScroller == frame->view()) {
            // FrameView scrolling is handled via processWheelEventForScrolling().
            return;
        }

        if (platformEvent) {
            auto copiedEvent = platformEvent->copyWithDeltaAndVelocity(filteredPlatformDelta, filteredVelocity);
            if (handleWheelEventInScrollableArea(copiedEvent, *latchedScroller, eventHandling))
                wheelEvent.setDefaultHandled();
            return;
        }
    }
#endif

    if (handleWheelEventInAppropriateEnclosingBox(startNode, wheelEvent, filteredPlatformDelta, filteredVelocity, eventHandling))
        wheelEvent.setDefaultHandled();
}

#if ENABLE(CONTEXT_MENU_EVENT)
bool EventHandler::sendContextMenuEvent(const PlatformMouseEvent& event)
{
    Ref frame = m_frame.get();

#if ENABLE(POINTER_LOCK)
    // Context menus should not be handled while pointer is locked.
    if (auto* page = frame->page(); !page || page->pointerLockController().isLocked())
        return false;
#endif

    RefPtr doc = frame->document();
    RefPtr view = frame->view();
    if (!view)
        return false;

    // Caret blinking is normally un-suspended in handleMouseReleaseEvent, but we
    // won't receive that event once the context menu is up.
    frame->selection().setCaretBlinkingSuspended(false);
    // Clear mouse press state to avoid initiating a drag while context menu is up.
    m_mousePressed = false;
    bool swallowEvent;
    LayoutPoint viewportPos = view->windowToContents(event.position());
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent };
    MouseEventWithHitTestResults mouseEvent = doc->prepareMouseEvent(hitType, viewportPos, event);

    // Do not show context menus when clicking on scrollbars.
    if (mouseEvent.scrollbar() || view->scrollbarAtPoint(event.position()))
        return false;

    if (frame->editor().behavior().shouldSelectOnContextualMenuClick()
        && !frame->selection().contains(viewportPos)) {
        m_mouseDownMayStartSelect = true; // context menu events are always allowed to perform a selection
        selectClosestContextualWordOrLinkFromHitTestResult(mouseEvent.hitTestResult(), shouldAppendTrailingWhitespace(mouseEvent, m_frame));
    }

    swallowEvent = !dispatchMouseEvent(eventNames().contextmenuEvent, mouseEvent.protectedTargetNode().get(), 0, event, FireMouseOverOut::No);
    
    return swallowEvent;
}

bool EventHandler::sendContextMenuEventForKey()
{
    Ref frame = m_frame.get();

    RefPtr view = frame->view();
    if (!view)
        return false;

    RefPtr doc = frame->document();
    if (!doc)
        return false;

    // Clear mouse press state to avoid initiating a drag while context menu is up.
    m_mousePressed = false;

    static const int kContextMenuMargin = 1;

#if OS(WINDOWS)
    int rightAligned = ::GetSystemMetrics(SM_MENUDROPALIGNMENT);
#else
    int rightAligned = 0;
#endif
    IntPoint location { rightAligned ? view->contentsWidth() - kContextMenuMargin : kContextMenuMargin, kContextMenuMargin };

    RefPtr focusedElement = doc->focusedElement();
    const VisibleSelection& selection = frame->selection().selection();

    if (selection.start().deprecatedNode() && (selection.rootEditableElement() || selection.isRange())) {
        std::optional<SimpleRange> targetRange;
        if (selection.isCaret())
            targetRange = selection.toNormalizedRange();
        else {
            auto endPosition = selection.visibleEnd();
            targetRange = VisibleSelection { endPosition.previous(CannotCrossEditingBoundary), endPosition }.toNormalizedRange();
        }
        if (targetRange) {
            IntRect targetRect = frame->editor().firstRectForRange(*targetRange);
            int x = rightAligned ? targetRect.maxX() : targetRect.x();
            // In a multiline edit, firstRect.maxY() would endup on the next line, so -1.
            int y = targetRect.maxY() ? targetRect.maxY() - 1 : 0;
            location = IntPoint(x, y);
        }
    } else if (focusedElement) {
        RenderBoxModelObject* box = focusedElement->renderBoxModelObject();
        if (!box)
            return false;

        IntRect boundingBoxRect = box->absoluteBoundingBoxRect(true);
        location = IntPoint(boundingBoxRect.x(), boundingBoxRect.maxY() - 1);
    } else {
        location = IntPoint(
            rightAligned ? view->contentsWidth() - kContextMenuMargin : kContextMenuMargin,
            kContextMenuMargin);
    }

    frame->protectedView()->setCursor(pointerCursor());

    IntPoint position = view->contentsToRootView(location);
    IntPoint globalPosition = view->hostWindow()->rootViewToScreen(IntRect(position, IntSize())).location();

    RefPtr<Node> targetNode = doc->focusedElement();
    if (!targetNode)
        targetNode = doc;

    // Use the focused node as the target for hover and active.
    HitTestResult result(position);
    result.setInnerNode(targetNode.get());
    doc->updateHoverActiveState(OptionSet<HitTestRequest::Type> { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent }, result.targetElement());

    // The contextmenu event is a mouse event even when invoked using the keyboard.
    // This is required for web compatibility.

#if OS(WINDOWS)
    PlatformEvent::Type eventType = PlatformEvent::Type::MouseReleased;
#else
    PlatformEvent::Type eventType = PlatformEvent::Type::MousePressed;
#endif
    PlatformMouseEvent platformMouseEvent(position, globalPosition, MouseButton::Right, eventType, 1, { }, WallTime::now(), ForceAtClick, SyntheticClickType::NoTap);

    return sendContextMenuEvent(platformMouseEvent);
}
#endif // ENABLE(CONTEXT_MENU_EVENT)

void EventHandler::scheduleHoverStateUpdate()
{
    if (!m_hoverTimer.isActive())
        m_hoverTimer.startOneShot(0_s);
}

void EventHandler::scheduleCursorUpdate()
{
    if (m_hasScheduledCursorUpdate)
        return;

    RefPtr page = m_frame->page();
    if (!page)
        return;

    if (!page->chrome().client().supportsSettingCursor())
        return;

    m_hasScheduledCursorUpdate = true;
    page->scheduleRenderingUpdate(RenderingUpdateStep::CursorUpdate);
}

void EventHandler::dispatchFakeMouseMoveEventSoon()
{
#if !ENABLE(IOS_TOUCH_EVENTS)
    if (m_mousePressed)
        return;

    if (!m_lastKnownMousePosition)
        return;

    if (RefPtr page = m_frame->page()) {
        if (!page->chrome().client().shouldDispatchFakeMouseMoveEvents())
            return;
    }

    // If the content has ever taken longer than fakeMouseMoveShortInterval we
    // reschedule the timer and use a longer time. This will cause the content
    // to receive these moves only after the user is done scrolling, reducing
    // pauses during the scroll.
    if (m_fakeMouseMoveEventTimer.isActive())
        m_fakeMouseMoveEventTimer.stop();
    m_fakeMouseMoveEventTimer.startOneShot(m_maxMouseMovedDuration > fakeMouseMoveDurationThreshold ? fakeMouseMoveLongInterval : fakeMouseMoveShortInterval);
#endif
}

void EventHandler::dispatchFakeMouseMoveEventSoonInQuad(const FloatQuad& quad)
{
#if ENABLE(IOS_TOUCH_EVENTS)
    UNUSED_PARAM(quad);
#else
    RefPtr view = m_frame->view();
    if (!view)
        return;

    if (!quad.containsPoint(view->windowToContents(valueOrDefault(m_lastKnownMousePosition))))
        return;

    dispatchFakeMouseMoveEventSoon();
#endif
}

#if !ENABLE(IOS_TOUCH_EVENTS)
void EventHandler::cancelFakeMouseMoveEvent()
{
    m_fakeMouseMoveEventTimer.stop();
}

void EventHandler::fakeMouseMoveEventTimerFired()
{
    ASSERT(!m_mousePressed);

    Ref frame = m_frame.get();
    if (!frame->view())
        return;

    if (!frame->page() || !frame->page()->isVisible() || !frame->page()->focusController().isActive())
        return;

    auto modifiers = PlatformKeyboardEvent::currentStateOfModifierKeys();
    PlatformMouseEvent fakeMouseMoveEvent(valueOrDefault(m_lastKnownMousePosition), m_lastKnownMouseGlobalPosition, MouseButton::None, PlatformEvent::Type::MouseMoved, 0, modifiers, WallTime::now(), 0, SyntheticClickType::NoTap);
    mouseMoved(fakeMouseMoveEvent);
}
#endif // !ENABLE(IOS_TOUCH_EVENTS)

void EventHandler::setResizingFrameSet(HTMLFrameSetElement* frameSet)
{
    m_frameSetBeingResized = frameSet;
}

void EventHandler::resizeLayerDestroyed()
{
    ASSERT(m_resizeLayer);
    m_resizeLayer = nullptr;
}

void EventHandler::hoverTimerFired()
{
    m_hoverTimer.stop();

    ASSERT(m_frame->document());

    Ref frame = m_frame.get();

    if (RefPtr document = frame->document()) {
        if (RefPtr view = frame->view()) {
            HitTestResult result(view->windowToContents(valueOrDefault(m_lastKnownMousePosition)));
            constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Move, HitTestRequest::Type::DisallowUserAgentShadowContent };
            document->hitTest(hitType, result);
            document->updateHoverActiveState(hitType, result.targetElement());
        }
    }
}

#if ENABLE(IMAGE_ANALYSIS)

void EventHandler::textRecognitionHoverTimerFired()
{
    RefPtr element = this->textRecognitionCandidateElement();
    if (!element)
        return;

    if (RefPtr page = m_frame->page())
        page->chrome().client().requestTextRecognition(*element, { });
}

#endif // ENABLE(IMAGE_ANALYSIS)

bool EventHandler::handleAccessKey(const PlatformKeyboardEvent& event)
{
    Ref frame = m_frame.get();
    // FIXME: Ignoring the state of Shift key is what neither IE nor Firefox do.
    // IE matches lower and upper case access keys regardless of Shift key state - but if both upper and
    // lower case variants are present in a document, the correct element is matched based on Shift key state.
    // Firefox only matches an access key if Shift is not pressed, and does that case-insensitively.
    ASSERT(!accessKeyModifiers().contains(PlatformEvent::Modifier::ShiftKey));

    if ((event.modifiers() - PlatformEvent::Modifier::ShiftKey) != accessKeyModifiers())
        return false;
    RefPtr element = frame->protectedDocument()->elementForAccessKey(event.unmodifiedText());
    if (!element)
        return false;
    element->accessKeyAction(false);
    return true;
}

#if !PLATFORM(MAC)
bool EventHandler::needsKeyboardEventDisambiguationQuirks() const
{
    return false;
}
#endif

#if ENABLE(FULLSCREEN_API)
bool EventHandler::isKeyEventAllowedInFullScreen(const PlatformKeyboardEvent& keyEvent) const
{
    RefPtr document = m_frame->document();
    if (document->fullscreen().isFullscreenKeyboardInputAllowed())
        return true;

    if (keyEvent.type() == PlatformKeyboardEvent::Type::Char) {
        if (keyEvent.text().length() != 1)
            return false;
        char16_t character = keyEvent.text()[0];
        return character == ' ';
    }

    int keyCode = keyEvent.windowsVirtualKeyCode();
    return (keyCode >= VK_BACK && keyCode <= VK_CAPITAL)
        || (keyCode >= VK_SPACE && keyCode <= VK_DELETE)
        || (keyCode >= VK_OEM_1 && keyCode <= VK_OEM_PLUS)
        || (keyCode >= VK_MULTIPLY && keyCode <= VK_OEM_8);
}
#endif

bool EventHandler::keyEvent(const PlatformKeyboardEvent& keyEvent)
{
    Ref frame = m_frame.get();
    RefPtr page = frame->page();
    RefPtr mainFrameDocument = frame->document() ? frame->document()->mainFrameDocument() : nullptr;
    MonotonicTime savedLastHandledUserGestureTimestamp;
    bool savedUserDidInteractWithPage = page ? page->userDidInteractWithPage() : false;

    if (RefPtr document = frame->document())
        savedLastHandledUserGestureTimestamp = document->lastHandledUserGestureTimestamp();

    bool wasHandled = internalKeyEvent(keyEvent);

    // If the key event was not handled, do not treat it as user interaction with the page.
    if (mainFrameDocument) {
        if (!wasHandled) {
            if (page)
                page->setUserDidInteractWithPage(savedUserDidInteractWithPage);
        } else
            ResourceLoadObserver::shared().logUserInteractionWithReducedTimeResolution(*mainFrameDocument);
    }

    if (!wasHandled && frame->document())
        frame->protectedDocument()->updateLastHandledUserGestureTimestamp(savedLastHandledUserGestureTimestamp);

    return wasHandled;
}

void EventHandler::capsLockStateMayHaveChanged() const
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(m_frame->document()->focusedElement());
    if (!input)
        return;
    input->capsLockStateMayHaveChanged();
}

bool EventHandler::internalKeyEvent(const PlatformKeyboardEvent& initialKeyEvent)
{
    Ref frame = m_frame.get();
    RefPtr protectedView { frame->view() };

    LOG(Editing, "EventHandler %p keyEvent (text %s keyIdentifier %s)", this, initialKeyEvent.text().utf8().data(), initialKeyEvent.keyIdentifier().utf8().data());

#if ENABLE(POINTER_LOCK)
    if (initialKeyEvent.type() == PlatformEvent::Type::KeyDown && initialKeyEvent.windowsVirtualKeyCode() == VK_ESCAPE && frame->page()->pointerLockController().element()) {
        frame->protectedPage()->pointerLockController().requestPointerUnlockAndForceCursorVisible();
    }
#endif

    if (initialKeyEvent.type() == PlatformEvent::Type::KeyDown && initialKeyEvent.windowsVirtualKeyCode() == VK_ESCAPE) {
        if (RefPtr page = frame->page()) {
            if (auto* validationMessageClient = page->validationMessageClient())
                validationMessageClient->hideAnyValidationMessage();
        }
    }

#if ENABLE(FULLSCREEN_API)
    RefPtr document = frame->document();
    if (RefPtr documentFullscreen = document->fullscreenIfExists(); documentFullscreen && documentFullscreen->isFullscreen()) {
        if (initialKeyEvent.type() == PlatformEvent::Type::KeyDown && initialKeyEvent.windowsVirtualKeyCode() == VK_ESCAPE) {
            documentFullscreen->fullyExitFullscreen();
            return true;
        }

        if (!isKeyEventAllowedInFullScreen(initialKeyEvent))
            return false;
    }
#endif

    if (initialKeyEvent.windowsVirtualKeyCode() == VK_CAPITAL)
        capsLockStateMayHaveChanged();

#if ENABLE(PAN_SCROLLING)
    auto* localFrame = dynamicDowncast<LocalFrame>(frame->mainFrame());
    if (!localFrame)
        return false;

    if (Ref(*localFrame)->eventHandler().panScrollInProgress()) {
        // If a key is pressed while the panScroll is in progress then we want to stop
        if (initialKeyEvent.type() == PlatformEvent::Type::KeyDown || initialKeyEvent.type() == PlatformEvent::Type::RawKeyDown)
            stopAutoscrollTimer();

        // If we were in panscroll mode, we swallow the key event
        return true;
    }
#endif

    // Check for cases where we are too early for events -- possible unmatched key up
    // from pressing return in the location bar.
    RefPtr<Element> element = eventTargetElementForDocument(frame->protectedDocument().get());
    if (!element)
        return false;

    UserGestureType gestureType = userGestureTypeForPlatformEvent(initialKeyEvent);

    auto canRequestDOMPaste = frame->protectedDocument()->quirks().needsDisableDOMPasteAccessQuirk() ? CanRequestDOMPaste::No : CanRequestDOMPaste::Yes;
    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, frame->protectedDocument().get(), gestureType, UserGestureIndicator::ProcessInteractionStyle::Delayed, initialKeyEvent.authorizationToken(), canRequestDOMPaste);
    UserTypingGestureIndicator typingGestureIndicator(frame);

    // FIXME (bug 68185): this call should be made at another abstraction layer
    frame->loader().resetMultipleFormSubmissionProtection();

    // In IE, access keys are special, they are handled after default keydown processing, but cannot be canceled - this is hard to match.
    // On macOS, we process them before dispatching keydown, as the default keydown handler implements Emacs key bindings, which may conflict
    // with access keys. Then we dispatch keydown, but suppress its default handling.
    // On Windows, WebKit explicitly calls handleAccessKey() instead of dispatching a keypress event for WM_SYSCHAR messages.
    // Other platforms currently match either Mac or Windows behavior, depending on whether they send combined KeyDown events.
    bool matchedAnAccessKey = false;
    if (initialKeyEvent.type() == PlatformEvent::Type::KeyDown)
        matchedAnAccessKey = handleAccessKey(initialKeyEvent);

    if (initialKeyEvent.type() == PlatformEvent::Type::KeyUp)
        stopKeyboardScrolling();

    // FIXME: it would be fair to let an input method handle KeyUp events before DOM dispatch.
    if (initialKeyEvent.type() == PlatformEvent::Type::KeyUp || initialKeyEvent.type() == PlatformEvent::Type::Char)
        return !element->dispatchKeyEvent(initialKeyEvent);

    bool backwardCompatibilityMode = needsKeyboardEventDisambiguationQuirks();

    PlatformKeyboardEvent keyDownEvent = initialKeyEvent;    
    if (keyDownEvent.type() != PlatformEvent::Type::RawKeyDown)
        keyDownEvent.disambiguateKeyDownEvent(PlatformEvent::Type::RawKeyDown, backwardCompatibilityMode);
    auto keydown = KeyboardEvent::create(keyDownEvent, &frame->windowProxy());
    if (matchedAnAccessKey)
        keydown->preventDefault();
    keydown->setTarget(element.copyRef());

    auto setHasFocusVisibleIfNeeded = [initialKeyEvent, keydown](Element& element) {
        // If the user interacts with the page via the keyboard, the currently focused element should match :focus-visible.
        // Just typing a modifier key is not considered user interaction with the page, but Shift + a (or Caps Lock + a) is considered an interaction.
        bool userHasInteractedViaKeyword = keydown->modifierKeys().isEmpty() || ((keydown->shiftKey() || keydown->capsLockKey()) && !initialKeyEvent.text().isEmpty());

        if (element.focused() && userHasInteractedViaKeyword) {
            Style::PseudoClassChangeInvalidation focusVisibleStyleInvalidation(element, CSSSelector::PseudoClass::FocusVisible, true);
            element.setHasFocusVisible(true);
        }
    };
    setHasFocusVisibleIfNeeded(*element);

    if (initialKeyEvent.type() == PlatformEvent::Type::RawKeyDown) {
        element->dispatchEvent(keydown);
        // If frame changed as a result of keydown dispatch, then return true to avoid sending a subsequent keypress message to the new frame.
        bool changedFocusedFrame = frame->page() && frame.ptr() != frame->page()->focusController().focusedOrMainFrame();
        return keydown->defaultHandled() || keydown->defaultPrevented() || changedFocusedFrame;
    }

    // Run input method in advance of DOM event handling.  This may result in the IM
    // modifying the page prior the keydown event, but this behaviour is necessary
    // in order to match IE:
    // 1. preventing default handling of keydown and keypress events has no effect on IM input;
    // 2. if an input method handles the event, its keyCode is set to 229 in keydown event.
    frame->editor().handleInputMethodKeydown(keydown.get());
    
    bool handledByInputMethod = keydown->defaultHandled();
    
    if (handledByInputMethod) {
        keyDownEvent.setWindowsVirtualKeyCode(CompositionEventKeyCode);
        keydown = KeyboardEvent::create(keyDownEvent, &frame->windowProxy());
        keydown->setTarget(element.copyRef());
        keydown->setIsDefaultEventHandlerIgnored();
    }
    
    if (accessibilityPreventsEventPropagation(keydown))
        keydown->stopPropagation();

#if ENABLE(CONTENT_CHANGE_OBSERVER)
    DeferDOMTimersForScope deferralScope { frame->document()->quirks().needsDeferKeyDownAndKeyPressTimersUntilNextEditingCommand() };
#endif

    element->dispatchEvent(keydown);
    if (handledByInputMethod) {
        frame->editor().didDispatchInputMethodKeydown(keydown.get());
        return true;
    }

    // If frame changed as a result of keydown dispatch, then return early to avoid sending a subsequent keypress message to the new frame.
    bool changedFocusedFrame = frame->page() && frame.ptr() != frame->page()->focusController().focusedOrMainFrame();
    bool keydownResult = keydown->defaultHandled() || keydown->defaultPrevented() || changedFocusedFrame;
    if (keydownResult && !backwardCompatibilityMode)
        return keydownResult;

    // Focus may have changed during keydown handling, so refetch element.
    // But if we are dispatching a fake backward compatibility keypress, then we pretend that the keypress happened on the original element.
    if (!keydownResult) {
        element = eventTargetElementForDocument(frame->protectedDocument().get());
        if (!element)
            return false;
        setHasFocusVisibleIfNeeded(*element);
    }

    PlatformKeyboardEvent keyPressEvent = initialKeyEvent;
    keyPressEvent.disambiguateKeyDownEvent(PlatformEvent::Type::Char, backwardCompatibilityMode);
    if (keyPressEvent.text().isEmpty())
        return keydownResult;
    auto keypress = KeyboardEvent::create(keyPressEvent, &frame->windowProxy());
    keypress->setTarget(element.copyRef());
    if (keypress->isComposing()) {
        frame->editor().handleKeyboardEvent(keypress);
        return keydownResult;
    }
    if (keydownResult)
        keypress->preventDefault();
#if PLATFORM(COCOA)
    keypress->keypressCommands() = keydown->keypressCommands();
#endif
    element->dispatchEvent(keypress);

    return keydownResult || keypress->defaultPrevented() || keypress->defaultHandled();
}

static FocusDirection focusDirectionForKey(const AtomString& keyIdentifier)
{
    static MainThreadNeverDestroyed<const AtomString> Down("Down"_s);
    static MainThreadNeverDestroyed<const AtomString> Up("Up"_s);
    static MainThreadNeverDestroyed<const AtomString> Left("Left"_s);
    static MainThreadNeverDestroyed<const AtomString> Right("Right"_s);

    FocusDirection retVal = FocusDirection::None;

    if (keyIdentifier == Down)
        retVal = FocusDirection::Down;
    else if (keyIdentifier == Up)
        retVal = FocusDirection::Up;
    else if (keyIdentifier == Left)
        retVal = FocusDirection::Left;
    else if (keyIdentifier == Right)
        retVal = FocusDirection::Right;

    return retVal;
}

static void setInitialKeyboardSelection(LocalFrame& frame, SelectionDirection direction)
{
    RefPtr document = frame.document();
    if (!document)
        return;

    FrameSelection& selection = frame.selection();

    if (!selection.isNone())
        return;

    RefPtr focusedElement = document->focusedElement();
    VisiblePosition visiblePosition;

    switch (direction) {
    case SelectionDirection::Backward:
    case SelectionDirection::Left:
        if (focusedElement)
            visiblePosition = VisiblePosition(positionBeforeNode(focusedElement.get()));
        else
            visiblePosition = endOfDocument(document.get());
        break;
    case SelectionDirection::Forward:
    case SelectionDirection::Right:
        if (focusedElement)
            visiblePosition = VisiblePosition(positionAfterNode(focusedElement.get()));
        else
            visiblePosition = startOfDocument(document.get());
        break;
    }

    AXTextStateChangeIntent intent(AXTextStateChangeTypeSelectionMove, AXTextSelection { AXTextSelectionDirectionDiscontiguous, AXTextSelectionGranularityUnknown, false });
    selection.setSelection(visiblePosition, FrameSelection::defaultSetSelectionOptions(UserTriggered::Yes), intent);
}

static void handleKeyboardSelectionMovement(LocalFrame& frame, KeyboardEvent& event)
{
    FrameSelection& selection = frame.selection();

    bool isCommanded = event.getModifierState("Meta"_s);
    bool isOptioned = event.getModifierState("Alt"_s);
    bool isSelection = !selection.isNone();

    FrameSelection::Alteration alternation = event.getModifierState("Shift"_s) ? FrameSelection::Alteration::Extend : FrameSelection::Alteration::Move;
    SelectionDirection direction = SelectionDirection::Forward;
    TextGranularity granularity = TextGranularity::CharacterGranularity;

    switch (focusDirectionForKey(event.keyIdentifier())) {
    case FocusDirection::None:
        return;
    case FocusDirection::Forward:
    case FocusDirection::Backward:
        ASSERT_NOT_REACHED();
        return;
    case FocusDirection::Up:
        direction = SelectionDirection::Backward;
        granularity = isCommanded ? TextGranularity::DocumentBoundary : TextGranularity::LineGranularity;
        break;
    case FocusDirection::Down:
        direction = SelectionDirection::Forward;
        granularity = isCommanded ? TextGranularity::DocumentBoundary : TextGranularity::LineGranularity;
        break;
    case FocusDirection::Left:
        direction = SelectionDirection::Left;
        granularity = (isCommanded) ? TextGranularity::LineBoundary : (isOptioned) ? TextGranularity::WordGranularity : TextGranularity::CharacterGranularity;
        break;
    case FocusDirection::Right:
        direction = SelectionDirection::Right;
        granularity = (isCommanded) ? TextGranularity::LineBoundary : (isOptioned) ? TextGranularity::WordGranularity : TextGranularity::CharacterGranularity;
        break;
    }

    if (isSelection)
        selection.modify(alternation, direction, granularity, UserTriggered::Yes);
    else
        setInitialKeyboardSelection(frame, direction);

    event.setDefaultHandled();
}

void EventHandler::handleKeyboardSelectionMovementForAccessibility(KeyboardEvent& event)
{
    if (event.type() == eventNames().keydownEvent) {
        if (AXObjectCache::accessibilityEnhancedUserInterfaceEnabled())
            handleKeyboardSelectionMovement(protectedFrame(), event);
    }
}

bool EventHandler::accessibilityPreventsEventPropagation(KeyboardEvent& event)
{
#if PLATFORM(COCOA)
    if (!AXObjectCache::accessibilityEnhancedUserInterfaceEnabled())
        return false;

    if (!m_frame->settings().preventKeyboardDOMEventDispatch())
        return false;

    // Check for key events that are relevant to accessibility: tab and arrows keys that change focus
    if (event.keyIdentifier() == "U+0009"_s)
        return true;
    FocusDirection direction = focusDirectionForKey(event.keyIdentifier());
    if (direction != FocusDirection::None)
        return true;
#else
    UNUSED_PARAM(event);
#endif
    return false;
}

void EventHandler::defaultKeyboardEventHandler(KeyboardEvent& event)
{
    Ref frame = m_frame.get();

    // 'keyup' is handled preemptively in `EventHandler::internalKeyEvent` so that keyboard scrolls
    // can be properly terminated even if the event is default-prevented.

    if (event.type() == eventNames().keydownEvent) {
        frame->editor().handleKeyboardEvent(event);
        if (event.defaultHandled())
            return;

        if (event.key() == "Escape"_s) {
            if (frame->settings().closeWatcherEnabled())
                frame->document()->window()->closeWatcherManager().escapeKeyHandler(event);
            if (RefPtr activeModalDialog = frame->document()->activeModalDialog())
                activeModalDialog->queueCancelTask();
            if (RefPtr topmostAutoPopover = frame->document()->topmostAutoPopover())
                topmostAutoPopover->hidePopover();
        } else if (event.keyIdentifier() == "U+0009"_s)
            defaultTabEventHandler(event);
        else if (event.keyIdentifier() == "U+0008"_s)
            defaultBackspaceEventHandler(event);
        else if (event.keyIdentifier() == "PageDown"_s || event.keyIdentifier() == "PageUp"_s)
            defaultPageUpDownEventHandler(event);
        else if (event.keyIdentifier() == "Home"_s || event.keyIdentifier() == "End"_s)
            defaultHomeEndEventHandler(event);
        else {
            FocusDirection direction = focusDirectionForKey(event.keyIdentifier());
            if (direction != FocusDirection::None)
                defaultArrowEventHandler(direction, event);
        }

        handleKeyboardSelectionMovementForAccessibility(event);
    }
    if (event.type() == eventNames().keypressEvent) {
        frame->editor().handleKeyboardEvent(event);
        if (event.defaultHandled())
            return;
        if (event.charCode() == ' ')
            defaultSpaceEventHandler(event);
    }
}

#if ENABLE(DRAG_SUPPORT)
bool EventHandler::dragHysteresisExceeded(const IntPoint& floatDragViewportLocation) const
{
    FloatPoint dragViewportLocation(floatDragViewportLocation.x(), floatDragViewportLocation.y());
    return dragHysteresisExceeded(dragViewportLocation);
}

bool EventHandler::dragHysteresisExceeded(const FloatPoint& dragViewportLocation) const
{
    auto dragOperation = dragState().type.toSingleValue();
    ASSERT(dragOperation);
    int threshold = GeneralDragHysteresis;
    if (dragOperation) {
        switch (*dragOperation) {
        case DragSourceAction::Selection:
            threshold = TextDragHysteresis;
            break;
        case DragSourceAction::Image:
#if ENABLE(ATTACHMENT_ELEMENT)
        case DragSourceAction::Attachment:
#endif
#if ENABLE(MODEL_ELEMENT)
        case DragSourceAction::Model:
#endif
            threshold = ImageDragHysteresis;
            break;
        case DragSourceAction::Link:
            threshold = LinkDragHysteresis;
            break;
        case DragSourceAction::Color:
            threshold = ColorDragHystersis;
            break;
        case DragSourceAction::DHTML:
            break;
        }
    }

    return mouseMovementExceedsThreshold(dragViewportLocation, threshold);
}

void EventHandler::invalidateDataTransfer()
{
    if (!dragState().dataTransfer)
        return;
    dragState().dataTransfer->makeInvalidForSecurity();
    dragState().dataTransfer = nullptr;
}

static void removeDraggedContentDocumentMarkersFromAllFramesInPage(Page& page)
{
    page.forEachDocument([] (Document& document) {
        document.markers().removeMarkers(DocumentMarkerType::DraggedContent);
    });

    if (RefPtr localMainFrame = page.localMainFrame()) {
        if (auto* mainFrameRenderer = localMainFrame->contentRenderer())
            mainFrameRenderer->repaintRootContents();
    }
}

void EventHandler::dragCancelled()
{
#if PLATFORM(IOS_FAMILY)
    if (RefPtr page = m_frame->page())
        removeDraggedContentDocumentMarkersFromAllFramesInPage(*page);
#endif
}

void EventHandler::didStartDrag()
{
#if PLATFORM(IOS_FAMILY)
    RefPtr dragSource = draggedElement();
    if (!dragSource)
        return;

    if (!dragSource->renderer())
        return;

    std::optional<SimpleRange> draggedContentRange;
    if (dragState().type.contains(DragSourceAction::Selection))
        draggedContentRange = m_frame->selection().selection().toNormalizedRange();
    else
        draggedContentRange = makeRangeSelectingNode(*dragSource);

    if (draggedContentRange) {
        draggedContentRange->start.document().markers().addDraggedContentMarker(*draggedContentRange);
        if (CheckedPtr renderer = m_frame->contentRenderer())
            renderer->repaintRootContents();
    }
#endif
}

std::optional<RemoteUserInputEventData> EventHandler::dragSourceEndedAt(const PlatformMouseEvent& event, OptionSet<DragOperation> dragOperationMask, MayExtendDragSession mayExtendDragSession)
{
    // Send a hit test request so that RenderLayer gets a chance to update the :hover and :active pseudoclasses.
    auto mouseEvent = prepareMouseEvent(OptionSet<HitTestRequest::Type> { HitTestRequest::Type::Release, HitTestRequest::Type::DisallowUserAgentShadowContent }, event);
    if (RefPtr remoteSubframe = dynamicDowncast<RemoteFrame>(subframeForHitTestResult(mouseEvent))) {
        // FIXME(264611): These mouse coordinates need to be correctly transformed.
        return RemoteUserInputEventData { remoteSubframe->frameID(),  mouseEvent.hitTestResult().roundedPointInInnerNodeFrame() };
    }

    if (shouldDispatchEventsToDragSourceElement()) {
        dragState().dataTransfer->setDestinationOperationMask(dragOperationMask);
        dispatchEventToDragSourceElement(eventNames().dragendEvent, event);
    }
    invalidateDataTransfer();

    if (mayExtendDragSession == MayExtendDragSession::No) {
        if (RefPtr page = m_frame->page())
            removeDraggedContentDocumentMarkersFromAllFramesInPage(*page);
    }

    setDragStateSource(nullptr);
    // In case the drag was ended due to an escape key press we need to ensure
    // that consecutive mousemove events don't reinitiate the drag and drop.
    m_mouseDownMayStartDrag = false;
    return std::nullopt;
}

void EventHandler::updateDragStateAfterEditDragIfNeeded(Element& rootEditableElement)
{
    // If inserting the dragged contents removed the drag source, we still want to fire dragend at the root editable element.
    if (draggedElement() && !draggedElement()->isConnected())
        setDragStateSource(&rootEditableElement);
}

bool EventHandler::shouldDispatchEventsToDragSourceElement()
{
    return draggedElement() && dragState().dataTransfer && dragState().shouldDispatchEvents;
}

void EventHandler::dispatchEventToDragSourceElement(const AtomString& eventType, const PlatformMouseEvent& event)
{
    if (shouldDispatchEventsToDragSourceElement())
        dispatchDragEvent(eventType, *protectedDraggedElement(), event, *dragState().dataTransfer);
}

bool EventHandler::dispatchDragStartEventOnSourceElement(DataTransfer& dataTransfer)
{
    if (RefPtr page = m_frame->page())
        page->dragController().prepareForDragStart(protectedFrame(), dragState().type, *protectedDraggedElement(), dataTransfer, m_mouseDownContentsPosition);
    return !dispatchDragEvent(eventNames().dragstartEvent, *protectedDraggedElement(), m_mouseDownEvent, dataTransfer) && !m_frame->selection().selection().isInPasswordField();
}

bool EventHandler::handleDrag(const MouseEventWithHitTestResults& event, CheckDragHysteresis checkDragHysteresis)
{
    if (event.event().button() != MouseButton::Left || event.event().type() != PlatformEvent::Type::MouseMoved) {
        // If we allowed the other side of the bridge to handle a drag
        // last time, then m_mousePressed might still be set. So we
        // clear it now to make sure the next move after a drag
        // doesn't look like a drag.
        m_mousePressed = false;
        return false;
    }
    
    Ref frame = m_frame.get();

    if (eventLoopHandleMouseDragged(event))
        return true;
    
    // Careful that the drag starting logic stays in sync with eventMayStartDrag().
    if (m_mouseDownMayStartDrag && !draggedElement()) {
        dragState().shouldDispatchEvents = updateDragSourceActionsAllowed().contains(DragSourceAction::DHTML);
        dragState().restrictedOriginForImageData = nullptr;

        // Try to find an element that wants to be dragged.
        HitTestResult result(m_mouseDownContentsPosition);
        frame->protectedDocument()->hitTest(OptionSet<HitTestRequest::Type> { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::DisallowUserAgentShadowContent }, result);
        if (RefPtr page = frame->page())
            setDragStateSource(page->dragController().draggableElement(frame.ptr(), result.protectedTargetElement().get(), m_mouseDownContentsPosition, dragState()).get());

        if (!draggedElement())
            m_mouseDownMayStartDrag = false; // no element is draggable
        else
            m_dragMayStartSelectionInstead = dragState().type.contains(DragSourceAction::Selection);
    }
    
    // For drags starting in the selection, the user must wait between the mousedown and mousedrag,
    // or else we bail on the dragging stuff and allow selection to occur
    if (m_mouseDownMayStartDrag && m_dragMayStartSelectionInstead && dragState().type.contains(DragSourceAction::Selection) && event.event().timestamp() - m_mouseDownTimestamp < TextDragDelay) {
        ASSERT(event.event().type() == PlatformEvent::Type::MouseMoved);
        if (dragState().type.contains(DragSourceAction::Image)) {
            // ... unless the mouse is over an image, then we start dragging just the image
            dragState().type = DragSourceAction::Image;
        } else if (!dragState().type.containsAny({ DragSourceAction::DHTML, DragSourceAction::Link })) {
            // ... but only bail if we're not over an unselectable element.
            m_mouseDownMayStartDrag = false;
            setDragStateSource(nullptr);
            // ... but if this was the first click in the window, we don't even want to start selection
            if (eventActivatedView(event.event()))
                m_mouseDownMayStartSelect = false;
        } else {
            // Prevent the following case from occuring:
            // 1. User starts a drag immediately after mouse down over an unselectable element.
            // 2. We enter this block and decided that since we're over an unselectable element, don't cancel the drag.
            // 3. The drag gets resolved as a potential selection drag below /but/ we haven't exceeded the drag hysteresis yet.
            // 4. We enter this block again, and since it's now marked as a selection drag, we cancel the drag.
            m_dragMayStartSelectionInstead = false;
        }
    }
    
    if (!m_mouseDownMayStartDrag)
        return !mouseDownMayStartSelect() && !m_mouseDownMayStartAutoscroll;
    ASSERT(draggedElement());

    if (!dragState().type.hasExactlyOneBitSet()) {
        ASSERT(dragState().type.contains(DragSourceAction::Selection));
#ifndef NDEBUG
        auto actionMaskCopy = dragState().type;
        actionMaskCopy.remove(DragSourceAction::Selection);
        ASSERT(actionMaskCopy.hasExactlyOneBitSet());
#endif

        dragState().type = DragSourceAction::Selection;
    }

    // We are starting a text/image/url drag, so the cursor should be an arrow
    if (RefPtr view = frame->view()) {
        // FIXME <rdar://7577595>: Custom cursors aren't supported during drag and drop (default to pointer).
        view->setCursor(pointerCursor());
    }

    if (checkDragHysteresis == ShouldCheckDragHysteresis && !dragHysteresisExceeded(event.event().position()))
        return true;
    
    // Once we're past the hysteresis point, we don't want to treat this gesture as a click
    invalidateClick();
    
    OptionSet<DragOperation> sourceOperationMask;
    
    // This does work only if we missed a dragEnd. Do it anyway, just to make sure the old dataTransfer gets numbed.
    // FIXME: Consider doing this earlier in this function as the earliest point we're sure it would be safe to drop an old drag.
    invalidateDataTransfer();

    RefPtr document = frame->document();
    if (!document)
        return false;

    dragState().dataTransfer = DataTransfer::createForDrag(*document);
    auto hasNonDefaultPasteboardData = HasNonDefaultPasteboardData::No;
    
    if (dragState().shouldDispatchEvents) {
        ASSERT(draggedElement());
        auto dragStartDataTransfer = DataTransfer::createForDragStartEvent(draggedElement()->protectedDocument());
        m_mouseDownMayStartDrag = dispatchDragStartEventOnSourceElement(dragStartDataTransfer);
        if (downcast<StaticPasteboard>(dragStartDataTransfer->pasteboard()).hasNonDefaultData())
            hasNonDefaultPasteboardData = HasNonDefaultPasteboardData::Yes;
        dragState().dataTransfer->moveDragState(WTFMove(dragStartDataTransfer));

        if (RefPtr draggedElement = this->draggedElement(); draggedElement && dragState().type == DragSourceAction::DHTML && !dragState().dataTransfer->hasDragImage()) {
            draggedElement->protectedDocument()->updateStyleIfNeeded();
            if (auto* renderer = draggedElement->renderer()) {
                auto absolutePosition = renderer->localToAbsolute();
                auto delta = m_mouseDownContentsPosition - roundedIntPoint(absolutePosition);
                dragState().dataTransfer->setDragImage(draggedElement.releaseNonNull(), delta.width(), delta.height());
            } else {
                dispatchEventToDragSourceElement(eventNames().dragendEvent, event.event());
                m_mouseDownMayStartDrag = false;
                invalidateDataTransfer();
                setDragStateSource(nullptr);
                return true;
            }
        }

        if (draggedElement() && dragState().type.containsAny({ DragSourceAction::DHTML, DragSourceAction::Image })) {
            if (auto* renderImage = dynamicDowncast<RenderImage>(draggedElement()->renderer())) {
                auto* image = renderImage->cachedImage();
                if (image && !image->isCORSSameOrigin())
                    dragState().restrictedOriginForImageData = SecurityOrigin::create(image->url());
            }
        }

        dragState().dataTransfer->makeInvalidForSecurity();

        if (m_mouseDownMayStartDrag) {
            // Gather values from DHTML element, if it set any.
            sourceOperationMask = dragState().dataTransfer->sourceOperationMask();
            
            // Yuck, a draggedImage:moveTo: message can be fired as a result of kicking off the
            // drag with dragImage! Because of that reentrancy, we may think we've not
            // started the drag when that happens. So we have to assume it's started before we kick it off.
            dragState().dataTransfer->setDragHasStarted();
        }
    }
    
    if (m_mouseDownMayStartDrag) {
        RefPtr page = frame->page();
        m_didStartDrag = page && page->dragController().startDrag(frame, dragState(), sourceOperationMask, event.event(), m_mouseDownContentsPosition, hasNonDefaultPasteboardData);
        // In WebKit2 we could re-enter this code and start another drag.
        // On macOS this causes problems with the ownership of the pasteboard and the promised types.
        if (m_didStartDrag) {
            m_mouseDownMayStartDrag = false;
            return true;
        }
        if (shouldDispatchEventsToDragSourceElement()) {
            // Drag was canned at the last minute. We owe dragSource a dragend event.
            dispatchEventToDragSourceElement(eventNames().dragendEvent, event.event());
            m_mouseDownMayStartDrag = false;
        }
    }

    if (!m_mouseDownMayStartDrag) {
        // Something failed to start the drag, clean up.
        invalidateDataTransfer();
        setDragStateSource(nullptr);
    }
    
    // No more default handling (like selection), whether we're past the hysteresis bounds or not
    return true;
}
#endif // ENABLE(DRAG_SUPPORT)

bool EventHandler::mouseMovementExceedsThreshold(const FloatPoint& viewportLocation, int pointsThreshold) const
{
    RefPtr view = m_frame->view();
    if (!view)
        return false;
    IntPoint location = view->windowToContents(flooredIntPoint(viewportLocation));
    IntSize delta = location - m_mouseDownContentsPosition;
    
    return std::abs(delta.width()) >= pointsThreshold || std::abs(delta.height()) >= pointsThreshold;
}

bool EventHandler::handleTextInputEvent(const String& text, Event* underlyingEvent, TextEventInputType inputType)
{
    LOG(Editing, "EventHandler %p handleTextInputEvent (text %s)", this, text.utf8().data());

    // Platforms should differentiate real commands like selectAll from text input in disguise (like insertNewline),
    // and avoid dispatching text input events from keydown default handlers.
    ASSERT(!is<KeyboardEvent>(underlyingEvent) || downcast<KeyboardEvent>(*underlyingEvent).type() == eventNames().keypressEvent);

    Ref frame = m_frame.get();

    EventTarget* target;
    if (underlyingEvent)
        target = underlyingEvent->target();
    else
        target = eventTargetElementForDocument(frame->protectedDocument().get());
    if (!target)
        return false;

    auto event = TextEvent::create(&frame->windowProxy(), text, inputType);
    event->setUnderlyingEvent(underlyingEvent);

    target->dispatchEvent(event);
    return event->defaultHandled();
}
    
bool EventHandler::isKeyboardOptionTab(const FocusEventData& focusEventData)
{
    auto& eventNames = WebCore::eventNames();
    return (focusEventData.type == eventNames.keydownEvent || focusEventData.type == eventNames.keypressEvent)
        && focusEventData.altKey
        && focusEventData.keyIdentifier == "U+0009"_s;
}

bool EventHandler::eventInvertsTabsToLinksClientCallResult(const FocusEventData& focusEventData)
{
#if PLATFORM(COCOA)
    return isKeyboardOptionTab(focusEventData);
#else
    UNUSED_PARAM(focusEventData);
    return false;
#endif
}

bool EventHandler::tabsToLinks(KeyboardEvent* event) const
{
    return event ? tabsToLinks(event->focusEventData()) : false;
}

bool EventHandler::tabsToLinks(const FocusEventData& focusEventData) const
{
    // FIXME: This function needs a better name. It can be called for keypresses other than Tab when spatial navigation is enabled.

    RefPtr page = m_frame->page();
    if (!page)
        return false;

    bool tabsToLinksClientCallResult = page->chrome().client().keyboardUIMode() & KeyboardAccessTabsToLinks;
    return eventInvertsTabsToLinksClientCallResult(focusEventData) ? !tabsToLinksClientCallResult : tabsToLinksClientCallResult;
}

bool EventHandler::tabsToAllFormControls(KeyboardEvent* event) const
{
    return event ? tabsToAllFormControls(event->focusEventData()) : false;
}

bool EventHandler::tabsToAllFormControls(const FocusEventData& focusEventData) const
{
#if PLATFORM(COCOA)
    RefPtr page = m_frame->page();
    if (!page)
        return false;

    KeyboardUIMode keyboardUIMode = page->chrome().client().keyboardUIMode();
    bool handlingOptionTab = isKeyboardOptionTab(focusEventData);

    // If tab-to-links is off, option-tab always highlights all controls
    if (!(keyboardUIMode & KeyboardAccessTabsToLinks) && handlingOptionTab)
        return true;

    // If system preferences say to include all controls, we always include all controls
    if (keyboardUIMode & KeyboardAccessFull)
        return true;

    // Otherwise tab-to-links includes all controls, unless the sense is flipped via option-tab.
    if (keyboardUIMode & KeyboardAccessTabsToLinks)
        return !handlingOptionTab;

    return handlingOptionTab;
#else
    UNUSED_PARAM(focusEventData);
    // We always allow tabs to all controls
    return true;
#endif
}

void EventHandler::defaultTextInputEventHandler(TextEvent& event)
{
    if (m_frame->editor().handleTextEvent(event))
        event.setDefaultHandled();
}

bool EventHandler::defaultKeyboardScrollEventHandler(KeyboardEvent& event, ScrollLogicalDirection direction, ScrollGranularity granularity)
{
    if (shouldUseSmoothKeyboardScrollingForFocusedScrollableArea())
        return keyboardScrollRecursively(scrollDirectionForKeyboardEvent(event), scrollGranularityForKeyboardEvent(event), nullptr, event.repeat());

    return logicalScrollRecursively(direction, granularity);
}

void EventHandler::defaultPageUpDownEventHandler(KeyboardEvent& event)
{
#if PLATFORM(GTK) || PLATFORM(WPE) || PLATFORM(WIN)
    ASSERT(event.type() == eventNames().keydownEvent);

    if (event.ctrlKey() || event.metaKey() || event.altKey() || event.shiftKey())
        return;

    ScrollLogicalDirection direction = event.keyIdentifier() == "PageUp"_s ? ScrollBlockDirectionBackward : ScrollBlockDirectionForward;
    if (defaultKeyboardScrollEventHandler(event, direction, ScrollGranularity::Page))
        event.setDefaultHandled();
#else
    UNUSED_PARAM(event);
#endif
}

void EventHandler::defaultHomeEndEventHandler(KeyboardEvent& event)
{
#if PLATFORM(GTK) || PLATFORM(WPE) || PLATFORM(WIN)
    ASSERT(event.type() == eventNames().keydownEvent);

    if (event.ctrlKey() || event.metaKey() || event.altKey() || event.shiftKey())
        return;

    ScrollLogicalDirection direction = event.keyIdentifier() == "Home"_s ? ScrollBlockDirectionBackward : ScrollBlockDirectionForward;
    if (defaultKeyboardScrollEventHandler(event, direction, ScrollGranularity::Document))
        event.setDefaultHandled();
#else
    UNUSED_PARAM(event);
#endif
}

void EventHandler::defaultSpaceEventHandler(KeyboardEvent& event)
{
    Ref frame = m_frame.get();

    ASSERT(event.type() == eventNames().keypressEvent);

    if (event.ctrlKey() || event.metaKey() || event.altKey())
        return;

    ScrollLogicalDirection direction = event.shiftKey() ? ScrollBlockDirectionBackward : ScrollBlockDirectionForward;
    if (logicalScrollOverflow(direction, ScrollGranularity::Page)) {
        event.setDefaultHandled();
        return;
    }

    RefPtr view = frame->view();
    if (!view)
        return;

    bool defaultHandled = false;
    if (shouldUseSmoothKeyboardScrollingForFocusedScrollableArea())
        defaultHandled = keyboardScroll(scrollDirectionForKeyboardEvent(event), scrollGranularityForKeyboardEvent(event), nullptr, event.repeat());
    else
        defaultHandled = view->logicalScroll(direction, ScrollGranularity::Page);

    if (defaultHandled)
        event.setDefaultHandled();
}

void EventHandler::defaultBackspaceEventHandler(KeyboardEvent& event)
{
    ASSERT(event.type() == eventNames().keydownEvent);

    if (event.ctrlKey() || event.metaKey() || event.altKey())
        return;

    if (!m_frame->editor().behavior().shouldNavigateBackOnBackspace())
        return;
    
    RefPtr page = m_frame->page();
    if (!page)
        return;

    if (!m_frame->settings().backspaceKeyNavigationEnabled())
        return;
    
    bool handledEvent = false;

    if (event.shiftKey())
        handledEvent = page->checkedBackForward()->goForward();
    else
        handledEvent = page->checkedBackForward()->goBack();

    if (handledEvent)
        event.setDefaultHandled();
}

void EventHandler::stopKeyboardScrolling()
{
    RefPtr page = m_frame->page();
    if (!page)
        return;
    if (CheckedPtr animator = page->currentKeyboardScrollingAnimator())
        animator->handleKeyUpEvent();
}

bool EventHandler::beginKeyboardScrollGesture(KeyboardScrollingAnimator* animator, ScrollDirection direction, ScrollGranularity granularity, bool isKeyRepeat)
{
    if (animator && animator->beginKeyboardScrollGesture(direction, granularity, isKeyRepeat)) {
        m_frame->protectedPage()->setCurrentKeyboardScrollingAnimator(animator);
        return true;
    }

    return false;
}

bool EventHandler::startKeyboardScrollAnimationOnDocument(ScrollDirection direction, ScrollGranularity granularity, bool isKeyRepeat)
{
    RefPtr view = m_frame->view();
    if (!view)
        return false;

    if (RefPtr pluginDocument = dynamicDowncast<PluginDocument>(m_frame->document())) {
        if (RefPtr plugin = dynamicDowncast<RenderEmbeddedObject>(pluginDocument->pluginElement()->renderer())) {
            if (startKeyboardScrollAnimationOnPlugin(direction, granularity, *plugin, isKeyRepeat))
                return true;
        }
    }

    auto* animator = view->scrollAnimator().keyboardScrollingAnimator();
    return beginKeyboardScrollGesture(animator, direction, granularity, isKeyRepeat);
}

bool EventHandler::startKeyboardScrollAnimationOnRenderBoxLayer(ScrollDirection direction, ScrollGranularity granularity, RenderBox* renderBox, bool isKeyRepeat)
{
    auto* scrollableArea = renderBox->layer() ? renderBox->layer()->scrollableArea() : nullptr;
    if (!scrollableArea)
        return false;

    auto* animator = scrollableArea->scrollAnimator().keyboardScrollingAnimator();
    return beginKeyboardScrollGesture(animator, direction, granularity, isKeyRepeat);
}

bool EventHandler::startKeyboardScrollAnimationOnRenderBoxAndItsAncestors(ScrollDirection direction, ScrollGranularity granularity, RenderBox* renderBox, bool isKeyRepeat)
{
    while (renderBox && !renderBox->isRenderView()) {
        if (startKeyboardScrollAnimationOnRenderBoxLayer(direction, granularity, renderBox, isKeyRepeat))
            return true;
        renderBox = renderBox->containingBlock();
    }

    return false;
}

bool EventHandler::startKeyboardScrollAnimationOnPlugin(ScrollDirection direction, ScrollGranularity granularity, RenderEmbeddedObject& pluginRenderer, bool isKeyRepeat)
{
    auto* scrollableArea = pluginRenderer.scrollableArea();
    if (!scrollableArea)
        return false;

    auto* animator = scrollableArea->scrollAnimator().keyboardScrollingAnimator();
    if (!animator)
        return false;

    return beginKeyboardScrollGesture(animator, direction, granularity, isKeyRepeat);
}

bool EventHandler::startKeyboardScrollAnimationOnEnclosingScrollableContainer(ScrollDirection direction, ScrollGranularity granularity, Node* startingNode, bool isKeyRepeat)
{
    RefPtr node = startingNode;

    if (!node)
        node = m_frame->document()->focusedElement();

    if (!node)
        node = m_mousePressNode.get();

    if (node) {
        auto renderer = node->renderer();
        if (!renderer)
            return false;

        if (RefPtr plugin = dynamicDowncast<RenderEmbeddedObject>(renderer)) {
            if (startKeyboardScrollAnimationOnPlugin(direction, granularity, *plugin, isKeyRepeat))
                return true;
        }

        RenderBox& renderBox = renderer->enclosingBox();
        if (!renderer->isRenderListBox() && startKeyboardScrollAnimationOnRenderBoxAndItsAncestors(direction, granularity, &renderBox, isKeyRepeat))
            return true;
    }
    return false;
}

ScrollableArea* EventHandler::focusedScrollableArea() const
{
    RefPtr<Node> node = m_frame->document()->focusedElement();
    if (!node)
        node = m_mousePressNode;

    if (!node)
        node = lastTouchedNode();

    return enclosingScrollableArea(node.get());
}

bool EventHandler::shouldUseSmoothKeyboardScrollingForFocusedScrollableArea()
{
    if (!m_frame->settings().eventHandlerDrivenSmoothKeyboardScrollingEnabled())
        return false;

    auto scrollableArea = focusedScrollableArea();
    if (!scrollableArea)
        return false;

    if (scrollableArea->scrollAnimator().usesScrollSnap())
        return false;

#if PLATFORM(GTK) || PLATFORM(WPE)
    if (!m_frame->settings().asyncFrameScrollingEnabled())
        return false;
#endif

    if (!scrollableArea->scrollAnimatorEnabled())
        return false;

    return true;
}

bool EventHandler::keyboardScrollRecursively(std::optional<ScrollDirection> direction, std::optional<ScrollGranularity> granularity, Node* startingNode, bool isKeyRepeat)
{
    if (!direction || !granularity)
        return false;

    Ref frame = m_frame.get();

    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();

    if (startKeyboardScrollAnimationOnEnclosingScrollableContainer(*direction, *granularity, startingNode, isKeyRepeat))
        return true;

    if (startKeyboardScrollAnimationOnDocument(*direction, *granularity, isKeyRepeat))
        return true;

    frame = m_frame.get();
    RefPtr parent = frame->tree().parent();
    if (!parent)
        return false;
    RefPtr localParent = dynamicDowncast<LocalFrame>(parent.get());
    if (!localParent)
        return false;

    return localParent->eventHandler().keyboardScrollRecursively(direction, granularity, frame->protectedOwnerElement().get(), isKeyRepeat);
}

bool EventHandler::keyboardScroll(std::optional<ScrollDirection> direction, std::optional<ScrollGranularity> granularity, Node* startingNode, bool isKeyRepeat)
{
    if (!direction || !granularity)
        return false;

    Ref frame = m_frame.get();

    frame->protectedDocument()->updateLayoutIgnorePendingStylesheets();

    if (startKeyboardScrollAnimationOnEnclosingScrollableContainer(*direction, *granularity, startingNode, isKeyRepeat))
        return true;

    return startKeyboardScrollAnimationOnDocument(*direction, *granularity, isKeyRepeat);
}

void EventHandler::defaultArrowEventHandler(FocusDirection focusDirection, KeyboardEvent& event)
{
    ASSERT(event.type() == eventNames().keydownEvent);

    if (!m_frame->document()->settings().spatialNavigationEnabled()) {
        ScrollLogicalDirection direction;
        switch (focusDirection) {
        case FocusDirection::Down:
            direction = ScrollBlockDirectionForward;
            break;
        case FocusDirection::Right:
            direction = ScrollInlineDirectionForward;
            break;
        case FocusDirection::Up:
            direction = ScrollBlockDirectionBackward;
            break;
        case FocusDirection::Left:
            direction = ScrollInlineDirectionBackward;
            break;
        case FocusDirection::None:
        case FocusDirection::Backward:
        case FocusDirection::Forward:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        if (defaultKeyboardScrollEventHandler(event, direction, ScrollGranularity::Line))
            event.setDefaultHandled();
        return;
    }

    if (event.ctrlKey() || event.metaKey() || event.shiftKey())
        return;

    RefPtr page = m_frame->page();
    if (!page)
        return;

    // Arrows and other possible directional navigation keys can be used in design
    // mode editing.
    if (m_frame->document()->inDesignMode())
        return;

    if (page->focusController().advanceFocus(focusDirection, &event))
        event.setDefaultHandled();
}

void EventHandler::defaultTabEventHandler(KeyboardEvent& event)
{
    Ref frame = m_frame.get();

    ASSERT(event.type() == eventNames().keydownEvent);

    // We should only advance focus on tabs if no special modifier keys are held down.
    if (event.ctrlKey() || event.metaKey())
        return;

    RefPtr page = frame->page();
    if (!page)
        return;

    // Tabs can be used in design mode editing.
    if (frame->document()->inDesignMode())
        return;

    if (!page->tabKeyCyclesThroughElements())
        return;

    if (page->focusController().advanceFocus(event.shiftKey() ? FocusDirection::Backward : FocusDirection::Forward, &event))
        event.setDefaultHandled();
}

void EventHandler::scheduleScrollEvent()
{
    Ref frame = m_frame.get();
    setFrameWasScrolledByUser();
    if (!frame->view())
        return;
    if (RefPtr document = frame->document())
        document->addPendingScrollEventTarget(*document);
}

void EventHandler::setFrameWasScrolledByUser()
{
    if (RefPtr view = m_frame->view())
        view->setLastUserScrollType(LocalFrameView::UserScrollType::Explicit);
}

bool EventHandler::passMousePressEventToScrollbar(MouseEventWithHitTestResults& mouseEventAndResult, Scrollbar* scrollbar)
{
    if (!scrollbar || !scrollbar->enabled())
        return false;
    setFrameWasScrolledByUser();
    return scrollbar->mouseDown(mouseEventAndResult.event());
}

// If scrollbar (under mouse) is different from last, send a mouse exited.
void EventHandler::updateLastScrollbarUnderMouse(Scrollbar* scrollbar, SetOrClearLastScrollbar setOrClear)
{
    if (m_lastScrollbarUnderMouse != scrollbar) {
        // Send mouse exited to the old scrollbar.
        if (m_lastScrollbarUnderMouse)
            m_lastScrollbarUnderMouse->mouseExited();

        // Send mouse entered if we're setting a new scrollbar.
        if (scrollbar && setOrClear == SetOrClearLastScrollbar::Set) {
            scrollbar->mouseEntered();
            m_lastScrollbarUnderMouse = *scrollbar;
        } else
            m_lastScrollbarUnderMouse = nullptr;
    }
}

#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
static const AtomString& eventNameForTouchPointState(PlatformTouchPoint::State state)
{
    switch (state) {
    case PlatformTouchPoint::TouchReleased:
        return eventNames().touchendEvent;
    case PlatformTouchPoint::TouchCancelled:
        return eventNames().touchcancelEvent;
    case PlatformTouchPoint::TouchPressed:
        return eventNames().touchstartEvent;
    case PlatformTouchPoint::TouchMoved:
        return eventNames().touchmoveEvent;
    case PlatformTouchPoint::TouchStationary:
        // TouchStationary state is not converted to touch events, so fall through to assert.
    default:
        ASSERT_NOT_REACHED();
        return emptyAtom();
    }
}

static HitTestResult hitTestResultInFrame(LocalFrame* frame, const LayoutPoint& point, OptionSet<HitTestRequest::Type> hitType)
{
    HitTestResult result(point);

    if (!frame || !frame->contentRenderer())
        return result;

    if (frame->view()) {
        IntRect rect = frame->view()->visibleContentRect();
        if (!rect.contains(roundedIntPoint(point)))
            return result;
    }
    frame->protectedDocument()->hitTest(hitType, result);
    return result;
}

Expected<bool, RemoteFrameGeometryTransformer> EventHandler::handleTouchEvent(const PlatformTouchEvent& event)
{
    Ref frame = m_frame.get();

    // First build up the lists to use for the 'touches', 'targetTouches' and 'changedTouches' attributes
    // in the JS event. See https://www.sitepen.com/blog/touching-and-gesturing-on-the-iphone/
    // for an overview of how these lists fit together.

    // Holds the complete set of touches on the screen and will be used as the 'touches' list in the JS event.
    RefPtr<TouchList> touches = TouchList::create();

    // A different view on the 'touches' list above, filtered and grouped by event target. Used for the
    // 'targetTouches' list in the JS event.
    typedef HashMap<EventTarget*, RefPtr<TouchList>> TargetTouchesMap;
    TargetTouchesMap touchesByTarget;

    // Array of touches per state, used to assemble the 'changedTouches' list in the JS event.
    typedef HashSet<RefPtr<EventTarget>> EventTargetSet;
    struct Touches {
        // The touches corresponding to the particular change state this struct instance represents.
        RefPtr<TouchList> m_touches;
        // Set of targets involved in m_touches.
        EventTargetSet m_targets;
    };
    std::array<Touches, PlatformTouchPoint::TouchStateEnd> changedTouches;

    const Vector<PlatformTouchPoint>& points = event.touchPoints();
    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, frame->protectedDocument().get(), userGestureTypeForPlatformEvent(event), UserGestureIndicator::ProcessInteractionStyle::Immediate, event.authorizationToken());

    bool freshTouchEvents = true;
    bool allTouchReleased = true;
    for (auto& point : points) {
        if (point.state() != PlatformTouchPoint::TouchPressed)
            freshTouchEvents = false;
        if (point.state() != PlatformTouchPoint::TouchReleased && point.state() != PlatformTouchPoint::TouchCancelled)
            allTouchReleased = false;
    }

    for (unsigned index = 0; index < points.size(); index++) {
        auto& point = points[index];
        PlatformTouchPoint::State pointState = point.state();
        LayoutPoint pagePoint = documentPointForWindowPoint(frame, point.pos());

        OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::TouchEvent };
        // The HitTestRequest types used for mouse events map quite adequately
        // to touch events. Note that in addition to meaning that the hit test
        // should affect the active state of the current node if necessary,
        // HitTestRequest::Type::Active signifies that the hit test is taking place
        // with the mouse (or finger in this case) being pressed.
        switch (pointState) {
        case PlatformTouchPoint::TouchPressed:
            hitType.add(HitTestRequest::Type::Active);
            break;
        case PlatformTouchPoint::TouchMoved:
            hitType.add({ HitTestRequest::Type::Active, HitTestRequest::Type::Move, HitTestRequest::Type::ReadOnly });
            break;
        case PlatformTouchPoint::TouchReleased:
        case PlatformTouchPoint::TouchCancelled:
            hitType.add(HitTestRequest::Type::Release);
            break;
        case PlatformTouchPoint::TouchStationary:
            hitType.add({ HitTestRequest::Type::Active, HitTestRequest::Type::ReadOnly });
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }

        if (shouldGesturesTriggerActive())
            hitType.add(HitTestRequest::Type::ReadOnly);

        // Increment the platform touch id by 1 to avoid storing a key of 0 in the hashmap.
        unsigned touchPointTargetKey = point.id() + 1;
#if PLATFORM(WPE)
        bool pointerCancelled = false;
#endif
        RefPtr<EventTarget> touchTarget;
        RefPtr<EventTarget> pointerTarget;
        if (pointState == PlatformTouchPoint::TouchPressed) {
            HitTestResult result;
            if (freshTouchEvents) {
                result = hitTestResultAtPoint(pagePoint, hitType | HitTestRequest::Type::AllowChildFrameContent);
                m_originatingTouchPointTargetKey = touchPointTargetKey;
            } else if (m_originatingTouchPointDocument && m_originatingTouchPointDocument->frame()) {
                Ref frame = *m_originatingTouchPointDocument->frame();
                LayoutPoint pagePointInOriginatingDocument = documentPointForWindowPoint(frame, point.pos());
                result = hitTestResultInFrame(frame.ptr(), pagePointInOriginatingDocument, hitType);
                if (!result.innerNode())
                    continue;
            } else
                continue;

            RefPtr element = result.targetElement();
            ASSERT(element);

            if (element && InspectorInstrumentation::handleTouchEvent(frame, *element))
                return true;

            Ref doc = element->document();
            // Record the originating touch document even if it does not have a touch listener.
            if (freshTouchEvents) {
                m_originatingTouchPointDocument = doc.ptr();
                freshTouchEvents = false;
            }
            if (!doc->hasTouchEventHandlers())
                continue;
            m_originatingTouchPointTargets.set(touchPointTargetKey, element);
            touchTarget = element;
            pointerTarget = element;
        } else if (pointState == PlatformTouchPoint::TouchReleased || pointState == PlatformTouchPoint::TouchCancelled) {
            // No need to perform a hit-test since we only need to unset :hover and :active states.
            if (!shouldGesturesTriggerActive() && allTouchReleased)
                frame->protectedDocument()->updateHoverActiveState(hitType, 0);
            if (touchPointTargetKey == m_originatingTouchPointTargetKey)
                m_originatingTouchPointTargetKey = 0;

            // The target should be the original target for this touch, so get it from the hashmap. As it's a release or cancel
            // we also remove it from the map.
            touchTarget = m_originatingTouchPointTargets.take(touchPointTargetKey);

#if PLATFORM(WPE)
            HitTestResult result = hitTestResultAtPoint(pagePoint, hitType | HitTestRequest::Type::AllowChildFrameContent);
            pointerTarget = result.targetElement();
            pointerCancelled = (pointerTarget != touchTarget);
#endif
        } else {
            // No hittest is performed on move or stationary, since the target is not allowed to change anyway.
            touchTarget = m_originatingTouchPointTargets.get(touchPointTargetKey);

            HitTestResult result = hitTestResultAtPoint(pagePoint, hitType | HitTestRequest::Type::AllowChildFrameContent);
            pointerTarget = result.targetElement();
        }

        RefPtr touchTargetNode = dynamicDowncast<Node>(touchTarget);
        if (!touchTargetNode)
            continue;
        Ref document = touchTargetNode->document();
        if (!document->hasTouchEventHandlers())
            continue;
        RefPtr targetFrame = document->frame();
        if (!targetFrame)
            continue;

#if PLATFORM(WPE)
        // FIXME: WPE currently does not send touch stationary events, so create a naive TouchReleased PlatformTouchPoint
        // on release if the hit test result changed since the previous TouchPressed or TouchMoved
        if (pointState == PlatformTouchPoint::TouchReleased && pointerCancelled) {
            PlatformTouchEvent cancelEvent = event;
            Vector<PlatformTouchPoint> cancelEventPoints = event.touchPoints();
            cancelEventPoints.at(index) = PlatformTouchPoint(
                point.id(), PlatformTouchPoint::State::TouchCancelled, point.screenPos(), point.pos());
            cancelEvent.setTouchPoints(cancelEventPoints);
            document->protectedPage()->pointerCaptureController().dispatchEventForTouchAtIndex(
                *touchTarget, cancelEvent, index, !index, *document->windowProxy(), { 0, 0 });
        }

        // FIXME: Pass the touch delta for pointermove events by remembering the position per pointerID similar to
        // Apple's m_touchLastGlobalPositionAndDeltaMap
        document->protectedPage()->pointerCaptureController().dispatchEventForTouchAtIndex(
            *pointerTarget, event, index, !index, *document->windowProxy(), { 0, 0 });
#endif

        if (frame.ptr() != targetFrame) {
            // pagePoint should always be relative to the target elements containing frame.
            pagePoint = documentPointForWindowPoint(*targetFrame, point.pos());
        }

        float scaleFactor = targetFrame->pageZoomFactor() * targetFrame->frameScaleFactor();

        int adjustedPageX = lroundf(pagePoint.x() / scaleFactor);
        int adjustedPageY = lroundf(pagePoint.y() / scaleFactor);

        auto touch = Touch::create(targetFrame.get(), touchTarget.get(), point.id(),
            point.screenPos().x(), point.screenPos().y(), adjustedPageX, adjustedPageY,
            point.radiusX(), point.radiusY(), point.rotationAngle(), point.force());

        // Ensure this target's touch list exists, even if it ends up empty, so it can always be passed to TouchEvent::Create below.
        TargetTouchesMap::iterator targetTouchesIterator = touchesByTarget.find(touchTarget.get());
        if (targetTouchesIterator == touchesByTarget.end())
            targetTouchesIterator = touchesByTarget.set(touchTarget.get(), TouchList::create()).iterator;

        // touches and targetTouches should only contain information about touches still on the screen, so if this point is
        // released or cancelled it will only appear in the changedTouches list.
        if (pointState != PlatformTouchPoint::TouchReleased && pointState != PlatformTouchPoint::TouchCancelled) {
            touches->append(touch.copyRef());
            targetTouchesIterator->value->append(touch.copyRef());
        }

        // Now build up the correct list for changedTouches.
        // Note that  any touches that are in the TouchStationary state (e.g. if
        // the user had several points touched but did not move them all) should
        // never be in the changedTouches list so we do not handle them explicitly here.
        // See https://bugs.webkit.org/show_bug.cgi?id=37609 for further discussion
        // about the TouchStationary state.
        if (pointState != PlatformTouchPoint::TouchStationary) {
            ASSERT(pointState < PlatformTouchPoint::TouchStateEnd);
            if (!changedTouches[pointState].m_touches)
                changedTouches[pointState].m_touches = TouchList::create();
            changedTouches[pointState].m_touches->append(WTFMove(touch));
            changedTouches[pointState].m_targets.add(touchTarget);
        }
    }
    m_touchPressed = touches->length() > 0;
    if (allTouchReleased)
        m_originatingTouchPointDocument = nullptr;

    // Now iterate the changedTouches list and m_targets within it, sending events to the targets as required.
    bool swallowedEvent = false;
    RefPtr<TouchList> emptyList = TouchList::create();
    for (unsigned state = 0; state != PlatformTouchPoint::TouchStateEnd; ++state) {
        if (!changedTouches[state].m_touches)
            continue;

        // When sending a touch cancel event, use empty touches and targetTouches lists.
        bool isTouchCancelEvent = (state == PlatformTouchPoint::TouchCancelled);
        RefPtr<TouchList>& effectiveTouches(isTouchCancelEvent ? emptyList : touches);
        const AtomString& stateName(eventNameForTouchPointState(static_cast<PlatformTouchPoint::State>(state)));

        for (auto& target : changedTouches[state].m_targets) {
            ASSERT(is<Node>(target));

            RefPtr<TouchList> targetTouches(isTouchCancelEvent ? emptyList : touchesByTarget.get(target.get()));
            ASSERT(targetTouches);

            Ref<TouchEvent> touchEvent = TouchEvent::create(effectiveTouches.get(), targetTouches.get(), changedTouches[state].m_touches.get(),
                stateName, downcast<Node>(*target).document().windowProxy(), { }, event.modifiers());
            target->dispatchEvent(touchEvent);
            swallowedEvent = swallowedEvent || touchEvent->defaultPrevented() || touchEvent->defaultHandled();
        }
    }

    return swallowedEvent;
}
#endif // ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)

#if ENABLE(TOUCH_EVENTS)
bool EventHandler::dispatchSyntheticTouchEventIfEnabled(const PlatformMouseEvent& platformMouseEvent)
{
#if ENABLE(IOS_TOUCH_EVENTS)
    UNUSED_PARAM(platformMouseEvent);
    return false;
#else
    if (!m_frame->settings().isTouchEventEmulationEnabled())
        return false;

    PlatformEvent::Type eventType = platformMouseEvent.type();
    if (eventType != PlatformEvent::Type::MouseMoved && eventType != PlatformEvent::Type::MousePressed && eventType != PlatformEvent::Type::MouseReleased)
        return false;

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent };
    MouseEventWithHitTestResults mouseEvent = prepareMouseEvent(hitType, platformMouseEvent);
    if (mouseEvent.scrollbar() || subframeForHitTestResult(mouseEvent))
        return false;

    // The order is important. This check should follow the subframe test: http://webkit.org/b/111292.
    if (eventType == PlatformEvent::Type::MouseMoved && !m_touchPressed)
        return true;

    SyntheticSingleTouchEvent touchEvent(platformMouseEvent);
    return handleTouchEvent(touchEvent).value_or(false);
#endif
}
#endif // ENABLE(TOUCH_EVENTS)

void EventHandler::setLastKnownMousePosition(IntPoint position, IntPoint globalPosition)
{
    m_lastKnownMousePosition = position;
    m_lastKnownMouseGlobalPosition = globalPosition;
}

void EventHandler::setImmediateActionStage(ImmediateActionStage stage)
{
    m_immediateActionStage = stage;
}

#if !PLATFORM(COCOA)
OptionSet<PlatformEvent::Modifier> EventHandler::accessKeyModifiers()
{
    return PlatformEvent::Modifier::AltKey;
}

HandleUserInputEventResult EventHandler::passMousePressEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe)
{
    subframe.eventHandler().handleMousePressEvent(mouseEventAndResult.event());
    return true;
}

HandleUserInputEventResult EventHandler::passMouseReleaseEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe)
{
    subframe.eventHandler().handleMouseReleaseEvent(mouseEventAndResult.event());
    return true;
}

bool EventHandler::passWheelEventToWidget(const PlatformWheelEvent& event, Widget& widget, OptionSet<WheelEventProcessingSteps> processingSteps)
{
    RefPtr frameView = dynamicDowncast<LocalFrameView>(widget);
    if (!frameView)
        return false;

    auto [result, _] = frameView->frame().eventHandler().handleWheelEvent(event, processingSteps);
    return result.wasHandled();
}

bool EventHandler::passWidgetMouseDownEventToWidget(RenderWidget* renderWidget)
{
    return passMouseDownEventToWidget(renderWidget->widget());
}

bool EventHandler::passWidgetMouseDownEventToWidget(const MouseEventWithHitTestResults& event)
{
    auto* target = event.targetNode() ? event.targetNode()->renderer() : nullptr;
    auto* renderWidget = dynamicDowncast<RenderWidget>(target);
    if (!renderWidget)
        return false;
    return passMouseDownEventToWidget(renderWidget->widget());
}

bool EventHandler::passMouseDownEventToWidget(Widget*)
{
    notImplemented();
    return false;
}

void EventHandler::focusDocumentView()
{
    if (RefPtr page = m_frame->page())
        page->focusController().setFocusedFrame(protectedFrame().ptr());
}
#endif // !PLATFORM(COCOA)

void EventHandler::resetCapturingMouseEventsElement()
{
    m_capturingMouseEventsElement = nullptr;
    m_isCapturingRootElementForMouseEvents = false;
}

Ref<LocalFrame> EventHandler::protectedFrame() const
{
    return m_frame.get();
}

#if !PLATFORM(COCOA) && !PLATFORM(WIN)
bool EventHandler::eventActivatedView(const PlatformMouseEvent&) const
{
    notImplemented();
    return false;
}

HandleUserInputEventResult EventHandler::passMouseMoveEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe, HitTestResult* result)
{
    subframe.eventHandler().handleMouseMoveEvent(mouseEventAndResult.event(), result);
    return true;
}
#endif // !PLATFORM(COCOA) && !PLATFORM(WIN)

} // namespace WebCore
