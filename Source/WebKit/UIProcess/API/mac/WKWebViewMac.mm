/*
 * Copyright (C) 2014-2023 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKWebViewMac.h"

#if PLATFORM(MAC)

#import "AppKitSPI.h"
#import "WKIntelligenceTextEffectCoordinator.h"
#import "WKTextFinderClient.h"
#import "WKWebViewConfigurationPrivate.h"
#import "WebBackForwardList.h"
#import "WebFrameProxy.h"
#import "WebPageProxy.h"
#import "WebPreferences.h"
#import "WebProcessProxy.h"
#import "WebViewImpl.h"
#import "_WKFrameHandleInternal.h"
#import "_WKHitTestResultInternal.h"
#import "_WKWarningView.h"
#import <WebCore/CGWindowUtilities.h>
#import <WebKit/WKUIDelegatePrivate.h>
#import <pal/spi/mac/NSTextFinderSPI.h>
#import <pal/spi/mac/NSTextInputContextSPI.h>
#import <pal/spi/mac/NSViewSPI.h>
#import <wtf/StdLibExtras.h>
#import <wtf/cocoa/RuntimeApplicationChecksCocoa.h>

_WKOverlayScrollbarStyle toAPIScrollbarStyle(std::optional<WebCore::ScrollbarOverlayStyle> coreScrollbarStyle)
{
    if (!coreScrollbarStyle)
        return _WKOverlayScrollbarStyleAutomatic;
    
    switch (coreScrollbarStyle.value()) {
    case WebCore::ScrollbarOverlayStyle::Dark:
        return _WKOverlayScrollbarStyleDark;
    case WebCore::ScrollbarOverlayStyle::Light:
        return _WKOverlayScrollbarStyleLight;
    case WebCore::ScrollbarOverlayStyle::Default:
        return _WKOverlayScrollbarStyleDefault;
    }
    ASSERT_NOT_REACHED();
    return _WKOverlayScrollbarStyleAutomatic;
}

std::optional<WebCore::ScrollbarOverlayStyle> toCoreScrollbarStyle(_WKOverlayScrollbarStyle scrollbarStyle)
{
    switch (scrollbarStyle) {
    case _WKOverlayScrollbarStyleDark:
        return WebCore::ScrollbarOverlayStyle::Dark;
    case _WKOverlayScrollbarStyleLight:
        return WebCore::ScrollbarOverlayStyle::Light;
    case _WKOverlayScrollbarStyleDefault:
        return WebCore::ScrollbarOverlayStyle::Default;
    case _WKOverlayScrollbarStyleAutomatic:
        break;
    }
    return std::nullopt;
}

static WebCore::FloatBoxExtent coreBoxExtentsFromEdgeInsets(NSEdgeInsets insets)
{
    return {
        static_cast<float>(insets.top),
        static_cast<float>(insets.right),
        static_cast<float>(insets.bottom),
        static_cast<float>(insets.left)
    };
}

@interface WKWebView (WKImplementationMac) <NSTextInputClient
    , NSTextInputClient_Async
#if HAVE(TOUCH_BAR)
    , NSTouchBarProvider
#endif
#if ENABLE(DRAG_SUPPORT)
    , NSFilePromiseProviderDelegate
    , NSDraggingSource
#endif
    , NSScrollViewSeparatorTrackingAdapter
    >
@end

@implementation WKWebView (WKImplementationMac)

#pragma mark - NSView overrides

- (BOOL)acceptsFirstResponder
{
    return _impl->acceptsFirstResponder();
}

- (BOOL)becomeFirstResponder
{
    return _impl->becomeFirstResponder();
}

- (BOOL)resignFirstResponder
{
    return _impl->resignFirstResponder();
}

- (void)viewWillStartLiveResize
{
    _impl->viewWillStartLiveResize();
}

- (void)viewDidEndLiveResize
{
    _impl->viewDidEndLiveResize();
}

- (BOOL)isFlipped
{
    return YES;
}

- (NSSize)intrinsicContentSize
{
    return NSSizeFromCGSize(_impl->intrinsicContentSize());
}

- (void)prepareContentInRect:(NSRect)rect
{
    _impl->prepareContentInRect(NSRectToCGRect(rect));
}

- (BOOL)_holdWindowResizeSnapshotIfNeeded
{
#if HAVE(NSWINDOW_SNAPSHOT_READINESS_HANDLER)
    if (_windowSnapshotReadinessHandler)
        return NO;

    if (!self.window)
        return NO;

    RefPtr page = _page;
    if (!page)
        return NO;

    if (!page->protectedLegacyMainFrameProcess()->isResponsive())
        return NO;

    if (page->isSuspended())
        return NO;

    if (self.hidden)
        return NO;

    _windowSnapshotReadinessHandler = makeBlockPtr([self.window _holdResizeSnapshotWithReason:@"full screen"]);
    if (!_windowSnapshotReadinessHandler)
        return NO;

    RELEASE_LOG(ViewState, "%p - [pageProxyID=%" PRIu64 ", webPageID=%" PRIu64 ", PID=%i] Began holding window resize snapshot for window full screen",
        self, page->identifier().toUInt64(), page->webPageIDInMainFrameProcess().toUInt64(), page->legacyMainFrameProcessID());
    return YES;
#else
    return NO;
#endif
}

- (void)_doWindowSnapshotReadinessUpdate
{
#if HAVE(NSWINDOW_SNAPSHOT_READINESS_HANDLER)
    [self performSelector:@selector(_invalidateWindowSnapshotReadinessHandler) withObject:nil afterDelay:1];
    [self _doAfterNextPresentationUpdate:makeBlockPtr([weakSelf = WeakObjCPtr<WKWebView>(self)] {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return;

        [NSObject cancelPreviousPerformRequestsWithTarget:strongSelf.get() selector:@selector(_invalidateWindowSnapshotReadinessHandler) object:nil];
        [strongSelf _invalidateWindowSnapshotReadinessHandler];
    }).get()];
#endif // HAVE(NSWINDOW_SNAPSHOT_READINESS_HANDLER)
}

- (void)setFrameSize:(NSSize)size
{
    BOOL didCreateWindowSnapshotReadinessHandler = [self _holdWindowResizeSnapshotIfNeeded];

    [super setFrameSize:size];
    [_warningView setFrame:self.bounds];
    if (_impl)
        _impl->setFrameSize(NSSizeToCGSize(size));

    [self _recalculateViewportSizesWithMinimumViewportInset:_minimumViewportInset maximumViewportInset:_maximumViewportInset throwOnInvalidInput:NO];

    if (didCreateWindowSnapshotReadinessHandler)
        [self _doWindowSnapshotReadinessUpdate];
}

- (void)setUserInterfaceLayoutDirection:(NSUserInterfaceLayoutDirection)userInterfaceLayoutDirection
{
    [super setUserInterfaceLayoutDirection:userInterfaceLayoutDirection];
    if (_impl)
        _impl->setUserInterfaceLayoutDirection(userInterfaceLayoutDirection);
}

- (void)_setSemanticContext:(NSViewSemanticContext)semanticContext
{
    auto wasUsingFormSemanticContext = _impl ? _impl->useFormSemanticContext() : false;

    [super _setSemanticContext:semanticContext];

    if (!_impl)
        return;

    if (wasUsingFormSemanticContext != _impl->useFormSemanticContext())
        _impl->semanticContextDidChange();
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)renewGState
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
#if ENABLE(SCREEN_TIME)
    [self _updateScreenTimeViewGeometry];
#endif

    if (_impl)
        _impl->renewGState();
    [super renewGState];
}

#pragma mark - macOS IBAction/NSResponder

#define WEBCORE_COMMAND(command) - (void)command:(id)sender { _impl->executeEditCommandForSelector(_cmd); }

WEBCORE_COMMAND(alignCenter)
WEBCORE_COMMAND(alignJustified)
WEBCORE_COMMAND(alignLeft)
WEBCORE_COMMAND(alignRight)
WEBCORE_COMMAND(copy)
WEBCORE_COMMAND(copyFont)
WEBCORE_COMMAND(cut)
WEBCORE_COMMAND(delete)
WEBCORE_COMMAND(deleteBackward)
WEBCORE_COMMAND(deleteBackwardByDecomposingPreviousCharacter)
WEBCORE_COMMAND(deleteForward)
WEBCORE_COMMAND(deleteToBeginningOfLine)
WEBCORE_COMMAND(deleteToBeginningOfParagraph)
WEBCORE_COMMAND(deleteToEndOfLine)
WEBCORE_COMMAND(deleteToEndOfParagraph)
WEBCORE_COMMAND(deleteToMark)
WEBCORE_COMMAND(deleteWordBackward)
WEBCORE_COMMAND(deleteWordForward)
WEBCORE_COMMAND(ignoreSpelling)
WEBCORE_COMMAND(indent)
WEBCORE_COMMAND(insertBacktab)
WEBCORE_COMMAND(insertLineBreak)
WEBCORE_COMMAND(insertNewline)
WEBCORE_COMMAND(insertNewlineIgnoringFieldEditor)
WEBCORE_COMMAND(insertParagraphSeparator)
WEBCORE_COMMAND(insertTab)
WEBCORE_COMMAND(insertTabIgnoringFieldEditor)
WEBCORE_COMMAND(makeTextWritingDirectionLeftToRight)
WEBCORE_COMMAND(makeTextWritingDirectionNatural)
WEBCORE_COMMAND(makeTextWritingDirectionRightToLeft)
WEBCORE_COMMAND(moveBackward)
WEBCORE_COMMAND(moveBackwardAndModifySelection)
WEBCORE_COMMAND(moveDown)
WEBCORE_COMMAND(moveDownAndModifySelection)
WEBCORE_COMMAND(moveForward)
WEBCORE_COMMAND(moveForwardAndModifySelection)
WEBCORE_COMMAND(moveLeft)
WEBCORE_COMMAND(moveLeftAndModifySelection)
WEBCORE_COMMAND(moveParagraphBackwardAndModifySelection)
WEBCORE_COMMAND(moveParagraphForwardAndModifySelection)
WEBCORE_COMMAND(moveRight)
WEBCORE_COMMAND(moveRightAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfDocument)
WEBCORE_COMMAND(moveToBeginningOfDocumentAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfLine)
WEBCORE_COMMAND(moveToBeginningOfLineAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfParagraph)
WEBCORE_COMMAND(moveToBeginningOfParagraphAndModifySelection)
WEBCORE_COMMAND(moveToBeginningOfSentence)
WEBCORE_COMMAND(moveToBeginningOfSentenceAndModifySelection)
WEBCORE_COMMAND(moveToEndOfDocument)
WEBCORE_COMMAND(moveToEndOfDocumentAndModifySelection)
WEBCORE_COMMAND(moveToEndOfLine)
WEBCORE_COMMAND(moveToEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveToEndOfParagraph)
WEBCORE_COMMAND(moveToEndOfParagraphAndModifySelection)
WEBCORE_COMMAND(moveToEndOfSentence)
WEBCORE_COMMAND(moveToEndOfSentenceAndModifySelection)
WEBCORE_COMMAND(moveToLeftEndOfLine)
WEBCORE_COMMAND(moveToLeftEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveToRightEndOfLine)
WEBCORE_COMMAND(moveToRightEndOfLineAndModifySelection)
WEBCORE_COMMAND(moveUp)
WEBCORE_COMMAND(moveUpAndModifySelection)
WEBCORE_COMMAND(moveWordBackward)
WEBCORE_COMMAND(moveWordBackwardAndModifySelection)
WEBCORE_COMMAND(moveWordForward)
WEBCORE_COMMAND(moveWordForwardAndModifySelection)
WEBCORE_COMMAND(moveWordLeft)
WEBCORE_COMMAND(moveWordLeftAndModifySelection)
WEBCORE_COMMAND(moveWordRight)
WEBCORE_COMMAND(moveWordRightAndModifySelection)
WEBCORE_COMMAND(outdent)
WEBCORE_COMMAND(pageDown)
WEBCORE_COMMAND(pageDownAndModifySelection)
WEBCORE_COMMAND(pageUp)
WEBCORE_COMMAND(pageUpAndModifySelection)
WEBCORE_COMMAND(paste)
WEBCORE_COMMAND(pasteAsPlainText)
WEBCORE_COMMAND(scrollPageDown)
WEBCORE_COMMAND(scrollPageUp)
WEBCORE_COMMAND(pasteFont)
WEBCORE_COMMAND(scrollLineDown)
WEBCORE_COMMAND(scrollLineUp)
WEBCORE_COMMAND(scrollToBeginningOfDocument)
WEBCORE_COMMAND(scrollToEndOfDocument)
WEBCORE_COMMAND(selectAll)
WEBCORE_COMMAND(selectLine)
WEBCORE_COMMAND(selectParagraph)
WEBCORE_COMMAND(selectSentence)
WEBCORE_COMMAND(selectToMark)
WEBCORE_COMMAND(selectWord)
WEBCORE_COMMAND(setMark)
WEBCORE_COMMAND(subscript)
WEBCORE_COMMAND(superscript)
WEBCORE_COMMAND(swapWithMark)
WEBCORE_COMMAND(takeFindStringFromSelection)
WEBCORE_COMMAND(transpose)
WEBCORE_COMMAND(underline)
WEBCORE_COMMAND(unscript)
WEBCORE_COMMAND(yank)
WEBCORE_COMMAND(yankAndSelect)

#undef WEBCORE_COMMAND

- (BOOL)writeSelectionToPasteboard:(NSPasteboard *)pasteboard types:(NSArray *)types
{
    return _impl->writeSelectionToPasteboard(pasteboard, types);
}

- (void)centerSelectionInVisibleArea:(id)sender
{
    _impl->centerSelectionInVisibleArea();
}

- (id)validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType
{
    return _impl->validRequestorForSendAndReturnTypes(sendType, returnType);
}

- (BOOL)readSelectionFromPasteboard:(NSPasteboard *)pasteboard
{
    return _impl->readSelectionFromPasteboard(pasteboard);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)changeFont:(id)sender
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    _impl->changeFontFromFontManager();
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)changeColor:(id)sender
ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
{
    _impl->changeFontColorFromSender(sender);
}

- (void)changeAttributes:(id)sender
{
    _impl->changeFontAttributesFromSender(sender);
}

- (IBAction)startSpeaking:(id)sender
{
    _impl->startSpeaking();
}

- (IBAction)stopSpeaking:(id)sender
{
    _impl->stopSpeaking(sender);
}

- (IBAction)showGuessPanel:(id)sender
{
    _impl->showGuessPanel(sender);
}

- (IBAction)checkSpelling:(id)sender
{
    _impl->checkSpelling();
}

- (void)changeSpelling:(id)sender
{
    _impl->changeSpelling(sender);
}

- (IBAction)toggleContinuousSpellChecking:(id)sender
{
    _impl->toggleContinuousSpellChecking();
}

- (BOOL)isGrammarCheckingEnabled
{
    return _impl->isGrammarCheckingEnabled();
}

- (void)setGrammarCheckingEnabled:(BOOL)flag
{
    _impl->setGrammarCheckingEnabled(flag);
}

- (IBAction)toggleGrammarChecking:(id)sender
{
    _impl->toggleGrammarChecking();
}

- (IBAction)toggleAutomaticSpellingCorrection:(id)sender
{
    _impl->toggleAutomaticSpellingCorrection();
}

- (void)orderFrontSubstitutionsPanel:(id)sender
{
    _impl->orderFrontSubstitutionsPanel(sender);
}

- (IBAction)toggleSmartInsertDelete:(id)sender
{
    _impl->toggleSmartInsertDelete();
}

- (BOOL)isAutomaticQuoteSubstitutionEnabled
{
    return _impl->isAutomaticQuoteSubstitutionEnabled();
}

- (void)setAutomaticQuoteSubstitutionEnabled:(BOOL)flag
{
    _impl->setAutomaticQuoteSubstitutionEnabled(flag);
}

- (void)toggleAutomaticQuoteSubstitution:(id)sender
{
    _impl->toggleAutomaticQuoteSubstitution();
}

- (BOOL)isAutomaticDashSubstitutionEnabled
{
    return _impl->isAutomaticDashSubstitutionEnabled();
}

- (void)setAutomaticDashSubstitutionEnabled:(BOOL)flag
{
    _impl->setAutomaticDashSubstitutionEnabled(flag);
}

- (void)toggleAutomaticDashSubstitution:(id)sender
{
    _impl->toggleAutomaticDashSubstitution();
}

- (BOOL)isAutomaticLinkDetectionEnabled
{
    return _impl->isAutomaticLinkDetectionEnabled();
}

- (void)setAutomaticLinkDetectionEnabled:(BOOL)flag
{
    _impl->setAutomaticLinkDetectionEnabled(flag);
}

- (void)toggleAutomaticLinkDetection:(id)sender
{
    _impl->toggleAutomaticLinkDetection();
}

- (BOOL)isAutomaticTextReplacementEnabled
{
    return _impl->isAutomaticTextReplacementEnabled();
}

- (void)setAutomaticTextReplacementEnabled:(BOOL)flag
{
    _impl->setAutomaticTextReplacementEnabled(flag);
}

- (void)toggleAutomaticTextReplacement:(id)sender
{
    _impl->toggleAutomaticTextReplacement();
}

- (void)uppercaseWord:(id)sender
{
    _impl->uppercaseWord();
}

- (void)lowercaseWord:(id)sender
{
    _impl->lowercaseWord();
}

- (void)capitalizeWord:(id)sender
{
    _impl->capitalizeWord();
}

- (BOOL)_wantsKeyDownForEvent:(NSEvent *)event
{
    return _impl->wantsKeyDownForEvent(event);
}

- (void)scrollWheel:(NSEvent *)event
{
    _impl->scrollWheel(event);
}

- (void)swipeWithEvent:(NSEvent *)event
{
    _impl->swipeWithEvent(event);
}

- (void)mouseDown:(NSEvent *)event
{
    _impl->mouseDown(event);
}

- (void)mouseUp:(NSEvent *)event
{
    _impl->mouseUp(event);
}

- (void)mouseDragged:(NSEvent *)event
{
    _impl->mouseDragged(event);
}

- (void)otherMouseDown:(NSEvent *)event
{
    _impl->otherMouseDown(event);
}

- (void)otherMouseDragged:(NSEvent *)event
{
    _impl->otherMouseDragged(event);
}

- (void)otherMouseUp:(NSEvent *)event
{
    _impl->otherMouseUp(event);
}

- (void)rightMouseDown:(NSEvent *)event
{
    _impl->rightMouseDown(event);
}

- (void)rightMouseDragged:(NSEvent *)event
{
    _impl->rightMouseDragged(event);
}

- (void)rightMouseUp:(NSEvent *)event
{
    _impl->rightMouseUp(event);
}

- (void)pressureChangeWithEvent:(NSEvent *)event
{
    _impl->pressureChangeWithEvent(event);
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return _impl->acceptsFirstMouse(event);
}

- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent *)event
{
    return _impl->shouldDelayWindowOrderingForEvent(event);
}

- (void)doCommandBySelector:(SEL)selector
{
    _impl->doCommandBySelector(selector);
}

- (NSTextInputContext *)inputContext
{
    if (!_impl)
        return nil;
    return _impl->inputContext();
}

- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    return _impl->performKeyEquivalent(event);
}

- (void)keyUp:(NSEvent *)theEvent
{
    _impl->keyUp(theEvent);
}

- (void)keyDown:(NSEvent *)theEvent
{
    _impl->keyDown(theEvent);
}

- (void)flagsChanged:(NSEvent *)theEvent
{
    _impl->flagsChanged(theEvent);
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)newSelectedRange replacementRange:(NSRange)replacementRange
{
    _impl->setMarkedText(string, newSelectedRange, replacementRange);
}

- (void)unmarkText
{
    _impl->unmarkText();
}

- (NSRange)selectedRange
{
    return _impl->selectedRange();
}

- (BOOL)hasMarkedText
{
    return _impl->hasMarkedText();
}

- (NSRange)markedRange
{
    return _impl->markedRange();
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)nsRange actualRange:(NSRangePointer)actualRange
{
    return _impl->attributedSubstringForProposedRange(nsRange, actualRange);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint
{
    return _impl->characterIndexForPoint(thePoint);
}

- (void)typingAttributesWithCompletionHandler:(void(^)(NSDictionary<NSString *, id> *))completion
{
    _impl->typingAttributesWithCompletionHandler(completion);
}

- (NSRect)firstRectForCharacterRange:(NSRange)theRange actualRange:(NSRangePointer)actualRange
{
    return _impl->firstRectForCharacterRange(theRange, actualRange);
}

- (void)selectedRangeWithCompletionHandler:(void(^)(NSRange selectedRange))completionHandlerPtr
{
    _impl->selectedRangeWithCompletionHandler(completionHandlerPtr);
}

- (void)markedRangeWithCompletionHandler:(void(^)(NSRange markedRange))completionHandlerPtr
{
    _impl->markedRangeWithCompletionHandler(completionHandlerPtr);
}

- (void)hasMarkedTextWithCompletionHandler:(void(^)(BOOL hasMarkedText))completionHandlerPtr
{
    _impl->hasMarkedTextWithCompletionHandler(completionHandlerPtr);
}

- (void)attributedSubstringForProposedRange:(NSRange)nsRange completionHandler:(void(^)(NSAttributedString *attrString, NSRange actualRange))completionHandlerPtr
{
    _impl->attributedSubstringForProposedRange(nsRange, completionHandlerPtr);
}

// FIXME: actually return valid information.
// rdar://130702677
- (void)unionRectForCharacterRange:(NSRange)range completionHandler:(void(^)(NSRect rect))completionHandler
{
    completionHandler(NSZeroRect);
}

- (void)firstRectForCharacterRange:(NSRange)theRange completionHandler:(void(^)(NSRect firstRect, NSRange actualRange))completionHandlerPtr
{
    _impl->firstRectForCharacterRange(theRange, completionHandlerPtr);
}

- (void)characterIndexForPoint:(NSPoint)thePoint completionHandler:(void(^)(NSUInteger))completionHandlerPtr
{
    _impl->characterIndexForPoint(thePoint, completionHandlerPtr);
}

- (NSArray *)validAttributesForMarkedText
{
    return _impl->validAttributesForMarkedText();
}

- (void)insertTextPlaceholderWithSize:(CGSize)size completionHandler:(void (^)(NSTextPlaceholder *))completionHandler
{
    _impl->insertTextPlaceholderWithSize(size, completionHandler);
}

- (void)removeTextPlaceholder:(NSTextPlaceholder *)placeholder willInsertText:(BOOL)willInsertText completionHandler:(void (^)(void))completionHandler
{
    _impl->removeTextPlaceholder(placeholder, willInsertText, completionHandler);
}

- (void)showContextMenuForSelection:(id)sender
{
    _page->handleContextMenuKeyEvent();
}

#if ENABLE(WRITING_TOOLS)

- (void)showWritingTools:(id)sender
{
    WTRequestedTool tool = (WTRequestedTool)[sender tag];
    if (tool == -1)
        tool = WTRequestedToolIndex;

    _impl->showWritingTools(tool);
}

#endif

#if ENABLE(DRAG_SUPPORT)
ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)draggedImage:(NSImage *)image endedAt:(NSPoint)endPoint operation:(NSDragOperation)operation
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    _impl->draggedImage(image, NSPointToCGPoint(endPoint), operation);
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)draggingInfo
{
    return _impl->draggingEntered(draggingInfo);
}

- (NSDragOperation)draggingUpdated:(id <NSDraggingInfo>)draggingInfo
{
    return _impl->draggingUpdated(draggingInfo);
}

- (void)draggingExited:(id <NSDraggingInfo>)draggingInfo
{
    _impl->draggingExited(draggingInfo);
}

- (BOOL)prepareForDragOperation:(id <NSDraggingInfo>)draggingInfo
{
    return _impl->prepareForDragOperation(draggingInfo);
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)draggingInfo
{
    return _impl->performDragOperation(draggingInfo);
}

- (NSView *)_hitTest:(NSPoint *)point dragTypes:(NSSet *)types
{
    return _impl->hitTestForDragTypes(NSPointToCGPoint(*point), types);
}
#endif // ENABLE(DRAG_SUPPORT)

- (BOOL)_windowResizeMouseLocationIsInVisibleScrollerThumb:(NSPoint)point
{
    return _impl->windowResizeMouseLocationIsInVisibleScrollerThumb(NSPointToCGPoint(point));
}

- (void)viewWillMoveToWindow:(NSWindow *)window
{
    _impl->viewWillMoveToWindow(window);
}

- (void)viewDidMoveToWindow
{
    _impl->viewDidMoveToWindow();
}

- (void)drawRect:(NSRect)rect
{
    _impl->drawRect(NSRectToCGRect(rect));
}

- (BOOL)isOpaque
{
    return _impl->isOpaque();
}

- (BOOL)mouseDownCanMoveWindow
{
    return WebKit::WebViewImpl::mouseDownCanMoveWindow();
}

- (void)viewDidHide
{
    _impl->viewDidHide();
}

- (void)viewDidUnhide
{
    _impl->viewDidUnhide();
}

- (void)viewDidChangeBackingProperties
{
    _impl->viewDidChangeBackingProperties();
}

- (void)_activeSpaceDidChange:(NSNotification *)notification
{
    _impl->activeSpaceDidChange();
}

- (id)accessibilityFocusedUIElement
{
    return _impl->accessibilityFocusedUIElement();
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (BOOL)accessibilityIsIgnored
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    return _impl->accessibilityIsIgnored();
}

- (id)accessibilityHitTest:(NSPoint)point
{
    return _impl->accessibilityHitTest(NSPointToCGPoint(point));
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (id)accessibilityAttributeValue:(NSString *)attribute
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    return _impl->accessibilityAttributeValue(attribute);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (id)accessibilityAttributeValue:(NSString *)attribute forParameter:(id)parameter
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    return _impl->accessibilityAttributeValue(attribute, parameter);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (NSArray<NSString *> *)accessibilityParameterizedAttributeNames
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    NSArray<NSString *> *names = [super accessibilityParameterizedAttributeNames];
    return [names arrayByAddingObject:@"AXConvertRelativeFrame"];
}

- (NSView *)hitTest:(NSPoint)point
{
    if (!_impl)
        return [super hitTest:point];
    return _impl->hitTest(NSPointToCGPoint(point)).autorelease();
}

- (NSInteger)conversationIdentifier
{
    return (NSInteger)self;
}

- (void)quickLookWithEvent:(NSEvent *)event
{
    _impl->quickLookWithEvent(event);
}

- (NSTrackingRectTag)addTrackingRect:(NSRect)rect owner:(id)owner userData:(void *)data assumeInside:(BOOL)assumeInside
{
    return _impl->addTrackingRect(NSRectToCGRect(rect), owner, data, assumeInside);
}

- (NSTrackingRectTag)_addTrackingRect:(NSRect)rect owner:(id)owner userData:(void *)data assumeInside:(BOOL)assumeInside useTrackingNum:(int)tag
{
    return _impl->addTrackingRectWithTrackingNum(NSRectToCGRect(rect), owner, data, assumeInside, tag);
}

- (void)_addTrackingRects:(NSRect *)rawRects owner:(id)owner userDataList:(void **)userDataList assumeInsideList:(BOOL *)assumeInsideList trackingNums:(NSTrackingRectTag *)trackingNums count:(int)count
{
    auto nsRects = unsafeMakeSpan(rawRects, count);
    auto cgRects = WTF::map(nsRects, [](auto& nsRect) {
        return NSRectToCGRect(nsRect);
    });
    _impl->addTrackingRectsWithTrackingNums(cgRects, owner, userDataList, assumeInsideList, trackingNums);
}

- (void)removeTrackingRect:(NSTrackingRectTag)tag
{
    if (!_impl)
        return;
    _impl->removeTrackingRect(tag);
}

- (void)_removeTrackingRects:(NSTrackingRectTag *)tags count:(int)count
{
    if (!_impl)
        return;
    _impl->removeTrackingRects(unsafeMakeSpan(tags, count));
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (NSString *)view:(NSView *)view stringForToolTip:(NSToolTipTag)tag point:(NSPoint)point userData:(void *)data
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    return _impl->stringForToolTip(tag);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)pasteboardChangedOwner:(NSPasteboard *)pasteboard
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    _impl->pasteboardChangedOwner(pasteboard);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (void)pasteboard:(NSPasteboard *)pasteboard provideDataForType:(NSString *)type
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    _impl->provideDataForPasteboard(pasteboard, type);
}

ALLOW_DEPRECATED_IMPLEMENTATIONS_BEGIN
- (NSArray *)namesOfPromisedFilesDroppedAtDestination:(NSURL *)dropDestination
ALLOW_DEPRECATED_IMPLEMENTATIONS_END
{
    return _impl->namesOfPromisedFilesDroppedAtDestination(dropDestination);
}

- (BOOL)wantsUpdateLayer
{
    return WebKit::WebViewImpl::wantsUpdateLayer();
}

- (void)updateLayer
{
    _impl->updateLayer();
}

- (void)smartMagnifyWithEvent:(NSEvent *)event
{
    _impl->smartMagnifyWithEvent(event);
}

- (void)magnifyWithEvent:(NSEvent *)event
{
    _impl->magnifyWithEvent(event);
}

#if ENABLE(MAC_GESTURE_EVENTS)
- (void)rotateWithEvent:(NSEvent *)event
{
    _impl->rotateWithEvent(event);
}
#endif // ENABLE(MAC_GESTURE_EVENTS)

- (WKTextFinderClient *)_ensureTextFinderClient
{
    if (!_textFinderClient)
        _textFinderClient = adoptNS([[WKTextFinderClient alloc] initWithPage:*_page view:self usePlatformFindUI:_usePlatformFindUI]);
    return _textFinderClient.get();
}

- (void)findMatchesForString:(NSString *)targetString relativeToMatch:(id <NSTextFinderAsynchronousDocumentFindMatch>)relativeMatch findOptions:(NSTextFinderAsynchronousDocumentFindOptions)findOptions maxResults:(NSUInteger)maxResults resultCollector:(void (^)(NSArray *matches, BOOL didWrap))resultCollector
{
    [[self _ensureTextFinderClient] findMatchesForString:targetString relativeToMatch:relativeMatch findOptions:findOptions maxResults:maxResults resultCollector:resultCollector];
}

- (void)replaceMatches:(NSArray *)matches withString:(NSString *)replacementString inSelectionOnly:(BOOL)selectionOnly resultCollector:(void (^)(NSUInteger replacementCount))resultCollector
{
    [[self _ensureTextFinderClient] replaceMatches:matches withString:replacementString inSelectionOnly:selectionOnly resultCollector:resultCollector];
}

- (void)scrollFindMatchToVisible:(id<NSTextFinderAsynchronousDocumentFindMatch>)match
{
    [[self _ensureTextFinderClient] scrollFindMatchToVisible:match];
}

- (NSView *)documentContainerView
{
    return self;
}

- (void)getSelectedText:(void (^)(NSString *selectedTextString))completionHandler
{
    [[self _ensureTextFinderClient] getSelectedText:completionHandler];
}

- (void)selectFindMatch:(id <NSTextFinderAsynchronousDocumentFindMatch>)findMatch completionHandler:(void (^)(void))completionHandler
{
    [[self _ensureTextFinderClient] selectFindMatch:findMatch completionHandler:completionHandler];
}

#if ENABLE(DRAG_SUPPORT)

- (NSString *)filePromiseProvider:(NSFilePromiseProvider *)filePromiseProvider fileNameForType:(NSString *)fileType
{
    return _impl->fileNameForFilePromiseProvider(filePromiseProvider, fileType);
}

- (void)filePromiseProvider:(NSFilePromiseProvider *)filePromiseProvider writePromiseToURL:(NSURL *)url completionHandler:(void (^)(NSError *error))completionHandler
{
    _impl->writeToURLForFilePromiseProvider(filePromiseProvider, url, completionHandler);
}

- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    return _impl->dragSourceOperationMask(session, context);
}

- (void)draggingSession:(NSDraggingSession *)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation
{
    _impl->draggingSessionEnded(session, screenPoint, operation);
}

#endif // ENABLE(DRAG_SUPPORT)

- (void)_prepareForImmediateActionAnimation
{
}

- (void)_cancelImmediateActionAnimation
{
}

- (void)_completeImmediateActionAnimation
{
}

- (void)_setDrawsTransparentBackground:(BOOL)drawsTransparentBackground
{
    static BOOL hasLoggedDeprecationWarning;
    if (!hasLoggedDeprecationWarning) {
        // See bug 155550 for details.
        NSLog(@"-[WKWebView _setDrawsTransparentBackground:] is deprecated and should not be used.");
        hasLoggedDeprecationWarning = YES;
    }
    [self _setDrawsBackground:!drawsTransparentBackground];
}

- (void)shareSheetDidDismiss:(WKShareSheet *)shareSheet
{
    _impl->shareSheetDidDismiss(shareSheet);
}

#pragma mark - Touch Bar

#if HAVE(TOUCH_BAR)

@dynamic touchBar;

- (NSTouchBar *)makeTouchBar
{
    return _impl->makeTouchBar();
}

- (NSCandidateListTouchBarItem *)candidateListTouchBarItem
{
    return _impl->candidateListTouchBarItem();
}

- (void)_web_didAddMediaControlsManager:(id)controlsManager
{
    [self _addMediaPlaybackControlsView:controlsManager];
}

- (void)_web_didRemoveMediaControlsManager
{
    [self _removeMediaPlaybackControlsView];
}

- (void)_interactWithMediaControlsForTesting
{
    [self _setWantsMediaPlaybackControlsView:YES];
    [self makeTouchBar];
}

#endif // HAVE(TOUCH_BAR)

#pragma mark - NSScrollViewSeparatorTrackingAdapter

- (NSRect)scrollViewFrame
{
    if (!_impl)
        return NSZeroRect;
    return _impl->scrollViewFrame();
}

- (BOOL)hasScrolledContentsUnderTitlebar
{
    if (!_impl)
        return NO;
    return _impl->hasScrolledContentsUnderTitlebar();
}

#pragma mark – NSAdaptiveImageGlyph

#if ENABLE(MULTI_REPRESENTATION_HEIC)

- (BOOL)supportsAdaptiveImageGlyph
{
    if ([self _isEditable] || [_configuration _multiRepresentationHEICInsertionEnabled])
        return _impl->isContentRichlyEditable();

    return NO;
}

- (void)insertAdaptiveImageGlyph:(NSAdaptiveImageGlyph *)adaptiveImageGlyph replacementRange:(NSRange)replacementRange
{
    _impl->insertMultiRepresentationHEIC(adaptiveImageGlyph.imageContent, adaptiveImageGlyph.contentDescription);
}

#endif

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)

- (BOOL)scrollViewDrawsMagicPocket
{
    if (!_page)
        return NO;

    if (!_page->preferences().contentInsetBackgroundFillEnabled())
        return NO;

    return _page->obscuredContentInsets().top() > 0;
}

- (void)registerPocketContainer:(NSView *)container onEdge:(NSScrollPocketEdge)edge
{
    if (edge == NSScrollPocketEdgeTop)
        _impl->registerViewAboveScrollPocket(container);
}

- (void)unregisterPocketContainer:(NSView *)container onEdge:(NSScrollPocketEdge)edge
{
    if (edge == NSScrollPocketEdgeTop)
        _impl->unregisterViewAboveScrollPocket(container);
}

#endif // ENABLE(CONTENT_INSET_BACKGROUND_FILL)

@end

#pragma mark -

@implementation WKWebView (WKIBActions)

- (BOOL)validateUserInterfaceItem:(id <NSValidatedUserInterfaceItem>)item
{
    SEL action = item.action;

    if (action == @selector(goBack:))
        return !!_page->backForwardList().backItem();

    if (action == @selector(goForward:))
        return !!_page->backForwardList().forwardItem();

    if (action == @selector(stopLoading:)) {
        // FIXME: Return no if we're stopped.
        return YES;
    }

    if (action == @selector(reload:) || action == @selector(reloadFromOrigin:)) {
        // FIXME: Return no if we're loading.
        return YES;
    }

    return _impl->validateUserInterfaceItem(item);
}

- (IBAction)goBack:(id)sender
{
    [self goBack];
}

- (IBAction)goForward:(id)sender
{
    [self goForward];
}

- (IBAction)reload:(id)sender
{
    [self reload];
}

- (IBAction)reloadFromOrigin:(id)sender
{
    [self reloadFromOrigin];
}

- (IBAction)stopLoading:(id)sender
{
    _page->stopLoading();
}

@end

#pragma mark -

@implementation WKWebView (WKInternalMac)

- (NSTextInputContext *)_web_superInputContext
{
    return [super inputContext];
}

- (void)_web_superQuickLookWithEvent:(NSEvent *)event
{
    [super quickLookWithEvent:event];
}

- (void)_web_superSwipeWithEvent:(NSEvent *)event
{
    [super swipeWithEvent:event];
}

- (void)_web_superMagnifyWithEvent:(NSEvent *)event
{
    [super magnifyWithEvent:event];
}

- (void)_web_superSmartMagnifyWithEvent:(NSEvent *)event
{
    [super smartMagnifyWithEvent:event];
}

- (void)_web_superRemoveTrackingRect:(NSTrackingRectTag)tag
{
    [super removeTrackingRect:tag];
}

- (id)_web_superAccessibilityAttributeValue:(NSString *)attribute
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    return [super accessibilityAttributeValue:attribute];
ALLOW_DEPRECATED_DECLARATIONS_END
}

- (void)_web_superDoCommandBySelector:(SEL)selector
{
    [super doCommandBySelector:selector];
}

- (BOOL)_web_superPerformKeyEquivalent:(NSEvent *)event
{
    return [super performKeyEquivalent:event];
}

- (void)_web_superKeyDown:(NSEvent *)event
{
    [super keyDown:event];
}

- (NSView *)_web_superHitTest:(NSPoint)point
{
    return [super hitTest:point];
}

- (id)_web_immediateActionAnimationControllerForHitTestResultInternal:(API::HitTestResult*)hitTestResult withType:(uint32_t)type userData:(API::Object*)userData
{
    RetainPtr data = userData ? static_cast<id<NSSecureCoding>>(userData->wrapper()) : nil;
    return [self _immediateActionAnimationControllerForHitTestResult:wrapper(*hitTestResult) withType:(_WKImmediateActionType)type userData:data.get()];
}

- (void)_web_prepareForImmediateActionAnimation
{
    id <WKUIDelegatePrivate> uiDelegate = (id <WKUIDelegatePrivate>)[self UIDelegate];
    if ([uiDelegate respondsToSelector:@selector(_prepareForImmediateActionAnimationForWebView:)])
        [uiDelegate _prepareForImmediateActionAnimationForWebView:self];
    else
        [self _prepareForImmediateActionAnimation];
}

- (void)_web_cancelImmediateActionAnimation
{
    id <WKUIDelegatePrivate> uiDelegate = (id <WKUIDelegatePrivate>)[self UIDelegate];
    if ([uiDelegate respondsToSelector:@selector(_cancelImmediateActionAnimationForWebView:)])
        [uiDelegate _cancelImmediateActionAnimationForWebView:self];
    else
        [self _cancelImmediateActionAnimation];
}

- (void)_web_completeImmediateActionAnimation
{
    id <WKUIDelegatePrivate> uiDelegate = (id <WKUIDelegatePrivate>)[self UIDelegate];
    if ([uiDelegate respondsToSelector:@selector(_completeImmediateActionAnimationForWebView:)])
        [uiDelegate _completeImmediateActionAnimationForWebView:self];
    else
        [self _completeImmediateActionAnimation];
}

- (void)_web_didChangeContentSize:(NSSize)newSize
{
}

- (BOOL)_web_hasActiveIntelligenceTextEffects
{
#if ENABLE(WRITING_TOOLS)
    return [_intelligenceTextEffectCoordinator hasActiveEffects];
#else
    return NO;
#endif
}

- (void)_web_suppressContentRelativeChildViews
{
#if ENABLE(WRITING_TOOLS)
    [_intelligenceTextEffectCoordinator hideEffectsWithCompletionHandler:^{ }];
#endif
}

- (void)_web_restoreContentRelativeChildViews
{
#if ENABLE(WRITING_TOOLS)
    [_intelligenceTextEffectCoordinator showEffectsWithCompletionHandler:^{ }];
#endif
}

#if ENABLE(DRAG_SUPPORT)

- (WKDragDestinationAction)_web_dragDestinationActionForDraggingInfo:(id <NSDraggingInfo>)draggingInfo
{
    id <WKUIDelegatePrivate> uiDelegate = (id <WKUIDelegatePrivate>)[self UIDelegate];
    if ([uiDelegate respondsToSelector:@selector(_webView:dragDestinationActionMaskForDraggingInfo:)])
        return [uiDelegate _webView:self dragDestinationActionMaskForDraggingInfo:draggingInfo];

    if (!linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::DropToNavigateDisallowedByDefault))
        return WKDragDestinationActionAny;

    return WKDragDestinationActionAny & ~WKDragDestinationActionLoad;
}

- (void)_web_didPerformDragOperation:(BOOL)handled
{
    id <WKUIDelegatePrivate> uiDelegate = (id <WKUIDelegatePrivate>)self.UIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:didPerformDragOperation:)])
        [uiDelegate _webView:self didPerformDragOperation:handled];
}

#endif // ENABLE(DRAG_SUPPORT)

- (void)_web_dismissContentRelativeChildWindows
{
    _impl->dismissContentRelativeChildWindowsFromViewOnly();
}

- (void)_web_dismissContentRelativeChildWindowsWithAnimation:(BOOL)withAnimation
{
    _page->clearTextIndicatorWithAnimation(withAnimation ? WebCore::TextIndicatorDismissalAnimation::FadeOut : WebCore::TextIndicatorDismissalAnimation::None);
    _impl->dismissContentRelativeChildWindowsWithAnimationFromViewOnly(withAnimation);
}

- (void)_web_editorStateDidChange
{
    [self _didChangeEditorState];
}

- (void)_web_gestureEventWasNotHandledByWebCore:(NSEvent *)event
{
    [self _gestureEventWasNotHandledByWebCore:event];
}

- (void)_takeFindStringFromSelectionInternal:(id)sender
{
    [self takeFindStringFromSelection:sender];
}

- (void)insertText:(id)string
{
    _impl->insertText(string);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    _impl->insertText(string, replacementRange);
}

- (void)_resetSecureInputState
{
    if (_impl)
        _impl->resetSecureInputState();
}

- (void)_setContentOffsetX:(NSNumber *)x y:(NSNumber *)y animated:(BOOL)animated
{
    std::optional<int> optionalX = std::nullopt;
    if (x)
        optionalX = static_cast<int>([x doubleValue]);

    std::optional<int> optionalY = std::nullopt;
    if (y)
        optionalY = static_cast<int>([y doubleValue]);

    _page->setContentOffset(optionalX, optionalY, animated ? WebCore::ScrollIsAnimated::Yes : WebCore::ScrollIsAnimated::No);
}

#pragma mark - QLPreviewPanelController

- (BOOL)acceptsPreviewPanelControl:(QLPreviewPanel *)panel
{
    return _impl->acceptsPreviewPanelControl(panel);
}

- (void)beginPreviewPanelControl:(QLPreviewPanel *)panel
{
    _impl->beginPreviewPanelControl(panel);
}

- (void)endPreviewPanelControl:(QLPreviewPanel *)panel
{
    _impl->endPreviewPanelControl(panel);
}

#pragma mark - NSTextCheckingClient_WritingTools

- (BOOL)providesWritingToolsContextMenu
{
    return YES;
}

@end

#pragma mark -

@implementation WKWebView (WKPrivateMac)

- (WKPageRef)_pageRefForTransitionToWKWebView
{
    return toAPI(_page.get());
}

- (BOOL)_hasActiveVideoForControlsManager
{
    return _page && _page->hasActiveVideoForControlsManager();
}

- (BOOL)_ignoresNonWheelEvents
{
    return _impl->ignoresNonWheelEvents();
}

- (void)_setIgnoresNonWheelEvents:(BOOL)ignoresNonWheelEvents
{
    RELEASE_LOG(MouseHandling, "[pageProxyID=%lld] [WKWebView _setIgnoresNonWheelEvents:%d]", _page->identifier().toUInt64(), ignoresNonWheelEvents);
    _impl->setIgnoresNonWheelEvents(ignoresNonWheelEvents);
}

- (BOOL)_ignoresMouseMoveEvents
{
    return _impl->ignoresMouseMoveEvents();
}

- (void)_setIgnoresMouseMoveEvents:(BOOL)ignoresMouseMoveEvents
{
    _impl->setIgnoresMouseMoveEvents(ignoresMouseMoveEvents);
}

- (NSView *)_safeBrowsingWarning
{
    return _impl->warningView();
}

- (_WKRectEdge)_pinnedState
{
    return _impl->pinnedState();
}

- (_WKRectEdge)_rubberBandingEnabled
{
    return _impl->rubberBandingEnabled();
}

- (void)_setRubberBandingEnabled:(_WKRectEdge)state
{
    _impl->setRubberBandingEnabled(state);
}

- (BOOL)_alwaysBounceVertical
{
    return _impl->alwaysBounceVertical();
}

- (void)_setAlwaysBounceVertical:(BOOL)value
{
    _impl->setAlwaysBounceVertical(value);
}

- (BOOL)_alwaysBounceHorizontal
{
    return _impl->alwaysBounceHorizontal();
}

- (void)_setAlwaysBounceHorizontal:(BOOL)value
{
    _impl->setAlwaysBounceHorizontal(value);
}

- (NSColor *)_backgroundColor
{
    return _impl->backgroundColor();
}

- (void)_setBackgroundColor:(NSColor *)backgroundColor
{
    _impl->setBackgroundColor(backgroundColor);
}

- (NSColor *)_underlayColor
{
    return _impl->underlayColor().autorelease();
}

- (void)_setUnderlayColor:(NSColor *)underlayColor
{
    _impl->setUnderlayColor(underlayColor);
}

- (void)_setTotalHeightOfBanners:(CGFloat)totalHeightOfBanners
{
    _impl->setTotalHeightOfBanners(totalHeightOfBanners);
}

- (CGFloat)_totalHeightOfBanners
{
    return _impl->totalHeightOfBanners();
}

- (BOOL)_drawsBackground
{
    return _impl->drawsBackground();
}

- (void)_setDrawsBackground:(BOOL)drawsBackground
{
    _impl->setDrawsBackground(drawsBackground);
}

- (void)_setTopContentInset:(CGFloat)inset
{
    auto insets = _impl->obscuredContentInsets();
    insets.setTop(static_cast<float>(inset));
    _impl->setObscuredContentInsets(insets);
}

- (CGFloat)_topContentInset
{
    return _impl->obscuredContentInsets().top();
}

- (void)_setTopContentInset:(CGFloat)inset immediate:(BOOL)immediate
{
    auto insets = _impl->obscuredContentInsets();
    insets.setTop(static_cast<float>(inset));
    _impl->setObscuredContentInsets(insets);
    if (immediate)
        _impl->flushPendingObscuredContentInsetChanges();
}

- (void)_setObscuredContentInsets:(NSEdgeInsets)insets immediate:(BOOL)immediate
{
    if (insets.top < 0 || insets.left < 0 || insets.bottom < 0 || insets.right < 0) {
        [NSException raise:NSInvalidArgumentException format:@"Obscured insets cannot be negative: {%f, %f, %f, %f}", insets.top, insets.left, insets.bottom, insets.right];
        return;
    }

    _impl->setObscuredContentInsets(coreBoxExtentsFromEdgeInsets(insets));

    if (immediate)
        _impl->flushPendingObscuredContentInsetChanges();
}

- (NSEdgeInsets)_obscuredContentInsets
{
    auto insets = _impl->obscuredContentInsets();
    return NSEdgeInsetsMake(
        static_cast<CGFloat>(insets.top()),
        static_cast<CGFloat>(insets.left()),
        static_cast<CGFloat>(insets.bottom()),
        static_cast<CGFloat>(insets.right())
    );
}

- (NSColor *)_overrideTopScrollEdgeEffectColor
{
    return _overrideTopScrollEdgeEffectColor.get();
}

- (void)_setOverrideTopScrollEdgeEffectColor:(NSColor *)color
{
    if (_overrideTopScrollEdgeEffectColor == color || [_overrideTopScrollEdgeEffectColor isEqual:color])
        return;

    _overrideTopScrollEdgeEffectColor = adoptNS(color.copy);

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)
    [self _doAfterAdjustingColorForTopContentInsetFromUIDelegate:[strongSelf = RetainPtr { self }] {
        [strongSelf _updateTopScrollPocketCaptureColor];
    }];
#endif
}

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)

- (NSScrollPocket *)_topScrollPocket
{
    return _impl->topScrollPocket();
}

#endif

- (void)_setUsesAutomaticContentInsetBackgroundFill:(BOOL)value
{
    if (_usesAutomaticContentInsetBackgroundFill == value)
        return;

    _usesAutomaticContentInsetBackgroundFill = value;

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)
    _impl->updateTopScrollPocketStyle();
    _impl->updateScrollPocketVisibilityWhenScrolledToTop();
    _impl->updateTopScrollPocketCaptureColor();
#endif
}

- (BOOL)_usesAutomaticContentInsetBackgroundFill
{
    return _usesAutomaticContentInsetBackgroundFill;
}

- (void)_setAutomaticallyAdjustsContentInsets:(BOOL)automaticallyAdjustsContentInsets
{
    _impl->setAutomaticallyAdjustsContentInsets(automaticallyAdjustsContentInsets);
}

- (BOOL)_automaticallyAdjustsContentInsets
{
    return _impl->automaticallyAdjustsContentInsets();
}

- (BOOL)_windowOcclusionDetectionEnabled
{
    return _impl->windowOcclusionDetectionEnabled();
}

- (void)_setWindowOcclusionDetectionEnabled:(BOOL)enabled
{
    _impl->setWindowOcclusionDetectionEnabled(enabled);
}

- (NSInteger)_spellCheckerDocumentTag
{
    return _impl->spellCheckerDocumentTag();
}

- (BOOL)_shouldExpandContentToViewHeightForAutoLayout
{
    return _impl->shouldExpandToViewHeightForAutoLayout();
}

- (void)_setShouldExpandContentToViewHeightForAutoLayout:(BOOL)shouldExpand
{
    return _impl->setShouldExpandToViewHeightForAutoLayout(shouldExpand);
}

- (CGFloat)_minimumLayoutWidth
{
    return _page->minimumSizeForAutoLayout().width();
}

- (void)_setMinimumLayoutWidth:(CGFloat)width
{
    BOOL expandsToFit = width > 0;

    _page->setMinimumSizeForAutoLayout(WebCore::IntSize(width, 0));
    _page->setMainFrameIsScrollable(!expandsToFit);

    _impl->setClipsToVisibleRect(expandsToFit);
}

- (CGSize)_sizeToContentAutoSizeMaximumSize
{
    return _page->minimumSizeForAutoLayout();
}

- (void)_setSizeToContentAutoSizeMaximumSize:(CGSize)size
{
    BOOL expandsToFit = size.width > 0 && size.height > 0;

    _page->setSizeToContentAutoSizeMaximumSize(WebCore::IntSize(size.width, size.height));
    _page->setMainFrameIsScrollable(!expandsToFit);

    _impl->setClipsToVisibleRect(expandsToFit);
}

- (BOOL)_clipsToVisibleRect
{
    return _impl->clipsToVisibleRect();
}

- (void)_setClipsToVisibleRect:(BOOL)clipsToVisibleRect
{
    _impl->setClipsToVisibleRect(clipsToVisibleRect);
}

- (BOOL)_alwaysShowsHorizontalScroller
{
    return _page->alwaysShowsHorizontalScroller();
}

- (void)_setAlwaysShowsHorizontalScroller:(BOOL)alwaysShowsHorizontalScroller
{
    _page->setAlwaysShowsHorizontalScroller(alwaysShowsHorizontalScroller);
}

- (BOOL)_alwaysShowsVerticalScroller
{
    return _page->alwaysShowsVerticalScroller();
}

- (void)_setAlwaysShowsVerticalScroller:(BOOL)alwaysShowsVerticalScroller
{
    _page->setAlwaysShowsVerticalScroller(alwaysShowsVerticalScroller);
}

- (void)_setOverlayScrollbarStyle:(_WKOverlayScrollbarStyle)scrollbarStyle
{
    _impl->setOverlayScrollbarStyle(toCoreScrollbarStyle(scrollbarStyle));
}

- (_WKOverlayScrollbarStyle)_overlayScrollbarStyle
{
    return toAPIScrollbarStyle(_impl->overlayScrollbarStyle());
}

- (NSView *)_inspectorAttachmentView
{
    return _impl->inspectorAttachmentView().autorelease();
}

- (void)_setInspectorAttachmentView:(NSView *)newView
{
    _impl->setInspectorAttachmentView(newView);
}

- (void)_setHeaderBannerLayer:(CALayer *)headerBannerLayer
{
    if (headerBannerLayer)
        [headerBannerLayer setContentsScale:_page->pageScaleFactor()];

    _impl->setHeaderBannerLayer(headerBannerLayer);
}

- (CALayer *)_headerBannerLayer
{
    return _impl->headerBannerLayer();
}

- (void)_setFooterBannerLayer:(CALayer *)footerBannerLayer
{
    if (footerBannerLayer)
        [footerBannerLayer setContentsScale:_page->pageScaleFactor()];

    _impl->setFooterBannerLayer(footerBannerLayer);
}

- (CALayer *)_footerBannerLayer
{
    return _impl->footerBannerLayer();
}

- (void)_setThumbnailView:(_WKThumbnailView *)thumbnailView
{
    _impl->setThumbnailView(thumbnailView);
}

- (_WKThumbnailView *)_thumbnailView
{
    if (!_impl)
        return nil;
    return _impl->thumbnailView().autorelease();
}

- (void)_setIgnoresAllEvents:(BOOL)ignoresAllEvents
{
    RELEASE_LOG(MouseHandling, "[pageProxyID=%lld] [WKWebView _setIgnoresAllEvents:%d]", _page->identifier().toUInt64(), ignoresAllEvents);
    _impl->setIgnoresAllEvents(ignoresAllEvents);
}

- (BOOL)_ignoresAllEvents
{
    return _impl->ignoresAllEvents();
}

- (BOOL)_usePlatformFindUI
{
    return _usePlatformFindUI;
}

- (void)_setUsePlatformFindUI:(BOOL)usePlatformFindUI
{
    _usePlatformFindUI = usePlatformFindUI;

    if (_textFinderClient)
        [self _hideFindUI];
    _textFinderClient = nil;
}

- (void)_setShouldSuppressFirstResponderChanges:(BOOL)shouldSuppress
{
    _impl->setShouldSuppressFirstResponderChanges(shouldSuppress);
}

- (BOOL)_canChangeFrameLayout:(_WKFrameHandle *)frameHandle
{
    if (RefPtr webFrameProxy = WebKit::WebFrameProxy::webFrame(frameHandle->_frameHandle->frameID()))
        return _impl->canChangeFrameLayout(*webFrameProxy);
    return false;
}

- (BOOL)_tryToSwipeWithEvent:(NSEvent *)event ignoringPinnedState:(BOOL)ignoringPinnedState
{
    return _impl->tryToSwipeWithEvent(event, ignoringPinnedState);
}

- (void)_dismissContentRelativeChildWindows
{
    _impl->dismissContentRelativeChildWindowsFromViewOnly();
}

- (void)_setFrame:(NSRect)rect andScrollBy:(NSSize)offset
{
    _impl->setFrameAndScrollBy(NSRectToCGRect(rect), NSSizeToCGSize(offset));
}

- (void)_gestureEventWasNotHandledByWebCore:(NSEvent *)event
{
    _impl->gestureEventWasNotHandledByWebCoreFromViewOnly(event);
}

- (double)minimumMagnification
{
    return _page->minPageZoomFactor();
}

- (void)_disableFrameSizeUpdates
{
    _impl->disableFrameSizeUpdates();
}

- (void)_enableFrameSizeUpdates
{
    _impl->enableFrameSizeUpdates();
}

- (void)_beginDeferringViewInWindowChanges
{
    _impl->beginDeferringViewInWindowChanges();
}

- (void)_endDeferringViewInWindowChanges
{
    _impl->endDeferringViewInWindowChanges();
}

- (void)_endDeferringViewInWindowChangesSync
{
    _impl->endDeferringViewInWindowChangesSync();
}

- (void)_setCustomSwipeViews:(NSArray *)customSwipeViews
{
    _impl->setCustomSwipeViews(customSwipeViews);
}

- (void)_setCustomSwipeViewsTopContentInset:(float)topContentInset
{
    auto insets = _impl->customSwipeViewsObscuredContentInsets();
    insets.setTop(topContentInset);
    _impl->setCustomSwipeViewsObscuredContentInsets(WTFMove(insets));
}

- (void)_setCustomSwipeViewsObscuredContentInsets:(NSEdgeInsets)insets
{
    _impl->setCustomSwipeViewsObscuredContentInsets(coreBoxExtentsFromEdgeInsets(insets));
}

- (void)_setDidMoveSwipeSnapshotCallback:(void(^)(CGRect))callback
{
    _impl->setDidMoveSwipeSnapshotCallback(callback);
}

- (NSView *)_fullScreenPlaceholderView
{
    return _impl->fullScreenPlaceholderView();
}

- (NSWindow *)_fullScreenWindow
{
    return _impl->fullScreenWindow();
}

- (id)_immediateActionAnimationControllerForHitTestResult:(_WKHitTestResult *)hitTestResult withType:(_WKImmediateActionType)type userData:(id<NSSecureCoding>)userData
{
    return nil;
}

- (NSPrintOperation *)_printOperationWithPrintInfo:(NSPrintInfo *)printInfo
{
    return [self printOperationWithPrintInfo:printInfo];
}

- (NSPrintOperation *)_printOperationWithPrintInfo:(NSPrintInfo *)printInfo forFrame:(_WKFrameHandle *)frameHandle
{
    if (RefPtr webFrameProxy = WebKit::WebFrameProxy::webFrame(frameHandle->_frameHandle->frameID()))
        return _impl->printOperationWithPrintInfo(printInfo, *webFrameProxy).autorelease();
    return nil;
}

- (BOOL)_wantsMediaPlaybackControlsView
{
#if HAVE(TOUCH_BAR)
    return _impl->clientWantsMediaPlaybackControlsView();
#else
    return NO;
#endif
}

- (void)_setWantsMediaPlaybackControlsView:(BOOL)wantsMediaPlaybackControlsView
{
#if HAVE(TOUCH_BAR)
    _impl->setClientWantsMediaPlaybackControlsView(wantsMediaPlaybackControlsView);
#endif
}

- (id)_mediaPlaybackControlsView
{
#if HAVE(TOUCH_BAR)
    return _impl->clientWantsMediaPlaybackControlsView() ? _impl->mediaPlaybackControlsView() : nil;
#else
    return nil;
#endif
}

// This method is for subclasses to override.
- (void)_addMediaPlaybackControlsView:(id)mediaPlaybackControlsView
{
}

// This method is for subclasses to override.
- (void)_removeMediaPlaybackControlsView
{
}

- (void)_prepareForMoveToWindow:(NSWindow *)targetWindow completionHandler:(void(^)(void))completionHandler
{
    auto completionHandlerCopy = makeBlockPtr(completionHandler);
    _impl->prepareForMoveToWindow(targetWindow, [completionHandlerCopy] {
        completionHandlerCopy();
    });
}

- (void)_simulateMouseMove:(NSEvent *)event
{
    return _impl->mouseMoved(event);
}

- (void)_simulateMouseEnter:(NSEvent *)event
{
    _impl->mouseEntered(event);
}

- (void)_setFont:(NSFont *)font sender:(id)sender
{
    _impl->setFontForWebView(font, sender);
}

- (void)_showWritingTools
{
#if ENABLE(WRITING_TOOLS)
    _impl->showWritingTools();
#endif
}

@end // WKWebView (WKPrivateMac)

@implementation WKWebView (WKWindowSnapshot)
- (NSImage *)_windowSnapshotInRect:(CGRect)rect withOptions:(CGWindowImageOption)options
{
    RetainPtr snapshot = WebCore::cgWindowListCreateImage(rect, kCGWindowListOptionIncludingWindow, (CGSWindowID)[[self window] windowNumber], options);
    if (!snapshot)
        return nil;

    return [[NSImage alloc] initWithCGImage:snapshot.get() size:NSZeroSize];
}
@end

#endif // PLATFORM(MAC)
