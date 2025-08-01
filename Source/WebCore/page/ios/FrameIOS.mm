/*
 * Copyright (C) 2006-2023 Apple Inc. All rights reserved.
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

#import "config.h"
#import "LocalFrame.h"

#if PLATFORM(IOS_FAMILY)

#import "CSSStyleProperties.h"
#import "CommonVM.h"
#import "ComposedTreeIterator.h"
#import "ContainerNodeInlines.h"
#import "Document.h"
#import "DocumentInlines.h"
#import "DocumentMarkerController.h"
#import "Editor.h"
#import "EditorClient.h"
#import "ElementRareData.h"
#import "EventHandler.h"
#import "EventNames.h"
#import "EventTargetInlines.h"
#import "FormController.h"
#import "FrameSelection.h"
#import "HTMLAreaElement.h"
#import "HTMLBodyElement.h"
#import "HTMLDocument.h"
#import "HTMLHtmlElement.h"
#import "HTMLNames.h"
#import "HTMLObjectElement.h"
#import "HitTestRequest.h"
#import "HitTestResult.h"
#import "LocalDOMWindow.h"
#import "LocalFrameView.h"
#import "Logging.h"
#import "NodeInlines.h"
#import "NodeRenderStyle.h"
#import "NodeTraversal.h"
#import "Page.h"
#import "PageTransitionEvent.h"
#import "PlatformScreen.h"
#import "Range.h"
#import "RenderLayer.h"
#import "RenderLayerCompositor.h"
#import "RenderLayerScrollableArea.h"
#import "RenderObjectInlines.h"
#import "RenderTextControl.h"
#import "RenderView.h"
#import "RenderedDocumentMarker.h"
#import "ShadowRoot.h"
#import "TextBoundaries.h"
#import "TextIterator.h"
#import "VisiblePosition.h"
#import "VisibleUnits.h"
#import "WAKWindow.h"
#import <JavaScriptCore/JSLock.h>
#import <wtf/BlockObjCExceptions.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/text/TextStream.h>

using namespace WebCore::HTMLNames;
using namespace WTF::Unicode;

using JSC::JSLockHolder;

namespace WebCore {

// Create <html><body (style="...")></body></html> doing minimal amount of work.
void LocalFrame::initWithSimpleHTMLDocument(const AtomString& style, const URL& url)
{
    m_loader->initForSynthesizedDocument(url);

    auto document = HTMLDocument::createSynthesizedDocument(*this, url);
    document->setCompatibilityMode(DocumentCompatibilityMode::LimitedQuirksMode);
    document->createDOMWindow();
    setDocument(document.copyRef());

    auto rootElement = HTMLHtmlElement::create(document);

    auto body = HTMLBodyElement::create(document);
    if (!style.isEmpty())
        body->setAttribute(HTMLNames::styleAttr, style);

    rootElement->appendChild(body);
    document->appendChild(rootElement);
}

const ViewportArguments& LocalFrame::viewportArguments() const
{
    return m_viewportArguments;
}

void LocalFrame::setViewportArguments(const ViewportArguments& arguments)
{
    m_viewportArguments.get() = arguments;
}

NSArray *LocalFrame::wordsInCurrentParagraph() const
{
    protectedDocument()->updateLayout();

    if (!page() || !page()->selection().isCaret())
        return nil;

    VisiblePosition position(page()->selection().start(), page()->selection().affinity());
    VisiblePosition end(position);

    if (!isStartOfParagraph(end)) {
        VisiblePosition previous = end.previous();
        char16_t c(previous.characterAfter());
        // FIXME: Should use something from ICU or ASCIICType that is not subject to POSIX current language rather than iswpunct.
        if (!iswpunct(c) && !deprecatedIsSpaceOrNewline(c) && c != noBreakSpace)
            end = startOfWord(end);
    }
    VisiblePosition start(startOfParagraph(end));

    auto searchRange = makeSimpleRange(start, end);
    if (!searchRange || searchRange->collapsed())
        return nil;

    NSMutableArray *words = [NSMutableArray array];

    WordAwareIterator it(*searchRange);
    while (!it.atEnd()) {
        StringView text = it.text();
        int length = text.length();
        if (length > 1 || !deprecatedIsSpaceOrNewline(text[0])) {
            int startOfWordBoundary = 0;
            for (int i = 1; i < length; i++) {
                if (deprecatedIsSpaceOrNewline(text[i]) || text[i] == noBreakSpace) {
                    int wordLength = i - startOfWordBoundary;
                    if (wordLength > 0) {
                        RetainPtr<NSString> chunk = text.substring(startOfWordBoundary, wordLength).createNSString();
                        [words addObject:chunk.get()];
                    }
                    startOfWordBoundary += wordLength + 1;
                }
            }
            if (startOfWordBoundary < length) {
                RetainPtr<NSString> chunk = text.substring(startOfWordBoundary, length - startOfWordBoundary).createNSString();
                [words addObject:chunk.get()];
            }
        }
        it.advance();
    }

    if ([words count] > 0 && isEndOfParagraph(position) && !isStartOfParagraph(position)) {
        VisiblePosition previous = position.previous();
        char16_t c(previous.characterAfter());
        if (!deprecatedIsSpaceOrNewline(c) && c != noBreakSpace)
            [words removeLastObject];
    }

    return words;
}

#define CHECK_FONT_SIZE 0
#define RECT_LOGGING 0

CGRect LocalFrame::renderRectForPoint(CGPoint point, bool* isReplaced, float* fontSize) const
{
    *isReplaced = false;
    *fontSize = 0;

    if (!m_doc || !m_doc->renderBox())
        return CGRectZero;

    // FIXME: Why this layer check?
    RenderLayer* layer = m_doc->renderBox()->layer();
    if (!layer)
        return CGRectZero;

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowChildFrameContent };
    auto result = eventHandler().hitTestResultAtPoint(IntPoint(roundf(point.x), roundf(point.y)), hitType);

    Node* node = result.innerNode();
    if (!node)
        return CGRectZero;

    RenderObject* hitRenderer = node->renderer();
    RenderObject* renderer = hitRenderer;
#if RECT_LOGGING
    printf("\n%f %f\n", point.x, point.y);
#endif
    while (renderer && !renderer->isBody() && !renderer->isDocumentElementRenderer()) {
#if RECT_LOGGING
        CGRect rect = renderer->absoluteBoundingBoxRect(true);
        if (renderer->node()) {
            const char *nodeName = renderer->node()->nodeName().ascii().data();
            printf("%s %f %f %f %f\n", nodeName, rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
        }
#endif
        if (renderer->isRenderBlock() || renderer->isNonReplacedAtomicInlineLevelBox() || renderer->isBlockLevelReplacedOrAtomicInline()) {
            *isReplaced = renderer->isBlockLevelReplacedOrAtomicInline();
#if CHECK_FONT_SIZE
            for (RenderObject* textRenderer = hitRenderer; textRenderer; textRenderer = textRenderer->traverseNext(hitRenderer)) {
                if (textRenderer->isText()) {
                    *fontSize = textRenderer->font(true).size();
                    break;
                }
            }
#endif
            IntRect targetRect = renderer->absoluteBoundingBoxRect(true);
            for (Widget* currView = &(renderer->view().frameView()); currView && currView != view(); currView = currView->parent())
                targetRect = currView->convertToContainingView(targetRect);

            return targetRect;
        }
        renderer = renderer->parent();
    }

    return CGRectZero;
}

void LocalFrame::betterApproximateNode(const IntPoint& testPoint, const NodeQualifier& nodeQualifierFunction, Node*& best, Node* failedNode, IntPoint& bestPoint, IntRect& bestRect, const IntRect& testRect)
{
    IntRect candidateRect;
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    auto* candidate = nodeQualifierFunction(eventHandler().hitTestResultAtPoint(testPoint, hitType), failedNode, &candidateRect);

    // Bail if we have no candidate, or the candidate is already equal to our current best node,
    // or our candidate is the avoidedNode and there is a current best node.
    if (!candidate || candidate == best)
        return;

    // The document should never be considered the best alternative.
    if (candidate->isDocumentNode())
        return;

    if (best) {
        IntRect bestIntersect = intersection(testRect, bestRect);
        IntRect candidateIntersect = intersection(testRect, candidateRect);
        // if the candidate intersection is smaller than the current best intersection, bail.
        if (candidateIntersect.width() * candidateIntersect.height() <= bestIntersect.width() * bestIntersect.height())
            return;
    }

    // At this point we either don't have a previous best, or our current candidate has a better intersection.
    best = candidate;
    bestPoint = testPoint;
    bestRect = candidateRect;
}

bool LocalFrame::hitTestResultAtViewportLocation(const FloatPoint& viewportLocation, HitTestResult& hitTestResult, IntPoint& center)
{
    if (!m_doc || !m_doc->renderView())
        return false;

    auto* view = m_view.get();
    if (!view)
        return false;

    center = view->windowToContents(roundedIntPoint(viewportLocation));
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowVisibleChildFrameContentOnly };
    hitTestResult = eventHandler().hitTestResultAtPoint(center, hitType);
    return true;
}

Node* LocalFrame::qualifyingNodeAtViewportLocation(const FloatPoint& viewportLocation, FloatPoint& adjustedViewportLocation, const NodeQualifier& nodeQualifierFunction, ShouldApproximate shouldApproximate, ShouldFindRootEditableElement shouldFindRootEditableElement)
{
    adjustedViewportLocation = viewportLocation;

    IntPoint testCenter;
    HitTestResult candidateInfo;
    if (!hitTestResultAtViewportLocation(viewportLocation, candidateInfo, testCenter))
        return nullptr;

    IntPoint bestPoint = testCenter;

    // We have the candidate node at the location, check whether it or one of its ancestors passes
    // the qualifier function, which typically checks if the node responds to a particular event type.
    Node* approximateNode = nodeQualifierFunction(candidateInfo, 0, 0);

    if (shouldFindRootEditableElement == ShouldFindRootEditableElement::Yes && approximateNode && approximateNode->isContentEditable()) {
        // If we are in editable content, we look for the root editable element.
        approximateNode = approximateNode->rootEditableElement();
        // If we have a focusable node, there is no need to approximate.
        if (approximateNode)
            shouldApproximate = ShouldApproximate::No;
    }

    float scale = page() ? page()->pageScaleFactor() : 1;
    float ppiFactor = screenPPIFactor();

    static const float unscaledSearchRadius = 15;
    int searchRadius = static_cast<int>(unscaledSearchRadius * ppiFactor / scale);

    if (approximateNode && shouldApproximate == ShouldApproximate::Yes) {
        constexpr std::array testOffsets {
            -.3f, -.3f,
            -.6f, -.6f,
            +.3f, +.3f,
            -.9f, -.9f,
        };

        Node* originalApproximateNode = approximateNode;
        for (unsigned n = 0; n < std::size(testOffsets); n += 2) {
            IntSize testOffset(testOffsets[n] * searchRadius, testOffsets[n + 1] * searchRadius);
            IntPoint testPoint = testCenter + testOffset;

            constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowChildFrameContent };
            auto candidateInfo = eventHandler().hitTestResultAtPoint(testPoint, hitType);
            Node* candidateNode = nodeQualifierFunction(candidateInfo, 0, 0);
            if (candidateNode && candidateNode->isDescendantOf(originalApproximateNode)) {
                approximateNode = candidateNode;
                bestPoint = testPoint;
                break;
            }
        }
    } else if (!approximateNode && shouldApproximate == ShouldApproximate::Yes) {
        // Grab the closest parent element of our failed candidate node.
        Node* candidate = candidateInfo.innerNode();
        Node* failedNode = candidate;

        while (candidate && !candidate->isElementNode())
            candidate = candidate->parentInComposedTree();

        if (candidate)
            failedNode = candidate;

        // The center point was tested earlier.
        constexpr std::array testOffsets {
            -.3f, -.3f,
            +.3f, -.3f,
            -.3f, +.3f,
            +.3f, +.3f,
            -.6f, -.6f,
            +.6f, -.6f,
            -.6f, +.6f,
            +.6f, +.6f,
            -1.f, 0.f,
            +1.f, 0.f,
            0.f, +1.f,
            0.f, -1.f,
        };
        IntRect bestFrame;
        IntRect testRect(testCenter, IntSize());
        testRect.inflate(searchRadius);
        int currentTestRadius = 0;
        for (unsigned n = 0; n < std::size(testOffsets); n += 2) {
            IntSize testOffset(testOffsets[n] * searchRadius, testOffsets[n + 1] * searchRadius);
            IntPoint testPoint = testCenter + testOffset;
            int testRadius = std::max(std::abs(testOffset.width()), std::abs(testOffset.height()));
            if (testRadius > currentTestRadius) {
                // Bail out with the best match within a radius
                currentTestRadius = testRadius;
                if (approximateNode)
                    break;
            }
            betterApproximateNode(testPoint, nodeQualifierFunction, approximateNode, failedNode, bestPoint, bestFrame, testRect);
        }
    }

    if (approximateNode) {
        IntPoint p = m_view->contentsToWindow(bestPoint);
        adjustedViewportLocation = p;
        if (shouldFindRootEditableElement == ShouldFindRootEditableElement::Yes && approximateNode->isContentEditable()) {
            // When in editable content, look for the root editable node again,
            // since this could be the node found with the approximation.
            approximateNode = approximateNode->rootEditableElement();
        }
    }

    return approximateNode;
}

Node* LocalFrame::deepestNodeAtLocation(const FloatPoint& viewportLocation)
{
    IntPoint center;
    HitTestResult hitTestResult;
    if (!hitTestResultAtViewportLocation(viewportLocation, hitTestResult, center))
        return nullptr;

    return hitTestResult.innerNode();
}

static bool nodeIsMouseFocusable(Node& node)
{
    auto* element = dynamicDowncast<Element>(node);
    if (!element)
        return false;

    if (element->isMouseFocusable())
        return true;

    if (RefPtr shadowRoot = element->shadowRoot()) {
        if (shadowRoot->delegatesFocus()) {
            for (auto& node : composedTreeDescendants(*element)) {
                if (auto* element = dynamicDowncast<Element>(node); element && element->isMouseFocusable())
                    return true;
            }
        }
    }

    return false;
}

static bool nodeWillRespondToMouseEvents(Node& node)
{
    return node.willRespondToMouseClickEvents() || node.willRespondToMouseMoveEvents() || nodeIsMouseFocusable(node);
}

Node* LocalFrame::approximateNodeAtViewportLocationLegacy(const FloatPoint& viewportLocation, FloatPoint& adjustedViewportLocation)
{
    // This function is only used for UIWebView.
    auto&& ancestorRespondingToClickEvents = [](const HitTestResult& hitTestResult, Node* terminationNode, IntRect* nodeBounds) -> Node* {
        bool bodyHasBeenReached = false;
        bool pointerCursorStillValid = true;

        if (nodeBounds)
            *nodeBounds = IntRect();

        auto node = hitTestResult.innerNode();
        if (!node)
            return nullptr;

        Node* pointerCursorNode = nullptr;
        for (; node && node != terminationNode; node = node->parentInComposedTree()) {
            // We only accept pointer nodes before reaching the body tag.
            if (node->hasTagName(HTMLNames::bodyTag)) {
                // Make sure we cover the case of an empty editable body.
                if (!pointerCursorNode && node->isContentEditable())
                    pointerCursorNode = node;
                bodyHasBeenReached = true;
                pointerCursorStillValid = false;
            }

            // If we already have a pointer, and we reach a table, don't accept it.
            if (pointerCursorNode && (node->hasTagName(HTMLNames::tableTag) || node->hasTagName(HTMLNames::tbodyTag)))
                pointerCursorStillValid = false;

            // If we haven't reached the body, and we are still paying attention to pointer cursors, and the node has a pointer cursor.
            if (pointerCursorStillValid && node->renderStyle() && node->renderStyle()->cursorType() == CursorType::Pointer)
                pointerCursorNode = node;
            else if (pointerCursorNode) {
                // We want the lowest unbroken chain of pointer cursors.
                pointerCursorStillValid = false;
            }

            if (nodeWillRespondToMouseEvents(*node)) {
                // If we're at the body or higher, use the pointer cursor node (which may be null).
                if (bodyHasBeenReached)
                    node = pointerCursorNode;

                // If we are interested about the frame, use it.
                if (nodeBounds) {
                    // This is a check to see whether this node is an area element. The only way this can happen is if this is the first check.
                    if (node == hitTestResult.innerNode() && node != hitTestResult.innerNonSharedNode() && is<HTMLAreaElement>(*node))
                        *nodeBounds = snappedIntRect(downcast<HTMLAreaElement>(*node).computeRect(hitTestResult.innerNonSharedNode()->renderer()));
                    else if (node && node->renderer())
                        *nodeBounds = node->renderer()->absoluteBoundingBoxRect(true);
                }

                return node;
            }
        }

        return nullptr;
    };

    return qualifyingNodeAtViewportLocation(viewportLocation, adjustedViewportLocation, WTFMove(ancestorRespondingToClickEvents), ShouldApproximate::Yes);
}

static inline NodeQualifier ancestorRespondingToClickEventsNodeQualifier(SecurityOrigin* securityOrigin = nullptr)
{
    return [securityOrigin](const HitTestResult& hitTestResult, Node* terminationNode, IntRect* nodeBounds) -> Node* {
        if (nodeBounds)
            *nodeBounds = IntRect();

        auto node = hitTestResult.innerNode();
        if (!node || (securityOrigin && !securityOrigin->isSameOriginAs(node->protectedDocument()->protectedSecurityOrigin())))
            return nullptr;

        for (; node && node != terminationNode; node = node->parentInComposedTree()) {
            if (nodeWillRespondToMouseEvents(*node)) {
                // If we are interested about the frame, use it.
                if (nodeBounds) {
                    // This is a check to see whether this node is an area element. The only way this can happen is if this is the first check.
                    if (node == hitTestResult.innerNode() && node != hitTestResult.innerNonSharedNode() && is<HTMLAreaElement>(*node))
                        *nodeBounds = snappedIntRect(downcast<HTMLAreaElement>(*node).computeRect(hitTestResult.innerNonSharedNode()->renderer()));
                    else if (node && node->renderer())
                        *nodeBounds = node->renderer()->absoluteBoundingBoxRect(true);
                }

                return node;
            }
        }

        return nullptr;
    };
}

Node* LocalFrame::nodeRespondingToClickEvents(const FloatPoint& viewportLocation, FloatPoint& adjustedViewportLocation, SecurityOrigin* securityOrigin)
{
    return qualifyingNodeAtViewportLocation(viewportLocation, adjustedViewportLocation, ancestorRespondingToClickEventsNodeQualifier(securityOrigin), ShouldApproximate::Yes);
}

Node* LocalFrame::nodeRespondingToDoubleClickEvent(const FloatPoint& viewportLocation, FloatPoint& adjustedViewportLocation)
{
    auto&& ancestorRespondingToDoubleClickEvent = [](const HitTestResult& hitTestResult, Node* terminationNode, IntRect* nodeBounds) -> Node* {
        if (nodeBounds)
            *nodeBounds = IntRect();

        auto* node = hitTestResult.innerNode();
        if (!node)
            return nullptr;

        for (; node && node != terminationNode; node = node->parentInComposedTree()) {
            if (!node->hasEventListeners(eventNames().dblclickEvent))
                continue;
#if ENABLE(TOUCH_EVENTS)
            if (!node->allowsDoubleTapGesture())
                continue;
#endif
            if (nodeBounds && node->renderer())
                *nodeBounds = node->renderer()->absoluteBoundingBoxRect(true);
            return node;
        }
        return nullptr;
    };

    return qualifyingNodeAtViewportLocation(viewportLocation, adjustedViewportLocation, WTFMove(ancestorRespondingToDoubleClickEvent), ShouldApproximate::Yes);
}

Node* LocalFrame::nodeRespondingToInteraction(const FloatPoint& viewportLocation, FloatPoint& adjustedViewportLocation)
{
    return qualifyingNodeAtViewportLocation(viewportLocation, adjustedViewportLocation, ancestorRespondingToClickEventsNodeQualifier(), ShouldApproximate::Yes, ShouldFindRootEditableElement::No);
}

Node* LocalFrame::nodeRespondingToScrollWheelEvents(const FloatPoint& viewportLocation)
{
    auto&& ancestorRespondingToScrollWheelEvents = [](const HitTestResult& hitTestResult, Node* terminationNode, IntRect* nodeBounds) -> Node* {
        if (nodeBounds)
            *nodeBounds = IntRect();

        Node* scrollingAncestor = nullptr;
        for (Node* node = hitTestResult.innerNode(); node && node != terminationNode && !node->hasTagName(HTMLNames::bodyTag); node = node->parentNode()) {
            RenderObject* renderer = node->renderer();
            if (!renderer)
                continue;

            if ((renderer->isRenderTextControlSingleLine() || renderer->isRenderTextControlMultiLine()) && downcast<RenderTextControl>(*renderer).canScroll()) {
                scrollingAncestor = node;
                continue;
            }

            auto& style = renderer->style();

            if (renderer->hasNonVisibleOverflow()
                && (style.overflowY() == Overflow::Auto || style.overflowY() == Overflow::Scroll
                || style.overflowX() == Overflow::Auto || style.overflowX() == Overflow::Scroll)) {
                scrollingAncestor = node;
            }
        }

        return scrollingAncestor;
    };

    FloatPoint adjustedViewportLocation;
    return qualifyingNodeAtViewportLocation(viewportLocation, adjustedViewportLocation, WTFMove(ancestorRespondingToScrollWheelEvents), ShouldApproximate::No);
}

int LocalFrame::preferredHeight() const
{
    RefPtr document = this->document();
    if (!document)
        return 0;

    document->updateLayout();

    auto* body = document->bodyOrFrameset();
    if (!body)
        return 0;

    auto* block = dynamicDowncast<RenderBlock>(body->renderer());
    if (!block)
        return 0;

    return block->height() + block->marginTop() + block->marginBottom();
}

void LocalFrame::updateLayout() const
{
    RefPtr document = this->document();
    if (!document)
        return;

    document->updateLayout();

    if (auto* view = this->view())
        view->adjustViewSize();
}

IntRect LocalFrame::caretRect()
{
    VisibleSelection visibleSelection = selection().selection();
    if (visibleSelection.isNone())
        return { };
    return visibleSelection.isCaret() ? selection().absoluteCaretBounds() : VisiblePosition(visibleSelection.end()).absoluteCaretBounds();
}

IntRect LocalFrame::rectForScrollToVisible()
{
    VisibleSelection selection(this->selection().selection());

    if (selection.isNone())
        return { };

    if (selection.isCaret())
        return caretRect();

    return unionRect(selection.visibleStart().absoluteCaretBounds(), selection.visibleEnd().absoluteCaretBounds());
}

void LocalFrame::setTimersPaused(bool paused)
{
    auto* page = this->page();
    if (!page)
        return;
    JSLockHolder lock(commonVM());
    if (paused)
        page->suspendActiveDOMObjectsAndAnimations();
    else
        page->resumeActiveDOMObjectsAndAnimations();
}

void LocalFrame::dispatchPageHideEventBeforePause()
{
    ASSERT(isMainFrame());
    if (!isMainFrame())
        return;

    Page::forEachDocumentFromMainFrame(*this, [](Document& document) {
        document.dispatchPagehideEvent(PageshowEventPersistence::Persisted);
    });
}

void LocalFrame::dispatchPageShowEventBeforeResume()
{
    ASSERT(isMainFrame());
    if (!isMainFrame())
        return;

    Page::forEachDocumentFromMainFrame(*this, [](Document& document) {
        document.dispatchPageshowEvent(PageshowEventPersistence::Persisted);
    });
}

void LocalFrame::setRangedSelectionBaseToCurrentSelection()
{
    m_rangedSelectionBase.get() = selection().selection();
}

void LocalFrame::setRangedSelectionBaseToCurrentSelectionStart()
{
    const VisibleSelection& visibleSelection = selection().selection();
    m_rangedSelectionBase.get() = VisibleSelection(visibleSelection.start(), visibleSelection.affinity());
}

void LocalFrame::setRangedSelectionBaseToCurrentSelectionEnd()
{
    const VisibleSelection& visibleSelection = selection().selection();
    m_rangedSelectionBase.get() = VisibleSelection(visibleSelection.end(), visibleSelection.affinity());
}

VisibleSelection LocalFrame::rangedSelectionBase() const
{
    return m_rangedSelectionBase.get();
}

void LocalFrame::clearRangedSelectionInitialExtent()
{
    m_rangedSelectionInitialExtent.get() = VisibleSelection();
}

void LocalFrame::setRangedSelectionInitialExtentToCurrentSelectionStart()
{
    const VisibleSelection& visibleSelection = selection().selection();
    m_rangedSelectionInitialExtent.get() = VisibleSelection(visibleSelection.start(), visibleSelection.affinity());
}

void LocalFrame::setRangedSelectionInitialExtentToCurrentSelectionEnd()
{
    const VisibleSelection& visibleSelection = selection().selection();
    m_rangedSelectionInitialExtent.get() = VisibleSelection(visibleSelection.end(), visibleSelection.affinity());
}

VisibleSelection LocalFrame::rangedSelectionInitialExtent() const
{
    return m_rangedSelectionInitialExtent.get();
}

void LocalFrame::recursiveSetUpdateAppearanceEnabled(bool enabled)
{
    selection().setUpdateAppearanceEnabled(enabled);
    for (RefPtr child = tree().firstChild(); child; child = child->tree().nextSibling()) {
        auto* localChild = dynamicDowncast<LocalFrame>(child.get());
        if (!localChild)
            continue;
        localChild->recursiveSetUpdateAppearanceEnabled(enabled);
    }
}

// FIXME: Break this function up into pieces with descriptive function names so that it's easier to follow.
NSArray *LocalFrame::interpretationsForCurrentRoot() const
{
    if (!document())
        return nil;

    auto* root = selection().isNone() ? document()->bodyOrFrameset() : selection().selection().rootEditableElement();
    auto rangeOfRootContents = makeRangeSelectingNodeContents(*root);

    auto markersInRoot = document()->markers().markersInRange(rangeOfRootContents, DocumentMarkerType::DictationPhraseWithAlternatives);

    // There are no phrases with alternatives, so there is just one interpretation.
    if (markersInRoot.isEmpty())
        return @[plainText(rangeOfRootContents).createNSString().get()];

    // The number of interpretations will be i1 * i2 * ... * iN, where iX is the number of interpretations for the Xth phrase with alternatives.
    size_t interpretationsCount = 1;

    for (auto& marker : markersInRoot)
        interpretationsCount *= std::get<Vector<String>>(marker->data()).size() + 1;

    Vector<Vector<char16_t>> interpretations;
    interpretations.grow(interpretationsCount);

    Position precedingTextStartPosition = makeDeprecatedLegacyPosition(root, 0);

    unsigned combinationsSoFar = 1;

    for (auto& node : intersectingNodes(rangeOfRootContents)) {
        for (auto& marker : document()->markers().markersFor(node, DocumentMarkerType::DictationPhraseWithAlternatives)) {
            auto& alternatives = std::get<Vector<String>>(marker->data());

            auto rangeForMarker = makeSimpleRange(node, *marker);

            if (auto precedingTextRange = makeSimpleRange(precedingTextStartPosition, rangeForMarker.start)) {
                String precedingText = plainText(*precedingTextRange);
                if (!precedingText.isEmpty()) {
                    for (auto& interpretation : interpretations)
                        append(interpretation, precedingText);
                }
            }

            String visibleTextForMarker = plainText(rangeForMarker);
            size_t interpretationsCountForCurrentMarker = alternatives.size() + 1;
            for (size_t i = 0; i < interpretationsCount; ++i) {
                size_t indexOfInterpretationForCurrentMarker = (i / combinationsSoFar) % interpretationsCountForCurrentMarker;
                if (!indexOfInterpretationForCurrentMarker)
                    append(interpretations[i], visibleTextForMarker);
                else
                    append(interpretations[i], alternatives[i % alternatives.size()]);
            }

            combinationsSoFar *= interpretationsCountForCurrentMarker;

            precedingTextStartPosition = makeDeprecatedLegacyPosition(rangeForMarker.end);
        }
    }

    // Finally, add any text after the last marker.
    if (auto range = makeSimpleRange(precedingTextStartPosition, rangeOfRootContents.end)) {
        String textAfterLastMarker = plainText(*range);
        if (!textAfterLastMarker.isEmpty()) {
            for (auto& interpretation : interpretations)
                append(interpretation, textAfterLastMarker);
        }
    }

    return createNSArray(interpretations, [] (auto& interpretation) {
        return adoptNS([[NSString alloc] initWithCharacters:reinterpret_cast<const unichar*>(interpretation.span().data()) length:interpretation.size()]);
    }).autorelease();
}

void LocalFrame::viewportOffsetChanged(ViewportOffsetChangeType changeType)
{
    LOG_WITH_STREAM(Scrolling, stream << "Frame::viewportOffsetChanged - " << (changeType == IncrementalScrollOffset ? "incremental" : "completed"));

    if (changeType == IncrementalScrollOffset) {
        if (RenderView* root = contentRenderer())
            root->compositor().didChangeVisibleRect();
    }

    if (changeType == CompletedScrollOffset) {
        if (RenderView* root = contentRenderer())
            root->compositor().updateCompositingLayers(CompositingUpdateType::OnScroll);
    }
}

bool LocalFrame::containsTiledBackingLayers() const
{
    if (RenderView* root = contentRenderer())
        return root->compositor().hasNonMainLayersWithTiledBacking();

    return false;
}

void LocalFrame::overflowScrollPositionChangedForNode(const IntPoint& position, Node* node, bool isUserScroll)
{
    LOG_WITH_STREAM(Scrolling, stream << "Frame::overflowScrollPositionChangedForNode " << node << " position " << position);

    RenderObject* renderer = node->renderer();
    if (!renderer || !renderer->hasLayer())
        return;

    auto* layer = downcast<RenderBoxModelObject>(*renderer).layer();
    if (!layer)
        return;
    auto* scrollableArea = layer->ensureLayerScrollableArea();

    auto oldScrollType = scrollableArea->currentScrollType();
    scrollableArea->setCurrentScrollType(isUserScroll ? ScrollType::User : ScrollType::Programmatic);
    scrollableArea->scrollToOffsetWithoutAnimation(position);
    scrollableArea->setCurrentScrollType(oldScrollType);

    scrollableArea->didEndScroll(); // FIXME: Should we always call this?
}

void LocalFrame::resetAllGeolocationPermission()
{
    if (document()->window())
        document()->window()->resetAllGeolocationPermission();

    for (RefPtr child = tree().firstChild(); child; child = child->tree().nextSibling()) {
        auto* localChild = dynamicDowncast<LocalFrame>(child.get());
        if (!localChild)
            continue;
        localChild->resetAllGeolocationPermission();
    }
}

} // namespace WebCore

#endif // PLATFORM(IOS_FAMILY)
