/*
 * Copyright (C) 2012-2024 Apple Inc. All rights reserved.
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
#import "WKContentViewInteraction.h"

#if PLATFORM(IOS_FAMILY)

#import "APIUIClient.h"
#import "CocoaImage.h"
#import "CompletionHandlerCallChecker.h"
#import "DocumentEditingContext.h"
#import "ImageAnalysisUtilities.h"
#import "InsertTextOptions.h"
#import "KeyEventInterpretationContext.h"
#import "Logging.h"
#import "NativeWebKeyboardEvent.h"
#import "NativeWebTouchEvent.h"
#import "NetworkProcessMessages.h"
#import "PageClient.h"
#import "PickerDismissalReason.h"
#import "PlatformWritingToolsUtilities.h"
#import "RemoteLayerTreeDrawingAreaProxy.h"
#import "RemoteLayerTreeViews.h"
#import "RemoteScrollingCoordinatorProxyIOS.h"
#import "RevealItem.h"
#import "SmartMagnificationController.h"
#import "TextChecker.h"
#import "TextCheckerState.h"
#import "TextInputSPI.h"
#import "TextRecognitionUpdateResult.h"
#import "UIGamepadProvider.h"
#import "UIKitSPI.h"
#import "UIKitUtilities.h"
#import "WKActionSheetAssistant.h"
#import "WKContextMenuElementInfoInternal.h"
#import "WKContextMenuElementInfoPrivate.h"
#import "WKDatePickerViewController.h"
#import "WKDateTimeInputControl.h"
#import "WKError.h"
#import "WKExtendedTextInputTraits.h"
#import "WKFocusedFormControlView.h"
#import "WKFormColorControl.h"
#import "WKFormSelectControl.h"
#import "WKFrameInfoInternal.h"
#import "WKHighlightLongPressGestureRecognizer.h"
#import "WKImageAnalysisGestureRecognizer.h"
#import "WKImagePreviewViewController.h"
#import "WKInspectorNodeSearchGestureRecognizer.h"
#import "WKNSURLExtras.h"
#import "WKPreviewActionItemIdentifiers.h"
#import "WKPreviewActionItemInternal.h"
#import "WKPreviewElementInfoInternal.h"
#import "WKQuickboardViewControllerDelegate.h"
#import "WKScrollView.h"
#import "WKSelectMenuListViewController.h"
#import "WKSyntheticFlagsChangedWebEvent.h"
#import "WKTapHighlightView.h"
#import "WKTextInputListViewController.h"
#import "WKTextInteractionWrapper.h"
#import "WKTextPlaceholder.h"
#import "WKTextSelectionRect.h"
#import "WKTimePickerViewController.h"
#import "WKTouchEventsGestureRecognizer.h"
#import "WKUIDelegatePrivate.h"
#import "WKWebViewConfiguration.h"
#import "WKWebViewConfigurationPrivate.h"
#import "WKWebViewIOS.h"
#import "WKWebViewInternal.h"
#import "WKWebViewPrivate.h"
#import "WKWebViewPrivateForTesting.h"
#import "WebAutocorrectionContext.h"
#import "WebAutocorrectionData.h"
#import "WebDataListSuggestionsDropdownIOS.h"
#import "WebEvent.h"
#import "WebFoundTextRange.h"
#import "WebIOSEventFactory.h"
#import "WebPageMessages.h"
#import "WebPageProxy.h"
#import "WebPageProxyMessages.h"
#import "WebProcessProxy.h"
#import "_WKActivatedElementInfoInternal.h"
#import "_WKDragActionsInternal.h"
#import "_WKElementAction.h"
#import "_WKElementActionInternal.h"
#import "_WKFocusedElementInfo.h"
#import "_WKInputDelegate.h"
#import "_WKTextInputContextInternal.h"
#import <CoreText/CTFont.h>
#import <CoreText/CTFontDescriptor.h>
#import <MobileCoreServices/UTCoreTypes.h>
#import <UniformTypeIdentifiers/UTCoreTypes.h>
#import <WebCore/AppHighlight.h>
#import <WebCore/ColorCocoa.h>
#import <WebCore/ColorSerialization.h>
#import <WebCore/CompositionHighlight.h>
#import <WebCore/DOMPasteAccess.h>
#import <WebCore/DataDetection.h>
#import <WebCore/FloatQuad.h>
#import <WebCore/FloatRect.h>
#import <WebCore/FontAttributeChanges.h>
#import <WebCore/InputMode.h>
#import <WebCore/KeyEventCodesIOS.h>
#import <WebCore/KeyboardScroll.h>
#import <WebCore/LocalizedStrings.h>
#import <WebCore/MIMETypeRegistry.h>
#import <WebCore/NodeIdentifier.h>
#import <WebCore/NotImplemented.h>
#import <WebCore/Pasteboard.h>
#import <WebCore/Path.h>
#import <WebCore/PathUtilities.h>
#import <WebCore/PlatformLayerIdentifier.h>
#import <WebCore/PlatformTextAlternatives.h>
#import <WebCore/PromisedAttachmentInfo.h>
#import <WebCore/ScrollTypes.h>
#import <WebCore/Scrollbar.h>
#import <WebCore/ShareData.h>
#import <WebCore/TextAlternativeWithRange.h>
#import <WebCore/TextAnimationTypes.h>
#import <WebCore/TextIndicator.h>
#import <WebCore/TextRecognitionResult.h>
#import <WebCore/TouchAction.h>
#import <WebCore/UTIRegistry.h>
#import <WebCore/UTIUtilities.h>
#import <WebCore/VisibleSelection.h>
#import <WebCore/WebCoreCALayerExtras.h>
#import <WebCore/WebEventPrivate.h>
#import <WebCore/WebTextIndicatorLayer.h>
#import <WebCore/WindowsKeyboardCodes.h>
#import <WebCore/WritingDirection.h>
#import <WebKit/WebSelectionRect.h> // FIXME: WebKit should not include WebKitLegacy headers!
#import <pal/spi/cg/CoreGraphicsSPI.h>
#import <pal/spi/cocoa/DataDetectorsCoreSPI.h>
#import <pal/spi/cocoa/LaunchServicesSPI.h>
#import <pal/spi/cocoa/NSAttributedStringSPI.h>
#import <pal/spi/cocoa/QuartzCoreSPI.h>
#import <pal/spi/ios/BarcodeSupportSPI.h>
#import <pal/spi/ios/GraphicsServicesSPI.h>
#import <pal/spi/ios/ManagedConfigurationSPI.h>
#import <pal/system/ios/UserInterfaceIdiom.h>
#import <ranges>
#import <wtf/BlockObjCExceptions.h>
#import <wtf/BlockPtr.h>
#import <wtf/CallbackAggregator.h>
#import <wtf/RuntimeApplicationChecks.h>
#import <wtf/Scope.h>
#import <wtf/SetForScope.h>
#import <wtf/StdLibExtras.h>
#import <wtf/SystemFree.h>
#import <wtf/WeakObjCPtr.h>
#import <wtf/cocoa/NSURLExtras.h>
#import <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#import <wtf/cocoa/TypeCastsCocoa.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/text/MakeString.h>
#import <wtf/text/TextStream.h>

#if ENABLE(DRAG_SUPPORT)
#import <WebCore/DragData.h>
#import <WebCore/DragItem.h>
#import <WebCore/PlatformPasteboard.h>
#import <WebCore/WebItemProviderPasteboard.h>
#if ENABLE(MODEL_PROCESS)
#import "ModelPresentationManagerProxy.h"
#endif
#endif

#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
#import <UIKit/_UILookupGestureRecognizer.h>
#endif

#if ENABLE(ATTACHMENT_ELEMENT)
#import "APIAttachment.h"
#endif

#if HAVE(PEPPER_UI_CORE)
#import "PepperUICoreSPI.h"
#endif

#if HAVE(AVKIT)
#import <pal/spi/cocoa/AVKitSPI.h>
#endif

#if HAVE(DIGITAL_CREDENTIALS_UI)
#import "WKDigitalCredentialsPicker.h"
#endif

#if ENABLE(WRITING_TOOLS)
#import "WKTextAnimationManagerIOS.h"
#endif

#if ENABLE(MODEL_PROCESS) || ENABLE(WRITING_TOOLS)
#import "WebKitSwiftSoftLink.h"
#endif

#import <pal/cocoa/VisionKitCoreSoftLink.h>
#import <pal/cocoa/TranslationUIServicesSoftLink.h>
#import <pal/ios/ManagedConfigurationSoftLink.h>
#import <pal/ios/QuickLookSoftLink.h>
#import <pal/spi/ios/DataDetectorsUISoftLink.h>

#if HAVE(AUTOCORRECTION_ENHANCEMENTS)
#define UIWKDocumentRequestAutocorrectedRanges (1 << 7)
#endif

#if USE(BROWSERENGINEKIT)

@interface WKUITextSelectionRect : UITextSelectionRect
+ (instancetype)selectionRectWithCGRect:(CGRect)rect;
@end

@implementation WKUITextSelectionRect {
    CGRect _selectionRect;
}

+ (instancetype)selectionRectWithCGRect:(CGRect)rect
{
    return adoptNS([[WKUITextSelectionRect alloc] initWithCGRect:rect]).autorelease();
}

- (instancetype)initWithCGRect:(CGRect)rect
{
    if (self = [super init])
        _selectionRect = rect;
    return self;
}

- (CGRect)rect
{
    return _selectionRect;
}

@end

#endif // USE(BROWSERENGINEKIT)

#if HAVE(LINK_PREVIEW) && USE(UICONTEXTMENU)
static NSString * const webkitShowLinkPreviewsPreferenceKey = @"WebKitShowLinkPreviews";
#endif

#if HAVE(UI_POINTER_INTERACTION)
static NSString * const pointerRegionIdentifier = @"WKPointerRegion";
static NSString * const editablePointerRegionIdentifier = @"WKEditablePointerRegion";

@interface WKContentView (WKUIPointerInteractionDelegate) <UIPointerInteractionDelegate>
@end
#endif

#if HAVE(PENCILKIT_TEXT_INPUT)
@interface WKContentView (WKUIIndirectScribbleInteractionDelegate) <UIIndirectScribbleInteractionDelegate>
@end
#endif

#if HAVE(PEPPER_UI_CORE)
#if HAVE(QUICKBOARD_CONTROLLER)
@interface WKContentView (QuickboardControllerSupport) <PUICQuickboardControllerDelegate>
@end
#endif // HAVE(QUICKBOARD_CONTROLLER)

@interface WKContentView (WatchSupport) <WKFocusedFormControlViewDelegate, WKSelectMenuListViewControllerDelegate, WKTextInputListViewControllerDelegate>
@end
#endif // HAVE(PEPPER_UI_CORE)

static void *WKContentViewKVOTransformContext = &WKContentViewKVOTransformContext;

#if ENABLE(IMAGE_ANALYSIS)

@interface WKContentView (ImageAnalysis) <WKImageAnalysisGestureRecognizerDelegate>
@end

#if USE(QUICK_LOOK)

@interface WKContentView (ImageAnalysisPreview) <QLPreviewControllerDelegate, QLPreviewControllerDataSource, QLPreviewItemDataProvider>
@end

#endif // USE(QUICK_LOOK)

#endif // ENABLE(IMAGE_ANALYSIS)

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

@interface WKContentView (ImageAnalysisInteraction) <VKCImageAnalysisInteractionDelegate>
@end

#endif

#if ENABLE(IMAGE_ANALYSIS)

static bool canAttemptTextRecognitionForNonImageElements(const WebKit::InteractionInformationAtPosition& information, const WebKit::WebPreferences& preferences)
{
    return preferences.textRecognitionInVideosEnabled() && information.isPausedVideo;
}

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

@interface AVPlayerViewController (Staging_86237428)
- (void)setImageAnalysis:(CocoaImageAnalysis *)analysis;
@end

#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

#endif // ENABLE(IMAGE_ANALYSIS)

#if HAVE(UI_TEXT_SELECTION_DISPLAY_INTERACTION)

@interface WKContentView (UITextSelectionDisplayInteraction) <UITextSelectionDisplayInteractionDelegate>
@end

#endif

namespace WebKit {
using namespace WebCore;

enum class IgnoreTapGestureReason : uint8_t {
    None,
    ToggleEditMenu,
    DeferToScrollView,
};

static NSArray<NSString *> *supportedPlainTextPasteboardTypes()
{
    static NeverDestroyed supportedTypes = [] {
        auto types = adoptNS([[NSMutableArray alloc] init]);
        [types addObject:@(WebCore::PasteboardCustomData::cocoaType().characters())];
        [types addObject:UTTypeURL.identifier];
        [types addObjectsFromArray:UIPasteboardTypeListString];
        return types;
    }();
    return supportedTypes.get().get();
}

static NSArray<NSString *> *supportedRichTextPasteboardTypesForPasteConfiguration()
{
    static NeverDestroyed supportedTypes = [] {
        auto types = adoptNS([[NSMutableArray alloc] init]);
        [types addObject:UTTypeWebArchive.identifier];
        [types addObjectsFromArray:UIPasteboardTypeListImage];
        [types addObjectsFromArray:supportedPlainTextPasteboardTypes()];
        return types;
    }();
    return supportedTypes.get().get();
}

static NSArray<NSString *> *supportedRichTextPasteboardTypes()
{
    static NeverDestroyed supportedTypes = [] {
        auto types = adoptNS([[NSMutableArray alloc] init]);
        [types addObject:WebCore::WebArchivePboardType];
        [types addObjectsFromArray:supportedRichTextPasteboardTypesForPasteConfiguration()];
        return types;
    }();
    return supportedTypes.get().get();
}

#if HAVE(UI_PASTE_CONFIGURATION)

static NSArray<NSString *> *supportedRichTextPasteboardTypesWithAttachmentsForPasteConfiguration()
{
    static NeverDestroyed supportedTypes = [] {
        auto types = adoptNS([[NSMutableArray alloc] init]);
        [types addObjectsFromArray:supportedRichTextPasteboardTypesForPasteConfiguration()];
        [types addObjectsFromArray:WebCore::Pasteboard::supportedFileUploadPasteboardTypes()];
        return types;
    }();
    return supportedTypes.get().get();
}

#endif // HAVE(UI_PASTE_CONFIGURATION)

WKSelectionDrawingInfo::WKSelectionDrawingInfo()
    : type(SelectionType::None)
{
}

WKSelectionDrawingInfo::WKSelectionDrawingInfo(const EditorState& editorState)
{
    if (editorState.selectionIsNone || (!editorState.selectionIsRange && !editorState.isContentEditable)) {
        type = SelectionType::None;
        return;
    }

    type = SelectionType::Range;
    if (!editorState.postLayoutData)
        return;

    auto& postLayoutData = *editorState.postLayoutData;
    auto& visualData = *editorState.visualData;
    caretRect = visualData.caretRectAtEnd;
    caretColor = postLayoutData.caretColor;
    selectionGeometries = visualData.selectionGeometries;
    selectionClipRect = visualData.selectionClipRect;
    enclosingLayerID = visualData.enclosingLayerID;
}

inline bool operator==(const WKSelectionDrawingInfo& a, const WKSelectionDrawingInfo& b)
{
    if (a.type != b.type)
        return false;

    if (a.type == WKSelectionDrawingInfo::SelectionType::Range) {
        if (a.caretRect != b.caretRect)
            return false;

        if (a.caretColor != b.caretColor)
            return false;

        if (a.selectionGeometries.size() != b.selectionGeometries.size())
            return false;

        for (unsigned i = 0; i < a.selectionGeometries.size(); ++i) {
            auto& aGeometry = a.selectionGeometries[i];
            auto& bGeometry = b.selectionGeometries[i];
            auto behavior = aGeometry.behavior();
            if (behavior != bGeometry.behavior())
                return false;

            if (behavior == WebCore::SelectionRenderingBehavior::CoalesceBoundingRects && aGeometry.rect() != bGeometry.rect())
                return false;

            if (behavior == WebCore::SelectionRenderingBehavior::UseIndividualQuads && aGeometry.quad() != bGeometry.quad())
                return false;
        }
    }

    if (a.type != WKSelectionDrawingInfo::SelectionType::None && a.selectionClipRect != b.selectionClipRect)
        return false;

    if (a.enclosingLayerID != b.enclosingLayerID)
        return false;

    return true;
}

static TextStream& operator<<(TextStream& stream, WKSelectionDrawingInfo::SelectionType type)
{
    switch (type) {
    case WKSelectionDrawingInfo::SelectionType::None: stream << "none"; break;
    case WKSelectionDrawingInfo::SelectionType::Range: stream << "range"; break;
    }
    
    return stream;
}

TextStream& operator<<(TextStream& stream, const WKSelectionDrawingInfo& info)
{
    TextStream::GroupScope group(stream);
    stream.dumpProperty("type"_s, info.type);
    stream.dumpProperty("caret rect"_s, info.caretRect);
    stream.dumpProperty("caret color"_s, info.caretColor);
    stream.dumpProperty("selection geometries"_s, info.selectionGeometries);
    stream.dumpProperty("selection clip rect"_s, info.selectionClipRect);
    stream.dumpProperty("layer"_s, info.enclosingLayerID);
    return stream;
}

#if ENABLE(IMAGE_ANALYSIS)

class ImageAnalysisGestureDeferralToken final : public RefCounted<ImageAnalysisGestureDeferralToken> {
public:
    static RefPtr<ImageAnalysisGestureDeferralToken> create(WKContentView *view)
    {
        return adoptRef(*new ImageAnalysisGestureDeferralToken(view));
    }

    ~ImageAnalysisGestureDeferralToken()
    {
        if (auto view = m_view.get())
            [view _endImageAnalysisGestureDeferral:m_shouldPreventTextSelection ? WebKit::ShouldPreventGestures::Yes : WebKit::ShouldPreventGestures::No];
    }

    void setShouldPreventTextSelection()
    {
        m_shouldPreventTextSelection = true;
    }

private:
    ImageAnalysisGestureDeferralToken(WKContentView *view)
        : m_view(view)
    {
    }

    WeakObjCPtr<WKContentView> m_view;
    bool m_shouldPreventTextSelection { false };
};

#endif // ENABLE(IMAGE_ANALYSIS)

enum class TextPositionAnchor : uint8_t {
    Start = 1 << 0,
    End = 1 << 1
};

enum class NotifyInputDelegate : bool { No, Yes };

} // namespace WebKit

@interface WKRelativeTextPosition : UITextPosition
- (instancetype)initWithAnchors:(OptionSet<WebKit::TextPositionAnchor>)start offset:(NSInteger)offset;
@property (nonatomic, readonly) OptionSet<WebKit::TextPositionAnchor> anchors;
@property (nonatomic, readonly, getter=isRelativeToStart) BOOL relativeToStart;
@property (nonatomic, readonly) NSInteger offset;
@end

@interface WKRelativeTextRange : UITextRange
- (instancetype)initWithStart:(UITextPosition *)start end:(UITextPosition *)end;
@property (nonatomic, readonly) WKRelativeTextPosition *start;
@property (nonatomic, readonly) WKRelativeTextPosition *end;
@end

constexpr float highlightDelay = 0.12;
constexpr float tapAndHoldDelay = 0.75;
constexpr CGFloat minimumTapHighlightRadius = 2.0;
constexpr double fasterTapSignificantZoomThreshold = 0.8;

@interface WKTextRange : UITextRange {
    CGRect _startRect;
    CGRect _endRect;
    BOOL _isNone;
    BOOL _isRange;
    BOOL _isEditable;
    NSArray<WKTextSelectionRect *>*_selectionRects;
    NSUInteger _selectedTextLength;
}
@property (nonatomic) CGRect startRect;
@property (nonatomic) CGRect endRect;
@property (nonatomic) BOOL isNone;
@property (nonatomic) BOOL isRange;
@property (nonatomic) BOOL isEditable;
@property (nonatomic) NSUInteger selectedTextLength;
@property (copy, nonatomic) NSArray<WKTextSelectionRect *> *selectionRects;

+ (WKTextRange *)textRangeWithState:(BOOL)isNone isRange:(BOOL)isRange isEditable:(BOOL)isEditable startRect:(CGRect)startRect endRect:(CGRect)endRect selectionRects:(NSArray *)selectionRects selectedTextLength:(NSUInteger)selectedTextLength;

@end

@interface WKTextPosition : UITextPosition

@property (nonatomic) CGRect positionRect;
@property (nonatomic) OptionSet<WebKit::TextPositionAnchor> anchors;

+ (WKTextPosition *)textPositionWithRect:(CGRect)positionRect;

@end

#if HAVE(UIFINDINTERACTION)

@interface WKFoundTextRange : UITextRange

@property (nonatomic, copy) NSString *frameIdentifier;
@property (nonatomic) NSUInteger order;

+ (WKFoundTextRange *)foundTextRangeWithWebFoundTextRange:(WebKit::WebFoundTextRange)range;

- (WebKit::WebFoundTextRange)webFoundTextRange;

@end

@interface WKFoundTextPosition : UITextPosition

@property (nonatomic) NSUInteger order;

@end

@interface WKFoundDOMTextRange : WKFoundTextRange

@property (nonatomic) NSUInteger location;
@property (nonatomic) NSUInteger length;

@end

@interface WKFoundDOMTextPosition : WKFoundTextPosition

@property (nonatomic) NSUInteger offset;

+ (WKFoundDOMTextPosition *)textPositionWithOffset:(NSUInteger)offset order:(NSUInteger)order;

@end

@interface WKFoundPDFTextRange : WKFoundTextRange

@property (nonatomic) NSUInteger startPage;
@property (nonatomic) NSUInteger startPageOffset;
@property (nonatomic) NSUInteger endPage;
@property (nonatomic) NSUInteger endPageOffset;

@end

@interface WKFoundPDFTextPosition : WKFoundTextPosition

@property (nonatomic) NSUInteger page;
@property (nonatomic) NSUInteger offset;

+ (WKFoundPDFTextPosition *)textPositionWithPage:(NSUInteger)page offset:(NSUInteger)offset;

@end

#endif

inline static UITextPosition *positionWithOffsetFrom(UITextPosition *position, NSInteger offset)
{
    if (!offset)
        return position;

    if (auto concretePosition = dynamic_objc_cast<WKTextPosition>(position); !concretePosition.anchors.isEmpty())
        return adoptNS([[WKRelativeTextPosition alloc] initWithAnchors:concretePosition.anchors offset:offset]).autorelease();

    if (auto relativePosition = dynamic_objc_cast<WKRelativeTextPosition>(position))
        return adoptNS([[WKRelativeTextPosition alloc] initWithAnchors:[relativePosition anchors] offset:offset + [relativePosition offset]]).autorelease();

    return nil;
}

inline static std::pair<OptionSet<WebKit::TextPositionAnchor>, NSInteger> anchorsAndOffset(UITextPosition *position)
{
    if (auto concretePosition = dynamic_objc_cast<WKTextPosition>(position); concretePosition.anchors)
        return { concretePosition.anchors, 0 };

    if (auto relativePosition = dynamic_objc_cast<WKRelativeTextPosition>(position))
        return { relativePosition.anchors, relativePosition.offset };

    return { { }, 0 };
}

inline static RetainPtr<NSString> textRelativeToSelectionStart(WKRelativeTextRange *range, const WebKit::EditorState::PostLayoutData& data, std::optional<char32_t> lastInsertedCharacterOverride)
{
    auto start = range.start;
    auto end = range.end;
    if (!start || !end)
        return nil;

    if (!start.relativeToStart || !end.relativeToStart)
        return nil;

    auto startOffset = start.offset;
    auto endOffset = end.offset;
    if (startOffset < -2 || endOffset > 1 || startOffset >= endOffset)
        return nil;

    StringBuilder string;
    string.reserveCapacity(endOffset - startOffset);
    auto appendIfNonZero = [&string](auto character) {
        if (character)
            string.append(character);
    };
    for (auto offset = startOffset; offset < endOffset; ++offset) {
        switch (offset) {
        case -2:
            appendIfNonZero(data.twoCharacterBeforeSelection);
            break;
        case -1: {
            appendIfNonZero(lastInsertedCharacterOverride.value_or(data.characterBeforeSelection));
            break;
        }
        case 0:
            appendIfNonZero(data.characterAfterSelection);
            break;
        }
    }
    return string.createNSString();
}

@implementation WKRelativeTextPosition

- (instancetype)initWithAnchors:(OptionSet<WebKit::TextPositionAnchor>)anchors offset:(NSInteger)offset
{
    if (self = [super init]) {
        _anchors = anchors;
        _offset = offset;
    }
    return self;
}

- (BOOL)isRelativeToStart
{
    return _anchors.contains(WebKit::TextPositionAnchor::Start);
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"WKRelativeTextPosition(%s, %d)", _anchors.contains(WebKit::TextPositionAnchor::Start) ? "start" : "end", static_cast<int>(_offset)];
}

@end

@implementation WKRelativeTextRange {
    RetainPtr<WKRelativeTextPosition> _start;
    RetainPtr<WKRelativeTextPosition> _end;
}

- (instancetype)initWithStart:(UITextPosition *)start end:(UITextPosition *)end
{
    if (self = [super init]) {
        auto [startAnchors, startOffset] = anchorsAndOffset(start);
        auto [endAnchors, endOffset] = anchorsAndOffset(end);
        if (!startAnchors.isEmpty() && !endAnchors.isEmpty()) {
            _start = adoptNS([[WKRelativeTextPosition alloc] initWithAnchors:startAnchors offset:startOffset]);
            _end = adoptNS([[WKRelativeTextPosition alloc] initWithAnchors:endAnchors offset:endOffset]);
        }
    }
    return self;
}

- (WKRelativeTextPosition *)start
{
    return _start.get();
}

- (WKRelativeTextPosition *)end
{
    return _end.get();
}

- (BOOL)isEmpty
{
    return [_start anchors] == [_end anchors] && [_start offset] == [_end offset];
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"WKRelativeTextRange(start=%@, end=%@)", _start.get(), _end.get()];
}

@end

@interface WKAutocorrectionRects : UIWKAutocorrectionRects
+ (WKAutocorrectionRects *)autocorrectionRectsWithFirstCGRect:(CGRect)firstRect lastCGRect:(CGRect)lastRect;
@end

@interface WKAutocorrectionContext : UIWKAutocorrectionContext
+ (WKAutocorrectionContext *)emptyAutocorrectionContext;
+ (WKAutocorrectionContext *)autocorrectionContextWithWebContext:(const WebKit::WebAutocorrectionContext&)context;
@end

@interface UIView (UIViewInternalHack)
+ (BOOL)_addCompletion:(void(^)(BOOL))completion;
@end

@interface WKFocusedElementInfo : NSObject <_WKFocusedElementInfo>
- (instancetype)initWithFocusedElementInformation:(const WebKit::FocusedElementInformation&)information isUserInitiated:(BOOL)isUserInitiated webView:(WKWebView *)webView userObject:(NSObject <NSSecureCoding> *)userObject;
@end

@implementation WKFormInputSession {
    WeakObjCPtr<WKContentView> _contentView;
    RetainPtr<WKFocusedElementInfo> _focusedElementInfo;
    RetainPtr<UIView> _customInputView;
    RetainPtr<UIView> _customInputAccessoryView;
    RetainPtr<NSArray<UITextSuggestion *>> _suggestions;
    BOOL _accessoryViewShouldNotShow;
    BOOL _forceSecureTextEntry;
    BOOL _requiresStrongPasswordAssistance;
}

- (instancetype)initWithContentView:(WKContentView *)view focusedElementInfo:(WKFocusedElementInfo *)elementInfo requiresStrongPasswordAssistance:(BOOL)requiresStrongPasswordAssistance
{
    if (!(self = [super init]))
        return nil;

    _contentView = view;
    _focusedElementInfo = elementInfo;
    _requiresStrongPasswordAssistance = requiresStrongPasswordAssistance;

    return self;
}

- (id <_WKFocusedElementInfo>)focusedElementInfo
{
    return _focusedElementInfo.get();
}

- (NSObject <NSSecureCoding> *)userObject
{
    return [_focusedElementInfo userObject];
}

- (BOOL)isValid
{
    return !!_contentView;
}

- (NSString *)accessoryViewCustomButtonTitle
{
    return [_contentView formAccessoryView].autoFillButtonItem.title;
}

- (void)setAccessoryViewCustomButtonTitle:(NSString *)title
{
    if (title.length)
        [[_contentView formAccessoryView] showAutoFillButtonWithTitle:title];
    else
        [[_contentView formAccessoryView] hideAutoFillButton];
    if (!PAL::currentUserInterfaceIdiomIsSmallScreen())
        [_contentView reloadInputViews];
}

- (BOOL)accessoryViewShouldNotShow
{
    return _accessoryViewShouldNotShow;
}

- (void)setAccessoryViewShouldNotShow:(BOOL)accessoryViewShouldNotShow
{
    if (_accessoryViewShouldNotShow == accessoryViewShouldNotShow)
        return;

    _accessoryViewShouldNotShow = accessoryViewShouldNotShow;
    [_contentView reloadInputViews];
}

- (BOOL)forceSecureTextEntry
{
    return _forceSecureTextEntry;
}

- (void)setForceSecureTextEntry:(BOOL)forceSecureTextEntry
{
    if (_forceSecureTextEntry == forceSecureTextEntry)
        return;

    _forceSecureTextEntry = forceSecureTextEntry;
    [_contentView reloadInputViews];
}

- (UIView *)customInputView
{
    return _customInputView.get();
}

- (void)setCustomInputView:(UIView *)customInputView
{
    if (customInputView == _customInputView)
        return;

    _customInputView = customInputView;
    [_contentView reloadInputViews];
}

- (UIView *)customInputAccessoryView
{
    return _customInputAccessoryView.get();
}

- (void)setCustomInputAccessoryView:(UIView *)customInputAccessoryView
{
    if (_customInputAccessoryView == customInputAccessoryView)
        return;

    _customInputAccessoryView = customInputAccessoryView;
    [_contentView reloadInputViews];
}

- (void)endEditing
{
    if ([_customInputView conformsToProtocol:@protocol(WKFormControl)])
        [(id<WKFormControl>)_customInputView.get() controlEndEditing];
}

- (NSArray<UITextSuggestion *> *)suggestions
{
    return _suggestions.get();
}

- (void)setSuggestions:(NSArray<UITextSuggestion *> *)suggestions
{
    if (suggestions == _suggestions || [suggestions isEqualToArray:_suggestions.get()])
        return;

    _suggestions = adoptNS([suggestions copy]);
    [_contentView updateTextSuggestionsForInputDelegate];
}

- (BOOL)requiresStrongPasswordAssistance
{
    return _requiresStrongPasswordAssistance;
}

- (void)invalidate
{
    [std::exchange(_contentView, nil) _provideSuggestionsToInputDelegate:nil];
}

- (void)reloadFocusedElementContextView
{
    [_contentView reloadContextViewForPresentedListViewController];
}

@end

@implementation WKFocusedElementInfo {
    WKInputType _type;
    RetainPtr<NSString> _value;
    BOOL _isUserInitiated;
    RetainPtr<NSObject <NSSecureCoding>> _userObject;
    RetainPtr<NSString> _placeholder;
    RetainPtr<NSString> _label;
    RetainPtr<WKFrameInfo> _frame;
}

- (instancetype)initWithFocusedElementInformation:(const WebKit::FocusedElementInformation&)information isUserInitiated:(BOOL)isUserInitiated webView:(WKWebView *)webView userObject:(NSObject <NSSecureCoding> *)userObject
{
    if (!(self = [super init]))
        return nil;

    switch (information.elementType) {
    case WebKit::InputType::ContentEditable:
        _type = WKInputTypeContentEditable;
        break;
    case WebKit::InputType::Text:
        _type = WKInputTypeText;
        break;
    case WebKit::InputType::Password:
        _type = WKInputTypePassword;
        break;
    case WebKit::InputType::TextArea:
        _type = WKInputTypeTextArea;
        break;
    case WebKit::InputType::Search:
        _type = WKInputTypeSearch;
        break;
    case WebKit::InputType::Email:
        _type = WKInputTypeEmail;
        break;
    case WebKit::InputType::URL:
        _type = WKInputTypeURL;
        break;
    case WebKit::InputType::Phone:
        _type = WKInputTypePhone;
        break;
    case WebKit::InputType::Number:
        _type = WKInputTypeNumber;
        break;
    case WebKit::InputType::NumberPad:
        _type = WKInputTypeNumberPad;
        break;
    case WebKit::InputType::Date:
        _type = WKInputTypeDate;
        break;
    case WebKit::InputType::DateTimeLocal:
        _type = WKInputTypeDateTimeLocal;
        break;
    case WebKit::InputType::Month:
        _type = WKInputTypeMonth;
        break;
    case WebKit::InputType::Week:
        _type = WKInputTypeWeek;
        break;
    case WebKit::InputType::Time:
        _type = WKInputTypeTime;
        break;
    case WebKit::InputType::Select:
        _type = WKInputTypeSelect;
        break;
    case WebKit::InputType::Drawing:
        _type = WKInputTypeDrawing;
        break;
    case WebKit::InputType::Color:
        _type = WKInputTypeColor;
        break;
    case WebKit::InputType::None:
        _type = WKInputTypeNone;
        break;
    }
    _value = information.value.createNSString().get();
    _isUserInitiated = isUserInitiated;
    _userObject = userObject;
    _placeholder = information.placeholder.createNSString().get();
    _label = information.label.createNSString().get();
    if (information.frame)
        _frame = wrapper(API::FrameInfo::create(WebKit::FrameInfoData { *information.frame }, [webView _page].get()));
    return self;
}

- (WKInputType)type
{
    return _type;
}

- (NSString *)value
{
    return _value.get();
}

- (BOOL)isUserInitiated
{
    return _isUserInitiated;
}

- (NSObject <NSSecureCoding> *)userObject
{
    return _userObject.get();
}

- (NSString *)label
{
    return _label.get();
}

- (NSString *)placeholder
{
    return _placeholder.get();
}

- (WKFrameInfo *)frame
{
    return _frame.get();
}

@end

#if ENABLE(DRAG_SUPPORT)

@interface WKDragSessionContext : NSObject
- (void)addTemporaryDirectory:(NSString *)temporaryDirectory;
- (void)cleanUpTemporaryDirectories;
@end

@implementation WKDragSessionContext {
    RetainPtr<NSMutableArray> _temporaryDirectories;
}

- (void)addTemporaryDirectory:(NSString *)temporaryDirectory
{
    if (!_temporaryDirectories)
        _temporaryDirectories = adoptNS([NSMutableArray new]);
    [_temporaryDirectories addObject:temporaryDirectory];
}

- (void)cleanUpTemporaryDirectories
{
    for (NSString *directory in _temporaryDirectories.get()) {
        NSError *error = nil;
        [[NSFileManager defaultManager] removeItemAtPath:directory error:&error];
        RELEASE_LOG(DragAndDrop, "Removed temporary download directory: %@ with error: %@", directory, error);
    }
    _temporaryDirectories = nil;
}

@end

static WKDragSessionContext *existingLocalDragSessionContext(id <UIDragSession> session)
{
    return dynamic_objc_cast<WKDragSessionContext>(session.localContext);
}

static WKDragSessionContext *ensureLocalDragSessionContext(id <UIDragSession> session)
{
    if (WKDragSessionContext *existingContext = existingLocalDragSessionContext(session))
        return existingContext;

    if (session.localContext) {
        RELEASE_LOG(DragAndDrop, "Overriding existing local context: %@ on session: %@", session.localContext, session);
        ASSERT_NOT_REACHED();
    }

    session.localContext = adoptNS([[WKDragSessionContext alloc] init]).get();
    return existingLocalDragSessionContext(session);
}

#endif // ENABLE(DRAG_SUPPORT)

@implementation UIGestureRecognizer (WKContentViewHelpers)

- (void)_wk_cancel
{
    if (!self.enabled)
        return;

    [self setEnabled:NO];
    [self setEnabled:YES];
}

- (BOOL)_wk_isTapAndAHalf
{
    return [NSStringFromClass(self.class) containsString:@"TapAndAHalfRecognizer"];
}

@end

@interface WKTargetedPreviewContainer : UIView
- (instancetype)initWithContentView:(WKContentView *)contentView NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithFrame:(CGRect)frame NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder *)coder NS_UNAVAILABLE;
@end

@implementation WKTargetedPreviewContainer {
    __weak WKContentView *_contentView;
}

- (instancetype)initWithContentView:(WKContentView *)contentView
{
    if (!(self = [super initWithFrame:CGRectZero]))
        return nil;

    _contentView = contentView;
    return self;
}

- (void)_didRemoveSubview:(UIView *)subview
{
    [super _didRemoveSubview:subview];

    if (self.subviews.count)
        return;

#if USE(UICONTEXTMENU)
    [_contentView _targetedPreviewContainerDidRemoveLastSubview:self];
#endif
}

@end

#if USE(UICONTEXTMENU)

@protocol UIContextMenuInteractionDelegate_LegacyAsyncSupport<NSObject>
- (void)_contextMenuInteraction:(UIContextMenuInteraction *)interaction configurationForMenuAtLocation:(CGPoint)location completion:(void(^)(UIContextMenuConfiguration *))completion;
@end

#endif

@interface WKContentView (WKInteractionPrivate)
- (void)accessibilitySpeakSelectionSetContent:(NSString *)string;
- (NSArray *)webSelectionRectsForSelectionGeometries:(const Vector<WebCore::SelectionGeometry>&)selectionRects;
- (void)_accessibilityDidGetSelectionRects:(NSArray *)selectionRects withGranularity:(UITextGranularity)granularity atOffset:(NSInteger)offset;
#if USE(UICONTEXTMENU)
- (void)_internalContextMenuInteraction:(UIContextMenuInteraction *)interaction configurationForMenuAtLocation:(CGPoint)location completion:(void(^)(UIContextMenuConfiguration *))completion;
#endif
@end

#define RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED() do { \
    if (self.shouldUseAsyncInteractions) [[unlikely]] { \
        RELEASE_LOG_ERROR(TextInteraction, "Received unexpected call to %s", __PRETTY_FUNCTION__); \
        RELEASE_ASSERT_NOT_REACHED(); \
    } \
} while (0)

@implementation WKContentView (WKInteraction)

- (BOOL)preventsPanningInXAxis
{
    return _preventsPanningInXAxis;
}

- (BOOL)preventsPanningInYAxis
{
    return _preventsPanningInYAxis;
}

- (WKFormInputSession *)_formInputSession
{
    return _formInputSession.get();
}

- (void)_createAndConfigureDoubleTapGestureRecognizer
{
    if (_doubleTapGestureRecognizer) {
        [self removeGestureRecognizer:_doubleTapGestureRecognizer.get()];
        [_doubleTapGestureRecognizer setDelegate:nil];
        [_doubleTapGestureRecognizer setGestureFailedTarget:nil action:nil];
    }

    _doubleTapGestureRecognizer = adoptNS([[WKSyntheticTapGestureRecognizer alloc] initWithTarget:self action:@selector(_doubleTapRecognized:)]);
    [_doubleTapGestureRecognizer setGestureFailedTarget:self action:@selector(_doubleTapDidFail:)];
    [_doubleTapGestureRecognizer setNumberOfTapsRequired:2];
    [_doubleTapGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_doubleTapGestureRecognizer.get()];
    [_singleTapGestureRecognizer requireGestureRecognizerToFail:_doubleTapGestureRecognizer.get()];
    [_keyboardDismissalGestureRecognizer requireGestureRecognizerToFail:_doubleTapGestureRecognizer.get()];
}

- (void)_createAndConfigureHighlightLongPressGestureRecognizer
{
    _highlightLongPressGestureRecognizer = adoptNS([[WKHighlightLongPressGestureRecognizer alloc] initWithTarget:self action:@selector(_highlightLongPressRecognized:)]);
    [_highlightLongPressGestureRecognizer setDelay:highlightDelay];
    [_highlightLongPressGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_highlightLongPressGestureRecognizer.get()];
}

- (void)_createAndConfigureLongPressGestureRecognizer
{
    _longPressGestureRecognizer = adoptNS([[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(_longPressRecognized:)]);
    [_longPressGestureRecognizer setDelay:tapAndHoldDelay];
    [_longPressGestureRecognizer setDelegate:self];
    [_longPressGestureRecognizer _setRequiresQuietImpulse:YES];
    [self addGestureRecognizer:_longPressGestureRecognizer.get()];
}

- (BOOL)shouldUseAsyncInteractions
{
    return _page->preferences().useAsyncUIKitInteractions();
}

- (BOOL)selectionHonorsOverflowScrolling
{
    return _page->preferences().selectionHonorsOverflowScrolling();
}

- (ScopeExit<Function<void()>>)makeTextSelectionViewsNonInteractiveForScope
{
    Vector<RetainPtr<UIView>> viewsToRestore;
    for (UIView *view in [_textInteractionWrapper managedTextSelectionViews]) {
        [view _wk_collectDescendantsIncludingSelf:viewsToRestore matching:^(UIView *view) {
            return view.userInteractionEnabled;
        }];
    }

    for (RetainPtr view : viewsToRestore)
        [view setUserInteractionEnabled:NO];

    return makeScopeExit(Function<void()> { [viewsToRestore = WTFMove(viewsToRestore)] {
        for (RetainPtr view : viewsToRestore)
            [view setUserInteractionEnabled:YES];
    } });
}

- (BOOL)_shouldUseUIContextMenuAsyncConfiguration
{
#if USE(BROWSERENGINEKIT)
    return self.shouldUseAsyncInteractions;
#else
    return NO;
#endif
}

- (BOOL)_shouldUseTextCursorDragAnimator
{
#if HAVE(UI_TEXT_CURSOR_DROP_POSITION_ANIMATOR)
    return YES;
#else
    return NO;
#endif
}

- (void)_updateRuntimeProtocolConformanceIfNeeded
{
    static bool hasUpdatedProtocolConformance = false;
    if (std::exchange(hasUpdatedProtocolConformance, true))
        return;

#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        RELEASE_LOG(TextInteraction, "Conforming to BrowserEngineKit text input protocol");
        class_addProtocol(self.class, @protocol(BETextInput));
    } else
#endif
    {
        RELEASE_LOG(TextInteraction, "Conforming to legacy UIKit interaction and text input protocols");
        auto legacyProtocols = std::array {
            @protocol(UIWKInteractionViewProtocol),
            @protocol(UITextInputPrivate),
            @protocol(UITextAutoscrolling),
            @protocol(UITextInputMultiDocument),
            @protocol(_UITextInputTranslationSupport),
        };
        for (auto protocol : legacyProtocols)
            class_addProtocol(self.class, protocol);
    }

#if USE(UICONTEXTMENU)
    if (!self._shouldUseUIContextMenuAsyncConfiguration) {
        auto legacyAsyncConfigurationSelector = @selector(_contextMenuInteraction:configurationForMenuAtLocation:completion:);
        auto internalAsyncConfigurationSelector = @selector(_internalContextMenuInteraction:configurationForMenuAtLocation:completion:);
        auto internalMethod = class_getInstanceMethod(self.class, internalAsyncConfigurationSelector);
        auto internalImplementation = method_getImplementation(internalMethod);
        class_addMethod(self.class, legacyAsyncConfigurationSelector, internalImplementation, method_getTypeEncoding(internalMethod));
    }
#endif // USE(UICONTEXTMENU)
}

- (void)setUpInteraction
{
    // If the page is not valid yet then delay interaction setup until the process is launched/relaunched.
    if (!_page->hasRunningProcess())
        return;

    if (_hasSetUpInteractions)
        return;

    _cachedHasCustomTintColor = std::nullopt;

    if (!_interactionViewsContainerView) {
        _interactionViewsContainerView = adoptNS([[UIView alloc] init]);
        [_interactionViewsContainerView layer].name = @"InteractionViewsContainer";
        [_interactionViewsContainerView setOpaque:NO];
        [_interactionViewsContainerView layer].anchorPoint = CGPointZero;
        [self.superview addSubview:_interactionViewsContainerView.get()];
    }

    _keyboardScrollingAnimator = adoptNS([[WKKeyboardScrollViewAnimator alloc] init]);
    [_keyboardScrollingAnimator setDelegate:self];

    [self.layer addObserver:self forKeyPath:@"transform" options:NSKeyValueObservingOptionInitial context:WKContentViewKVOTransformContext];

    _touchActionLeftSwipeGestureRecognizer = adoptNS([[UISwipeGestureRecognizer alloc] initWithTarget:nil action:nil]);
    [_touchActionLeftSwipeGestureRecognizer setDirection:UISwipeGestureRecognizerDirectionLeft];
    [_touchActionLeftSwipeGestureRecognizer setDelegate:self];
    [_touchActionLeftSwipeGestureRecognizer setName:@"Touch action swipe left"];
    [self addGestureRecognizer:_touchActionLeftSwipeGestureRecognizer.get()];

    _touchActionRightSwipeGestureRecognizer = adoptNS([[UISwipeGestureRecognizer alloc] initWithTarget:nil action:nil]);
    [_touchActionRightSwipeGestureRecognizer setDirection:UISwipeGestureRecognizerDirectionRight];
    [_touchActionRightSwipeGestureRecognizer setDelegate:self];
    [_touchActionRightSwipeGestureRecognizer setName:@"Touch action swipe right"];
    [self addGestureRecognizer:_touchActionRightSwipeGestureRecognizer.get()];

    _touchActionUpSwipeGestureRecognizer = adoptNS([[UISwipeGestureRecognizer alloc] initWithTarget:nil action:nil]);
    [_touchActionUpSwipeGestureRecognizer setDirection:UISwipeGestureRecognizerDirectionUp];
    [_touchActionUpSwipeGestureRecognizer setDelegate:self];
    [_touchActionUpSwipeGestureRecognizer setName:@"Touch action swipe up"];
    [self addGestureRecognizer:_touchActionUpSwipeGestureRecognizer.get()];

    _touchActionDownSwipeGestureRecognizer = adoptNS([[UISwipeGestureRecognizer alloc] initWithTarget:nil action:nil]);
    [_touchActionDownSwipeGestureRecognizer setDirection:UISwipeGestureRecognizerDirectionDown];
    [_touchActionDownSwipeGestureRecognizer setDelegate:self];
    [_touchActionDownSwipeGestureRecognizer setName:@"Touch action swipe down"];
    [self addGestureRecognizer:_touchActionDownSwipeGestureRecognizer.get()];

    _touchStartDeferringGestureRecognizerForImmediatelyResettableGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchStartDeferringGestureRecognizerForImmediatelyResettableGestures setName:@"Deferrer for touch start (immediate reset)"];

    _touchStartDeferringGestureRecognizerForDelayedResettableGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchStartDeferringGestureRecognizerForDelayedResettableGestures setName:@"Deferrer for touch start (delayed reset)"];

    _touchStartDeferringGestureRecognizerForSyntheticTapGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchStartDeferringGestureRecognizerForSyntheticTapGestures setName:@"Deferrer for touch start (synthetic tap)"];

    _touchEndDeferringGestureRecognizerForImmediatelyResettableGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchEndDeferringGestureRecognizerForImmediatelyResettableGestures setName:@"Deferrer for touch end (immediate reset)"];

    _touchEndDeferringGestureRecognizerForDelayedResettableGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchEndDeferringGestureRecognizerForDelayedResettableGestures setName:@"Deferrer for touch end (delayed reset)"];

    _touchEndDeferringGestureRecognizerForSyntheticTapGestures = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchEndDeferringGestureRecognizerForSyntheticTapGestures setName:@"Deferrer for touch end (synthetic tap)"];

    _touchMoveDeferringGestureRecognizer = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_touchMoveDeferringGestureRecognizer setName:@"Deferrer for touch move"];

#if ENABLE(IMAGE_ANALYSIS)
    _imageAnalysisDeferringGestureRecognizer = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [_imageAnalysisDeferringGestureRecognizer setName:@"Deferrer for image analysis"];
    [_imageAnalysisDeferringGestureRecognizer setImmediatelyFailsAfterTouchEnd:YES];
    [_imageAnalysisDeferringGestureRecognizer setEnabled:WebKit::isLiveTextAvailableAndEnabled()];
#endif

    for (WKDeferringGestureRecognizer *gesture in self.deferringGestures) {
        gesture.delegate = self;
        [self addGestureRecognizer:gesture];
    }

    if (_gestureRecognizerConsistencyEnforcer)
        _gestureRecognizerConsistencyEnforcer->reset();

    _touchEventGestureRecognizer = adoptNS([[WKTouchEventsGestureRecognizer alloc] initWithContentView:self]);
    [_touchEventGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_touchEventGestureRecognizer.get()];

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    [self setUpMouseGestureRecognizer];
#endif

#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
    _lookupGestureRecognizer = adoptNS([[_UILookupGestureRecognizer alloc] initWithTarget:self action:@selector(_lookupGestureRecognized:)]);
    [_lookupGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_lookupGestureRecognizer.get()];
#endif

    _singleTapGestureRecognizer = adoptNS([[WKSyntheticTapGestureRecognizer alloc] initWithTarget:self action:@selector(_singleTapRecognized:)]);
    [_singleTapGestureRecognizer setDelegate:self];
    [_singleTapGestureRecognizer setGestureIdentifiedTarget:self action:@selector(_singleTapIdentified:)];
    [_singleTapGestureRecognizer setResetTarget:self action:@selector(_singleTapDidReset:)];
    [_singleTapGestureRecognizer setSupportingTouchEventsGestureRecognizer:_touchEventGestureRecognizer.get()];
    [self addGestureRecognizer:_singleTapGestureRecognizer.get()];

#if ENABLE(MODEL_PROCESS)
    _modelInteractionPanGestureRecognizer = adoptNS([[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(_modelInteractionPanGestureRecognized:)]);
    [_modelInteractionPanGestureRecognizer setMinimumNumberOfTouches:1];
    [_modelInteractionPanGestureRecognizer setMaximumNumberOfTouches:1];
    [_modelInteractionPanGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_modelInteractionPanGestureRecognizer.get()];
#endif

    _nonBlockingDoubleTapGestureRecognizer = adoptNS([[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(_nonBlockingDoubleTapRecognized:)]);
    [_nonBlockingDoubleTapGestureRecognizer setNumberOfTapsRequired:2];
    [_nonBlockingDoubleTapGestureRecognizer setDelegate:self];
    [_nonBlockingDoubleTapGestureRecognizer setEnabled:NO];
    [self addGestureRecognizer:_nonBlockingDoubleTapGestureRecognizer.get()];

    _doubleTapGestureRecognizerForDoubleClick = adoptNS([[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(_doubleTapRecognizedForDoubleClick:)]);
    [_doubleTapGestureRecognizerForDoubleClick setNumberOfTapsRequired:2];
    [_doubleTapGestureRecognizerForDoubleClick setDelegate:self];
    [self addGestureRecognizer:_doubleTapGestureRecognizerForDoubleClick.get()];

    [self _createAndConfigureDoubleTapGestureRecognizer];

    _twoFingerDoubleTapGestureRecognizer = adoptNS([[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(_twoFingerDoubleTapRecognized:)]);
    [_twoFingerDoubleTapGestureRecognizer setNumberOfTapsRequired:2];
    [_twoFingerDoubleTapGestureRecognizer setNumberOfTouchesRequired:2];
    [_twoFingerDoubleTapGestureRecognizer setDelegate:self];
    [self addGestureRecognizer:_twoFingerDoubleTapGestureRecognizer.get()];

    [self _createAndConfigureHighlightLongPressGestureRecognizer];
    [self _createAndConfigureLongPressGestureRecognizer];

    [self _updateLongPressAndHighlightLongPressGestures];

#if ENABLE(DRAG_SUPPORT)
    [self setUpDragAndDropInteractions];
#endif

#if HAVE(UI_POINTER_INTERACTION)
    [self setUpPointerInteraction];
#endif

#if HAVE(PENCILKIT_TEXT_INPUT)
    [self setUpScribbleInteraction];
#endif

    _twoFingerSingleTapGestureRecognizer = adoptNS([[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(_twoFingerSingleTapGestureRecognized:)]);
    [_twoFingerSingleTapGestureRecognizer setAllowableMovement:60];
    [_twoFingerSingleTapGestureRecognizer _setAllowableSeparation:150];
    [_twoFingerSingleTapGestureRecognizer setNumberOfTapsRequired:1];
    [_twoFingerSingleTapGestureRecognizer setNumberOfTouchesRequired:2];
    [_twoFingerSingleTapGestureRecognizer setDelaysTouchesEnded:NO];
    [_twoFingerSingleTapGestureRecognizer setDelegate:self];
    [_twoFingerSingleTapGestureRecognizer setEnabled:!self.webView._editable];
    [self addGestureRecognizer:_twoFingerSingleTapGestureRecognizer.get()];

    _touchActionGestureRecognizer = adoptNS([[WKTouchActionGestureRecognizer alloc] initWithTouchActionDelegate:self]);
    [self addGestureRecognizer:_touchActionGestureRecognizer.get()];

    // FIXME: This should be called when we get notified that loading has completed.
    [self setUpTextSelectionAssistant];

    _keyboardDismissalGestureRecognizer = adoptNS([[WKScrollViewTrackingTapGestureRecognizer alloc] initWithTarget:self action:@selector(_keyboardDismissalGestureRecognized:)]);
    [_keyboardDismissalGestureRecognizer setNumberOfTapsRequired:1];
    [_keyboardDismissalGestureRecognizer setDelegate:self];
    [_keyboardDismissalGestureRecognizer setName:@"Keyboard dismissal tap gesture"];
    [_keyboardDismissalGestureRecognizer setEnabled:_page->preferences().keyboardDismissalGestureEnabled()];
    [self addGestureRecognizer:_keyboardDismissalGestureRecognizer.get()];

#if HAVE(UI_PASTE_CONFIGURATION)
    self.pasteConfiguration = adoptNS([[UIPasteConfiguration alloc] initWithAcceptableTypeIdentifiers:[&] {
        if (_page->preferences().attachmentElementEnabled())
            return WebKit::supportedRichTextPasteboardTypesWithAttachmentsForPasteConfiguration();
        return WebKit::supportedRichTextPasteboardTypesForPasteConfiguration();
    }()]).get();
#endif

#if HAVE(LINK_PREVIEW)
    [self _registerPreview];
#endif

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    [center addObserver:self selector:@selector(_willHideMenu:) name:UIMenuControllerWillHideMenuNotification object:nil];
ALLOW_DEPRECATED_DECLARATIONS_END
    
    [center addObserver:self selector:@selector(_keyboardDidRequestDismissal:) name:UIKeyboardPrivateDidRequestDismissalNotification object:nil];
    
    _actionSheetAssistant = adoptNS([[WKActionSheetAssistant alloc] initWithView:self]);
    [_actionSheetAssistant setDelegate:self];
    _smartMagnificationController = WebKit::SmartMagnificationController::create(self);
    _touchEventsCanPreventNativeGestures = YES;
    _isExpectingFastSingleTapCommit = NO;
    _potentialTapInProgress = NO;
    _isDoubleTapPending = NO;
    _showDebugTapHighlightsForFastClicking = [[NSUserDefaults standardUserDefaults] boolForKey:@"WebKitShowFastClickDebugTapHighlights"];
    _needsDeferredEndScrollingSelectionUpdate = NO;
    _isChangingFocus = NO;
    _isBlurringFocusedElement = NO;
#if USE(UICONTEXTMENU)
    _isDisplayingContextMenuWithAnimation = NO;
#endif
    _isUpdatingAccessoryView = NO;

    _dataListTextSuggestionsInputView = nil;
    _dataListTextSuggestions = nil;

#if ENABLE(PLATFORM_DRIVEN_TEXT_CHECKING)
    _textCheckingController = makeUnique<WebKit::TextCheckingController>(*_page);
#endif

    _autocorrectionContextNeedsUpdate = YES;

    _page->legacyMainFrameProcess().updateTextCheckerState();
    _page->setScreenIsBeingCaptured([self screenIsBeingCaptured]);

#if ENABLE(IMAGE_ANALYSIS)
    [self _setUpImageAnalysis];
#endif

    _sourceAnimationIDtoDestinationAnimationID = adoptNS([[NSMutableDictionary alloc] init]);

    _selectionInteractionType = SelectionInteractionType::None;

    _hasSetUpInteractions = YES;
}

- (void)cleanUpInteraction
{
    if (!_hasSetUpInteractions)
        return;

    _textInteractionWrapper = nil;
    
    [_actionSheetAssistant cleanupSheet];
    _actionSheetAssistant = nil;
    
    _smartMagnificationController = nil;
    _didAccessoryTabInitiateFocus = NO;
    _isChangingFocusUsingAccessoryTab = NO;
    _isExpectingFastSingleTapCommit = NO;
    _needsDeferredEndScrollingSelectionUpdate = NO;
    [_formInputSession invalidate];
    _formInputSession = nil;
    [_tapHighlightView removeFromSuperview];
    _lastOutstandingPositionInformationRequest = std::nullopt;
    _isWaitingOnPositionInformation = NO;

    _focusRequiresStrongPasswordAssistance = NO;
    _additionalContextForStrongPasswordAssistance = nil;
    _waitingForEditDragSnapshot = NO;

    _autocorrectionContextNeedsUpdate = YES;
    _lastAutocorrectionContext = { };

    _candidateViewNeedsUpdate = NO;
    _seenHardwareKeyDownInNonEditableElement = NO;

    _textInteractionDidChangeFocusedElement = NO;
    _activeTextInteractionCount = 0;

    _treatAsContentEditableUntilNextEditorStateUpdate = NO;
    _isHandlingActiveKeyEvent = NO;
    _isHandlingActivePressesEvent = NO;
    _isDeferringKeyEventsToInputMethod = NO;
    _usingMouseDragForSelection = NO;

    if (_interactionViewsContainerView) {
        [self.layer removeObserver:self forKeyPath:@"transform" context:WKContentViewKVOTransformContext];
        [_interactionViewsContainerView removeFromSuperview];
        _interactionViewsContainerView = nil;
    }

    _waitingForKeyboardAppearanceAnimationToStart = NO;
    _lastInsertedCharacterToOverrideCharacterBeforeSelection = std::nullopt;

    [_touchEventGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_touchEventGestureRecognizer.get()];

    for (WKDeferringGestureRecognizer *gesture in self.deferringGestures) {
        gesture.delegate = nil;
        [self removeGestureRecognizer:gesture];
    }

    if (_gestureRecognizerConsistencyEnforcer)
        _gestureRecognizerConsistencyEnforcer->reset();

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    [self removeInteraction:_mouseInteraction.get()];
#endif

#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
    [_lookupGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_lookupGestureRecognizer.get()];
#endif

    [_keyboardDismissalGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_keyboardDismissalGestureRecognizer.get()];

    [_singleTapGestureRecognizer setDelegate:nil];
    [_singleTapGestureRecognizer setGestureIdentifiedTarget:nil action:nil];
    [_singleTapGestureRecognizer setResetTarget:nil action:nil];
    [_singleTapGestureRecognizer setSupportingTouchEventsGestureRecognizer:nil];
    [self removeGestureRecognizer:_singleTapGestureRecognizer.get()];

#if ENABLE(MODEL_PROCESS)
    [_modelInteractionPanGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_modelInteractionPanGestureRecognizer.get()];
#endif

    [_highlightLongPressGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_highlightLongPressGestureRecognizer.get()];

    [_longPressGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_longPressGestureRecognizer.get()];

    [_doubleTapGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_doubleTapGestureRecognizer.get()];

    [_nonBlockingDoubleTapGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_nonBlockingDoubleTapGestureRecognizer.get()];

    [_doubleTapGestureRecognizerForDoubleClick setDelegate:nil];
    [self removeGestureRecognizer:_doubleTapGestureRecognizerForDoubleClick.get()];

    [_twoFingerDoubleTapGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_twoFingerDoubleTapGestureRecognizer.get()];

    [_twoFingerSingleTapGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_twoFingerSingleTapGestureRecognizer.get()];

    [self removeGestureRecognizer:_touchActionGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionLeftSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionRightSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionUpSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionDownSwipeGestureRecognizer.get()];

    _layerTreeTransactionIdAtLastInteractionStart = { };

#if USE(UICONTEXTMENU)
    [self _removeContextMenuHintContainerIfPossible];
#endif // USE(UICONTEXTMENU)

#if ENABLE(DRAG_SUPPORT)
    [existingLocalDragSessionContext(_dragDropInteractionState.dragSession()) cleanUpTemporaryDirectories];
    [self teardownDragAndDropInteractions];
#endif

#if HAVE(UI_POINTER_INTERACTION)
    [self removeInteraction:_pointerInteraction.get()];
    _pointerInteraction = nil;
    _pointerInteractionRegionNeedsUpdate = NO;
#endif

    _revealFocusedElementDeferrer = nullptr;

#if HAVE(PENCILKIT_TEXT_INPUT)
    [self cleanUpScribbleInteraction];
#endif

    _inspectorNodeSearchEnabled = NO;
    if (_inspectorNodeSearchGestureRecognizer) {
        [_inspectorNodeSearchGestureRecognizer setDelegate:nil];
        [self removeGestureRecognizer:_inspectorNodeSearchGestureRecognizer.get()];
        _inspectorNodeSearchGestureRecognizer = nil;
    }

#if HAVE(LINK_PREVIEW)
    [self _unregisterPreview];
#endif

    [self dismissPickersIfNeededWithReason:WebKit::PickerDismissalReason::ProcessExited];

    [self stopDeferringInputViewUpdatesForAllSources];
    _focusedElementInformation = { };
    
    [_keyboardScrollingAnimator invalidate];
    _keyboardScrollingAnimator = nil;

    _dataListTextSuggestionsInputView = nil;
    _dataListTextSuggestions = nil;

#if ENABLE(IMAGE_ANALYSIS)
    [self _tearDownImageAnalysis];
#endif

    [_webView _updateFixedContainerEdges:WebCore::FixedContainerEdges { }];

    [self _removeContainerForContextMenuHintPreviews];
    [self _removeContainerForDragPreviews];
    [self _removeContainerForDropPreviews];
    [self unsuppressSoftwareKeyboardUsingLastAutocorrectionContextIfNeeded];

    _hasSetUpInteractions = NO;
    _suppressSelectionAssistantReasons = { };

    [self _resetPanningPreventionFlags];
    [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
    [self _cancelPendingKeyEventHandlers:NO];

#if HAVE(UI_PASTE_CONFIGURATION)
    self.pasteConfiguration = nil;
#endif

    _selectionInteractionType = SelectionInteractionType::None;
    _lastSelectionChildScrollViewContentOffset = std::nullopt;
    _lastSiblingBeforeSelectionHighlight = nil;
    _waitingForEditorStateAfterScrollingSelectionContainer = NO;

    _cachedHasCustomTintColor = std::nullopt;
    _cachedSelectedTextRange = nil;
    _cachedSelectionContainerView = nil;
}

- (void)cleanUpInteractionPreviewContainers
{
    [self _removeContainerForContextMenuHintPreviews];
}

- (void)_cancelPendingKeyEventHandlers:(BOOL)handled
{
    for (auto [event, keyEventHandler] : std::exchange(_keyWebEventHandlers, { }))
        keyEventHandler(event.get(), handled);
}

- (void)_removeDefaultGestureRecognizers
{
    for (WKDeferringGestureRecognizer *gesture in self.deferringGestures)
        [self removeGestureRecognizer:gesture];
    [self removeGestureRecognizer:_touchEventGestureRecognizer.get()];
    [self removeGestureRecognizer:_singleTapGestureRecognizer.get()];
    [self removeGestureRecognizer:_highlightLongPressGestureRecognizer.get()];
    [self removeGestureRecognizer:_doubleTapGestureRecognizer.get()];
    [self removeGestureRecognizer:_nonBlockingDoubleTapGestureRecognizer.get()];
    [self removeGestureRecognizer:_doubleTapGestureRecognizerForDoubleClick.get()];
    [self removeGestureRecognizer:_twoFingerDoubleTapGestureRecognizer.get()];
    [self removeGestureRecognizer:_twoFingerSingleTapGestureRecognizer.get()];
#if ENABLE(MODEL_PROCESS)
    [self removeGestureRecognizer:_modelInteractionPanGestureRecognizer.get()];
#endif
#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    [self removeInteraction:_mouseInteraction.get()];
#endif
#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
    [self removeGestureRecognizer:_lookupGestureRecognizer.get()];
#endif
    [self removeGestureRecognizer:_touchActionGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionLeftSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionRightSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionUpSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_touchActionDownSwipeGestureRecognizer.get()];
    [self removeGestureRecognizer:_keyboardDismissalGestureRecognizer.get()];
}

- (void)_addDefaultGestureRecognizers
{
    for (WKDeferringGestureRecognizer *gesture in self.deferringGestures)
        [self addGestureRecognizer:gesture];
    [self addGestureRecognizer:_touchEventGestureRecognizer.get()];
    [self addGestureRecognizer:_singleTapGestureRecognizer.get()];
    [self addGestureRecognizer:_highlightLongPressGestureRecognizer.get()];
    [self addGestureRecognizer:_doubleTapGestureRecognizer.get()];
    [self addGestureRecognizer:_nonBlockingDoubleTapGestureRecognizer.get()];
    [self addGestureRecognizer:_doubleTapGestureRecognizerForDoubleClick.get()];
    [self addGestureRecognizer:_twoFingerDoubleTapGestureRecognizer.get()];
    [self addGestureRecognizer:_twoFingerSingleTapGestureRecognizer.get()];
#if ENABLE(MODEL_PROCESS)
    [self addGestureRecognizer:_modelInteractionPanGestureRecognizer.get()];
#endif
#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    [self addInteraction:_mouseInteraction.get()];
#endif
#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
    [self addGestureRecognizer:_lookupGestureRecognizer.get()];
#endif
    [self addGestureRecognizer:_touchActionGestureRecognizer.get()];
    [self addGestureRecognizer:_touchActionLeftSwipeGestureRecognizer.get()];
    [self addGestureRecognizer:_touchActionRightSwipeGestureRecognizer.get()];
    [self addGestureRecognizer:_touchActionUpSwipeGestureRecognizer.get()];
    [self addGestureRecognizer:_touchActionDownSwipeGestureRecognizer.get()];
    [self addGestureRecognizer:_keyboardDismissalGestureRecognizer.get()];
}

- (void)_didChangeLinkPreviewAvailability
{
    [self _updateLongPressAndHighlightLongPressGestures];
}

- (void)_updateLongPressAndHighlightLongPressGestures
{
#if HAVE(LINK_PREVIEW)
    // We only disable the highlight long press gesture in the case where UIContextMenu is available and we
    // also allow link previews, since the context menu interaction's gestures need to take precedence over
    // highlight long press gestures.
    [_highlightLongPressGestureRecognizer setEnabled:!self._shouldUseContextMenus || !self.webView.allowsLinkPreview];

    // We only enable the long press gesture in the case where the app is linked on iOS 12 or earlier (and
    // therefore prefers the legacy action sheet over context menus), and link previews are also enabled.
    [_longPressGestureRecognizer setEnabled:!self._shouldUseContextMenus && self.webView.allowsLinkPreview];
#else
    [_highlightLongPressGestureRecognizer setEnabled:NO];
    [_longPressGestureRecognizer setEnabled:NO];
#endif
}

- (UIView *)unscaledView
{
    return _interactionViewsContainerView.get();
}

- (UIScrollView *)_scroller
{
    return [_webView scrollView];
}

- (CGRect)unobscuredContentRect
{
    return _page->unobscuredContentRect();
}

#pragma mark - UITextAutoscrolling

- (void)startAutoscroll:(CGPoint)pointInDocument
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    _page->startAutoscrollAtPosition(pointInDocument);
}

- (void)cancelAutoscroll
{
    _selectionInteractionType = SelectionInteractionType::None;
    _page->cancelAutoscroll();
}

- (void)scrollSelectionToVisible:(BOOL)animated
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();
    // Used to scroll selection on keyboard up; we already scroll to visible.
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    if (context != WKContentViewKVOTransformContext) {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
        return;
    }

    ASSERT([keyPath isEqualToString:@"transform"]);
    ASSERT(object == self.layer);

    if ([UIView _isInAnimationBlock] && _page->editorState().selectionIsNone) {
        // If the utility views are not already visible, we don't want them to become visible during the animation since
        // they could not start from a reasonable state.
        // This is not perfect since views could also get updated during the animation, in practice this is rare and the end state
        // remains correct.
        [self _cancelInteraction];
        [_interactionViewsContainerView setHidden:YES];
        [UIView _addCompletion:^(BOOL){ [_interactionViewsContainerView setHidden:NO]; }];
    }

    [self _updateTapHighlight];

    if (_page->editorState().selectionIsNone && _lastSelectionDrawingInfo.type == WebKit::WKSelectionDrawingInfo::SelectionType::None)
        return;

    _selectionNeedsUpdate = YES;
    [self _updateChangedSelection:YES];
}

- (void)_enableInspectorNodeSearch
{
    _inspectorNodeSearchEnabled = YES;

    [self _cancelInteraction];

    [self _removeDefaultGestureRecognizers];
    _inspectorNodeSearchGestureRecognizer = adoptNS([[WKInspectorNodeSearchGestureRecognizer alloc] initWithTarget:self action:@selector(_inspectorNodeSearchRecognized:)]);
    [self addGestureRecognizer:_inspectorNodeSearchGestureRecognizer.get()];
}

- (void)_disableInspectorNodeSearch
{
    _inspectorNodeSearchEnabled = NO;

    [self _addDefaultGestureRecognizers];
    [self removeGestureRecognizer:_inspectorNodeSearchGestureRecognizer.get()];
    _inspectorNodeSearchGestureRecognizer = nil;
}

- (UIView *)hitTest:(CGPoint)point withEvent:(::UIEvent *)event
{
    for (UIView *subView in [_interactionViewsContainerView.get() subviews]) {
        UIView *hitView = [subView hitTest:[subView convertPoint:point fromView:self] withEvent:event];
        if (hitView) {
            LOG_WITH_STREAM(UIHitTesting, stream << self << "hitTest at " << WebCore::FloatPoint(point) << " found interaction view " << hitView);
            return hitView;
        }
    }

    LOG_WITH_STREAM(UIHitTesting, stream << "hit-testing WKContentView subviews " << [[self recursiveDescription] UTF8String]);
    UIView* hitView = [super hitTest:point withEvent:event];
    LOG_WITH_STREAM(UIHitTesting, stream << " found view " << [hitView class] << " " << (void*)hitView);
    return hitView;
}

- (const WebKit::InteractionInformationAtPosition&)positionInformation
{
    return _positionInformation;
}

- (void)setInputDelegate:(id <UITextInputDelegate>)inputDelegate
{
    _inputDelegate = inputDelegate;
}

- (id <UITextInputDelegate>)inputDelegate
{
    return _inputDelegate.getAutoreleased();
}

- (CGPoint)lastInteractionLocation
{
    return _lastInteractionLocation;
}

- (BOOL)shouldHideSelectionInFixedPositionWhenScrolling
{
    if (self.selectionHonorsOverflowScrolling)
        return NO;

    if (_isEditable)
        return _focusedElementInformation.insideFixedPosition;

    auto& editorState = _page->editorState();
    return editorState.hasPostLayoutData() && editorState.selectionIsRange && editorState.postLayoutData->insideFixedPosition;
}

- (BOOL)isEditable
{
    return _isEditable;
}

- (BOOL)setIsEditable:(BOOL)isEditable
{
    if (isEditable == _isEditable)
        return NO;

    _isEditable = isEditable;
    return YES;
}

- (void)_endEditing
{
    [_inputPeripheral endEditing];
    [_formInputSession endEditing];
    [_dataListTextSuggestionsInputView controlEndEditing];
}

- (void)_cancelPreviousResetInputViewDeferralRequest
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(stopDeferringInputViewUpdatesForAllSources) object:nil];
}

- (void)_scheduleResetInputViewDeferralAfterBecomingFirstResponder
{
    [self _cancelPreviousResetInputViewDeferralRequest];

    const NSTimeInterval inputViewDeferralWatchdogTimerDuration = 0.5;
    [self performSelector:@selector(stopDeferringInputViewUpdatesForAllSources) withObject:self afterDelay:inputViewDeferralWatchdogTimerDuration];
}

- (BOOL)canBecomeFirstResponder
{
    return _becomingFirstResponder;
}

- (BOOL)canBecomeFirstResponderForWebView
{
    if (_resigningFirstResponder)
        return NO;
    // We might want to return something else
    // if we decide to enable/disable interaction programmatically.
    return YES;
}

- (BOOL)becomeFirstResponder
{
    return [_webView becomeFirstResponder];
}

- (BOOL)becomeFirstResponderForWebView
{
    if (_resigningFirstResponder)
        return NO;

    [self startDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::BecomeFirstResponder];

    BOOL didBecomeFirstResponder;
    {
        SetForScope becomingFirstResponder { _becomingFirstResponder, YES };
        didBecomeFirstResponder = [super becomeFirstResponder];
    }

    if (didBecomeFirstResponder) {
        _page->installActivityStateChangeCompletionHandler([weakSelf = WeakObjCPtr<WKContentView>(self)] {
            if (!weakSelf)
                return;

            auto strongSelf = weakSelf.get();
            [strongSelf stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::BecomeFirstResponder];
        });

#if ENABLE(GAMEPAD)
        WebKit::UIGamepadProvider::singleton().viewBecameActive(*_page);
#endif

        _page->activityStateDidChange(WebCore::ActivityState::IsFocused, WebKit::WebPageProxy::ActivityStateChangeDispatchMode::Immediate);

        if (self._shouldActivateSelectionAfterBecomingFirstResponder) {
            _page->callAfterNextPresentationUpdate([weakSelf = WeakObjCPtr<WKContentView>(self)] {
                RetainPtr strongSelf = weakSelf.get();
                if (![strongSelf _shouldActivateSelectionAfterBecomingFirstResponder])
                    return;

                [strongSelf->_textInteractionWrapper setNeedsSelectionUpdate];
                [strongSelf->_textInteractionWrapper activateSelection];
            });
        }

        [self _scheduleResetInputViewDeferralAfterBecomingFirstResponder];
    } else
        [self stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::BecomeFirstResponder];

    return didBecomeFirstResponder;
}

- (BOOL)_shouldActivateSelectionAfterBecomingFirstResponder
{
    return self.canShowNonEmptySelectionView || (!_suppressSelectionAssistantReasons && _activeTextInteractionCount);
}

- (BOOL)resignFirstResponder
{
    return [_webView resignFirstResponder];
}

typedef NS_ENUM(NSInteger, EndEditingReason) {
    EndEditingReasonAccessoryDone,
    EndEditingReasonResigningFirstResponder,
};

- (void)endEditingAndUpdateFocusAppearanceWithReason:(EndEditingReason)reason
{
    if (!self.webView._retainingActiveFocusedState) {
        // We need to complete the editing operation before we blur the element.
        [self _endEditing];

        auto shouldBlurFocusedElement = [&] {
            if (_keyboardDidRequestDismissal)
                return true;

            if (self._shouldUseLegacySelectPopoverDismissalBehavior)
                return true;

            if (reason == EndEditingReasonAccessoryDone) {
                if (_focusRequiresStrongPasswordAssistance)
                    return true;

                if (PAL::currentUserInterfaceIdiomIsSmallScreen())
                    return true;
            }

            return false;
        };

        if (shouldBlurFocusedElement()) {
            _page->blurFocusedElement();
            // Don't wait for WebPageProxy::blurFocusedElement() to round-trip back to us to hide the keyboard
            // because we know that the user explicitly requested us to do so.
            [self _elementDidBlur];
        }
    }

    [self _cancelInteraction];

    BOOL shouldDeactivateSelection = [&]() -> BOOL {
#if PLATFORM(MACCATALYST)
        if (reason == EndEditingReasonResigningFirstResponder) {
            // This logic is based on a similar check on macOS (in WebViewImpl::resignFirstResponder()) to
            // maintain the active selection when resigning first responder, if the new responder is in a
            // modal popover or panel.
            // FIXME: Ideally, this would additionally check that the new first responder corresponds to
            // a view underneath this view controller; however, there doesn't seem to be any way of doing
            // so at the moment. In lieu of this, we can at least check that the web view itself isn't
            // inside the popover.
            auto controller = self._wk_viewControllerForFullScreenPresentation;
            return [self isDescendantOfView:controller.viewIfLoaded] || controller.modalPresentationStyle != UIModalPresentationPopover;
        }
#endif // PLATFORM(MACCATALYST)
        return YES;
    }();

    if (shouldDeactivateSelection)
        [_textInteractionWrapper deactivateSelection];

    [self stopDeferringInputViewUpdatesForAllSources];
}

- (BOOL)resignFirstResponderForWebView
{
    // FIXME: Maybe we should call resignFirstResponder on the superclass
    // and do nothing if the return value is NO.

    SetForScope resigningFirstResponderScope { _resigningFirstResponder, YES };

    [self endEditingAndUpdateFocusAppearanceWithReason:EndEditingReasonResigningFirstResponder];

    // If the user explicitly dismissed the keyboard then we will lose first responder
    // status only to gain it back again. Just don't resign in that case.
    if (_keyboardDidRequestDismissal) {
        _keyboardDidRequestDismissal = NO;
        return NO;
    }

    bool superDidResign = [super resignFirstResponder];

    if (superDidResign) {
        [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
        _page->activityStateDidChange(WebCore::ActivityState::IsFocused, WebKit::WebPageProxy::ActivityStateChangeDispatchMode::Immediate);

#if ENABLE(GAMEPAD)
        WebKit::UIGamepadProvider::singleton().viewBecameInactive(*_page);
#endif

        _isHandlingActiveKeyEvent = NO;
        _isHandlingActivePressesEvent = NO;

        if (!_keyWebEventHandlers.isEmpty()) {
            RunLoop::mainSingleton().dispatch([weakSelf = WeakObjCPtr<WKContentView>(self)] {
                RetainPtr strongSelf = weakSelf.get();
                if (!strongSelf || [strongSelf isFirstResponder])
                    return;
                [strongSelf _cancelPendingKeyEventHandlers:YES];
            });
        }
    }

    return superDidResign;
}

- (void)cancelPointersForGestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
{
    NSMapTable<NSNumber *, UITouch *> *activeTouches = [_touchEventGestureRecognizer activeTouchesByIdentifier];
    for (NSNumber *touchIdentifier in activeTouches) {
        UITouch *touch = [activeTouches objectForKey:touchIdentifier];
        if ([touch.gestureRecognizers containsObject:gestureRecognizer])
            _page->cancelPointer([touchIdentifier unsignedIntValue], WebCore::roundedIntPoint([touch locationInView:self]));
    }
}

- (std::optional<unsigned>)activeTouchIdentifierForGestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
{
    NSMapTable<NSNumber *, UITouch *> *activeTouches = [_touchEventGestureRecognizer activeTouchesByIdentifier];
    for (NSNumber *touchIdentifier in activeTouches) {
        UITouch *touch = [activeTouches objectForKey:touchIdentifier];
        if ([touch.gestureRecognizers containsObject:gestureRecognizer])
            return [touchIdentifier unsignedIntValue];
    }
    return std::nullopt;
}

- (BOOL)_touchEventsMustRequireGestureRecognizerToFail:(UIGestureRecognizer *)gestureRecognizer
{
    auto webView = self.webView;
    if ([webView _isNavigationSwipeGestureRecognizer:gestureRecognizer])
        return YES;

    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([webView UIDelegate]);
    return gestureRecognizer.view == webView && [uiDelegate respondsToSelector:@selector(_webView:touchEventsMustRequireGestureRecognizerToFail:)]
        && [uiDelegate _webView:webView touchEventsMustRequireGestureRecognizerToFail:gestureRecognizer];
}

- (BOOL)_gestureRecognizerCanBePreventedByTouchEvents:(UIGestureRecognizer *)gestureRecognizer
{
    id<WKUIDelegatePrivate> uiDelegate = static_cast<id<WKUIDelegatePrivate>>([self.webView UIDelegate]);
    return [uiDelegate respondsToSelector:@selector(_webView:gestureRecognizerCanBePreventedByTouchEvents:)] && [uiDelegate _webView:self.webView gestureRecognizerCanBePreventedByTouchEvents:gestureRecognizer];
}

- (void)_touchEventsRecognized
{
    if (!_page->hasRunningProcess())
        return;

    auto& lastTouchEvent = [_touchEventGestureRecognizer lastTouchEvent];
    _lastInteractionLocation = lastTouchEvent.locationInRootViewCoordinates;

    if (lastTouchEvent.type == WebKit::WKTouchEventType::Begin) {
        if (!_failedTouchStartDeferringGestures)
            _failedTouchStartDeferringGestures = { { } };

        [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
        _layerTreeTransactionIdAtLastInteractionStart = downcast<WebKit::RemoteLayerTreeDrawingAreaProxy>(*_page->drawingArea()).lastCommittedMainFrameLayerTreeTransactionID();

#if ENABLE(TOUCH_EVENTS)
        _page->didBeginTouchPoint(lastTouchEvent.locationInRootViewCoordinates);
#endif

        WebKit::InteractionInformationRequest positionInformationRequest { WebCore::IntPoint(_lastInteractionLocation) };
        [self doAfterPositionInformationUpdate:[assistant = WeakObjCPtr<WKActionSheetAssistant>(_actionSheetAssistant.get())] (WebKit::InteractionInformationAtPosition information) {
            [assistant interactionDidStartWithPositionInformation:information];
        } forRequest:positionInformationRequest];
    }

#if ENABLE(TOUCH_EVENTS)
    WebKit::NativeWebTouchEvent nativeWebTouchEvent { lastTouchEvent, [_touchEventGestureRecognizer modifierFlags] };
    nativeWebTouchEvent.setCanPreventNativeGestures(_touchEventsCanPreventNativeGestures || [_touchEventGestureRecognizer isDefaultPrevented]);

    [self _handleTouchActionsForTouchEvent:nativeWebTouchEvent];

    if (_touchEventsCanPreventNativeGestures)
        _page->handlePreventableTouchEvent(nativeWebTouchEvent);
    else
        _page->handleUnpreventableTouchEvent(nativeWebTouchEvent);

    if (nativeWebTouchEvent.allTouchPointsAreReleased()) {
        _touchEventsCanPreventNativeGestures = YES;

        if (!_page->isScrollingOrZooming())
            [self _resetPanningPreventionFlags];

        if (nativeWebTouchEvent.isPotentialTap() && self.hasHiddenContentEditable && self._hasFocusedElement && !self.window.keyWindow)
            [self.window makeKeyWindow];

        auto stopDeferringNativeGesturesIfNeeded = [] (WKDeferringGestureRecognizer *gestureRecognizer) {
            if (gestureRecognizer.state == UIGestureRecognizerStatePossible)
                gestureRecognizer.state = UIGestureRecognizerStateFailed;
        };

        if (!_page->isHandlingPreventableTouchStart()) {
            for (WKDeferringGestureRecognizer *gestureRecognizer in self._touchStartDeferringGestures)
                stopDeferringNativeGesturesIfNeeded(gestureRecognizer);
        }

        if (!_page->isHandlingPreventableTouchMove())
            stopDeferringNativeGesturesIfNeeded(_touchMoveDeferringGestureRecognizer.get());

        if (!_page->isHandlingPreventableTouchEnd()) {
            for (WKDeferringGestureRecognizer *gestureRecognizer in self._touchEndDeferringGestures)
                stopDeferringNativeGesturesIfNeeded(gestureRecognizer);
        }

        _failedTouchStartDeferringGestures = std::nullopt;
    }
#endif // ENABLE(TOUCH_EVENTS)
}

#if ENABLE(TOUCH_EVENTS)
- (void)_handleTouchActionsForTouchEvent:(const WebKit::NativeWebTouchEvent&)touchEvent
{
    auto* scrollingCoordinator = downcast<WebKit::RemoteScrollingCoordinatorProxyIOS>(_page->scrollingCoordinatorProxy());
    if (!scrollingCoordinator)
        return;

    for (const auto& touchPoint : touchEvent.touchPoints()) {
        auto phase = touchPoint.phase();
        if (phase == WebKit::WebPlatformTouchPoint::State::Pressed) {
            auto touchActions = WebKit::touchActionsForPoint(self, touchPoint.locationInRootView());
            LOG_WITH_STREAM(UIHitTesting, stream << "touchActionsForPoint " << touchPoint.locationInRootView() << " found " << touchActions);
            if (!touchActions || touchActions.contains(WebCore::TouchAction::Auto))
                continue;
            [_touchActionGestureRecognizer setTouchActions:touchActions forTouchIdentifier:touchPoint.identifier()];
            scrollingCoordinator->setTouchActionsForTouchIdentifier(touchActions, touchPoint.identifier());
            _preventsPanningInXAxis = !touchActions.containsAny({ WebCore::TouchAction::PanX, WebCore::TouchAction::Manipulation });
            _preventsPanningInYAxis = !touchActions.containsAny({ WebCore::TouchAction::PanY, WebCore::TouchAction::Manipulation });
        } else if (phase == WebKit::WebPlatformTouchPoint::State::Released || phase == WebKit::WebPlatformTouchPoint::State::Cancelled) {
            [_touchActionGestureRecognizer clearTouchActionsForTouchIdentifier:touchPoint.identifier()];
            scrollingCoordinator->clearTouchActionsForTouchIdentifier(touchPoint.identifier());
        }
    }
}
#endif // ENABLE(TOUCH_EVENTS)

- (BOOL)_isTouchActionSwipeGestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
{
    return gestureRecognizer == _touchActionLeftSwipeGestureRecognizer || gestureRecognizer == _touchActionRightSwipeGestureRecognizer || gestureRecognizer == _touchActionUpSwipeGestureRecognizer || gestureRecognizer == _touchActionDownSwipeGestureRecognizer;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceiveEvent:(UIEvent *)event
{
    if ([self _isTouchActionSwipeGestureRecognizer:gestureRecognizer])
        return event.type != UIEventTypeScroll;
    return YES;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceiveTouch:(UITouch *)touch
{
#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
    if (gestureRecognizer == _lookupGestureRecognizer)
        return YES;
#endif

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    auto isMouseGestureRecognizer = gestureRecognizer == [_mouseInteraction mouseTouchGestureRecognizer];
    if (!isMouseGestureRecognizer && [_mouseInteraction mouseTouch] == touch && touch._isPointerTouch)
        return NO;

    if (isMouseGestureRecognizer && !touch._isPointerTouch)
        return NO;

    if (gestureRecognizer == _doubleTapGestureRecognizer || gestureRecognizer == _nonBlockingDoubleTapGestureRecognizer)
        return touch.type != UITouchTypeIndirectPointer;
#endif

    if ([self _isTouchActionSwipeGestureRecognizer:gestureRecognizer]) {
        // We update the enabled state of the various swipe gesture recognizers such that if we have a unidirectional touch-action
        // specified (only pan-x or only pan-y) we enable the two recognizers in the opposite axis to prevent scrolling from starting
        // if the initial gesture is such a swipe. Since the recognizers are specified to use a single finger for recognition, we don't
        // need to worry about the case where there may be more than a single touch for a given UIScrollView.
        auto touchLocation = [touch locationInView:self];
        auto touchActions = WebKit::touchActionsForPoint(self, WebCore::roundedIntPoint(touchLocation));
        LOG_WITH_STREAM(UIHitTesting, stream << "gestureRecognizer:shouldReceiveTouch: at " << WebCore::FloatPoint(touchLocation) << " - touch actions " << touchActions);
        if (gestureRecognizer == _touchActionLeftSwipeGestureRecognizer || gestureRecognizer == _touchActionRightSwipeGestureRecognizer)
            return touchActions == WebCore::TouchAction::PanY;
        return touchActions == WebCore::TouchAction::PanX;
    }

    return YES;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceivePress:(UIPress *)press
{
    return YES;
}

#pragma mark - WKTouchActionGestureRecognizerDelegate implementation

- (BOOL)gestureRecognizerMayPanWebView:(UIGestureRecognizer *)gestureRecognizer
{
    // The gesture recognizer is the main UIScrollView's UIPanGestureRecognizer.
    if (gestureRecognizer == [_webView scrollView].panGestureRecognizer)
        return YES;

    // The gesture recognizer is a child UIScrollView's UIPanGestureRecognizer created by WebKit.
    if (gestureRecognizer.view && [gestureRecognizer.view isKindOfClass:[WKChildScrollView class]])
        return YES;

    return NO;
}

- (BOOL)gestureRecognizerMayPinchToZoomWebView:(UIGestureRecognizer *)gestureRecognizer
{
    // The gesture recognizer is the main UIScrollView's UIPinchGestureRecognizer.
    if (gestureRecognizer == [_webView scrollView].pinchGestureRecognizer)
        return YES;

    // The gesture recognizer is another UIPinchGestureRecognizer known to lead to pinch-to-zoom.
    if ([gestureRecognizer isKindOfClass:[UIPinchGestureRecognizer class]]) {
        if (auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate)) {
            if ([uiDelegate respondsToSelector:@selector(_webView:gestureRecognizerCouldPinch:)])
                return [uiDelegate _webView:self.webView gestureRecognizerCouldPinch:gestureRecognizer];
        }
    }
    return NO;
}

- (BOOL)gestureRecognizerMayDoubleTapToZoomWebView:(UIGestureRecognizer *)gestureRecognizer
{
    return gestureRecognizer == _doubleTapGestureRecognizer || gestureRecognizer == _twoFingerDoubleTapGestureRecognizer;
}

- (NSMapTable<NSNumber *, UITouch *> *)touchActionActiveTouches
{
    return [_touchEventGestureRecognizer activeTouchesByIdentifier];
}

- (void)_resetPanningPreventionFlags
{
    _preventsPanningInXAxis = NO;
    _preventsPanningInYAxis = NO;
}

- (void)_inspectorNodeSearchRecognized:(UIGestureRecognizer *)gestureRecognizer
{
    ASSERT(_inspectorNodeSearchEnabled);
    [self _resetIsDoubleTapPending];

    CGPoint point = [gestureRecognizer locationInView:self];

    switch (gestureRecognizer.state) {
    case UIGestureRecognizerStateBegan:
    case UIGestureRecognizerStateChanged:
        _page->inspectorNodeSearchMovedToPosition(point);
        break;
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled:
    default: // To ensure we turn off node search.
        _page->inspectorNodeSearchEndedAtPosition(point);
        break;
    }
}

static WebCore::FloatQuad inflateQuad(const WebCore::FloatQuad& quad, float inflateSize)
{
    // We sort the output points like this (as expected by the highlight view):
    //  p2------p3
    //  |       |
    //  p1------p4

    // 1) Sort the points horizontally.
    std::array points { quad.p1(), quad.p4(), quad.p2(), quad.p3() };
    if (points[0].x() > points[1].x())
        std::swap(points[0], points[1]);
    if (points[2].x() > points[3].x())
        std::swap(points[2], points[3]);

    if (points[0].x() > points[2].x())
        std::swap(points[0], points[2]);
    if (points[1].x() > points[3].x())
        std::swap(points[1], points[3]);

    if (points[1].x() > points[2].x())
        std::swap(points[1], points[2]);

    // 2) Swap them vertically to have the output points [p2, p1, p3, p4].
    if (points[1].y() < points[0].y())
        std::swap(points[0], points[1]);
    if (points[3].y() < points[2].y())
        std::swap(points[2], points[3]);

    // 3) Adjust the positions.
    points[0].move(-inflateSize, -inflateSize);
    points[1].move(-inflateSize, inflateSize);
    points[2].move(inflateSize, -inflateSize);
    points[3].move(inflateSize, inflateSize);

    return WebCore::FloatQuad(points[1], points[0], points[2], points[3]);
}

#if ENABLE(TOUCH_EVENTS)
- (void)_touchEvent:(const WebKit::WebTouchEvent&)touchEvent preventsNativeGestures:(BOOL)preventsNativeGesture
{
    if (!preventsNativeGesture || ![_touchEventGestureRecognizer isDispatchingTouchEvents])
        return;

    _longPressCanClick = NO;
    _touchEventsCanPreventNativeGestures = NO;
    [_touchEventGestureRecognizer setDefaultPrevented:YES];
}
#endif

- (WKTouchEventsGestureRecognizer *)touchEventGestureRecognizer
{
    return _touchEventGestureRecognizer.get();
}

- (WebKit::GestureRecognizerConsistencyEnforcer&)gestureRecognizerConsistencyEnforcer
{
    if (!_gestureRecognizerConsistencyEnforcer)
        lazyInitialize(_gestureRecognizerConsistencyEnforcer, makeUniqueWithoutRefCountedCheck<WebKit::GestureRecognizerConsistencyEnforcer>(self));

    return *_gestureRecognizerConsistencyEnforcer;
}

- (NSArray<WKDeferringGestureRecognizer *> *)deferringGestures
{
    auto gestures = [NSMutableArray arrayWithCapacity:7];
    [gestures addObjectsFromArray:self._touchStartDeferringGestures];
    [gestures addObjectsFromArray:self._touchEndDeferringGestures];
    if (_touchMoveDeferringGestureRecognizer)
        [gestures addObject:_touchMoveDeferringGestureRecognizer.get()];
#if ENABLE(IMAGE_ANALYSIS)
    if (_imageAnalysisDeferringGestureRecognizer)
        [gestures addObject:_imageAnalysisDeferringGestureRecognizer.get()];
#endif
    return gestures;
}

static void appendRecognizerIfNonNull(RetainPtr<NSMutableArray>& array, const RetainPtr<WKDeferringGestureRecognizer>& recognizer)
{
    if (recognizer)
        [array addObject:recognizer.get()];
}

- (NSArray<WKDeferringGestureRecognizer *> *)_touchStartDeferringGestures
{
    RetainPtr recognizers = adoptNS([[NSMutableArray alloc] initWithCapacity:3]);
    appendRecognizerIfNonNull(recognizers, _touchStartDeferringGestureRecognizerForImmediatelyResettableGestures);
    appendRecognizerIfNonNull(recognizers, _touchStartDeferringGestureRecognizerForDelayedResettableGestures);
    appendRecognizerIfNonNull(recognizers, _touchStartDeferringGestureRecognizerForSyntheticTapGestures);
    return recognizers.autorelease();
}

- (NSArray<WKDeferringGestureRecognizer *> *)_touchEndDeferringGestures
{
    RetainPtr recognizers = adoptNS([[NSMutableArray alloc] initWithCapacity:3]);
    appendRecognizerIfNonNull(recognizers, _touchEndDeferringGestureRecognizerForImmediatelyResettableGestures);
    appendRecognizerIfNonNull(recognizers, _touchEndDeferringGestureRecognizerForDelayedResettableGestures);
    appendRecognizerIfNonNull(recognizers, _touchEndDeferringGestureRecognizerForSyntheticTapGestures);
    return recognizers.autorelease();
}

- (void)_doneDeferringTouchStart:(BOOL)preventNativeGestures
{
    for (WKDeferringGestureRecognizer *gestureRecognizer in self._touchStartDeferringGestures) {
        [gestureRecognizer endDeferral:preventNativeGestures ? WebKit::ShouldPreventGestures::Yes : WebKit::ShouldPreventGestures::No];
        if (_failedTouchStartDeferringGestures && !preventNativeGestures)
            _failedTouchStartDeferringGestures->add(gestureRecognizer);
    }
}

- (void)_doneDeferringTouchMove:(BOOL)preventNativeGestures
{
    [_touchMoveDeferringGestureRecognizer endDeferral:preventNativeGestures ? WebKit::ShouldPreventGestures::Yes : WebKit::ShouldPreventGestures::No];
}

- (void)_doneDeferringTouchEnd:(BOOL)preventNativeGestures
{
    for (WKDeferringGestureRecognizer *gesture in self._touchEndDeferringGestures)
        [gesture endDeferral:preventNativeGestures ? WebKit::ShouldPreventGestures::Yes : WebKit::ShouldPreventGestures::No];
}

- (BOOL)_isTouchStartDeferringGesture:(WKDeferringGestureRecognizer *)gesture
{
    return gesture == _touchStartDeferringGestureRecognizerForSyntheticTapGestures || gesture == _touchStartDeferringGestureRecognizerForImmediatelyResettableGestures
        || gesture == _touchStartDeferringGestureRecognizerForDelayedResettableGestures;
}

- (BOOL)_isTouchEndDeferringGesture:(WKDeferringGestureRecognizer *)gesture
{
    return gesture == _touchEndDeferringGestureRecognizerForSyntheticTapGestures || gesture == _touchEndDeferringGestureRecognizerForImmediatelyResettableGestures
        || gesture == _touchEndDeferringGestureRecognizerForDelayedResettableGestures;
}

static inline WebCore::FloatSize tapHighlightBorderRadius(WebCore::FloatSize borderRadii, CGFloat borderRadiusScale)
{
    auto inflatedSize = borderRadii * borderRadiusScale;
    inflatedSize.expand(minimumTapHighlightRadius, minimumTapHighlightRadius);
    return inflatedSize;
}

- (void)_updateTapHighlight
{
    if (![_tapHighlightView superview])
        return;

    [_tapHighlightView setColor:cocoaColor(_tapHighlightInformation.color).get()];

    auto& highlightedQuads = _tapHighlightInformation.quads;
    bool allRectilinear = true;
    for (auto& quad : highlightedQuads) {
        if (!quad.isRectilinear()) {
            allRectilinear = false;
            break;
        }
    }

    auto selfScale = self.layer.transform.m11;
    if (allRectilinear) {
        float deviceScaleFactor = _page->deviceScaleFactor();
        auto rects = createNSArray(highlightedQuads, [&] (auto& quad) {
            auto boundingBox = quad.boundingBox();
            boundingBox.scale(selfScale);
            boundingBox.inflate(minimumTapHighlightRadius);
            return [NSValue valueWithCGRect:encloseRectToDevicePixels(boundingBox, deviceScaleFactor)];
        });
        [_tapHighlightView setFrames:highlightedQuads.map([&](auto& quad) {
            auto boundingBox = quad.boundingBox();
            boundingBox.scale(selfScale);
            boundingBox.inflate(minimumTapHighlightRadius);
            return boundingBox;
        })];
    } else {
        auto quads = adoptNS([[NSMutableArray alloc] initWithCapacity:highlightedQuads.size() * 4]);
        for (auto quad : highlightedQuads) {
            quad.scale(selfScale);
            auto extendedQuad = inflateQuad(quad, minimumTapHighlightRadius);
            [quads addObject:[NSValue valueWithCGPoint:extendedQuad.p1()]];
            [quads addObject:[NSValue valueWithCGPoint:extendedQuad.p2()]];
            [quads addObject:[NSValue valueWithCGPoint:extendedQuad.p3()]];
            [quads addObject:[NSValue valueWithCGPoint:extendedQuad.p4()]];
        }

        auto inflatedHighlightQuads = highlightedQuads.map([&](auto quad) {
            quad.scale(selfScale);
            return inflateQuad(quad, minimumTapHighlightRadius);
        });
        [_tapHighlightView setQuads:WTFMove(inflatedHighlightQuads) boundaryRect:_page->exposedContentRect()];
    }

    [_tapHighlightView setCornerRadii:WebCore::FloatRoundedRect::Radii {
        tapHighlightBorderRadius(_tapHighlightInformation.topLeftRadius, selfScale),
        tapHighlightBorderRadius(_tapHighlightInformation.topRightRadius, selfScale),
        tapHighlightBorderRadius(_tapHighlightInformation.bottomLeftRadius, selfScale),
        tapHighlightBorderRadius(_tapHighlightInformation.bottomRightRadius, selfScale)
    }];
}

- (CGRect)tapHighlightViewRect
{
    return [_tapHighlightView frame];
}

- (UIGestureRecognizer *)imageAnalysisGestureRecognizer
{
#if ENABLE(IMAGE_ANALYSIS)
    return _imageAnalysisGestureRecognizer.get();
#else
    return nil;
#endif
}

- (void)_showTapHighlight
{
    auto shouldPaintTapHighlight = [&](const WebCore::FloatRect& rect) {
#if PLATFORM(MACCATALYST)
        UNUSED_PARAM(rect);
        return NO;
#else
        if (_tapHighlightInformation.nodeHasBuiltInClickHandling)
            return true;

        static const float highlightPaintThreshold = 0.3; // 30%
        float highlightArea = 0;
        for (auto highlightQuad : _tapHighlightInformation.quads) {
            auto boundingBox = highlightQuad.boundingBox();
            highlightArea += boundingBox.area(); 
            if (boundingBox.width() > (rect.width() * highlightPaintThreshold) || boundingBox.height() > (rect.height() * highlightPaintThreshold))
                return false;
        }
        return highlightArea < rect.area() * highlightPaintThreshold;
#endif
    };

    if (!shouldPaintTapHighlight(_page->unobscuredContentRect()) && !_showDebugTapHighlightsForFastClicking)
        return;

    if (!_tapHighlightView) {
        _tapHighlightView = adoptNS([[WKTapHighlightView alloc] initWithFrame:CGRectZero]);
        [_tapHighlightView setUserInteractionEnabled:NO];
        [_tapHighlightView setOpaque:NO];
        [_tapHighlightView setMinimumCornerRadius:minimumTapHighlightRadius];
    }
    [_tapHighlightView setAlpha:1];
    [_interactionViewsContainerView addSubview:_tapHighlightView.get()];
    [self _updateTapHighlight];
}

- (void)_didGetTapHighlightForRequest:(WebKit::TapIdentifier)requestID color:(const WebCore::Color&)color quads:(const Vector<WebCore::FloatQuad>&)highlightedQuads topLeftRadius:(const WebCore::IntSize&)topLeftRadius topRightRadius:(const WebCore::IntSize&)topRightRadius bottomLeftRadius:(const WebCore::IntSize&)bottomLeftRadius bottomRightRadius:(const WebCore::IntSize&)bottomRightRadius nodeHasBuiltInClickHandling:(BOOL)nodeHasBuiltInClickHandling
{
    if (!_isTapHighlightIDValid || _latestTapID != requestID)
        return;

    if (self._hasFocusedElement && _positionInformation.elementContext && _positionInformation.elementContext->isSameElement(_focusedElementInformation.elementContext))
        return;

    _isTapHighlightIDValid = NO;

    _tapHighlightInformation.quads = highlightedQuads;
    _tapHighlightInformation.topLeftRadius = topLeftRadius;
    _tapHighlightInformation.topRightRadius = topRightRadius;
    _tapHighlightInformation.bottomLeftRadius = bottomLeftRadius;
    _tapHighlightInformation.bottomRightRadius = bottomRightRadius;
    _tapHighlightInformation.nodeHasBuiltInClickHandling = nodeHasBuiltInClickHandling;
    if (_showDebugTapHighlightsForFastClicking)
        _tapHighlightInformation.color = [self _tapHighlightColorForFastClick:![_doubleTapGestureRecognizer isEnabled]];
    else
        _tapHighlightInformation.color = color;

    if (_potentialTapInProgress) {
        _hasTapHighlightForPotentialTap = YES;
        return;
    }

    [self _showTapHighlight];
    if (_isExpectingFastSingleTapCommit) {
        [self _finishInteraction];
        if (!_potentialTapInProgress)
            _isExpectingFastSingleTapCommit = NO;
    }
}

- (BOOL)_mayDisableDoubleTapGesturesDuringSingleTap
{
    return _potentialTapInProgress;
}

- (void)_disableDoubleTapGesturesDuringTapIfNecessary:(WebKit::TapIdentifier)requestID
{
    if (_latestTapID != requestID)
        return;

    [self _setDoubleTapGesturesEnabled:NO];
}

- (void)_handleSmartMagnificationInformationForPotentialTap:(WebKit::TapIdentifier)requestID renderRect:(const WebCore::FloatRect&)renderRect fitEntireRect:(BOOL)fitEntireRect viewportMinimumScale:(double)viewportMinimumScale viewportMaximumScale:(double)viewportMaximumScale nodeIsRootLevel:(BOOL)nodeIsRootLevel nodeIsPluginElement:(BOOL)nodeIsPluginElement
{
    const auto& preferences = _page->preferences();

    ASSERT(preferences.fasterClicksEnabled());
    if (!_potentialTapInProgress)
        return;

    // We check both the system preference and the page preference, because we only want this
    // to apply in "desktop" mode.
    if (preferences.preferFasterClickOverDoubleTap() && _page->preferFasterClickOverDoubleTap()) {
        RELEASE_LOG(ViewGestures, "Potential tap found an element and fast taps are preferred. Trigger click. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
        if (preferences.zoomOnDoubleTapWhenRoot() && nodeIsRootLevel) {
            RELEASE_LOG(ViewGestures, "The click handler was on a root-level element, so don't disable double-tap. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
            return;
        }

        if (preferences.alwaysZoomOnDoubleTap()) {
            RELEASE_LOG(ViewGestures, "DTTZ is forced on, so don't disable double-tap. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
            return;
        }

        if (nodeIsPluginElement) {
            RELEASE_LOG(ViewGestures, "The click handler was on a plugin element, so don't disable double-tap. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
            return;
        }

        RELEASE_LOG(ViewGestures, "Give preference to click by disabling double-tap. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
        [self _setDoubleTapGesturesEnabled:NO];
        return;
    }

    auto currentScale = self._contentZoomScale;
    auto targetScale = _smartMagnificationController->zoomFactorForTargetRect(renderRect, fitEntireRect, viewportMinimumScale, viewportMaximumScale);
    if (std::min(targetScale, currentScale) / std::max(targetScale, currentScale) > fasterTapSignificantZoomThreshold) {
        RELEASE_LOG(ViewGestures, "Potential tap would not cause a significant zoom. Trigger click. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
        [self _setDoubleTapGesturesEnabled:NO];
        return;
    }
    RELEASE_LOG(ViewGestures, "Potential tap may cause significant zoom. Wait. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
}

- (void)_cancelLongPressGestureRecognizer
{
    [_highlightLongPressGestureRecognizer cancel];
}

- (void)_cancelTouchEventGestureRecognizer
{
    [_touchEventGestureRecognizer cancel];
}

- (void)_didScroll
{
    [self _updateFrameOfContainerForContextMenuHintPreviewsIfNeeded];

    [self _cancelLongPressGestureRecognizer];
    [self _cancelInteraction];
}

- (void)_scrollingNodeScrollingWillBegin:(std::optional<WebCore::ScrollingNodeID>)scrollingNodeID
{
    [_textInteractionWrapper willStartScrollingOverflow:[self _scrollViewForScrollingNodeID:scrollingNodeID]];
}

- (void)_scrollingNodeScrollingDidEnd:(std::optional<WebCore::ScrollingNodeID>)scrollingNodeID
{
    if (auto& state = _page->editorState(); state.hasVisualData() && scrollingNodeID && scrollingNodeID == state.visualData->enclosingScrollingNodeID) {
        _page->scheduleFullEditorStateUpdate();
        _waitingForEditorStateAfterScrollingSelectionContainer = YES;
        _page->callAfterNextPresentationUpdate([weakSelf = WeakObjCPtr<WKContentView>(self)] {
            if (auto strongSelf = weakSelf.get())
                strongSelf->_waitingForEditorStateAfterScrollingSelectionContainer = NO;
        });
    }

    // If scrolling ends before we've received a selection update,
    // we postpone showing the selection until the update is received.
    if (!_selectionNeedsUpdate) {
        _shouldRestoreSelection = YES;
        return;
    }
    [self _updateChangedSelection];
    [_textInteractionWrapper didEndScrollingOverflow];

    [_webView _didFinishScrolling:[self _scrollViewForScrollingNodeID:scrollingNodeID]];
}

- (BOOL)shouldShowAutomaticKeyboardUI
{
    if (_focusedElementInformation.inputMode == WebCore::InputMode::None)
        return NO;

    if (_focusedElementInformation.isFocusingWithDataListDropdown && !UIKeyboard.isInHardwareKeyboardMode)
        return NO;

    return [self _shouldShowAutomaticKeyboardUIIgnoringInputMode];
}

- (BOOL)_shouldShowAutomaticKeyboardUIIgnoringInputMode
{
    if (_focusedElementInformation.isReadOnly)
        return NO;

    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::None:
    case WebKit::InputType::Color:
    case WebKit::InputType::Drawing:
    case WebKit::InputType::Date:
    case WebKit::InputType::Month:
    case WebKit::InputType::DateTimeLocal:
    case WebKit::InputType::Time:
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return NO;
    case WebKit::InputType::Select: {
        if (self._shouldUseContextMenusForFormControls)
            return NO;
        return PAL::currentUserInterfaceIdiomIsSmallScreen();
    }
    default:
        return YES;
    }
    return NO;
}

- (BOOL)_disableAutomaticKeyboardUI
{
    // Always enable automatic keyboard UI if we are not the first responder to avoid
    // interfering with other focused views (e.g. Find-in-page).
    return [self isFirstResponder] && ![self shouldShowAutomaticKeyboardUI];
}

- (BOOL)_requiresKeyboardWhenFirstResponder
{
    BOOL webViewIsEditable = [_webView _isEditable];
#if USE(BROWSERENGINEKIT)
    if (!webViewIsEditable && self.shouldUseAsyncInteractions)
        return [super _requiresKeyboardWhenFirstResponder];
#endif

    if (webViewIsEditable && !self._disableAutomaticKeyboardUI)
        return YES;

    // FIXME: We should add the logic to handle keyboard visibility during focus redirects.
    return [self _shouldShowAutomaticKeyboardUIIgnoringInputMode] || _seenHardwareKeyDownInNonEditableElement;
}

- (BOOL)_requiresKeyboardResetOnReload
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions)
        return [super _requiresKeyboardResetOnReload];
#endif

    return YES;
}

- (WebCore::FloatRect)rectToRevealWhenZoomingToFocusedElement
{
    WebCore::IntRect elementInteractionRect;
    if (_focusedElementInformation.interactionRect.contains(_focusedElementInformation.lastInteractionLocation))
        elementInteractionRect = { _focusedElementInformation.lastInteractionLocation, { 1, 1 } };

    if (!mayContainSelectableText(_focusedElementInformation.elementType))
        return elementInteractionRect;

    if (!_page->editorState().hasPostLayoutAndVisualData())
        return elementInteractionRect;

    auto boundingRect = _page->selectionBoundingRectInRootViewCoordinates();
    boundingRect.intersect(_focusedElementInformation.interactionRect);
    return boundingRect;
}

- (void)_keyboardWillShow
{
    _waitingForKeyboardAppearanceAnimationToStart = NO;

    if (_revealFocusedElementDeferrer)
        _revealFocusedElementDeferrer->fulfill(WebKit::RevealFocusedElementDeferralReason::KeyboardWillShow);
}

- (void)_keyboardDidShow
{
    // FIXME: This deferred call to -_zoomToRevealFocusedElement works around the fact that Mail compose
    // disables automatic content inset adjustment using the keyboard height, and instead has logic to
    // explicitly set WKScrollView's contentScrollInset after receiving UIKeyboardDidShowNotification.
    // This means that if we -_zoomToRevealFocusedElement immediately after focusing the body field in
    // Mail, we won't take the keyboard height into account when scrolling.
    // Mitigate this by deferring the call to -_zoomToRevealFocusedElement in this case until after the
    // keyboard has finished animating. We can revert this once rdar://87733414 is fixed.
    RunLoop::mainSingleton().dispatch([weakSelf = WeakObjCPtr<WKContentView>(self)] {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || !strongSelf->_revealFocusedElementDeferrer)
            return;

        strongSelf->_revealFocusedElementDeferrer->fulfill(WebKit::RevealFocusedElementDeferralReason::KeyboardDidShow);
    });

#if USE(UICONTEXTMENU)
    [_fileUploadPanel repositionContextMenuIfNeeded:WebKit::KeyboardIsDismissing::No];
#endif
}

- (void)_zoomToRevealFocusedElement
{
    _revealFocusedElementDeferrer = nullptr;

    if (_focusedElementInformation.preventScroll)
        return;

    if (_suppressSelectionAssistantReasons || _activeTextInteractionCount)
        return;

    // In case user scaling is force enabled, do not use that scaling when zooming in with an input field.
    // Zooming above the page's default scale factor should only happen when the user performs it.
    [self _zoomToFocusRect:_focusedElementInformation.interactionRect
        selectionRect:_didAccessoryTabInitiateFocus ? WebCore::FloatRect() : self.rectToRevealWhenZoomingToFocusedElement
        fontSize:_focusedElementInformation.nodeFontSize
        minimumScale:_focusedElementInformation.minimumScaleFactor
        maximumScale:_focusedElementInformation.maximumScaleFactorIgnoringAlwaysScalable
        allowScaling:_focusedElementInformation.allowsUserScalingIgnoringAlwaysScalable && PAL::currentUserInterfaceIdiomIsSmallScreen()
        forceScroll:[self requiresAccessoryView]];
}

- (UIView *)inputView
{
    return [_webView inputView];
}

- (UIView *)inputViewForWebView
{
    if (!self._hasFocusedElement)
        return nil;

    if (_inputPeripheral) {
        // FIXME: UIKit may invoke -[WKContentView inputView] at any time when WKContentView is the first responder;
        // as such, it doesn't make sense to change the enclosing scroll view's zoom scale and content offset to reveal
        // the focused element here. It seems this behavior was added to match logic in legacy WebKit (refer to
        // UIWebBrowserView). Instead, we should find the places where we currently assume that UIKit (or other clients)
        // invoke -inputView to zoom to the focused element, and either surface SPI for clients to zoom to the focused
        // element, or explicitly trigger the zoom from WebKit.
        // For instance, one use case that currently relies on this detail is adjusting the zoom scale and viewport upon
        // rotation, when a select element is focused. See <https://webkit.org/b/192878> for more information.
        [self _zoomToRevealFocusedElement];

        [self _updateAccessory];
    }

    if (UIView *customInputView = [_formInputSession customInputView])
        return customInputView;

    if (_dataListTextSuggestionsInputView)
        return _dataListTextSuggestionsInputView.get();

    return [_inputPeripheral assistantView];
}

- (CGRect)_selectionClipRect
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return self.selectionClipRect;
}

static BOOL isBuiltInScrollViewPanGestureRecognizer(UIGestureRecognizer *recognizer)
{
    static Class scrollViewPanGestureClass;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        scrollViewPanGestureClass = NSClassFromString(@"UIScrollViewPanGestureRecognizer");
    });
    return [recognizer isKindOfClass:scrollViewPanGestureClass];
}

static BOOL isBuiltInScrollViewGestureRecognizer(UIGestureRecognizer *recognizer)
{
    static Class scrollViewPinchGestureClass;
    static Class scrollViewKnobLongPressGestureRecognizerClass;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        scrollViewPinchGestureClass = NSClassFromString(@"UIScrollViewPinchGestureRecognizer");
        scrollViewKnobLongPressGestureRecognizerClass = NSClassFromString(@"UIScrollViewKnobLongPressGestureRecognizer");
    });
    return isBuiltInScrollViewPanGestureRecognizer(recognizer)
        || [recognizer isKindOfClass:scrollViewPinchGestureClass]
        || [recognizer isKindOfClass:scrollViewKnobLongPressGestureRecognizerClass];
}

- (BOOL)_isContextMenuGestureRecognizerForFailureRelationships:(UIGestureRecognizer *)gestureRecognizer
{
    return [gestureRecognizer.name isEqualToString:@"com.apple.UIKit.clickPresentationFailure"] && gestureRecognizer.view == self;
}

#if ENABLE(DRAG_SUPPORT)

- (BOOL)_isDragInitiationGestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
{
    if (!_dragInteraction)
        return NO;

    id gestureDelegate = gestureRecognizer.delegate;
    if (![gestureDelegate respondsToSelector:@selector(delegate)])
        return NO;

    return _dragInteraction == [gestureDelegate delegate];
}

#endif // ENABLE(DRAG_SUPPORT)

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)preventingGestureRecognizer canPreventGestureRecognizer:(UIGestureRecognizer *)preventedGestureRecognizer
{
    // A long-press gesture can not be recognized while panning, but a pan can be recognized
    // during a long-press gesture.
    BOOL shouldNotPreventScrollViewGestures = preventingGestureRecognizer == _highlightLongPressGestureRecognizer || preventingGestureRecognizer == _longPressGestureRecognizer;
    
    return !(shouldNotPreventScrollViewGestures && isBuiltInScrollViewGestureRecognizer(preventedGestureRecognizer));
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)preventedGestureRecognizer canBePreventedByGestureRecognizer:(UIGestureRecognizer *)preventingGestureRecognizer
{
    if (preventingGestureRecognizer._wk_isTextInteractionLoupeGesture && (preventedGestureRecognizer == _highlightLongPressGestureRecognizer || preventedGestureRecognizer == _longPressGestureRecognizer)) {
#if PLATFORM(MACCATALYST)
        return YES;
#else
        return NO;
#endif
    }

    return YES;
}

- (UIGestureRecognizer *)textInteractionLoupeGestureRecognizer
{
    if (_cachedTextInteractionLoupeGestureRecognizer.view != self) {
        _cachedTextInteractionLoupeGestureRecognizer = nil;
        for (UIGestureRecognizer *gestureRecognizer in self.gestureRecognizers) {
            if (gestureRecognizer._wk_isTextInteractionLoupeGesture) {
                _cachedTextInteractionLoupeGestureRecognizer = gestureRecognizer;
                break;
            }
        }
    }
    return _cachedTextInteractionLoupeGestureRecognizer;
}

- (UIGestureRecognizer *)textInteractionTapGestureRecognizer
{
    if (_cachedTextInteractionTapGestureRecognizer.view != self) {
        _cachedTextInteractionTapGestureRecognizer = nil;
        for (UIGestureRecognizer *gestureRecognizer in self.gestureRecognizers) {
            if (gestureRecognizer._wk_isTextInteractionTapGesture) {
                _cachedTextInteractionTapGestureRecognizer = gestureRecognizer;
                break;
            }
        }
    }
    return _cachedTextInteractionTapGestureRecognizer;
}

static inline bool isSamePair(UIGestureRecognizer *a, UIGestureRecognizer *b, UIGestureRecognizer *x, UIGestureRecognizer *y)
{
    return (a == x && b == y) || (b == x && a == y);
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer
{
    if (gestureRecognizer == _keyboardDismissalGestureRecognizer || otherGestureRecognizer == _keyboardDismissalGestureRecognizer)
        return YES;

    for (WKDeferringGestureRecognizer *gesture in self.deferringGestures) {
        if (isSamePair(gestureRecognizer, otherGestureRecognizer, _touchEventGestureRecognizer.get(), gesture))
            return YES;
    }

    if ([gestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class] && [otherGestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class])
        return YES;

#if ENABLE(IMAGE_ANALYSIS)
    if (gestureRecognizer == _imageAnalysisDeferringGestureRecognizer)
        return ![self shouldDeferGestureDueToImageAnalysis:otherGestureRecognizer];

    if (otherGestureRecognizer == _imageAnalysisDeferringGestureRecognizer)
        return ![self shouldDeferGestureDueToImageAnalysis:gestureRecognizer];
#endif

    if (gestureRecognizer == _singleTapGestureRecognizer
        && isBuiltInScrollViewPanGestureRecognizer(otherGestureRecognizer)
        && [otherGestureRecognizer.view isKindOfClass:UIScrollView.class]
        && ![self _isInterruptingDecelerationForScrollViewOrAncestor:[_singleTapGestureRecognizer lastTouchedScrollView]])
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _highlightLongPressGestureRecognizer.get(), _longPressGestureRecognizer.get()))
        return YES;

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    if ([_mouseInteraction hasGesture:otherGestureRecognizer])
        return YES;
#endif

#if PLATFORM(MACCATALYST)
    if ((gestureRecognizer == _singleTapGestureRecognizer && otherGestureRecognizer._wk_isTextInteractionLoupeGesture)
        || (otherGestureRecognizer == _singleTapGestureRecognizer && gestureRecognizer._wk_isTextInteractionLoupeGesture))
        return YES;

    if (([gestureRecognizer isKindOfClass:[_UILookupGestureRecognizer class]] && [otherGestureRecognizer isKindOfClass:[UILongPressGestureRecognizer class]]) || ([otherGestureRecognizer isKindOfClass:[UILongPressGestureRecognizer class]] && [gestureRecognizer isKindOfClass:[_UILookupGestureRecognizer class]]))
        return YES;
#endif // PLATFORM(MACCATALYST)

    if (gestureRecognizer == _highlightLongPressGestureRecognizer.get() || otherGestureRecognizer == _highlightLongPressGestureRecognizer.get()) {
        if (gestureRecognizer._wk_isTextInteractionLoupeGesture || otherGestureRecognizer._wk_isTextInteractionLoupeGesture)
            return YES;

        if (gestureRecognizer._wk_isTapAndAHalf || otherGestureRecognizer._wk_isTapAndAHalf)
            return YES;
    }

    if ((gestureRecognizer == _singleTapGestureRecognizer && otherGestureRecognizer._wk_isTextInteractionTapGesture)
        || (otherGestureRecognizer == _singleTapGestureRecognizer && gestureRecognizer._wk_isTextInteractionTapGesture))
        return YES;

#if ENABLE(MODEL_PROCESS)
    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _modelInteractionPanGestureRecognizer.get(), _longPressGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _modelInteractionPanGestureRecognizer.get(), _nonBlockingDoubleTapGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _modelInteractionPanGestureRecognizer.get(), _singleTapGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _modelInteractionPanGestureRecognizer.get(), _previewGestureRecognizer.get()))
        return YES;
#endif

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _singleTapGestureRecognizer.get(), _nonBlockingDoubleTapGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _highlightLongPressGestureRecognizer.get(), _nonBlockingDoubleTapGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _highlightLongPressGestureRecognizer.get(), _previewSecondaryGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _highlightLongPressGestureRecognizer.get(), _previewGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _nonBlockingDoubleTapGestureRecognizer.get(), _doubleTapGestureRecognizerForDoubleClick.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _doubleTapGestureRecognizer.get(), _doubleTapGestureRecognizerForDoubleClick.get()))
        return YES;

#if ENABLE(IMAGE_ANALYSIS)
    if (gestureRecognizer == _imageAnalysisGestureRecognizer)
        return YES;
#endif

#if PLATFORM(VISION)
    Class graspGesture = NSClassFromString(@"MRUIGraspGestureRecognizer");
    if (([gestureRecognizer isKindOfClass:graspGesture] && otherGestureRecognizer == _singleTapGestureRecognizer.get()) || (gestureRecognizer == _singleTapGestureRecognizer.get() && [otherGestureRecognizer isKindOfClass:graspGesture]))
        return YES;
#endif

    return NO;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldRequireFailureOfGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer
{
    if (gestureRecognizer == _touchEventGestureRecognizer && [self _touchEventsMustRequireGestureRecognizerToFail:otherGestureRecognizer])
        return YES;

    if ([otherGestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class])
        return [(WKDeferringGestureRecognizer *)otherGestureRecognizer shouldDeferGestureRecognizer:gestureRecognizer];

    return NO;
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldBeRequiredToFailByGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer
{
    if ([gestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class])
        return [(WKDeferringGestureRecognizer *)gestureRecognizer shouldDeferGestureRecognizer:otherGestureRecognizer];

    return NO;
}

- (void)_showImageSheet
{
    [_actionSheetAssistant showImageSheet];
}

- (void)_showAttachmentSheet
{
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    if (![uiDelegate respondsToSelector:@selector(_webView:showCustomSheetForElement:)])
        return;

    auto element = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeAttachment image:nil information:_positionInformation]);
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    [uiDelegate _webView:self.webView showCustomSheetForElement:element.get()];
ALLOW_DEPRECATED_DECLARATIONS_END
}

- (void)_showLinkSheet
{
    [_actionSheetAssistant showLinkSheet];
}

- (void)_showDataDetectorsUI
{
    [self _showDataDetectorsUIForPositionInformation:_positionInformation];
}

- (void)_showDataDetectorsUIForPositionInformation:(const WebKit::InteractionInformationAtPosition&)positionInformation
{
    [_actionSheetAssistant showDataDetectorsUIForPositionInformation:positionInformation];
}

- (SEL)_actionForLongPressFromPositionInformation:(const WebKit::InteractionInformationAtPosition&)positionInformation
{
    if (!self.webView.configuration._longPressActionsEnabled)
        return nil;

    if (!positionInformation.touchCalloutEnabled)
        return nil;

    if (positionInformation.isImage)
        return @selector(_showImageSheet);

    if (positionInformation.isLink) {
#if ENABLE(DATA_DETECTION)
        if (WebCore::DataDetection::canBePresentedByDataDetectors(positionInformation.url))
            return @selector(_showDataDetectorsUI);
#endif
        return @selector(_showLinkSheet);
    }
    if (positionInformation.isAttachment)
        return @selector(_showAttachmentSheet);

    return nil;
}

- (SEL)_actionForLongPress
{
    return [self _actionForLongPressFromPositionInformation:_positionInformation];
}

- (void)doAfterPositionInformationUpdate:(void (^)(WebKit::InteractionInformationAtPosition))action forRequest:(WebKit::InteractionInformationRequest)request
{
    if ([self _currentPositionInformationIsValidForRequest:request]) {
        // If the most recent position information is already valid, invoke the given action block immediately.
        action(_positionInformation);
        return;
    }

    _pendingPositionInformationHandlers.append(InteractionInformationRequestAndCallback(request, action));

    if (![self _hasValidOutstandingPositionInformationRequest:request])
        [self requestAsynchronousPositionInformationUpdate:request];
}

- (BOOL)ensurePositionInformationIsUpToDate:(WebKit::InteractionInformationRequest)request
{
    if ([self _currentPositionInformationIsValidForRequest:request])
        return YES;

    if (!_page->hasRunningProcess())
        return NO;

    Ref process = _page->legacyMainFrameProcess();
    if (!process->hasConnection())
        return NO;

    if (_isWaitingOnPositionInformation)
        return NO;

    _isWaitingOnPositionInformation = YES;

    if (![self _hasValidOutstandingPositionInformationRequest:request])
        [self requestAsynchronousPositionInformationUpdate:request];

    bool receivedResponse = process->protectedConnection()->waitForAndDispatchImmediately<Messages::WebPageProxy::DidReceivePositionInformation>(_page->webPageIDInMainFrameProcess(), 1_s, IPC::WaitForOption::InterruptWaitingIfSyncMessageArrives) == IPC::Error::NoError;
    _hasValidPositionInformation = receivedResponse && _positionInformation.canBeValid;
    return _hasValidPositionInformation;
}

- (void)requestAsynchronousPositionInformationUpdate:(WebKit::InteractionInformationRequest)request
{
    if ([self _currentPositionInformationIsValidForRequest:request])
        return;

    _lastOutstandingPositionInformationRequest = request;

    _page->requestPositionInformation(request);
}

- (BOOL)_currentPositionInformationIsValidForRequest:(const WebKit::InteractionInformationRequest&)request
{
    return _hasValidPositionInformation && _positionInformation.request.isValidForRequest(request);
}

- (BOOL)_hasValidOutstandingPositionInformationRequest:(const WebKit::InteractionInformationRequest&)request
{
    return _lastOutstandingPositionInformationRequest && _lastOutstandingPositionInformationRequest->isValidForRequest(request);
}

- (BOOL)_currentPositionInformationIsApproximatelyValidForRequest:(const WebKit::InteractionInformationRequest&)request radiusForApproximation:(int)radius
{
    return _hasValidPositionInformation && _positionInformation.request.isApproximatelyValidForRequest(request, radius);
}

- (void)_invokeAndRemovePendingHandlersValidForCurrentPositionInformation
{
    ASSERT(_hasValidPositionInformation);

    // FIXME: We need to clean up these handlers in the event that we are not able to collect data, or if the WebProcess crashes.
    ++_positionInformationCallbackDepth;
    auto updatedPositionInformation = _positionInformation;

    for (size_t index = 0; index < _pendingPositionInformationHandlers.size(); ++index) {
        auto requestAndHandler = _pendingPositionInformationHandlers[index];
        if (!requestAndHandler)
            continue;

        if (![self _currentPositionInformationIsValidForRequest:requestAndHandler->first])
            continue;

        _pendingPositionInformationHandlers[index] = std::nullopt;

        if (requestAndHandler->second)
            requestAndHandler->second(updatedPositionInformation);
    }

    if (--_positionInformationCallbackDepth)
        return;

    for (int index = _pendingPositionInformationHandlers.size() - 1; index >= 0; --index) {
        if (!_pendingPositionInformationHandlers[index])
            _pendingPositionInformationHandlers.removeAt(index);
    }
}

#if ENABLE(DATA_DETECTION)
- (NSArray *)_dataDetectionResults
{
    return _page->dataDetectionResults();
}
#endif

- (BOOL)_pointIsInsideSelectionRect:(CGPoint)point outBoundingRect:(WebCore::FloatRect *)outBoundingRect
{
    BOOL pointIsInSelectionRect = NO;
    for (auto& rectInfo : _lastSelectionDrawingInfo.selectionGeometries) {
        auto rect = rectInfo.rect();
        if (rect.isEmpty())
            continue;

        pointIsInSelectionRect |= rect.contains(WebCore::roundedIntPoint(point));
        if (outBoundingRect)
            outBoundingRect->unite(rect);
    }

    if (!pointIsInSelectionRect)
        return NO;

    if (!self.selectionHonorsOverflowScrolling)
        return YES;

    RetainPtr hitView = [self hitTest:point withEvent:nil];
    if (!hitView)
        return NO;

    RetainPtr hitScroller = dynamic_objc_cast<UIScrollView>(hitView.get()) ?: [hitView _wk_parentScrollView];
    return hitScroller && hitScroller == self._selectionContainerViewInternal._wk_parentScrollView;
}

- (BOOL)_shouldToggleEditMenuAfterTapAt:(CGPoint)point
{
    if (_lastSelectionDrawingInfo.selectionGeometries.isEmpty())
        return NO;

    WebCore::FloatRect selectionBoundingRect;
    BOOL pointIsInSelectionRect = [self _pointIsInsideSelectionRect:point outBoundingRect:&selectionBoundingRect];
    WebCore::FloatRect unobscuredContentRect = self.unobscuredContentRect;
    selectionBoundingRect.intersect(unobscuredContentRect);

    float unobscuredArea = unobscuredContentRect.area();
    float ratioForConsideringSelectionRectToCoverVastMajorityOfContent = 0.75;
    if (!unobscuredArea || selectionBoundingRect.area() / unobscuredArea > ratioForConsideringSelectionRectToCoverVastMajorityOfContent)
        return NO;

    return pointIsInSelectionRect;
}

- (BOOL)_hasEnclosingScrollView:(UIView *)firstView matchingCriteria:(Function<BOOL(UIScrollView *)>&&)matchFunction
{
    UIView *view = firstView ?: self.webView.scrollView;
    for (; view; view = view.superview) {
        if (auto scrollView = dynamic_objc_cast<UIScrollView>(view); scrollView && matchFunction(scrollView))
            return YES;
    }
    return NO;
}

- (BOOL)_isPanningScrollViewOrAncestor:(UIScrollView *)scrollView
{
    return [self _hasEnclosingScrollView:scrollView matchingCriteria:[](UIScrollView *scrollView) {
        if (scrollView.dragging || scrollView.decelerating)
            return YES;

        auto *panGesture = scrollView.panGestureRecognizer;
        ASSERT(panGesture);
        return [panGesture _wk_hasRecognizedOrEnded];
    }];
}

- (BOOL)_isInterruptingDecelerationForScrollViewOrAncestor:(UIScrollView *)scrollView
{
    return [self _hasEnclosingScrollView:scrollView matchingCriteria:[](UIScrollView *scrollView) {
        return scrollView._wk_isInterruptingDeceleration;
    }];
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer
{
    CGPoint point = [gestureRecognizer locationInView:self];

    auto ignoreTapGestureReason = [&](WKScrollViewTrackingTapGestureRecognizer *tapGesture) -> WebKit::IgnoreTapGestureReason {
        if ([self _shouldToggleEditMenuAfterTapAt:point])
            return WebKit::IgnoreTapGestureReason::ToggleEditMenu;

        RetainPtr scrollView = [tapGesture lastTouchedScrollView];
        if ([self _isPanningScrollViewOrAncestor:scrollView.get()] || [self _isInterruptingDecelerationForScrollViewOrAncestor:scrollView.get()])
            return WebKit::IgnoreTapGestureReason::DeferToScrollView;

        return WebKit::IgnoreTapGestureReason::None;
    };

    if (gestureRecognizer == _singleTapGestureRecognizer) {
        switch (ignoreTapGestureReason(_singleTapGestureRecognizer.get())) {
        case WebKit::IgnoreTapGestureReason::ToggleEditMenu:
            _page->clearSelectionAfterTappingSelectionHighlightIfNeeded(point);
            [[fallthrough]];
        case WebKit::IgnoreTapGestureReason::DeferToScrollView:
            return NO;
        case WebKit::IgnoreTapGestureReason::None:
            return YES;
        }
    }

#if ENABLE(MODEL_PROCESS)
    if (gestureRecognizer == _modelInteractionPanGestureRecognizer) {
        WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
        if (![self ensurePositionInformationIsUpToDate:request])
            return NO;

        if (self._hasFocusedElement) {
            // Prevent the gesture if it is the same node.
            if (_positionInformation.elementContext && _positionInformation.elementContext->isSameElement(_focusedElementInformation.elementContext))
                return NO;
        } else {
            // Prevent the gesture if there is no action for the node.
            return _positionInformation.isInteractiveModel;
        }
    }
#endif

    if (gestureRecognizer == _keyboardDismissalGestureRecognizer) {
        auto tapIsInEditableRoot = [&] {
            auto& state = _page->editorState();
            return state.hasVisualData() && state.visualData->editableRootBounds.contains(WebCore::roundedIntPoint(point));
        };
        return self._hasFocusedElement
            && !self.hasHiddenContentEditable
            && !tapIsInEditableRoot()
            && ignoreTapGestureReason(_keyboardDismissalGestureRecognizer.get()) == WebKit::IgnoreTapGestureReason::None;
    }

    if (gestureRecognizer == _doubleTapGestureRecognizerForDoubleClick) {
        // Do not start the double-tap-for-double-click gesture recognizer unless we've got a dblclick event handler on the node at the tap location.
        WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
        if (![self _currentPositionInformationIsApproximatelyValidForRequest:request radiusForApproximation:[_doubleTapGestureRecognizerForDoubleClick allowableMovement]]) {
            if (![self ensurePositionInformationIsUpToDate:request])
                return NO;
        }
        return _positionInformation.nodeAtPositionHasDoubleClickHandler.value_or(false);
    }

    if (gestureRecognizer == _highlightLongPressGestureRecognizer
        || gestureRecognizer == _doubleTapGestureRecognizer
        || gestureRecognizer == _nonBlockingDoubleTapGestureRecognizer
        || gestureRecognizer == _twoFingerDoubleTapGestureRecognizer) {

        if (self._hasFocusedElement) {
            // Request information about the position with sync message.
            // If the focused element is the same, prevent the gesture.
            if (![self ensurePositionInformationIsUpToDate:WebKit::InteractionInformationRequest(WebCore::roundedIntPoint(point))])
                return NO;
            if (_positionInformation.elementContext && _positionInformation.elementContext->isSameElement(_focusedElementInformation.elementContext))
                return NO;
        }
    }

    if (gestureRecognizer == _highlightLongPressGestureRecognizer) {
        if ([self _isInterruptingDecelerationForScrollViewOrAncestor:[_highlightLongPressGestureRecognizer lastTouchedScrollView]])
            return NO;

        if (self._hasFocusedElement) {
            // This is a different element than the focused one.
            // Prevent the gesture if there is no node.
            // Allow the gesture if it is a node that wants highlight or if there is an action for it.
            if (!_positionInformation.isElement)
                return NO;
            return [self _actionForLongPress] != nil;
        }
        // We still have no idea about what is at the location.
        // Send an async message to find out.
        _hasValidPositionInformation = NO;
        WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));

        // If 3D Touch is enabled, asynchronously collect snapshots in the hopes that
        // they'll arrive before we have to synchronously request them in
        // _interactionShouldBeginFromPreviewItemController.
        if (self.traitCollection.forceTouchCapability == UIForceTouchCapabilityAvailable) {
            request.includeSnapshot = true;
            request.includeLinkIndicator = true;
            request.linkIndicatorShouldHaveLegacyMargins = !self._shouldUseContextMenus;
            request.gatherAnimations = [self.webView _allowAnimationControls];
        }

        [self requestAsynchronousPositionInformationUpdate:request];
        return YES;

    }

    if (gestureRecognizer == _longPressGestureRecognizer) {
        // Use the information retrieved with one of the previous calls
        // to gestureRecognizerShouldBegin.
        // Force a sync call if not ready yet.
        WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
        if (![self ensurePositionInformationIsUpToDate:request])
            return NO;

        if (self._hasFocusedElement) {
            // Prevent the gesture if it is the same node.
            if (_positionInformation.elementContext && _positionInformation.elementContext->isSameElement(_focusedElementInformation.elementContext))
                return NO;
        } else {
            // Prevent the gesture if there is no action for the node.
            return [self _actionForLongPress] != nil;
        }
    }

    return YES;
}

- (void)_cancelInteraction
{
    _isTapHighlightIDValid = NO;
    [_tapHighlightView removeFromSuperview];
}

- (void)_finishInteraction
{
    _isTapHighlightIDValid = NO;
    [self _fadeTapHighlightViewIfNeeded];
}

- (void)_fadeTapHighlightViewIfNeeded
{
    if (![_tapHighlightView superview] || _isTapHighlightFading)
        return;

    _isTapHighlightFading = YES;
    CGFloat tapHighlightFadeDuration = _showDebugTapHighlightsForFastClicking ? 0.25 : 0.1;
    [UIView animateWithDuration:tapHighlightFadeDuration
        animations:^{
            [_tapHighlightView setAlpha:0];
        }
        completion:^(BOOL finished) {
            if (finished)
                [_tapHighlightView removeFromSuperview];
            _isTapHighlightFading = NO;
        }];
}

- (BOOL)canShowNonEmptySelectionView
{
    if (_suppressSelectionAssistantReasons)
        return NO;

    auto& state = _page->editorState();
    return state.hasPostLayoutData() && !state.selectionIsNone;
}

- (BOOL)hasSelectablePositionAtPoint:(CGPoint)point
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if ([_imageAnalysisInteraction interactableItemExistsAtPoint:point])
        return NO;
#endif

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (!self.webView.configuration._textInteractionGesturesEnabled)
        return NO;
ALLOW_DEPRECATED_DECLARATIONS_END

    if (!_page->preferences().textInteractionEnabled())
        return NO;

    if (_suppressSelectionAssistantReasons)
        return NO;

    if (_inspectorNodeSearchEnabled)
        return NO;

    WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
    if (![self ensurePositionInformationIsUpToDate:request])
        return NO;

#if ENABLE(IMAGE_ANALYSIS)
    if (_elementPendingImageAnalysis && _positionInformation.hostImageOrVideoElementContext == _elementPendingImageAnalysis)
        return YES;
#endif

    return _positionInformation.isSelectable();
}

- (BOOL)pointIsNearMarkedText:(CGPoint)point
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if ([_imageAnalysisInteraction interactableItemExistsAtPoint:point])
        return NO;
#endif

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (!self.webView.configuration._textInteractionGesturesEnabled)
        return NO;
ALLOW_DEPRECATED_DECLARATIONS_END

    if (!_page->preferences().textInteractionEnabled())
        return NO;
    
    if (_suppressSelectionAssistantReasons)
        return NO;

    WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
    if (![self ensurePositionInformationIsUpToDate:request])
        return NO;
    return _positionInformation.isNearMarkedText;
}

- (BOOL)textInteractionGesture:(WKBEGestureType)gesture shouldBeginAtPoint:(CGPoint)point
{
#if USE(BROWSERENGINEKIT)
    if (gesture == WKBEGestureTypeForceTouch)
        return [self hasSelectablePositionAtPoint:point];
#endif

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if ([_imageAnalysisInteraction interactableItemExistsAtPoint:point])
        return NO;
#endif

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (!self.webView.configuration._textInteractionGesturesEnabled)
        return NO;
ALLOW_DEPRECATED_DECLARATIONS_END

    if (!_page->preferences().textInteractionEnabled())
        return NO;
    
    if (_domPasteRequestHandler)
        return NO;

    if (_suppressSelectionAssistantReasons)
        return NO;

    if (!self.isFocusingElement) {
        if (gesture == WKBEGestureTypeDoubleTap) {
            // Don't allow double tap text gestures in noneditable content.
            return NO;
        }

        if (gesture == WKBEGestureTypeOneFingerTap) {
            ASSERT(_suppressNonEditableSingleTapTextInteractionCount >= 0);
            if (_suppressNonEditableSingleTapTextInteractionCount > 0)
                return NO;

            if (self.textInteractionLoupeGestureRecognizer._wk_hasRecognizedOrEnded) {
                // Avoid handling one-finger taps while the web process is processing certain selection changes.
                // This works around a scenario where UIKeyboardImpl blocks the main thread while handling a one-
                // finger tap, which subsequently prevents the UI process from handling any incoming IPC messages.
                return NO;
            }
            return _page->editorState().selectionIsRange;
        }
    }

    WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
    if (![self ensurePositionInformationIsUpToDate:request])
        return NO;

    if (gesture == WKBEGestureTypeLoupe && _positionInformation.selectability == WebKit::InteractionInformationAtPosition::Selectability::UnselectableDueToUserSelectNoneOrQuirk)
        return NO;

    if (_positionInformation.preventTextInteraction)
        return NO;

#if ENABLE(IMAGE_ANALYSIS)
    if (_elementPendingImageAnalysis && _positionInformation.hostImageOrVideoElementContext == _elementPendingImageAnalysis)
        return YES;
#endif

    // If we're currently focusing an editable element, only allow the selection to move within that focused element.
    if (self.isFocusingElement)
        return _positionInformation.elementContext && _positionInformation.elementContext->isSameElement(_focusedElementInformation.elementContext);

    if (_positionInformation.prefersDraggingOverTextSelection)
        return NO;

    // If we're selecting something, don't activate highlight.
    if (gesture == WKBEGestureTypeLoupe && [self hasSelectablePositionAtPoint:point])
        [self _cancelLongPressGestureRecognizer];
    
    // Otherwise, if we're using a text interaction assistant outside of editing purposes (e.g. the selection mode
    // is character granularity) then allow text selection.
    return YES;
}

- (NSArray *)webSelectionRectsForSelectionGeometries:(const Vector<WebCore::SelectionGeometry>&)selectionGeometries
{
    if (selectionGeometries.isEmpty())
        return nil;

    return createNSArray(selectionGeometries, [] (auto& geometry) {
        auto webRect = [WebSelectionRect selectionRect];
        webRect.rect = geometry.rect();
        webRect.writingDirection = geometry.direction() == WebCore::TextDirection::LTR ? WKWritingDirectionLeftToRight : WKWritingDirectionRightToLeft;
        webRect.isLineBreak = geometry.isLineBreak();
        webRect.isFirstOnLine = geometry.isFirstOnLine();
        webRect.isLastOnLine = geometry.isLastOnLine();
        webRect.containsStart = geometry.containsStart();
        webRect.containsEnd = geometry.containsEnd();
        webRect.isInFixedPosition = geometry.isInFixedPosition();
        webRect.isHorizontal = geometry.isHorizontal();
        return webRect;
    }).autorelease();
}

- (NSArray *)webSelectionRects
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    if (!_page->editorState().hasPostLayoutAndVisualData() || _page->editorState().selectionIsNone)
        return nil;
    const auto& selectionGeometries = _page->editorState().visualData->selectionGeometries;
    return [self webSelectionRectsForSelectionGeometries:selectionGeometries];
}

- (WebKit::TapIdentifier)nextTapIdentifier
{
    _latestTapID = WebKit::TapIdentifier::generate();
    return *_latestTapID;
}

- (void)_highlightLongPressRecognized:(UILongPressGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _highlightLongPressGestureRecognizer);
    [self _resetIsDoubleTapPending];

    auto startPoint = [gestureRecognizer startPoint];

    _lastInteractionLocation = startPoint;

    switch ([gestureRecognizer state]) {
    case UIGestureRecognizerStateBegan:
        _longPressCanClick = YES;
        cancelPotentialTapIfNecessary(self);
        _page->tapHighlightAtPosition(startPoint, [self nextTapIdentifier]);
        _isTapHighlightIDValid = YES;
        break;
    case UIGestureRecognizerStateEnded:
        if (_longPressCanClick && _positionInformation.isElement) {
            [self _attemptSyntheticClickAtLocation:startPoint modifierFlags:gestureRecognizer.modifierFlags];
            [self _finishInteraction];
        } else
            [self _cancelInteraction];
        _longPressCanClick = NO;
        break;
    case UIGestureRecognizerStateCancelled:
        [self _cancelInteraction];
        _longPressCanClick = NO;
        break;
    default:
        break;
    }
}

- (void)_doubleTapRecognizedForDoubleClick:(UITapGestureRecognizer *)gestureRecognizer
{
    RELEASE_ASSERT(_layerTreeTransactionIdAtLastInteractionStart);
    _page->handleDoubleTapForDoubleClickAtPoint(WebCore::IntPoint([gestureRecognizer locationInView:self]), WebKit::webEventModifierFlags(gestureRecognizer.modifierFlags), *_layerTreeTransactionIdAtLastInteractionStart);
}

- (void)_twoFingerSingleTapGestureRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    _isTapHighlightIDValid = YES;
    _isExpectingFastSingleTapCommit = YES;
    _page->handleTwoFingerTapAtPoint(WebCore::roundedIntPoint([gestureRecognizer locationInView:self]), WebKit::webEventModifierFlags(gestureRecognizer.modifierFlags | UIKeyModifierCommand), [self nextTapIdentifier]);
}

- (void)_longPressRecognized:(UILongPressGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _longPressGestureRecognizer);
    [self _resetIsDoubleTapPending];
    [self _cancelTouchEventGestureRecognizer];

    _page->didRecognizeLongPress();

    _lastInteractionLocation = [gestureRecognizer startPoint];

    if ([gestureRecognizer state] == UIGestureRecognizerStateBegan) {
        SEL action = [self _actionForLongPress];
        if (action) {
            [self performSelector:action];
            [self _cancelLongPressGestureRecognizer];
        }
    }
}

- (void)_endPotentialTapAndEnableDoubleTapGesturesIfNecessary
{
    if (self.webView._allowsDoubleTapGestures) {
        RELEASE_LOG(ViewGestures, "ending potential tap - double taps are back. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());

        [self _setDoubleTapGesturesEnabled:YES];
    }

    RELEASE_LOG(ViewGestures, "Ending potential tap. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());

    _potentialTapInProgress = NO;
}

- (BOOL)isPotentialTapInProgress
{
    return _potentialTapInProgress;
}

- (void)_singleTapIdentified:(UITapGestureRecognizer *)gestureRecognizer
{
    auto position = [gestureRecognizer locationInView:self];

    if ([self _handleTapOverInteractiveControl:position])
        return;

    ASSERT(gestureRecognizer == _singleTapGestureRecognizer);
    ASSERT(!_potentialTapInProgress);
    [self _resetIsDoubleTapPending];

    [_inputPeripheral setSingleTapShouldEndEditing:[_inputPeripheral isEditing]];

    bool shouldRequestMagnificationInformation = _page->preferences().fasterClicksEnabled();
    if (shouldRequestMagnificationInformation)
        RELEASE_LOG(ViewGestures, "Single tap identified. Request details on potential zoom. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());

    _page->potentialTapAtPosition(std::nullopt, position, shouldRequestMagnificationInformation, [self nextTapIdentifier]);
    _potentialTapInProgress = YES;
    _isTapHighlightIDValid = YES;
    _isExpectingFastSingleTapCommit = !_doubleTapGestureRecognizer.get().enabled;
}

static void cancelPotentialTapIfNecessary(WKContentView* contentView)
{
    if (contentView->_potentialTapInProgress) {
        [contentView _endPotentialTapAndEnableDoubleTapGesturesIfNecessary];
        [contentView _cancelInteraction];
        contentView->_page->cancelPotentialTap();
    }
}

- (void)_singleTapDidReset:(UITapGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _singleTapGestureRecognizer);
    cancelPotentialTapIfNecessary(self);
    if (auto* singleTapTouchIdentifier = [_singleTapGestureRecognizer lastActiveTouchIdentifier]) {
        WebCore::PointerID pointerId = [singleTapTouchIdentifier unsignedIntValue];
        if (_commitPotentialTapPointerId != pointerId)
            _page->touchWithIdentifierWasRemoved(pointerId);
    }

    if (!_isTapHighlightIDValid)
        [self _fadeTapHighlightViewIfNeeded];
}

- (void)_keyboardDismissalGestureRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _keyboardDismissalGestureRecognizer);

    if (!self._hasFocusedElement)
        return;

    _page->shouldDismissKeyboardAfterTapAtPoint([gestureRecognizer locationInView:self], [weakSelf = WeakObjCPtr<WKContentView>(self), element = _focusedElementInformation.elementContext](bool shouldDismiss) {
        if (!shouldDismiss)
            return;

        RetainPtr strongSelf = weakSelf.get();
        if (![strongSelf _hasFocusedElement] || !strongSelf->_focusedElementInformation.elementContext.isSameElement(element))
            return;

        RELEASE_LOG(ViewGestures, "Dismissing keyboard after tap (%p, pageProxyID=%llu)", strongSelf.get(), strongSelf->_page->identifier().toUInt64());
        [strongSelf _elementDidBlur];
    });
}

- (void)_doubleTapDidFail:(UITapGestureRecognizer *)gestureRecognizer
{
    RELEASE_LOG(ViewGestures, "Double tap was not recognized. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
    ASSERT(gestureRecognizer == _doubleTapGestureRecognizer);
}

- (void)_commitPotentialTapFailed
{
    _page->touchWithIdentifierWasRemoved(_commitPotentialTapPointerId);
    _commitPotentialTapPointerId = 0;

    [self _cancelInteraction];
    
    [self stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::TapGesture];
}

- (void)_didNotHandleTapAsClick:(const WebCore::IntPoint&)point
{
    [self stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::TapGesture];

    if (!_isDoubleTapPending)
        return;

    _smartMagnificationController->handleSmartMagnificationGesture(_lastInteractionLocation);
    _isDoubleTapPending = NO;
}

- (void)_didHandleTapAsHover
{
    [self stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::TapGesture];
}

- (void)_didCompleteSyntheticClick
{
    _page->touchWithIdentifierWasRemoved(_commitPotentialTapPointerId);
    _commitPotentialTapPointerId = 0;

    RELEASE_LOG(ViewGestures, "Synthetic click completed. (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());
    [self stopDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::TapGesture];
}

#if ENABLE(MODEL_PROCESS)
- (void)_modelInteractionPanGestureRecognized:(UIPanGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _modelInteractionPanGestureRecognizer);

    _lastInteractionLocation = [gestureRecognizer locationInView:self];

    switch (gestureRecognizer.state) {
    case UIGestureRecognizerStateBegan: {
        [self modelInteractionPanGestureDidBeginAtPoint:_lastInteractionLocation];
        break;
    }
    case UIGestureRecognizerStateChanged: {
        [self modelInteractionPanGestureDidUpdateWithPoint:_lastInteractionLocation];
        break;
    }
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled: {
        [self modelInteractionPanGestureDidEnd];
        break;
    }
    default:
        break;
    }
}
#endif

- (void)_singleTapRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    ASSERT(gestureRecognizer == _singleTapGestureRecognizer);

    if (!_potentialTapInProgress)
        return;

    if (![self isFirstResponder]) {
        [self startDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::TapGesture];
        [self becomeFirstResponder];
    }

    _lastInteractionLocation = [gestureRecognizer locationInView:self];

    [self _endPotentialTapAndEnableDoubleTapGesturesIfNecessary];

    if (_hasTapHighlightForPotentialTap) {
        [self _showTapHighlight];
        _hasTapHighlightForPotentialTap = NO;
    }

    if ([_inputPeripheral singleTapShouldEndEditing])
        [_inputPeripheral endEditing];

    RELEASE_LOG(ViewGestures, "Single tap recognized - commit potential tap (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());

    WebCore::PointerID pointerId = WebCore::mousePointerID;
    if (auto* singleTapTouchIdentifier = [_singleTapGestureRecognizer lastActiveTouchIdentifier]) {
        pointerId = [singleTapTouchIdentifier unsignedIntValue];
        _commitPotentialTapPointerId = pointerId;
    }
    RELEASE_ASSERT(_layerTreeTransactionIdAtLastInteractionStart);
    _page->commitPotentialTap(std::nullopt, WebKit::webEventModifierFlags(gestureRecognizer.modifierFlags), *_layerTreeTransactionIdAtLastInteractionStart, pointerId);

    if (!_isExpectingFastSingleTapCommit)
        [self _finishInteraction];

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if (![_imageAnalysisInteraction interactableItemExistsAtPoint:_lastInteractionLocation])
        [_imageAnalysisInteraction resetSelection];
#endif
}

- (void)_doubleTapRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    RELEASE_LOG(ViewGestures, "Identified a double tap (%p, pageProxyID=%llu)", self, _page->identifier().toUInt64());

    [self _resetIsDoubleTapPending];

    auto location = [gestureRecognizer locationInView:self];
    _lastInteractionLocation = location;
    _smartMagnificationController->handleSmartMagnificationGesture(location);
}

- (void)_resetIsDoubleTapPending
{
    _isDoubleTapPending = NO;
}

- (void)_nonBlockingDoubleTapRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    _lastInteractionLocation = [gestureRecognizer locationInView:self];
    _isDoubleTapPending = YES;
}

- (void)_twoFingerDoubleTapRecognized:(UITapGestureRecognizer *)gestureRecognizer
{
    [self _resetIsDoubleTapPending];

    auto location = [gestureRecognizer locationInView:self];
    _lastInteractionLocation = location;
    _smartMagnificationController->handleResetMagnificationGesture(location);
}

- (void)_attemptSyntheticClickAtLocation:(CGPoint)location modifierFlags:(UIKeyModifierFlags)modifierFlags
{
    if (![self isFirstResponder])
        [self becomeFirstResponder];

    [_inputPeripheral endEditing];
    RELEASE_ASSERT(_layerTreeTransactionIdAtLastInteractionStart);
    _page->attemptSyntheticClick(location, WebKit::webEventModifierFlags(modifierFlags), *_layerTreeTransactionIdAtLastInteractionStart);
}

- (void)setUpTextSelectionAssistant
{
    if (!_textInteractionWrapper)
        _textInteractionWrapper = adoptNS([[WKTextInteractionWrapper alloc] initWithView:self]);
    else {
        // Reset the gesture recognizers in case editability has changed.
        [_textInteractionWrapper setGestureRecognizers];
    }

    _cachedTextInteractionLoupeGestureRecognizer = nil;
    _cachedTextInteractionTapGestureRecognizer = nil;
}

- (void)pasteWithCompletionHandler:(void (^)(void))completionHandler
{
    _page->executeEditCommand("Paste"_s, { }, [completion = makeBlockPtr(completionHandler)] {
        if (completion)
            completion();
    });
}

- (void)clearSelection
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalClearSelection];
}

- (void)_internalClearSelection
{
    [self _elementDidBlur];
    _page->clearSelection();
}

- (void)_invalidateCurrentPositionInformation
{
    _hasValidPositionInformation = NO;
    _positionInformation = { };
}

- (void)_positionInformationDidChange:(const WebKit::InteractionInformationAtPosition&)info
{
    if (_lastOutstandingPositionInformationRequest && info.request.isValidForRequest(*_lastOutstandingPositionInformationRequest))
        _lastOutstandingPositionInformationRequest = std::nullopt;

    _isWaitingOnPositionInformation = NO;

    WebKit::InteractionInformationAtPosition newInfo = info;
    newInfo.mergeCompatibleOptionalInformation(_positionInformation);

    _positionInformation = newInfo;
    _hasValidPositionInformation = _positionInformation.canBeValid;
    if (_actionSheetAssistant)
        [_actionSheetAssistant updateSheetPosition];
    [self _invokeAndRemovePendingHandlersValidForCurrentPositionInformation];
}

- (void)_willStartScrollingOrZooming
{
    [_textInteractionWrapper willStartScrollingOrZooming];
    _page->setIsScrollingOrZooming(true);

#if HAVE(PEPPER_UI_CORE)
    [_focusedFormControlView disengageFocusedFormControlNavigation];
#endif
}

- (void)scrollViewWillStartPanOrPinchGesture
{
    _page->hideValidationMessage();

    [_keyboardScrollingAnimator willStartInteractiveScroll];

    _touchEventsCanPreventNativeGestures = NO;
}

- (void)_didEndScrollingOrZooming
{
    if (!_needsDeferredEndScrollingSelectionUpdate)
        [_textInteractionWrapper didEndScrollingOrZooming];
    _page->setIsScrollingOrZooming(false);

    [self _resetPanningPreventionFlags];

#if HAVE(PEPPER_UI_CORE)
    [_focusedFormControlView engageFocusedFormControlNavigation];
#endif
}

- (BOOL)_elementTypeRequiresAccessoryView:(WebKit::InputType)type
{
    switch (type) {
    case WebKit::InputType::None:
    case WebKit::InputType::Color:
    case WebKit::InputType::Drawing:
    case WebKit::InputType::Date:
    case WebKit::InputType::DateTimeLocal:
    case WebKit::InputType::Month:
    case WebKit::InputType::Time:
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return NO;
    case WebKit::InputType::Select: {
        if (self._shouldUseContextMenusForFormControls)
            return NO;
        return PAL::currentUserInterfaceIdiomIsSmallScreen();
    }
    case WebKit::InputType::Text:
    case WebKit::InputType::Password:
    case WebKit::InputType::Search:
    case WebKit::InputType::Email:
    case WebKit::InputType::URL:
    case WebKit::InputType::Phone:
    case WebKit::InputType::Number:
    case WebKit::InputType::NumberPad:
    case WebKit::InputType::ContentEditable:
    case WebKit::InputType::TextArea:
#if !ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return PAL::currentUserInterfaceIdiomIsSmallScreen();
    }
}

- (BOOL)requiresAccessoryView
{
#if ENABLE(WRITING_TOOLS)
    if (_isPresentingWritingTools)
        return NO;
#endif

    if ([_formInputSession accessoryViewShouldNotShow])
        return NO;

    if ([_formInputSession customInputAccessoryView])
        return YES;

    if ([_webView _isDisplayingPDF])
        return NO;

    return [self _elementTypeRequiresAccessoryView:_focusedElementInformation.elementType];
}

- (UITextInputAssistantItem *)inputAssistantItem
{
    return [_webView inputAssistantItem];
}

- (UITextInputAssistantItem *)inputAssistantItemForWebView
{
    return [super inputAssistantItem];
}

- (UIView *)inputAccessoryView
{
    return [_webView inputAccessoryView];
}

- (UIView *)inputAccessoryViewForWebView
{
    if (![self requiresAccessoryView])
        return nil;

    return [_formInputSession customInputAccessoryView] ?: self.formAccessoryView;
}

- (NSArray *)supportedPasteboardTypesForCurrentSelection
{
    if (_page->editorState().selectionIsNone)
        return nil;

    if (_page->editorState().isContentRichlyEditable)
        return WebKit::supportedRichTextPasteboardTypes();

    return WebKit::supportedPlainTextPasteboardTypes();
}

#define FORWARD_ACTION_TO_WKWEBVIEW(_action) \
    - (void)_action:(id)sender \
    { \
        SEL action = @selector(_action:);\
        [self _willPerformAction:action sender:sender];\
        [_webView _action:sender]; \
        [self _didPerformAction:action sender:sender];\
    }

FOR_EACH_WKCONTENTVIEW_ACTION(FORWARD_ACTION_TO_WKWEBVIEW)
FOR_EACH_PRIVATE_WKCONTENTVIEW_ACTION(FORWARD_ACTION_TO_WKWEBVIEW)

#undef FORWARD_ACTION_TO_WKWEBVIEW

- (void)_lookupForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self lookupForWebView:sender];
}

- (void)defineForWebView:(id)sender
{
    [self lookupForWebView:sender];
}

- (void)lookupForWebView:(id)sender
{
    _page->getSelectionContext([view = retainPtr(self)](const String& selectedText, const String& textBefore, const String& textAfter) {
        if (!selectedText)
            return;

        auto& editorState = view->_page->editorState();
        if (!editorState.hasPostLayoutAndVisualData())
            return;
        auto& visualData = *editorState.visualData;
        CGRect presentationRect;
        if (editorState.selectionIsRange && !visualData.selectionGeometries.isEmpty())
            presentationRect = view->_page->selectionBoundingRectInRootViewCoordinates();
        else
            presentationRect = visualData.caretRectAtStart;
        
        auto selectionContext = makeString(textBefore, selectedText, textAfter);
        NSRange selectedRangeInContext = NSMakeRange(textBefore.length(), selectedText.length());

        if (auto textSelectionAssistant = view->_textInteractionWrapper)
            [textSelectionAssistant lookup:selectionContext.createNSString().get() withRange:selectedRangeInContext fromRect:presentationRect];
    });
}

- (void)_shareForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self shareForWebView:sender];
}

- (void)shareForWebView:(id)sender
{
    RetainPtr<WKContentView> view = self;
    _page->getSelectionOrContentsAsString([view](const String& string) {
        if (!view->_textInteractionWrapper || !string || !view->_page->editorState().hasVisualData())
            return;

        auto& selectionGeometries = view->_page->editorState().visualData->selectionGeometries;
        if (selectionGeometries.isEmpty())
            return;

        [view->_textInteractionWrapper showShareSheetFor:string.createNSString().get() fromRect:selectionGeometries.first().rect()];
    });
}

- (void)_translateForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self translateForWebView:sender];
}

- (void)translateForWebView:(id)sender
{
    _page->getSelectionOrContentsAsString([weakSelf = WeakObjCPtr<WKContentView>(self)] (const String& string) {
        if (!weakSelf)
            return;

        if (string.isEmpty())
            return;

        auto strongSelf = weakSelf.get();
        if (!strongSelf->_page->editorState().hasVisualData())
            return;

        if (strongSelf->_page->editorState().visualData->selectionGeometries.isEmpty())
            return;

        [strongSelf->_textInteractionWrapper translate:string.createNSString().get() fromRect:strongSelf->_page->selectionBoundingRectInRootViewCoordinates()];
    });
}

- (void)_addShortcutForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self addShortcutForWebView:sender];
}

- (void)addShortcutForWebView:(id)sender
{
    if (!_page->editorState().visualData)
        return;
    [_textInteractionWrapper showTextServiceFor:[self selectedText] fromRect:_page->editorState().visualData->selectionGeometries[0].rect()];
}

- (NSString *)selectedText
{
    if (!_page->editorState().postLayoutData)
        return nil;
    return _page->editorState().postLayoutData->wordAtSelection.createNSString().autorelease();
}

- (NSArray *)alternativesForSelectedText
{
    if (!_page->editorState().postLayoutData)
        return nil;
    auto& dictationContextsForSelection = _page->editorState().postLayoutData->dictationContextsForSelection;
    return createNSArray(dictationContextsForSelection, [&] (auto& dictationContext) -> NSObject * {
        RetainPtr alternatives = _page->protectedPageClient()->platformDictationAlternatives(dictationContext);
#if USE(BROWSERENGINEKIT)
        if (!self.shouldUseAsyncInteractions)
            return [[alternatives _nsTextAlternative] autorelease];
#endif
        return alternatives.autorelease();
    }).autorelease();
}

- (void)makeTextWritingDirectionNaturalForWebView:(id)sender
{
    // Match platform behavior on iOS as well as legacy WebKit behavior by modifying the
    // base (paragraph) writing direction rather than the inline direction.
    _page->setBaseWritingDirection(WebCore::WritingDirection::Natural);
}

- (void)makeTextWritingDirectionLeftToRightForWebView:(id)sender
{
    _page->setBaseWritingDirection(WebCore::WritingDirection::LeftToRight);
}

- (void)makeTextWritingDirectionRightToLeftForWebView:(id)sender
{
    _page->setBaseWritingDirection(WebCore::WritingDirection::RightToLeft);
}

- (BOOL)isReplaceAllowed
{
    if (!_page->editorState().postLayoutData)
        return NO;
    return _page->editorState().postLayoutData->isReplaceAllowed;
}

- (void)replaceText:(NSString *)text withText:(NSString *)word
{
    _autocorrectionContextNeedsUpdate = YES;
    _page->replaceSelectedText(text, word);
}

- (void)_promptForReplaceForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self promptForReplaceForWebView:sender];
}

- (void)promptForReplaceForWebView:(id)sender
{
    if (!_page->editorState().postLayoutData)
        return;
    const auto& wordAtSelection = _page->editorState().postLayoutData->wordAtSelection;
    if (wordAtSelection.isEmpty())
        return;

    [_textInteractionWrapper scheduleReplacementsForText:wordAtSelection.createNSString().get()];
}

- (void)_transliterateChineseForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self transliterateChineseForWebView:sender];
}

- (void)transliterateChineseForWebView:(id)sender
{
    if (!_page->editorState().postLayoutData)
        return;
    [_textInteractionWrapper scheduleChineseTransliterationForText:_page->editorState().postLayoutData->wordAtSelection.createNSString().get()];
}

- (void)replaceForWebView:(id)sender
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        [_asyncInputDelegate textInput:self.asBETextInput deferReplaceTextActionToSystem:sender];
        return;
    }
#endif
    [[UIKeyboardImpl sharedInstance] replaceText:sender];
}

#define WEBCORE_COMMAND_FOR_WEBVIEW(command) \
    - (void)_ ## command ## ForWebView:(id)sender { _page->executeEditCommand(#command ## _s); } \
    - (void)command ## ForWebView:(id)sender { [self _ ## command ## ForWebView:sender]; }
WEBCORE_COMMAND_FOR_WEBVIEW(insertOrderedList);
WEBCORE_COMMAND_FOR_WEBVIEW(insertUnorderedList);
WEBCORE_COMMAND_FOR_WEBVIEW(insertNestedOrderedList);
WEBCORE_COMMAND_FOR_WEBVIEW(insertNestedUnorderedList);
WEBCORE_COMMAND_FOR_WEBVIEW(indent);
WEBCORE_COMMAND_FOR_WEBVIEW(outdent);
WEBCORE_COMMAND_FOR_WEBVIEW(alignLeft);
WEBCORE_COMMAND_FOR_WEBVIEW(alignRight);
WEBCORE_COMMAND_FOR_WEBVIEW(alignCenter);
WEBCORE_COMMAND_FOR_WEBVIEW(alignJustified);
WEBCORE_COMMAND_FOR_WEBVIEW(pasteAndMatchStyle);
#undef WEBCORE_COMMAND_FOR_WEBVIEW

- (void)_increaseListLevelForWebView:(id)sender
{
    _page->increaseListLevel();
}

- (void)_decreaseListLevelForWebView:(id)sender
{
    _page->decreaseListLevel();
}

- (void)_changeListTypeForWebView:(id)sender
{
    _page->changeListType();
}

- (void)_toggleStrikeThroughForWebView:(id)sender
{
    _page->executeEditCommand("StrikeThrough"_s);
}

- (void)increaseSizeForWebView:(id)sender
{
    _page->executeEditCommand("FontSizeDelta"_s, "1"_s);
}

- (void)decreaseSizeForWebView:(id)sender
{
    _page->executeEditCommand("FontSizeDelta"_s, "-1"_s);
}

- (void)_setFontForWebView:(UIFont *)font sender:(id)sender
{
    WebCore::FontChanges changes;
    changes.setFontFamily(font.familyName);
    changes.setFontName(font.fontName);
    changes.setFontSize(font.pointSize);
    changes.setBold(font.traits & UIFontTraitBold);
    changes.setItalic(font.traits & UIFontTraitItalic);

    if (NSString *textStyleAttribute = [font.fontDescriptor.fontAttributes objectForKey:UIFontDescriptorTextStyleAttribute])
        changes.setFontFamily(textStyleAttribute);

    _page->changeFont(WTFMove(changes));
}

- (void)_setFontSizeForWebView:(CGFloat)fontSize sender:(id)sender
{
    WebCore::FontChanges changes;
    changes.setFontSize(fontSize);
    _page->changeFont(WTFMove(changes));
}

- (void)_setTextColorForWebView:(UIColor *)color sender:(id)sender
{
    WebCore::Color textColor(WebCore::roundAndClampToSRGBALossy(color.CGColor));
    _page->executeEditCommand("ForeColor"_s, WebCore::serializationForHTML(textColor));
}

- (void)toggleStrikeThroughForWebView:(id)sender
{
    [self _toggleStrikeThroughForWebView:sender];
}

- (NSDictionary *)textStylingAtPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction
{
    NSMutableDictionary* result = [NSMutableDictionary dictionary];
    [result setObject:[UIColor blackColor] forKey:NSForegroundColorAttributeName];
    if (!position || !_page->editorState().isContentRichlyEditable)
        return result;

    if (!_page->editorState().postLayoutData)
        return result;

    auto typingAttributes = _page->editorState().postLayoutData->typingAttributes;

    RetainPtr font = _autocorrectionData.font.get();
    double zoomScale = self._contentZoomScale;
    if (std::abs(zoomScale - 1) > FLT_EPSILON)
        font = [font fontWithSize:[font pointSize] * zoomScale];

    if (font) {
        auto originalTraits = [font fontDescriptor].symbolicTraits;
        auto newTraits = originalTraits;
        if (typingAttributes.contains(WebKit::TypingAttribute::Bold))
            newTraits |= UIFontDescriptorTraitBold;

        if (typingAttributes.contains(WebKit::TypingAttribute::Italics))
            newTraits |= UIFontDescriptorTraitItalic;

        if (originalTraits != newTraits) {
            RetainPtr descriptor = [[font fontDescriptor] ?: adoptNS([UIFontDescriptor new]) fontDescriptorWithSymbolicTraits:newTraits];
            if (RetainPtr fontWithTraits = [UIFont fontWithDescriptor:descriptor.get() size:[font pointSize]])
                font = WTFMove(fontWithTraits);
        }
        [result setObject:font.get() forKey:NSFontAttributeName];
    }

    if (typingAttributes.contains(WebKit::TypingAttribute::Underline))
        [result setObject:@(NSUnderlineStyleSingle) forKey:NSUnderlineStyleAttributeName];

    return result;
}

- (UIColor *)insertionPointColor
{
    // On macCatalyst we need to explicitly return the color we have calculated, rather than rely on textTraits, as on macCatalyst, UIKit ignores text traits.
#if PLATFORM(MACCATALYST)
    return [self _cascadeInteractionTintColor];
#else
#if USE(BROWSERENGINEKIT)
    if (!self._requiresLegacyTextInputTraits)
        return self.extendedTraitsDelegate.insertionPointColor;
#endif
    return [self.textInputTraits insertionPointColor];
#endif
}

- (UIColor *)selectionBarColor
{
#if USE(BROWSERENGINEKIT)
    if (!self._requiresLegacyTextInputTraits)
        return self.extendedTextInputTraits.selectionHandleColor;
#endif
    return [self.textInputTraits selectionBarColor];
}

- (UIColor *)selectionHighlightColor
{
#if USE(BROWSERENGINEKIT)
    if (!self._requiresLegacyTextInputTraits)
        return self.extendedTraitsDelegate.selectionHighlightColor;
#endif
    return [self.textInputTraits selectionHighlightColor];
}

- (BOOL)_hasCustomTintColor
{
    if (!_cachedHasCustomTintColor) {
        auto defaultTintColor = WebCore::colorFromCocoaColor([adoptNS([UIView new]) tintColor]);
        _cachedHasCustomTintColor = defaultTintColor != WebCore::colorFromCocoaColor(self.tintColor);
    }
    return *_cachedHasCustomTintColor;
}

- (UIColor *)_cascadeInteractionTintColor
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (!self.webView.configuration._textInteractionGesturesEnabled)
        return [UIColor clearColor];
ALLOW_DEPRECATED_DECLARATIONS_END

    if (!_page->preferences().textInteractionEnabled())
        return [UIColor clearColor];

    RetainPtr<UIColor> caretColorFromStyle;
    BOOL hasExplicitlySetCaretColorFromStyle = NO;
    if (_page->editorState().hasPostLayoutData()) {
        auto& postLayoutData = *_page->editorState().postLayoutData;
        if (auto caretColor = postLayoutData.caretColor; caretColor.isValid()) {
            caretColorFromStyle = cocoaColor(caretColor);
            hasExplicitlySetCaretColorFromStyle = !postLayoutData.hasCaretColorAuto;
        }
    }

    if (hasExplicitlySetCaretColorFromStyle)
        return caretColorFromStyle.autorelease();

    auto tintColorFromNativeView = self.tintColor;
    if (self._hasCustomTintColor)
        return tintColorFromNativeView;

    return caretColorFromStyle.autorelease() ?: tintColorFromNativeView;
}

- (void)_updateTextInputTraitsForInteractionTintColor
{
#if !PLATFORM(WATCHOS)
    auto tintColor = [self _cascadeInteractionTintColor];
    [_legacyTextInputTraits _setColorsToMatchTintColor:tintColor];
    [_extendedTextInputTraits setSelectionColorsToMatchTintColor:tintColor];
#endif
}

- (void)tintColorDidChange
{
    [super tintColorDidChange];

    _cachedHasCustomTintColor = std::nullopt;

    BOOL shouldUpdateTextSelection = self.isFirstResponder && [self canShowNonEmptySelectionView];
    if (shouldUpdateTextSelection)
        [_textInteractionWrapper deactivateSelection];
    [self _updateTextInputTraitsForInteractionTintColor];
    if (shouldUpdateTextSelection)
        [_textInteractionWrapper activateSelection];

    _page->insertionPointColorDidChange();
}

- (BOOL)shouldAllowHighlightLinkCreation
{
    URL url { _page->currentURL() };
    if (!url.isValid() || !url.protocolIsInHTTPFamily() || [_webView _isDisplayingPDF])
        return NO;

    auto editorState = _page->editorState();
    return editorState.selectionIsRange && !editorState.isContentEditable && !editorState.selectionIsRangeInsideImageOverlay;
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender
{
    if (_domPasteRequestHandler)
        return action == @selector(paste:);

    auto& editorState = _page->editorState();
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        if (action == @selector(moveInLayoutDirection:) || action == @selector(extendInLayoutDirection:) || action == @selector(moveInStorageDirection:byGranularity:) || action == @selector(extendInStorageDirection:byGranularity:))
            return !editorState.selectionIsNone;

        if (action == @selector(deleteInDirection:toGranularity:) || action == @selector(transposeCharactersAroundSelection))
            return editorState.isContentEditable;
    } else
#endif // USE(BROWSERENGINEKIT)
    {
        // These are UIKit IPI selectors. We don't want to forward them to the web view.
        if (action == @selector(_moveDown:withHistory:) || action == @selector(_moveLeft:withHistory:) || action == @selector(_moveRight:withHistory:)
            || action == @selector(_moveToEndOfDocument:withHistory:) || action == @selector(_moveToEndOfLine:withHistory:) || action == @selector(_moveToEndOfParagraph:withHistory:)
            || action == @selector(_moveToEndOfWord:withHistory:) || action == @selector(_moveToStartOfDocument:withHistory:) || action == @selector(_moveToStartOfLine:withHistory:)
            || action == @selector(_moveToStartOfParagraph:withHistory:) || action == @selector(_moveToStartOfWord:withHistory:) || action == @selector(_moveUp:withHistory:))
            return !editorState.selectionIsNone;

        if (action == @selector(_deleteByWord) || action == @selector(_deleteForwardByWord) || action == @selector(_deleteForwardAndNotify:)
            || action == @selector(_deleteToEndOfParagraph) || action == @selector(_deleteToStartOfLine) || action == @selector(_transpose))
            return editorState.isContentEditable;
    }

    return [_webView canPerformAction:action withSender:sender];
}

- (BOOL)canPerformActionForWebView:(SEL)action withSender:(id)sender
{
    if (_domPasteRequestHandler)
        return action == @selector(paste:);

    if (action == @selector(_nextAccessoryTab:))
        return self._hasFocusedElement && _focusedElementInformation.hasNextNode;
    if (action == @selector(_previousAccessoryTab:))
        return self._hasFocusedElement && _focusedElementInformation.hasPreviousNode;

    auto editorState = _page->editorState();
    // FIXME: Some of the following checks should be removed once internal clients move to the underscore-prefixed versions.
    if (action == @selector(toggleBoldface:) || action == @selector(toggleItalics:) || action == @selector(toggleUnderline:) || action == @selector(_toggleStrikeThrough:)
        || action == @selector(_alignLeft:) || action == @selector(_alignRight:) || action == @selector(_alignCenter:) || action == @selector(_alignJustified:)
        || action == @selector(alignLeft:) || action == @selector(alignRight:) || action == @selector(alignCenter:) || action == @selector(alignJustified:)
        || action == @selector(_setTextColor:sender:) || action == @selector(_setFont:sender:) || action == @selector(_setFontSize:sender:)
        || action == @selector(_insertOrderedList:) || action == @selector(_insertUnorderedList:) || action == @selector(_insertNestedOrderedList:) || action == @selector(_insertNestedUnorderedList:)
        || action == @selector(_increaseListLevel:) || action == @selector(_decreaseListLevel:) || action == @selector(_changeListType:) || action == @selector(_indent:) || action == @selector(_outdent:)
        || action == @selector(increaseSize:) || action == @selector(decreaseSize:) || action == @selector(makeTextWritingDirectionNatural:)) {
        // FIXME: This should be more nuanced in the future, rather than returning YES for all richly editable areas. For instance, outdent: should be disabled when the selection is already
        // at the outermost indentation level.
        return editorState.isContentRichlyEditable;
    }
    if (action == @selector(cut:))
        return !editorState.isInPasswordField && editorState.isContentEditable && editorState.selectionIsRange;

    if (action == @selector(paste:) || action == @selector(_pasteAsQuotation:) || action == @selector(_pasteAndMatchStyle:) || action == @selector(pasteAndMatchStyle:)) {
        if (editorState.selectionIsNone || !editorState.isContentEditable)
            return NO;
        UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
        NSArray *types = [self supportedPasteboardTypesForCurrentSelection];
        NSIndexSet *indices = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, [pasteboard numberOfItems])];
        if ([pasteboard containsPasteboardTypes:types inItemSet:indices])
            return YES;

#if PLATFORM(IOS) || PLATFORM(VISION)
        if (editorState.isContentRichlyEditable && _page->preferences().attachmentElementEnabled()) {
            for (NSItemProvider *itemProvider in pasteboard.itemProviders) {
                auto preferredPresentationStyle = itemProvider.preferredPresentationStyle;
                if (preferredPresentationStyle == UIPreferredPresentationStyleInline)
                    continue;

                if (preferredPresentationStyle == UIPreferredPresentationStyleUnspecified && !itemProvider.suggestedName.length)
                    continue;

                if (itemProvider.web_fileUploadContentTypes.count)
                    return YES;
            }
        }
#endif // PLATFORM(IOS) || PLATFORM(VISION)
    }

    if (action == @selector(copy:)) {
        if (editorState.isInPasswordField && !editorState.selectionIsRangeInAutoFilledAndViewableField)
            return NO;
        return editorState.selectionIsRange;
    }

    if (action == @selector(_define:) || action == @selector(define:) || action == @selector(lookup:)) {
        if (editorState.isInPasswordField || !editorState.selectionIsRange)
            return NO;

        NSUInteger textLength = editorState.postLayoutData ? editorState.postLayoutData->selectedTextLength : 0;
        // FIXME: We should be calling UIReferenceLibraryViewController to check if the length is
        // acceptable, but the interface takes a string.
        // <rdar://problem/15254406>
        if (!textLength || textLength > 200)
            return NO;

#if !PLATFORM(MACCATALYST)
        if ([(MCProfileConnection *)[PAL::getMCProfileConnectionClass() sharedConnection] effectiveBoolValueForSetting:PAL::get_ManagedConfiguration_MCFeatureDefinitionLookupAllowed()] == MCRestrictedBoolExplicitNo)
            return NO;
#endif
            
        return YES;
    }

    if (action == @selector(_lookup:)) {
        if (editorState.isInPasswordField)
            return NO;

#if !PLATFORM(MACCATALYST)
        if ([(MCProfileConnection *)[PAL::getMCProfileConnectionClass() sharedConnection] effectiveBoolValueForSetting:PAL::get_ManagedConfiguration_MCFeatureDefinitionLookupAllowed()] == MCRestrictedBoolExplicitNo)
            return NO;
#endif

        return editorState.selectionIsRange;
    }

    if (action == @selector(_share:) || action == @selector(share:)) {
        if (editorState.isInPasswordField || !editorState.selectionIsRange)
            return NO;

        return editorState.postLayoutData && editorState.postLayoutData->selectedTextLength > 0;
    }

    if (action == @selector(_addShortcut:) || action == @selector(addShortcut:)) {
        if (editorState.isInPasswordField || !editorState.selectionIsRange)
            return NO;

        NSString *selectedText = [self selectedText];
        if (![selectedText length])
            return NO;

        if (!UIKeyboardEnabledInputModesAllowOneToManyShortcuts())
            return NO;
        if (![selectedText _containsCJScripts])
            return NO;
        return YES;
    }

    if (action == @selector(_promptForReplace:) || action == @selector(promptForReplace:)) {
        if (!editorState.selectionIsRange || !editorState.postLayoutData || !editorState.postLayoutData->isReplaceAllowed || ![[UIKeyboardImpl activeInstance] autocorrectSpellingEnabled])
            return NO;
        if ([[self selectedText] _containsCJScriptsOnly])
            return NO;
        return YES;
    }

    if (action == @selector(_transliterateChinese:) || action == @selector(transliterateChinese:)) {
        if (!editorState.selectionIsRange || !editorState.postLayoutData || !editorState.postLayoutData->isReplaceAllowed || ![[UIKeyboardImpl activeInstance] autocorrectSpellingEnabled])
            return NO;
        return UIKeyboardEnabledInputModesAllowChineseTransliterationForText([self selectedText]);
    }

#if HAVE(TRANSLATION_UI_SERVICES)
    if (action == @selector(_translate:) || action == @selector(translate:)) {
        if (!PAL::isTranslationUIServicesFrameworkAvailable() || ![PAL::getLTUITranslationViewControllerClass() isAvailable])
            return NO;
        return !editorState.isInPasswordField && editorState.selectionIsRange;
    }
#endif // HAVE(TRANSLATION_UI_SERVICES)

    if (action == @selector(select:)) {
        // Disable select in password fields so that you can't see word boundaries.
        return !editorState.isInPasswordField && !editorState.selectionIsRange && self._hasContent;
    }

    auto isPreparingEditMenu = [&] {
        return [sender isKindOfClass:UIKeyCommand.class] && !_isInterpretingKeyEvent;
    };

    if (action == @selector(selectAll:)) {
        if (isPreparingEditMenu()) {
            // By platform convention we don't show Select All in the edit menu for a range selection.
            return !editorState.selectionIsRange && self._hasContent;
        }
        return YES;
    }

    if (action == @selector(replace:))
        return editorState.isContentEditable && !editorState.isInPasswordField;

    if (action == @selector(makeTextWritingDirectionLeftToRight:) || action == @selector(makeTextWritingDirectionRightToLeft:)) {
        if (!editorState.isContentEditable)
            return NO;

        if (!editorState.postLayoutData)
            return NO;
        auto baseWritingDirection = editorState.postLayoutData->baseWritingDirection;
        if (baseWritingDirection == WebCore::WritingDirection::LeftToRight && !UIKeyboardIsRightToLeftInputModeActive()) {
            // A keyboard is considered "active" if it is available for the user to switch to. As such, this check prevents
            // text direction actions from showing up in the case where a user has only added left-to-right keyboards, and
            // is also not editing right-to-left content.
            return NO;
        }

        if (action == @selector(makeTextWritingDirectionLeftToRight:))
            return baseWritingDirection != WebCore::WritingDirection::LeftToRight;

        return baseWritingDirection != WebCore::WritingDirection::RightToLeft;
    }

#if ENABLE(IMAGE_ANALYSIS)
    if (action == @selector(captureTextFromCamera:)) {
        if (!mayContainSelectableText(_focusedElementInformation.elementType) || _focusedElementInformation.isReadOnly)
            return NO;

        if (isPreparingEditMenu() && editorState.selectionIsRange)
            return NO;
    }
#endif // ENABLE(IMAGE_ANALYSIS)

#if HAVE(UIFINDINTERACTION)
    if (action == @selector(useSelectionForFind:) || action == @selector(findSelected:) || action == @selector(_findSelected:)) {
        if (!self.webView._findInteractionEnabled)
            return NO;

        if (!editorState.selectionIsRange || !self.selectedText.length)
            return NO;

        return YES;
    }
#endif

    return [super canPerformAction:action withSender:sender];
}

- (id)targetForAction:(SEL)action withSender:(id)sender
{
    return [_webView targetForAction:action withSender:sender];
}

- (id)targetForActionForWebView:(SEL)action withSender:(id)sender
{
    BOOL hasFallbackAsyncTextInputAction = action == @selector(_define:) || action == @selector(_translate:) || action == @selector(_lookup:);
    if (self.shouldUseAsyncInteractions && hasFallbackAsyncTextInputAction)
        return nil;
    return [super targetForAction:action withSender:sender];
}

- (void)_willHideMenu:(NSNotification *)notification
{
    [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
}

- (void)_keyboardDidRequestDismissal:(NSNotification *)notification
{
    if (_isEditable && [self isFirstResponder])
        _keyboardDidRequestDismissal = YES;

#if USE(UICONTEXTMENU)
    [_fileUploadPanel repositionContextMenuIfNeeded:WebKit::KeyboardIsDismissing::Yes];
#endif
}

- (void)copyForWebView:(id)sender
{
    _page->executeEditCommand("copy"_s);
}

- (void)cutForWebView:(id)sender
{
    [self _executeEditCommand:@"cut"];
}

- (void)pasteForWebView:(id)sender
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if (sender == UIMenuController.sharedMenuController && [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::GrantedForGesture])
        return;
ALLOW_DEPRECATED_DECLARATIONS_END

    _autocorrectionContextNeedsUpdate = YES;
    _page->executeEditCommand("paste"_s);
}

- (void)_pasteAsQuotationForWebView:(id)sender
{
    _autocorrectionContextNeedsUpdate = YES;
    _page->executeEditCommand("PasteAsQuotation"_s);
}

- (void)selectForWebView:(id)sender
{
    if (!_page->preferences().textInteractionEnabled())
        return;

    _autocorrectionContextNeedsUpdate = YES;
    [_textInteractionWrapper selectWord];
    // We cannot use selectWord command, because we want to be able to select the word even when it is the last in the paragraph.
    _page->extendSelection(WebCore::TextGranularity::WordGranularity, [] { });
}

- (void)selectAllForWebView:(id)sender
{
    if (!_page->preferences().textInteractionEnabled())
        return;

    _autocorrectionContextNeedsUpdate = YES;
    [_textInteractionWrapper selectAll:sender];
    _page->selectAll();
}

- (BOOL)shouldSynthesizeKeyEvents
{
    if (_focusedElementInformation.shouldSynthesizeKeyEventsForEditing && self.hasHiddenContentEditable)
        return true;
    return false;
}

- (void)toggleBoldfaceForWebView:(id)sender
{
    if (!_page->editorState().isContentRichlyEditable)
        return;

    [self _executeEditCommand:@"toggleBold"];

    if (self.shouldSynthesizeKeyEvents)
        _page->generateSyntheticEditingCommand(WebKit::SyntheticEditingCommandType::ToggleBoldface);
}

- (void)toggleItalicsForWebView:(id)sender
{
    if (!_page->editorState().isContentRichlyEditable)
        return;

    [self _executeEditCommand:@"toggleItalic"];

    if (self.shouldSynthesizeKeyEvents)
        _page->generateSyntheticEditingCommand(WebKit::SyntheticEditingCommandType::ToggleItalic);
}

- (void)toggleUnderlineForWebView:(id)sender
{
    if (!_page->editorState().isContentRichlyEditable)
        return;

    [self _executeEditCommand:@"toggleUnderline"];

    if (self.shouldSynthesizeKeyEvents)
        _page->generateSyntheticEditingCommand(WebKit::SyntheticEditingCommandType::ToggleUnderline);
}

- (void)_defineForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self lookupForWebView:sender];
}

- (void)accessibilityRetrieveSpeakSelectionContent
{
    RetainPtr<WKContentView> view = self;
    RetainPtr<WKWebView> webView = _webView.get();
    _page->getSelectionOrContentsAsString([view, webView](const String& string) {
        RetainPtr nsString = string.createNSString();
        [webView _accessibilityDidGetSpeakSelectionContent:nsString.get()];
        if ([view respondsToSelector:@selector(accessibilitySpeakSelectionSetContent:)])
            [view accessibilitySpeakSelectionSetContent:nsString.get()];
    });
}

- (void)_accessibilityRetrieveRectsEnclosingSelectionOffset:(NSInteger)offset withGranularity:(UITextGranularity)granularity
{
    _page->requestRectsForGranularityWithSelectionOffset(toWKTextGranularity(granularity), offset, [view = retainPtr(self), offset, granularity](const Vector<WebCore::SelectionGeometry>& selectionGeometries) {
        if ([view respondsToSelector:@selector(_accessibilityDidGetSelectionRects:withGranularity:atOffset:)])
            [view _accessibilityDidGetSelectionRects:[view webSelectionRectsForSelectionGeometries:selectionGeometries] withGranularity:granularity atOffset:offset];
    });
}

- (void)_accessibilityRetrieveRectsAtSelectionOffset:(NSInteger)offset withText:(NSString *)text
{
    [self _accessibilityRetrieveRectsAtSelectionOffset:offset withText:text completionHandler:nil];
}

- (void)_accessibilityRetrieveRectsAtSelectionOffset:(NSInteger)offset withText:(NSString *)text completionHandler:(void (^)(const Vector<WebCore::SelectionGeometry>& geometries))completionHandler
{
    RetainPtr<WKContentView> view = self;
    _page->requestRectsAtSelectionOffsetWithText(offset, text, [view, offset, capturedCompletionHandler = makeBlockPtr(completionHandler)](const Vector<WebCore::SelectionGeometry>& selectionGeometries) {
        if (capturedCompletionHandler)
            capturedCompletionHandler(selectionGeometries);

        if ([view respondsToSelector:@selector(_accessibilityDidGetSelectionRects:withGranularity:atOffset:)])
            [view _accessibilityDidGetSelectionRects:[view webSelectionRectsForSelectionGeometries:selectionGeometries] withGranularity:UITextGranularityWord atOffset:offset];
    });
}

- (void)_accessibilityStoreSelection
{
    _page->storeSelectionForAccessibility(true);
}

- (void)_accessibilityClearSelection
{
    _page->storeSelectionForAccessibility(false);
}

static UIPasteboardName pasteboardNameForAccessCategory(WebCore::DOMPasteAccessCategory pasteAccessCategory)
{
    switch (pasteAccessCategory) {
    case WebCore::DOMPasteAccessCategory::General:
    case WebCore::DOMPasteAccessCategory::Fonts:
        return UIPasteboardNameGeneral;
    }
}

static UIPasteboard *pasteboardForAccessCategory(WebCore::DOMPasteAccessCategory pasteAccessCategory)
{
    switch (pasteAccessCategory) {
    case WebCore::DOMPasteAccessCategory::General:
    case WebCore::DOMPasteAccessCategory::Fonts:
        return UIPasteboard.generalPasteboard;
    }
}

- (BOOL)_handleDOMPasteRequestWithResult:(WebCore::DOMPasteAccessResponse)response
{
    if (auto pasteAccessCategory = std::exchange(_domPasteRequestCategory, std::nullopt)) {
        if (response == WebCore::DOMPasteAccessResponse::GrantedForCommand || response == WebCore::DOMPasteAccessResponse::GrantedForGesture) {
            if (auto replyID = _page->grantAccessToCurrentPasteboardData(pasteboardNameForAccessCategory(*pasteAccessCategory), [] () { }))
                _page->websiteDataStore().protectedNetworkProcess()->connection().waitForAsyncReplyAndDispatchImmediately<Messages::NetworkProcess::AllowFilesAccessFromWebProcess>(*replyID, 100_ms);
        }
    }

    if (auto pasteHandler = WTFMove(_domPasteRequestHandler)) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        [UIMenuController.sharedMenuController hideMenuFromView:self];
ALLOW_DEPRECATED_DECLARATIONS_END
        pasteHandler(response);
        return YES;
    }
    return NO;
}

- (void)_willPerformAction:(SEL)action sender:(id)sender
{
    if (action != @selector(paste:))
        [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
}

- (void)_didPerformAction:(SEL)action sender:(id)sender
{
    if (action == @selector(paste:))
        [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];
}

// UIWKInteractionViewProtocol

static inline WebKit::GestureType toGestureType(WKBEGestureType gestureType)
{
    switch (gestureType) {
    case WKBEGestureTypeLoupe:
        return WebKit::GestureType::Loupe;
    case WKBEGestureTypeOneFingerTap:
        return WebKit::GestureType::OneFingerTap;
    case WKBEGestureTypeDoubleTapAndHold:
        return WebKit::GestureType::TapAndAHalf;
    case WKBEGestureTypeDoubleTap:
        return WebKit::GestureType::DoubleTap;
    case WKBEGestureTypeOneFingerDoubleTap:
        return WebKit::GestureType::OneFingerDoubleTap;
    case WKBEGestureTypeOneFingerTripleTap:
        return WebKit::GestureType::OneFingerTripleTap;
    case WKBEGestureTypeTwoFingerSingleTap:
        return WebKit::GestureType::TwoFingerSingleTap;
    case WKBEGestureTypeIMPhraseBoundaryDrag:
        return WebKit::GestureType::PhraseBoundary;
    default:
        ASSERT_NOT_REACHED();
        return WebKit::GestureType::Loupe;
    }
}

static inline WKBEGestureType toWKBEGestureType(WebKit::GestureType gestureType)
{
    switch (gestureType) {
    case WebKit::GestureType::Loupe:
        return WKBEGestureTypeLoupe;
    case WebKit::GestureType::OneFingerTap:
        return WKBEGestureTypeOneFingerTap;
    case WebKit::GestureType::TapAndAHalf:
        return WKBEGestureTypeDoubleTapAndHold;
    case WebKit::GestureType::DoubleTap:
        return WKBEGestureTypeDoubleTap;
    case WebKit::GestureType::OneFingerDoubleTap:
        return WKBEGestureTypeOneFingerDoubleTap;
    case WebKit::GestureType::OneFingerTripleTap:
        return WKBEGestureTypeOneFingerTripleTap;
    case WebKit::GestureType::TwoFingerSingleTap:
        return WKBEGestureTypeTwoFingerSingleTap;
    case WebKit::GestureType::PhraseBoundary:
        return WKBEGestureTypeIMPhraseBoundaryDrag;
    }
}

static TextStream& operator<<(TextStream& stream, WebKit::GestureType gestureType)
{
    switch (gestureType) {
    case WebKit::GestureType::Loupe: stream << "Loupe"; break;
    case WebKit::GestureType::OneFingerTap: stream << "OneFingerTap"; break;
    case WebKit::GestureType::TapAndAHalf: stream << "TapAndAHalf"; break;
    case WebKit::GestureType::DoubleTap: stream << "DoubleTap"; break;
    case WebKit::GestureType::OneFingerDoubleTap: stream << "OneFingerDoubleTap"; break;
    case WebKit::GestureType::OneFingerTripleTap: stream << "OneFingerTripleTap"; break;
    case WebKit::GestureType::TwoFingerSingleTap: stream << "TwoFingerSingleTap"; break;
    case WebKit::GestureType::PhraseBoundary: stream << "PhraseBoundary"; break;
    }

    return stream;
}

static inline WebKit::SelectionTouch toSelectionTouch(WKBESelectionTouchPhase touch)
{
    switch (touch) {
    case WKBESelectionTouchPhaseStarted:
        return WebKit::SelectionTouch::Started;
    case WKBESelectionTouchPhaseMoved:
        return WebKit::SelectionTouch::Moved;
    case WKBESelectionTouchPhaseEnded:
        return WebKit::SelectionTouch::Ended;
    case WKBESelectionTouchPhaseEndedMovingForward:
        return WebKit::SelectionTouch::EndedMovingForward;
    case WKBESelectionTouchPhaseEndedMovingBackward:
        return WebKit::SelectionTouch::EndedMovingBackward;
    case WKBESelectionTouchPhaseEndedNotMoving:
        return WebKit::SelectionTouch::EndedNotMoving;
    }
    ASSERT_NOT_REACHED();
    return WebKit::SelectionTouch::Ended;
}

static inline WKBESelectionTouchPhase toWKBESelectionTouchPhase(WebKit::SelectionTouch touch)
{
    switch (touch) {
    case WebKit::SelectionTouch::Started:
        return WKBESelectionTouchPhaseStarted;
    case WebKit::SelectionTouch::Moved:
        return WKBESelectionTouchPhaseMoved;
    case WebKit::SelectionTouch::Ended:
        return WKBESelectionTouchPhaseEnded;
    case WebKit::SelectionTouch::EndedMovingForward:
        return WKBESelectionTouchPhaseEndedMovingForward;
    case WebKit::SelectionTouch::EndedMovingBackward:
        return WKBESelectionTouchPhaseEndedMovingBackward;
    case WebKit::SelectionTouch::EndedNotMoving:
        return WKBESelectionTouchPhaseEndedNotMoving;
    }
}

static TextStream& operator<<(TextStream& stream, WebKit::SelectionTouch touch)
{
    switch (touch) {
    case WebKit::SelectionTouch::Started: stream << "Started"; break;
    case WebKit::SelectionTouch::Moved: stream << "Moved"; break;
    case WebKit::SelectionTouch::Ended: stream << "Ended"; break;
    case WebKit::SelectionTouch::EndedMovingForward: stream << "EndedMovingForward"; break;
    case WebKit::SelectionTouch::EndedMovingBackward: stream << "EndedMovingBackward"; break;
    case WebKit::SelectionTouch::EndedNotMoving: stream << "EndedNotMoving"; break;
    }

    return stream;
}

static inline WebKit::GestureRecognizerState toGestureRecognizerState(UIGestureRecognizerState state)
{
    switch (state) {
    case UIGestureRecognizerStatePossible:
        return WebKit::GestureRecognizerState::Possible;
    case UIGestureRecognizerStateBegan:
        return WebKit::GestureRecognizerState::Began;
    case UIGestureRecognizerStateChanged:
        return WebKit::GestureRecognizerState::Changed;
    case UIGestureRecognizerStateCancelled:
        return WebKit::GestureRecognizerState::Cancelled;
    case UIGestureRecognizerStateEnded:
        return WebKit::GestureRecognizerState::Ended;
    case UIGestureRecognizerStateFailed:
        return WebKit::GestureRecognizerState::Failed;
    }
}

static inline UIGestureRecognizerState toUIGestureRecognizerState(WebKit::GestureRecognizerState state)
{
    switch (state) {
    case WebKit::GestureRecognizerState::Possible:
        return UIGestureRecognizerStatePossible;
    case WebKit::GestureRecognizerState::Began:
        return UIGestureRecognizerStateBegan;
    case WebKit::GestureRecognizerState::Changed:
        return UIGestureRecognizerStateChanged;
    case WebKit::GestureRecognizerState::Cancelled:
        return UIGestureRecognizerStateCancelled;
    case WebKit::GestureRecognizerState::Ended:
        return UIGestureRecognizerStateEnded;
    case WebKit::GestureRecognizerState::Failed:
        return UIGestureRecognizerStateFailed;
    }
}

static TextStream& operator<<(TextStream& stream, WebKit::GestureRecognizerState state)
{
    switch (state) {
    case WebKit::GestureRecognizerState::Possible: stream << "Possible"; break;
    case WebKit::GestureRecognizerState::Began: stream << "Began"; break;
    case WebKit::GestureRecognizerState::Changed: stream << "Changed"; break;
    case WebKit::GestureRecognizerState::Cancelled: stream << "Cancelled"; break;
    case WebKit::GestureRecognizerState::Ended: stream << "Ended"; break;
    case WebKit::GestureRecognizerState::Failed: stream << "Failed"; break;
    }

    return stream;
}

static inline WKBESelectionFlags toWKBESelectionFlags(OptionSet<WebKit::SelectionFlags> flags)
{
#if USE(BROWSERENGINEKIT)
    NSUInteger uiFlags = WKBESelectionFlagsNone;
#else
    NSInteger uiFlags = UIWKNone;
#endif
    if (flags.contains(WebKit::SelectionFlags::WordIsNearTap))
        uiFlags |= WKBEWordIsNearTap;
    if (flags.contains(WebKit::SelectionFlags::SelectionFlipped))
        uiFlags |= WKBESelectionFlipped;
    if (flags.contains(WebKit::SelectionFlags::PhraseBoundaryChanged))
        uiFlags |= WKBEPhraseBoundaryChanged;

    return static_cast<WKBESelectionFlags>(uiFlags);
}

static inline OptionSet<WebKit::SelectionFlags> toSelectionFlags(WKBESelectionFlags uiFlags)
{
    OptionSet<WebKit::SelectionFlags> flags;
    if (uiFlags & WKBEWordIsNearTap)
        flags.add(WebKit::SelectionFlags::WordIsNearTap);
    if (uiFlags & WKBESelectionFlipped)
        flags.add(WebKit::SelectionFlags::SelectionFlipped);
    if (uiFlags & WKBEPhraseBoundaryChanged)
        flags.add(WebKit::SelectionFlags::PhraseBoundaryChanged);
    return flags;
}

static TextStream& operator<<(TextStream& stream, OptionSet<WebKit::SelectionFlags> flags)
{
    bool didAppend = false;

    auto appendIf = [&](auto flag, auto message) {
        if (!flags.contains(flag))
            return;
        if (didAppend)
            stream << "|";
        stream << message;
        didAppend = true;
    };

    appendIf(WebKit::SelectionFlags::WordIsNearTap, "WordIsNearTap");
    appendIf(WebKit::SelectionFlags::SelectionFlipped, "SelectionFlipped");
    appendIf(WebKit::SelectionFlags::PhraseBoundaryChanged, "PhraseBoundaryChanged");

    if (!didAppend)
        stream << "None";

    return stream;
}

static inline WebCore::TextGranularity toWKTextGranularity(UITextGranularity granularity)
{
    switch (granularity) {
    case UITextGranularityCharacter:
        return WebCore::TextGranularity::CharacterGranularity;
    case UITextGranularityWord:
        return WebCore::TextGranularity::WordGranularity;
    case UITextGranularitySentence:
        return WebCore::TextGranularity::SentenceGranularity;
    case UITextGranularityParagraph:
        return WebCore::TextGranularity::ParagraphGranularity;
    case UITextGranularityLine:
        return WebCore::TextGranularity::LineGranularity;
    case UITextGranularityDocument:
        return WebCore::TextGranularity::DocumentGranularity;
    }
}

static inline WebCore::SelectionDirection toWKSelectionDirection(UITextDirection direction)
{
    switch (direction) {
    case UITextLayoutDirectionDown:
    case UITextLayoutDirectionRight:
        return WebCore::SelectionDirection::Right;
    case UITextLayoutDirectionUp:
    case UITextLayoutDirectionLeft:
        return WebCore::SelectionDirection::Left;
    default:
        // UITextDirection is not an enum, but we only want to accept values from UITextLayoutDirection.
        ASSERT_NOT_REACHED();
        return WebCore::SelectionDirection::Right;
    }
}

static void selectionChangedWithGesture(WKTextInteractionWrapper *interaction, const WebCore::IntPoint& point, WebKit::GestureType gestureType, WebKit::GestureRecognizerState gestureState, OptionSet<WebKit::SelectionFlags> flags)
{
    [interaction selectionChangedWithGestureAt:(CGPoint)point withGesture:toWKBEGestureType(gestureType) withState:toUIGestureRecognizerState(gestureState) withFlags:toWKBESelectionFlags(flags)];
}

static void selectionChangedWithTouch(WKTextInteractionWrapper *interaction, const WebCore::IntPoint& point, WebKit::SelectionTouch touch, OptionSet<WebKit::SelectionFlags> flags)
{
    [interaction selectionChangedWithTouchAt:(CGPoint)point withSelectionTouch:toWKBESelectionTouchPhase(touch) withFlags:toWKBESelectionFlags(flags)];
}

- (BOOL)_hasFocusedElement
{
    return _focusedElementInformation.elementType != WebKit::InputType::None;
}

- (void)changeSelectionWithGestureAt:(CGPoint)point withGesture:(WKBEGestureType)gestureType withState:(UIGestureRecognizerState)state
{
    [self changeSelectionWithGestureAt:point withGesture:gestureType withState:state withFlags:UIWKNone];
}

- (void)changeSelectionWithGestureAt:(CGPoint)point withGesture:(WKBEGestureType)gestureType withState:(UIGestureRecognizerState)state withFlags:(WKBESelectionFlags)flags
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point, state, gestureType, std::nullopt, flags);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
    _page->selectWithGesture(WebCore::IntPoint(point), toGestureType(gestureType), toGestureRecognizerState(state), self._hasFocusedElement, [self, strongSelf = retainPtr(self), state, flags](const WebCore::IntPoint& point, WebKit::GestureType gestureType, WebKit::GestureRecognizerState gestureState, OptionSet<WebKit::SelectionFlags> innerFlags) {
        selectionChangedWithGesture(_textInteractionWrapper.get(), point, gestureType, gestureState, toSelectionFlags(flags) | innerFlags);
        if (state == UIGestureRecognizerStateEnded || state == UIGestureRecognizerStateCancelled)
            _usingGestureForSelection = NO;
    });
}

- (void)updateSelection
{
    switch (_selectionInteractionType) {
    case SelectionInteractionType::None:
        break;
    case SelectionInteractionType::Touch:
        [self updateSelectionWithTouchAt:[self convertPoint:_lastSelectionTouch.point fromView:self.webView] withSelectionTouch:WKBESelectionTouchPhaseMoved baseIsStart:_lastSelectionTouch.baseIsStart withFlags:_lastSelectionTouch.flags];
        break;
    case SelectionInteractionType::ExtentPoint:
        [self updateSelectionWithExtentPoint:[self convertPoint:_lastSelectionExtentPoint.point fromView:self.webView] hasFocusedElement:self._hasFocusedElement respectSelectionAnchor:_lastSelectionExtentPoint.respectSelectionAnchor completionHandler:^(BOOL selectionEndIsMoving) { }];
        break;
    case SelectionInteractionType::ExtentPointAndBoundary:
        [self updateSelectionWithExtentPointAndBoundary:[self convertPoint:_lastSelectionExtentPointAndBoundary.point fromView:self.webView] textGranularity:_lastSelectionExtentPointAndBoundary.granularity textInteractionSource:_lastSelectionExtentPointAndBoundary.interactionSource completionHandler:^(BOOL selectionEndIsMoving) { }];
        break;
    }
}

- (void)changeSelectionWithTouchAt:(CGPoint)point withSelectionTouch:(WKBESelectionTouchPhase)touch baseIsStart:(BOOL)baseIsStart withFlags:(WKBESelectionFlags)flags
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point, std::nullopt, std::nullopt, touch, flags);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;

    if (_page->isAutoscrolling()) {
        auto touchType = toSelectionTouch(touch);
        switch (touchType) {
        case WebKit::SelectionTouch::Started:
        case WebKit::SelectionTouch::Moved:
            _selectionInteractionType = SelectionInteractionType::Touch;
            _lastSelectionTouch.point = [self convertPoint:point toView:self.webView];
            _lastSelectionTouch.baseIsStart = baseIsStart;
            _lastSelectionTouch.flags = flags;
            break;
        case WebKit::SelectionTouch::Ended:
        case WebKit::SelectionTouch::EndedMovingForward:
        case WebKit::SelectionTouch::EndedMovingBackward:
        case WebKit::SelectionTouch::EndedNotMoving:
            _selectionInteractionType = SelectionInteractionType::None;
            break;
        }
    }

    [self updateSelectionWithTouchAt:point withSelectionTouch:touch baseIsStart:baseIsStart withFlags:flags];

}

- (void)updateSelectionWithTouchAt:(CGPoint)point withSelectionTouch:(WKBESelectionTouchPhase)touch baseIsStart:(BOOL)baseIsStart withFlags:(WKBESelectionFlags)flags
{
    _page->updateSelectionWithTouches(WebCore::IntPoint(point), toSelectionTouch(touch), baseIsStart, [self, strongSelf = retainPtr(self), flags](const WebCore::IntPoint& point, WebKit::SelectionTouch touch, OptionSet<WebKit::SelectionFlags> innerFlags) {
        if (_selectionInteractionType == SelectionInteractionType::Touch && innerFlags.contains(WebKit::SelectionFlags::SelectionFlipped))
            _lastSelectionTouch.baseIsStart = !_lastSelectionTouch.baseIsStart;
        selectionChangedWithTouch(_textInteractionWrapper.get(), point, touch, toSelectionFlags(flags) | innerFlags);
        if (toWKBESelectionTouchPhase(touch) != WKBESelectionTouchPhaseStarted && toWKBESelectionTouchPhase(touch) != WKBESelectionTouchPhaseMoved)
            _usingGestureForSelection = NO;
    });
}

- (void)changeSelectionWithTouchesFrom:(CGPoint)from to:(CGPoint)to withGesture:(WKBEGestureType)gestureType withState:(UIGestureRecognizerState)gestureState
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, from, gestureState, gestureType);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
    _page->selectWithTwoTouches(WebCore::IntPoint(from), WebCore::IntPoint(to), toGestureType(gestureType), toGestureRecognizerState(gestureState), [self, strongSelf = retainPtr(self)](const WebCore::IntPoint& point, WebKit::GestureType gestureType, WebKit::GestureRecognizerState gestureState, OptionSet<WebKit::SelectionFlags> flags) {
        selectionChangedWithGesture(_textInteractionWrapper.get(), point, gestureType, gestureState, flags);
        if (toUIGestureRecognizerState(gestureState) == UIGestureRecognizerStateEnded || toUIGestureRecognizerState(gestureState) == UIGestureRecognizerStateCancelled)
            _usingGestureForSelection = NO;
    });
}

- (void)moveByOffset:(NSInteger)offset
{
    if (!offset)
        return;
    
    [self _internalBeginSelectionChange];
    RetainPtr<WKContentView> view = self;
    _page->moveSelectionByOffset(offset, [view] {
        [view _internalEndSelectionChange];
    });
}

- (const WebKit::WKAutoCorrectionData&)autocorrectionData
{
    return _autocorrectionData;
}

// The completion handler can pass nil if input does not match the actual text preceding the insertion point.
- (void)requestAutocorrectionRectsForString:(NSString *)input withCompletionHandler:(void (^)(UIWKAutocorrectionRects *rectsForInput))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    if (!completionHandler) {
        [NSException raise:NSInvalidArgumentException format:@"Expected a nonnull completion handler in %s.", __PRETTY_FUNCTION__];
        return;
    }

    [self _internalRequestTextRectsForString:input completion:[view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)](auto& rects) {
        completionHandler(!rects.isEmpty() ? [WKAutocorrectionRects autocorrectionRectsWithFirstCGRect:rects.first() lastCGRect:rects.last()] : nil);
    }];
}

- (void)_internalRequestTextRectsForString:(NSString *)input completion:(Function<void(const Vector<WebCore::FloatRect>&)>&&)completion
{
    if (!input.length)
        return completion({ });

    _page->requestAutocorrectionData(input, [view = retainPtr(self), completion = WTFMove(completion)](const WebKit::WebAutocorrectionData& data) mutable {
        CGRect firstRect;
        CGRect lastRect;
        auto& rects = data.textRects;
        if (rects.isEmpty()) {
            firstRect = CGRectZero;
            lastRect = CGRectZero;
        } else {
            firstRect = rects.first();
            lastRect = rects.last();
        }

        view->_autocorrectionData.font = data.font;
        view->_autocorrectionData.textFirstRect = firstRect;
        view->_autocorrectionData.textLastRect = lastRect;

        completion(rects);
    });
}

#if HAVE(UI_EDIT_MENU_INTERACTION)

- (void)requestPreferredArrowDirectionForEditMenuWithCompletionHandler:(void(^)(UIEditMenuArrowDirection))completion
{
    if (self.shouldSuppressEditMenu) {
        completion(UIEditMenuArrowDirectionAutomatic);
        return;
    }

    auto requestRectsToEvadeIfNeeded = [startTime = ApproximateTime::now(), weakSelf = WeakObjCPtr<WKContentView>(self), completion = makeBlockPtr(completion)]() mutable {
        auto strongSelf = weakSelf.get();
        if (!strongSelf) {
            completion(UIEditMenuArrowDirectionAutomatic);
            return;
        }

        if ([strongSelf webView]._editable) {
            completion(UIEditMenuArrowDirectionAutomatic);
            return;
        }

        auto focusedElementType = strongSelf->_focusedElementInformation.elementType;
        if (focusedElementType != WebKit::InputType::ContentEditable && focusedElementType != WebKit::InputType::TextArea) {
            completion(UIEditMenuArrowDirectionAutomatic);
            return;
        }

        // Give the page some time to present custom editing UI before attempting to detect and evade it.
        auto delayBeforeShowingEditMenu = std::max(0_s, 0.25_s - (ApproximateTime::now() - startTime));
        WorkQueue::mainSingleton().dispatchAfter(delayBeforeShowingEditMenu, [completion = WTFMove(completion), weakSelf]() mutable {
            auto strongSelf = weakSelf.get();
            if (!strongSelf) {
                completion(UIEditMenuArrowDirectionAutomatic);
                return;
            }

            if (!strongSelf->_page) {
                completion(UIEditMenuArrowDirectionAutomatic);
                return;
            }

            strongSelf->_page->requestEvasionRectsAboveSelection([completion = WTFMove(completion)](auto& rects) mutable {
                completion(rects.isEmpty() ? UIEditMenuArrowDirectionAutomatic : UIEditMenuArrowDirectionUp);
            });
        });
    };

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    [self doAfterComputingImageAnalysisResultsForBackgroundRemoval:WTFMove(requestRectsToEvadeIfNeeded)];
#else
    requestRectsToEvadeIfNeeded();
#endif
}

#endif // HAVE(UI_EDIT_MENU_INTERACTION)

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (UIMenu *)removeBackgroundMenu
{
    if (!_page || !_page->preferences().removeBackgroundEnabled())
        return nil;

    if (!_page->editorState().hasPostLayoutData() || !_removeBackgroundData)
        return nil;

    if (_removeBackgroundData->element != _page->editorState().postLayoutData->selectedEditableImage)
        return nil;

    return [self menuWithInlineAction:WebCore::contextMenuItemTitleRemoveBackground().createNSString().get() image:[UIImage _systemImageNamed:@"circle.rectangle.filled.pattern.diagonalline"] identifier:@"WKActionRemoveBackground" handler:[](WKContentView *view) {
        auto data = std::exchange(view->_removeBackgroundData, { });
        if (!data)
            return;

        auto [elementContext, image, preferredMIMEType] = *data;
        if (auto [data, type] = WebKit::imageDataForRemoveBackground(image.get(), preferredMIMEType.createCFString().get()); data)
            view->_page->replaceImageForRemoveBackground(elementContext, { String { type.get() } }, span(data.get()));
    }];
}

- (void)doAfterComputingImageAnalysisResultsForBackgroundRemoval:(CompletionHandler<void()>&&)completion
{
    if (!_page->editorState().hasPostLayoutData()) {
        completion();
        return;
    }

    auto elementToAnalyze = _page->editorState().postLayoutData->selectedEditableImage;
    if (_removeBackgroundData && _removeBackgroundData->element == elementToAnalyze) {
        completion();
        return;
    }

    _removeBackgroundData = std::nullopt;

    if (!elementToAnalyze) {
        completion();
        return;
    }

    _page->shouldAllowRemoveBackground(*elementToAnalyze, [context = *elementToAnalyze, completion = WTFMove(completion), weakSelf = WeakObjCPtr<WKContentView>(self)](bool shouldAllow) mutable {
        auto strongSelf = weakSelf.get();
        if (!shouldAllow || !strongSelf) {
            completion();
            return;
        }

        strongSelf->_page->requestImageBitmap(context, [context, completion = WTFMove(completion), weakSelf = WTFMove(weakSelf)](std::optional<WebCore::ShareableBitmapHandle>&& imageData, auto& sourceMIMEType) mutable {
            auto strongSelf = weakSelf.get();
            if (!strongSelf) {
                completion();
                return;
            }

            if (!imageData) {
                completion();
                return;
            }

            auto imageBitmap = WebCore::ShareableBitmap::create(WTFMove(*imageData));
            if (!imageBitmap) {
                completion();
                return;
            }

            auto cgImage = imageBitmap->makeCGImage();
            if (!cgImage) {
                completion();
                return;
            }

            WebKit::requestBackgroundRemoval(cgImage.get(), [sourceMIMEType, context, completion = WTFMove(completion), weakSelf](CGImageRef result) mutable {
                auto strongSelf = weakSelf.get();
                if (!strongSelf) {
                    completion();
                    return;
                }

                if (result)
                    strongSelf->_removeBackgroundData = { { context, { result }, sourceMIMEType } };
                else
                    strongSelf->_removeBackgroundData = std::nullopt;
                completion();
            });
        });
    });
}

#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (BOOL)_handleTapOverInteractiveControl:(CGPoint)position
{
    auto *hitButton = dynamic_objc_cast<UIControl>([self hitTest:position withEvent:nil]);
    if (!hitButton)
        return NO;

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    UIButton *analysisButton = [_imageAnalysisInteraction analysisButton];
    // This has to be a class check instead of a straight equality check because the `analysisButton`
    // isn't in the view hierarchy, so this is the only way to disambiguate this particular button.
    if (analysisButton && hitButton.class == analysisButton.class)
        [_imageAnalysisInteraction setHighlightSelectableItems:![_imageAnalysisInteraction highlightSelectableItems]];
#endif

    [hitButton sendActionsForControlEvents:UIControlEventTouchUpInside];

    return YES;
}

static void logTextInteraction(const char* methodName, UIGestureRecognizer *loupeGestureRecognizer, std::optional<CGPoint> location = std::nullopt, std::optional<UIGestureRecognizerState> gestureState = std::nullopt, std::optional<WKBEGestureType> gestureType = std::nullopt, std::optional<WKBESelectionTouchPhase> selectionTouch = std::nullopt, std::optional<WKBESelectionFlags> selectionFlags = std::nullopt)
{
    TextStream selectionChangeStream(TextStream::LineMode::SingleLine);
    selectionChangeStream << "loupeGestureState=" << toGestureRecognizerState(loupeGestureRecognizer.state);
    if (location)
        selectionChangeStream << ", location={" << location->x << ", " << location->y << "}";

    if (gestureState)
        selectionChangeStream << ", " << "gestureState=" << toGestureRecognizerState(*gestureState);

    if (gestureType)
        selectionChangeStream << ", " << "gestureType=" << toGestureType(*gestureType);

    if (selectionTouch)
        selectionChangeStream << ", " << "selectionTouch=" << toSelectionTouch(*selectionTouch);

    if (selectionFlags)
        selectionChangeStream << ", " << "selectionFlags=" << toSelectionFlags(*selectionFlags);

    RELEASE_LOG(TextInteraction, "Text interaction changing selection using '%s' (%s).", methodName, selectionChangeStream.release().utf8().data());
}

- (void)selectPositionAtPoint:(CGPoint)point completionHandler:(void (^)(void))completionHandler
{
    _autocorrectionContextNeedsUpdate = YES;
    [self _selectPositionAtPoint:point stayingWithinFocusedElement:self._hasFocusedElement completionHandler:completionHandler];
}

- (void)_selectPositionAtPoint:(CGPoint)point stayingWithinFocusedElement:(BOOL)stayingWithinFocusedElement completionHandler:(void (^)(void))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;

    _page->selectPositionAtPoint(WebCore::IntPoint(point), stayingWithinFocusedElement, [view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)]() {
        completionHandler();
        view->_usingGestureForSelection = NO;
    });
}

- (void)selectPositionAtBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction fromPoint:(CGPoint)point completionHandler:(void (^)(void))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
    _page->selectPositionAtBoundaryWithDirection(WebCore::IntPoint(point), toWKTextGranularity(granularity), toWKSelectionDirection(direction), self._hasFocusedElement, [view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)]() {
        completionHandler();
        view->_usingGestureForSelection = NO;
    });
}

- (void)moveSelectionAtBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction completionHandler:(void (^)(void))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
    _page->moveSelectionAtBoundaryWithDirection(toWKTextGranularity(granularity), toWKSelectionDirection(direction), [view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)] {
        completionHandler();
        view->_usingGestureForSelection = NO;
    });
}

- (void)selectTextWithGranularity:(UITextGranularity)granularity atPoint:(CGPoint)point completionHandler:(void (^)(void))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point);

    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    _usingMouseDragForSelection = [_mouseInteraction mouseTouchGestureRecognizer]._wk_hasRecognizedOrEnded;
#endif
    ++_suppressNonEditableSingleTapTextInteractionCount;
    _page->selectTextWithGranularityAtPoint(WebCore::IntPoint(point), toWKTextGranularity(granularity), self._hasFocusedElement, [view = retainPtr(self), selectionHandler = makeBlockPtr(completionHandler)] {
        selectionHandler();
        view->_usingGestureForSelection = NO;
        --view->_suppressNonEditableSingleTapTextInteractionCount;
    });
}

- (void)beginSelectionInDirection:(UITextDirection)direction completionHandler:(void (^)(BOOL endIsMoving))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer);

    _autocorrectionContextNeedsUpdate = YES;
    _page->beginSelectionInDirection(toWKSelectionDirection(direction), [selectionHandler = makeBlockPtr(completionHandler)] (bool endIsMoving) {
        selectionHandler(endIsMoving);
    });
}

- (void)updateSelectionWithExtentPoint:(CGPoint)point completionHandler:(void (^)(BOOL endIsMoving))completionHandler
{
    [self updateSelectionWithExtentPoint:point withBoundary:UITextGranularityCharacter completionHandler:completionHandler];
}

- (void)updateSelectionWithExtentPoint:(CGPoint)point withBoundary:(UITextGranularity)granularity completionHandler:(void (^)(BOOL selectionEndIsMoving))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point);

    _autocorrectionContextNeedsUpdate = YES;

    if (granularity == UITextGranularityCharacter && !_usingMouseDragForSelection) {
        auto triggeredByFloatingCursor = !self.textInteractionLoupeGestureRecognizer._wk_hasRecognizedOrEnded
#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
            && ![_mouseInteraction mouseTouchGestureRecognizer]._wk_hasRecognizedOrEnded
#endif
            && !self.textInteractionTapGestureRecognizer._wk_hasRecognizedOrEnded;

        auto respectSelectionAnchor = triggeredByFloatingCursor ? WebKit::RespectSelectionAnchor::Yes : WebKit::RespectSelectionAnchor::No;
        [self updateSelectionWithExtentPoint:point hasFocusedElement:self._hasFocusedElement respectSelectionAnchor:respectSelectionAnchor completionHandler:completionHandler];

        if (!triggeredByFloatingCursor && _page->isAutoscrolling()) {
            _selectionInteractionType = SelectionInteractionType::ExtentPoint;
            _lastSelectionExtentPoint.point = [self convertPoint:point toView:self.webView];
            _lastSelectionExtentPoint.respectSelectionAnchor = respectSelectionAnchor;
        }
        return;
    }

    auto source = _usingMouseDragForSelection ? WebKit::TextInteractionSource::Mouse : WebKit::TextInteractionSource::Touch;
    [self updateSelectionWithExtentPointAndBoundary:point textGranularity:toWKTextGranularity(granularity) textInteractionSource:source completionHandler:completionHandler];

    if (_page->isAutoscrolling()) {
        _selectionInteractionType = SelectionInteractionType::ExtentPointAndBoundary;
        _lastSelectionExtentPointAndBoundary.point = [self convertPoint:point toView:self.webView];
        _lastSelectionExtentPointAndBoundary.granularity = toWKTextGranularity(granularity);
        _lastSelectionExtentPointAndBoundary.interactionSource = source;
    }
}

- (void)updateSelectionWithExtentPoint:(CGPoint)point hasFocusedElement:(BOOL)hasFocusedElement respectSelectionAnchor:(WebKit::RespectSelectionAnchor)respectSelectionAnchor completionHandler:(void (^)(BOOL selectionEndIsMoving))completionHandler
{
    _page->updateSelectionWithExtentPoint(WebCore::IntPoint(point), self._hasFocusedElement, respectSelectionAnchor, [completionHandler = makeBlockPtr(completionHandler)](bool endIsMoving) {
        completionHandler(static_cast<BOOL>(endIsMoving));
    });
}

- (void)updateSelectionWithExtentPointAndBoundary:(CGPoint)point textGranularity:(WebCore::TextGranularity)textGranularity textInteractionSource:(WebKit::TextInteractionSource)interactionSource completionHandler:(void (^)(BOOL selectionEndIsMoving))completionHandler
{
    ++_suppressNonEditableSingleTapTextInteractionCount;
    _page->updateSelectionWithExtentPointAndBoundary(WebCore::IntPoint(point), textGranularity, self._hasFocusedElement, interactionSource, [completionHandler = makeBlockPtr(completionHandler), protectedSelf = retainPtr(self)] (bool endIsMoving) {
        completionHandler(static_cast<BOOL>(endIsMoving));
        --protectedSelf->_suppressNonEditableSingleTapTextInteractionCount;
    });
}

- (UTF32Char)_characterInRelationToCaretSelection:(int)amount
{
    if (self.shouldUseAsyncInteractions)
        return [super _characterInRelationToCaretSelection:amount];

    if (amount == -1 && _lastInsertedCharacterToOverrideCharacterBeforeSelection)
        return *_lastInsertedCharacterToOverrideCharacterBeforeSelection;

    auto& state = _page->editorState();
    if (!state.isContentEditable || state.selectionIsNone || state.selectionIsRange || !state.postLayoutData)
        return 0;

    switch (amount) {
    case 0:
        return state.postLayoutData->characterAfterSelection;
    case -1:
        return state.postLayoutData->characterBeforeSelection;
    case -2:
        return state.postLayoutData->twoCharacterBeforeSelection;
    default:
        return 0;
    }
}

- (BOOL)_selectionAtDocumentStart
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return self.selectionAtDocumentStart;
}

- (CGRect)textFirstRect
{
    auto& editorState = _page->editorState();
    if (editorState.hasComposition) {
        if (!editorState.hasVisualData())
            return CGRectZero;
        auto& markedTextRects = editorState.visualData->markedTextRects;
        return markedTextRects.isEmpty() ? CGRectZero : markedTextRects.first().rect();
    }
    return _autocorrectionData.textFirstRect;
}

- (CGRect)textLastRect
{
    auto& editorState = _page->editorState();
    if (editorState.hasComposition) {
        if (!editorState.hasVisualData())
            return CGRectZero;
        auto& markedTextRects = editorState.visualData->markedTextRects;
        return markedTextRects.isEmpty() ? CGRectZero : markedTextRects.last().rect();
    }
    return _autocorrectionData.textLastRect;
}

- (void)willInsertFinalDictationResult
{
    _page->willInsertFinalDictationResult();
}

- (void)didInsertFinalDictationResult
{
    _page->didInsertFinalDictationResult();
}

- (void)replaceDictatedText:(NSString*)oldText withText:(NSString *)newText
{
    if (_isHidingKeyboard && _isChangingFocus)
        return;

    _autocorrectionContextNeedsUpdate = YES;
    _page->replaceDictatedText(oldText, newText);
}

// The completion handler should pass the rect of the correction text after replacing the input text, or nil if the replacement could not be performed.
- (void)applyAutocorrection:(NSString *)correction toString:(NSString *)input isCandidate:(BOOL)isCandidate withCompletionHandler:(void (^)(UIWKAutocorrectionRects *rectsForCorrection))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalReplaceText:input withText:correction isCandidate:isCandidate completion:[completionHandler = makeBlockPtr(completionHandler), view = retainPtr(self)](bool wasApplied) {
        completionHandler(wasApplied ? [WKAutocorrectionRects autocorrectionRectsWithFirstCGRect:view->_autocorrectionData.textFirstRect lastCGRect:view->_autocorrectionData.textLastRect] : nil);
    }];
}

- (void)_internalReplaceText:(NSString *)input withText:(NSString *)correction isCandidate:(BOOL)isCandidate completion:(Function<void(bool)>&&)completionHandler
{
    if ([self _disableAutomaticKeyboardUI]) {
        if (completionHandler)
            completionHandler(false);
        return;
    }

    // FIXME: Remove the synchronous call when <rdar://problem/16207002> is fixed.
    const bool useSyncRequest = true;

    if (useSyncRequest) {
        if (completionHandler)
            completionHandler(_page->applyAutocorrection(correction, input, isCandidate));
        return;
    }

    _page->applyAutocorrection(correction, input, isCandidate, [view = retainPtr(self), completionHandler = WTFMove(completionHandler)](auto& string) {
        if (completionHandler)
            completionHandler(!string.isNull());
    });
}

- (void)applyAutocorrection:(NSString *)correction toString:(NSString *)input withCompletionHandler:(void (^)(UIWKAutocorrectionRects *rectsForCorrection))completionHandler
{
    [self applyAutocorrection:correction toString:input isCandidate:NO withCompletionHandler:completionHandler];
}

- (void)applyAutocorrection:(NSString *)correction toString:(NSString *)input shouldUnderline:(BOOL)shouldUnderline withCompletionHandler:(void (^)(UIWKAutocorrectionRects *rectsForCorrection))completionHandler
{
    [self applyAutocorrection:correction toString:input isCandidate:shouldUnderline withCompletionHandler:completionHandler];
}

- (void)_cancelPendingAutocorrectionContextHandler
{
    if (auto handler = WTFMove(_pendingAutocorrectionContextHandler))
        handler(WebKit::RequestAutocorrectionContextResult::Empty);
}

- (void)requestAutocorrectionContextWithCompletionHandler:(void (^)(UIWKAutocorrectionContext *autocorrectionContext))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    if (!completionHandler) {
        [NSException raise:NSInvalidArgumentException format:@"Expected a nonnull completion handler in %s.", __PRETTY_FUNCTION__];
        return;
    }

    [self _internalRequestAutocorrectionContextWithCompletionHandler:[completionHandler = makeBlockPtr(completionHandler), strongSelf = retainPtr(self)](WebKit::RequestAutocorrectionContextResult result) {
        switch (result) {
        case WebKit::RequestAutocorrectionContextResult::Empty:
            completionHandler(WKAutocorrectionContext.emptyAutocorrectionContext);
            break;
        case WebKit::RequestAutocorrectionContextResult::LastContext:
            completionHandler([WKAutocorrectionContext autocorrectionContextWithWebContext:strongSelf->_lastAutocorrectionContext]);
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    }];
}

- (void)_internalRequestAutocorrectionContextWithCompletionHandler:(CompletionHandler<void(WebKit::RequestAutocorrectionContextResult)>&&)completionHandler
{
    // FIXME: We can remove this code and unconditionally ignore autocorrection context requests when
    // out-of-process keyboard is enabled, once rdar://111936621 is addressed; we'll also be able to
    // remove `EditorState::PostLayoutData::hasGrammarDocumentMarkers` then.
    bool requiresContextToShowGrammarCheckingResults = [&] {
        auto& state = _page->editorState();
        return state.hasPostLayoutData() && state.postLayoutData->hasGrammarDocumentMarkers;
    }();

    if (UIKeyboard.usesInputSystemUI && !requiresContextToShowGrammarCheckingResults)
        return completionHandler(WebKit::RequestAutocorrectionContextResult::Empty);

    if (self._disableAutomaticKeyboardUI)
        return completionHandler(WebKit::RequestAutocorrectionContextResult::Empty);

    if (!_page->hasRunningProcess())
        return completionHandler(WebKit::RequestAutocorrectionContextResult::Empty);

    bool respondWithLastKnownAutocorrectionContext = ([&] {
        if (_page->shouldAvoidSynchronouslyWaitingToPreventDeadlock())
            return true;

        if (_domPasteRequestHandler)
            return true;

        if (_isUnsuppressingSoftwareKeyboardUsingLastAutocorrectionContext)
            return true;

        return !_autocorrectionContextNeedsUpdate;
    })();

    if (respondWithLastKnownAutocorrectionContext)
        return completionHandler(WebKit::RequestAutocorrectionContextResult::LastContext);

    if (_pendingAutocorrectionContextHandler)
        return completionHandler(WebKit::RequestAutocorrectionContextResult::Empty);

    _pendingAutocorrectionContextHandler = WTFMove(completionHandler);
    _page->requestAutocorrectionContext();

    if (_page->legacyMainFrameProcess().connection().waitForAndDispatchImmediately<Messages::WebPageProxy::HandleAutocorrectionContext>(_page->webPageIDInMainFrameProcess(), 1_s, IPC::WaitForOption::DispatchIncomingSyncMessagesWhileWaiting) != IPC::Error::NoError)
        RELEASE_LOG(TextInput, "Timed out while waiting for autocorrection context.");

    if (_autocorrectionContextNeedsUpdate)
        [self _cancelPendingAutocorrectionContextHandler];
    else if (auto handler = WTFMove(_pendingAutocorrectionContextHandler))
        handler(WebKit::RequestAutocorrectionContextResult::LastContext);
}

- (void)_handleAutocorrectionContext:(const WebKit::WebAutocorrectionContext&)context
{
    _lastAutocorrectionContext = context;
    _autocorrectionContextNeedsUpdate = NO;
    [self unsuppressSoftwareKeyboardUsingLastAutocorrectionContextIfNeeded];
}

- (void)updateSoftwareKeyboardSuppressionStateFromWebView
{
    BOOL webViewIsSuppressingSoftwareKeyboard = [_webView _suppressSoftwareKeyboard];
    if (webViewIsSuppressingSoftwareKeyboard) {
        _unsuppressSoftwareKeyboardAfterNextAutocorrectionContextUpdate = NO;
        self._suppressSoftwareKeyboard = webViewIsSuppressingSoftwareKeyboard;
        return;
    }

    if (self._suppressSoftwareKeyboard == webViewIsSuppressingSoftwareKeyboard)
        return;

    if (!std::exchange(_unsuppressSoftwareKeyboardAfterNextAutocorrectionContextUpdate, YES))
        _page->requestAutocorrectionContext();
}

- (void)unsuppressSoftwareKeyboardUsingLastAutocorrectionContextIfNeeded
{
    if (!std::exchange(_unsuppressSoftwareKeyboardAfterNextAutocorrectionContextUpdate, NO))
        return;

    SetForScope unsuppressSoftwareKeyboardScope { _isUnsuppressingSoftwareKeyboardUsingLastAutocorrectionContext, YES };
    self._suppressSoftwareKeyboard = NO;
}

- (void)runModalJavaScriptDialog:(CompletionHandler<void()>&&)callback
{
    if (_isFocusingElementWithKeyboard)
        _pendingRunModalJavaScriptDialogCallback = WTFMove(callback);
    else
        callback();
}

- (void)_didStartProvisionalLoadForMainFrame
{
    // Reset the double tap gesture recognizer to prevent any double click that is in the process of being recognized.
    [_doubleTapGestureRecognizerForDoubleClick _wk_cancel];
    // We also need to disable the double-tap gesture recognizers that are enabled for double-tap-to-zoom and which
    // are enabled when a single tap is first recognized. This avoids tests running in sequence and simulating taps
    // in the same location to trigger double-tap recognition.
    [self _setDoubleTapGesturesEnabled:NO];
    [_twoFingerDoubleTapGestureRecognizer _wk_cancel];
#if ENABLE(IMAGE_ANALYSIS)
    [self _cancelImageAnalysis];
#endif
}

- (void)_didCommitLoadForMainFrame
{
    [_keyboardScrollingAnimator stopScrollingImmediately];

    _seenHardwareKeyDownInNonEditableElement = NO;

    [self _elementDidBlur];
    [self _cancelLongPressGestureRecognizer];
    [self _removeContainerForContextMenuHintPreviews];
    [self _removeContainerForDragPreviews];
    [self _removeContainerForDropPreviews];
    [_webView _didCommitLoadForMainFrame];

    _textInteractionDidChangeFocusedElement = NO;
    _activeTextInteractionCount = 0;
    _treatAsContentEditableUntilNextEditorStateUpdate = NO;
    [self _invalidateCurrentPositionInformation];

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    [self uninstallImageAnalysisInteraction];
#endif

    [_textInteractionWrapper reset];
}

- (void)_nextAccessoryTabForWebView:(id)sender
{
    [self accessoryTab:YES];
}

- (void)_previousAccessoryTabForWebView:(id)sender
{
    [self accessoryTab:NO];
}

- (void)_becomeFirstResponderWithSelectionMovingForward:(BOOL)selectingForward completionHandler:(void (^)(BOOL didBecomeFirstResponder))completionHandler
{
    constexpr bool isKeyboardEventValid = false;
    _page->setInitialFocus(selectingForward, isKeyboardEventValid, { }, [protectedSelf = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)] {
        if (completionHandler)
            completionHandler([protectedSelf becomeFirstResponder]);
    });
}

- (WebCore::Color)_tapHighlightColorForFastClick:(BOOL)forFastClick
{
    ASSERT(_showDebugTapHighlightsForFastClicking);
    return forFastClick ? WebCore::SRGBA<uint8_t> { 0, 225, 0, 127 } : WebCore::SRGBA<uint8_t> { 225, 0, 0, 127 };
}

- (void)_setDoubleTapGesturesEnabled:(BOOL)enabled
{
    if (enabled && ![_doubleTapGestureRecognizer isEnabled]) {
        // The first tap recognized after re-enabling double tap gestures will not wait for the
        // second tap before committing. To fix this, we use a new double tap gesture recognizer.
        [self _createAndConfigureDoubleTapGestureRecognizer];
    }

    if (_showDebugTapHighlightsForFastClicking && !enabled)
        _tapHighlightInformation.color = [self _tapHighlightColorForFastClick:YES];

    _doubleTapGesturesAreDisabledTemporarilyForFastTap = !enabled;
    [self _updateDoubleTapGestureRecognizerEnablement];
}

- (void)_updateDoubleTapGestureRecognizerEnablement
{
    [_doubleTapGestureRecognizer setEnabled:!_doubleTapGesturesAreDisabledTemporarilyForFastTap && [_webView _allowsMagnification]];
    [_nonBlockingDoubleTapGestureRecognizer setEnabled:_doubleTapGesturesAreDisabledTemporarilyForFastTap && [_webView _allowsMagnification]];
    [self _resetIsDoubleTapPending];
}

// MARK: WKFormAccessoryViewDelegate protocol and accessory methods

- (void)accessoryDone
{
    [self accessoryViewDone:_formAccessoryView.get()];
}

- (void)accessoryViewDone:(WKFormAccessoryView *)view
{
    if ([_webView _resetFocusPreservationCount])
        RELEASE_LOG_ERROR(ViewState, "Keyboard dismissed with nonzero focus preservation count; check for unbalanced calls to -_incrementFocusPreservationCount");

    [self stopRelinquishingFirstResponderToFocusedElement];
    [self endEditingAndUpdateFocusAppearanceWithReason:EndEditingReasonAccessoryDone];
    _page->setIsShowingInputViewForFocusedElement(false);
}

- (void)updateFocusedElementValue:(NSString *)value
{
    _page->setFocusedElementValue(_focusedElementInformation.elementContext, value);
    _focusedElementInformation.value = value;
}

- (void)updateFocusedElementValueAsColor:(UIColor *)value
{
    auto color = [&] {
        if (_page->preferences().inputTypeColorEnhancementsEnabled())
            return WebCore::Color::createAndPreserveColorSpace(value.CGColor);
        return WebCore::Color(WebCore::roundAndClampToSRGBALossy(value.CGColor));
    }();
    auto valueAsString = WebCore::serializationForHTML(color);

    _page->setFocusedElementValue(_focusedElementInformation.elementContext, valueAsString);
    _focusedElementInformation.value = valueAsString;
    _focusedElementInformation.colorValue = color;
}

- (void)updateFocusedElementSelectedIndex:(uint32_t)index allowsMultipleSelection:(bool)allowsMultipleSelection
{
    _page->setFocusedElementSelectedIndex(_focusedElementInformation.elementContext, index, allowsMultipleSelection);
}

- (void)updateFocusedElementFocusedWithDataListDropdown:(BOOL)value
{
    _focusedElementInformation.isFocusingWithDataListDropdown = value;
    [self reloadInputViews];
}

- (void)accessoryTab:(BOOL)isNext
{
    auto direction = isNext ? WebKit::TabDirection::Next : WebKit::TabDirection::Previous;
    [self accessoryView:_formAccessoryView.get() tabInDirection:direction];
}

- (void)accessoryView:(WKFormAccessoryView *)view tabInDirection:(WebKit::TabDirection)direction
{
    // The input peripheral may need to update the focused DOM node before we switch focus. The UI process does
    // not maintain a handle to the actual focused DOM node – only the web process has such a handle. So, we need
    // to end the editing session now before we tell the web process to switch focus. Once the web process tells
    // us the newly focused element we are no longer are in a position to effect the previously focused element.
    // See <https://bugs.webkit.org/show_bug.cgi?id=134409>.
    [self _endEditing];
    _inputPeripheral = nil; // Nullify so that we don't tell the input peripheral to end editing again in -_elementDidBlur.

    _isChangingFocusUsingAccessoryTab = YES;
    [self _internalBeginSelectionChange];
    _page->focusNextFocusedElement(direction == WebKit::TabDirection::Next, [protectedSelf = retainPtr(self)] {
        [protectedSelf _internalEndSelectionChange];
        [protectedSelf reloadInputViews];
        protectedSelf->_isChangingFocusUsingAccessoryTab = NO;
    });
}

- (void)accessoryViewAutoFill:(WKFormAccessoryView *)view
{
    id <_WKInputDelegate> inputDelegate = [_webView _inputDelegate];
    if ([inputDelegate respondsToSelector:@selector(_webView:accessoryViewCustomButtonTappedInFormInputSession:)])
        [inputDelegate _webView:self.webView accessoryViewCustomButtonTappedInFormInputSession:_formInputSession.get()];
}

- (WKFormAccessoryView *)formAccessoryView
{
#if PLATFORM(APPLETV)
    return nil;
#else
#if HAVE(UI_USER_INTERFACE_IDIOM_VISION)
    if (self.traitCollection.userInterfaceIdiom == UIUserInterfaceIdiomVision)
        return nil;
#endif

    if (!_formAccessoryView)
        _formAccessoryView = adoptNS([[WKFormAccessoryView alloc] initWithInputAssistantItem:self.inputAssistantItem delegate:self]);

    return _formAccessoryView.get();
#endif
}

- (void)accessoryOpen
{
    if (!_inputPeripheral)
        return;
    [self _zoomToRevealFocusedElement];
    [self _updateAccessory];
    [_inputPeripheral beginEditing];
}

- (void)_updateAccessory
{
    if (_isUpdatingAccessoryView)
        return;

    SetForScope updateAccessoryScope { _isUpdatingAccessoryView, YES };

    auto* accessoryView = self.formAccessoryView; // Creates one, if needed.

    if ([accessoryView respondsToSelector:@selector(setNextPreviousItemsVisible:)])
        [accessoryView setNextPreviousItemsVisible:!self.webView._editable];

    [accessoryView setNextEnabled:_focusedElementInformation.hasNextNode];
    [accessoryView setPreviousEnabled:_focusedElementInformation.hasPreviousNode];
}

// MARK: Keyboard interaction
// UITextInput protocol implementation

- (BOOL)_allowAnimatedUpdateSelectionRectViews
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return NO;
}

- (void)beginSelectionChange
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalBeginSelectionChange];
}

- (void)_internalBeginSelectionChange
{
    [self _updateInternalStateBeforeSelectionChange];

#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        [_asyncInputDelegate selectionWillChangeForTextInput:self.asBETextInput];
        return;
    }
#endif

    [self.inputDelegate selectionWillChange:self];
}

- (void)_updateInternalStateBeforeSelectionChange
{
    _autocorrectionContextNeedsUpdate = YES;
    _selectionChangeNestingLevel++;
}

- (void)endSelectionChange
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalEndSelectionChange];
}

- (void)_internalEndSelectionChange
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions)
        [_asyncInputDelegate selectionDidChangeForTextInput:self.asBETextInput];
    else
#endif // USE(BROWSERENGINEKIT)
        [self.inputDelegate selectionDidChange:self];

    [self _updateInternalStateAfterSelectionChange];
}

- (void)_updateInternalStateAfterSelectionChange
{
    if (_selectionChangeNestingLevel) {
        // FIXME (228083): Some layout tests end up triggering unbalanced calls to -endSelectionChange.
        // We should investigate why this happens, (ideally) prevent it from happening, and then assert
        // that `_selectionChangeNestingLevel` is nonzero when calling -endSelectionChange.
        _selectionChangeNestingLevel--;
    }
}

- (void)willFinishIgnoringCalloutBarFadeAfterPerformingAction
{
    _ignoreSelectionCommandFadeCount++;
    _page->scheduleFullEditorStateUpdate();
    _page->callAfterNextPresentationUpdate([weakSelf = WeakObjCPtr<WKContentView>(self)] {
        if (auto strongSelf = weakSelf.get())
            strongSelf->_ignoreSelectionCommandFadeCount--;
    });
}

- (void)_didChangeWebViewEditability
{
    BOOL webViewIsEditable = self.webView._editable;
    if ([_formAccessoryView respondsToSelector:@selector(setNextPreviousItemsVisible:)])
        [_formAccessoryView setNextPreviousItemsVisible:!webViewIsEditable];
    
    [_twoFingerSingleTapGestureRecognizer setEnabled:!webViewIsEditable];

    if (webViewIsEditable) {
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            class_addProtocol(self.class, @protocol(UITextInputMultiDocument));
        });
    }
}

- (void)insertTextSuggestion:(id)textSuggestion
{
    _autocorrectionContextNeedsUpdate = YES;

    if (RetainPtr autoFillSuggestion = dynamic_objc_cast<UITextAutofillSuggestion>(textSuggestion)) {
        // Maintain binary compatibility with UITextAutofillSuggestion, even when using the ServiceExtensions text input.
        _page->autofillLoginCredentials([autoFillSuggestion username], [autoFillSuggestion password]);
        return;
    }

#if USE(BROWSERENGINEKIT)
    if (RetainPtr autoFillSuggestion = dynamic_objc_cast<BEAutoFillTextSuggestion>(textSuggestion)) {
        RetainPtr contents = [autoFillSuggestion contents];
        _page->autofillLoginCredentials([contents objectForKey:UITextContentTypeUsername], [contents objectForKey:UITextContentTypePassword]);
        return;
    }
#endif

    if ([_dataListTextSuggestions count] && ![[_formInputSession suggestions] count]) {
        RetainPtr inputText = [textSuggestion inputText];
        for (WKBETextSuggestion *dataListTextSuggestion in _dataListTextSuggestions.get()) {
            if ([inputText isEqualToString:dataListTextSuggestion.inputText]) {
                _page->setFocusedElementValue(_focusedElementInformation.elementContext, inputText.get());
                return;
            }
        }
    }

    id <_WKInputDelegate> inputDelegate = [_webView _inputDelegate];
    if ([inputDelegate respondsToSelector:@selector(_webView:insertTextSuggestion:inInputSession:)]) {
        auto uiTextSuggestion = [&]() -> RetainPtr<UITextSuggestion> {
#if USE(BROWSERENGINEKIT)
            if (auto beTextSuggestion = dynamic_objc_cast<BETextSuggestion>(textSuggestion))
                return beTextSuggestion._uikitTextSuggestion;
#endif
            return textSuggestion;
        }();
        [inputDelegate _webView:self.webView insertTextSuggestion:uiTextSuggestion.get() inInputSession:_formInputSession.get()];
    }
}

- (NSString *)textInRange:(UITextRange *)range
{
    if (!_page)
        return nil;

    auto& editorState = _page->editorState();
    if (!editorState.hasPostLayoutData())
        return nil;

    if (self.selectedTextRange == range && editorState.selectionIsRange)
        return editorState.postLayoutData->wordAtSelection.createNSString().autorelease();

    if (auto relativeRange = dynamic_objc_cast<WKRelativeTextRange>(range))
        return textRelativeToSelectionStart(relativeRange, *editorState.postLayoutData, _lastInsertedCharacterToOverrideCharacterBeforeSelection).autorelease();

    return nil;
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text
{
}

- (NSArray<WKTextSelectionRect *> *)_textSelectionRects:(const Vector<WebCore::SelectionGeometry>&)rects
{
    return createNSArray(rects, [&](auto& rect) {
        return adoptNS([[WKTextSelectionRect alloc] initWithSelectionGeometry:rect delegate:self]);
    }).autorelease();
}

- (CGFloat)scaleFactorForSelectionRect:(WKTextSelectionRect *)rect
{
    return self._contentZoomScale;
}

- (WebCore::FloatQuad)selectionRect:(WKTextSelectionRect *)rect convertQuadToSelectionContainer:(const WebCore::FloatQuad&)quad
{
    return [self _wk_convertQuad:quad toCoordinateSpace:[self _selectionContainerViewInternal]];
}

- (UITextRange *)selectedTextRange
{
    auto& editorState = _page->editorState();
    auto hasSelection = !editorState.selectionIsNone;
    if (!hasSelection || !editorState.hasPostLayoutAndVisualData())
        return nil;

    auto isRange = editorState.selectionIsRange;
    auto isContentEditable = editorState.isContentEditable;
    // UIKit does not expect caret selections in non-editable content.
    if (!isContentEditable && !isRange)
        return nil;

    if (_cachedSelectedTextRange)
        return _cachedSelectedTextRange.get();

    auto caretStartRect = _page->editorState().visualData->caretRectAtStart;
    auto caretEndRect = _page->editorState().visualData->caretRectAtEnd;
    auto selectionRects = [self _textSelectionRects:_page->editorState().visualData->selectionGeometries];
    auto selectedTextLength = editorState.postLayoutData->selectedTextLength;

    _cachedSelectedTextRange = [WKTextRange textRangeWithState:!hasSelection isRange:isRange isEditable:isContentEditable startRect:caretStartRect endRect:caretEndRect selectionRects:selectionRects selectedTextLength:selectedTextLength];
    return _cachedSelectedTextRange.get();
}

- (CGRect)caretRectForPosition:(UITextPosition *)position
{
    return dynamic_objc_cast<WKTextPosition>(position).positionRect;
}

- (NSArray *)selectionRectsForRange:(UITextRange *)range
{
    return dynamic_objc_cast<WKTextRange>(range).selectionRects;
}

- (void)setSelectedTextRange:(UITextRange *)range
{
    if (range)
        return;
#if !PLATFORM(MACCATALYST)
    if (!self._hasFocusedElement)
        return;
#endif
    [self _internalClearSelection];
}

- (BOOL)hasMarkedText
{
    return [_markedText length];
}

- (NSString *)markedText
{
    return _markedText.get();
}

- (UITextRange *)markedTextRange
{
    auto& editorState = _page->editorState();
    bool hasComposition = editorState.hasComposition;
    if (!hasComposition || !editorState.hasPostLayoutAndVisualData())
        return nil;
    auto& postLayoutData = *editorState.postLayoutData;
    auto& visualData = *editorState.visualData;
    auto caretStartRect = visualData.markedTextCaretRectAtStart;
    auto caretEndRect = visualData.markedTextCaretRectAtEnd;
    auto isRange = caretStartRect != caretEndRect;
    auto isContentEditable = editorState.isContentEditable;
    auto selectionRects = [self _textSelectionRects:visualData.markedTextRects];
    auto selectedTextLength = postLayoutData.markedText.length();
    return [WKTextRange textRangeWithState:!hasComposition isRange:isRange isEditable:isContentEditable startRect:caretStartRect endRect:caretEndRect selectionRects:selectionRects selectedTextLength:selectedTextLength];
}

- (NSDictionary *)markedTextStyle
{
    return nil;
}

- (void)setMarkedTextStyle:(NSDictionary *)styleDictionary
{
}

#if HAVE(REDESIGNED_TEXT_CURSOR)
static BOOL shouldUseHighlightsForMarkedText(NSAttributedString *string)
{
    __block BOOL result = NO;

    [string enumerateAttributesInRange:NSMakeRange(0, string.length) options:0 usingBlock:^(NSDictionary<NSAttributedStringKey, id> *attributes, NSRange, BOOL *stop) {
        BOOL hasUnderlineStyle = !![attributes objectForKey:NSUnderlineStyleAttributeName];
        BOOL hasUnderlineColor = !![attributes objectForKey:NSUnderlineColorAttributeName];

        BOOL hasBackgroundColor = !![attributes objectForKey:NSBackgroundColorAttributeName];
        BOOL hasForegroundColor = !![attributes objectForKey:NSForegroundColorAttributeName];

        // Marked text may be represented either as an underline or a highlight; this mode is dictated
        // by the attributes it has, and therefore having both types of attributes is not allowed.
        ASSERT(!((hasUnderlineStyle || hasUnderlineColor) && (hasBackgroundColor || hasForegroundColor)));

        if (hasUnderlineStyle || hasUnderlineColor) {
            result = NO;
            *stop = YES;
        } else if (hasBackgroundColor || hasForegroundColor) {
            result = YES;
            *stop = YES;
        }
    }];

    return result;
}

static Vector<WebCore::CompositionUnderline> extractUnderlines(NSAttributedString *string)
{
    if (!string.length)
        return { };

    Vector<WebCore::CompositionUnderline> underlines;
    [string enumerateAttributesInRange:NSMakeRange(0, string.length) options:0 usingBlock:[&underlines](NSDictionary<NSAttributedStringKey, id> *attributes, NSRange range, BOOL *) {
        underlines.append({
            static_cast<unsigned>(range.location),
            static_cast<unsigned>(NSMaxRange(range)),
            WebCore::CompositionUnderlineColor::GivenColor,
            WebCore::Color::black,
            [attributes[NSUnderlineStyleAttributeName] isEqual:@(NSUnderlineStyleThick)] || attributes[NSBackgroundColorAttributeName]
        });
    }];

    std::ranges::sort(underlines, [](auto& a, auto& b) {
        if (a.startOffset < b.startOffset)
            return true;
        if (a.startOffset > b.startOffset)
            return false;
        return a.endOffset < b.endOffset;
    });

    Vector<WebCore::CompositionUnderline> mergedUnderlines;
    if (!underlines.isEmpty())
        mergedUnderlines.append({ underlines.first().startOffset, underlines.last().endOffset, WebCore::CompositionUnderlineColor::GivenColor, WebCore::Color::black, false });

    for (auto& underline : underlines) {
        if (underline.thick)
            mergedUnderlines.append(underline);
    }

    return mergedUnderlines;
}
#endif

static Vector<WebCore::CompositionHighlight> compositionHighlights(NSAttributedString *string)
{
    if (!string.length)
        return { };

    Vector<WebCore::CompositionHighlight> highlights;
    [string enumerateAttributesInRange:NSMakeRange(0, string.length) options:0 usingBlock:[&highlights](NSDictionary<NSAttributedStringKey, id> *attributes, NSRange range, BOOL *) {
        WebCore::Color backgroundHighlightColor { WebCore::CompositionHighlight::defaultCompositionFillColor };
        if (UIColor *backgroundColor = attributes[NSBackgroundColorAttributeName])
            backgroundHighlightColor = WebCore::colorFromCocoaColor(backgroundColor);

        std::optional<WebCore::Color> foregroundHighlightColor;
        if (UIColor *foregroundColor = attributes[NSForegroundColorAttributeName])
            foregroundHighlightColor = WebCore::colorFromCocoaColor(foregroundColor);

        highlights.append({ static_cast<unsigned>(range.location), static_cast<unsigned>(NSMaxRange(range)), backgroundHighlightColor, foregroundHighlightColor });
    }];

    std::ranges::sort(highlights, [](auto& a, auto& b) {
        if (a.startOffset < b.startOffset)
            return true;
        if (a.startOffset > b.startOffset)
            return false;
        return a.endOffset < b.endOffset;
    });

    Vector<WebCore::CompositionHighlight> mergedHighlights;
    mergedHighlights.reserveInitialCapacity(highlights.size());
    for (auto& highlight : highlights) {
        if (mergedHighlights.isEmpty() || mergedHighlights.last().backgroundColor != highlight.backgroundColor || mergedHighlights.last().foregroundColor != highlight.foregroundColor)
            mergedHighlights.append(highlight);
        else
            mergedHighlights.last().endOffset = highlight.endOffset;
    }

    return mergedHighlights;
}

- (void)setAttributedMarkedText:(NSAttributedString *)markedText selectedRange:(NSRange)selectedRange
{
    BOOL hasTextCompletion = ^{
        if (!markedText.length)
            return NO;

        // UIKit doesn't include the `NSTextCompletionAttributeName`, so the next best way to detect if this method
        // is being used for a text completion is to check if the attributes match these hard-coded ones.
        RetainPtr textCompletionAttributes = @{
            NSForegroundColorAttributeName : UIColor.systemGrayColor,
            NSBackgroundColorAttributeName : UIColor.clearColor,
        };

        RetainPtr markedTextAttributes = [markedText attributesAtIndex:0 effectiveRange:nil];

        RetainPtr foregroundColor = [markedTextAttributes objectForKey:NSForegroundColorAttributeName];
        if (![foregroundColor isEqual:UIColor.systemGrayColor])
            return NO;

        RetainPtr backgroundColor = [markedTextAttributes objectForKey:NSBackgroundColorAttributeName];
        if (![backgroundColor isEqual:UIColor.clearColor])
            return NO;

        return YES;
    }();

    if (hasTextCompletion) {
        _page->setWritingSuggestion([markedText string], { 0, 0 });
        return;
    }

    Vector<WebCore::CompositionUnderline> underlines;
    Vector<WebCore::CompositionHighlight> highlights;

#if HAVE(REDESIGNED_TEXT_CURSOR)
    if (!shouldUseHighlightsForMarkedText(markedText))
        underlines = extractUnderlines(markedText);
    else
#endif
        highlights = compositionHighlights(markedText);

    [self _setMarkedText:markedText.string underlines:underlines highlights:highlights selectedRange:selectedRange];
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange
{
    Vector<WebCore::CompositionUnderline> underlines;

#if HAVE(REDESIGNED_TEXT_CURSOR)
    underlines.append(WebCore::CompositionUnderline(0, [_markedText length], WebCore::CompositionUnderlineColor::GivenColor, WebCore::Color::black, false));
#endif

    [self _setMarkedText:markedText underlines:underlines highlights:Vector<WebCore::CompositionHighlight> { } selectedRange:selectedRange];
}

- (void)_setMarkedText:(NSString *)markedText underlines:(const Vector<WebCore::CompositionUnderline>&)underlines highlights:(const Vector<WebCore::CompositionHighlight>&)highlights selectedRange:(NSRange)selectedRange
{
    _autocorrectionContextNeedsUpdate = YES;
    _candidateViewNeedsUpdate = !self.hasMarkedText && _isDeferringKeyEventsToInputMethod;
    _markedText = markedText;
    _page->setCompositionAsync(markedText, underlines, highlights, { }, selectedRange, { });
}

- (void)unmarkText
{
    _isDeferringKeyEventsToInputMethod = NO;
    _markedText = nil;
    _page->confirmCompositionAsync();
}

- (UITextPosition *)beginningOfDocument
{
    return nil;
}

- (UITextPosition *)endOfDocument
{
    return nil;
}

- (BOOL)_isAnchoredToCurrentSelection:(UITextPosition *)position
{
    return [position isKindOfClass:WKRelativeTextPosition.class] || [position isEqual:self.selectedTextRange.start] || [position isEqual:self.selectedTextRange.end];
}

- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition toPosition:(UITextPosition *)toPosition
{
    if (!self.shouldUseAsyncInteractions)
        return nil;

    if (![self _isAnchoredToCurrentSelection:fromPosition] || ![self _isAnchoredToCurrentSelection:toPosition])
        return nil;

    return adoptNS([[WKRelativeTextRange alloc] initWithStart:fromPosition end:toPosition]).autorelease();
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset
{
    if (!self.shouldUseAsyncInteractions)
        return nil;

    if (![self _isAnchoredToCurrentSelection:position])
        return nil;

    return positionWithOffsetFrom(position, offset);
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset
{
    return nil;
}

- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other
{
    return NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)to
{
#if HAVE(UIFINDINTERACTION)
    if ([from isKindOfClass:[WKFoundDOMTextPosition class]] && [to isKindOfClass:[WKFoundDOMTextPosition class]]) {
        WKFoundDOMTextPosition *fromPosition = (WKFoundDOMTextPosition *)from;
        WKFoundDOMTextPosition *toPosition = (WKFoundDOMTextPosition *)to;

        if (fromPosition.order == toPosition.order)
            return fromPosition.offset - toPosition.offset;
    }

    if ([from isKindOfClass:[WKFoundPDFTextPosition class]] && [to isKindOfClass:[WKFoundPDFTextPosition class]]) {
        WKFoundPDFTextPosition *fromPosition = (WKFoundPDFTextPosition *)from;
        WKFoundPDFTextPosition *toPosition = (WKFoundPDFTextPosition *)to;

        if (fromPosition.order == toPosition.order) {
            if (fromPosition.page == toPosition.page)
                return fromPosition.offset - toPosition.offset;
            return fromPosition.page - toPosition.page;
        }
    }

    if ([from isKindOfClass:[WKFoundTextPosition class]] && [to isKindOfClass:[WKFoundTextPosition class]]) {
        WKFoundTextPosition *fromPosition = (WKFoundTextPosition *)from;
        WKFoundTextPosition *toPosition = (WKFoundTextPosition *)to;

        return fromPosition.order - toPosition.order;
    }
#endif

    return 0;
}

- (id <UITextInputTokenizer>)tokenizer
{
    return nil;
}

- (UITextPosition *)positionWithinRange:(UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction
{
    return nil;
}

- (UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction
{
    return nil;
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction
{
    return NSWritingDirectionLeftToRight;
}

static WebKit::WritingDirection coreWritingDirection(NSWritingDirection direction)
{
    switch (direction) {
    case NSWritingDirectionNatural:
        return WebCore::WritingDirection::Natural;
    case NSWritingDirectionLeftToRight:
        return WebCore::WritingDirection::LeftToRight;
    case NSWritingDirectionRightToLeft:
        return WebCore::WritingDirection::RightToLeft;
    default:
        ASSERT_NOT_REACHED();
        return WebCore::WritingDirection::Natural;
    }
}

- (void)setBaseWritingDirection:(NSWritingDirection)direction forRange:(UITextRange *)range
{
    if (!_page->isEditable())
        return;

    if (range && ![range isEqual:self.selectedTextRange]) {
        // We currently only support changing the base writing direction at the selection.
        return;
    }
    _page->setBaseWritingDirection(coreWritingDirection(direction));
}

- (CGRect)firstRectForRange:(UITextRange *)range
{
    return CGRectZero;
}

/* Hit testing. */
- (UITextPosition *)closestPositionToPoint:(CGPoint)point
{
#if PLATFORM(MACCATALYST)
    WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(point));
    [self requestAsynchronousPositionInformationUpdate:request];
    if ([self _currentPositionInformationIsApproximatelyValidForRequest:request radiusForApproximation:2] && _positionInformation.isSelectable())
        return [WKTextPosition textPositionWithRect:_positionInformation.caretRect];
#endif
    return nil;
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range
{
    return nil;
}

- (UITextRange *)characterRangeAtPoint:(CGPoint)point
{
    return nil;
}

- (void)deleteBackward
{
    _page->executeEditCommand("deleteBackward"_s);
}

- (BOOL)_shouldSimulateKeyboardInputOnTextInsertion
{
#if HAVE(PENCILKIT_TEXT_INPUT)
    return [_scribbleInteraction isHandlingWriting];
#else
    return NO;
#endif
}

// Inserts the given string, replacing any selected or marked text.
- (void)insertText:(NSString *)aStringValue
{
    if (_isHidingKeyboard && _isChangingFocus)
        return;

    WebKit::InsertTextOptions options;
    options.processingUserGesture = _isHandlingActiveKeyEvent || _isHandlingActivePressesEvent;
    options.shouldSimulateKeyboardInput = [self _shouldSimulateKeyboardInputOnTextInsertion];
    options.directionFromCurrentInputMode = [self] -> std::optional<WebCore::TextDirection> {
        NSString *inputLanguage = self.textInputMode.primaryLanguage;
        if (!inputLanguage.length)
            return { };

        switch ([NSLocale characterDirectionForLanguage:inputLanguage]) {
        case NSLocaleLanguageDirectionRightToLeft:
            return WebCore::TextDirection::RTL;
        case NSLocaleLanguageDirectionLeftToRight:
            return WebCore::TextDirection::LTR;
        case NSLocaleLanguageDirectionTopToBottom:
        case NSLocaleLanguageDirectionBottomToTop:
        case NSLocaleLanguageDirectionUnknown:
            return { };
        }
        ASSERT_NOT_REACHED();
        return { };
    }();
    _page->insertTextAsync(aStringValue, { }, WTFMove(options));

    if (_focusedElementInformation.autocapitalizeType == WebCore::AutocapitalizeType::Words && aStringValue.length) {
        _lastInsertedCharacterToOverrideCharacterBeforeSelection = [aStringValue characterAtIndex:aStringValue.length - 1];
        _page->scheduleFullEditorStateUpdate();
    }

    _autocorrectionContextNeedsUpdate = YES;
}

- (void)insertText:(NSString *)aStringValue alternatives:(NSArray<NSString *> *)alternatives style:(UITextAlternativeStyle)style
{
    if (!alternatives.count) {
        [self insertText:aStringValue];
        return;
    }

    BOOL isLowConfidence = style == UITextAlternativeStyleLowConfidence;
    auto nsAlternatives = adoptNS([[NSTextAlternatives alloc] initWithPrimaryString:aStringValue alternativeStrings:alternatives isLowConfidence:isLowConfidence]);
#if USE(BROWSERENGINEKIT)
    auto textAlternatives = adoptNS([[PlatformTextAlternatives alloc] _initWithNSTextAlternatives:nsAlternatives.get()]);
#else
    auto textAlternatives = nsAlternatives;
#endif
    WebCore::TextAlternativeWithRange textAlternativeWithRange { textAlternatives.get(), NSMakeRange(0, aStringValue.length) };

    WebKit::InsertTextOptions options;
    options.shouldSimulateKeyboardInput = [self _shouldSimulateKeyboardInputOnTextInsertion];
    _page->insertDictatedTextAsync(aStringValue, { }, { textAlternativeWithRange }, WTFMove(options));

    _autocorrectionContextNeedsUpdate = YES;
}

- (BOOL)hasText
{
    if (_isFocusingElementWithKeyboard || _page->waitingForPostLayoutEditorStateUpdateAfterFocusingElement())
        return _focusedElementInformation.hasPlainText;

    auto& editorState = _page->editorState();
    return editorState.hasPostLayoutData() && editorState.postLayoutData->hasPlainText;
}

// Accepts either NSTextAlternatives, or an equivalent type that's currently defined as PlatformTextAlternatives.
- (void)addTextAlternatives:(NSObject *)alternatives
{
    RetainPtr platformAlternatives = dynamic_objc_cast<PlatformTextAlternatives>(alternatives);

#if USE(BROWSERENGINEKIT)
    if (!platformAlternatives) {
        if (RetainPtr nsAlternatives = dynamic_objc_cast<NSTextAlternatives>(alternatives))
            platformAlternatives = adoptNS([[PlatformTextAlternatives alloc] _initWithNSTextAlternatives:nsAlternatives.get()]);
    }
#endif

    if (!platformAlternatives)
        return;

    _page->addDictationAlternative({ platformAlternatives.get(), NSMakeRange(0, [platformAlternatives primaryString].length) });
}

- (void)removeEmojiAlternatives
{
    _page->dictationAlternativesAtSelection([weakSelf = WeakObjCPtr<WKContentView>(self)](auto&& contexts) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || !strongSelf->_page)
            return;

        RefPtr page = strongSelf->_page;
        Vector<WebCore::DictationContext> contextsToRemove;
        for (auto context : contexts) {
            auto alternatives = page->protectedPageClient()->platformDictationAlternatives(context);
            if (!alternatives)
                continue;

            auto originalAlternatives = alternatives.alternativeStrings;
            auto nonEmojiAlternatives = [NSMutableArray arrayWithCapacity:originalAlternatives.count];
            for (NSString *alternative in originalAlternatives) {
                if (!alternative._containsEmojiOnly)
                    [nonEmojiAlternatives addObject:alternative];
            }

            if (nonEmojiAlternatives.count == originalAlternatives.count)
                continue;

            RetainPtr<NSTextAlternatives> nsReplacement;
            if (nonEmojiAlternatives.count)
                nsReplacement = adoptNS([[NSTextAlternatives alloc] initWithPrimaryString:alternatives.primaryString alternativeStrings:nonEmojiAlternatives isLowConfidence:alternatives.isLowConfidence]);
            else
                contextsToRemove.append(context);

            RetainPtr<PlatformTextAlternatives> platformReplacement;
#if USE(BROWSERENGINEKIT)
            platformReplacement = adoptNS([[PlatformTextAlternatives alloc] _initWithNSTextAlternatives:nsReplacement.get()]);
#else
            platformReplacement = nsReplacement;
#endif
            if (RefPtr pageClient = page->pageClient())
                pageClient->replaceDictationAlternatives(platformReplacement.get(), context);
        }
        page->clearDictationAlternatives(WTFMove(contextsToRemove));
    });
}

// end of UITextInput protocol implementation

static UITextAutocapitalizationType toUITextAutocapitalize(WebCore::AutocapitalizeType webkitType)
{
    switch (webkitType) {
    case WebCore::AutocapitalizeType::Default:
        return UITextAutocapitalizationTypeSentences;
    case WebCore::AutocapitalizeType::None:
        return UITextAutocapitalizationTypeNone;
    case WebCore::AutocapitalizeType::Words:
        return UITextAutocapitalizationTypeWords;
    case WebCore::AutocapitalizeType::Sentences:
        return UITextAutocapitalizationTypeSentences;
    case WebCore::AutocapitalizeType::AllCharacters:
        return UITextAutocapitalizationTypeAllCharacters;
    }

    return UITextAutocapitalizationTypeSentences;
}

- (NSString *)contentTypeFromFieldName:(WebCore::AutofillFieldName)fieldName
{
    switch (fieldName) {
    case WebCore::AutofillFieldName::Name:
        return UITextContentTypeName;
    case WebCore::AutofillFieldName::HonorificPrefix:
        return UITextContentTypeNamePrefix;
    case WebCore::AutofillFieldName::GivenName:
        return UITextContentTypeGivenName;
    case WebCore::AutofillFieldName::AdditionalName:
        return UITextContentTypeMiddleName;
    case WebCore::AutofillFieldName::FamilyName:
        return UITextContentTypeFamilyName;
    case WebCore::AutofillFieldName::HonorificSuffix:
        return UITextContentTypeNameSuffix;
    case WebCore::AutofillFieldName::Nickname:
        return UITextContentTypeNickname;
    case WebCore::AutofillFieldName::OrganizationTitle:
        return UITextContentTypeJobTitle;
    case WebCore::AutofillFieldName::Organization:
        return UITextContentTypeOrganizationName;
    case WebCore::AutofillFieldName::StreetAddress:
        return UITextContentTypeFullStreetAddress;
    case WebCore::AutofillFieldName::AddressLine1:
        return UITextContentTypeStreetAddressLine1;
    case WebCore::AutofillFieldName::AddressLine2:
        return UITextContentTypeStreetAddressLine2;
    case WebCore::AutofillFieldName::AddressLevel3:
        return UITextContentTypeSublocality;
    case WebCore::AutofillFieldName::AddressLevel2:
        return UITextContentTypeAddressCity;
    case WebCore::AutofillFieldName::AddressLevel1:
        return UITextContentTypeAddressState;
    case WebCore::AutofillFieldName::CountryName:
        return UITextContentTypeCountryName;
    case WebCore::AutofillFieldName::PostalCode:
        return UITextContentTypePostalCode;
    case WebCore::AutofillFieldName::Tel:
        return UITextContentTypeTelephoneNumber;
    case WebCore::AutofillFieldName::Email:
        return UITextContentTypeEmailAddress;
    case WebCore::AutofillFieldName::URL:
        return UITextContentTypeURL;
    case WebCore::AutofillFieldName::Username:
    case WebCore::AutofillFieldName::WebAuthn:
        return UITextContentTypeUsername;
    case WebCore::AutofillFieldName::OneTimeCode:
        return UITextContentTypeOneTimeCode;
#if HAVE(ESIM_AUTOFILL_SYSTEM_SUPPORT)
    case WebCore::AutofillFieldName::DeviceEID:
        return _page->shouldAllowAutoFillForCellularIdentifiers() ? UITextContentTypeCellularEID : nil;
    case WebCore::AutofillFieldName::DeviceIMEI:
        return _page->shouldAllowAutoFillForCellularIdentifiers() ? UITextContentTypeCellularIMEI : nil;
#else
    case WebCore::AutofillFieldName::DeviceEID:
    case WebCore::AutofillFieldName::DeviceIMEI:
#endif
    case WebCore::AutofillFieldName::None:
    case WebCore::AutofillFieldName::NewPassword:
    case WebCore::AutofillFieldName::CurrentPassword:
    case WebCore::AutofillFieldName::AddressLine3:
    case WebCore::AutofillFieldName::AddressLevel4:
    case WebCore::AutofillFieldName::Country:
    case WebCore::AutofillFieldName::CcName:
    case WebCore::AutofillFieldName::CcGivenName:
    case WebCore::AutofillFieldName::CcAdditionalName:
    case WebCore::AutofillFieldName::CcFamilyName:
    case WebCore::AutofillFieldName::CcNumber:
    case WebCore::AutofillFieldName::CcExp:
    case WebCore::AutofillFieldName::CcExpMonth:
    case WebCore::AutofillFieldName::CcExpYear:
    case WebCore::AutofillFieldName::CcCsc:
    case WebCore::AutofillFieldName::CcType:
    case WebCore::AutofillFieldName::TransactionCurrency:
    case WebCore::AutofillFieldName::TransactionAmount:
    case WebCore::AutofillFieldName::Language:
    case WebCore::AutofillFieldName::Bday:
    case WebCore::AutofillFieldName::BdayDay:
    case WebCore::AutofillFieldName::BdayMonth:
    case WebCore::AutofillFieldName::BdayYear:
    case WebCore::AutofillFieldName::Sex:
    case WebCore::AutofillFieldName::Photo:
    case WebCore::AutofillFieldName::TelCountryCode:
    case WebCore::AutofillFieldName::TelNational:
    case WebCore::AutofillFieldName::TelAreaCode:
    case WebCore::AutofillFieldName::TelLocal:
    case WebCore::AutofillFieldName::TelLocalPrefix:
    case WebCore::AutofillFieldName::TelLocalSuffix:
    case WebCore::AutofillFieldName::TelExtension:
    case WebCore::AutofillFieldName::Impp:
        break;
    };

    return nil;
}

- (BOOL)_requiresLegacyTextInputTraits
{
    if (_cachedRequiresLegacyTextInputTraits.has_value())
        return *_cachedRequiresLegacyTextInputTraits;

    _cachedRequiresLegacyTextInputTraits = [&]() -> BOOL {
        if (!self.shouldUseAsyncInteractions)
            return YES;

        if ([_webView class] == WKWebView.class)
            return NO;

        static auto defaultTextInputTraitsMethod = class_getMethodImplementation(WKWebView.class, @selector(_textInputTraits));
        return defaultTextInputTraitsMethod != class_getMethodImplementation([_webView class], @selector(_textInputTraits));
    }();
    return *_cachedRequiresLegacyTextInputTraits;
}

// UITextInputPrivate protocol
// Direct access to the (private) UITextInputTraits object.
- (UITextInputTraits *)textInputTraits
{
    if (!self._requiresLegacyTextInputTraits)
        RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    _legacyTextInputTraits = [_webView _textInputTraits];
    return _legacyTextInputTraits.get();
}

- (UITextInputTraits *)textInputTraitsForWebView
{
    if (!_legacyTextInputTraits)
        _legacyTextInputTraits = adoptNS([UITextInputTraits new]);

    // Do not change traits when dismissing the keyboard.
    if (!_isBlurringFocusedElement)
        [self _updateTextInputTraits:_legacyTextInputTraits.get()];

    return _legacyTextInputTraits.get();
}

- (void)_updateTextInputTraits:(id<UITextInputTraits>)traits
{
    traits.secureTextEntry = _focusedElementInformation.elementType == WebKit::InputType::Password || [_formInputSession forceSecureTextEntry];

    switch (_focusedElementInformation.enterKeyHint) {
    case WebCore::EnterKeyHint::Enter:
        traits.returnKeyType = UIReturnKeyDefault;
        break;
    case WebCore::EnterKeyHint::Done:
        traits.returnKeyType = UIReturnKeyDone;
        break;
    case WebCore::EnterKeyHint::Go:
        traits.returnKeyType = UIReturnKeyGo;
        break;
    case WebCore::EnterKeyHint::Next:
        traits.returnKeyType = UIReturnKeyNext;
        break;
    case WebCore::EnterKeyHint::Search:
        traits.returnKeyType = UIReturnKeySearch;
        break;
    case WebCore::EnterKeyHint::Send:
        traits.returnKeyType = UIReturnKeySend;
        break;
    default: {
        if (!_focusedElementInformation.formAction.isEmpty())
            traits.returnKeyType = _focusedElementInformation.elementType == WebKit::InputType::Search ? UIReturnKeySearch : UIReturnKeyGo;
    }
    }

#if HAVE(UI_CONVERSATION_CONTEXT)
    if ([traits respondsToSelector:@selector(setConversationContext:)])
        traits.conversationContext = [_webView conversationContext];
#endif

    BOOL disableAutocorrectAndAutocapitalize = _focusedElementInformation.elementType == WebKit::InputType::Password || _focusedElementInformation.elementType == WebKit::InputType::Email
        || _focusedElementInformation.elementType == WebKit::InputType::URL || _focusedElementInformation.formAction.contains("login"_s);
    if ([traits respondsToSelector:@selector(setAutocapitalizationType:)])
        traits.autocapitalizationType = disableAutocorrectAndAutocapitalize ? UITextAutocapitalizationTypeNone : toUITextAutocapitalize(_focusedElementInformation.autocapitalizeType);
    if ([traits respondsToSelector:@selector(setAutocorrectionType:)])
        traits.autocorrectionType = disableAutocorrectAndAutocapitalize ? UITextAutocorrectionTypeNo : (_focusedElementInformation.isAutocorrect ? UITextAutocorrectionTypeYes : UITextAutocorrectionTypeNo);

    if (!_focusedElementInformation.isSpellCheckingEnabled) {
        if ([traits respondsToSelector:@selector(setSmartQuotesType:)])
            traits.smartQuotesType = UITextSmartQuotesTypeNo;
        if ([traits respondsToSelector:@selector(setSmartDashesType:)])
            traits.smartDashesType = UITextSmartDashesTypeNo;
        if ([traits respondsToSelector:@selector(setSpellCheckingType:)])
            traits.spellCheckingType = UITextSpellCheckingTypeNo;
    }

    switch (_focusedElementInformation.inputMode) {
    case WebCore::InputMode::None:
    case WebCore::InputMode::Unspecified:
        switch (_focusedElementInformation.elementType) {
        case WebKit::InputType::Phone:
            traits.keyboardType = UIKeyboardTypePhonePad;
            break;
        case WebKit::InputType::URL:
            traits.keyboardType = UIKeyboardTypeURL;
            break;
        case WebKit::InputType::Email:
            traits.keyboardType = UIKeyboardTypeEmailAddress;
            break;
        case WebKit::InputType::Number:
            traits.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
            break;
        case WebKit::InputType::NumberPad:
            traits.keyboardType = UIKeyboardTypeNumberPad;
            break;
        case WebKit::InputType::None:
        case WebKit::InputType::ContentEditable:
        case WebKit::InputType::Text:
        case WebKit::InputType::Password:
        case WebKit::InputType::TextArea:
        case WebKit::InputType::Search:
        case WebKit::InputType::Date:
        case WebKit::InputType::DateTimeLocal:
        case WebKit::InputType::Month:
        case WebKit::InputType::Week:
        case WebKit::InputType::Time:
        case WebKit::InputType::Select:
        case WebKit::InputType::Drawing:
        case WebKit::InputType::Color:
            traits.keyboardType = UIKeyboardTypeDefault;
        }
        break;
    case WebCore::InputMode::Text:
        traits.keyboardType = UIKeyboardTypeDefault;
        break;
    case WebCore::InputMode::Telephone:
        traits.keyboardType = UIKeyboardTypePhonePad;
        break;
    case WebCore::InputMode::Url:
        traits.keyboardType = UIKeyboardTypeURL;
        break;
    case WebCore::InputMode::Email:
        traits.keyboardType = UIKeyboardTypeEmailAddress;
        break;
    case WebCore::InputMode::Numeric:
        traits.keyboardType = UIKeyboardTypeNumberPad;
        break;
    case WebCore::InputMode::Decimal:
        traits.keyboardType = UIKeyboardTypeDecimalPad;
        break;
    case WebCore::InputMode::Search:
        traits.keyboardType = UIKeyboardTypeWebSearch;
        break;
    }

#if HAVE(PEPPER_UI_CORE)
    traits.textContentType = self.textContentTypeForQuickboard;
#else
    traits.textContentType = [self contentTypeFromFieldName:_focusedElementInformation.autofillFieldName];
#endif

    auto extendedTraits = dynamic_objc_cast<WKExtendedTextInputTraits>(traits);
    auto privateTraits = (id <UITextInputTraits_Private>)traits;

    BOOL isSingleLineDocument = ^{
        switch (_focusedElementInformation.elementType) {
        case WebKit::InputType::ContentEditable:
        case WebKit::InputType::TextArea:
        case WebKit::InputType::None:
            return NO;
        case WebKit::InputType::Color:
        case WebKit::InputType::Date:
        case WebKit::InputType::DateTimeLocal:
        case WebKit::InputType::Drawing:
        case WebKit::InputType::Email:
        case WebKit::InputType::Month:
        case WebKit::InputType::Number:
        case WebKit::InputType::NumberPad:
        case WebKit::InputType::Password:
        case WebKit::InputType::Phone:
        case WebKit::InputType::Search:
        case WebKit::InputType::Select:
        case WebKit::InputType::Text:
        case WebKit::InputType::Time:
        case WebKit::InputType::URL:
        case WebKit::InputType::Week:
            return YES;
        }
    }();

    if ([extendedTraits respondsToSelector:@selector(setSingleLineDocument:)])
        extendedTraits.singleLineDocument = isSingleLineDocument;
    else if ([privateTraits respondsToSelector:@selector(setIsSingleLineDocument:)])
        privateTraits.isSingleLineDocument = isSingleLineDocument;

    if (_focusedElementInformation.hasEverBeenPasswordField) {
        if ([privateTraits respondsToSelector:@selector(setLearnsCorrections:)])
            privateTraits.learnsCorrections = NO;
        extendedTraits.typingAdaptationEnabled = NO;
    }

    if ([privateTraits respondsToSelector:@selector(setShortcutConversionType:)])
        privateTraits.shortcutConversionType = _focusedElementInformation.elementType == WebKit::InputType::Password ? UITextShortcutConversionTypeNo : UITextShortcutConversionTypeDefault;

#if HAVE(INLINE_PREDICTIONS)
    bool allowsInlinePredictions = [&] {
#if PLATFORM(MACCATALYST)
        return false;
#else
        if (self.webView.configuration.allowsInlinePredictions)
            return true;
        if (auto& state = _page->editorState(); state.hasPostLayoutData() && !_isFocusingElementWithKeyboard && !_page->waitingForPostLayoutEditorStateUpdateAfterFocusingElement())
            return state.postLayoutData->canEnableWritingSuggestions;
        return _focusedElementInformation.isWritingSuggestionsEnabled;
#endif
    }();
    traits.inlinePredictionType = allowsInlinePredictions ? UITextInlinePredictionTypeDefault : UITextInlinePredictionTypeNo;
#endif

    [self _updateTextInputTraitsForInteractionTintColor];
}

- (UITextInteractionAssistant *)interactionAssistant
{
    if (!self.shouldUseAsyncInteractions)
        return [_textInteractionWrapper textInteractionAssistant];
    return super.interactionAssistant;
}

// NSRange support.  Would like to deprecate to the extent possible, although some support
// (i.e. selectionRange) has shipped as API.
- (NSRange)selectionRange
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return NSMakeRange(NSNotFound, 0);
}

- (CGRect)rectForNSRange:(NSRange)range
{
    return CGRectZero;
}

- (NSRange)_markedTextNSRange
{
    return NSMakeRange(NSNotFound, 0);
}

// DOM range support.
- (DOMRange *)selectedDOMRange
{
    return nil;
}

- (void)setSelectedDOMRange:(DOMRange *)range affinityDownstream:(BOOL)affinityDownstream
{
}

// Modify text without starting a new undo grouping.
- (void)replaceRangeWithTextWithoutClosingTyping:(UITextRange *)range replacementText:(NSString *)text
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();
}

// Caret rect support.  Shouldn't be necessary, but firstRectForRange doesn't offer precisely
// the same functionality.
- (CGRect)rectContainingCaretSelection
{
    return CGRectZero;
}

- (BOOL)_isTextInputContextFocused:(_WKTextInputContext *)context
{
    ASSERT(context);
    // We ignore bounding rect changes as the bounding rect of the focused element is not kept up-to-date.
    return self._hasFocusedElement && context._textInputContext.isSameElement(_focusedElementInformation.elementContext);
}

- (void)_focusTextInputContext:(_WKTextInputContext *)context placeCaretAt:(CGPoint)point completionHandler:(void (^)(UIResponder<UITextInput> *))completionHandler
{
    ASSERT(context);
    // This function can be called more than once during a text interaction (e.g. <rdar://problem/59430806>).
    if (![self becomeFirstResponder]) {
        completionHandler(nil);
        return;
    }
    if ([self _isTextInputContextFocused:context]) {
        completionHandler(_focusedElementInformation.isReadOnly ? nil : self);
        return;
    }
    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
    auto checkFocusedElement = [weakSelf = WeakObjCPtr<WKContentView> { self }, context = adoptNS([context copy]), completionHandler = makeBlockPtr(completionHandler)] (bool success) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf) {
            completionHandler(nil);
            return;
        }
        bool isFocused = success && [strongSelf _isTextInputContextFocused:context.get()];
        bool isEditable = success && !strongSelf->_focusedElementInformation.isReadOnly;
        strongSelf->_textInteractionDidChangeFocusedElement |= isFocused;
        strongSelf->_usingGestureForSelection = NO;
        completionHandler(isFocused && isEditable ? strongSelf.get() : nil);
    };
    _page->focusTextInputContextAndPlaceCaret(context._textInputContext, WebCore::IntPoint { point }, WTFMove(checkFocusedElement));
}

- (void)_requestTextInputContextsInRect:(CGRect)rect completionHandler:(void (^)(NSArray<_WKTextInputContext *> *))completionHandler
{
    WebCore::FloatRect searchRect { rect };
#if ENABLE(EDITABLE_REGION)
    bool hitInteractionRect = self._hasFocusedElement && searchRect.inclusivelyIntersects(_focusedElementInformation.interactionRect);
    if (!self.webView._editable && !hitInteractionRect && !WebKit::mayContainEditableElementsInRect(self, rect)) {
        completionHandler(@[ ]);
        return;
    }
#endif
    _page->textInputContextsInRect(searchRect, [weakSelf = WeakObjCPtr<WKContentView>(self), completionHandler = makeBlockPtr(completionHandler)] (const Vector<WebCore::ElementContext>& contexts) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || contexts.isEmpty()) {
            completionHandler(@[ ]);
            return;
        }
        completionHandler(createNSArray(contexts, [] (const auto& context) {
            return adoptNS([[_WKTextInputContext alloc] _initWithTextInputContext:context]);
        }).get());
    });
}

- (void)_willBeginTextInteractionInTextInputContext:(_WKTextInputContext *)context
{
    ASSERT(context);

    _page->setCanShowPlaceholder(context._textInputContext, false);

    ++_activeTextInteractionCount;
    if (_activeTextInteractionCount > 1)
        return;

    _textInteractionDidChangeFocusedElement = NO;
    _page->setShouldRevealCurrentSelectionAfterInsertion(false);
    _autocorrectionContextNeedsUpdate = YES;
    _usingGestureForSelection = YES;
}

- (void)_didFinishTextInteractionInTextInputContext:(_WKTextInputContext *)context
{
    ASSERT(context);

    _page->setCanShowPlaceholder(context._textInputContext, true);

    ASSERT(_activeTextInteractionCount > 0);
    --_activeTextInteractionCount;
    if (_activeTextInteractionCount)
        return;

    _usingGestureForSelection = NO;

    if (_textInteractionDidChangeFocusedElement) {
        // Mark to zoom to reveal the newly focused element on the next editor state update.
        // Then tell the web process to reveal the current selection, which will send us (the
        // UI process) an editor state update.
        _revealFocusedElementDeferrer = WebKit::RevealFocusedElementDeferrer::create(self, WebKit::RevealFocusedElementDeferralReason::EditorState);
        _page->setWaitingForPostLayoutEditorStateUpdateAfterFocusingElement(true);
        _textInteractionDidChangeFocusedElement = NO;
    }

    _page->setShouldRevealCurrentSelectionAfterInsertion(true);
}

- (void)modifierFlagsDidChangeFrom:(UIKeyModifierFlags)oldFlags to:(UIKeyModifierFlags)newFlags
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    auto dispatchSyntheticFlagsChangedEvents = [&] (UIKeyModifierFlags flags, bool keyDown) {
        if (flags & UIKeyModifierShift)
            [self handleKeyWebEvent:adoptNS([[WKSyntheticFlagsChangedWebEvent alloc] initWithShiftState:keyDown]).get()];
        if (flags & UIKeyModifierAlphaShift)
            [self handleKeyWebEvent:adoptNS([[WKSyntheticFlagsChangedWebEvent alloc] initWithCapsLockState:keyDown]).get()];
    };

    if (UIKeyModifierFlags removedFlags = oldFlags & ~newFlags)
        dispatchSyntheticFlagsChangedEvents(removedFlags, false);
    if (UIKeyModifierFlags addedFlags = newFlags & ~oldFlags)
        dispatchSyntheticFlagsChangedEvents(addedFlags, true);
}

- (BOOL)shouldSuppressUpdateCandidateView
{
    return _candidateViewNeedsUpdate;
}

// Web events.
- (BOOL)requiresKeyEvents
{
    return YES;
}

- (BOOL)_tryToHandlePressesEvent:(UIPressesEvent *)event
{
    bool isHardwareKeyboardEvent = !!event._hidEvent;
    // We only want to handle key event from the hardware keyboard when we are
    // first responder and we are not interacting with editable content.
    if (self.isFirstResponder && isHardwareKeyboardEvent && (_inputPeripheral || !_page->editorState().isContentEditable)) {
        if ([_inputPeripheral respondsToSelector:@selector(handleKeyEvent:)]) {
            if ([_inputPeripheral handleKeyEvent:event])
                return YES;
        }
        if (!_seenHardwareKeyDownInNonEditableElement) {
            _seenHardwareKeyDownInNonEditableElement = YES;
            [self reloadInputViews];
        }
    }
    return NO;
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    if ([self _tryToHandlePressesEvent:event])
        return;

    _isHandlingActivePressesEvent = YES;
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    if ([self _tryToHandlePressesEvent:event])
        return;

    _isHandlingActivePressesEvent = NO;
    [super pressesEnded:presses withEvent:event];
}

- (void)pressesChanged:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    if ([self _tryToHandlePressesEvent:event])
        return;

    [super pressesChanged:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    if ([self _tryToHandlePressesEvent:event])
        return;

    _isHandlingActivePressesEvent = NO;
    [super pressesCancelled:presses withEvent:event];
}

- (void)generateSyntheticEditingCommand:(WebKit::SyntheticEditingCommandType)command
{
    _page->generateSyntheticEditingCommand(command);
}

- (void)handleKeyWebEvent:(::WebEvent *)theEvent
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalHandleKeyWebEvent:theEvent];
}

- (void)_internalHandleKeyWebEvent:(::WebEvent *)theEvent
{
    _page->handleKeyboardEvent(WebKit::NativeWebKeyboardEvent(theEvent, WebKit::NativeWebKeyboardEvent::HandledByInputMethod::No));
}

- (void)handleKeyWebEvent:(::WebEvent *)event withCompletionHandler:(void (^)(::WebEvent *theEvent, BOOL wasHandled))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalHandleKeyWebEvent:event withCompletionHandler:completionHandler];
}

- (BOOL)_deferKeyEventToInputMethodEditing:(::WebEvent *)event
{
    if (!_page->editorState().isContentEditable && !_treatAsContentEditableUntilNextEditorStateUpdate)
        return NO;

#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        RetainPtr inputDelegate = _asyncInputDelegate;
        [self _logMissingSystemInputDelegateIfNeeded:__PRETTY_FUNCTION__];
        if (!inputDelegate)
            return NO;

        auto context = adoptNS([[BEKeyEntryContext alloc] initWithKeyEntry:event.originalKeyEntry]);
        [context setDocumentEditable:YES];
        [context setShouldEvaluateForInputSystemHandling:YES];
        return [inputDelegate shouldDeferEventHandlingToSystemForTextInput:self.asBETextInput context:context.get()];
    }
#endif // USE(BROWSERENGINEKIT)

    return [[UIKeyboardImpl sharedInstance] handleKeyInputMethodCommandForCurrentEvent];
}

- (void)_internalHandleKeyWebEvent:(::WebEvent *)event withCompletionHandler:(void (^)(::WebEvent *event, BOOL wasHandled))completionHandler
{
    if (!isUIThread()) {
        RELEASE_LOG_FAULT(KeyHandling, "%s was invoked on a background thread.", __PRETTY_FUNCTION__);
        completionHandler(event, NO);
        return;
    }

#if PLATFORM(MACCATALYST)
    bool hasPendingArrowKey = _keyWebEventHandlers.containsIf([](auto& eventAndCompletion) {
        switch ([eventAndCompletion.event keyCode]) {
        case VK_RIGHT:
        case VK_LEFT:
        case VK_DOWN:
        case VK_UP:
            return true;
        default:
            return false;
        }
    });

    if (hasPendingArrowKey) {
        RELEASE_LOG_ERROR(KeyHandling, "Ignoring incoming key event due to pending arrow key event");
        completionHandler(event, NO);
        return;
    }
#endif // PLATFORM(MACCATALYST)

    if (event.type == WebEventKeyDown)
        _isHandlingActiveKeyEvent = YES;

    [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::DeniedForGesture];

    using HandledByInputMethod = WebKit::NativeWebKeyboardEvent::HandledByInputMethod;
    if ([self _deferKeyEventToInputMethodEditing:event]) {
        completionHandler(event, YES);
        _isDeferringKeyEventsToInputMethod = YES;
        _page->handleKeyboardEvent(WebKit::NativeWebKeyboardEvent(event, HandledByInputMethod::Yes));
        return;
    }

    if (_page->handleKeyboardEvent(WebKit::NativeWebKeyboardEvent(event, HandledByInputMethod::No)))
        _keyWebEventHandlers.append({ event, makeBlockPtr(completionHandler) });
    else
        completionHandler(event, NO);
}

- (void)_didHandleKeyEvent:(::WebEvent *)event eventWasHandled:(BOOL)eventWasHandled
{
    if ([event isKindOfClass:[WKSyntheticFlagsChangedWebEvent class]])
        return;

    if (!(event.keyboardFlags & WebEventKeyboardInputModifierFlagsChanged))
        [_keyboardScrollingAnimator handleKeyEvent:event];

    if (event.type == WebEventKeyUp)
        _isHandlingActiveKeyEvent = NO;

    auto indexOfHandlerToCall = _keyWebEventHandlers.findIf([event](auto& entry) {
        return entry.event == event;
    });

    if (indexOfHandlerToCall == notFound)
        return;

    auto handler = _keyWebEventHandlers[indexOfHandlerToCall].completionBlock;
    if (!handler) {
        ASSERT_NOT_REACHED();
        return;
    }

    _keyWebEventHandlers.removeAt(indexOfHandlerToCall);
    handler(event, eventWasHandled);
}

- (BOOL)_interpretKeyEvent:(::WebEvent *)event withContext:(WebKit::KeyEventInterpretationContext&&)context
{
    SetForScope interpretKeyEventScope { _isInterpretingKeyEvent, YES };

    BOOL isCharEvent = context.isCharEvent;

    RetainPtr scrollView = [self _scrollViewForScrollingNodeID:context.scrollingNode];

    if ([_keyboardScrollingAnimator beginWithEvent:event scrollView:(WKBaseScrollView *)scrollView.get() ?: self.webView._scrollViewInternal] || [_keyboardScrollingAnimator scrollTriggeringKeyIsPressed])
        return YES;

#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        RetainPtr systemDelegate = _asyncInputDelegate;
        [self _logMissingSystemInputDelegateIfNeeded:__PRETTY_FUNCTION__];
        if (!systemDelegate)
            return NO;

        auto context = adoptNS([[BEKeyEntryContext alloc] initWithKeyEntry:event.originalKeyEntry]);
        [context setDocumentEditable:_page->editorState().isContentEditable];
        [context setShouldInsertCharacter:isCharEvent];
        return [systemDelegate shouldDeferEventHandlingToSystemForTextInput:self.asBETextInput context:context.get()];
    }
#endif // USE(BROWSERENGINEKIT)

    if (event.keyboardFlags & WebEventKeyboardInputModifierFlagsChanged)
        return NO;

    BOOL contentEditable = _page->editorState().isContentEditable;

    if (!contentEditable && event.isTabKey)
        return NO;

    UIKeyboardImpl *keyboard = [UIKeyboardImpl sharedInstance];

    if (!isCharEvent && [keyboard handleKeyTextCommandForCurrentEvent])
        return YES;
    if (isCharEvent && [keyboard handleKeyAppCommandForCurrentEvent])
        return YES;

    // Don't insert character for an unhandled Command-key key command. This matches iOS and Mac platform conventions.
    if (event.modifierFlags & WebEventFlagMaskCommandKey)
        return NO;

    NSString *characters = event.characters;
    if (!characters.length)
        return NO;

    switch ([characters characterAtIndex:0]) {
    case NSBackspaceCharacter:
    case NSDeleteCharacter:
        if (contentEditable) {
            [keyboard deleteFromInputWithFlags:event.keyboardFlags];
            return YES;
        }
        break;
    case NSEnterCharacter:
    case NSCarriageReturnCharacter:
        if (contentEditable && isCharEvent) {
            // Map \r from HW keyboard to \n to match the behavior of the soft keyboard.
            [keyboard addInputString:@"\n" withFlags:0 withInputManagerHint:nil];
            return YES;
        }
        break;
    default:
        if (contentEditable && isCharEvent) {
            [keyboard addInputString:event.characters withFlags:event.keyboardFlags withInputManagerHint:event.inputManagerHint];
            return YES;
        }
        break;
    }

    return NO;
}

- (NSArray<NSString *> *)filePickerAcceptedTypeIdentifiers
{
    if (!_fileUploadPanel)
        return @[];

    return [_fileUploadPanel acceptedTypeIdentifiers];
}

- (void)dismissFilePicker
{
    [_fileUploadPanel dismissIfNeededWithReason:WebKit::PickerDismissalReason::Testing];
}

- (BOOL)isScrollableForKeyboardScrollViewAnimator:(WKKeyboardScrollViewAnimator *)animator
{
    if (_page->editorState().isContentEditable)
        return NO;

    if (_focusedElementInformation.elementType == WebKit::InputType::Select)
        return NO;

    if (!self.webView.scrollView.scrollEnabled)
        return NO;

#if HAVE(UISCROLLVIEW_ALLOWS_KEYBOARD_SCROLLING)
    if (!self.webView.scrollView.allowsKeyboardScrolling)
        return NO;
#endif

    return YES;
}

- (CGFloat)keyboardScrollViewAnimator:(WKKeyboardScrollViewAnimator *)animator distanceForIncrement:(WebCore::ScrollGranularity)increment inDirection:(WebCore::ScrollDirection)direction
{
    BOOL directionIsHorizontal = direction == WebCore::ScrollDirection::ScrollLeft || direction == WebCore::ScrollDirection::ScrollRight;

    switch (increment) {
    case WebCore::ScrollGranularity::Document: {
        CGSize documentSize = [self convertRect:self.bounds toView:self.webView].size;
        return directionIsHorizontal ? documentSize.width : documentSize.height;
    }
    case WebCore::ScrollGranularity::Page: {
        CGSize pageSize = [self convertSize:CGSizeMake(0, WebCore::Scrollbar::pageStep(_page->unobscuredContentRect().height(), self.bounds.size.height)) toView:self.webView];
        return directionIsHorizontal ? pageSize.width : pageSize.height;
    }
    case WebCore::ScrollGranularity::Line:
        return [self convertSize:CGSizeMake(0, WebCore::Scrollbar::pixelsPerLineStep()) toView:self.webView].height;
    case WebCore::ScrollGranularity::Pixel:
        return 0;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

- (void)keyboardScrollViewAnimatorWillScroll:(WKKeyboardScrollViewAnimator *)animator
{
    _isKeyboardScrollingAnimationRunning = YES;
    [self willStartZoomOrScroll];
}

- (void)keyboardScrollViewAnimatorDidFinishScrolling:(WKKeyboardScrollViewAnimator *)animator
{
    [_webView _didFinishScrolling:animator.scrollView];
    _isKeyboardScrollingAnimationRunning = NO;
}

- (BOOL)isKeyboardScrollingAnimationRunning
{
    return _isKeyboardScrollingAnimationRunning;
}

- (void)_executeEditCommand:(NSString *)commandName
{
    [self _executeEditCommand:commandName notifyDelegate:WebKit::NotifyInputDelegate::Yes];
}

- (void)_executeEditCommand:(NSString *)commandName notifyDelegate:(WebKit::NotifyInputDelegate)notifyDelegate
{
    _autocorrectionContextNeedsUpdate = YES;
    // FIXME: Editing commands are not considered by WebKit as user initiated even if they are the result
    // of keydown or keyup. We need to query the keyboard to determine if this was called from the keyboard
    // or not to know whether to tell WebKit to treat this command as user initiated or not.
    if (notifyDelegate == WebKit::NotifyInputDelegate::Yes)
        [self _internalBeginSelectionChange];
    _page->executeEditCommand(commandName, { }, [view = retainPtr(self), notifyDelegate] {
        if (notifyDelegate == WebKit::NotifyInputDelegate::Yes)
            [view _internalEndSelectionChange];
    });
}

- (void)_deleteByWord
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteWordBackward"];
}

- (void)_deleteForwardByWord
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteWordForward"];
}

- (void)_deleteToStartOfLine
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteToBeginningOfLine"];
}

- (void)_deleteToEndOfLine
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteToEndOfLine"];
}

- (void)_deleteForwardAndNotify:(BOOL)notify
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteForward"];
}

- (void)_deleteToEndOfParagraph
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:@"deleteToEndOfParagraph"];
}

- (void)_transpose
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self transposeCharactersAroundSelection];
}

- (UITextInputArrowKeyHistory *)_moveUp:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveUpAndModifySelection" : @"moveUp"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveDown:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveDownAndModifySelection" : @"moveDown"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveLeft:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending? @"moveLeftAndModifySelection" : @"moveLeft"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveRight:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveRightAndModifySelection" : @"moveRight"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToStartOfWord:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveWordBackwardAndModifySelection" : @"moveWordBackward"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToStartOfParagraph:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveBackwardAndModifySelection" : @"moveBackward"];
    [self _executeEditCommand:extending ? @"moveToBeginningOfParagraphAndModifySelection" : @"moveToBeginningOfParagraph"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToStartOfLine:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveToBeginningOfLineAndModifySelection" : @"moveToBeginningOfLine"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToStartOfDocument:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveToBeginningOfDocumentAndModifySelection" : @"moveToBeginningOfDocument"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToEndOfWord:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveWordForwardAndModifySelection" : @"moveWordForward"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToEndOfParagraph:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveForwardAndModifySelection" : @"moveForward"];
    [self _executeEditCommand:extending ? @"moveToEndOfParagraphAndModifySelection" : @"moveToEndOfParagraph"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToEndOfLine:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveToEndOfLineAndModifySelection" : @"moveToEndOfLine"];
    return nil;
}

- (UITextInputArrowKeyHistory *)_moveToEndOfDocument:(BOOL)extending withHistory:(UITextInputArrowKeyHistory *)history
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _executeEditCommand:extending ? @"moveToEndOfDocumentAndModifySelection" : @"moveToEndOfDocument"];
    return nil;
}

// Sets a buffer to make room for autocorrection views
- (void)setBottomBufferHeight:(CGFloat)bottomBuffer
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();
}

- (UIView *)automaticallySelectedOverlay
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return [self unscaledView];
}

- (UITextGranularity)selectionGranularity
{
    return UITextGranularityCharacter;
}

// The can all be (and have been) trivially implemented in terms of UITextInput.  Deprecate and remove.
- (void)moveBackward:(unsigned)count
{
}

- (void)moveForward:(unsigned)count
{
}

- (unichar)characterBeforeCaretSelection
{
    return 0;
}

- (NSString *)wordContainingCaretSelection
{
    return nil;
}

- (DOMRange *)wordRangeContainingCaretSelection
{
    return nil;
}

- (void)setMarkedText:(NSString *)text
{
}

- (BOOL)hasContent
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return self._hasContent;
}

- (BOOL)_hasContent
{
    auto& editorState = _page->editorState();
    return !editorState.selectionIsNone && editorState.postLayoutData && editorState.postLayoutData->hasContent;
}

- (void)selectAll
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();
}

- (BOOL)hasSelection
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return NO;
}

- (BOOL)isPosition:(UITextPosition *)position atBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction
{
    if (granularity == UITextGranularityParagraph) {
        if (!_page->editorState().postLayoutData)
            return NO;

        if (direction == UITextStorageDirectionBackward && [position isEqual:self.selectedTextRange.start])
            return _page->editorState().postLayoutData->selectionStartIsAtParagraphBoundary;

        if (direction == UITextStorageDirectionForward && [position isEqual:self.selectedTextRange.end])
            return _page->editorState().postLayoutData->selectionEndIsAtParagraphBoundary;
    }

    return NO;
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position toBoundary:(UITextGranularity)granularity inDirection:(UITextDirection)direction
{
    return nil;
}

- (BOOL)isPosition:(UITextPosition *)position withinTextUnit:(UITextGranularity)granularity inDirection:(UITextDirection)direction
{
    return NO;
}

- (UITextRange *)rangeEnclosingPosition:(UITextPosition *)position withGranularity:(UITextGranularity)granularity inDirection:(UITextDirection)direction
{
    return nil;
}

- (void)takeTraitsFrom:(UITextInputTraits *)traits
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [[self textInputTraits] takeTraitsFrom:traits];
}

- (void)_showKeyboard
{
    [self setUpTextSelectionAssistant];
    
    if (self.isFirstResponder && !_suppressSelectionAssistantReasons)
        [_textInteractionWrapper activateSelection];

    [self reloadInputViews];
}

- (void)_hideKeyboard
{
    SetForScope isHidingKeyboardScope { _isHidingKeyboard, YES };

    self.inputDelegate = nil;
    [self setUpTextSelectionAssistant];
    
    [_textInteractionWrapper deactivateSelection];
    [_formAccessoryView hideAutoFillButton];

    // FIXME: Does it make sense to call -reloadInputViews on watchOS?
    [self reloadInputViews];
    if (_formAccessoryView)
        [self _updateAccessory];
}

- (BOOL)_formControlRefreshEnabled
{
    if (!_page)
        return NO;

    return YES;
}

- (const WebKit::FocusedElementInformation&)focusedElementInformation
{
    return _focusedElementInformation;
}

- (Vector<WebKit::OptionItem>&)focusedSelectElementOptions
{
    return _focusedElementInformation.selectOptions;
}

// Note that selectability is also affected by the CSS property user-select.
static bool mayContainSelectableText(WebKit::InputType type)
{
    switch (type) {
    case WebKit::InputType::None:
    // The following types have custom UI and do not look or behave like a text field.
    case WebKit::InputType::Color:
    case WebKit::InputType::Date:
    case WebKit::InputType::DateTimeLocal:
    case WebKit::InputType::Drawing:
    case WebKit::InputType::Month:
    case WebKit::InputType::Select:
    case WebKit::InputType::Time:
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return false;
    // The following types look and behave like a text field.
    case WebKit::InputType::ContentEditable:
    case WebKit::InputType::Email:
    case WebKit::InputType::Number:
    case WebKit::InputType::NumberPad:
    case WebKit::InputType::Password:
    case WebKit::InputType::Phone:
    case WebKit::InputType::Search:
    case WebKit::InputType::Text:
    case WebKit::InputType::TextArea:
    case WebKit::InputType::URL:
#if !ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return true;
    }
}

- (BOOL)_shouldShowKeyboardForElement:(const WebKit::FocusedElementInformation&)information
{
    if (information.inputMode == WebCore::InputMode::None)
        return NO;

    return [self _shouldShowKeyboardForElementIgnoringInputMode:information];
}

- (BOOL)_shouldShowKeyboardForElementIgnoringInputMode:(const WebKit::FocusedElementInformation&)information
{
    return mayContainSelectableText(information.elementType) || [self _elementTypeRequiresAccessoryView:information.elementType];
}

static RetainPtr<NSObject <WKFormPeripheral>> createInputPeripheralWithView(WebKit::InputType type, WKContentView *view)
{
#if PLATFORM(WATCHOS)
    UNUSED_PARAM(type);
    UNUSED_PARAM(view);
    return nil;
#else
    switch (type) {
    case WebKit::InputType::Select:
        return adoptNS([[WKFormSelectControl alloc] initWithView:view]);
    case WebKit::InputType::Color:
#if PLATFORM(APPLETV)
        return nil;
#else
        return adoptNS([[WKFormColorControl alloc] initWithView:view]);
#endif
    case WebKit::InputType::Date:
    case WebKit::InputType::DateTimeLocal:
    case WebKit::InputType::Month:
    case WebKit::InputType::Time:
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
    case WebKit::InputType::Week:
#endif
        return adoptNS([[WKDateTimeInputControl alloc] initWithView:view]);
    default:
        return nil;
    }
#endif
}

- (void)startDeferringInputViewUpdates:(WebKit::InputViewUpdateDeferralSources)sources
{
    ASSERT(!sources.isEmpty());
    if (_inputViewUpdateDeferralSources.isEmpty()) {
        RELEASE_LOG(TextInput, "Started deferring input view updates (%02x)", sources.toRaw());
        [self _beginPinningInputViews];
    }

    _inputViewUpdateDeferralSources.add(sources);
}

- (void)stopDeferringInputViewUpdates:(WebKit::InputViewUpdateDeferralSources)sources
{
    ASSERT(!sources.isEmpty());
    if (!_inputViewUpdateDeferralSources.containsAny(sources))
        return;

    _inputViewUpdateDeferralSources.remove(sources);

    if (_inputViewUpdateDeferralSources.isEmpty()) {
        [self _cancelPreviousResetInputViewDeferralRequest];
        RELEASE_LOG(TextInput, "Stopped deferring input view updates (%02x)", sources.toRaw());
        [self _endPinningInputViews];
    }
}

- (void)stopDeferringInputViewUpdatesForAllSources
{
    if (_inputViewUpdateDeferralSources.isEmpty())
        return;

    RELEASE_LOG(TextInput, "Stopped deferring all input view updates (%02x)", _inputViewUpdateDeferralSources.toRaw());
    [self _cancelPreviousResetInputViewDeferralRequest];
    _inputViewUpdateDeferralSources = { };
    [self _endPinningInputViews];
}

- (void)_elementDidFocus:(const WebKit::FocusedElementInformation&)information userIsInteracting:(BOOL)userIsInteracting blurPreviousNode:(BOOL)blurPreviousNode activityStateChanges:(OptionSet<WebCore::ActivityState>)activityStateChanges userObject:(NSObject <NSSecureCoding> *)userObject
{
    CompletionHandlerCallingScope restoreValues([
        weakSelf = WeakObjCPtr { self },
        changingFocusValueToRestore = _isChangingFocus,
        focusingElementWithKeyboardValueToRestore = _isFocusingElementWithKeyboard
    ] {
        RetainPtr strongSelf = weakSelf.get();
        if (!strongSelf)
            return;
        strongSelf->_isChangingFocus = changingFocusValueToRestore;
        strongSelf->_isFocusingElementWithKeyboard = focusingElementWithKeyboardValueToRestore;

        if (auto callback = std::exchange(strongSelf->_pendingRunModalJavaScriptDialogCallback, { }))
            callback();

        constexpr OptionSet sourcesToStopDeferring {
            WebKit::InputViewUpdateDeferralSource::ChangingFocusedElement,
            WebKit::InputViewUpdateDeferralSource::BecomeFirstResponder
        };
        [strongSelf stopDeferringInputViewUpdates:sourcesToStopDeferring];
    });

    _isChangingFocus = self._hasFocusedElement;
    _isFocusingElementWithKeyboard = [self _shouldShowKeyboardForElement:information];

    _autocorrectionContextNeedsUpdate = YES;
    _didAccessoryTabInitiateFocus = _isChangingFocusUsingAccessoryTab;

    id <_WKInputDelegate> inputDelegate = [_webView _inputDelegate];
    RetainPtr<WKFocusedElementInfo> focusedElementInfo = adoptNS([[WKFocusedElementInfo alloc] initWithFocusedElementInformation:information isUserInitiated:userIsInteracting webView:_webView.get().get() userObject:userObject]);

    _WKFocusStartsInputSessionPolicy startInputSessionPolicy = _WKFocusStartsInputSessionPolicyAuto;

    if ([inputDelegate respondsToSelector:@selector(_webView:focusShouldStartInputSession:)]) {
        if ([inputDelegate _webView:self.webView focusShouldStartInputSession:focusedElementInfo.get()])
            startInputSessionPolicy = _WKFocusStartsInputSessionPolicyAllow;
        else
            startInputSessionPolicy = _WKFocusStartsInputSessionPolicyDisallow;
    }

    if ([inputDelegate respondsToSelector:@selector(_webView:decidePolicyForFocusedElement:)])
        startInputSessionPolicy = [inputDelegate _webView:self.webView decidePolicyForFocusedElement:focusedElementInfo.get()];

    BOOL shouldShowInputView = [&] {
        switch (startInputSessionPolicy) {
        case _WKFocusStartsInputSessionPolicyAuto:
            // The default behavior is to allow node assistance if the user is interacting.
            // We also allow node assistance if the keyboard already is showing, unless we're in extra zoom mode.
            if (userIsInteracting)
                return YES;

            if (self.isFirstResponder || _becomingFirstResponder) {
                // When the software keyboard is being used to enter an url, only the focus activity state is changing.
                // In this case, auto focus on the page being navigated to should be disabled, unless a hardware
                // keyboard is attached.
                if (activityStateChanges && activityStateChanges != WebCore::ActivityState::IsFocused)
                    return YES;

#if HAVE(PEPPER_UI_CORE)
                if (_isChangingFocus && ![_focusedFormControlView isHidden])
                    return YES;
#else
                if (_isChangingFocus)
                    return YES;

                if ([self _shouldShowKeyboardForElementIgnoringInputMode:information] && UIKeyboard.isInHardwareKeyboardMode)
                    return YES;
#endif
            }
            return NO;
        case _WKFocusStartsInputSessionPolicyAllow:
            return YES;
        case _WKFocusStartsInputSessionPolicyDisallow:
            return NO;
        default:
            ASSERT_NOT_REACHED();
            return NO;
        }
    }();

    // Do not present input peripherals if a validation message is being displayed.
    if (information.isFocusingWithValidationMessage && !_isFocusingElementWithKeyboard)
        shouldShowInputView = NO;

    if (blurPreviousNode) {
        // Defer view updates until the end of this function to avoid a noticeable flash when switching focus
        // between elements that require the keyboard.
        [self startDeferringInputViewUpdates:WebKit::InputViewUpdateDeferralSource::ChangingFocusedElement];
        [self _elementDidBlur];
    }

    if (!shouldShowInputView || information.elementType == WebKit::InputType::None) {
        _page->setIsShowingInputViewForFocusedElement(false);
        return;
    }

    _page->setIsShowingInputViewForFocusedElement(true);

    // FIXME: We should remove this check when we manage to send ElementDidFocus from the WebProcess
    // only when it is truly time to show the keyboard.
    if (self._hasFocusedElement && _focusedElementInformation.elementContext.isSameElement(information.elementContext)) {
        if (_inputPeripheral) {
            if (!self.isFirstResponder)
                [self becomeFirstResponder];
            [self accessoryOpen];
        }
        return;
    }

    [_webView _resetFocusPreservationCount];

    _focusRequiresStrongPasswordAssistance = NO;
    _additionalContextForStrongPasswordAssistance = nil;

    _pendingFocusedElementIdentifier = information.identifier;

    if ([inputDelegate respondsToSelector:@selector(_webView:focusRequiresStrongPasswordAssistance:)]) {
        [self _continueElementDidFocus:information
            requiresStrongPasswordAssistance:[inputDelegate _webView:self.webView focusRequiresStrongPasswordAssistance:focusedElementInfo.get()]
            focusedElementInfo:focusedElementInfo
            activityStateChanges:activityStateChanges
            restoreValues:WTFMove(restoreValues)];
    } else if ([inputDelegate respondsToSelector:@selector(_webView:focusRequiresStrongPasswordAssistance:completionHandler:)]) {
        auto checker = WebKit::CompletionHandlerCallChecker::create(inputDelegate, @selector(_webView:focusRequiresStrongPasswordAssistance:completionHandler:));
        [inputDelegate _webView:self.webView focusRequiresStrongPasswordAssistance:focusedElementInfo.get() completionHandler:makeBlockPtr([
            weakSelf = RetainPtr { self },
            checker = WTFMove(checker),
            information,
            focusedElementInfo,
            activityStateChanges,
            restoreValues = WTFMove(restoreValues)
        ] (BOOL result) mutable {
            if (checker->completionHandlerHasBeenCalled())
                return;
            checker->didCallCompletionHandler();
            RetainPtr strongSelf = weakSelf.get();
            if (!strongSelf)
                return;
            [strongSelf _continueElementDidFocus:information
                requiresStrongPasswordAssistance:result
                focusedElementInfo:focusedElementInfo
                activityStateChanges:activityStateChanges
                restoreValues:WTFMove(restoreValues)];
        }).get()];
    } else {
        [self _continueElementDidFocus:information
            requiresStrongPasswordAssistance:NO
            focusedElementInfo:focusedElementInfo
            activityStateChanges:activityStateChanges
            restoreValues:WTFMove(restoreValues)];
    }
}

- (void)_continueElementDidFocus:(const WebKit::FocusedElementInformation&)information requiresStrongPasswordAssistance:(BOOL)requiresStrongPasswordAssistance focusedElementInfo:(RetainPtr<WKFocusedElementInfo>)focusedElementInfo activityStateChanges:(OptionSet<WebCore::ActivityState>)activityStateChanges restoreValues:(CompletionHandlerCallingScope&&)restoreValues
{
    if (_pendingFocusedElementIdentifier != information.identifier)
        return;

    _focusRequiresStrongPasswordAssistance = requiresStrongPasswordAssistance;

    id<_WKInputDelegate> inputDelegate = [_webView _inputDelegate];

    if ([inputDelegate respondsToSelector:@selector(_webViewAdditionalContextForStrongPasswordAssistance:)])
        _additionalContextForStrongPasswordAssistance = [inputDelegate _webViewAdditionalContextForStrongPasswordAssistance:self.webView];
    else
        _additionalContextForStrongPasswordAssistance = @{ };

    bool delegateImplementsWillStartInputSession = [inputDelegate respondsToSelector:@selector(_webView:willStartInputSession:)];
    bool delegateImplementsDidStartInputSession = [inputDelegate respondsToSelector:@selector(_webView:didStartInputSession:)];

    if (delegateImplementsWillStartInputSession || delegateImplementsDidStartInputSession) {
        [_formInputSession invalidate];
        _formInputSession = adoptNS([[WKFormInputSession alloc] initWithContentView:self focusedElementInfo:focusedElementInfo.get() requiresStrongPasswordAssistance:_focusRequiresStrongPasswordAssistance]);
    }

    if (delegateImplementsWillStartInputSession)
        [inputDelegate _webView:self.webView willStartInputSession:_formInputSession.get()];

    BOOL requiresKeyboard = mayContainSelectableText(information.elementType);
    BOOL editableChanged = [self setIsEditable:requiresKeyboard];
    _focusedElementInformation = information;
    _legacyTextInputTraits = nil;
    _extendedTextInputTraits = nil;

    if (![self isFirstResponder])
        [self becomeFirstResponder];

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)
    if (_focusedElementInformation.shouldHideSoftTopScrollEdgeEffect && ![[_webView _wkScrollView] _usesHardTopScrollEdgeEffect])
        [_webView _addReasonToHideTopScrollPocket:WebKit::HideScrollPocketReason::SiteSpecificQuirk];
    else
        [_webView _removeReasonToHideTopScrollPocket:WebKit::HideScrollPocketReason::SiteSpecificQuirk];
#endif

    if (!_suppressSelectionAssistantReasons && requiresKeyboard && activityStateChanges.contains(WebCore::ActivityState::IsFocused)) {
        _treatAsContentEditableUntilNextEditorStateUpdate = YES;
        [_textInteractionWrapper activateSelection];
        _page->restoreSelectionInFocusedEditableElement();
        _page->scheduleFullEditorStateUpdate();
    }

    _inputPeripheral = createInputPeripheralWithView(_focusedElementInformation.elementType, self);
    _waitingForKeyboardAppearanceAnimationToStart = requiresKeyboard && !_isChangingFocus;

#if HAVE(PEPPER_UI_CORE)
    [self addFocusedFormControlOverlay];
    if (!_isChangingFocus)
        [self presentViewControllerForCurrentFocusedElement];
#else
    if (requiresKeyboard) {
        [self _showKeyboard];
        if (!self.window.keyWindow)
            [self.window makeKeyWindow];
    } else
        [self reloadInputViews];
#endif

    if (!UIKeyboard.activeKeyboard) {
        // The lack of keyboard here suggests that we're not running in a context where the keyboard can become visible
        // (e.g. when running API tests outside of the context of a UI application). In this scenario, don't bother waiting
        // for keyboard appearance notifications.
        _waitingForKeyboardAppearanceAnimationToStart = NO;
    }

    // The custom fixed position rect behavior is affected by -isFocusingElement, so if that changes we need to recompute rects.
    if (editableChanged)
        [_webView _scheduleVisibleContentRectUpdate];

    // For elements that have selectable content (e.g. text field) we need to wait for the web process to send an up-to-date
    // selection rect before we can zoom and reveal the selection. Non-selectable elements (e.g. <select>) can be zoomed
    // immediately because they have no selection to reveal.
    if (requiresKeyboard) {
        bool ignorePreviousKeyboardWillShowNotification = [] {
            // In the case where out-of-process keyboard is enabled and the software keyboard is shown,
            // we end up getting two sets of "KeyboardWillShow" -> "KeyboardDidShow" notifications when
            // the keyboard animates up, after reloading input views. When the first set of notifications
            // is dispatched underneath the call to -reloadInputViews above, the keyboard hasn't yet
            // become full height, so attempts to reveal the focused element using the current height will
            // fail. The second set of keyboard notifications contains the final keyboard height, and is the
            // one we should use for revealing the focused element.
            // See also: <rdar://111704216>.
            if (!UIKeyboard.usesInputSystemUI)
                return false;

            auto keyboard = UIKeyboard.activeKeyboard;
            return keyboard && !keyboard.isMinimized;
        }();
        _revealFocusedElementDeferrer = WebKit::RevealFocusedElementDeferrer::create(self, [&] {
            OptionSet reasons { WebKit::RevealFocusedElementDeferralReason::EditorState };
            if (!self._scroller.firstResponderKeyboardAvoidanceEnabled)
                reasons.add(WebKit::RevealFocusedElementDeferralReason::KeyboardDidShow);
            else if (_waitingForKeyboardAppearanceAnimationToStart || ignorePreviousKeyboardWillShowNotification)
                reasons.add(WebKit::RevealFocusedElementDeferralReason::KeyboardWillShow);
            return reasons;
        }());
        _page->setWaitingForPostLayoutEditorStateUpdateAfterFocusingElement(true);
    } else
        [self _zoomToRevealFocusedElement];

    [self _updateAccessory];

#if HAVE(PEPPER_UI_CORE)
    if (_isChangingFocus)
        [_focusedFormControlView reloadData:YES];
#endif

    // _inputPeripheral has been initialized in inputView called by reloadInputViews.
    [_inputPeripheral beginEditing];

    if (delegateImplementsDidStartInputSession)
        [inputDelegate _webView:self.webView didStartInputSession:_formInputSession.get()];
    
    [_webView didStartFormControlInteraction];
}

- (void)_elementDidBlur
{
    SetForScope isBlurringFocusedElementForScope { _isBlurringFocusedElement, YES };

    [_webView _resetFocusPreservationCount];

    [self _endEditing];

#if ENABLE(CONTENT_INSET_BACKGROUND_FILL)
    if (!_isChangingFocus)
        [_webView _removeReasonToHideTopScrollPocket:WebKit::HideScrollPocketReason::SiteSpecificQuirk];
#endif

    [_formInputSession invalidate];
    _formInputSession = nil;

    _dataListTextSuggestionsInputView = nil;
    _dataListTextSuggestions = nil;

    _pendingFocusedElementIdentifier = { };

    BOOL editableChanged = [self setIsEditable:NO];
    // FIXME: We should completely invalidate _focusedElementInformation here, instead of a subset of individual members.
    _focusedElementInformation.elementType = WebKit::InputType::None;
    _focusedElementInformation.shouldSynthesizeKeyEventsForEditing = false;
    _focusedElementInformation.shouldAvoidResizingWhenInputViewBoundsChange = false;
    _focusedElementInformation.shouldAvoidScrollingWhenFocusedContentIsVisible = false;
    _focusedElementInformation.shouldHideSoftTopScrollEdgeEffect = false;
    _focusedElementInformation.shouldUseLegacySelectPopoverDismissalBehaviorInDataActivation = false;
    _focusedElementInformation.isFocusingWithValidationMessage = false;
    _focusedElementInformation.preventScroll = false;
    _inputPeripheral = nil;
    _focusRequiresStrongPasswordAssistance = NO;
    _autocorrectionContextNeedsUpdate = YES;
    _additionalContextForStrongPasswordAssistance = nil;
    _waitingForKeyboardAppearanceAnimationToStart = NO;
    _revealFocusedElementDeferrer = nullptr;

    // When defocusing an editable element reset a seen keydown before calling -_hideKeyboard so that we
    // re-evaluate whether we still need a keyboard when UIKit calls us back in -_requiresKeyboardWhenFirstResponder.
    if (editableChanged)
        _seenHardwareKeyDownInNonEditableElement = NO;

    [self _hideKeyboard];

#if HAVE(PEPPER_UI_CORE)
    [self dismissAllInputViewControllers:YES];
    if (!_isChangingFocus)
        [self removeFocusedFormControlOverlay];
#endif

    if (editableChanged) {
        // The custom fixed position rect behavior is affected by -isFocusingElement, so if that changes we need to recompute rects.
        [_webView _scheduleVisibleContentRectUpdate];

        [_webView didEndFormControlInteraction];
        _page->setIsShowingInputViewForFocusedElement(false);
    }

    _page->setWaitingForPostLayoutEditorStateUpdateAfterFocusingElement(false);

    if (!_isChangingFocus)
        _didAccessoryTabInitiateFocus = NO;

    _lastInsertedCharacterToOverrideCharacterBeforeSelection = std::nullopt;
}

- (void)_updateInputContextAfterBlurringAndRefocusingElement
{
    if (!self._hasFocusedElement || !_suppressSelectionAssistantReasons)
        return;

    [self _internalInvalidateTextEntryContext];
}

- (void)_didProgrammaticallyClearFocusedElement:(WebCore::ElementContext&&)context
{
    if (!self._hasFocusedElement)
        return;

    if (!context.isSameElement(_focusedElementInformation.elementContext))
        return;

    [self _internalInvalidateTextEntryContext];
}

- (void)_internalInvalidateTextEntryContext
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        [_asyncInputDelegate invalidateTextEntryContextForTextInput:self.asBETextInput];
        return;
    }
#endif

    [UIKeyboardImpl.activeInstance updateForChangedSelection];
}

- (void)_updateFocusedElementInformation:(const WebKit::FocusedElementInformation&)information
{
    if (!self._hasFocusedElement)
        return;

    if (!_focusedElementInformation.elementContext.isSameElement(information.elementContext))
        return;

    _focusedElementInformation = information;
    [_inputPeripheral updateEditing];
}

- (BOOL)shouldIgnoreKeyboardWillHideNotification
{
    // Ignore keyboard will hide notifications sent during rotation. They're just there for
    // backwards compatibility reasons and processing the will hide notification would
    // temporarily screw up the unobscured view area.
    if (UIPeripheralHost.sharedInstance.rotationState)
        return YES;

    if (_isChangingFocus && _isFocusingElementWithKeyboard)
        return YES;

    return NO;
}

- (void)_hardwareKeyboardAvailabilityChanged
{
    _seenHardwareKeyDownInNonEditableElement = NO;
    [self reloadInputViews];
}

- (void)_didUpdateInputMode:(WebCore::InputMode)mode
{
    if (!self.inputDelegate || !self._hasFocusedElement)
        return;

#if !PLATFORM(WATCHOS)
    _focusedElementInformation.inputMode = mode;
    [self reloadInputViews];
#endif
}

static BOOL allPasteboardItemOriginsMatchOrigin(UIPasteboard *pasteboard, const String& originIdentifier)
{
    if (originIdentifier.isEmpty())
        return NO;

    auto *indices = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, [pasteboard numberOfItems])];
    auto *allCustomData = [pasteboard dataForPasteboardType:@(WebCore::PasteboardCustomData::cocoaType().characters()) inItemSet:indices];
    if (!allCustomData.count)
        return NO;

    BOOL foundAtLeastOneMatchingIdentifier = NO;
    for (NSData *data in allCustomData) {
        if (!data.length)
            continue;

        auto buffer = WebCore::SharedBuffer::create(data);
        if (WebCore::PasteboardCustomData::fromSharedBuffer(buffer.get()).origin() != originIdentifier)
            return NO;

        foundAtLeastOneMatchingIdentifier = YES;
    }

    return foundAtLeastOneMatchingIdentifier;
}

- (void)_requestDOMPasteAccessForCategory:(WebCore::DOMPasteAccessCategory)pasteAccessCategory requiresInteraction:(WebCore::DOMPasteRequiresInteraction)requiresInteraction elementRect:(const WebCore::IntRect&)elementRect originIdentifier:(const String&)originIdentifier completionHandler:(CompletionHandler<void(WebCore::DOMPasteAccessResponse)>&&)completionHandler
{
    if (auto existingCompletionHandler = std::exchange(_domPasteRequestHandler, WTFMove(completionHandler))) {
        ASSERT_NOT_REACHED();
        existingCompletionHandler(WebCore::DOMPasteAccessResponse::DeniedForGesture);
    }

    _domPasteRequestCategory = pasteAccessCategory;

    if (requiresInteraction == WebCore::DOMPasteRequiresInteraction::No && allPasteboardItemOriginsMatchOrigin(pasteboardForAccessCategory(pasteAccessCategory), originIdentifier)) {
        [self _handleDOMPasteRequestWithResult:WebCore::DOMPasteAccessResponse::GrantedForCommand];
        return;
    }

    WebCore::IntRect menuControllerRect = elementRect;

    const CGFloat maximumElementWidth = 300;
    const CGFloat maximumElementHeight = 120;
    if (elementRect.isEmpty() || elementRect.width() > maximumElementWidth || elementRect.height() > maximumElementHeight) {
        const CGFloat interactionLocationMargin = 10;
        menuControllerRect = { WebCore::IntPoint(_lastInteractionLocation), { } };
        menuControllerRect.inflate(interactionLocationMargin);
    }
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    [UIMenuController.sharedMenuController showMenuFromView:self rect:menuControllerRect];
ALLOW_DEPRECATED_DECLARATIONS_END
}

- (void)doAfterEditorStateUpdateAfterFocusingElement:(dispatch_block_t)block
{
    if (!_page->waitingForPostLayoutEditorStateUpdateAfterFocusingElement()) {
        block();
        return;
    }

    _actionsToPerformAfterEditorStateUpdate.append(makeBlockPtr(block));
}

- (void)_reconcileEnclosingScrollViewContentOffset:(WebKit::EditorState&)state
{
    if (!state.hasVisualData())
        return;

    auto scrollingNodeID = state.visualData->enclosingScrollingNodeID;
    RetainPtr scroller = [self _scrollViewForScrollingNodeID:scrollingNodeID];
    if (!scroller)
        return;

    if ([_webView _isInStableState:scroller.get()])
        return;

    auto possiblyStaleScrollOffset = state.visualData->enclosingScrollOffset;
    auto scrollDelta = possiblyStaleScrollOffset - WebCore::roundedIntPoint([scroller contentOffset]);

    static constexpr auto adjustmentThreshold = 1;
    if (std::abs(scrollDelta.width()) <= adjustmentThreshold && std::abs(scrollDelta.height()) <= adjustmentThreshold)
        return;

    CGSize contentDelta = [scroller convertRect:CGRectMake(0, 0, scrollDelta.width(), scrollDelta.height()) toView:self].size;
    state.move(std::copysign(contentDelta.width, scrollDelta.width()), std::copysign(contentDelta.height, scrollDelta.height()));
}

- (void)_didUpdateEditorState
{
    [self _updateInitialWritingDirectionIfNecessary];

    // FIXME: If the initial writing direction just changed, we should wait until we get the next post-layout editor state
    // before zooming to reveal the selection rect.
    if (_revealFocusedElementDeferrer)
        _revealFocusedElementDeferrer->fulfill(WebKit::RevealFocusedElementDeferralReason::EditorState);

    _treatAsContentEditableUntilNextEditorStateUpdate = NO;

    for (auto block : std::exchange(_actionsToPerformAfterEditorStateUpdate, { }))
        block();
}

- (void)_updateInitialWritingDirectionIfNecessary
{
    if (!_page->isEditable())
        return;

    auto& editorState = _page->editorState();
    if (editorState.selectionIsNone || editorState.selectionIsRange)
        return;

    UIKeyboardImpl *keyboard = UIKeyboardImpl.activeInstance;
    if (keyboard.delegate != self)
        return;

    // Synchronize the keyboard's writing direction with the newly received EditorState.
    [keyboard setInitialDirection];
}

- (void)updateCurrentFocusedElementInformation:(Function<void(bool didUpdate)>&&)callback
{
    WeakObjCPtr<WKContentView> weakSelf { self };
    auto identifierBeforeUpdate = _focusedElementInformation.identifier;
    _page->requestFocusedElementInformation([callback = WTFMove(callback), identifierBeforeUpdate, weakSelf] (auto& info) {
        if (!weakSelf || !info || info->identifier != identifierBeforeUpdate) {
            // If the focused element may have changed in the meantime, don't overwrite focused element information.
            callback(false);
            return;
        }

        weakSelf.get()->_focusedElementInformation = info.value();
        callback(true);
    });
}

- (void)reloadContextViewForPresentedListViewController
{
#if HAVE(PEPPER_UI_CORE)
    if ([_presentedFullScreenInputViewController isKindOfClass:[WKTextInputListViewController class]])
        [(WKTextInputListViewController *)_presentedFullScreenInputViewController.get() reloadContextView];
#endif
}

#if HAVE(PEPPER_UI_CORE)

- (void)addFocusedFormControlOverlay
{
    if (_focusedFormControlView)
        return;

    _activeFocusedStateRetainBlock = makeBlockPtr(self.webView._retainActiveFocusedState);

    _focusedFormControlView = adoptNS([[WKFocusedFormControlView alloc] initWithFrame:self.webView.bounds delegate:self]);
    [_focusedFormControlView hide:NO];
    [_webView addSubview:_focusedFormControlView.get()];
    [self setInputDelegate:static_cast<id<UITextInputDelegate>>(_focusedFormControlView.get())];
}

- (void)removeFocusedFormControlOverlay
{
    if (!_focusedFormControlView)
        return;

    if (auto releaseActiveFocusState = WTFMove(_activeFocusedStateRetainBlock))
        releaseActiveFocusState();

    [_focusedFormControlView removeFromSuperview];
    _focusedFormControlView = nil;
    [self setInputDelegate:nil];
}

- (RetainPtr<PUICTextInputContext>)createQuickboardTextInputContext
{
    auto context = adoptNS([[PUICTextInputContext alloc] init]);
    [self _updateTextInputTraits:context.get()];
    [context setInitialText:_focusedElementInformation.value.createNSString().get()];
#if HAVE(QUICKBOARD_CONTROLLER)
    [context setAcceptsEmoji:YES];
    [context setShouldPresentModernTextInputUI:YES];
    [context setPlaceholder:self.inputLabelText];
#endif
    return context;
}

#if HAVE(QUICKBOARD_CONTROLLER)

- (RetainPtr<PUICQuickboardController>)_createQuickboardController:(UIViewController *)presentingViewController
{
    auto quickboardController = adoptNS([[PUICQuickboardController alloc] init]);
    auto context = self.createQuickboardTextInputContext;
    [quickboardController setQuickboardPresentingViewController:presentingViewController];
    [quickboardController setExcludedFromScreenCapture:[context isSecureTextEntry]];
    [quickboardController setTextInputContext:context.get()];
    [quickboardController setDelegate:self];

    return quickboardController;
}

static bool canUseQuickboardControllerFor(UITextContentType type)
{
    return [type isEqualToString:UITextContentTypePassword];
}

#endif // HAVE(QUICKBOARD_CONTROLLER)

- (void)presentViewControllerForCurrentFocusedElement
{
    [self dismissAllInputViewControllers:NO];

    _shouldRestoreFirstResponderStatusAfterLosingFocus = self.isFirstResponder;
    auto presentingViewController = self._wk_viewControllerForFullScreenPresentation;

    ASSERT(!_presentedFullScreenInputViewController);

    BOOL prefersModalPresentation = NO;

    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::Select:
        _presentedFullScreenInputViewController = adoptNS([[WKSelectMenuListViewController alloc] initWithDelegate:self]);
        break;
    case WebKit::InputType::Time:
        // Time inputs are special, in that the only UI affordances for dismissal are push buttons rather than status bar chevrons.
        // As such, modal presentation and dismissal is preferred even if a navigation stack exists.
        prefersModalPresentation = YES;
        _presentedFullScreenInputViewController = adoptNS([[WKTimePickerViewController alloc] initWithDelegate:self]);
        break;
    case WebKit::InputType::Date:
        _presentedFullScreenInputViewController = adoptNS([[WKDatePickerViewController alloc] initWithDelegate:self]);
        break;
    case WebKit::InputType::None:
        break;
    default: {
#if HAVE(QUICKBOARD_CONTROLLER)
        if (canUseQuickboardControllerFor(self.textContentTypeForQuickboard)) {
            _presentedQuickboardController = [self _createQuickboardController:presentingViewController];
            break;
        }
#endif
        _presentedFullScreenInputViewController = adoptNS([[WKTextInputListViewController alloc] initWithDelegate:self]);
        break;
    }
    }

#if HAVE(QUICKBOARD_CONTROLLER)
    ASSERT_IMPLIES(_presentedQuickboardController, !_presentedFullScreenInputViewController);
    ASSERT_IMPLIES(_presentedFullScreenInputViewController, !_presentedQuickboardController);
#endif // HAVE(QUICKBOARD_CONTROLLER)
    ASSERT(self._isPresentingFullScreenInputView);
    ASSERT(presentingViewController);

    if (!prefersModalPresentation && [presentingViewController isKindOfClass:[UINavigationController class]])
        _inputNavigationViewControllerForFullScreenInputs = (UINavigationController *)presentingViewController;
    else
        _inputNavigationViewControllerForFullScreenInputs = nil;

    RetainPtr<UIViewController> controller;
    if (_presentedFullScreenInputViewController) {
        // Present the input view controller on an existing navigation stack, if possible. If there is no navigation stack we can use, fall back to presenting modally.
        // This is because the HI specification (for certain scenarios) calls for navigation-style view controller presentation, but WKWebView can't make any guarantees
        // about clients' view controller hierarchies, so we can only try our best to avoid presenting modally. Clients can implicitly opt in to specced behavior by using
        // UINavigationController to present the web view.
        if (_inputNavigationViewControllerForFullScreenInputs)
            [_inputNavigationViewControllerForFullScreenInputs pushViewController:_presentedFullScreenInputViewController.get() animated:YES];
        else
            [presentingViewController presentViewController:_presentedFullScreenInputViewController.get() animated:YES completion:nil];
        controller = _presentedFullScreenInputViewController.get();
    }

#if HAVE(QUICKBOARD_CONTROLLER)
    if (_presentedQuickboardController) {
        [_presentedQuickboardController present];
        controller = [presentingViewController presentedViewController];
    }
#endif // HAVE(QUICKBOARD_CONTROLLER)

    // Presenting a fullscreen input view controller fully obscures the web view. Without taking this token, the web content process will get backgrounded.
    _page->legacyMainFrameProcess().startBackgroundActivityForFullscreenInput();

    // FIXME: PUICQuickboardController does not present its view controller immediately, since it asynchronously
    // establishes a connection to QuickboardViewService before presenting the remote view controller.
    // Fixing this requires a version of `-[PUICQuickboardController present]` that takes a completion handler.
    [presentingViewController.transitionCoordinator animateAlongsideTransition:nil completion:[weakWebView = WeakObjCPtr<WKWebView>(_webView), controller] (id <UIViewControllerTransitionCoordinatorContext>) {
        auto strongWebView = weakWebView.get();
        id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([strongWebView UIDelegate]);
        if ([uiDelegate respondsToSelector:@selector(_webView:didPresentFocusedElementViewController:)])
            [uiDelegate _webView:strongWebView.get() didPresentFocusedElementViewController:controller.get()];
    }];
}

- (BOOL)_isPresentingFullScreenInputView
{
#if HAVE(QUICKBOARD_CONTROLLER)
    if (_presentedQuickboardController)
        return YES;
#endif // HAVE(QUICKBOARD_CONTROLLER)
    return !!_presentedFullScreenInputViewController;
}

- (void)dismissAllInputViewControllers:(BOOL)animated
{
    auto navigationController = std::exchange(_inputNavigationViewControllerForFullScreenInputs, nil);

    if (!self._isPresentingFullScreenInputView) {
        ASSERT(!navigationController);
        return;
    }

    if (auto controller = std::exchange(_presentedFullScreenInputViewController, nil)) {
        auto dispatchDidDismiss = makeBlockPtr([weakWebView = WeakObjCPtr<WKWebView>(_webView), controller] {
            auto strongWebView = weakWebView.get();
            id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([strongWebView UIDelegate]);
            if ([uiDelegate respondsToSelector:@selector(_webView:didDismissFocusedElementViewController:)])
                [uiDelegate _webView:strongWebView.get() didDismissFocusedElementViewController:controller.get()];
        });

        if ([navigationController viewControllers].lastObject == controller.get()) {
            [navigationController popViewControllerAnimated:animated];
            [[controller transitionCoordinator] animateAlongsideTransition:nil completion:[dispatchDidDismiss = WTFMove(dispatchDidDismiss)] (id <UIViewControllerTransitionCoordinatorContext>) {
                dispatchDidDismiss();
            }];
        } else if (auto presentedViewController = retainPtr([controller presentedViewController])) {
            [presentedViewController dismissViewControllerAnimated:animated completion:[controller, animated, dispatchDidDismiss = WTFMove(dispatchDidDismiss)] {
                [controller dismissViewControllerAnimated:animated completion:dispatchDidDismiss.get()];
            }];
        } else
            [controller dismissViewControllerAnimated:animated completion:dispatchDidDismiss.get()];
    }

#if HAVE(QUICKBOARD_CONTROLLER)
    if (auto controller = std::exchange(_presentedQuickboardController, nil)) {
        auto presentedViewController = retainPtr([controller quickboardPresentingViewController].presentedViewController);
        [controller dismissWithCompletion:[weakWebView = WeakObjCPtr<WKWebView>(_webView), presentedViewController] {
            auto strongWebView = weakWebView.get();
            id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([strongWebView UIDelegate]);
            if ([uiDelegate respondsToSelector:@selector(_webView:didDismissFocusedElementViewController:)])
                [uiDelegate _webView:strongWebView.get() didDismissFocusedElementViewController:presentedViewController.get()];
        }];
    }
#endif // HAVE(QUICKBOARD_CONTROLLER)

    if (_shouldRestoreFirstResponderStatusAfterLosingFocus) {
        _shouldRestoreFirstResponderStatusAfterLosingFocus = NO;
        if (!self.isFirstResponder)
            [self becomeFirstResponder];
    }

    _page->legacyMainFrameProcess().endBackgroundActivityForFullscreenInput();
}

- (void)focusedFormControlViewDidSubmit:(WKFocusedFormControlView *)view
{
    [self insertText:@"\n"];
    _page->blurFocusedElement();
}

- (void)focusedFormControlViewDidCancel:(WKFocusedFormControlView *)view
{
    _page->blurFocusedElement();
}

- (void)focusedFormControlViewDidBeginEditing:(WKFocusedFormControlView *)view
{
    [self updateCurrentFocusedElementInformation:[weakSelf = WeakObjCPtr<WKContentView>(self)] (bool didUpdate) {
        if (!didUpdate)
            return;

        auto strongSelf = weakSelf.get();
        [strongSelf presentViewControllerForCurrentFocusedElement];
        [strongSelf->_focusedFormControlView hide:YES];
    }];
}

- (CGRect)rectForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    return [self convertRect:_focusedElementInformation.interactionRect toView:view];
}

- (CGRect)nextRectForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    if (!_focusedElementInformation.hasNextNode)
        return CGRectNull;

    return [self convertRect:_focusedElementInformation.nextNodeRect toView:view];
}

- (CGRect)previousRectForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    if (!_focusedElementInformation.hasPreviousNode)
        return CGRectNull;

    return [self convertRect:_focusedElementInformation.previousNodeRect toView:view];
}

- (UIScrollView *)scrollViewForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    return self._scroller;
}

- (NSString *)actionNameForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    if (_focusedElementInformation.formAction.isEmpty())
        return nil;

    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::Select:
    case WebKit::InputType::Time:
    case WebKit::InputType::Date:
    case WebKit::InputType::Color:
        return nil;
    case WebKit::InputType::Search:
        return WebCore::formControlSearchButtonTitle().createNSString().autorelease();
    default:
        return WebCore::formControlGoButtonTitle().createNSString().autorelease();
    }
}

- (void)focusedFormControlViewDidRequestNextNode:(WKFocusedFormControlView *)view
{
    if (_focusedElementInformation.hasNextNode)
        _page->focusNextFocusedElement(true, [] { });
}

- (void)focusedFormControlViewDidRequestPreviousNode:(WKFocusedFormControlView *)view
{
    if (_focusedElementInformation.hasPreviousNode)
        _page->focusNextFocusedElement(false, [] { });
}

- (BOOL)hasNextNodeForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    return _focusedElementInformation.hasNextNode;
}

- (BOOL)hasPreviousNodeForFocusedFormControlView:(WKFocusedFormControlView *)view
{
    return _focusedElementInformation.hasPreviousNode;
}

- (void)focusedFormControllerDidUpdateSuggestions:(WKFocusedFormControlView *)view
{
    if (_isBlurringFocusedElement || ![_presentedFullScreenInputViewController isKindOfClass:[WKTextInputListViewController class]])
        return;

    [(WKTextInputListViewController *)_presentedFullScreenInputViewController updateTextSuggestions:[_focusedFormControlView suggestions]];
}

#pragma mark - WKSelectMenuListViewControllerDelegate

- (void)selectMenu:(WKSelectMenuListViewController *)selectMenu didSelectItemAtIndex:(NSUInteger)index
{
    ASSERT(!_focusedElementInformation.isMultiSelect);
    [self updateFocusedElementSelectedIndex:index allowsMultipleSelection:false];
}

- (NSUInteger)numberOfItemsInSelectMenu:(WKSelectMenuListViewController *)selectMenu
{
    return self.focusedSelectElementOptions.size();
}

- (NSString *)selectMenu:(WKSelectMenuListViewController *)selectMenu displayTextForItemAtIndex:(NSUInteger)index
{
    auto& options = self.focusedSelectElementOptions;
    if (index >= options.size()) {
        ASSERT_NOT_REACHED();
        return @"";
    }

    return options[index].text.createNSString().autorelease();
}

- (void)selectMenu:(WKSelectMenuListViewController *)selectMenu didCheckItemAtIndex:(NSUInteger)index checked:(BOOL)checked
{
    ASSERT(_focusedElementInformation.isMultiSelect);
    if (index >= self.focusedSelectElementOptions.size()) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto& option = self.focusedSelectElementOptions[index];
    if (option.isSelected == checked) {
        ASSERT_NOT_REACHED();
        return;
    }

    [self updateFocusedElementSelectedIndex:index allowsMultipleSelection:true];
    option.isSelected = checked;
}

- (BOOL)selectMenuUsesMultipleSelection:(WKSelectMenuListViewController *)selectMenu
{
    return _focusedElementInformation.isMultiSelect;
}

- (BOOL)selectMenu:(WKSelectMenuListViewController *)selectMenu hasSelectedOptionAtIndex:(NSUInteger)index
{
    if (index >= self.focusedSelectElementOptions.size()) {
        ASSERT_NOT_REACHED();
        return NO;
    }

    return self.focusedSelectElementOptions[index].isSelected;
}

#if HAVE(QUICKBOARD_CONTROLLER)

#pragma mark - PUICQuickboardControllerDelegate

- (void)quickboardController:(PUICQuickboardController *)controller textInputValueDidChange:(NSAttributedString *)attributedText
{
    _page->setTextAsync(attributedText.string);
    [self dismissQuickboardViewControllerAndRevealFocusedFormOverlayIfNecessary:controller];
}

- (void)quickboardControllerTextInputValueCancelled:(PUICQuickboardController *)controller
{
    [self dismissQuickboardViewControllerAndRevealFocusedFormOverlayIfNecessary:controller];
}

#endif // HAVE(QUICKBOARD_CONTROLLER)

#endif // HAVE(PEPPER_UI_CORE)

- (void)_wheelChangedWithEvent:(UIEvent *)event
{
#if HAVE(PEPPER_UI_CORE)
    if ([_focusedFormControlView handleWheelEvent:event])
        return;
#endif
    [super _wheelChangedWithEvent:event];
}

- (void)_updateSelectionAssistantSuppressionState
{
    static const double minimumFocusedElementAreaForSuppressingSelectionAssistant = 4;

    auto& editorState = _page->editorState();
    if (!editorState.hasPostLayoutAndVisualData())
        return;

    BOOL selectionIsTransparentOrFullyClipped = NO;
    BOOL focusedElementIsTooSmall = NO;
    if (!editorState.selectionIsNone) {
        auto& postLayoutData = *editorState.postLayoutData;
        auto& visualData = *editorState.visualData;
        if (postLayoutData.selectionIsTransparentOrFullyClipped)
            selectionIsTransparentOrFullyClipped = YES;

        if (self._hasFocusedElement) {
            auto elementArea = visualData.editableRootBounds.area<RecordOverflow>();
            if (!elementArea.hasOverflowed() && elementArea < minimumFocusedElementAreaForSuppressingSelectionAssistant)
                focusedElementIsTooSmall = YES;
        }
    }

    if (selectionIsTransparentOrFullyClipped)
        [self _startSuppressingSelectionAssistantForReason:WebKit::SelectionIsTransparentOrFullyClipped];
    else
        [self _stopSuppressingSelectionAssistantForReason:WebKit::SelectionIsTransparentOrFullyClipped];

    if (focusedElementIsTooSmall)
        [self _startSuppressingSelectionAssistantForReason:WebKit::FocusedElementIsTooSmall];
    else
        [self _stopSuppressingSelectionAssistantForReason:WebKit::FocusedElementIsTooSmall];
}

- (void)_selectionChanged
{
    _autocorrectionContextNeedsUpdate = YES;

    [self _updateSelectionAssistantSuppressionState];

    _cachedSelectionContainerView = nil;
    _cachedSelectedTextRange = nil;
    _selectionNeedsUpdate = YES;
    // If we are changing the selection with a gesture there is no need
    // to wait to paint the selection.
    if (_usingGestureForSelection)
        [self _updateChangedSelection];

    if (_candidateViewNeedsUpdate) {
        _candidateViewNeedsUpdate = NO;
#if USE(BROWSERENGINEKIT)
        if (self.shouldUseAsyncInteractions)
            [_asyncInputDelegate invalidateTextEntryContextForTextInput:self.asBETextInput];
        else
#endif // USE(BROWSERENGINEKIT)
        {
            auto inputDelegate = self.inputDelegate;
            if ([inputDelegate respondsToSelector:@selector(layoutHasChanged)])
                [(id<UITextInputDelegatePrivate>)inputDelegate layoutHasChanged];
        }
    }
    
    [_webView _didChangeEditorState];

    if (_page->editorState().hasPostLayoutAndVisualData()) {
        _lastInsertedCharacterToOverrideCharacterBeforeSelection = std::nullopt;

        if (!_usingGestureForSelection && !_selectionChangeNestingLevel && _page->editorState().triggeredByAccessibilitySelectionChange) {
            // Force UIKit to reload all EditorState-based UI; in particular, this includes text candidates.
            [self _internalBeginSelectionChange];
            [self _internalEndSelectionChange];
        }
    }
}

- (void)selectWordForReplacement
{
    [self _internalBeginSelectionChange];
    _page->extendSelectionForReplacement([weakSelf = WeakObjCPtr<WKContentView>(self)] {
        if (auto strongSelf = weakSelf.get())
            [strongSelf _internalEndSelectionChange];
    });
}

#if ENABLE(PLATFORM_DRIVEN_TEXT_CHECKING)
- (void)replaceSelectionOffset:(NSInteger)selectionOffset length:(NSUInteger)length withAnnotatedString:(NSAttributedString *)annotatedString relativeReplacementRange:(NSRange)relativeReplacementRange
{
    _textCheckingController->replaceRelativeToSelection(annotatedString, selectionOffset, length, relativeReplacementRange.location, relativeReplacementRange.length);
}

- (void)removeAnnotation:(NSAttributedStringKey)annotationName forSelectionOffset:(NSInteger)selectionOffset length:(NSUInteger)length
{
    _textCheckingController->removeAnnotationRelativeToSelection(annotationName, selectionOffset, length);
}
#endif

- (void)_updateChangedSelection
{
    [self _updateChangedSelection:NO];
}

- (UIScrollView *)_scrollViewForScrollingNodeID:(std::optional<WebCore::ScrollingNodeID>)scrollingNodeID
{
    if (WeakPtr coordinator = downcast<WebKit::RemoteScrollingCoordinatorProxyIOS>(_page->scrollingCoordinatorProxy()))
        return coordinator->scrollViewForScrollingNodeID(scrollingNodeID);

    return nil;
}

- (void)_updateChangedSelection:(BOOL)force
{
    auto& editorState = _page->editorState();
    if (!editorState.hasPostLayoutAndVisualData())
        return;

    auto& postLayoutData = *editorState.postLayoutData;
    WebKit::WKSelectionDrawingInfo selectionDrawingInfo { editorState };
    if (force || selectionDrawingInfo != _lastSelectionDrawingInfo) {
        LOG_WITH_STREAM(Selection, stream << "_updateChangedSelection " << selectionDrawingInfo);

        _cachedSelectedTextRange = nil;
        _lastSelectionDrawingInfo = selectionDrawingInfo;

        if (_textInteractionWrapper) {
            _markedText = editorState.hasComposition ? postLayoutData.markedText.createNSString().get() : @"";
            if (![_markedText length])
                _isDeferringKeyEventsToInputMethod = NO;
            RetainPtr containerView = [self _selectionContainerViewInternal];
            [_textInteractionWrapper prepareToMoveSelectionContainer:containerView.get()];
            [_textInteractionWrapper selectionChanged];
            _lastSelectionChildScrollViewContentOffset = [containerView] -> std::optional<WebCore::IntPoint> {
                RetainPtr scrollView = [containerView _wk_parentScrollView];
                if (is_objc<WKChildScrollView>(scrollView.get()))
                    return WebCore::roundedIntPoint([scrollView contentOffset]);
                return { };
            }();
            _lastSiblingBeforeSelectionHighlight = [self _siblingBeforeSelectionHighlight];
        }

        _selectionNeedsUpdate = NO;
        if (_shouldRestoreSelection) {
            [_textInteractionWrapper didEndScrollingOverflow];
            _shouldRestoreSelection = NO;
        }
    } else {
        if (_lastSiblingBeforeSelectionHighlight != [self _siblingBeforeSelectionHighlight])
            [_textInteractionWrapper prepareToMoveSelectionContainer:self._selectionContainerViewInternal];
        [self _updateSelectionViewsInChildScrollViewIfNeeded];
    }

    if (postLayoutData.isStableStateUpdate && _needsDeferredEndScrollingSelectionUpdate && _page->inStableState()) {
        auto firstResponder = self.firstResponder;
        if ((!firstResponder || self == firstResponder) && !_suppressSelectionAssistantReasons)
            [_textInteractionWrapper activateSelection];

        [_textInteractionWrapper didEndScrollingOverflow];

        _needsDeferredEndScrollingSelectionUpdate = NO;
    }
}

- (UIView *)_siblingBeforeSelectionHighlight
{
    return [[_textInteractionWrapper selectionHighlightView] _wk_previousSibling];
}

- (void)_updateSelectionViewsInChildScrollViewIfNeeded
{
    if (_waitingForEditorStateAfterScrollingSelectionContainer)
        return;

    RetainPtr selectionContainer = [self _selectionContainerViewInternal];
    RetainPtr scroller = [selectionContainer _wk_parentScrollView];
    if (![_webView _isInStableState:scroller.get()])
        return;

    if (!is_objc<WKChildScrollView>(scroller.get()))
        return;

    auto contentOffset = WebCore::roundedIntPoint([scroller contentOffset]);
    if (_lastSelectionChildScrollViewContentOffset == contentOffset)
        return;

    _lastSelectionChildScrollViewContentOffset = contentOffset;
    [_textInteractionWrapper setNeedsSelectionUpdate];
}

- (BOOL)shouldAllowHidingSelectionCommands
{
    ASSERT(_ignoreSelectionCommandFadeCount >= 0);
    return !_ignoreSelectionCommandFadeCount;
}

- (BOOL)supportsTextSelectionWithCharacterGranularity
{
    return YES;
}

#if ENABLE(REVEAL)
- (void)requestRVItemInSelectedRangeWithCompletionHandler:(void(^)(RVItem *item))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    _page->requestRVItemInCurrentSelectedRange([completionHandler = makeBlockPtr(completionHandler), weakSelf = WeakObjCPtr<WKContentView>(self)](const WebKit::RevealItem& item) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return completionHandler(nil);

        completionHandler(item.item());
    });
}

- (void)prepareSelectionForContextMenuWithLocationInView:(CGPoint)locationInView completionHandler:(void(^)(BOOL shouldPresentMenu, RVItem *item))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self _internalSelectTextForContextMenuWithLocationInView:locationInView completionHandler:[completionHandler = makeBlockPtr(completionHandler)](BOOL shouldPresentMenu, const WebKit::RevealItem& item) {
        completionHandler(shouldPresentMenu, item.item());
    }];
}

- (void)_internalSelectTextForContextMenuWithLocationInView:(CGPoint)locationInView completionHandler:(void(^)(BOOL shouldPresentMenu, const WebKit::RevealItem& item))completionHandler
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    _removeBackgroundData = std::nullopt;
#endif

    _page->prepareSelectionForContextMenuWithLocationInView(WebCore::roundedIntPoint(locationInView), [weakSelf = WeakObjCPtr<WKContentView>(self), completionHandler = makeBlockPtr(completionHandler)](bool shouldPresentMenu, auto& item) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return completionHandler(false, { });

        if (shouldPresentMenu && ![strongSelf shouldSuppressEditMenu])
            [strongSelf->_textInteractionWrapper activateSelection];

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
        [strongSelf doAfterComputingImageAnalysisResultsForBackgroundRemoval:[completionHandler, shouldPresentMenu, item, weakSelf] {
            auto strongSelf = weakSelf.get();
            if (!strongSelf)
                return completionHandler(false, { });

            completionHandler(shouldPresentMenu, item);
        }];
#else
        completionHandler(shouldPresentMenu, item);
#endif
    });
}
#endif

- (BOOL)hasHiddenContentEditable
{
    return _suppressSelectionAssistantReasons.containsAny({ WebKit::SelectionIsTransparentOrFullyClipped, WebKit::FocusedElementIsTooSmall });
}

- (BOOL)_shouldSuppressSelectionCommands
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    return self.shouldSuppressEditMenu;
}

- (void)_startSuppressingSelectionAssistantForReason:(WebKit::SuppressSelectionAssistantReason)reason
{
    bool wasSuppressingSelectionAssistant = !!_suppressSelectionAssistantReasons;
    _suppressSelectionAssistantReasons.add(reason);

    if (!wasSuppressingSelectionAssistant)
        [_textInteractionWrapper deactivateSelection];
}

- (void)_stopSuppressingSelectionAssistantForReason:(WebKit::SuppressSelectionAssistantReason)reason
{
    bool wasSuppressingSelectionAssistant = !!_suppressSelectionAssistantReasons;
    _suppressSelectionAssistantReasons.remove(reason);

    if (wasSuppressingSelectionAssistant && !_suppressSelectionAssistantReasons)
        [_textInteractionWrapper activateSelection];
}

- (UIView <WKFormControl> *)dataListTextSuggestionsInputView
{
    return _dataListTextSuggestionsInputView.get();
}

- (NSArray<WKBETextSuggestion *> *)dataListTextSuggestions
{
    return _dataListTextSuggestions.get();
}

- (void)setDataListTextSuggestionsInputView:(UIView <WKFormControl> *)suggestionsInputView
{
    if (_dataListTextSuggestionsInputView == suggestionsInputView)
        return;

    _dataListTextSuggestionsInputView = suggestionsInputView;

    if (![_formInputSession customInputView])
        [self reloadInputViews];
}

- (void)setDataListTextSuggestions:(NSArray<WKBETextSuggestion *> *)textSuggestions
{
    if (textSuggestions == _dataListTextSuggestions || [textSuggestions isEqualToArray:_dataListTextSuggestions.get()])
        return;

    _dataListTextSuggestions = textSuggestions;

    if (![_formInputSession suggestions].count)
        [self updateTextSuggestionsForInputDelegate];
}

- (void)updateTextSuggestionsForInputDelegate
{
    // Text suggestions vended from clients take precedence over text suggestions from a focused form control with a datalist.
    NSArray<UITextSuggestion *> *formInputSessionSuggestions = [_formInputSession suggestions];
    if (formInputSessionSuggestions.count) {
        [self _provideUITextSuggestionsToInputDelegate:formInputSessionSuggestions];
        return;
    }

    if ([_dataListTextSuggestions count]) {
        [self _provideSuggestionsToInputDelegate:_dataListTextSuggestions.get()];
        return;
    }

    [self _provideSuggestionsToInputDelegate:nil];
}

- (void)_provideSuggestionsToInputDelegate:(NSArray<WKBETextSuggestion *> *)suggestions
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        [_asyncInputDelegate textInput:self.asBETextInput setCandidateSuggestions:suggestions];
        return;
    }

    RetainPtr uiSuggestions = [NSMutableArray arrayWithCapacity:suggestions.count];
    for (UITextSuggestion *suggestion in suggestions)
        [uiSuggestions addObject:[UITextSuggestion textSuggestionWithInputText:suggestion.inputText]];
#else
    RetainPtr uiSuggestions = suggestions;
#endif
    [(id<UITextInputSuggestionDelegate>)self.inputDelegate setSuggestions:uiSuggestions.get()];
}

- (void)_provideUITextSuggestionsToInputDelegate:(NSArray<UITextSuggestion *> *)suggestions
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions) {
        RetainPtr suggestionsForDelegate = [NSMutableArray arrayWithCapacity:suggestions.count];
        for (UITextSuggestion *suggestion in suggestions)
            [suggestionsForDelegate addObject:adoptNS([[WKBETextSuggestion alloc] _initWithUIKitTextSuggestion:suggestion]).get()];
        [_asyncInputDelegate textInput:self.asBETextInput setCandidateSuggestions:suggestionsForDelegate.get()];
        return;
    }
#endif // USE(BROWSERENGINEKIT)

    [(id<UITextInputSuggestionDelegate>)self.inputDelegate setSuggestions:suggestions];
}

- (void)_showPlaybackTargetPicker:(BOOL)hasVideo fromRect:(const WebCore::IntRect&)elementRect routeSharingPolicy:(WebCore::RouteSharingPolicy)routeSharingPolicy routingContextUID:(NSString *)routingContextUID
{
#if ENABLE(AIRPLAY_PICKER)
    if (!_airPlayRoutePicker)
        _airPlayRoutePicker = adoptNS([[WKAirPlayRoutePicker alloc] init]);
    [_airPlayRoutePicker showFromView:self routeSharingPolicy:routeSharingPolicy routingContextUID:routingContextUID hasVideo:hasVideo];
#endif
}

- (void)_showRunOpenPanel:(API::OpenPanelParameters*)parameters frameInfo:(const WebKit::FrameInfoData&)frameInfo resultListener:(WebKit::WebOpenPanelResultListenerProxy*)listener
{
    ASSERT(!_fileUploadPanel);
    if (_fileUploadPanel)
        return;

    _frameInfoForFileUploadPanel = frameInfo;
    _fileUploadPanel = adoptNS([[WKFileUploadPanel alloc] initWithView:self]);
    [_fileUploadPanel setDelegate:self];
    [_fileUploadPanel presentWithParameters:parameters resultListener:listener];
}

- (void)fileUploadPanelDidDismiss:(WKFileUploadPanel *)fileUploadPanel
{
    ASSERT(_fileUploadPanel.get() == fileUploadPanel);
    
    [_fileUploadPanel setDelegate:nil];
    _fileUploadPanel = nil;
}

- (BOOL)fileUploadPanelDestinationIsManaged:(WKFileUploadPanel *)fileUploadPanel
{
    ASSERT(_fileUploadPanel.get() == fileUploadPanel);

    auto webView = _webView.get();
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([webView UIDelegate]);
    return [uiDelegate respondsToSelector:@selector(_webView:fileUploadPanelContentIsManagedWithInitiatingFrame:)]
        && [uiDelegate _webView:webView.get() fileUploadPanelContentIsManagedWithInitiatingFrame:_frameInfoForFileUploadPanel ? wrapper(API::FrameInfo::create(*std::exchange(_frameInfoForFileUploadPanel, std::nullopt), _page.get())).get() : nil];
}

#if HAVE(PHOTOS_UI)
- (BOOL)fileUploadPanelPhotoPickerPrefersOriginalImageFormat:(WKFileUploadPanel *)fileUploadPanel
{
    ASSERT(_fileUploadPanel.get() == fileUploadPanel);
    return _page->preferences().photoPickerPrefersOriginalImageFormat();
}
#endif

- (void)_showShareSheet:(const WebCore::ShareDataWithParsedURL&)data inRect:(std::optional<WebCore::FloatRect>)rect completionHandler:(CompletionHandler<void(bool)>&&)completionHandler
{
#if HAVE(SHARE_SHEET_UI)
    if (_shareSheet)
        [_shareSheet dismissIfNeededWithReason:WebKit::PickerDismissalReason::ResetState];

    _shareSheet = adoptNS([[WKShareSheet alloc] initWithView:self.webView]);
    [_shareSheet setDelegate:self];

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    if (!rect) {
        if (auto lastLocation = [_mouseInteraction lastLocation]) {
            auto hoverLocationInWebView = [self convertPoint:*lastLocation toView:self.webView];
            rect = WebCore::FloatRect(hoverLocationInWebView.x, hoverLocationInWebView.y, 1, 1);
        }
    }
#endif
    
    [_shareSheet presentWithParameters:data inRect:rect completionHandler:WTFMove(completionHandler)];
#endif // HAVE(SHARE_SHEET_UI)
}

#if HAVE(SHARE_SHEET_UI)

- (void)shareSheetDidDismiss:(WKShareSheet *)shareSheet
{
    ASSERT(_shareSheet == shareSheet);

    [_shareSheet setDelegate:nil];
    _shareSheet = nil;
}

- (void)shareSheet:(WKShareSheet *)shareSheet willShowActivityItems:(NSArray *)activityItems
{
    ASSERT(_shareSheet == shareSheet);

    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    if ([uiDelegate respondsToSelector:@selector(_webView:willShareActivityItems:)])
        [uiDelegate _webView:self.webView willShareActivityItems:activityItems];
}

#endif // HAVE(SHARE_SHEET_UI)

#if HAVE(DIGITAL_CREDENTIALS_UI)
- (void)_showDigitalCredentialsPicker:(const WebCore::DigitalCredentialsRequestData&)requestData completionHandler:(WTF::CompletionHandler<void(Expected<WebCore::DigitalCredentialsResponseData, WebCore::ExceptionData>&&)>&&)completionHandler
{
    _digitalCredentialsPicker = adoptNS([[WKDigitalCredentialsPicker alloc] initWithView:self.webView page:_page.get()]);
    [_digitalCredentialsPicker presentWithRequestData:requestData completionHandler:WTFMove(completionHandler)];
}

- (void)_dismissDigitalCredentialsPicker:(WTF::CompletionHandler<void(bool)>&&)completionHandler
{
    if (!_digitalCredentialsPicker) {
        LOG(DigitalCredentials, "Digital credentials picker is not presented.");
        completionHandler(false);
        return;
    }
    [_digitalCredentialsPicker dismissWithCompletionHandler:WTFMove(completionHandler)];
}
#endif // HAVE(DIGITAL_CREDENTIALS_UI)

- (void)_showContactPicker:(const WebCore::ContactsRequestData&)requestData completionHandler:(WTF::CompletionHandler<void(std::optional<Vector<WebCore::ContactInfo>>&&)>&&)completionHandler
{
#if HAVE(CONTACTSUI)
    _contactPicker = adoptNS([[WKContactPicker alloc] initWithView:self.webView]);
    [_contactPicker setDelegate:self];
    [_contactPicker presentWithRequestData:requestData completionHandler:WTFMove(completionHandler)];
#else
    completionHandler(std::nullopt);
#endif
}

#if HAVE(CONTACTSUI)
- (void)contactPickerDidPresent:(WKContactPicker *)contactPicker
{
    ASSERT(_contactPicker == contactPicker);

    [_webView _didPresentContactPicker];
}

- (void)contactPickerDidDismiss:(WKContactPicker *)contactPicker
{
    ASSERT(_contactPicker == contactPicker);

    [_contactPicker setDelegate:nil];
    _contactPicker = nil;

    [_webView _didDismissContactPicker];
}
#endif

- (void)dismissPickersIfNeededWithReason:(WebKit::PickerDismissalReason)reason
{
    if ([_fileUploadPanel dismissIfNeededWithReason:reason])
        _fileUploadPanel = nil;

#if !PLATFORM(WATCHOS) && !PLATFORM(APPLETV)
    if ([_shareSheet dismissIfNeededWithReason:reason])
        _shareSheet = nil;
#endif

#if HAVE(CONTACTSUI)
    if ([_contactPicker dismissIfNeededWithReason:reason])
        _contactPicker = nil;
#endif
}

#if PLATFORM(WATCHOS)
static String fallbackLabelTextForUnlabeledInputFieldInZoomedFormControls(WebCore::InputMode inputMode, WebKit::InputType elementType)
{
    bool isPasswordField = false;

    auto elementTypeIsAnyOf = [elementType](std::initializer_list<WebKit::InputType>&& elementTypes) {
        return std::ranges::find(elementTypes, elementType) != elementTypes.end();
    };

    // If unspecified, try to infer the input mode from the input type
    if (inputMode == WebCore::InputMode::Unspecified) {
        if (elementTypeIsAnyOf({ WebKit::InputType::ContentEditable, WebKit::InputType::Text, WebKit::InputType::TextArea }))
            inputMode = WebCore::InputMode::Text;
        if (elementTypeIsAnyOf({ WebKit::InputType::URL }))
            inputMode = WebCore::InputMode::Url;
        if (elementTypeIsAnyOf({ WebKit::InputType::Number, WebKit::InputType::NumberPad }))
            inputMode = WebCore::InputMode::Numeric;
        if (elementTypeIsAnyOf({ WebKit::InputType::Search }))
            inputMode = WebCore::InputMode::Search;
        if (elementTypeIsAnyOf({ WebKit::InputType::Email }))
            inputMode = WebCore::InputMode::Email;
        if (elementTypeIsAnyOf({ WebKit::InputType::Phone }))
            inputMode = WebCore::InputMode::Telephone;
        if (elementTypeIsAnyOf({ WebKit::InputType::Password }))
            isPasswordField = true;
    }

    if (isPasswordField)
        return WEB_UI_STRING("Password", "Fallback label text for unlabeled password field.");

    switch (inputMode) {
    case WebCore::InputMode::Telephone:
        return WEB_UI_STRING("Phone number", "Fallback label text for unlabeled telephone number input field.");
    case WebCore::InputMode::Url:
        return WEB_UI_STRING("URL", "Fallback label text for unlabeled URL input field.");
    case WebCore::InputMode::Email:
        return WEB_UI_STRING("Email address", "Fallback label text for unlabeled email address field.");
    case WebCore::InputMode::Numeric:
    case WebCore::InputMode::Decimal:
        return WEB_UI_STRING("Enter a number", "Fallback label text for unlabeled numeric field.");
    case WebCore::InputMode::Search:
        return WEB_UI_STRING("Search", "Fallback label text for unlabeled search field");
    case WebCore::InputMode::Unspecified:
    case WebCore::InputMode::None:
    case WebCore::InputMode::Text:
        return WEB_UI_STRING("Enter text", "Fallback label text for unlabeled text field.");
    }
}
#endif

- (NSString *)inputLabelText
{
    if (!_focusedElementInformation.label.isEmpty())
        return _focusedElementInformation.label.createNSString().autorelease();

    if (!_focusedElementInformation.ariaLabel.isEmpty())
        return _focusedElementInformation.ariaLabel.createNSString().autorelease();

    if (!_focusedElementInformation.title.isEmpty())
        return _focusedElementInformation.title.createNSString().autorelease();

#if PLATFORM(WATCHOS)
    if (_focusedElementInformation.placeholder.isEmpty())
        return fallbackLabelTextForUnlabeledInputFieldInZoomedFormControls(_focusedElementInformation.inputMode, _focusedElementInformation.elementType).createNSString().autorelease();
#endif

    return _focusedElementInformation.placeholder.createNSString().autorelease();
}

#pragma mark - UITextInputMultiDocument

- (BOOL)_restoreFocusWithToken:(id <NSCopying, NSSecureCoding>)token
{
    if (_focusStateStack.isEmpty()) {
        ASSERT_NOT_REACHED();
        return NO;
    }

    if (_focusStateStack.takeLast())
        [_webView _decrementFocusPreservationCount];

    // FIXME: Our current behavior in -_restoreFocusWithToken: does not force the web view to become first responder
    // by refocusing the currently focused element. As such, we return NO here so that UIKit will tell WKContentView
    // to become first responder in the future.
    return NO;
}

- (void)startRelinquishingFirstResponderToFocusedElement
{
    if (_isRelinquishingFirstResponderToFocusedElement)
        return;

    _isRelinquishingFirstResponderToFocusedElement = YES;
    [_webView _incrementFocusPreservationCount];
}

- (void)stopRelinquishingFirstResponderToFocusedElement
{
    if (!_isRelinquishingFirstResponderToFocusedElement)
        return;

    _isRelinquishingFirstResponderToFocusedElement = NO;
    [_webView _decrementFocusPreservationCount];
}

- (void)_preserveFocusWithToken:(id <NSCopying, NSSecureCoding>)token destructively:(BOOL)destructively
{
    if (!_inputPeripheral) {
        [_webView _incrementFocusPreservationCount];
        _focusStateStack.append(true);
    } else
        _focusStateStack.append(false);
}

- (BOOL)_shouldIgnoreTouchEvent:(UIEvent *)event
{
    _touchEventsCanPreventNativeGestures = YES;

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if ([_imageAnalysisInteraction interactableItemExistsAtPoint:[_touchEventGestureRecognizer locationInView:self]])
        return YES;
#endif

    return [self gestureRecognizer:_touchEventGestureRecognizer.get() isInterruptingMomentumScrollingWithEvent:event];
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer isInterruptingMomentumScrollingWithEvent:(UIEvent *)event
{
    NSSet<UITouch *> *touches = [event touchesForGestureRecognizer:gestureRecognizer];
    for (UITouch *touch in touches) {
        if (dynamic_objc_cast<UIScrollView>(touch.view)._wk_isInterruptingDeceleration)
            return YES;
    }
    return self._scroller._wk_isInterruptingDeceleration;
}

#pragma mark - Implementation of WKActionSheetAssistantDelegate.

- (std::optional<WebKit::InteractionInformationAtPosition>)positionInformationForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    WebKit::InteractionInformationRequest request(_positionInformation.request.point);
    request.includeSnapshot = true;
    request.includeLinkIndicator = assistant.needsLinkIndicator;
    request.linkIndicatorShouldHaveLegacyMargins = !self._shouldUseContextMenus;
    request.gatherAnimations = [self.webView _allowAnimationControls];

    if (![self ensurePositionInformationIsUpToDate:request])
        return std::nullopt;

    return _positionInformation;
}

- (void)updatePositionInformationForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    _hasValidPositionInformation = NO;
    WebKit::InteractionInformationRequest request(_positionInformation.request.point);
    request.includeSnapshot = true;
    request.includeLinkIndicator = assistant.needsLinkIndicator;
    request.linkIndicatorShouldHaveLegacyMargins = !self._shouldUseContextMenus;
    request.gatherAnimations = [self.webView _allowAnimationControls];

    [self requestAsynchronousPositionInformationUpdate:request];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant performAction:(WebKit::SheetAction)action
{
    if (action == WebKit::SheetAction::Copy && [self _tryToCopyLinkURLFromPlugin])
        return;

    _page->performActionOnElement((uint32_t)action);
}

- (void)_actionSheetAssistant:(WKActionSheetAssistant *)assistant performAction:(WebKit::SheetAction)action onElements:(Vector<WebCore::ElementContext>&&)elements
{
    _page->performActionOnElements((uint32_t)action, WTFMove(elements));
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant openElementAtLocation:(CGPoint)location
{
    [self _attemptSyntheticClickAtLocation:location modifierFlags:0];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant shareElementWithURL:(NSURL *)url rect:(CGRect)boundingRect
{
    WebCore::ShareDataWithParsedURL shareData;
    shareData.url = { url };
    shareData.originator = WebCore::ShareDataOriginator::User;
    [self _showShareSheet:shareData inRect: { [self convertRect:boundingRect toView:self.webView] } completionHandler:nil];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant shareElementWithImage:(UIImage *)image rect:(CGRect)boundingRect
{
    WebCore::ShareDataWithParsedURL shareData;
    RetainPtr fileName = adoptNS([[NSString alloc] initWithFormat:@"%@.png", (NSString*)WEB_UI_STRING("Shared Image", "Default name for the file created for a shared image with no explicit name.").createNSString().get()]);
    shareData.files = { { fileName.get(), WebCore::SharedBuffer::create(UIImagePNGRepresentation(image)) } };
    shareData.originator = WebCore::ShareDataOriginator::User;
    [self _showShareSheet:shareData inRect: { [self convertRect:boundingRect toView:self.webView] } completionHandler:nil];
}

#if HAVE(APP_LINKS)
- (BOOL)actionSheetAssistant:(WKActionSheetAssistant *)assistant shouldIncludeAppLinkActionsForElement:(_WKActivatedElementInfo *)element
{
    return _page->uiClient().shouldIncludeAppLinkActionsForElement(element);
}
#endif

- (BOOL)actionSheetAssistant:(WKActionSheetAssistant *)assistant showCustomSheetForElement:(_WKActivatedElementInfo *)element
{
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    
    if ([uiDelegate respondsToSelector:@selector(_webView:showCustomSheetForElement:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate _webView:self.webView showCustomSheetForElement:element]) {
#if ENABLE(DRAG_SUPPORT)
            BOOL shouldCancelAllTouches = !_dragDropInteractionState.dragSession();
#else
            BOOL shouldCancelAllTouches = YES;
#endif

            // Prevent tap-and-hold and panning.
            if (shouldCancelAllTouches)
                [UIApplication.sharedApplication _cancelAllTouches];

            return YES;
        }
ALLOW_DEPRECATED_DECLARATIONS_END
    }

    return NO;
}

- (BOOL)_tryToCopyLinkURLFromPlugin
{
    if (!_positionInformation.isLink || !_positionInformation.isInPlugin)
        return NO;

    RetainPtr urlToCopy = _positionInformation.url.createNSURL();
    if (!urlToCopy)
        return NO;

    [UIPasteboard _performAsDataOwner:[_webView _effectiveDataOwner:self._dataOwnerForCopy] block:^{
        UIPasteboard.generalPasteboard.URL = urlToCopy.get();
    }];
    return YES;
}

// FIXME: Likely we can remove this special case for watchOS.
#if !PLATFORM(WATCHOS)
- (CGRect)unoccludedWindowBoundsForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    UIEdgeInsets contentInset = [[_webView scrollView] adjustedContentInset];
    CGRect rect = UIEdgeInsetsInsetRect([_webView bounds], contentInset);
    return [_webView convertRect:rect toView:[self window]];
}
#endif

- (RetainPtr<NSArray>)actionSheetAssistant:(WKActionSheetAssistant *)assistant decideActionsForElement:(_WKActivatedElementInfo *)element defaultActions:(RetainPtr<NSArray>)defaultActions
{
    return _page->uiClient().actionsForElement(element, WTFMove(defaultActions));
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant willStartInteractionWithElement:(_WKActivatedElementInfo *)element
{
    _page->startInteractionWithPositionInformation(_positionInformation);
}

- (void)actionSheetAssistantDidStopInteraction:(WKActionSheetAssistant *)assistant
{
    _page->stopInteraction();
}

- (NSDictionary *)dataDetectionContextForPositionInformation:(const WebKit::InteractionInformationAtPosition&)positionInformation
{
    RetainPtr<NSMutableDictionary> context;
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    if ([uiDelegate respondsToSelector:@selector(_dataDetectionContextForWebView:)])
        context = adoptNS([[uiDelegate _dataDetectionContextForWebView:self.webView] mutableCopy]);
    
    if (!context)
        context = adoptNS([[NSMutableDictionary alloc] init]);

#if ENABLE(DATA_DETECTION)
    if (!positionInformation.textBefore.isEmpty())
        context.get()[PAL::get_DataDetectorsUI_kDataDetectorsLeadingText()] = positionInformation.textBefore.createNSString().get();
    if (!positionInformation.textAfter.isEmpty())
        context.get()[PAL::get_DataDetectorsUI_kDataDetectorsTrailingText()] = positionInformation.textAfter.createNSString().get();

    auto canShowPreview = ^{
        if (!positionInformation.url.createNSURL().get().iTunesStoreURL)
            return YES;
        if (!_page->websiteDataStore().isPersistent())
            return NO;
#if ENABLE(ADVANCED_PRIVACY_PROTECTIONS)
        if (_page->advancedPrivacyProtectionsPolicies().contains(WebCore::AdvancedPrivacyProtections::BaselineProtections))
            return NO;
#endif
        return YES;
    }();

    if (!canShowPreview)
        context.get()[PAL::get_DataDetectorsUI_kDDContextMenuWantsPreviewKey()] = @NO;

    CGRect sourceRect;
    if (positionInformation.isLink && positionInformation.textIndicator)
        sourceRect = positionInformation.textIndicator->textBoundingRectInRootViewCoordinates();
    else if (!positionInformation.dataDetectorBounds.isEmpty())
        sourceRect = positionInformation.dataDetectorBounds;
    else
        sourceRect = positionInformation.bounds;

    CGRect frameInContainerViewCoordinates = [self convertRect:sourceRect toView:self.containerForContextMenuHintPreviews];
    return [PAL::getDDContextMenuActionClass() updateContext:context.get() withSourceRect:frameInContainerViewCoordinates];
#else
    return context.autorelease();
#endif
}

- (NSDictionary *)dataDetectionContextForActionSheetAssistant:(WKActionSheetAssistant *)assistant positionInformation:(const WebKit::InteractionInformationAtPosition&)positionInformation
{
    return [self dataDetectionContextForPositionInformation:positionInformation];
}

- (NSString *)selectedTextForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    return [self selectedText];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant getAlternateURLForImage:(UIImage *)image completion:(void (^)(NSURL *alternateURL, NSDictionary *userInfo))completion
{
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    if ([uiDelegate respondsToSelector:@selector(_webView:getAlternateURLFromImage:completionHandler:)]) {
        [uiDelegate _webView:self.webView getAlternateURLFromImage:image completionHandler:^(NSURL *alternateURL, NSDictionary *userInfo) {
            completion(alternateURL, userInfo);
        }];
    } else
        completion(nil, nil);
}

- (NSArray<UIMenuElement *> *)additionalMediaControlsContextMenuItemsForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
#if PLATFORM(VISION)
    if (self.webView.fullscreenState == WKFullscreenStateInFullscreen)
        return @[ [self.webView fullScreenWindowSceneDimmingAction] ];
#endif
    return @[ ];
}

#if USE(UICONTEXTMENU)

- (UITargetedPreview *)createTargetedContextMenuHintForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    return [self _createTargetedContextMenuHintPreviewIfPossible];
}

- (void)removeContextMenuViewIfPossibleForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    [self _removeContextMenuHintContainerIfPossible];
}

- (void)actionSheetAssistantDidShowContextMenu:(WKActionSheetAssistant *)assistant
{
    [_webView _didShowContextMenu];
}

- (void)actionSheetAssistantDidDismissContextMenu:(WKActionSheetAssistant *)assistant
{
    [_webView _didDismissContextMenu];
}

- (void)_targetedPreviewContainerDidRemoveLastSubview:(WKTargetedPreviewContainer *)containerView
{
    if (_contextMenuHintContainerView == containerView)
        [self _removeContextMenuHintContainerIfPossible];
}

#endif // USE(UICONTEXTMENU)

- (BOOL)_shouldUseContextMenus
{
#if USE(UICONTEXTMENU)
    return linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::HasUIContextMenuInteraction);
#endif
    return NO;
}

- (BOOL)_shouldUseContextMenusForFormControls
{
    return self._formControlRefreshEnabled && self._shouldUseContextMenus;
}

- (BOOL)_shouldAvoidResizingWhenInputViewBoundsChange
{
    return _focusedElementInformation.shouldAvoidResizingWhenInputViewBoundsChange;
}

- (BOOL)_shouldAvoidScrollingWhenFocusedContentIsVisible
{
    return _focusedElementInformation.shouldAvoidScrollingWhenFocusedContentIsVisible;
}

- (BOOL)_shouldUseLegacySelectPopoverDismissalBehavior
{
    if (PAL::currentUserInterfaceIdiomIsSmallScreen())
        return NO;

    if (_focusedElementInformation.elementType != WebKit::InputType::Select)
        return NO;

    if (!_focusedElementInformation.shouldUseLegacySelectPopoverDismissalBehaviorInDataActivation)
        return NO;

    return WTF::IOSApplication::isDataActivation();
}

#if ENABLE(IMAGE_ANALYSIS)

- (BOOL)shouldDeferGestureDueToImageAnalysis:(UIGestureRecognizer *)gesture
{
    return gesture._wk_isTextInteractionLoupeGesture || gesture._wk_isTapAndAHalf;
}

#endif // ENABLE(IMAGE_ANALYSIS)

static WebCore::DataOwnerType coreDataOwnerType(_UIDataOwner platformType)
{
    switch (platformType) {
    case _UIDataOwnerUser:
        return WebCore::DataOwnerType::User;
    case _UIDataOwnerEnterprise:
        return WebCore::DataOwnerType::Enterprise;
    case _UIDataOwnerShared:
        return WebCore::DataOwnerType::Shared;
    case _UIDataOwnerUndefined:
        return WebCore::DataOwnerType::Undefined;
    }
    ASSERT_NOT_REACHED();
    return WebCore::DataOwnerType::Undefined;
}

- (WebCore::DataOwnerType)_dataOwnerForPasteboard:(WebKit::PasteboardAccessIntent)intent
{
    auto clientSuppliedDataOwner = intent == WebKit::PasteboardAccessIntent::Read ? self._dataOwnerForPaste : self._dataOwnerForCopy;
    return coreDataOwnerType([_webView _effectiveDataOwner:clientSuppliedDataOwner]);
}

- (RetainPtr<WKTargetedPreviewContainer>)_createPreviewContainerWithLayerName:(NSString *)layerName
{
    auto container = adoptNS([[WKTargetedPreviewContainer alloc] initWithContentView:self]);
    [container layer].anchorPoint = CGPointZero;
    [container layer].name = layerName;
    return container;
}

- (UIView *)containerForDropPreviews
{
    if (!_dropPreviewContainerView) {
        _dropPreviewContainerView = [self _createPreviewContainerWithLayerName:@"Drop Preview Container"];
        [_interactionViewsContainerView addSubview:_dropPreviewContainerView.get()];
    }

    ASSERT([_dropPreviewContainerView superview]);
    return _dropPreviewContainerView.get();
}

- (void)_removeContainerForDropPreviews
{
    if (!_dropPreviewContainerView)
        return;

    [std::exchange(_dropPreviewContainerView, nil) removeFromSuperview];
}

- (UIView *)containerForDragPreviews
{
    if (!_dragPreviewContainerView) {
        _dragPreviewContainerView = [self _createPreviewContainerWithLayerName:@"Drag Preview Container"];
        [_interactionViewsContainerView addSubview:_dragPreviewContainerView.get()];
    }

    ASSERT([_dragPreviewContainerView superview]);
    return _dragPreviewContainerView.get();
}

- (void)_removeContainerForDragPreviews
{
    if (!_dragPreviewContainerView)
        return;

    [std::exchange(_dragPreviewContainerView, nil) removeFromSuperview];
}

- (UIView *)containerForContextMenuHintPreviews
{
    if (!_contextMenuHintContainerView) {
        _contextMenuHintContainerView = [self _createPreviewContainerWithLayerName:@"Context Menu Hint Preview Container"];

        RetainPtr<UIView> containerView;

        if (auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate)) {
            if ([uiDelegate respondsToSelector:@selector(_contextMenuHintPreviewContainerViewForWebView:)])
                containerView = [uiDelegate _contextMenuHintPreviewContainerViewForWebView:self.webView];
        }

        if (!containerView)
            containerView = _interactionViewsContainerView;

        [containerView addSubview:_contextMenuHintContainerView.get()];
    }

    ASSERT([_contextMenuHintContainerView superview]);
    return _contextMenuHintContainerView.get();
}

- (void)_removeContainerForContextMenuHintPreviews
{
    if (!_contextMenuHintContainerView)
        return;

    [std::exchange(_contextMenuHintContainerView, nil) removeFromSuperview];

    _scrollViewForTargetedPreview = nil;
    _scrollViewForTargetedPreviewInitialOffset = CGPointZero;
}

- (void)_updateFrameOfContainerForContextMenuHintPreviewsIfNeeded
{
    if (!_contextMenuHintContainerView)
        return;

    CGPoint newOffset = [_scrollViewForTargetedPreview convertPoint:CGPointZero toView:[_contextMenuHintContainerView superview]];

    CGRect frame = [_contextMenuHintContainerView frame];
    frame.origin.x = newOffset.x - _scrollViewForTargetedPreviewInitialOffset.x;
    frame.origin.y = newOffset.y - _scrollViewForTargetedPreviewInitialOffset.y;
    [_contextMenuHintContainerView setFrame:frame];
}

- (void)_updateTargetedPreviewScrollViewUsingContainerScrollingNodeID:(std::optional<WebCore::ScrollingNodeID>)scrollingNodeID
{
    if (scrollingNodeID) {
        if (RetainPtr scrollViewForScrollingNode = [self _scrollViewForScrollingNodeID:scrollingNodeID])
            _scrollViewForTargetedPreview = scrollViewForScrollingNode.get();
    }

    if (!_scrollViewForTargetedPreview)
        _scrollViewForTargetedPreview = self.webView.scrollView;

    _scrollViewForTargetedPreviewInitialOffset = [_scrollViewForTargetedPreview convertPoint:CGPointZero toView:[_contextMenuHintContainerView superview]];
}

#pragma mark - WKDeferringGestureRecognizerDelegate

- (WebKit::ShouldDeferGestures)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer willBeginTouchesWithEvent:(UIEvent *)event
{
    self.gestureRecognizerConsistencyEnforcer.beginTracking(deferringGestureRecognizer);

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if ([_imageAnalysisInteraction interactableItemExistsAtPoint:[deferringGestureRecognizer locationInView:self]])
        return WebKit::ShouldDeferGestures::No;
#endif

    return [self gestureRecognizer:deferringGestureRecognizer isInterruptingMomentumScrollingWithEvent:event] ? WebKit::ShouldDeferGestures::No : WebKit::ShouldDeferGestures::Yes;
}

- (void)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer didTransitionToState:(UIGestureRecognizerState)state
{
    if (state == UIGestureRecognizerStateEnded || state == UIGestureRecognizerStateFailed || state == UIGestureRecognizerStateCancelled)
        self.gestureRecognizerConsistencyEnforcer.endTracking(deferringGestureRecognizer);
}

- (void)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer didEndTouchesWithEvent:(UIEvent *)event
{
    self.gestureRecognizerConsistencyEnforcer.endTracking(deferringGestureRecognizer);

    if (deferringGestureRecognizer.state != UIGestureRecognizerStatePossible)
        return;

    if (_page->isHandlingPreventableTouchStart() && [self _isTouchStartDeferringGesture:deferringGestureRecognizer])
        return;

    if (_page->isHandlingPreventableTouchMove() && deferringGestureRecognizer == _touchMoveDeferringGestureRecognizer)
        return;

    if (_page->isHandlingPreventableTouchEnd() && [self _isTouchEndDeferringGesture:deferringGestureRecognizer])
        return;

    if ([_touchEventGestureRecognizer state] == UIGestureRecognizerStatePossible)
        return;

    // In the case where the touch event gesture recognizer has failed or ended already and we are not in the middle of handling
    // an asynchronous (but preventable) touch event, this is our last chance to lift the gesture "gate" by failing the deferring
    // gesture recognizer.
    deferringGestureRecognizer.state = UIGestureRecognizerStateFailed;
}

- (BOOL)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer shouldDeferOtherGestureRecognizer:(UIGestureRecognizer *)gestureRecognizer
{
#if ENABLE(IOS_TOUCH_EVENTS)
    if ([self _touchEventsMustRequireGestureRecognizerToFail:gestureRecognizer])
        return NO;

    if (_failedTouchStartDeferringGestures && _failedTouchStartDeferringGestures->contains(deferringGestureRecognizer)
        && deferringGestureRecognizer.state == UIGestureRecognizerStatePossible) {
        // This deferring gesture no longer has an oppportunity to defer native gestures (either because the touch region did not have any
        // active touch event listeners, or because any active touch event listeners on the page have already executed, and did not prevent
        // default). UIKit may have already reset the gesture to Possible state underneath us, in which case we still need to treat it as
        // if it has already failed; otherwise, we will incorrectly defer other gestures in the web view, such as scroll view pinching.
        return NO;
    }

    if (gestureRecognizer == _keyboardDismissalGestureRecognizer)
        return NO;

    auto webView = _webView.getAutoreleased();
    auto view = gestureRecognizer.view;
    BOOL gestureIsInstalledOnOrUnderWebView = NO;
    while (view) {
        if (view == webView) {
            gestureIsInstalledOnOrUnderWebView = YES;
            break;
        }
        view = view.superview;
    }

    if (!gestureIsInstalledOnOrUnderWebView && ![self _gestureRecognizerCanBePreventedByTouchEvents:gestureRecognizer])
        return NO;

    if ([gestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class])
        return NO;

    if (gestureRecognizer == _touchEventGestureRecognizer)
        return NO;

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    if ([_mouseInteraction hasGesture:gestureRecognizer])
        return NO;
#endif

#if ENABLE(IMAGE_ANALYSIS)
    if (deferringGestureRecognizer == _imageAnalysisDeferringGestureRecognizer)
        return [self shouldDeferGestureDueToImageAnalysis:gestureRecognizer];
#endif

    if (deferringGestureRecognizer == _touchMoveDeferringGestureRecognizer)
        return [gestureRecognizer isKindOfClass:UIPanGestureRecognizer.class] || [gestureRecognizer isKindOfClass:UIPinchGestureRecognizer.class];

    BOOL mayDelayReset = [&]() -> BOOL {
        if ([self _isContextMenuGestureRecognizerForFailureRelationships:gestureRecognizer])
            return YES;

#if ENABLE(DRAG_SUPPORT)
        if ([self _isDragInitiationGestureRecognizer:gestureRecognizer])
            return YES;
#endif

#if ENABLE(IMAGE_ANALYSIS)
        if (gestureRecognizer == _imageAnalysisGestureRecognizer)
            return YES;
#endif

        if (gestureRecognizer._wk_isTapAndAHalf)
            return YES;

        if (gestureRecognizer._wk_isTextInteractionLoupeGesture)
            return YES;
        
        if (gestureRecognizer == _highlightLongPressGestureRecognizer)
            return YES;

        if (auto *tapGesture = dynamic_objc_cast<UITapGestureRecognizer>(gestureRecognizer))
            return tapGesture.numberOfTapsRequired > 1 && tapGesture.numberOfTouchesRequired < 2;

        return NO;
    }();

    BOOL isSyntheticTap = [gestureRecognizer isKindOfClass:WKSyntheticTapGestureRecognizer.class];
    if ([gestureRecognizer isKindOfClass:UITapGestureRecognizer.class]) {
        if (deferringGestureRecognizer == _touchEndDeferringGestureRecognizerForSyntheticTapGestures)
            return isSyntheticTap;

        if (deferringGestureRecognizer == _touchEndDeferringGestureRecognizerForDelayedResettableGestures)
            return !isSyntheticTap && mayDelayReset;

        if (deferringGestureRecognizer == _touchEndDeferringGestureRecognizerForImmediatelyResettableGestures)
            return !isSyntheticTap && !mayDelayReset;
    }

    if (isSyntheticTap)
        return deferringGestureRecognizer == _touchStartDeferringGestureRecognizerForSyntheticTapGestures;

    if (mayDelayReset)
        return deferringGestureRecognizer == _touchStartDeferringGestureRecognizerForDelayedResettableGestures;

    return deferringGestureRecognizer == _touchStartDeferringGestureRecognizerForImmediatelyResettableGestures;
#else
    UNUSED_PARAM(deferringGestureRecognizer);
    UNUSED_PARAM(gestureRecognizer);
    return NO;
#endif
}

#if ENABLE(DRAG_SUPPORT)

static BOOL shouldEnableDragInteractionForPolicy(_WKDragInteractionPolicy policy)
{
    switch (policy) {
    case _WKDragInteractionPolicyAlwaysEnable:
        return YES;
    case _WKDragInteractionPolicyAlwaysDisable:
        return NO;
    default:
        return [UIDragInteraction isEnabledByDefault];
    }
}

- (void)_didChangeDragInteractionPolicy
{
    [_dragInteraction setEnabled:shouldEnableDragInteractionForPolicy(self.webView._dragInteractionPolicy)];
}

- (NSTimeInterval)dragLiftDelay
{
    static const NSTimeInterval mediumDragLiftDelay = 0.5;
    static const NSTimeInterval longDragLiftDelay = 0.65;
    auto dragLiftDelay = self.webView.configuration._dragLiftDelay;
    if (dragLiftDelay == _WKDragLiftDelayMedium)
        return mediumDragLiftDelay;
    if (dragLiftDelay == _WKDragLiftDelayLong)
        return longDragLiftDelay;
    return _UIDragInteractionDefaultLiftDelay();
}

- (id <WKUIDelegatePrivate>)webViewUIDelegate
{
    return (id <WKUIDelegatePrivate>)[_webView UIDelegate];
}

- (Class)_dragInteractionClass
{
#if USE(BROWSERENGINEKIT)
    if (self.shouldUseAsyncInteractions)
        return [BEDragInteraction class];
#endif
    return UIDragInteraction.class;
}

- (void)setUpDragAndDropInteractions
{
    _dragInteraction = adoptNS([[self._dragInteractionClass alloc] initWithDelegate:self]);
    _dropInteraction = adoptNS([[UIDropInteraction alloc] initWithDelegate:self]);
    [_dragInteraction setEnabled:shouldEnableDragInteractionForPolicy(self.webView._dragInteractionPolicy)];
#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)
    if (!self.shouldUseAsyncInteractions) {
        [_dragInteraction _setLiftDelay:self.dragLiftDelay];
        [_dragInteraction _setAllowsPointerDragBeforeLiftDelay:NO];
    }
#endif

    [self addInteraction:_dragInteraction.get()];
    [self addInteraction:_dropInteraction.get()];
}

- (void)teardownDragAndDropInteractions
{
    if (_dragInteraction)
        [self removeInteraction:_dragInteraction.get()];

    if (_dropInteraction)
        [self removeInteraction:_dropInteraction.get()];

    _dragInteraction = nil;
    _dropInteraction = nil;

    [self cleanUpDragSourceSessionState];
}

- (void)_startDrag:(RetainPtr<CGImageRef>)image item:(const WebCore::DragItem&)item nodeID:(std::optional<WebCore::NodeIdentifier>)nodeID
{
    ASSERT(item.sourceAction);

#if ENABLE(MODEL_PROCESS)
    _dragDropInteractionState.setElementIdentifier(nodeID);
    if (item.modelLayerID && _page) {
        if (RefPtr modelPresentationManager = _page->modelPresentationManagerProxy()) {
            if (RetainPtr viewForDragPreview = modelPresentationManager->startDragForModel(*item.modelLayerID)) {
                _dragDropInteractionState.stageDragItem(item, viewForDragPreview);
                return;
            }
        }
    }
#endif

    if (item.promisedAttachmentInfo)
        [self _prepareToDragPromisedAttachment:item.promisedAttachmentInfo];

    auto dragImage = adoptNS([[UIImage alloc] initWithCGImage:image.get() scale:_page->deviceScaleFactor() orientation:UIImageOrientationUp]);
    _dragDropInteractionState.stageDragItem(item, dragImage.get());
}

- (void)_didHandleAdditionalDragItemsRequest:(BOOL)added
{
    auto completion = _dragDropInteractionState.takeAddDragItemCompletionBlock();
    if (!completion)
        return;

    auto *registrationLists = [[WebItemProviderPasteboard sharedInstance] takeRegistrationLists];
    if (!added || ![registrationLists count] || !_dragDropInteractionState.hasStagedDragSource()) {
        _dragDropInteractionState.clearStagedDragSource();
        completion(@[ ]);
        return;
    }

    auto stagedDragSource = _dragDropInteractionState.stagedDragSource();
    NSArray *dragItemsToAdd = [self _itemsForBeginningOrAddingToSessionWithRegistrationLists:registrationLists stagedDragSource:stagedDragSource];

    RELEASE_LOG(DragAndDrop, "Drag session: %p adding %tu items", _dragDropInteractionState.dragSession(), dragItemsToAdd.count);
    _dragDropInteractionState.clearStagedDragSource(dragItemsToAdd.count ? WebKit::DragDropInteractionState::DidBecomeActive::Yes : WebKit::DragDropInteractionState::DidBecomeActive::No);

    completion(dragItemsToAdd);

    if (dragItemsToAdd.count)
        _page->didStartDrag();
}

- (void)_didHandleDragStartRequest:(BOOL)started
{
    BlockPtr<void()> savedCompletionBlock = _dragDropInteractionState.takeDragStartCompletionBlock();
    ASSERT(savedCompletionBlock);

    RELEASE_LOG(DragAndDrop, "Handling drag start request (started: %d, completion block: %p)", started, savedCompletionBlock.get());
    if (savedCompletionBlock)
        savedCompletionBlock();

    if (!_dragDropInteractionState.dragSession().items.count) {
        auto positionForDragEnd = WebCore::roundedIntPoint(_dragDropInteractionState.adjustedPositionForDragEnd());
        [self cleanUpDragSourceSessionState];
        if (started) {
            // A client of the Objective C SPI or UIKit might have prevented the drag from beginning entirely in the UI process, in which case
            // we need to balance the `dragstart` event with a `dragend`.
            _page->dragEnded(positionForDragEnd, positionForDragEnd, { });
        }
    }
}

- (void)computeClientAndGlobalPointsForDropSession:(id <UIDropSession>)session outClientPoint:(CGPoint *)outClientPoint outGlobalPoint:(CGPoint *)outGlobalPoint
{
    // FIXME: This makes the behavior of drag events on iOS consistent with other synthetic mouse events on iOS (see WebPage::completeSyntheticClick).
    // However, we should experiment with making the client position relative to the window and the global position in document coordinates. See
    // https://bugs.webkit.org/show_bug.cgi?id=173855 for more details.
    auto locationInContentView = [session locationInView:self];
    if (outClientPoint)
        *outClientPoint = locationInContentView;

    if (outGlobalPoint)
        *outGlobalPoint = locationInContentView;
}

static UIDropOperation dropOperationForWebCoreDragOperation(std::optional<WebCore::DragOperation> operation)
{
    if (operation) {
        if (*operation == WebCore::DragOperation::Move)
            return UIDropOperationMove;
        if (*operation == WebCore::DragOperation::Copy)
            return UIDropOperationCopy;
    }
    return UIDropOperationCancel;
}

static std::optional<WebCore::DragOperation> coreDragOperationForUIDropOperation(UIDropOperation dropOperation)
{
    switch (dropOperation) {
    case UIDropOperationCancel:
        return std::nullopt;
    case UIDropOperationForbidden:
        return WebCore::DragOperation::Private;
    case UIDropOperationCopy:
        return WebCore::DragOperation::Copy;
    case UIDropOperationMove:
        return WebCore::DragOperation::Move;
    }
    ASSERT_NOT_REACHED();
    return std::nullopt;
}

- (WebCore::DragData)dragDataForDropSession:(id <UIDropSession>)session dragDestinationAction:(WKDragDestinationAction)dragDestinationAction
{
    CGPoint global;
    CGPoint client;
    [self computeClientAndGlobalPointsForDropSession:session outClientPoint:&client outGlobalPoint:&global];

    auto dragOperationMask = WebCore::anyDragOperation();
    if (!session.allowsMoveOperation)
        dragOperationMask.remove(WebCore::DragOperation::Move);

    return {
        session,
        WebCore::roundedIntPoint(client),
        WebCore::roundedIntPoint(global),
        dragOperationMask,
        { },
        WebKit::coreDragDestinationActionMask(dragDestinationAction),
        _page->webPageIDInMainFrameProcess()
    };
}

- (void)cleanUpDragSourceSessionState
{
    if (_waitingForEditDragSnapshot)
        return;

    if (_dragDropInteractionState.dragSession() || _dragDropInteractionState.isPerformingDrop())
        RELEASE_LOG(DragAndDrop, "Cleaning up dragging state (has pending operation: %d)", [[WebItemProviderPasteboard sharedInstance] hasPendingOperation]);

    if (![[WebItemProviderPasteboard sharedInstance] hasPendingOperation]) {
        // If we're performing a drag operation, don't clear out the pasteboard yet, since another web view may still require access to it.
        // The pasteboard will be cleared after the last client is finished performing a drag operation using the item providers.
        [[WebItemProviderPasteboard sharedInstance] setItemProviders:nil];
    }

    [[WebItemProviderPasteboard sharedInstance] clearRegistrationLists];
    [self _restoreEditMenuIfNeeded];

#if ENABLE(MODEL_PROCESS)
    if (_page) {
        if (RefPtr modelPresentationManager = _page->modelPresentationManagerProxy())
            modelPresentationManager->doneWithCurrentDragSession();

        if (_dragDropInteractionState.nodeIdentifier())
            _page->modelDragEnded(_dragDropInteractionState.nodeIdentifier().value());
    }
#endif

    [self _removeContainerForDragPreviews];
    [std::exchange(_visibleContentViewSnapshot, nil) removeFromSuperview];
    [self _removeDropCaret];
    _shouldRestoreEditMenuAfterDrop = NO;

    _dragDropInteractionState.dragAndDropSessionsDidBecomeInactive();
    _dragDropInteractionState = { };
}

- (void)_insertDropCaret:(CGRect)rect
{
#if HAVE(UI_TEXT_CURSOR_DROP_POSITION_ANIMATOR)
    if (self._shouldUseTextCursorDragAnimator) {
        _editDropTextCursorView = [adoptNS([[UITextSelectionDisplayInteraction alloc] initWithTextInput:self delegate:self]) cursorView];
        [self addSubview:_editDropTextCursorView.get()];
        [_editDropTextCursorView setFrame:rect];
        _editDropCaretAnimator = adoptNS([[UITextCursorDropPositionAnimator alloc] initWithTextCursorView:_editDropTextCursorView.get() textInput:self]);
        [_editDropCaretAnimator setCursorVisible:YES animated:YES];
        [_editDropCaretAnimator placeCursorAtPosition:[WKTextPosition textPositionWithRect:rect] animated:NO];
        return;
    }
#endif // HAVE(UI_TEXT_CURSOR_DROP_POSITION_ANIMATOR)
    _editDropCaretView = adoptNS([[_UITextDragCaretView alloc] initWithTextInputView:self]);
    [_editDropCaretView insertAtPosition:[WKTextPosition textPositionWithRect:rect]];
}

- (void)_removeDropCaret
{
    [std::exchange(_editDropCaretView, nil) remove];
#if HAVE(UI_TEXT_CURSOR_DROP_POSITION_ANIMATOR)
    [std::exchange(_editDropCaretAnimator, nil) setCursorVisible:NO animated:NO];
    [std::exchange(_editDropTextCursorView, nil) removeFromSuperview];
#endif
}

static NSArray<NSItemProvider *> *extractItemProvidersFromDragItems(NSArray<UIDragItem *> *dragItems)
{
    NSMutableArray<NSItemProvider *> *providers = [NSMutableArray array];
    for (UIDragItem *item in dragItems) {
        if (NSItemProvider *provider = item.itemProvider)
            [providers addObject:provider];
    }
    return providers;
}

static NSArray<NSItemProvider *> *extractItemProvidersFromDropSession(id <UIDropSession> session)
{
    return extractItemProvidersFromDragItems(session.items);
}

- (void)_willReceiveEditDragSnapshot
{
    _waitingForEditDragSnapshot = YES;
}

- (void)_didReceiveEditDragSnapshot:(std::optional<WebCore::TextIndicatorData>)data
{
    _waitingForEditDragSnapshot = NO;

    [self _deliverDelayedDropPreviewIfPossible:data];
    [self cleanUpDragSourceSessionState];

    if (auto action = WTFMove(_actionToPerformAfterReceivingEditDragSnapshot))
        action();
}

- (void)_deliverDelayedDropPreviewIfPossible:(std::optional<WebCore::TextIndicatorData>)data
{
    if (!_visibleContentViewSnapshot)
        return;

    if (!data)
        return;

    if (!data->contentImage)
        return;

    auto snapshotWithoutSelection = data->contentImageWithoutSelection;
    if (!snapshotWithoutSelection)
        return;

    auto unselectedSnapshotImage = snapshotWithoutSelection->nativeImage();
    if (!unselectedSnapshotImage)
        return;

    if (!_dropAnimationCount)
        return;

    auto unselectedContentImageForEditDrag = adoptNS([[UIImage alloc] initWithCGImage:unselectedSnapshotImage->platformImage().get() scale:_page->deviceScaleFactor() orientation:UIImageOrientationUp]);
    _unselectedContentSnapshot = adoptNS([[UIImageView alloc] initWithImage:unselectedContentImageForEditDrag.get()]);
    [_unselectedContentSnapshot setFrame:data->contentImageWithoutSelectionRectInRootViewCoordinates];

    [self insertSubview:_unselectedContentSnapshot.get() belowSubview:_visibleContentViewSnapshot.get()];
    _dragDropInteractionState.deliverDelayedDropPreview(self, self.containerForDropPreviews, data.value());
}

- (void)_didPerformDragOperation:(BOOL)handled
{
    RELEASE_LOG(DragAndDrop, "Finished performing drag controller operation (handled: %d)", handled);
    [[WebItemProviderPasteboard sharedInstance] decrementPendingOperationCount];
    id <UIDropSession> dropSession = _dragDropInteractionState.dropSession();
    if ([self.webViewUIDelegate respondsToSelector:@selector(_webView:dataInteractionOperationWasHandled:forSession:itemProviders:)])
        [self.webViewUIDelegate _webView:self.webView dataInteractionOperationWasHandled:handled forSession:dropSession itemProviders:[WebItemProviderPasteboard sharedInstance].itemProviders];

    CGPoint global;
    CGPoint client;
    [self computeClientAndGlobalPointsForDropSession:dropSession outClientPoint:&client outGlobalPoint:&global];
    [self cleanUpDragSourceSessionState];
    auto currentDragOperation = _page->currentDragOperation();
    _page->dragEnded(WebCore::roundedIntPoint(client), WebCore::roundedIntPoint(global), currentDragOperation ? *currentDragOperation : OptionSet<WebCore::DragOperation>({ }));
}

- (void)_didChangeDragCaretRect:(CGRect)previousRect currentRect:(CGRect)rect
{
    BOOL previousRectIsEmpty = CGRectIsEmpty(previousRect);
    BOOL currentRectIsEmpty = CGRectIsEmpty(rect);
    if (previousRectIsEmpty && currentRectIsEmpty)
        return;

    if (previousRectIsEmpty) {
        [self _insertDropCaret:rect];
        return;
    }

    if (currentRectIsEmpty) {
        [self _removeDropCaret];
        return;
    }

    RetainPtr caretPosition = [WKTextPosition textPositionWithRect:rect];
#if HAVE(UI_TEXT_CURSOR_DROP_POSITION_ANIMATOR)
    [_editDropCaretAnimator placeCursorAtPosition:caretPosition.get() animated:YES];
#endif
    [_editDropCaretView updateToPosition:caretPosition.get()];
}

- (void)_prepareToDragPromisedAttachment:(const WebCore::PromisedAttachmentInfo&)info
{
    auto session = retainPtr(_dragDropInteractionState.dragSession());
    if (!session) {
        ASSERT_NOT_REACHED();
        return;
    }

    RELEASE_LOG(DragAndDrop, "Drag session: %p preparing to drag with attachment identifier: %s", session.get(), info.attachmentIdentifier.utf8().data());

    RetainPtr<NSString> utiType;
    RetainPtr<NSString> fileName;
    if (auto attachment = _page->attachmentForIdentifier(info.attachmentIdentifier)) {
        utiType = attachment->utiType().createNSString();
        fileName = attachment->fileName().createNSString();
    }

    auto registrationList = adoptNS([[WebItemProviderRegistrationInfoList alloc] init]);
    [registrationList setPreferredPresentationStyle:WebPreferredPresentationStyleAttachment];
    if ([fileName length])
        [registrationList setSuggestedName:fileName.get()];
    for (size_t index = 0; index < info.additionalTypesAndData.size(); ++index) {
        auto nsData = info.additionalTypesAndData[index].second->createNSData();
        [registrationList addData:nsData.get() forType:info.additionalTypesAndData[index].first.createNSString().get()];
    }

    [registrationList addPromisedType:utiType.get() fileCallback:[session = WTFMove(session), weakSelf = WeakObjCPtr<WKContentView>(self), info] (WebItemProviderFileCallback callback) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf) {
            callback(nil, [NSError errorWithDomain:WKErrorDomain code:WKErrorWebViewInvalidated userInfo:nil]);
            return;
        }

        NSString *temporaryBlobDirectory = FileSystem::createTemporaryDirectory(@"blobs");
        NSURL *destinationURL = [NSURL fileURLWithPath:[temporaryBlobDirectory stringByAppendingPathComponent:[NSUUID UUID].UUIDString] isDirectory:NO];

        if (auto attachment = strongSelf->_page->attachmentForIdentifier(info.attachmentIdentifier); attachment && !attachment->isEmpty()) {
            attachment->doWithFileWrapper([&](NSFileWrapper *fileWrapper) {
                RELEASE_LOG(DragAndDrop, "Drag session: %p delivering promised attachment: %s at path: %@", session.get(), info.attachmentIdentifier.utf8().data(), destinationURL.path);
                NSError *fileWrapperError = nil;
                if ([fileWrapper writeToURL:destinationURL options:0 originalContentsURL:nil error:&fileWrapperError])
                    callback(destinationURL, nil);
                else
                    callback(nil, fileWrapperError);
            });
        } else
            callback(nil, [NSError errorWithDomain:WKErrorDomain code:WKErrorWebViewInvalidated userInfo:nil]);

        [ensureLocalDragSessionContext(session.get()) addTemporaryDirectory:temporaryBlobDirectory];
    }];

    WebItemProviderPasteboard *pasteboard = [WebItemProviderPasteboard sharedInstance];
    pasteboard.itemProviders = @[ [registrationList itemProvider] ];
    [pasteboard stageRegistrationLists:@[ registrationList.get() ]];
}

- (WKDragDestinationAction)_dragDestinationActionForDropSession:(id <UIDropSession>)session
{
    id <WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:dragDestinationActionMaskForDraggingInfo:)])
        return [uiDelegate _webView:self.webView dragDestinationActionMaskForDraggingInfo:session];

    return WKDragDestinationActionAny & ~WKDragDestinationActionLoad;
}

- (OptionSet<WebCore::DragSourceAction>)_allowedDragSourceActions
{
    auto allowedActions = WebCore::anyDragSourceAction();
    if (!self.isFirstResponder || !_suppressSelectionAssistantReasons.isEmpty()) {
        // Don't allow starting a drag on a selection when selection views are not visible.
        allowedActions.remove(WebCore::DragSourceAction::Selection);
    }
    return allowedActions;
}

- (id <UIDragDropSession>)currentDragOrDropSession
{
    if (_dragDropInteractionState.dropSession())
        return _dragDropInteractionState.dropSession();
    return _dragDropInteractionState.dragSession();
}

- (void)_restoreEditMenuIfNeeded
{
    if (!_shouldRestoreEditMenuAfterDrop)
        return;

    [_textInteractionWrapper didConcludeDrop];
    _shouldRestoreEditMenuAfterDrop = NO;
}

- (NSArray<UIDragItem *> *)_itemsForBeginningOrAddingToSessionWithRegistrationLists:(NSArray<WebItemProviderRegistrationInfoList *> *)registrationLists stagedDragSource:(const WebKit::DragSourceState&)stagedDragSource
{
    if (!registrationLists.count)
        return @[ ];

    NSMutableArray *adjustedItemProviders = [NSMutableArray array];
    id <WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:adjustedDataInteractionItemProvidersForItemProvider:representingObjects:additionalData:)]) {
        // FIXME: We should consider a new UI delegate hook that accepts a list of item providers, so we don't need to invoke this delegate method repeatedly for multiple items.
        for (WebItemProviderRegistrationInfoList *list in registrationLists) {
            NSItemProvider *defaultItemProvider = list.itemProvider;
            if (!defaultItemProvider)
                continue;

            auto representingObjects = adoptNS([[NSMutableArray alloc] init]);
            auto additionalData = adoptNS([[NSMutableDictionary alloc] init]);
            [list enumerateItems:[representingObjects, additionalData] (id <WebItemProviderRegistrar> item, NSUInteger) {
                if ([item respondsToSelector:@selector(representingObjectForClient)])
                    [representingObjects addObject:item.representingObjectForClient];
                if ([item respondsToSelector:@selector(typeIdentifierForClient)] && [item respondsToSelector:@selector(dataForClient)])
                    [additionalData setObject:item.dataForClient forKey:item.typeIdentifierForClient];
            }];
            NSArray *adjustedItems = [uiDelegate _webView:self.webView adjustedDataInteractionItemProvidersForItemProvider:defaultItemProvider representingObjects:representingObjects.get() additionalData:additionalData.get()];
            if (adjustedItems.count)
                [adjustedItemProviders addObjectsFromArray:adjustedItems];
        }
    } else {
        for (WebItemProviderRegistrationInfoList *list in registrationLists) {
            if (auto *defaultItemProvider = list.itemProvider)
                [adjustedItemProviders addObject:defaultItemProvider];
        }
    }

    NSMutableArray *dragItems = [NSMutableArray arrayWithCapacity:adjustedItemProviders.count];
    for (NSItemProvider *itemProvider in adjustedItemProviders) {
        auto item = adoptNS([[UIDragItem alloc] initWithItemProvider:itemProvider]);
        [item _setPrivateLocalContext:@(stagedDragSource.itemIdentifier)];
        [dragItems addObject:item.get()];
    }

    return dragItems;
}

- (void)insertTextPlaceholderWithSize:(CGSize)size completionHandler:(void (^)(UITextPlaceholder *))completionHandler
{
    _page->insertTextPlaceholder(WebCore::IntSize { size }, [weakSelf = WeakObjCPtr<WKContentView>(self), completionHandler = makeBlockPtr(completionHandler)](const std::optional<WebCore::ElementContext>& placeholder) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || ![strongSelf webView] || !placeholder) {
            completionHandler(nil);
            return;
        }
        WebCore::ElementContext placeholderToUse { *placeholder };
        placeholderToUse.boundingRect = [strongSelf convertRect:placeholderToUse.boundingRect fromView:[strongSelf webView]];
        completionHandler(adoptNS([[WKTextPlaceholder alloc] initWithElementContext:placeholderToUse]).get());
    });
}

- (void)removeTextPlaceholder:(UITextPlaceholder *)placeholder willInsertText:(BOOL)willInsertText completionHandler:(void (^)(void))completionHandler
{
    // FIXME: Implement support for willInsertText. See <https://bugs.webkit.org/show_bug.cgi?id=208747>.
    if (RetainPtr wkTextPlaceholder = dynamic_objc_cast<WKTextPlaceholder>(placeholder))
        _page->removeTextPlaceholder([wkTextPlaceholder elementContext], makeBlockPtr(completionHandler));
    else
        completionHandler();
}

static Vector<WebCore::IntSize> sizesOfPlaceholderElementsToInsertWhenDroppingItems(NSArray<NSItemProvider *> *itemProviders)
{
    Vector<WebCore::IntSize> sizes;
    for (NSItemProvider *item in itemProviders) {
        if (!WebCore::MIMETypeRegistry::isSupportedImageMIMEType(WebCore::MIMETypeFromUTI(item.web_fileUploadContentTypes.firstObject)))
            return { };

        WebCore::IntSize presentationSize(item.preferredPresentationSize);
        if (presentationSize.isEmpty())
            return { };

        sizes.append(WTFMove(presentationSize));
    }
    return sizes;
}

- (BOOL)_handleDropByInsertingImagePlaceholders:(NSArray<NSItemProvider *> *)itemProviders session:(id<UIDropSession>)session
{
    if (!self.webView._editable)
        return NO;

    if (_dragDropInteractionState.dragSession())
        return NO;

    if (session.items.count != itemProviders.count)
        return NO;

    auto imagePlaceholderSizes = sizesOfPlaceholderElementsToInsertWhenDroppingItems(itemProviders);
    if (imagePlaceholderSizes.isEmpty())
        return NO;

    RELEASE_LOG(DragAndDrop, "Inserting dropped image placeholders for session: %p", session);

    _page->insertDroppedImagePlaceholders(imagePlaceholderSizes, [protectedSelf = retainPtr(self), dragItems = retainPtr(session.items)] (auto& placeholderRects, auto data) {
        auto& state = protectedSelf->_dragDropInteractionState;
        if (!data || !protectedSelf->_dropAnimationCount) {
            RELEASE_LOG(DragAndDrop, "Failed to animate image placeholders: missing text indicator data.");
            return;
        }

        auto snapshotWithoutSelection = data->contentImageWithoutSelection;
        if (!snapshotWithoutSelection) {
            RELEASE_LOG(DragAndDrop, "Failed to animate image placeholders: missing unselected content image.");
            return;
        }

        auto unselectedSnapshotImage = snapshotWithoutSelection->nativeImage();
        if (!unselectedSnapshotImage) {
            RELEASE_LOG(DragAndDrop, "Failed to animate image placeholders: could not decode unselected content image.");
            return;
        }

        auto unselectedContentImageForEditDrag = adoptNS([[UIImage alloc] initWithCGImage:unselectedSnapshotImage->platformImage().get() scale:protectedSelf->_page->deviceScaleFactor() orientation:UIImageOrientationUp]);
        auto snapshotView = adoptNS([[UIImageView alloc] initWithImage:unselectedContentImageForEditDrag.get()]);
        [snapshotView setFrame:data->contentImageWithoutSelectionRectInRootViewCoordinates];
        [protectedSelf addSubview:snapshotView.get()];
        protectedSelf->_unselectedContentSnapshot = WTFMove(snapshotView);
        state.deliverDelayedDropPreview(protectedSelf.get(), [protectedSelf unobscuredContentRect], dragItems.get(), placeholderRects);
    });

    return YES;
}

#pragma mark - UIDragInteractionDelegate

- (BOOL)_dragInteraction:(UIDragInteraction *)interaction shouldDelayCompetingGestureRecognizer:(UIGestureRecognizer *)competingGestureRecognizer
{
    if (_highlightLongPressGestureRecognizer == competingGestureRecognizer) {
        // Since 3D touch still recognizes alongside the drag lift, and also requires the highlight long press
        // gesture to be active to support cancelling when `touchstart` is prevented, we should also allow the
        // highlight long press to recognize simultaneously, and manually cancel it when the drag lift is
        // recognized (see _dragInteraction:prepareForSession:completion:).
        return NO;
    }
    return [competingGestureRecognizer isKindOfClass:[UILongPressGestureRecognizer class]];
}

- (NSInteger)_dragInteraction:(UIDragInteraction *)interaction dataOwnerForSession:(id<UIDragSession>)session
{
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    NSInteger dataOwner = 0;
    if ([uiDelegate respondsToSelector:@selector(_webView:dataOwnerForDragSession:)])
        dataOwner = [uiDelegate _webView:self.webView dataOwnerForDragSession:session];
    return dataOwner;
}

- (void)_dragInteraction:(UIDragInteraction *)interaction itemsForAddingToSession:(id<UIDragSession>)session withTouchAtPoint:(CGPoint)point completion:(void(^)(NSArray<UIDragItem *> *))completion
{
    if (!_dragDropInteractionState.shouldRequestAdditionalItemForDragSession(session)) {
        completion(@[ ]);
        return;
    }

    _dragDropInteractionState.dragSessionWillRequestAdditionalItem(completion);
    _page->requestAdditionalItemsForDragSession(std::nullopt, WebCore::roundedIntPoint(point), WebCore::roundedIntPoint(point), self._allowedDragSourceActions, [weakSelf = WeakObjCPtr { self }] (bool handled) {
        [weakSelf _didHandleAdditionalDragItemsRequest:handled];
    });
}

- (void)_dragInteraction:(UIDragInteraction *)interaction prepareForSession:(id<UIDragSession>)session completion:(dispatch_block_t)completion
{
    RELEASE_LOG(DragAndDrop, "Preparing for drag session: %p", session);
    if (self.currentDragOrDropSession) {
        // FIXME: Support multiple simultaneous drag sessions in the future.
        RELEASE_LOG(DragAndDrop, "Drag session failed: %p (a current drag session already exists)", session);
        completion();
        return;
    }

    [self cleanUpDragSourceSessionState];

    auto prepareForSession = [weakSelf = WeakObjCPtr<WKContentView>(self), session = retainPtr(session), completion = makeBlockPtr(completion)] (WebKit::ProceedWithTextSelectionInImage proceedWithTextSelectionInImage) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || proceedWithTextSelectionInImage == WebKit::ProceedWithTextSelectionInImage::Yes) {
            completion();
            return;
        }

        auto dragOrigin = [session locationInView:strongSelf.get()];
        strongSelf->_dragDropInteractionState.prepareForDragSession(session.get(), completion.get());
        strongSelf->_page->requestDragStart(std::nullopt, WebCore::roundedIntPoint(dragOrigin), WebCore::roundedIntPoint([strongSelf convertPoint:dragOrigin toView:[strongSelf window]]), [strongSelf _allowedDragSourceActions], [weakSelf = WeakObjCPtr { strongSelf.get() }] (bool started) {
            [weakSelf _didHandleDragStartRequest:started];
        });
        RELEASE_LOG(DragAndDrop, "Drag session requested: %p at origin: {%.0f, %.0f}", session.get(), dragOrigin.x, dragOrigin.y);
    };

#if ENABLE(IMAGE_ANALYSIS)
    [self _doAfterPendingImageAnalysis:prepareForSession];
#else
    prepareForSession(WebKit::ProceedWithTextSelectionInImage::No);
#endif
}

- (NSArray<UIDragItem *> *)dragInteraction:(UIDragInteraction *)interaction itemsForBeginningSession:(id<UIDragSession>)session
{
    ASSERT(interaction == _dragInteraction);
    RELEASE_LOG(DragAndDrop, "Drag items requested for session: %p", session);
    if (_dragDropInteractionState.dragSession() != session) {
        RELEASE_LOG(DragAndDrop, "Drag session failed: %p (delegate session does not match %p)", session, _dragDropInteractionState.dragSession());
        return @[ ];
    }

    if (!_dragDropInteractionState.hasStagedDragSource()) {
        RELEASE_LOG(DragAndDrop, "Drag session failed: %p (missing staged drag source)", session);
        return @[ ];
    }

    auto stagedDragSource = _dragDropInteractionState.stagedDragSource();
    auto *registrationLists = [[WebItemProviderPasteboard sharedInstance] takeRegistrationLists];
    NSArray *dragItems = [self _itemsForBeginningOrAddingToSessionWithRegistrationLists:registrationLists stagedDragSource:stagedDragSource];
    if (![dragItems count])
        _page->dragCancelled();
    else
        [self _cancelLongPressGestureRecognizer];

    RELEASE_LOG(DragAndDrop, "Drag session: %p starting with %tu items", session, [dragItems count]);
    _dragDropInteractionState.clearStagedDragSource([dragItems count] ? WebKit::DragDropInteractionState::DidBecomeActive::Yes : WebKit::DragDropInteractionState::DidBecomeActive::No);

    return dragItems;
}

- (UITargetedDragPreview *)dragInteraction:(UIDragInteraction *)interaction previewForLiftingItem:(UIDragItem *)item session:(id<UIDragSession>)session
{
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:previewForLiftingItem:session:)]) {
        UITargetedDragPreview *overriddenPreview = [uiDelegate _webView:self.webView previewForLiftingItem:item session:session];
        if (overriddenPreview)
            return overriddenPreview;
    }
    return _dragDropInteractionState.previewForLifting(item, self, self.containerForDragPreviews, std::exchange(_positionInformationLinkIndicator, { }));
}

- (void)dragInteraction:(UIDragInteraction *)interaction willAnimateLiftWithAnimator:(id<UIDragAnimating>)animator session:(id<UIDragSession>)session
{
    RELEASE_LOG(DragAndDrop, "Drag session willAnimateLiftWithAnimator: %p", session);
    if (_dragDropInteractionState.anyActiveDragSourceContainsSelection()) {
        [self cancelActiveTextInteractionGestures];
        if (!_shouldRestoreEditMenuAfterDrop) {
            [_textInteractionWrapper willBeginDragLift];
            _shouldRestoreEditMenuAfterDrop = YES;
        }
    }

    auto positionForDragEnd = WebCore::roundedIntPoint(_dragDropInteractionState.adjustedPositionForDragEnd());
    RetainPtr<WKContentView> protectedSelf(self);
    [animator addCompletion:[session, positionForDragEnd, protectedSelf, page = _page] (UIViewAnimatingPosition finalPosition) {
#if RELEASE_LOG_DISABLED
        UNUSED_PARAM(session);
#endif
        if (finalPosition == UIViewAnimatingPositionStart) {
            RELEASE_LOG(DragAndDrop, "Drag session ended at start: %p", session);
            // The lift was canceled, so -dropInteraction:sessionDidEnd: will never be invoked. This is the last chance to clean up.
            [protectedSelf cleanUpDragSourceSessionState];
            page->dragEnded(positionForDragEnd, positionForDragEnd, { });
        }
#if !RELEASE_LOG_DISABLED
        else
            RELEASE_LOG(DragAndDrop, "Drag session did not end at start: %p", session);
#endif
    }];
}

- (void)dragInteraction:(UIDragInteraction *)interaction sessionWillBegin:(id<UIDragSession>)session
{
    RELEASE_LOG(DragAndDrop, "Drag session beginning: %p", session);
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:dataInteraction:sessionWillBegin:)])
        [uiDelegate _webView:self.webView dataInteraction:interaction sessionWillBegin:session];

    [_actionSheetAssistant cleanupSheet];
    _dragDropInteractionState.dragSessionWillBegin();
    _page->didStartDrag();
}

- (void)dragInteraction:(UIDragInteraction *)interaction session:(id<UIDragSession>)session didEndWithOperation:(UIDropOperation)operation
{
    RELEASE_LOG(DragAndDrop, "Drag session ended: %p (with operation: %tu, performing operation: %d, began dragging: %d)", session, operation, _dragDropInteractionState.isPerformingDrop(), _dragDropInteractionState.didBeginDragging());

    [self _restoreEditMenuIfNeeded];

    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:dataInteraction:session:didEndWithOperation:)])
        [uiDelegate _webView:self.webView dataInteraction:interaction session:session didEndWithOperation:operation];

    if (_dragDropInteractionState.isPerformingDrop())
        return;

    [self cleanUpDragSourceSessionState];
    _page->dragEnded(WebCore::roundedIntPoint(_dragDropInteractionState.adjustedPositionForDragEnd()), WebCore::roundedIntPoint(_dragDropInteractionState.adjustedPositionForDragEnd()), coreDragOperationForUIDropOperation(operation));
}

- (UITargetedDragPreview *)dragInteraction:(UIDragInteraction *)interaction previewForCancellingItem:(UIDragItem *)item withDefault:(UITargetedDragPreview *)defaultPreview
{
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:previewForCancellingItem:withDefault:)]) {
        UITargetedDragPreview *overriddenPreview = [uiDelegate _webView:self.webView previewForCancellingItem:item withDefault:defaultPreview];
        if (overriddenPreview)
            return overriddenPreview;
    }
    return _dragDropInteractionState.previewForCancelling(item, self, self.unscaledView);
}

- (void)dragInteraction:(UIDragInteraction *)interaction item:(UIDragItem *)item willAnimateCancelWithAnimator:(id<UIDragAnimating>)animator
{
    _isAnimatingDragCancel = YES;
    RELEASE_LOG(DragAndDrop, "Drag interaction willAnimateCancelWithAnimator");
    auto previewViews = _dragDropInteractionState.takePreviewViewsForDragCancel();
    for (auto& previewView : previewViews)
        [previewView setAlpha:0];

    [animator addCompletion:[protectedSelf = retainPtr(self), previewViews = WTFMove(previewViews), page = _page] (UIViewAnimatingPosition finalPosition) {
        RELEASE_LOG(DragAndDrop, "Drag interaction willAnimateCancelWithAnimator (animation completion block fired)");
        for (auto& previewView : previewViews)
            [previewView setAlpha:1];

        page->dragCancelled();

        page->callAfterNextPresentationUpdate([previewViews = WTFMove(previewViews), protectedSelf = WTFMove(protectedSelf)] {
            for (auto& previewView : previewViews)
                [previewView removeFromSuperview];

            protectedSelf->_isAnimatingDragCancel = NO;
        });
    }];
}

- (void)dragInteraction:(UIDragInteraction *)interaction sessionDidTransferItems:(id<UIDragSession>)session
{
    [existingLocalDragSessionContext(session) cleanUpTemporaryDirectories];
}

#if USE(BROWSERENGINEKIT)

#pragma mark - BEDragInteractionDelegate

- (void)dragInteraction:(BEDragInteraction *)interaction prepareDragSession:(id<UIDragSession>)session completion:(BOOL(^)(void))completion
{
    [self _dragInteraction:interaction prepareForSession:session completion:[completion = makeBlockPtr(completion)] {
        completion();
    }];
}

- (void)dragInteraction:(BEDragInteraction *)interaction itemsForAddingToSession:(id<UIDragSession>)session forTouchAtPoint:(CGPoint)point completion:(BOOL(^)(NSArray<UIDragItem *> *))completion
{
    [self _dragInteraction:interaction itemsForAddingToSession:session withTouchAtPoint:point completion:[completion = makeBlockPtr(completion)](NSArray<UIDragItem *> *items) {
        completion(items);
    }];
}

#endif // USE(BROWSERENGINEKIT)

#pragma mark - UIDropInteractionDelegate

- (NSInteger)_dropInteraction:(UIDropInteraction *)interaction dataOwnerForSession:(id<UIDropSession>)session
{
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    NSInteger dataOwner = 0;
    if ([uiDelegate respondsToSelector:@selector(_webView:dataOwnerForDropSession:)])
        dataOwner = [uiDelegate _webView:self.webView dataOwnerForDropSession:session];
    return dataOwner;
}

- (BOOL)dropInteraction:(UIDropInteraction *)interaction canHandleSession:(id<UIDropSession>)session
{
    // FIXME: Support multiple simultaneous drop sessions in the future.
    id<UIDragDropSession> dragOrDropSession = self.currentDragOrDropSession;
    RELEASE_LOG(DragAndDrop, "Can handle drag session: %p with local session: %p existing session: %p?", session, session.localDragSession, dragOrDropSession);

    return !dragOrDropSession || session.localDragSession == dragOrDropSession;
}

- (void)dropInteraction:(UIDropInteraction *)interaction sessionDidEnter:(id<UIDropSession>)session
{
    RELEASE_LOG(DragAndDrop, "Drop session entered: %p with %tu items", session, session.items.count);
    auto dragData = [self dragDataForDropSession:session dragDestinationAction:[self _dragDestinationActionForDropSession:session]];

    _dragDropInteractionState.dropSessionDidEnterOrUpdate(session, dragData);

    [[WebItemProviderPasteboard sharedInstance] setItemProviders:extractItemProvidersFromDropSession(session) dropSession:session];
    _page->dragEntered(dragData, WebCore::Pasteboard::nameOfDragPasteboard());
}

- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session
{
    [[WebItemProviderPasteboard sharedInstance] setItemProviders:extractItemProvidersFromDropSession(session) dropSession:session];

    auto dragData = [self dragDataForDropSession:session dragDestinationAction:[self _dragDestinationActionForDropSession:session]];
    _page->dragUpdated(dragData, WebCore::Pasteboard::nameOfDragPasteboard());
    _dragDropInteractionState.dropSessionDidEnterOrUpdate(session, dragData);

    auto delegate = self.webViewUIDelegate;
    auto operation = dropOperationForWebCoreDragOperation(_page->currentDragOperation());
    if ([delegate respondsToSelector:@selector(_webView:willUpdateDataInteractionOperationToOperation:forSession:)])
        operation = static_cast<UIDropOperation>([delegate _webView:self.webView willUpdateDataInteractionOperationToOperation:operation forSession:session]);

    auto proposal = adoptNS([[UIDropProposal alloc] initWithDropOperation:static_cast<UIDropOperation>(operation)]);
    auto dragHandlingMethod = _page->currentDragHandlingMethod();
    if (dragHandlingMethod == WebCore::DragHandlingMethod::EditPlainText || dragHandlingMethod == WebCore::DragHandlingMethod::EditRichText) {
        // When dragging near the top or bottom edges of an editable element, enabling precision drop mode may result in the drag session hit-testing outside of the editable
        // element, causing the drag to no longer be accepted. This in turn disables precision drop mode, which causes the drag session to hit-test inside of the editable
        // element again, which enables precision mode, thus continuing the cycle. To avoid precision mode thrashing, we forbid precision mode when dragging near the top or
        // bottom of the editable element.
        auto minimumDistanceFromVerticalEdgeForPreciseDrop = 25 / self.webView.scrollView.zoomScale;
        [proposal setPrecise:CGRectContainsPoint(CGRectInset(_page->currentDragCaretEditableElementRect(), 0, minimumDistanceFromVerticalEdgeForPreciseDrop), [session locationInView:self])];
    } else
        [proposal setPrecise:NO];

    if ([delegate respondsToSelector:@selector(_webView:willUpdateDropProposalToProposal:forSession:)])
        proposal = [delegate _webView:self.webView willUpdateDropProposalToProposal:proposal.get() forSession:session];

    return proposal.autorelease();
}

- (void)dropInteraction:(UIDropInteraction *)interaction sessionDidExit:(id<UIDropSession>)session
{
    RELEASE_LOG(DragAndDrop, "Drop session exited: %p with %tu items", session, session.items.count);
    [[WebItemProviderPasteboard sharedInstance] setItemProviders:extractItemProvidersFromDropSession(session) dropSession:session];

    auto dragData = [self dragDataForDropSession:session dragDestinationAction:WKDragDestinationActionAny];
    _page->dragExited(dragData);
    _page->resetCurrentDragInformation();

    _dragDropInteractionState.dropSessionDidExit();
}

- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session
{
    NSArray <NSItemProvider *> *itemProviders = extractItemProvidersFromDropSession(session);
    id<WKUIDelegatePrivate> uiDelegate = self.webViewUIDelegate;
    if ([uiDelegate respondsToSelector:@selector(_webView:performDataInteractionOperationWithItemProviders:)]) {
        if ([uiDelegate _webView:self.webView performDataInteractionOperationWithItemProviders:itemProviders])
            return;
    }

    if ([uiDelegate respondsToSelector:@selector(_webView:willPerformDropWithSession:)]) {
        itemProviders = extractItemProvidersFromDragItems([uiDelegate _webView:self.webView willPerformDropWithSession:session]);
        if (!itemProviders.count)
            return;
    }

    _dragDropInteractionState.dropSessionWillPerformDrop();

    [[WebItemProviderPasteboard sharedInstance] setItemProviders:itemProviders dropSession:session];
    [[WebItemProviderPasteboard sharedInstance] incrementPendingOperationCount];
    auto dragData = [self dragDataForDropSession:session dragDestinationAction:WKDragDestinationActionAny];
    BOOL shouldSnapshotView = ![self _handleDropByInsertingImagePlaceholders:itemProviders session:session];

    RELEASE_LOG(DragAndDrop, "Loading data from %tu item providers for session: %p", itemProviders.count, session);
    // Always loading content from the item provider ensures that the web process will be allowed to call back in to the UI
    // process to access pasteboard contents at a later time. Ideally, we only need to do this work if we're over a file input
    // or the page prevented default on `dragover`, but without this, dropping into a normal editable areas will fail due to
    // item providers not loading any data.
    RetainPtr<WKContentView> retainedSelf(self);
    [[WebItemProviderPasteboard sharedInstance] doAfterLoadingProvidedContentIntoFileURLs:[retainedSelf, capturedDragData = WTFMove(dragData), shouldSnapshotView] (NSArray *fileURLs) mutable {
        RELEASE_LOG(DragAndDrop, "Loaded data into %tu files", fileURLs.count);
        Vector<String> filenames;
        for (NSURL *fileURL in fileURLs)
            filenames.append([fileURL path]);
        capturedDragData.setFileNames(filenames);

        WebKit::SandboxExtensionHandle sandboxExtensionHandle;
        Vector<WebKit::SandboxExtensionHandle> sandboxExtensionForUpload;
        auto dragPasteboardName = WebCore::Pasteboard::nameOfDragPasteboard();
        retainedSelf->_page->createSandboxExtensionsIfNeeded(filenames, sandboxExtensionHandle, sandboxExtensionForUpload);
        retainedSelf->_page->performDragOperation(capturedDragData, dragPasteboardName, WTFMove(sandboxExtensionHandle), WTFMove(sandboxExtensionForUpload));
        if (shouldSnapshotView) {
            retainedSelf->_visibleContentViewSnapshot = [retainedSelf snapshotViewAfterScreenUpdates:NO];
            [retainedSelf->_visibleContentViewSnapshot setFrame:[retainedSelf bounds]];
            [retainedSelf addSubview:retainedSelf->_visibleContentViewSnapshot.get()];
        }
    }];
}

- (void)dropInteraction:(UIDropInteraction *)interaction item:(UIDragItem *)item willAnimateDropWithAnimator:(id<UIDragAnimating>)animator
{
    _dropAnimationCount++;
    [animator addCompletion:[strongSelf = retainPtr(self)] (UIViewAnimatingPosition) {
        if (!--strongSelf->_dropAnimationCount)
            [std::exchange(strongSelf->_unselectedContentSnapshot, nil) removeFromSuperview];
    }];
}

- (void)dropInteraction:(UIDropInteraction *)interaction concludeDrop:(id<UIDropSession>)session
{
    [self _removeContainerForDropPreviews];
    [std::exchange(_visibleContentViewSnapshot, nil) removeFromSuperview];
    [std::exchange(_unselectedContentSnapshot, nil) removeFromSuperview];
    _page->didConcludeDrop();
}

- (UITargetedDragPreview *)dropInteraction:(UIDropInteraction *)interaction previewForDroppingItem:(UIDragItem *)item withDefault:(UITargetedDragPreview *)defaultPreview
{
    if (auto preview = _dragDropInteractionState.finalDropPreview(item))
        return preview;

    _dragDropInteractionState.addDefaultDropPreview(item, defaultPreview);

    CGRect caretRect = _page->currentDragCaretRect();
    if (CGRectIsEmpty(caretRect))
        return nil;

    UIView *textEffectsWindow = self.textEffectsWindow;
    auto caretRectInWindowCoordinates = [self convertRect:caretRect toCoordinateSpace:textEffectsWindow];
    auto caretCenterInWindowCoordinates = CGPointMake(CGRectGetMidX(caretRectInWindowCoordinates), CGRectGetMidY(caretRectInWindowCoordinates));
    auto targetPreviewCenterInWindowCoordinates = CGPointMake(caretCenterInWindowCoordinates.x + defaultPreview.size.width / 2, caretCenterInWindowCoordinates.y + defaultPreview.size.height / 2);
    auto target = adoptNS([[UIDragPreviewTarget alloc] initWithContainer:textEffectsWindow center:targetPreviewCenterInWindowCoordinates transform:CGAffineTransformIdentity]);
    return [defaultPreview retargetedPreviewWithTarget:target.get()];
}

- (void)dropInteraction:(UIDropInteraction *)interaction sessionDidEnd:(id<UIDropSession>)session
{
    RELEASE_LOG(DragAndDrop, "Drop session ended: %p (performing operation: %d, began dragging: %d)", session, _dragDropInteractionState.isPerformingDrop(), _dragDropInteractionState.didBeginDragging());
    if (_dragDropInteractionState.isPerformingDrop()) {
        // In the case where we are performing a drop, wait until after the drop is handled in the web process to reset drag and drop interaction state.
        return;
    }

    if (_dragDropInteractionState.didBeginDragging()) {
        // In the case where the content view is a source of drag items, wait until -dragInteraction:session:didEndWithOperation: to reset drag and drop interaction state.
        return;
    }

    CGPoint global;
    CGPoint client;
    [self computeClientAndGlobalPointsForDropSession:session outClientPoint:&client outGlobalPoint:&global];
    [self cleanUpDragSourceSessionState];
    _page->dragEnded(WebCore::roundedIntPoint(client), WebCore::roundedIntPoint(global), { });
}

#endif // ENABLE(DRAG_SUPPORT)

#if HAVE(UITOOLTIPINTERACTION)
- (void)_toolTipChanged:(NSString *)newToolTip
{
    if (!_toolTip) {
        _toolTip = adoptNS([[UIToolTipInteraction alloc] init]);
        [_toolTip setDelegate:self];
        [self addInteraction:_toolTip.get()];
    }

    // FIXME: rdar://156729915 ([Catalyst] Cannot update tooltip interaction on the same view)
    [_toolTip setDefaultToolTip:newToolTip];
    RetainPtr sceneView = [[UINSSharedApplicationDelegate() hostWindowForUIWindow:[self window]] sceneView];
    [[sceneView _dynamicToolTipManager] windowChangedKeyState];
}

// MARK: UIToolTipInteractionDelegate

- (UIToolTipConfiguration *)toolTipInteraction:(UIToolTipInteraction *)interaction configurationAtPoint:(CGPoint)point
{
    return [UIToolTipConfiguration configurationWithToolTip:interaction.defaultToolTip];
}
#endif

#pragma mark - Model Interaction Support
#if ENABLE(MODEL_PROCESS)
- (void)modelInteractionPanGestureDidBeginAtPoint:(CGPoint)inputPoint
{
    if (!_stageModeSession) {
        _stageModeSession = { { } };
        _page->requestInteractiveModelElementAtPoint(WebCore::roundedIntPoint(inputPoint));
    }
}

- (void)modelInteractionPanGestureDidUpdateWithPoint:(CGPoint)inputPoint
{
    if (_stageModeSession && !_stageModeSession->isPreparingForInteraction) {
        WebCore::TransformationMatrix transform;
        transform.translate3d(inputPoint.x, inputPoint.y, 0);
        _stageModeSession->transform = transform;
        _page->stageModeSessionDidUpdate(_stageModeSession->nodeID, _stageModeSession->transform);
    }
}

- (void)modelInteractionPanGestureDidEnd
{
    if (_stageModeSession && !_stageModeSession->isPreparingForInteraction)
        _page->stageModeSessionDidEnd(_stageModeSession->nodeID);

    [self cleanUpStageModeSessionState];
}

- (void)didReceiveInteractiveModelElement:(std::optional<WebCore::NodeIdentifier>)nodeID
{
    if (!_stageModeSession || !_stageModeSession->isPreparingForInteraction)
        return;

    _stageModeSession->isPreparingForInteraction = !nodeID;
    _stageModeSession->nodeID = nodeID;
}

- (void)cleanUpStageModeSessionState
{
    _stageModeSession = std::nullopt;
}
#endif

- (void)cancelActiveTextInteractionGestures
{
    [self.textInteractionLoupeGestureRecognizer _wk_cancel];
}

- (UIView *)textEffectsWindow
{
    return [UITextEffectsWindow sharedTextEffectsWindowForWindowScene:self.window.windowScene];
}

- (NSDictionary *)_autofillContext
{
    if (!self._hasFocusedElement)
        return nil;

    auto context = adoptNS([[NSMutableDictionary alloc] init]);
    context.get()[@"_WKAutofillContextVersion"] = @(2);

    if (_focusRequiresStrongPasswordAssistance && _focusedElementInformation.elementType == WebKit::InputType::Password) {
        context.get()[@"_automaticPasswordKeyboard"] = @YES;
        context.get()[@"strongPasswordAdditionalContext"] = _additionalContextForStrongPasswordAssistance.get();
    } else if (_focusedElementInformation.acceptsAutofilledLoginCredentials)
        context.get()[@"_acceptsLoginCredentials"] = @YES;

    if (RetainPtr platformURL = _focusedElementInformation.representingPageURL.createNSURL())
        context.get()[@"_WebViewURL"] = platformURL.get();

    if (_focusedElementInformation.nonAutofillCredentialType == WebCore::NonAutofillCredentialType::WebAuthn) {
        context.get()[@"_page_id"] = [NSNumber numberWithUnsignedLong:_page->webPageIDInMainFrameProcess().toUInt64()];
        context.get()[@"_frame_id"] = [NSNumber numberWithUnsignedLong:_focusedElementInformation.frame ? _focusedElementInformation.frame->frameID.toUInt64() : 0];
        context.get()[@"_credential_type"] = WebCore::nonAutofillCredentialTypeString(_focusedElementInformation.nonAutofillCredentialType).createNSString().get();
    }
    return context.autorelease();
}

#if USE(UICONTEXTMENU)

static RetainPtr<UIImage> uiImageForImage(WebCore::Image* image)
{
    if (!image)
        return nil;

    auto nativeImage = image->nativeImage();
    if (!nativeImage)
        return nil;

    return adoptNS([[UIImage alloc] initWithCGImage:nativeImage->platformImage().get()]);
}

// FIXME: This should be merged with createTargetedDragPreview in DragDropInteractionState.
static RetainPtr<UITargetedPreview> createTargetedPreview(UIImage *image, UIView *rootView, UIView *previewContainer, const WebCore::FloatRect& frameInRootViewCoordinates, const Vector<WebCore::FloatRect>& clippingRectsInFrameCoordinates, UIColor *backgroundColor)
{
    if (frameInRootViewCoordinates.isEmpty() || !image || !previewContainer.window)
        return nil;

    WebCore::FloatRect frameInContainerCoordinates = [rootView convertRect:frameInRootViewCoordinates toView:previewContainer];
    if (frameInContainerCoordinates.isEmpty())
        return nil;

    auto scalingRatio = frameInContainerCoordinates.size() / frameInRootViewCoordinates.size();
    auto clippingRectValuesInFrameCoordinates = createNSArray(clippingRectsInFrameCoordinates, [&] (WebCore::FloatRect rect) {
        rect.scale(scalingRatio);
        return [NSValue valueWithCGRect:rect];
    });

    RetainPtr<UIPreviewParameters> parameters;
    if ([clippingRectValuesInFrameCoordinates count])
        parameters = adoptNS([[UIPreviewParameters alloc] initWithTextLineRects:clippingRectValuesInFrameCoordinates.get()]);
    else
        parameters = adoptNS([[UIPreviewParameters alloc] init]);

    [parameters setBackgroundColor:(backgroundColor ?: [UIColor clearColor])];

    CGPoint centerInContainerCoordinates = { CGRectGetMidX(frameInContainerCoordinates), CGRectGetMidY(frameInContainerCoordinates) };
    auto target = adoptNS([[UIPreviewTarget alloc] initWithContainer:previewContainer center:centerInContainerCoordinates]);

    auto imageView = adoptNS([[UIImageView alloc] initWithImage:image]);
    [imageView setFrame:frameInContainerCoordinates];
    return adoptNS([[UITargetedPreview alloc] initWithView:imageView.get() parameters:parameters.get() target:target.get()]);
}

- (UITargetedPreview *)_createTargetedPreviewFromTextIndicator:(RefPtr<WebCore::TextIndicator>&&)textIndicator previewContainer:(UIView *)previewContainer
{
    if (!textIndicator)
        return nil;

    RetainPtr textIndicatorImage = uiImageForImage(textIndicator->contentImage());
    RetainPtr preview = createTargetedPreview(textIndicatorImage.get(), self, previewContainer, textIndicator->textBoundingRectInRootViewCoordinates(), textIndicator->textRectsInBoundingRectCoordinates(), ^{
        if (textIndicator->estimatedBackgroundColor() != WebCore::Color::transparentBlack)
            return cocoaColor(textIndicator->estimatedBackgroundColor()).autorelease();

        // In the case where background color estimation fails, it doesn't make sense to
        // show a text indicator preview with a clear background in light mode. Default
        // to the system background color instead.
        return UIColor.systemBackgroundColor;
    }());

    return preview.autorelease();
}

static RetainPtr<UITargetedPreview> createFallbackTargetedPreview(UIView *rootView, UIView *containerView, const WebCore::FloatRect& frameInRootViewCoordinates, UIColor *backgroundColor)
{
    if (!containerView.window)
        return nil;

    if (frameInRootViewCoordinates.isEmpty())
        return nil;

    auto parameters = adoptNS([[UIPreviewParameters alloc] init]);
    if (backgroundColor)
        [parameters setBackgroundColor:backgroundColor];

    RetainPtr snapshotView = [rootView resizableSnapshotViewFromRect:frameInRootViewCoordinates afterScreenUpdates:NO withCapInsets:UIEdgeInsetsZero];
    if (!snapshotView)
        snapshotView = adoptNS([UIView new]);

    CGRect frameInContainerViewCoordinates = [rootView convertRect:frameInRootViewCoordinates toView:containerView];

    if (CGRectIsEmpty(frameInContainerViewCoordinates))
        return nil;

    [snapshotView setFrame:frameInContainerViewCoordinates];

    CGPoint centerInContainerViewCoordinates = CGPointMake(CGRectGetMidX(frameInContainerViewCoordinates), CGRectGetMidY(frameInContainerViewCoordinates));
    auto target = adoptNS([[UIPreviewTarget alloc] initWithContainer:containerView center:centerInContainerViewCoordinates]);

    return adoptNS([[UITargetedPreview alloc] initWithView:snapshotView.get() parameters:parameters.get() target:target.get()]);
}

- (UITargetedPreview *)_createTargetedContextMenuHintPreviewForFocusedElement:(WebKit::TargetedPreviewPositioning)positioning
{
    auto backgroundColor = [&]() -> UIColor * {
        switch (_focusedElementInformation.elementType) {
        case WebKit::InputType::Date:
        case WebKit::InputType::Month:
        case WebKit::InputType::DateTimeLocal:
        case WebKit::InputType::Time:
#if ENABLE(INPUT_TYPE_WEEK_PICKER)
        case WebKit::InputType::Week:
#endif
            return UIColor.clearColor;
        default:
            return nil;
        }
    }();

    auto previewRect = _focusedElementInformation.interactionRect;
    if (positioning == WebKit::TargetedPreviewPositioning::LeadingOrTrailingEdge) {
        static constexpr auto defaultMenuWidth = 250;
        auto unobscuredRect = WebCore::IntRect { self.unobscuredContentRect };
        std::optional<int> previewOffsetX;

        auto leftEdge = previewRect.x() - defaultMenuWidth;
        bool hasSpaceAfterRightEdge = previewRect.maxX() + defaultMenuWidth <= unobscuredRect.maxX();
        bool hasSpaceBeforeLeftEdge = leftEdge > unobscuredRect.x();

        switch (self.effectiveUserInterfaceLayoutDirection) {
        case UIUserInterfaceLayoutDirectionLeftToRight:
            if (hasSpaceAfterRightEdge)
                previewOffsetX = previewRect.maxX();
            else if (hasSpaceBeforeLeftEdge)
                previewOffsetX = leftEdge;
            break;
        case UIUserInterfaceLayoutDirectionRightToLeft:
            if (hasSpaceBeforeLeftEdge)
                previewOffsetX = leftEdge;
            else if (hasSpaceAfterRightEdge)
                previewOffsetX = previewRect.maxX();
            break;
        }

        if (previewOffsetX) {
            static constexpr auto additionalOffsetToDockPresentedMenuToEdge = 20;
            previewRect.setX(additionalOffsetToDockPresentedMenuToEdge + *previewOffsetX);
            previewRect.setWidth(1);
        }
    }

    auto targetedPreview = createFallbackTargetedPreview(self, self.containerForContextMenuHintPreviews, previewRect, backgroundColor);

    [self _updateTargetedPreviewScrollViewUsingContainerScrollingNodeID:_focusedElementInformation.containerScrollingNodeID];

    _contextMenuInteractionTargetedPreview = WTFMove(targetedPreview);
    return _contextMenuInteractionTargetedPreview.get();
}

- (BOOL)positionInformationHasImageOverlayDataDetector
{
#if ENABLE(DATA_DETECTION)
    return _positionInformation.isImageOverlayText && [_positionInformation.dataDetectorResults count];
#else
    return NO;
#endif
}

- (UITargetedPreview *)_createTargetedContextMenuHintPreviewIfPossible
{
    RetainPtr<UITargetedPreview> targetedPreview;

    if (_positionInformation.isLink && _positionInformation.textIndicator && _positionInformation.textIndicator->contentImage()) {
        RefPtr textIndicator = _positionInformation.textIndicator;
        _positionInformationLinkIndicator = textIndicator ? std::optional { textIndicator->data() } : std::nullopt;

        targetedPreview = [self _createTargetedPreviewFromTextIndicator:WTFMove(textIndicator) previewContainer:self.containerForContextMenuHintPreviews];
    } else if ((_positionInformation.isAttachment || _positionInformation.isImage) && _positionInformation.image) {
        auto cgImage = _positionInformation.image->makeCGImageCopy();
        auto image = adoptNS([[UIImage alloc] initWithCGImage:cgImage.get()]);
        targetedPreview = createTargetedPreview(image.get(), self, self.containerForContextMenuHintPreviews, _positionInformation.bounds, { }, nil);
    }

    if (!targetedPreview) {
        auto boundsForFallbackPreview = [&] {
#if ENABLE(DATA_DETECTION)
            if (self.positionInformationHasImageOverlayDataDetector)
                return _positionInformation.dataDetectorBounds;
#endif
            return _positionInformation.bounds;
        }();

        targetedPreview = createFallbackTargetedPreview(self, self.containerForContextMenuHintPreviews, boundsForFallbackPreview, nil);
    }

    [self _updateTargetedPreviewScrollViewUsingContainerScrollingNodeID:_positionInformation.containerScrollingNodeID];

    _contextMenuInteractionTargetedPreview = WTFMove(targetedPreview);
    return _contextMenuInteractionTargetedPreview.get();
}

- (void)_removeContextMenuHintContainerIfPossible
{
#if HAVE(LINK_PREVIEW)
    // If a new _contextMenuElementInfo is installed, we've started another interaction,
    // and removing the hint container view will cause the animation to break.
    if (_contextMenuElementInfo)
        return;
#endif
    if (_isDisplayingContextMenuWithAnimation)
        return;
#if ENABLE(DATA_DETECTION)
    // We are also using this container for the action sheet assistant...
    if ([_actionSheetAssistant hasContextMenuInteraction])
        return;
#endif
    // and for the file upload panel...
    if (_fileUploadPanel)
        return;

    // and for the date/time picker.
    if ([self dateTimeInputControl])
        return;

    if ([self selectControl])
        return;

    if ([_contextMenuHintContainerView subviews].count)
        return;

    [self _removeContainerForContextMenuHintPreviews];
}

#endif // USE(UICONTEXTMENU)

#if HAVE(UI_WK_DOCUMENT_CONTEXT)

static inline OptionSet<WebKit::DocumentEditingContextRequest::Options> toWebDocumentRequestOptions(WKBETextDocumentRequestOptions flags)
{
    OptionSet<WebKit::DocumentEditingContextRequest::Options> options;

    if (flags & WKBETextDocumentRequestOptionText)
        options.add(WebKit::DocumentEditingContextRequest::Options::Text);
    if (flags & WKBETextDocumentRequestOptionAttributedText)
        options.add(WebKit::DocumentEditingContextRequest::Options::AttributedText);
    if (flags & WKBETextDocumentRequestOptionTextRects)
        options.add(WebKit::DocumentEditingContextRequest::Options::Rects);
    if (flags & UIWKDocumentRequestSpatial)
        options.add(WebKit::DocumentEditingContextRequest::Options::Spatial);
    if (flags & UIWKDocumentRequestAnnotation)
        options.add(WebKit::DocumentEditingContextRequest::Options::Annotation);
    if (flags & WKBETextDocumentRequestOptionMarkedTextRects)
        options.add(WebKit::DocumentEditingContextRequest::Options::MarkedTextRects);
    if (flags & UIWKDocumentRequestSpatialAndCurrentSelection)
        options.add(WebKit::DocumentEditingContextRequest::Options::SpatialAndCurrentSelection);
#if HAVE(AUTOCORRECTION_ENHANCEMENTS)
    if (flags & WKBETextDocumentRequestOptionAutocorrectedRanges)
        options.add(WebKit::DocumentEditingContextRequest::Options::AutocorrectedRanges);
#endif

    return options;
}

static WebKit::DocumentEditingContextRequest toWebRequest(id request)
{
    WKBETextDocumentRequestOptions options = WKBETextDocumentRequestOptionNone;
    CGRect documentRect;

    if (auto uiRequest = dynamic_objc_cast<UIWKDocumentRequest>(request)) {
        documentRect = uiRequest.documentRect;
        options = static_cast<WKBETextDocumentRequestOptions>(uiRequest.flags);
    }
#if USE(BROWSERENGINEKIT)
    else if (auto seRequest = dynamic_objc_cast<WKBETextDocumentRequest>(request)) {
        documentRect = seRequest._documentRect;
        options = seRequest.options;
    }
#endif
    WebKit::DocumentEditingContextRequest webRequest = {
        .options = toWebDocumentRequestOptions(options),
        .surroundingGranularity = toWKTextGranularity([request surroundingGranularity]),
        .granularityCount = [request granularityCount],
        .rect = documentRect
    };
    if (auto textInputContext = dynamic_objc_cast<_WKTextInputContext>([request inputElementIdentifier]))
        webRequest.textInputContext = [textInputContext _textInputContext];

    return webRequest;
}

- (void)adjustSelectionWithDelta:(NSRange)deltaRange completionHandler:(void (^)(void))completionHandler
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    // UIKit is putting casted signed integers into NSRange. Cast them back to reveal any negative values.
    [self _internalAdjustSelectionWithOffset:static_cast<NSInteger>(deltaRange.location) lengthDelta:static_cast<NSInteger>(deltaRange.length) completionHandler:completionHandler];
}

// The completion handler is called with either a UIWKDocumentContext, or a WKBETextDocumentContext.
- (void)requestDocumentContext:(id)request completionHandler:(void (^)(NSObject *))completionHandler
{
    auto webRequest = toWebRequest(request);
    auto options = webRequest.options;
    _page->requestDocumentEditingContext(WTFMove(webRequest), [useAsyncInteractions = self.shouldUseAsyncInteractions, completionHandler = makeBlockPtr(completionHandler), options] (auto&& editingContext) {
        completionHandler(useAsyncInteractions ? editingContext.toPlatformContext(options) : editingContext.toLegacyPlatformContext(options));
    });
}

- (void)selectPositionAtPoint:(CGPoint)point withContextRequest:(WKBETextDocumentRequest *)request completionHandler:(void (^)(NSObject *))completionHandler
{
    logTextInteraction(__PRETTY_FUNCTION__, self.textInteractionLoupeGestureRecognizer, point);

    // FIXME: Reduce to 1 message.
    [self selectPositionAtPoint:point completionHandler:[strongSelf = retainPtr(self), request = retainPtr(request), completionHandler = makeBlockPtr(completionHandler)]() mutable {
        [strongSelf requestDocumentContext:request.get() completionHandler:[completionHandler = WTFMove(completionHandler)](NSObject *context) {
            completionHandler(context);
        }];
    }];
}

#endif // HAVE(UI_WK_DOCUMENT_CONTEXT)

#if HAVE(PEPPER_UI_CORE)

- (void)dismissQuickboardViewControllerAndRevealFocusedFormOverlayIfNecessary:(id)controller
{
    BOOL shouldRevealFocusOverlay = NO;
    // In the case where there's nothing the user could potentially do besides dismiss the overlay, we can just automatically without asking the delegate.
    if ([self.webView._inputDelegate respondsToSelector:@selector(_webView:shouldRevealFocusOverlayForInputSession:)]
        && ([self actionNameForFocusedFormControlView:_focusedFormControlView.get()] || _focusedElementInformation.hasNextNode || _focusedElementInformation.hasPreviousNode))
        shouldRevealFocusOverlay = [self.webView._inputDelegate _webView:self.webView shouldRevealFocusOverlayForInputSession:_formInputSession.get()];

    if (shouldRevealFocusOverlay) {
        [_focusedFormControlView show:NO];
        [self updateCurrentFocusedElementInformation:[weakSelf = WeakObjCPtr<WKContentView>(self)] (bool didUpdate) {
            if (!didUpdate)
                return;

            auto focusedFormController = weakSelf.get()->_focusedFormControlView;
            [focusedFormController reloadData:YES];
            [focusedFormController engageFocusedFormControlNavigation];
        }];
    } else
        _page->blurFocusedElement();

    bool shouldDismissViewController = [controller isKindOfClass:UIViewController.class]
#if HAVE(QUICKBOARD_CONTROLLER)
        && !_presentedQuickboardController
#endif
        && controller != _presentedFullScreenInputViewController;
    // The Quickboard view controller passed into this delegate method is not necessarily the view controller we originally presented;
    // this happens in the case when the user chooses an input method (e.g. scribble) and a new Quickboard view controller is presented.
    if (shouldDismissViewController)
        [(UIViewController *)controller dismissViewControllerAnimated:YES completion:nil];

    [self dismissAllInputViewControllers:controller == _presentedFullScreenInputViewController];
}

- (UITextContentType)textContentTypeForQuickboard
{
    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::Password:
        return UITextContentTypePassword;
    case WebKit::InputType::URL:
        return UITextContentTypeURL;
    case WebKit::InputType::Email:
        return UITextContentTypeEmailAddress;
    case WebKit::InputType::Phone:
        return UITextContentTypeTelephoneNumber;
    default:
        // The element type alone is insufficient to infer content type; fall back to autofill data.
        if (auto contentType = [self contentTypeFromFieldName:_focusedElementInformation.autofillFieldName])
            return contentType;

        if (_focusedElementInformation.isAutofillableUsernameField)
            return UITextContentTypeUsername;

        return nil;
    }
}

#pragma mark - PUICQuickboardViewControllerDelegate

- (void)quickboard:(PUICQuickboardViewController *)quickboard textEntered:(NSAttributedString *)attributedText
{
    if (attributedText)
        _page->setTextAsync(attributedText.string);

    [self dismissQuickboardViewControllerAndRevealFocusedFormOverlayIfNecessary:quickboard];
}

- (void)quickboardInputCancelled:(PUICQuickboardViewController *)quickboard
{
    [self dismissQuickboardViewControllerAndRevealFocusedFormOverlayIfNecessary:quickboard];
}

#pragma mark - WKQuickboardViewControllerDelegate

- (BOOL)allowsLanguageSelectionForListViewController:(PUICQuickboardViewController *)controller
{
    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::ContentEditable:
    case WebKit::InputType::Text:
    case WebKit::InputType::TextArea:
    case WebKit::InputType::Search:
    case WebKit::InputType::Email:
    case WebKit::InputType::URL:
        return YES;
    default:
        return NO;
    }
}

- (UIView *)inputContextViewForViewController:(PUICQuickboardViewController *)controller
{
    id <_WKInputDelegate> delegate = self.webView._inputDelegate;
    if (![delegate respondsToSelector:@selector(_webView:focusedElementContextViewForInputSession:)])
        return nil;

    return [delegate _webView:self.webView focusedElementContextViewForInputSession:_formInputSession.get()];
}

- (NSString *)inputLabelTextForViewController:(PUICQuickboardViewController *)controller
{
    return [self inputLabelText];
}

- (NSString *)initialValueForViewController:(PUICQuickboardViewController *)controller
{
    return _focusedElementInformation.value.createNSString().autorelease();
}

- (BOOL)shouldDisplayInputContextViewForListViewController:(PUICQuickboardViewController *)controller
{
    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::ContentEditable:
    case WebKit::InputType::Text:
    case WebKit::InputType::Password:
    case WebKit::InputType::TextArea:
    case WebKit::InputType::Search:
    case WebKit::InputType::Email:
    case WebKit::InputType::URL:
    case WebKit::InputType::Phone:
        return YES;
    default:
        return NO;
    }
}

#pragma mark - WKTextInputListViewControllerDelegate

- (WKNumberPadInputMode)numericInputModeForListViewController:(WKTextInputListViewController *)controller
{
    switch (_focusedElementInformation.elementType) {
    case WebKit::InputType::Phone:
        return WKNumberPadInputModeTelephone;
    case WebKit::InputType::Number:
        return WKNumberPadInputModeNumbersAndSymbols;
    case WebKit::InputType::NumberPad:
        return WKNumberPadInputModeNumbersOnly;
    default:
        return WKNumberPadInputModeNone;
    }
}

- (PUICTextInputContext *)textInputContextForListViewController:(WKTextInputListViewController *)controller
{
    return self.createQuickboardTextInputContext.autorelease();
}

- (BOOL)allowsDictationInputForListViewController:(PUICQuickboardViewController *)controller
{
    return _focusedElementInformation.elementType != WebKit::InputType::Password;
}

#endif // HAVE(PEPPER_UI_CORE)

#if HAVE(LOOKUP_GESTURE_RECOGNIZER)
- (void)_lookupGestureRecognized:(UIGestureRecognizer *)gestureRecognizer
{
    NSPoint locationInViewCoordinates = [gestureRecognizer locationInView:self];
    _page->performDictionaryLookupAtLocation(WebCore::FloatPoint(locationInViewCoordinates));
}
#endif

- (void)buildMenuForWebViewWithBuilder:(id <UIMenuBuilder>)builder
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if (auto menu = self.removeBackgroundMenu)
        [builder insertSiblingMenu:menu beforeMenuForIdentifier:UIMenuFormat];
#endif

#if ENABLE(APP_HIGHLIGHTS)
    if (auto menu = self.appHighlightMenu)
        [builder insertChildMenu:menu atEndOfMenuForIdentifier:UIMenuRoot];
#endif

    if (auto menu = self.scrollToTextFragmentGenerationMenu)
        [builder insertSiblingMenu:menu beforeMenuForIdentifier:UIMenuShare];
}

- (UIMenu *)menuWithInlineAction:(NSString *)title image:(UIImage *)image identifier:(NSString *)identifier handler:(Function<void(WKContentView *)>&&)handler
{
    auto action = [UIAction actionWithTitle:title image:image identifier:identifier handler:makeBlockPtr([handler = WTFMove(handler), weakSelf = WeakObjCPtr<WKContentView>(self)](UIAction *) mutable {
        if (auto strongSelf = weakSelf.get())
            handler(strongSelf.get());
    }).get()];
    return [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:@[ action ]];
}

#if ENABLE(APP_HIGHLIGHTS)

- (UIMenu *)appHighlightMenu
{
    if (!_page->preferences().appHighlightsEnabled() || !_page->editorState().selectionIsRange || !self.shouldAllowHighlightLinkCreation)
        return nil;

    bool isVisible = _page->appHighlightsVisibility();
    auto title = isVisible ? WebCore::contextMenuItemTagAddHighlightToCurrentQuickNote() : WebCore::contextMenuItemTagAddHighlightToNewQuickNote();
    return [self menuWithInlineAction:title.createNSString().get() image:[UIImage _systemImageNamed:@"quicknote"] identifier:@"WKActionCreateQuickNote" handler:[isVisible](WKContentView *view) mutable {
        view->_page->createAppHighlightInSelectedRange(isVisible ? WebCore::CreateNewGroupForHighlight::No : WebCore::CreateNewGroupForHighlight::Yes, WebCore::HighlightRequestOriginatedInApp::No);
    }];
}

#endif // ENABLE(APP_HIGHLIGHTS)

- (UIMenu *)scrollToTextFragmentGenerationMenu
{
    if (!_page->preferences().scrollToTextFragmentGenerationEnabled() || !self.shouldAllowHighlightLinkCreation)
        return nil;

    return [self menuWithInlineAction:WebCore::contextMenuItemTagCopyLinkWithHighlight().createNSString().get() image:[UIImage systemImageNamed:@"text.quote"] identifier:@"WKActionScrollToTextFragmentGeneration" handler:[](WKContentView *view) mutable {
        view->_page->copyLinkWithHighlight();
    }];
}

- (void)setContinuousSpellCheckingEnabled:(BOOL)enabled
{
    if (WebKit::TextChecker::setContinuousSpellCheckingEnabled(enabled))
        _page->legacyMainFrameProcess().updateTextCheckerState();
}

- (void)setGrammarCheckingEnabled:(BOOL)enabled
{
    if (static_cast<bool>(enabled) == WebKit::TextChecker::state().contains(WebKit::TextCheckerState::GrammarCheckingEnabled))
        return;

    WebKit::TextChecker::setGrammarCheckingEnabled(enabled);
    _page->legacyMainFrameProcess().updateTextCheckerState();
}

#if HAVE(UIKIT_WITH_MOUSE_SUPPORT)

- (BOOL)shouldUseMouseGestureRecognizer
{
    static const BOOL shouldUseMouseGestureRecognizer = []() -> BOOL {
        // <rdar://problem/59521967> iAd Video does not respond to mouse events, only touch events
        if (WTF::IOSApplication::isNews() || WTF::IOSApplication::isStocks())
            return NO;

        if (!linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::SendsNativeMouseEvents)
            && WTF::IOSApplication::isEssentialSkeleton()) { // <rdar://problem/62694519>
            os_log_error(OS_LOG_DEFAULT, "WARNING: This application has been observed to ignore mouse events in web content; touch events will be sent until it is built against the iOS 13.4 SDK, but after that, the web content must respect mouse or pointer events in addition to touch events in order to behave correctly when a trackpad or mouse is used.");
            return NO;
        }

        return YES;
    }();

    switch (_mouseEventPolicy) {
    case WebCore::MouseEventPolicy::Default:
        break;
#if ENABLE(IOS_TOUCH_EVENTS)
    case WebCore::MouseEventPolicy::SynthesizeTouchEvents:
        return NO;
#endif
    }

    return shouldUseMouseGestureRecognizer;
}

- (void)setUpMouseGestureRecognizer
{
    if (_mouseInteraction)
        [self removeInteraction:_mouseInteraction.get()];

    _mouseInteraction = adoptNS([[WKMouseInteraction alloc] initWithDelegate:self]);
    [self addInteraction:_mouseInteraction.get()];

    [self _configureMouseGestureRecognizer];
}

- (void)mouseInteraction:(WKMouseInteraction *)interaction changedWithEvent:(const WebKit::NativeWebMouseEvent&)event
{
    if (!_page->hasRunningProcess())
        return;

    if (event.type() == WebKit::WebEventType::MouseDown) {
        _layerTreeTransactionIdAtLastInteractionStart = downcast<WebKit::RemoteLayerTreeDrawingAreaProxy>(*_page->drawingArea()).lastCommittedMainFrameLayerTreeTransactionID();

        if (auto lastLocation = interaction.lastLocation)
            _lastInteractionLocation = *lastLocation;
    } else if (event.type() == WebKit::WebEventType::MouseUp) {
        _usingMouseDragForSelection = NO;

        if (self.hasHiddenContentEditable && self._hasFocusedElement && !self.window.keyWindow)
            [self.window makeKeyWindow];
    }

    _page->handleMouseEvent(event);
}

- (void)_configureMouseGestureRecognizer
{
    [_mouseInteraction setEnabled:self.shouldUseMouseGestureRecognizer];
}

- (void)_setMouseEventPolicy:(WebCore::MouseEventPolicy)policy
{
    _mouseEventPolicy = policy;
    [self _configureMouseGestureRecognizer];
}

#endif // HAVE(UIKIT_WITH_MOUSE_SUPPORT)

#if ENABLE(MEDIA_CONTROLS_CONTEXT_MENUS) && USE(UICONTEXTMENU)

- (void)_showMediaControlsContextMenu:(WebCore::FloatRect&&)targetFrame items:(Vector<WebCore::MediaControlsContextMenuItem>&&)items completionHandler:(CompletionHandler<void(WebCore::MediaControlsContextMenuItem::ID)>&&)completionHandler
{
    [_actionSheetAssistant showMediaControlsContextMenu:WTFMove(targetFrame) items:WTFMove(items) completionHandler:WTFMove(completionHandler)];
}

#endif // ENABLE(MEDIA_CONTROLS_CONTEXT_MENUS) && USE(UICONTEXTMENU)

#if HAVE(UI_POINTER_INTERACTION)

- (void)setUpPointerInteraction
{
    _pointerInteraction = adoptNS([[UIPointerInteraction alloc] initWithDelegate:self]);
    [self addInteraction:_pointerInteraction.get()];
}

- (UIPointerRegion *)pointerInteraction:(UIPointerInteraction *)interaction regionForRequest:(UIPointerRegionRequest *)request defaultRegion:(UIPointerRegion *)defaultRegion
{
    [self _updateLastPointerRegionIfNeeded:request];
    return _lastPointerRegion.get();
}

- (void)_updateLastPointerRegionIfNeeded:(UIPointerRegionRequest *)request
{
    WebKit::InteractionInformationRequest interactionInformationRequest;
    interactionInformationRequest.point = WebCore::roundedIntPoint(request.location);
    interactionInformationRequest.includeCursorContext = true;
    interactionInformationRequest.includeHasDoubleClickHandler = false;

    if ([self _currentPositionInformationIsValidForRequest:interactionInformationRequest]) {
        _lastPointerRegion = [self pointerRegionForPositionInformation:_positionInformation point:request.location];
        [_webView _setPointerTouchCompatibilitySimulatorEnabled:_positionInformation.needsPointerTouchCompatibilityQuirk];
        _pointerInteractionRegionNeedsUpdate = NO;
        return;
    }

    // Note: In this case where position information is not up-to-date, checking the last cached
    // position information may not yield expected results, since the client (or any other code in
    // WebKit) may have queried position information at a totally different location, for unrelated
    // reasons. However, this seems to work well in practice, since pointer updates are very frequent,
    // so any invalid state would only persist for a single frame.
    if (self.webView._editable && !_positionInformation.cursorContext.shouldNotUseIBeamInEditableContent) {
        auto lastRegionRect = _lastPointerRegion ? [_lastPointerRegion rect] : self.bounds;
        _lastPointerRegion = [UIPointerRegion regionWithRect:lastRegionRect identifier:editablePointerRegionIdentifier];
    }

    if (_pointerInteractionRegionNeedsUpdate)
        return;

    _pointerInteractionRegionNeedsUpdate = YES;

    [self doAfterPositionInformationUpdate:[weakSelf = WeakObjCPtr<WKContentView>(self), location = request.location](WebKit::InteractionInformationAtPosition information) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return;

        strongSelf->_pointerInteractionRegionNeedsUpdate = NO;
        strongSelf->_lastPointerRegion = [strongSelf pointerRegionForPositionInformation:information point:location];
        [strongSelf->_webView _setPointerTouchCompatibilitySimulatorEnabled:information.needsPointerTouchCompatibilityQuirk];
        [strongSelf->_pointerInteraction invalidate];
    } forRequest:interactionInformationRequest];
}

- (UIPointerRegion *)pointerRegionForPositionInformation:(const WebKit::InteractionInformationAtPosition&)interactionInformation point:(CGPoint)location
{
    WebCore::FloatRect expandedLineRect = enclosingIntRect(interactionInformation.cursorContext.lineCaretExtent);

    // Pad lines of text in order to avoid switching back to the dot cursor between lines.
    // This matches the value that UIKit uses.
    // FIXME: Should this account for vertical writing mode?
    expandedLineRect.inflateY(10);

    if (auto cursor = interactionInformation.cursorContext.cursor) {
        auto cursorType = cursor->type();
        if (cursorType == WebCore::Cursor::Type::Hand)
            return [UIPointerRegion regionWithRect:interactionInformation.bounds identifier:pointerRegionIdentifier];

        if (cursorType == WebCore::Cursor::Type::IBeam && expandedLineRect.contains(location))
            return [UIPointerRegion regionWithRect:expandedLineRect identifier:pointerRegionIdentifier];
    }

    if (self.webView._editable) {
        if (expandedLineRect.contains(location))
            return [UIPointerRegion regionWithRect:expandedLineRect identifier:pointerRegionIdentifier];
        return [UIPointerRegion regionWithRect:self.bounds identifier:editablePointerRegionIdentifier];
    }

    return [UIPointerRegion regionWithRect:self.bounds identifier:pointerRegionIdentifier];
}

- (UIPointerStyle *)pointerInteraction:(UIPointerInteraction *)interaction styleForRegion:(UIPointerRegion *)region
{
    double scaleFactor = self._contentZoomScale;

    auto lineCaretExtent = _positionInformation.cursorContext.lineCaretExtent;
    UIPointerStyle *(^iBeamCursor)(void) = ^{
        bool isVertical = _positionInformation.cursorContext.isVerticalWritingMode;
        float beamLength = (isVertical ? lineCaretExtent.width() : lineCaretExtent.height()) * scaleFactor;
        auto axisOrientation = isVertical ? UIAxisHorizontal : UIAxisVertical;
        auto iBeamConstraintAxes = isVertical ? UIAxisHorizontal : UIAxisVertical;
        auto regionLengthInBlockAxis = isVertical ? CGRectGetWidth(region.rect) : CGRectGetHeight(region.rect);

        // If the I-beam is so large that the magnetism is hard to fight, we should not apply any magnetism.
        static constexpr auto maximumBeamSnappingLength = 100;
        if (beamLength > maximumBeamSnappingLength || regionLengthInBlockAxis > maximumBeamSnappingLength)
            iBeamConstraintAxes = UIAxisNeither;

        // If the region is the size of the view, we should not apply any magnetism.
        if ([region.identifier isEqual:editablePointerRegionIdentifier])
            iBeamConstraintAxes = UIAxisNeither;

        return [UIPointerStyle styleWithShape:[UIPointerShape beamWithPreferredLength:beamLength axis:axisOrientation] constrainedAxes:iBeamConstraintAxes];
    };

    if (self.webView._editable) {
        if (_positionInformation.cursorContext.shouldNotUseIBeamInEditableContent)
            return [UIPointerStyle systemPointerStyle];
        return iBeamCursor();
    }

    if (auto cursor = _positionInformation.cursorContext.cursor; cursor && [region.identifier isEqual:pointerRegionIdentifier]) {
        auto cursorType = cursor->type();
        if (cursorType == WebCore::Cursor::Type::Hand)
            return [UIPointerStyle systemPointerStyle];

        if (cursorType == WebCore::Cursor::Type::IBeam && lineCaretExtent.contains(_positionInformation.request.point))
            return iBeamCursor();
    }

    return [UIPointerStyle systemPointerStyle];
}

#endif // HAVE(UI_POINTER_INTERACTION)

#if HAVE(PENCILKIT_TEXT_INPUT)

- (void)setUpScribbleInteraction
{
    _scribbleInteraction = adoptNS([[UIIndirectScribbleInteraction alloc] initWithDelegate:self]);
    [self addInteraction:_scribbleInteraction.get()];
}

- (void)cleanUpScribbleInteraction
{
    [self removeInteraction:_scribbleInteraction.get()];
    _scribbleInteraction = nil;
}

- (_WKTextInputContext *)_textInputContextByScribbleIdentifier:(UIScribbleElementIdentifier)identifier
{
    _WKTextInputContext *textInputContext = (_WKTextInputContext *)identifier;
    if (![textInputContext isKindOfClass:_WKTextInputContext.class])
        return nil;
    auto elementContext = textInputContext._textInputContext;
    if (elementContext.webPageIdentifier != _page->webPageIDInMainFrameProcess())
        return nil;
    return textInputContext;
}

- (BOOL)_elementForTextInputContextIsFocused:(_WKTextInputContext *)context
{
    // We ignore bounding rect changes as the bounding rect of the focused element is not kept up-to-date.
    return self._hasFocusedElement && context && context._textInputContext.isSameElement(_focusedElementInformation.elementContext);
}

- (void)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction requestElementsInRect:(CGRect)rect completion:(void(^)(NSArray<UIScribbleElementIdentifier> *))completion
{
    ASSERT(_scribbleInteraction.get() == interaction);
    [self _requestTextInputContextsInRect:rect completionHandler:completion];
}

- (BOOL)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction isElementFocused:(UIScribbleElementIdentifier)identifier
{
    ASSERT(_scribbleInteraction.get() == interaction);
    return [self _elementForTextInputContextIsFocused:[self _textInputContextByScribbleIdentifier:identifier]];
}

- (void)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction focusElementIfNeeded:(UIScribbleElementIdentifier)identifier referencePoint:(CGPoint)initialPoint completion:(void (^)(UIResponder<UITextInput> *))completionBlock
{
    ASSERT(_scribbleInteraction.get() == interaction);
    if (auto *textInputContext = [self _textInputContextByScribbleIdentifier:identifier])
        [self _focusTextInputContext:textInputContext placeCaretAt:initialPoint completionHandler:completionBlock];
    else
        completionBlock(nil);
}

- (CGRect)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction frameForElement:(UIScribbleElementIdentifier)identifier
{
    ASSERT(_scribbleInteraction.get() == interaction);
    auto *textInputContext = [self _textInputContextByScribbleIdentifier:identifier];
    return textInputContext ? textInputContext.boundingRect : CGRectNull;
}

- (void)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction willBeginWritingInElement:(UIScribbleElementIdentifier)identifier
{
    ASSERT(_scribbleInteraction.get() == interaction);
    if (auto *textInputContext = [self _textInputContextByScribbleIdentifier:identifier])
        [self _willBeginTextInteractionInTextInputContext:textInputContext];
}

- (void)indirectScribbleInteraction:(UIIndirectScribbleInteraction *)interaction didFinishWritingInElement:(UIScribbleElementIdentifier)identifier
{
    ASSERT(_scribbleInteraction.get() == interaction);
    if (auto *textInputContext = [self _textInputContextByScribbleIdentifier:identifier])
        [self _didFinishTextInteractionInTextInputContext:textInputContext];
}

#endif // HAVE(PENCILKIT_TEXT_INPUT)

#if ENABLE(ATTACHMENT_ELEMENT)

#if !PLATFORM(WATCHOS) && !PLATFORM(APPLETV)

static RetainPtr<NSItemProvider> createItemProvider(const WebKit::WebPageProxy& page, const WebCore::PromisedAttachmentInfo& info)
{

    auto attachment = page.attachmentForIdentifier(info.attachmentIdentifier);
    if (!attachment)
        return { };

    RetainPtr utiType = attachment->utiType().createNSString();
    if (![utiType length])
        return { };

    if (attachment->isEmpty())
        return { };

    auto item = adoptNS([[NSItemProvider alloc] init]);
    [item setPreferredPresentationStyle:UIPreferredPresentationStyleAttachment];

    RetainPtr fileName = attachment->fileName().createNSString();
    if ([fileName length])
        [item setSuggestedName:fileName.get()];

    for (size_t index = 0; index < info.additionalTypesAndData.size(); ++index) {
        auto nsData = info.additionalTypesAndData[index].second->createNSData();
        [item registerDataRepresentationForTypeIdentifier:info.additionalTypesAndData[index].first.createNSString().get() visibility:NSItemProviderRepresentationVisibilityAll loadHandler:[nsData](void (^completionHandler)(NSData *, NSError *)) -> NSProgress * {
            completionHandler(nsData.get(), nil);
            return nil;
        }];
    }

    [item registerDataRepresentationForTypeIdentifier:utiType.get() visibility:NSItemProviderRepresentationVisibilityAll loadHandler:[attachment](void (^completionHandler)(NSData *, NSError *)) -> NSProgress * {
        attachment->doWithFileWrapper([&](NSFileWrapper *fileWrapper) {
            if (auto nsData = RetainPtr { fileWrapper.regularFileContents })
                completionHandler(nsData.get(), nil);
            else
                completionHandler(nil, [NSError errorWithDomain:WKErrorDomain code:WKErrorUnknown userInfo:nil]);
        });
        return nil;
    }];

    return item;
}

#endif // !PLATFORM(WATCHOS) && !PLATFORM(APPLETV)

- (void)_writePromisedAttachmentToPasteboard:(WebCore::PromisedAttachmentInfo&&)info
{
#if !PLATFORM(WATCHOS) && !PLATFORM(APPLETV)
    auto item = createItemProvider(*_page, WTFMove(info));
    if (!item)
        return;

    [UIPasteboard _performAsDataOwner:[_webView _effectiveDataOwner:self._dataOwnerForCopy] block:^{
        UIPasteboard.generalPasteboard.itemProviders = @[ item.get() ];
    }];
#else
    UNUSED_PARAM(info);
#endif
}

#endif // ENABLE(ATTACHMENT_ELEMENT)

#if ENABLE(IMAGE_ANALYSIS)

- (void)_endImageAnalysisGestureDeferral:(WebKit::ShouldPreventGestures)shouldPreventGestures
{
    [_imageAnalysisDeferringGestureRecognizer endDeferral:shouldPreventGestures];
}

- (void)_doAfterPendingImageAnalysis:(void(^)(WebKit::ProceedWithTextSelectionInImage))block
{
    if (self.hasPendingImageAnalysisRequest)
        _actionsToPerformAfterPendingImageAnalysis.append(makeBlockPtr(block));
    else
        block(WebKit::ProceedWithTextSelectionInImage::No);
}

- (void)_invokeAllActionsToPerformAfterPendingImageAnalysis:(WebKit::ProceedWithTextSelectionInImage)proceedWithTextSelectionInImage
{
    _pendingImageAnalysisRequestIdentifier = std::nullopt;
    _elementPendingImageAnalysis = std::nullopt;
    for (auto block : std::exchange(_actionsToPerformAfterPendingImageAnalysis, { }))
        block(proceedWithTextSelectionInImage);
}

#endif // ENABLE(IMAGE_ANALYSIS)

- (CALayer *)textIndicatorInstallationLayer
{
    return [self layer];
}

- (void)setUpTextIndicator:(Ref<WebCore::TextIndicator>)textIndicator
{
    if (_textIndicator == textIndicator.ptr())
        return;
    
    [self teardownTextIndicatorLayer];
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(startFadeOut) object:nil];
    
    _textIndicator = textIndicator.ptr();

    CGRect frame = _textIndicator->textBoundingRectInRootViewCoordinates();
    _textIndicatorLayer = adoptNS([[WebTextIndicatorLayer alloc] initWithFrame:frame
        textIndicator:textIndicator.ptr() margin:CGSizeZero offset:CGPointZero]);
    
    [[self layer] addSublayer:_textIndicatorLayer.get()];

    if (_textIndicator->presentationTransition() != WebCore::TextIndicatorPresentationTransition::None)
        [_textIndicatorLayer present];
    
    [self performSelector:@selector(startFadeOut) withObject:self afterDelay:WebCore::timeBeforeFadeStarts.value()];
}

- (void)updateTextIndicator:(Ref<WebCore::TextIndicator>)textIndicator
{
    if (_textIndicator != textIndicator.ptr())
        return;

    CGRect frame = _textIndicator->textBoundingRectInRootViewCoordinates();
    _textIndicatorLayer = adoptNS([[WebTextIndicatorLayer alloc] initWithFrame:frame
        textIndicator:textIndicator.ptr() margin:CGSizeZero offset:CGPointZero]);

    [[self layer] addSublayer:_textIndicatorLayer.get()];
}

- (void)clearTextIndicator:(WebCore::TextIndicatorDismissalAnimation)animation
{
    RefPtr<WebCore::TextIndicator> textIndicator = WTFMove(_textIndicator);
    
    if ([_textIndicatorLayer isFadingOut])
        return;

    if (textIndicator && textIndicator->wantsManualAnimation() && [_textIndicatorLayer hasCompletedAnimation] && animation == WebCore::TextIndicatorDismissalAnimation::FadeOut) {
        [self startFadeOut];
        return;
    }

    [self teardownTextIndicatorLayer];
}

- (void)setTextIndicatorAnimationProgress:(float)animationProgress
{
    if (!_textIndicator)
        return;

    [_textIndicatorLayer setAnimationProgress:animationProgress];
}

- (void)teardownTextIndicatorLayer
{
    [_textIndicatorLayer removeFromSuperlayer];
    _textIndicatorLayer = nil;
}

- (void)startFadeOut
{
    [_textIndicatorLayer setFadingOut:YES];
        
    [_textIndicatorLayer hideWithCompletionHandler:[weakSelf = WeakObjCPtr<WKContentView>(self)] {
        auto strongSelf = weakSelf.get();
        [strongSelf teardownTextIndicatorLayer];
    }];
}

#if HAVE(UIFINDINTERACTION)

- (void)find:(id)sender
{
    [self.webView find:sender];
}

- (void)findAndReplace:(id)sender
{
    [self.webView findAndReplace:sender];
}

- (void)findNext:(id)sender
{
    [self.webView findNext:sender];
}

- (void)findPrevious:(id)sender
{
    [self.webView findPrevious:sender];
}

- (void)useSelectionForFindForWebView:(id)sender
{
    if (!_page->hasSelectedRange())
        return;

    NSString *selectedText = self.selectedText;
    if (selectedText.length) {
        [[self.webView findInteraction] setSearchText:selectedText];
        UIFindInteraction._globalFindBuffer = selectedText;
    }
}

- (void)_findSelectedForWebView:(id)sender
{
    RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED();

    [self findSelectedForWebView:sender];
}

- (void)findSelectedForWebView:(id)sender
{
    [self useSelectionForFindForWebView:sender];
    [self.webView find:sender];
}

- (void)performTextSearchWithQueryString:(NSString *)string usingOptions:(UITextSearchOptions *)options resultAggregator:(id<UITextSearchAggregator>)aggregator
{
    OptionSet<WebKit::FindOptions> findOptions;
    switch (options.wordMatchMethod) {
    case UITextSearchMatchMethodStartsWith:
        findOptions.add(WebKit::FindOptions::AtWordStarts);
        break;
    case UITextSearchMatchMethodFullWord:
        findOptions.add({ WebKit::FindOptions::AtWordStarts, WebKit::FindOptions::AtWordEnds });
        break;
    default:
        break;
    }

    if (options.stringCompareOptions & NSCaseInsensitiveSearch)
        findOptions.add(WebKit::FindOptions::CaseInsensitive);

    // The limit matches the limit set on existing WKWebView find API.
    constexpr auto maxMatches = 1000;
    _page->findTextRangesForStringMatches(string, findOptions, maxMatches, [string = retainPtr(string), aggregator = retainPtr(aggregator)](const Vector<WebKit::WebFoundTextRange> ranges) {
        for (auto& range : ranges) {
            WKFoundTextRange *textRange = [WKFoundTextRange foundTextRangeWithWebFoundTextRange:range];
            [aggregator foundRange:textRange forSearchString:string.get() inDocument:nil];
        }

        [aggregator finishedSearching];
    });
}

- (void)replaceFoundTextInRange:(UITextRange *)range inDocument:(UITextSearchDocumentIdentifier)document withText:(NSString *)replacementText
{
    if (!self.supportsTextReplacement)
        return;

    auto foundTextRange = dynamic_objc_cast<WKFoundTextRange>(range);
    if (!foundTextRange)
        return;

    _page->replaceFoundTextRangeWithString([foundTextRange webFoundTextRange], replacementText);
}

- (void)decorateFoundTextRange:(UITextRange *)range inDocument:(UITextSearchDocumentIdentifier)document usingStyle:(UITextSearchFoundTextStyle)style
{
    auto foundTextRange = dynamic_objc_cast<WKFoundTextRange>(range);
    if (!foundTextRange)
        return;

    auto decorationStyle = WebKit::FindDecorationStyle::Normal;
    if (style == UITextSearchFoundTextStyleFound)
        decorationStyle = WebKit::FindDecorationStyle::Found;
    else if (style == UITextSearchFoundTextStyleHighlighted)
        decorationStyle = WebKit::FindDecorationStyle::Highlighted;

    _page->decorateTextRangeWithStyle([foundTextRange webFoundTextRange], decorationStyle);
}

- (void)scrollRangeToVisible:(UITextRange *)range inDocument:(UITextSearchDocumentIdentifier)document
{
    if (auto foundTextRange = dynamic_objc_cast<WKFoundTextRange>(range))
        _page->scrollTextRangeToVisible([foundTextRange webFoundTextRange]);
}

- (void)clearAllDecoratedFoundText
{
    _page->clearAllDecoratedFoundText();
}

- (void)didBeginTextSearchOperation
{
    [self.webView _showFindOverlay];
    _page->didBeginTextSearchOperation();
}

- (void)didEndTextSearchOperation
{
    [self.webView _hideFindOverlay];
}

- (BOOL)supportsTextReplacement
{
    return self.webView.supportsTextReplacement;
}

- (BOOL)supportsTextReplacementForWebView
{
    return self.webView._editable;
}

- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition inDocument:(UITextSearchDocumentIdentifier)document
{
    return [self offsetFromPosition:from toPosition:toPosition];
}

- (NSComparisonResult)compareFoundRange:(UITextRange *)fromRange toRange:(UITextRange *)toRange inDocument:(UITextSearchDocumentIdentifier)document
{
    NSInteger offset = [self offsetFromPosition:fromRange.start toPosition:toRange.start];

    if (offset < 0)
        return NSOrderedAscending;

    if (offset > 0)
        return NSOrderedDescending;

    return NSOrderedSame;
}

- (void)requestRectForFoundTextRange:(UITextRange *)range completionHandler:(void (^)(CGRect))completionHandler
{
    if (auto* foundTextRange = dynamic_objc_cast<WKFoundTextRange>(range)) {
        _page->requestRectForFoundTextRange([foundTextRange webFoundTextRange], [completionHandler = makeBlockPtr(completionHandler)] (WebCore::FloatRect rect) {
            completionHandler(rect);
        });
        return;
    }

    completionHandler(CGRectZero);
}

#endif // HAVE(UIFINDINTERACTION)

#if ENABLE(IMAGE_ANALYSIS)

- (BOOL)hasSelectableTextForImageContextMenu
{
    return valueOrDefault(_imageAnalysisContextMenuActionData).hasSelectableText;
}

- (BOOL)hasVisualSearchResultsForImageContextMenu
{
    return valueOrDefault(_imageAnalysisContextMenuActionData).hasVisualSearchResults;
}

- (CGImageRef)copySubjectResultForImageContextMenu
{
    return valueOrDefault(_imageAnalysisContextMenuActionData).copySubjectResult.get();
}

- (UIMenu *)machineReadableCodeSubMenuForImageContextMenu
{
    return valueOrDefault(_imageAnalysisContextMenuActionData).machineReadableCodeMenu.get();
}

#if USE(QUICK_LOOK)

- (void)presentVisualSearchPreviewControllerForImage:(UIImage *)image imageURL:(NSURL *)imageURL title:(NSString *)title imageBounds:(CGRect)imageBounds appearanceActions:(QLPreviewControllerFirstTimeAppearanceActions)appearanceActions
{
    ASSERT(self.hasSelectableTextForImageContextMenu || self.hasVisualSearchResultsForImageContextMenu);

    ASSERT(!_visualSearchPreviewController);
    _visualSearchPreviewController = adoptNS([PAL::allocQLPreviewControllerInstance() init]);
    [_visualSearchPreviewController setDelegate:self];
    [_visualSearchPreviewController setDataSource:self];
    [_visualSearchPreviewController setAppearanceActions:appearanceActions];
    [_visualSearchPreviewController setModalPresentationStyle:UIModalPresentationOverFullScreen];

    ASSERT(!_visualSearchPreviewImage);
    _visualSearchPreviewImage = image;

    ASSERT(!_visualSearchPreviewTitle);
    _visualSearchPreviewTitle = title;

    ASSERT(!_visualSearchPreviewImageURL);
    _visualSearchPreviewImageURL = imageURL;

    _visualSearchPreviewImageBounds = imageBounds;

    auto currentPresentingViewController = self._wk_viewControllerForFullScreenPresentation;
    [currentPresentingViewController presentViewController:_visualSearchPreviewController.get() animated:YES completion:nil];
}

#pragma mark - QLPreviewControllerDelegate

- (CGRect)previewController:(QLPreviewController *)controller frameForPreviewItem:(id <QLPreviewItem>)item inSourceView:(UIView **)outView
{
    *outView = self;
    return _visualSearchPreviewImageBounds;
}

- (UIImage *)previewController:(QLPreviewController *)controller transitionImageForPreviewItem:(id <QLPreviewItem>)item contentRect:(CGRect *)outContentRect
{
    // FIXME: We should remove this caching when UIKit doesn't call this delegate method twice, with
    // the second call being in the midst of an ongoing animation. See <rdar://problem/131368437>.
    if (!_cachedVisualSearchPreviewImageBoundsInWindowCoordinates)
        _cachedVisualSearchPreviewImageBoundsInWindowCoordinates = { CGPointZero, [self convertRect:_visualSearchPreviewImageBounds toView:nil].size };
    *outContentRect = *_cachedVisualSearchPreviewImageBoundsInWindowCoordinates;
    return _visualSearchPreviewImage.get();
}

- (void)previewControllerDidDismiss:(QLPreviewController *)controller
{
    ASSERT(controller == _visualSearchPreviewController);
    _visualSearchPreviewController.clear();
    _visualSearchPreviewImage.clear();
    _visualSearchPreviewTitle.clear();
    _visualSearchPreviewImageURL.clear();
    _cachedVisualSearchPreviewImageBoundsInWindowCoordinates.reset();
}

#pragma mark - QLPreviewControllerDataSource

- (NSInteger)numberOfPreviewItemsInPreviewController:(QLPreviewController *)controller
{
    ASSERT(controller == _visualSearchPreviewController);
    return 1;
}

- (id <QLPreviewItem>)previewController:(QLPreviewController *)controller previewItemAtIndex:(NSInteger)index
{
    ASSERT(controller == _visualSearchPreviewController);
    ASSERT(!index);
    auto item = adoptNS([PAL::allocQLItemInstance() initWithDataProvider:self contentType:UTTypeTIFF.identifier previewTitle:_visualSearchPreviewTitle.get()]);
    if ([item respondsToSelector:@selector(setPreviewOptions:)]) {
        auto previewOptions = adoptNS([[NSMutableDictionary alloc] initWithCapacity:2]);
        if (_visualSearchPreviewImageURL)
            [previewOptions setObject:_visualSearchPreviewImageURL.get() forKey:@"imageURL"];
        if (auto pageURL = URL { _page->currentURL() }; !pageURL.isEmpty())
            [previewOptions setObject:pageURL.createNSURL().get() forKey:@"pageURL"];
        if ([previewOptions count])
            [item setPreviewOptions:previewOptions.get()];
    }
    return item.autorelease();
}

#pragma mark - QLPreviewItemDataProvider

- (NSData *)provideDataForItem:(QLItem *)item
{
    ASSERT(_visualSearchPreviewImage);
    return WebKit::transcode([_visualSearchPreviewImage CGImage], (__bridge CFStringRef)UTTypeTIFF.identifier).autorelease();
}

#pragma mark - WKActionSheetAssistantDelegate

- (BOOL)actionSheetAssistant:(WKActionSheetAssistant *)assistant shouldIncludeShowTextActionForElement:(_WKActivatedElementInfo *)element
{
    return WebKit::isLiveTextAvailableAndEnabled() && self.hasSelectableTextForImageContextMenu;
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant showTextForImage:(UIImage *)image imageURL:(NSURL *)imageURL title:(NSString *)title imageBounds:(CGRect)imageBounds
{
    [self presentVisualSearchPreviewControllerForImage:image imageURL:imageURL title:title imageBounds:imageBounds appearanceActions:QLPreviewControllerFirstTimeAppearanceActionEnableVisualSearchDataDetection];
}

- (BOOL)actionSheetAssistant:(WKActionSheetAssistant *)assistant shouldIncludeLookUpImageActionForElement:(_WKActivatedElementInfo *)element
{
    return WebKit::isLiveTextAvailableAndEnabled() && self.hasVisualSearchResultsForImageContextMenu;
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant lookUpImage:(UIImage *)image imageURL:(NSURL *)imageURL title:(NSString *)title imageBounds:(CGRect)imageBounds
{
    [self presentVisualSearchPreviewControllerForImage:image imageURL:imageURL title:title imageBounds:imageBounds appearanceActions:QLPreviewControllerFirstTimeAppearanceActionEnableVisualSearchMode];
}

#endif // USE(QUICK_LOOK)

#pragma mark - Image Extraction

- (CocoaImageAnalyzer *)imageAnalyzer
{
    if (!_imageAnalyzer)
        _imageAnalyzer = WebKit::createImageAnalyzer();
    return _imageAnalyzer.get();
}

- (BOOL)hasPendingImageAnalysisRequest
{
    return !!_pendingImageAnalysisRequestIdentifier;
}

- (void)_setUpImageAnalysis
{
    if (!WebKit::isLiveTextAvailableAndEnabled())
        return;

    _pendingImageAnalysisRequestIdentifier = std::nullopt;
    _isProceedingWithTextSelectionInImage = NO;
    _elementPendingImageAnalysis = std::nullopt;
    _imageAnalysisGestureRecognizer = adoptNS([[WKImageAnalysisGestureRecognizer alloc] initWithImageAnalysisGestureDelegate:self]);
    [self addGestureRecognizer:_imageAnalysisGestureRecognizer.get()];
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    _removeBackgroundData = std::nullopt;
#endif
    _dynamicImageAnalysisContextMenuState = WebKit::DynamicImageAnalysisContextMenuState::NotWaiting;
    _imageAnalysisContextMenuActionData = std::nullopt;
}

- (void)_tearDownImageAnalysis
{
    if (!WebKit::isLiveTextAvailableAndEnabled())
        return;

    [_imageAnalysisGestureRecognizer setDelegate:nil];
    [self removeGestureRecognizer:_imageAnalysisGestureRecognizer.get()];
    _imageAnalysisGestureRecognizer = nil;
    _pendingImageAnalysisRequestIdentifier = std::nullopt;
    _isProceedingWithTextSelectionInImage = NO;
    _elementPendingImageAnalysis = std::nullopt;
    [std::exchange(_imageAnalyzer, nil) cancelAllRequests];
    [self _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    [self uninstallImageAnalysisInteraction];
    _removeBackgroundData = std::nullopt;
#endif
    _dynamicImageAnalysisContextMenuState = WebKit::DynamicImageAnalysisContextMenuState::NotWaiting;
    _imageAnalysisContextMenuActionData = std::nullopt;
}

- (void)_cancelImageAnalysis
{
    [_imageAnalyzer cancelAllRequests];
    RELEASE_LOG_IF(self.hasPendingImageAnalysisRequest, ImageAnalysis, "Image analysis request %" PRIu64 " cancelled.", _pendingImageAnalysisRequestIdentifier->toUInt64());
    _pendingImageAnalysisRequestIdentifier = std::nullopt;
    _isProceedingWithTextSelectionInImage = NO;
    _elementPendingImageAnalysis = std::nullopt;
}

- (RetainPtr<CocoaImageAnalyzerRequest>)createImageAnalyzerRequest:(VKAnalysisTypes)analysisTypes image:(CGImageRef)image imageURL:(NSURL *)imageURL
{
    auto request = WebKit::createImageAnalyzerRequest(image, analysisTypes);
    [request setImageURL:imageURL];
    [request setPageURL:[NSURL _web_URLWithWTFString:_page->currentURL()]];
    return request;
}

- (RetainPtr<CocoaImageAnalyzerRequest>)createImageAnalyzerRequest:(VKAnalysisTypes)analysisTypes image:(CGImageRef)image
{
    return [self createImageAnalyzerRequest:analysisTypes image:image imageURL:_positionInformation.imageURL.createNSURL().get()];
}


- (void)updateImageAnalysisForContextMenuPresentation:(CocoaImageAnalysis *)analysis elementBounds:(CGRect)elementBounds
{
#if USE(UICONTEXTMENU) && ENABLE(IMAGE_ANALYSIS_FOR_MACHINE_READABLE_CODES)
    analysis.presentingViewControllerForMrcAction = self._wk_viewControllerForFullScreenPresentation;
    if ([analysis respondsToSelector:@selector(setRectForMrcActionInPresentingViewController:)]) {
        CGRect boundsInPresentingViewController = [self convertRect:elementBounds toView:analysis.presentingViewControllerForMrcAction.viewIfLoaded];
        analysis.rectForMrcActionInPresentingViewController = boundsInPresentingViewController;
    }
#endif
}


- (BOOL)validateImageAnalysisRequestIdentifier:(WebKit::ImageAnalysisRequestIdentifier)identifier
{
    if (_pendingImageAnalysisRequestIdentifier == identifier)
        return YES;

    if (!self.hasPendingImageAnalysisRequest) {
        // Only invoke deferred image analysis blocks if there is no ongoing request; otherwise, this would
        // cause these blocks to be invoked too early (i.e. in the middle of the new image analysis request).
        [self _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];
    }

    RELEASE_LOG(ImageAnalysis, "Image analysis request %" PRIu64 " invalidated.", identifier.toUInt64());
    return NO;
}

- (void)requestTextRecognition:(NSURL *)imageURL imageData:(WebCore::ShareableBitmap::Handle&&)imageData sourceLanguageIdentifier:(NSString *)sourceLanguageIdentifier targetLanguageIdentifier:(NSString *)targetLanguageIdentifier completionHandler:(CompletionHandler<void(WebCore::TextRecognitionResult&&)>&&)completion
{
    auto imageBitmap = WebCore::ShareableBitmap::create(WTFMove(imageData));
    if (!imageBitmap) {
        completion({ });
        return;
    }

    auto cgImage = imageBitmap->makeCGImage();
    if (!cgImage) {
        completion({ });
        return;
    }

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if (targetLanguageIdentifier.length)
        return WebKit::requestVisualTranslation(self.imageAnalyzer, imageURL, sourceLanguageIdentifier, targetLanguageIdentifier, cgImage.get(), WTFMove(completion));
#else
    UNUSED_PARAM(sourceLanguageIdentifier);
    UNUSED_PARAM(targetLanguageIdentifier);
#endif

    auto request = [self createImageAnalyzerRequest:VKAnalysisTypeText image:cgImage.get()];
    [self.imageAnalyzer processRequest:request.get() progressHandler:nil completionHandler:makeBlockPtr([completion = WTFMove(completion)] (CocoaImageAnalysis *result, NSError *) mutable {
        completion(WebKit::makeTextRecognitionResult(result));
    }).get()];
}

#pragma mark - WKImageAnalysisGestureRecognizerDelegate

- (void)imageAnalysisGestureDidBegin:(WKImageAnalysisGestureRecognizer *)gestureRecognizer
{
    ASSERT(WebKit::isLiveTextAvailableAndEnabled());

    if ([self _isPanningScrollViewOrAncestor:gestureRecognizer.lastTouchedScrollView])
        return;

    auto requestIdentifier = WebKit::ImageAnalysisRequestIdentifier::generate();

    [_imageAnalyzer cancelAllRequests];
    _pendingImageAnalysisRequestIdentifier = requestIdentifier;
    _isProceedingWithTextSelectionInImage = NO;
    _elementPendingImageAnalysis = std::nullopt;
    _dynamicImageAnalysisContextMenuState = WebKit::DynamicImageAnalysisContextMenuState::NotWaiting;
    _imageAnalysisContextMenuActionData = std::nullopt;

    WebKit::InteractionInformationRequest request { WebCore::roundedIntPoint([gestureRecognizer locationInView:self]) };
    request.includeImageData = true;
    [self doAfterPositionInformationUpdate:[requestIdentifier = WTFMove(requestIdentifier), weakSelf = WeakObjCPtr<WKContentView>(self), gestureDeferralToken = WebKit::ImageAnalysisGestureDeferralToken::create(self)] (WebKit::InteractionInformationAtPosition information) mutable {
        auto strongSelf = weakSelf.get();
        if (![strongSelf validateImageAnalysisRequestIdentifier:requestIdentifier])
            return;

        bool shouldAnalyzeImageAtLocation = ([&] {
            if (!information.isImage && !canAttemptTextRecognitionForNonImageElements(information, strongSelf->_page->preferences()))
                return false;

            if (!information.image)
                return false;

            if (!information.hostImageOrVideoElementContext)
                return false;

            if (information.isAnimatedImage)
                return false;

            if (information.isContentEditable)
                return false;

            return true;
        })();

        if (!strongSelf->_pendingImageAnalysisRequestIdentifier || !shouldAnalyzeImageAtLocation) {
            [strongSelf _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];
            return;
        }

        auto cgImage = information.image->makeCGImageCopy();
        if (!cgImage) {
            [strongSelf _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];
            return;
        }

        RELEASE_LOG(ImageAnalysis, "Image analysis preflight gesture initiated (request %" PRIu64 ").", requestIdentifier.toUInt64());

        strongSelf->_elementPendingImageAnalysis = information.hostImageOrVideoElementContext;

        auto requestLocation = information.request.point;
        WebCore::ElementContext elementContext = *information.hostImageOrVideoElementContext;

        auto requestForTextSelection = [strongSelf createImageAnalyzerRequest:VKAnalysisTypeText image:cgImage.get()];
        if (information.isPausedVideo)
            [requestForTextSelection setImageSource:VKImageAnalyzerRequestImageSourceVideoFrame];

        if (information.elementContainsImageOverlay) {
            [strongSelf _completeImageAnalysisRequestForContextMenu:cgImage.get() requestIdentifier:requestIdentifier hasTextResults:YES];
            return;
        }

        auto textAnalysisStartTime = MonotonicTime::now();
        [[strongSelf imageAnalyzer] processRequest:requestForTextSelection.get() progressHandler:nil completionHandler:[requestIdentifier = WTFMove(requestIdentifier), weakSelf, elementContext, requestLocation, cgImage, gestureDeferralToken, textAnalysisStartTime] (CocoaImageAnalysis *result, NSError *error) mutable {
            auto strongSelf = weakSelf.get();
            if (![strongSelf validateImageAnalysisRequestIdentifier:requestIdentifier])
                return;

            BOOL hasTextResults = [result hasResultsForAnalysisTypes:VKAnalysisTypeText];
            RELEASE_LOG(ImageAnalysis, "Image analysis completed in %.0f ms (request %" PRIu64 "; found text? %d)", (MonotonicTime::now() - textAnalysisStartTime).milliseconds(), requestIdentifier.toUInt64(), hasTextResults);

            strongSelf->_page->updateWithTextRecognitionResult(WebKit::makeTextRecognitionResult(result), elementContext, requestLocation, [requestIdentifier = WTFMove(requestIdentifier), weakSelf, hasTextResults, cgImage, gestureDeferralToken] (WebKit::TextRecognitionUpdateResult updateResult) mutable {
                auto strongSelf = weakSelf.get();
                if (![strongSelf validateImageAnalysisRequestIdentifier:requestIdentifier])
                    return;

                if (updateResult == WebKit::TextRecognitionUpdateResult::Text) {
                    strongSelf->_isProceedingWithTextSelectionInImage = YES;
                    [strongSelf _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::Yes];
                    return;
                }

                gestureDeferralToken->setShouldPreventTextSelection();

                if (updateResult == WebKit::TextRecognitionUpdateResult::DataDetector) {
                    [strongSelf _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];
                    return;
                }

                [strongSelf _completeImageAnalysisRequestForContextMenu:cgImage.get() requestIdentifier:requestIdentifier hasTextResults:hasTextResults];
            });
        }];
    } forRequest:request];
}

#if ENABLE(IMAGE_ANALYSIS_FOR_MACHINE_READABLE_CODES)
static BOOL shouldUseMachineReadableCodeMenuFromImageAnalysisResult(CocoaImageAnalysis *result)
{
#if HAVE(BCS_LIVE_CAMERA_ONLY_ACTION_SPI)
    return [result.barcodeActions indexOfObjectPassingTest:^BOOL(BCSAction *action, NSUInteger index, BOOL *stop) {
        return [action respondsToSelector:@selector(isLiveCameraOnlyAction)] && [(id)action isLiveCameraOnlyAction];
    }] == NSNotFound;
#else // not HAVE(BCS_LIVE_CAMERA_ONLY_ACTION_SPI)
    return YES;
#endif // HAVE(BCS_LIVE_CAMERA_ONLY_ACTION_SPI)
}
#endif // ENABLE(IMAGE_ANALYSIS_FOR_MACHINE_READABLE_CODES)

- (void)_completeImageAnalysisRequestForContextMenu:(CGImageRef)image requestIdentifier:(WebKit::ImageAnalysisRequestIdentifier)requestIdentifier hasTextResults:(BOOL)hasTextResults
{
    _dynamicImageAnalysisContextMenuState = WebKit::DynamicImageAnalysisContextMenuState::WaitingForImageAnalysis;
    _imageAnalysisContextMenuActionData = std::nullopt;
    [self _invokeAllActionsToPerformAfterPendingImageAnalysis:WebKit::ProceedWithTextSelectionInImage::No];

    auto data = Box<WebKit::ImageAnalysisContextMenuActionData>::create();
    data->hasSelectableText = hasTextResults;

    auto weakSelf = WeakObjCPtr<WKContentView>(self);
    auto aggregator = MainRunLoopCallbackAggregator::create([weakSelf, data]() mutable {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || strongSelf->_dynamicImageAnalysisContextMenuState == WebKit::DynamicImageAnalysisContextMenuState::NotWaiting)
            return;

        strongSelf->_imageAnalysisContextMenuActionData = WTFMove(*data);
        [strongSelf _insertDynamicImageAnalysisContextMenuItemsIfPossible];
    });

    auto request = [self createImageAnalyzerRequest:VKAnalysisTypeVisualSearch | VKAnalysisTypeMachineReadableCode | VKAnalysisTypeAppClip image:image];
    if (_positionInformation.isPausedVideo)
        [request setImageSource:VKImageAnalyzerRequestImageSourceVideoFrame];

    auto elementBounds = _positionInformation.bounds;
    auto visualSearchAnalysisStartTime = MonotonicTime::now();
    [self.imageAnalyzer processRequest:request.get() progressHandler:nil completionHandler:[requestIdentifier = WTFMove(requestIdentifier), weakSelf, visualSearchAnalysisStartTime, aggregator = aggregator.copyRef(), data, elementBounds] (CocoaImageAnalysis *result, NSError *error) mutable {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return;

#if USE(QUICK_LOOK)
        BOOL hasVisualSearchResults = [result hasResultsForAnalysisTypes:VKAnalysisTypeVisualSearch];
        RELEASE_LOG(ImageAnalysis, "Image analysis completed in %.0f ms (request %" PRIu64 "; found visual search results? %d)", (MonotonicTime::now() - visualSearchAnalysisStartTime).milliseconds(), requestIdentifier.toUInt64(), hasVisualSearchResults);
#else
        UNUSED_PARAM(requestIdentifier);
        UNUSED_PARAM(visualSearchAnalysisStartTime);
#endif
        if (!result || error)
            return;

        [strongSelf updateImageAnalysisForContextMenuPresentation:result elementBounds:elementBounds];

#if ENABLE(IMAGE_ANALYSIS_FOR_MACHINE_READABLE_CODES)
        if (shouldUseMachineReadableCodeMenuFromImageAnalysisResult(result))
            data->machineReadableCodeMenu = result.mrcMenu;
#endif
#if USE(QUICK_LOOK)
        data->hasVisualSearchResults = hasVisualSearchResults;
#endif
    }];

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if (_page->preferences().removeBackgroundEnabled()) {
        WebKit::requestBackgroundRemoval(image, [weakSelf = WeakObjCPtr<WKContentView>(self), aggregator = aggregator.copyRef(), data](CGImageRef result) mutable {
            if (auto strongSelf = weakSelf.get())
                data->copySubjectResult = result;
        });
    }
#endif
}

- (void)_insertDynamicImageAnalysisContextMenuItemsIfPossible
{
    bool updated = false;
    [self.contextMenuInteraction updateVisibleMenuWithBlock:makeBlockPtr([&](UIMenu *menu) -> UIMenu * {
        updated = true;
        __block BOOL foundRevealImageItem = NO;
        __block BOOL foundShowTextItem = NO;
        auto revealImageIdentifier = elementActionTypeToUIActionIdentifier(_WKElementActionTypeRevealImage);
        auto showTextIdentifier = elementActionTypeToUIActionIdentifier(_WKElementActionTypeImageExtraction);
        [menu.children enumerateObjectsUsingBlock:^(UIMenuElement *child, NSUInteger index, BOOL* stop) {
            auto *action = dynamic_objc_cast<UIAction>(child);
            if ([action.identifier isEqualToString:revealImageIdentifier])
                foundRevealImageItem = YES;
            else if ([action.identifier isEqualToString:showTextIdentifier])
                foundShowTextItem = YES;
        }];

        auto adjustedChildren = adoptNS(menu.children.mutableCopy);
        auto newItems = adoptNS([NSMutableArray<UIMenuElement *> new]);

        for (UIMenuElement *child in adjustedChildren.get()) {
            UIAction *action = dynamic_objc_cast<UIAction>(child);
            if (!action)
                continue;

            if ([action.identifier isEqual:elementActionTypeToUIActionIdentifier(_WKElementActionTypeCopyCroppedImage)]) {
                if (self.copySubjectResultForImageContextMenu)
                    action.attributes &= ~UIMenuElementAttributesDisabled;

                continue;
            }

            if ([action.identifier isEqual:revealImageIdentifier]) {
                if (self.hasVisualSearchResultsForImageContextMenu)
                    action.attributes &= ~UIMenuElementAttributesDisabled;

                continue;
            }
        }

        if (!foundShowTextItem && self.hasSelectableTextForImageContextMenu) {
            // Dynamically insert the "Show Text" menu item, if it wasn't already inserted.
            // FIXME: This should probably be inserted unconditionally, and enabled if needed
            // like Look Up or Copy Subject.
            auto *elementInfo = [_WKActivatedElementInfo activatedElementInfoWithInteractionInformationAtPosition:_positionInformation userInfo:nil];
            auto *elementAction = [_WKElementAction _elementActionWithType:_WKElementActionTypeImageExtraction info:elementInfo assistant:_actionSheetAssistant.get()];
            [newItems addObject:[elementAction uiActionForElementInfo:elementInfo]];
        }

        if (foundRevealImageItem) {
            // Only dynamically insert machine-readable code items if the client didn't explicitly
            // remove the Look Up ("reveal image") item.
            if (UIMenu *subMenu = self.machineReadableCodeSubMenuForImageContextMenu)
                [newItems addObject:subMenu];
        }

        RELEASE_LOG(ImageAnalysis, "Dynamically inserting %zu context menu action(s)", [newItems count]);
        [adjustedChildren addObjectsFromArray:newItems.get()];
        return [menu menuByReplacingChildren:adjustedChildren.get()];
    }).get()];

    _dynamicImageAnalysisContextMenuState = updated ? WebKit::DynamicImageAnalysisContextMenuState::NotWaiting : WebKit::DynamicImageAnalysisContextMenuState::WaitingForVisibleMenu;
}

- (void)imageAnalysisGestureDidFail:(WKImageAnalysisGestureRecognizer *)gestureRecognizer
{
    [self _endImageAnalysisGestureDeferral:WebKit::ShouldPreventGestures::No];
}

- (void)captureTextFromCameraForWebView:(id)sender
{
    [super captureTextFromCamera:sender];
}

#endif // ENABLE(IMAGE_ANALYSIS)

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (BOOL)actionSheetAssistantShouldIncludeCopySubjectAction:(WKActionSheetAssistant *)assistant
{
    return !!self.copySubjectResultForImageContextMenu;
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant copySubject:(UIImage *)image sourceMIMEType:(NSString *)sourceMIMEType
{
    if (!self.copySubjectResultForImageContextMenu)
        return;

    auto [data, type] = WebKit::imageDataForRemoveBackground(self.copySubjectResultForImageContextMenu, (__bridge CFStringRef)sourceMIMEType);
    if (!data)
        return;

    [UIPasteboard _performAsDataOwner:self._dataOwnerForCopy block:[data = WTFMove(data), type = WTFMove(type)] {
        [UIPasteboard.generalPasteboard setData:data.get() forPasteboardType:(__bridge NSString *)type.get()];
    }];
}

#pragma mark - VKCImageAnalysisInteractionDelegate

- (CGRect)contentsRectForImageAnalysisInteraction:(VKCImageAnalysisInteraction *)interaction
{
    auto unitInteractionRect = _imageAnalysisInteractionBounds;
    WebCore::FloatRect contentViewBounds = self.bounds;
    unitInteractionRect.moveBy(-contentViewBounds.location());
    unitInteractionRect.scale(1 / contentViewBounds.size());
    return unitInteractionRect;
}

- (BOOL)imageAnalysisInteraction:(VKCImageAnalysisInteraction *)interaction shouldBeginAtPoint:(CGPoint)point forAnalysisType:(VKImageAnalysisInteractionTypes)analysisType
{
    return interaction.hasActiveTextSelection || [interaction interactableItemExistsAtPoint:point];
}

#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (void)beginTextRecognitionForFullscreenVideo:(WebCore::ShareableBitmap::Handle&&)imageData playerViewController:(AVPlayerViewController *)controller
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    ASSERT(_page->preferences().textRecognitionInVideosEnabled());

    if (_fullscreenVideoImageAnalysisRequestIdentifier)
        return;

    auto imageBitmap = WebCore::ShareableBitmap::create(WTFMove(imageData));
    if (!imageBitmap)
        return;

    auto cgImage = imageBitmap->makeCGImage();
    if (!cgImage)
        return;

    auto request = [self createImageAnalyzerRequest:WebKit::analysisTypesForFullscreenVideo() image:cgImage.get()];
    [request setImageSource:VKImageAnalyzerRequestImageSourceVideoFrame];
    _fullscreenVideoImageAnalysisRequestIdentifier = [self.imageAnalyzer processRequest:request.get() progressHandler:nil completionHandler:makeBlockPtr([weakSelf = WeakObjCPtr<WKContentView>(self), controller = RetainPtr { controller }] (CocoaImageAnalysis *result, NSError *) mutable {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return;

        strongSelf->_fullscreenVideoImageAnalysisRequestIdentifier = 0;

        if ([controller respondsToSelector:@selector(setImageAnalysis:)])
            [controller setImageAnalysis:result];
    }).get()];
#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
}

- (void)cancelTextRecognitionForFullscreenVideo:(AVPlayerViewController *)controller
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    if (auto identifier = std::exchange(_fullscreenVideoImageAnalysisRequestIdentifier, 0))
        [_imageAnalyzer cancelRequestID:identifier];

    if ([controller respondsToSelector:@selector(setImageAnalysis:)])
        [controller setImageAnalysis:nil];
#endif
}

- (BOOL)isTextRecognitionInFullscreenVideoEnabled
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    return _page->preferences().textRecognitionInVideosEnabled();
#else
    return NO;
#endif
}

- (void)beginTextRecognitionForVideoInElementFullscreen:(WebCore::ShareableBitmap::Handle&&)bitmapHandle bounds:(WebCore::FloatRect)bounds
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    auto imageBitmap = WebCore::ShareableBitmap::create(WTFMove(bitmapHandle));
    if (!imageBitmap)
        return;

    auto image = imageBitmap->makeCGImage();
    if (!image)
        return;

    auto request = WebKit::createImageAnalyzerRequest(image.get(), WebKit::analysisTypesForElementFullscreenVideo());
    [request setImageSource:VKImageAnalyzerRequestImageSourceVideoFrame];
    _fullscreenVideoImageAnalysisRequestIdentifier = [self.imageAnalyzer processRequest:request.get() progressHandler:nil completionHandler:[weakSelf = WeakObjCPtr<WKContentView>(self), bounds](CocoaImageAnalysis *result, NSError *error) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || !strongSelf->_fullscreenVideoImageAnalysisRequestIdentifier)
            return;

        strongSelf->_fullscreenVideoImageAnalysisRequestIdentifier = 0;
        if (error || !result)
            return;

        strongSelf->_imageAnalysisInteractionBounds = bounds;
        [strongSelf installImageAnalysisInteraction:result];
    }];
#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
}

- (void)cancelTextRecognitionForVideoInElementFullscreen
{
#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    [self uninstallImageAnalysisInteraction];

    if (auto previousIdentifier = std::exchange(_fullscreenVideoImageAnalysisRequestIdentifier, 0))
        [self.imageAnalyzer cancelRequestID:previousIdentifier];
#endif
}

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (void)installImageAnalysisInteraction:(VKCImageAnalysis *)analysis
{
    if (!_imageAnalysisInteraction) {
        _imageAnalysisActionButtons = adoptNS([[NSMutableSet alloc] initWithCapacity:1]);
        _imageAnalysisInteraction = adoptNS([PAL::allocVKCImageAnalysisInteractionInstance() init]);
        [_imageAnalysisInteraction setDelegate:self];
        [_imageAnalysisInteraction setAnalysisButtonRequiresVisibleContentGating:YES];
        [_imageAnalysisInteraction setQuickActionConfigurationUpdateHandler:[weakSelf = WeakObjCPtr<WKContentView>(self)] (UIButton *button) {
            if (auto strongSelf = weakSelf.get())
                [strongSelf->_imageAnalysisActionButtons addObject:button];
        }];
        WebKit::prepareImageAnalysisForOverlayView(_imageAnalysisInteraction.get());
        [self addInteraction:_imageAnalysisInteraction.get()];
        RELEASE_LOG(ImageAnalysis, "Installing image analysis interaction at {{ %.0f, %.0f }, { %.0f, %.0f }}",
            _imageAnalysisInteractionBounds.x(), _imageAnalysisInteractionBounds.y(), _imageAnalysisInteractionBounds.width(), _imageAnalysisInteractionBounds.height());
    }
    [_imageAnalysisInteraction setAnalysis:analysis];
    [_imageAnalysisDeferringGestureRecognizer setEnabled:NO];
    [_imageAnalysisGestureRecognizer setEnabled:NO];
}

- (void)uninstallImageAnalysisInteraction
{
    if (!_imageAnalysisInteraction)
        return;

    RELEASE_LOG(ImageAnalysis, "Uninstalling image analysis interaction");

    [self removeInteraction:_imageAnalysisInteraction.get()];
    [_imageAnalysisInteraction setDelegate:nil];
    [_imageAnalysisInteraction setQuickActionConfigurationUpdateHandler:nil];
    _imageAnalysisInteraction = nil;
    _imageAnalysisActionButtons = nil;
    _imageAnalysisInteractionBounds = { };
    [_imageAnalysisDeferringGestureRecognizer setEnabled:WebKit::isLiveTextAvailableAndEnabled()];
    [_imageAnalysisGestureRecognizer setEnabled:WebKit::isLiveTextAvailableAndEnabled()];
}

#endif // ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)

- (BOOL)_shouldAvoidSecurityHeuristicScoreUpdates
{
    // FIXME: The whole security heuristic thing should be a USE/HAVE.
#if PLATFORM(VISION)
    return YES;
#elif ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    return [_imageAnalysisInteraction hasActiveTextSelection];
#else
    return NO;
#endif
}

#pragma mark - _UITextInputTranslationSupport

- (BOOL)isImageBacked
{
    return _page && _page->editorState().selectionIsRangeInsideImageOverlay;
}

#if HAVE(UI_EDIT_MENU_INTERACTION)

- (void)willPresentEditMenuWithAnimator:(id<UIEditMenuInteractionAnimating>)animator
{
    [animator addCompletion:[weakSelf = WeakObjCPtr<WKContentView>(self)] {
        if (auto strongSelf = weakSelf.get())
            strongSelf->_isPresentingEditMenu = YES;
    }];

    auto delegate = self.webView.UIDelegate;
    if (![delegate respondsToSelector:@selector(webView:willPresentEditMenuWithAnimator:)])
        return;

    [delegate webView:self.webView willPresentEditMenuWithAnimator:animator];
}

- (void)willDismissEditMenuWithAnimator:(id<UIEditMenuInteractionAnimating>)animator
{
    [animator addCompletion:[weakSelf = WeakObjCPtr<WKContentView>(self)] {
        if (auto strongSelf = weakSelf.get())
            strongSelf->_isPresentingEditMenu = NO;
    }];

    auto delegate = self.webView.UIDelegate;
    if (![delegate respondsToSelector:@selector(webView:willDismissEditMenuWithAnimator:)])
        return;

    [delegate webView:self.webView willDismissEditMenuWithAnimator:animator];
}

#endif // HAVE(UI_EDIT_MENU_INTERACTION)

- (BOOL)isPresentingEditMenu
{
    return _isPresentingEditMenu;
}

- (CGSize)sizeForLegacyFormControlPickerViews
{
    auto size = self.window.bounds.size;
    size.height = 0; // Fall back to the default input view height.
    return size;
}

#if USE(BROWSERENGINEKIT)

#pragma mark - BETextInput (and related)

- (id<BETextInput>)asBETextInput
{
    ASSERT([self conformsToProtocol:@protocol(BETextInput)]);
    return static_cast<id<BETextInput>>(self);
}

- (void)_logMissingSystemInputDelegateIfNeeded:(const char*)methodName
{
    if (_asyncInputDelegate)
        return;

    static constexpr auto delayBetweenLogStatements = 10_s;
    static ApproximateTime lastLoggingTimestamp;
    if (auto timestamp = ApproximateTime::now(); timestamp - lastLoggingTimestamp > delayBetweenLogStatements) {
        RELEASE_LOG_ERROR(TextInput, "%{public}s - system input delegate is nil", methodName);
        lastLoggingTimestamp = timestamp;
    }
}

- (void)shiftKeyStateChangedFromState:(BEKeyModifierFlags)oldState toState:(BEKeyModifierFlags)newState
{
    ASSERT(oldState != newState);
    if (_isHandlingActivePressesEvent) {
        // UIKit will call into us to handle the individual events, so there's no need to dispatch
        // these synthetic modifier change key events.
        return;
    }

    auto dispatchSyntheticFlagsChangedEvents = [&] (BEKeyModifierFlags state, bool keyDown) {
        RetainPtr<WKSyntheticFlagsChangedWebEvent> syntheticEvent;
        switch (state) {
        case BEKeyModifierFlagNone:
            return;
        case BEKeyModifierFlagShift:
            syntheticEvent = adoptNS([[WKSyntheticFlagsChangedWebEvent alloc] initWithShiftState:keyDown]);
            break;
        case BEKeyModifierFlagCapsLock:
            syntheticEvent = adoptNS([[WKSyntheticFlagsChangedWebEvent alloc] initWithCapsLockState:keyDown]);
            break;
        }
        [self _internalHandleKeyWebEvent:syntheticEvent.get()];
    };

    dispatchSyntheticFlagsChangedEvents(oldState, false);
    dispatchSyntheticFlagsChangedEvents(newState, true);
}

- (void)systemWillPresentEditMenuWithAnimator:(id<UIEditMenuInteractionAnimating>)animator
{
    [self willPresentEditMenuWithAnimator:animator];
}

- (void)systemWillDismissEditMenuWithAnimator:(id<UIEditMenuInteractionAnimating>)animator
{
    [self willDismissEditMenuWithAnimator:animator];
}

- (void)insertTextAlternatives:(PlatformTextAlternatives *)alternatives
{
    auto text = alternatives.primaryString;
    if (!alternatives.alternativeStrings.count) {
        [self insertText:text];
        return;
    }

    WebCore::TextAlternativeWithRange textAlternativeWithRange { alternatives, NSMakeRange(0, text.length) };

    _page->insertDictatedTextAsync(text, { }, { textAlternativeWithRange }, WebKit::InsertTextOptions {
        .shouldSimulateKeyboardInput = static_cast<bool>([self _shouldSimulateKeyboardInputOnTextInsertion])
    });

    _autocorrectionContextNeedsUpdate = YES;
}

- (void)insertText:(NSString *)text textAlternatives:(PlatformTextAlternatives *)alternatives style:(UITextAlternativeStyle)style
{
    [self insertTextAlternatives:alternatives];
}

- (BOOL)isPointNearMarkedText:(CGPoint)point
{
    return [self pointIsNearMarkedText:point];
}

- (void)replaceSelectedText:(NSString *)text withText:(NSString *)replacementText
{
    [self replaceText:text withText:replacementText];
}

- (void)updateCurrentSelectionTo:(CGPoint)point fromGesture:(WKBEGestureType)gestureType inState:(UIGestureRecognizerState)state
{
    [self changeSelectionWithGestureAt:point withGesture:gestureType withState:state];
}

- (void)setSelectionFromPoint:(CGPoint)from toPoint:(CGPoint)to gesture:(WKBEGestureType)gestureType state:(UIGestureRecognizerState)gestureState
{
    [self changeSelectionWithTouchesFrom:from to:to withGesture:gestureType withState:gestureState];
}

- (void)adjustSelectionBoundaryToPoint:(CGPoint)point touchPhase:(WKBESelectionTouchPhase)touch baseIsStart:(BOOL)boundaryIsStart flags:(WKBESelectionFlags)flags
{
    [self changeSelectionWithTouchAt:point withSelectionTouch:touch baseIsStart:boundaryIsStart withFlags:flags];
}

- (void)updateSelectionWithExtentPoint:(CGPoint)point boundary:(UITextGranularity)granularity completionHandler:(void (^)(BOOL selectionEndIsMoving))completionHandler
{
    [self updateSelectionWithExtentPoint:point withBoundary:granularity completionHandler:completionHandler];
}

- (void)selectTextInGranularity:(UITextGranularity)granularity atPoint:(CGPoint)point completionHandler:(void (^)(void))completionHandler
{
    [self selectTextWithGranularity:granularity atPoint:point completionHandler:completionHandler];
}

- (void)moveSelectionAtBoundary:(UITextGranularity)granularity inStorageDirection:(UITextStorageDirection)direction completionHandler:(void (^)(void))completionHandler
{
    [self moveSelectionAtBoundary:granularity inDirection:direction completionHandler:completionHandler];
}

- (id<BEExtendedTextInputTraits>)extendedTextInputTraits
{
    return [self extendedTraitsDelegate];
}

#if ENABLE(REVEAL)

- (void)selectTextForEditMenuWithLocationInView:(CGPoint)locationInView completionHandler:(void(^)(BOOL shouldPresentMenu, NSString * _Nullable contextString, NSRange selectedRangeInContextString))completionHandler
{
    [self selectTextForContextMenuWithLocationInView:locationInView completionHandler:completionHandler];
}

#endif // ENABLE(REVEAL)

- (BOOL)isSelectionAtDocumentStart
{
    return self.selectionAtDocumentStart;
}

- (BOOL)automaticallyPresentEditMenu
{
    return _suppressSelectionAssistantReasons.isEmpty();
}

- (id<BETextInputDelegate>)asyncInputDelegate
{
    return _asyncInputDelegate;
}

- (void)setAsyncInputDelegate:(id<BETextInputDelegate>)delegate
{
    _asyncInputDelegate = delegate;
}

- (void)handleKeyEntry:(BEKeyEntry *)event withCompletionHandler:(void (^)(BEKeyEntry *, BOOL))completionHandler
{
    auto webEvent = adoptNS([[::WebEvent alloc] initWithKeyEntry:event]);
    [self _internalHandleKeyWebEvent:webEvent.get() withCompletionHandler:[originalEvent = retainPtr(event), completionHandler = makeBlockPtr(completionHandler)](::WebEvent *webEvent, BOOL handled) {
        ASSERT(webEvent.originalKeyEntry == originalEvent);
        completionHandler(originalEvent.get(), handled);
    }];
}

- (void)replaceText:(NSString *)originalText withText:(NSString *)replacementText options:(BETextReplacementOptions)options completionHandler:(void (^)(NSArray<UITextSelectionRect *> *rects))completionHandler
{
    [self _internalReplaceText:originalText withText:replacementText isCandidate:options & BETextReplacementOptionsAddUnderline completion:[view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)](bool wasReplaced) {
        if (!wasReplaced)
            return completionHandler(@[ ]);

        auto& data = view->_autocorrectionData;
        RetainPtr firstSelectionRect = [WKUITextSelectionRect selectionRectWithCGRect:data.textFirstRect];
        if (CGRectEqualToRect(data.textFirstRect, data.textLastRect))
            return completionHandler(@[ firstSelectionRect.get() ]);

        completionHandler(@[ firstSelectionRect.get(), [WKUITextSelectionRect selectionRectWithCGRect:data.textLastRect] ]);
    }];
}

- (void)requestTextRectsForString:(NSString *)input withCompletionHandler:(void (^)(NSArray<UITextSelectionRect *> *rects))completionHandler
{
    [self _internalRequestTextRectsForString:input completion:[view = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)](auto& rects) mutable {
        auto selectionRects = createNSArray(rects, [](auto& rect) {
            return [WKUITextSelectionRect selectionRectWithCGRect:rect];
        });
        completionHandler(selectionRects.get());
    }];
}

- (UIView *)textInputView
{
    return self;
}

- (void)autoscrollToPoint:(CGPoint)pointInDocument
{
    _page->startAutoscrollAtPosition(pointInDocument);
}

- (void)requestTextContextForAutocorrectionWithCompletionHandler:(void (^)(WKBETextDocumentContext *context))completion
{
    if (!completion) {
        [NSException raise:NSInvalidArgumentException format:@"Expected a nonnull completion handler in %s.", __PRETTY_FUNCTION__];
        return;
    }

    [self _internalRequestAutocorrectionContextWithCompletionHandler:[completion = makeBlockPtr(completion), strongSelf = retainPtr(self)](WebKit::RequestAutocorrectionContextResult result) {
        WebKit::DocumentEditingContext context;
        switch (result) {
        case WebKit::RequestAutocorrectionContextResult::Empty:
            break;
        case WebKit::RequestAutocorrectionContextResult::LastContext:
            auto& webContext = strongSelf->_lastAutocorrectionContext;
            context.contextBefore.string = webContext.contextBefore;
            context.markedText.string = webContext.markedText;
            context.selectedText.string = webContext.selectedText;
            context.contextAfter.string = webContext.contextAfter;
            NSRange selectedRangeInMarkedText = webContext.selectedRangeInMarkedText;
            context.selectedRangeInMarkedText = {
                .location = selectedRangeInMarkedText.location,
                .length = selectedRangeInMarkedText.length
            };
            break;
        }
        completion(context.toPlatformContext({ WebKit::DocumentEditingContextRequest::Options::Text }));
    }];
}

#if ENABLE(REVEAL)

- (void)selectTextForContextMenuWithLocationInView:(CGPoint)locationInView completionHandler:(void(^)(BOOL shouldPresentMenu, NSString *contextString, NSRange selectedRangeInContextString))completionHandler
{
    [self _internalSelectTextForContextMenuWithLocationInView:locationInView completionHandler:[completionHandler = makeBlockPtr(completionHandler)](BOOL shouldPresentMenu, const WebKit::RevealItem& item) {
        auto& selectedRange = item.selectedRange();
        completionHandler(shouldPresentMenu, item.text().createNSString().get(), NSMakeRange(selectedRange.location, selectedRange.length));
    }];
}

#endif // ENABLE(REVEAL)

inline static NSArray<NSString *> *deleteSelectionCommands(UITextStorageDirection direction, UITextGranularity granularity)
{
    BOOL backward = direction == UITextStorageDirectionBackward;
    switch (granularity) {
    case UITextGranularityCharacter:
        return @[ backward ? @"DeleteBackward" : @"DeleteForward" ];
    case UITextGranularityWord:
        return @[ backward ? @"DeleteWordBackward" : @"DeleteWordForward" ];
    case UITextGranularitySentence:
        return @[ backward ? @"MoveToBeginningOfSentenceAndModifySelection" : @"MoveToEndOfSentenceAndModifySelection", @"DeleteBackward" ];
    case UITextGranularityParagraph:
        return @[ backward ? @"DeleteToBeginningOfParagraph" : @"DeleteToEndOfParagraph" ];
    case UITextGranularityLine:
        return @[ backward ? @"DeleteToBeginningOfLine" : @"DeleteToEndOfLine" ];
    case UITextGranularityDocument:
        return @[ backward ? @"MoveToBeginningOfDocumentAndModifySelection" : @"MoveToEndOfDocumentAndModifySelection", @"DeleteBackward" ];
    }
    ASSERT_NOT_REACHED();
    return @[ ];
}

inline static NSArray<NSString *> *moveSelectionCommand(UITextStorageDirection direction, UITextGranularity granularity)
{
    BOOL backward = direction == UITextStorageDirectionBackward;
    switch (granularity) {
    case UITextGranularityCharacter:
        return @[ backward ? @"MoveBackward" : @"MoveForward" ];
    case UITextGranularityWord:
        return @[ backward ? @"MoveWordBackward" : @"MoveWordForward" ];
    case UITextGranularitySentence:
        return @[ backward ? @"MoveToBeginningOfSentence" : @"MoveToEndOfSentence" ];
    case UITextGranularityParagraph:
        return @[
            backward ? @"MoveBackward" : @"MoveForward",
            backward ? @"MoveToBeginningOfParagraph" : @"MoveToEndOfParagraph"
        ];
    case UITextGranularityLine:
        return @[ backward ? @"MoveToBeginningOfLine" : @"MoveToEndOfLine" ];
    case UITextGranularityDocument:
        return @[ backward ? @"MoveToBeginningOfDocument" : @"MoveToEndOfDocument" ];
    }
    ASSERT_NOT_REACHED();
    return nil;
}

inline static NSArray<NSString *> *extendSelectionCommand(UITextStorageDirection direction, UITextGranularity granularity)
{
    BOOL backward = direction == UITextStorageDirectionBackward;
    switch (granularity) {
    case UITextGranularityCharacter:
        return @[ backward ? @"MoveBackwardAndModifySelection" : @"MoveForwardAndModifySelection" ];
    case UITextGranularityWord:
        return @[ backward ? @"MoveWordBackwardAndModifySelection" : @"MoveWordForwardAndModifySelection" ];
    case UITextGranularitySentence:
        return @[ backward ? @"MoveToBeginningOfSentenceAndModifySelection" : @"MoveToEndOfSentenceAndModifySelection" ];
    case UITextGranularityParagraph:
        return @[
            backward ? @"MoveBackwardAndModifySelection" : @"MoveForwardAndModifySelection",
            backward ? @"MoveToBeginningOfParagraphAndModifySelection" : @"MoveToEndOfParagraphAndModifySelection"
        ];
    case UITextGranularityLine:
        return @[ backward ? @"MoveToBeginningOfLineAndModifySelection" : @"MoveToEndOfLineAndModifySelection" ];
    case UITextGranularityDocument:
        return @[ backward ? @"MoveToBeginningOfDocumentAndModifySelection" : @"MoveToEndOfDocumentAndModifySelection" ];
    }
    ASSERT_NOT_REACHED();
    return nil;
}

inline static NSString *moveSelectionCommand(UITextLayoutDirection direction)
{
    switch (direction) {
    case UITextLayoutDirectionRight:
        return @"MoveRight";
    case UITextLayoutDirectionLeft:
        return @"MoveLeft";
    case UITextLayoutDirectionUp:
        return @"MoveUp";
    case UITextLayoutDirectionDown:
        return @"MoveDown";
    }
    ASSERT_NOT_REACHED();
    return nil;
}

inline static NSString *extendSelectionCommand(UITextLayoutDirection direction)
{
    switch (direction) {
    case UITextLayoutDirectionRight:
        return @"MoveRightAndModifySelection";
    case UITextLayoutDirectionLeft:
        return @"MoveLeftAndModifySelection";
    case UITextLayoutDirectionUp:
        return @"MoveUpAndModifySelection";
    case UITextLayoutDirectionDown:
        return @"MoveDownAndModifySelection";
    }
    ASSERT_NOT_REACHED();
    return nil;
}

- (void)deleteInDirection:(UITextStorageDirection)direction toGranularity:(UITextGranularity)granularity
{
    auto notifyDelegate = granularity == UITextGranularityCharacter && direction == UITextStorageDirectionBackward
        ? WebKit::NotifyInputDelegate::No
        : WebKit::NotifyInputDelegate::Yes;

    for (NSString *command in deleteSelectionCommands(direction, granularity))
        [self _executeEditCommand:command notifyDelegate:notifyDelegate];
}

- (void)moveInStorageDirection:(UITextStorageDirection)direction byGranularity:(UITextGranularity)granularity
{
    for (NSString *command in moveSelectionCommand(direction, granularity))
        [self _executeEditCommand:command];
}

- (void)moveInLayoutDirection:(UITextLayoutDirection)direction
{
    [self _executeEditCommand:moveSelectionCommand(direction)];
}

- (void)extendInStorageDirection:(UITextStorageDirection)direction byGranularity:(UITextGranularity)granularity
{
    for (NSString *command in extendSelectionCommand(direction, granularity))
        [self _executeEditCommand:command];
}

- (void)extendInLayoutDirection:(UITextLayoutDirection)direction
{
    [self _executeEditCommand:extendSelectionCommand(direction)];
}

- (void)adjustSelectionByRange:(BEDirectionalTextRange)range completionHandler:(void (^)(void))completionHandler
{
    [self _internalAdjustSelectionWithOffset:range.offset lengthDelta:range.length completionHandler:completionHandler];
}

- (id<BEExtendedTextInputTraits>)extendedTraitsDelegate
{
    if (self._requiresLegacyTextInputTraits)
        return static_cast<id<BEExtendedTextInputTraits>>(self.textInputTraits);

    if (!_extendedTextInputTraits)
        _extendedTextInputTraits = adoptNS([WKExtendedTextInputTraits new]);

    if (!self._hasFocusedElement && !_isFocusingElementWithKeyboard) {
        [_extendedTextInputTraits restoreDefaultValues];
        [_extendedTextInputTraits setSelectionColorsToMatchTintColor:[self _cascadeInteractionTintColor]];
    } else if (!_isBlurringFocusedElement)
        [self _updateTextInputTraits:_extendedTextInputTraits.get()];

    return _extendedTextInputTraits.get();
}

#endif // USE(BROWSERENGINEKIT)

- (void)_internalAdjustSelectionWithOffset:(NSInteger)offset lengthDelta:(NSInteger)lengthDelta completionHandler:(void (^)(void))completionHandler
{
    _page->updateSelectionWithDelta(offset, lengthDelta, [capturedCompletionHandler = makeBlockPtr(completionHandler)] {
        capturedCompletionHandler();
    });
}

- (CGRect)selectionClipRect
{
    if (_page->waitingForPostLayoutEditorStateUpdateAfterFocusingElement())
        return _focusedElementInformation.interactionRect;

    if (_page->editorState().hasVisualData() && !_page->editorState().visualData->selectionClipRect.isEmpty())
        return _page->editorState().visualData->selectionClipRect;

    return CGRectNull;
}

- (UIView *)selectionContainerViewBelowText
{
    return self._selectionContainerViewInternal;
}

- (UIView *)selectionContainerViewAboveText
{
    return self._selectionContainerViewInternal;
}

- (void)transposeCharactersAroundSelection
{
    [self _executeEditCommand:@"transpose"];
}

- (BOOL)selectionAtDocumentStart
{
    if (!_page->editorState().postLayoutData)
        return NO;
    return !_page->editorState().postLayoutData->characterBeforeSelection;
}

- (BOOL)shouldSuppressEditMenu
{
    return !!_suppressSelectionAssistantReasons;
}

#pragma mark - WKTextAnimationSourceDelegate

#if ENABLE(WRITING_TOOLS)
- (void)targetedPreviewForID:(NSUUID *)uuid completionHandler:(void (^)(UITargetedPreview *))completionHandler
{
    auto animationID = WTF::UUID::fromNSUUID(uuid);

    _page->getTextIndicatorForID(*animationID, [protectedSelf = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)] (auto&& textIndicator) {
        if (!textIndicator) {
            completionHandler(nil);
            return;
        }

        RetainPtr targetedPreview = [protectedSelf _createTargetedPreviewFromTextIndicator:WTFMove(textIndicator) previewContainer:[protectedSelf containerForContextMenuHintPreviews]];
        completionHandler(targetedPreview.get());
    });
}

- (void)updateUnderlyingTextVisibilityForTextAnimationID:(NSUUID *)uuid visible:(BOOL)visible completionHandler:(void (^)(void))completionHandler
{
    NSUUID *destinationUUID = _sourceAnimationIDtoDestinationAnimationID.get()[uuid];
    auto textUUID = WTF::UUID::fromNSUUID(uuid);
    if (destinationUUID)
        textUUID = WTF::UUID::fromNSUUID(destinationUUID);

    _page->updateUnderlyingTextVisibilityForTextAnimationID(*textUUID, visible, [completionHandler = makeBlockPtr(completionHandler)] () {
        completionHandler();
    });
}

- (UIView *)containingViewForTextAnimationType
{
    return self;
}

- (void)callCompletionHandlerForAnimationID:(NSUUID *)uuid
{
    NSUUID *destinationUUID = _sourceAnimationIDtoDestinationAnimationID.get()[uuid];

    if (!destinationUUID)
        return;

    auto animationUUID = WTF::UUID::fromNSUUID(destinationUUID);
    _page->callCompletionHandlerForAnimationID(*animationUUID, WebCore::TextAnimationRunMode::RunAnimation);
}

- (void)callCompletionHandlerForAnimationID:(NSUUID *)uuid completionHandler:(void (^)(UITargetedPreview * _Nullable))completionHandler
{
    auto animationUUID = WTF::UUID::fromNSUUID(uuid);

    // Store this completion handler so that it can be called after the execution of the next
    // call to replace the text and eventually use this completion handler to pass the
    // text indicator to UIKit.
    _page->storeDestinationCompletionHandlerForAnimationID(*animationUUID, [protectedSelf = retainPtr(self), completionHandler = makeBlockPtr(completionHandler)] (auto&& textIndicatorData) {
        if (!textIndicatorData) {
            completionHandler(nil);
            return;
        }

        RetainPtr targetedPreview = [protectedSelf _createTargetedPreviewFromTextIndicator:WebCore::TextIndicator::create(*textIndicatorData) previewContainer:[protectedSelf containerForContextMenuHintPreviews]];
        completionHandler(targetedPreview.get());
    });

    _page->callCompletionHandlerForAnimationID(*animationUUID, WebCore::TextAnimationRunMode::RunAnimation);
}

- (void)replacementEffectDidComplete
{
    _page->didEndPartialIntelligenceTextAnimationImpl();
}

#endif

#pragma mark - BETextInteractionDelegate

#if USE(BROWSERENGINEKIT)

- (void)systemWillChangeSelectionForInteraction:(BETextInteraction *)interaction
{
    [self _updateInternalStateBeforeSelectionChange];
}

- (void)systemDidChangeSelectionForInteraction:(BETextInteraction *)interaction
{
    [self _updateInternalStateAfterSelectionChange];
}

#endif // USE(BROWSERENGINEKIT)

#if ENABLE(WRITING_TOOLS)

- (void)willPresentWritingTools
{
    _isPresentingWritingTools = YES;
    // FIXME (rdar://problem/136376688): Stop manually hiding the accessory view once UIKit fixes rdar://136304542.
    self.formAccessoryView.hidden = YES;
    [self reloadInputViews];
}

- (void)didDismissWritingTools
{
    _isPresentingWritingTools = NO;
    // FIXME (rdar://problem/136376688): Stop manually unhiding the accessory view once UIKit fixes rdar://136304542.
    self.formAccessoryView.hidden = NO;
    [self reloadInputViews];
}

- (CocoaWritingToolsResultOptions)allowedWritingToolsResultOptions
{
    return [_webView allowedWritingToolsResultOptions];
}

- (UIWritingToolsBehavior)writingToolsBehavior
{
    return [_webView writingToolsBehavior];
}

- (void)willBeginWritingToolsSession:(WTSession *)session requestContexts:(void (^)(NSArray<WTContext *> *))completion
{
    [_webView willBeginWritingToolsSession:session requestContexts:completion];
}

- (void)didBeginWritingToolsSession:(WTSession *)session contexts:(NSArray<WTContext *> *)contexts
{
    [_webView didBeginWritingToolsSession:session contexts:contexts];
}

- (void)proofreadingSession:(WTSession *)session didReceiveSuggestions:(NSArray<WTTextSuggestion *> *)suggestions processedRange:(NSRange)range inContext:(WTContext *)context finished:(BOOL)finished
{
    [_webView proofreadingSession:session didReceiveSuggestions:suggestions processedRange:range inContext:context finished:finished];
}

- (void)proofreadingSession:(WTSession *)session didUpdateState:(WTTextSuggestionState)state forSuggestionWithUUID:(NSUUID *)suggestionUUID inContext:(WTContext *)context
{
    [_webView proofreadingSession:session didUpdateState:state forSuggestionWithUUID:suggestionUUID inContext:context];
}

- (void)didEndWritingToolsSession:(WTSession *)session accepted:(BOOL)accepted
{
    [_webView didEndWritingToolsSession:session accepted:accepted];
}

- (void)compositionSession:(WTSession *)session didReceiveText:(NSAttributedString *)attributedText replacementRange:(NSRange)range inContext:(WTContext *)context finished:(BOOL)finished
{
    [_webView compositionSession:session didReceiveText:attributedText replacementRange:range inContext:context finished:finished];
}

- (void)writingToolsSession:(WTSession *)session didReceiveAction:(WTAction)action
{
    [_webView writingToolsSession:session didReceiveAction:action];
}

static inline WKTextAnimationType toWKTextAnimationType(WebCore::TextAnimationType style)
{
    switch (style) {
    case WebCore::TextAnimationType::Initial:
        return WKTextAnimationTypeInitial;
    case WebCore::TextAnimationType::Source:
        return WKTextAnimationTypeSource;
    case WebCore::TextAnimationType::Final:
        return WKTextAnimationTypeFinal;
    }
}

- (void)addTextAnimationForAnimationID:(NSUUID *)uuid withData:(const WebCore::TextAnimationData&)data
{
    if (!_page->preferences().textAnimationsEnabled())
        return;

    if (data.style == WebCore::TextAnimationType::Final)
        [_sourceAnimationIDtoDestinationAnimationID setObject:uuid forKey:data.sourceAnimationUUID.value_or(WTF::UUID(WTF::UUID::emptyValue)).createNSUUID().get()];

    if (!_textAnimationManager)
        _textAnimationManager = adoptNS([WebKit::allocWKTextAnimationManagerInstance() initWithDelegate:self]);

    [_textAnimationManager addTextAnimationForAnimationID:uuid withStyleType:toWKTextAnimationType(data.style)];
}

- (void)removeTextAnimationForAnimationID:(NSUUID *)uuid
{
    if (!uuid)
        return;

    if (!_page->preferences().textAnimationsEnabled())
        return;

    if (!_textAnimationManager)
        return;

    [_textAnimationManager removeTextAnimationForAnimationID:uuid];
}

#endif

#if ENABLE(MULTI_REPRESENTATION_HEIC)

- (BOOL)supportsAdaptiveImageGlyph
{
    if (self.webView._isEditable || self.webView.configuration._multiRepresentationHEICInsertionEnabled)
        return _page->editorState().isContentRichlyEditable;

    return NO;
}

- (void)insertAdaptiveImageGlyph:(NSAdaptiveImageGlyph *)adaptiveImageGlyph replacementRange:(UITextRange *)replacementRange
{
    _page->insertMultiRepresentationHEIC(adaptiveImageGlyph.imageContent, adaptiveImageGlyph.contentDescription);
}

#endif

- (void)_closeCurrentTypingCommand
{
    if (!_page)
        return;

    _page->closeCurrentTypingCommand();
}

- (UIView *)_selectionContainerViewAboveText
{
    // FIXME: Consider adding RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED()
    // once the changes in rdar://150645383 are in all relevant builds.
    return self._selectionContainerViewInternal;
}

- (UIView *)selectionContainerView
{
    // FIXME: Consider adding RELEASE_ASSERT_ASYNC_TEXT_INTERACTIONS_DISABLED()
    // once the changes in rdar://150645383 are in all relevant builds.
    return self._selectionContainerViewInternal;
}

- (UIView *)_selectionContainerViewInternal
{
    if (_cachedSelectionContainerView)
        return _cachedSelectionContainerView;

    _cachedSelectionContainerView = [&] -> UIView * {
        if (!self.selectionHonorsOverflowScrolling)
            return self;

        if (!_page->editorState().hasVisualData())
            return self;

        RetainPtr enclosingView = [self _viewForLayerID:_page->editorState().visualData->enclosingLayerID];
        if (!enclosingView)
            return self;

        for (UIView *selectedView in self.allViewsIntersectingSelectionRange) {
            if (enclosingView != selectedView && ![enclosingView _wk_isAncestorOf:selectedView])
                return self;
        }
        return enclosingView.get();
    }();

    ASSERT(_cachedSelectionContainerView);
    return _cachedSelectionContainerView;
}

- (UIView *)_viewForLayerID:(std::optional<WebCore::PlatformLayerIdentifier>)layerID
{
    WeakPtr drawingArea = downcast<WebKit::RemoteLayerTreeDrawingAreaProxy>(_page->drawingArea());
    if (!drawingArea)
        return nil;

    WeakPtr layerTreeNode = drawingArea->remoteLayerTreeHost().nodeForID(layerID);
    if (!layerTreeNode)
        return nil;

    return layerTreeNode->uiView();
}

- (NSArray<UIView *> *)allViewsIntersectingSelectionRange
{
    if (!self.selectionHonorsOverflowScrolling)
        return @[ ];

    if (!_page->editorState().hasVisualData())
        return @[ ];

    return createNSArray(_page->editorState().visualData->intersectingLayerIDs, [&](auto& layerID) {
        return [self _viewForLayerID:layerID];
    }).autorelease();
}

- (BOOL)_shouldHideSelectionDuringOverflowScroll:(UIScrollView *)scrollView
{
    if (self._selectionContainerViewInternal == self)
        return YES;

    auto& state = _page->editorState();
    if (!state.hasVisualData())
        return NO;

    auto enclosingScrollingNodeID = state.visualData->enclosingScrollingNodeID;
    RetainPtr enclosingScroller = [self _scrollViewForScrollingNodeID:enclosingScrollingNodeID] ?: self._scroller;
    if (!enclosingScroller || ![enclosingScroller _wk_isAncestorOf:scrollView])
        return NO;

    auto isInSeparateScrollView = [&](std::optional<WebCore::ScrollingNodeID> endpointScrollingNodeID) -> bool {
        if (endpointScrollingNodeID == enclosingScrollingNodeID)
            return false;

        RetainPtr endpointScroller = [self _scrollViewForScrollingNodeID:endpointScrollingNodeID] ?: self._scroller;
        return endpointScroller == scrollView || [scrollView _wk_isAncestorOf:endpointScroller.get()];
    };

    if (isInSeparateScrollView(state.visualData->scrollingNodeIDAtStart))
        return YES;

    if (isInSeparateScrollView(state.visualData->scrollingNodeIDAtEnd))
        return YES;

    return NO;
}

- (BOOL)_canStartNavigationSwipeAtLastInteractionLocation
{
    RetainPtr lastHitView = [self hitTest:_lastInteractionLocation withEvent:nil];
    if (!lastHitView)
        return YES;

    RetainPtr mainScroller = [_webView scrollView];
    RetainPtr innerScroller = dynamic_objc_cast<UIScrollView>(lastHitView.get()) ?: [lastHitView _wk_parentScrollView];
    for (RetainPtr scroller = innerScroller; scroller; scroller = [scroller _wk_parentScrollView]) {
        if ([scroller isDragging])
            return NO;

        if ([scroller panGestureRecognizer]._wk_hasRecognizedOrEnded)
            return NO;

        if (scroller == mainScroller)
            break;
    }

    auto touchActions = WebKit::touchActionsForPoint(self, WebCore::roundedIntPoint(_lastInteractionLocation));
    using enum WebCore::TouchAction;
    return touchActions.containsAny({ Auto, PanX, Manipulation });
}

#if ENABLE(MODEL_PROCESS)
- (void)_willInvalidateDraggedModelWithContainerView:(UIView *)containerView
{
    // The model is being removed while it's in the middle of a drag.
    // We need to make sure it stays in a window by adding it to the
    // WKContentView's container for drag previews, but keep it hidden.
    [containerView setFrame:CGRectZero];
    containerView.hidden = YES;
    [self.containerForDragPreviews addSubview:containerView];
}
#endif

#if HAVE(UI_CONVERSATION_CONTEXT)

- (UIConversationContext *)_conversationContext
{
    if (self._requiresLegacyTextInputTraits)
        return self.textInputTraits.conversationContext;

    return self.extendedTraitsDelegate.conversationContext;
}

- (void)_setConversationContext:(UIConversationContext *)context
{
    [_legacyTextInputTraits setConversationContext:context];
    [_extendedTextInputTraits setConversationContext:context];

    [self.inputDelegate conversationContext:context didChange:self];
}

- (void)insertInputSuggestion:(UIInputSuggestion *)suggestion
{
    RetainPtr webView = _webView.get();
    RetainPtr delegate = [webView UIDelegate];

    if (![delegate respondsToSelector:@selector(webView:insertInputSuggestion:)]) {
        RELEASE_LOG_ERROR(TextInput, "Ignored input suggestion (UI delegate does not implement -webView:insertInputSuggestion:)");
        return;
    }

    [delegate webView:webView.get() insertInputSuggestion:suggestion];
}

#endif // HAVE(UI_CONVERSATION_CONTEXT)

@end

@implementation WKContentView (WKTesting)

- (void)selectWordBackwardForTesting
{
    _autocorrectionContextNeedsUpdate = YES;
    _page->selectWordBackward();
}

- (void)_doAfterReceivingEditDragSnapshotForTesting:(dispatch_block_t)action
{
#if ENABLE(DRAG_SUPPORT)
    ASSERT(!_actionToPerformAfterReceivingEditDragSnapshot);
    if (_waitingForEditDragSnapshot) {
        _actionToPerformAfterReceivingEditDragSnapshot = action;
        return;
    }
#endif
    action();
}

#if !PLATFORM(WATCHOS)
- (WKDateTimeInputControl *)dateTimeInputControl
{
    if ([_inputPeripheral isKindOfClass:WKDateTimeInputControl.class])
        return (WKDateTimeInputControl *)_inputPeripheral.get();
    return nil;
}
#endif

- (WKFormSelectControl *)selectControl
{
    if ([_inputPeripheral isKindOfClass:WKFormSelectControl.class])
        return (WKFormSelectControl *)_inputPeripheral.get();
    return nil;
}

#if ENABLE(DRAG_SUPPORT)

- (BOOL)isAnimatingDragCancel
{
    return _isAnimatingDragCancel;
}

#endif // ENABLE(DRAG_SUPPORT)

- (void)_simulateTextEntered:(NSString *)text
{
#if HAVE(PEPPER_UI_CORE)
    if ([_presentedFullScreenInputViewController isKindOfClass:[WKTextInputListViewController class]]) {
        [(WKTextInputListViewController *)_presentedFullScreenInputViewController.get() enterText:text];
        return;
    }

#if HAVE(QUICKBOARD_CONTROLLER)
    if (_presentedQuickboardController) {
        id <PUICQuickboardControllerDelegate> delegate = [_presentedQuickboardController delegate];
        ASSERT(delegate == self);
        auto string = adoptNS([[NSAttributedString alloc] initWithString:text]);
        [delegate quickboardController:_presentedQuickboardController.get() textInputValueDidChange:string.get()];
        return;
    }
#endif // HAVE(QUICKBOARD_CONTROLLER)
#endif // HAVE(PEPPER_UI_CORE)

    [self insertText:text];
}

- (void)_simulateElementAction:(_WKElementActionType)actionType atLocation:(CGPoint)location
{
    _layerTreeTransactionIdAtLastInteractionStart = downcast<WebKit::RemoteLayerTreeDrawingAreaProxy>(*_page->drawingArea()).lastCommittedMainFrameLayerTreeTransactionID();
    [self doAfterPositionInformationUpdate:[actionType, self, protectedSelf = retainPtr(self)] (WebKit::InteractionInformationAtPosition info) {
        _WKActivatedElementInfo *elementInfo = [_WKActivatedElementInfo activatedElementInfoWithInteractionInformationAtPosition:info userInfo:nil];
        _WKElementAction *action = [_WKElementAction _elementActionWithType:actionType info:elementInfo assistant:_actionSheetAssistant.get()];
        [action runActionWithElementInfo:elementInfo];
    } forRequest:WebKit::InteractionInformationRequest(WebCore::roundedIntPoint(location))];
}

- (void)_simulateLongPressActionAtLocation:(CGPoint)location
{
    RetainPtr<WKContentView> protectedSelf = self;
    [self doAfterPositionInformationUpdate:[protectedSelf] (WebKit::InteractionInformationAtPosition) {
        if (SEL action = [protectedSelf _actionForLongPress])
            [protectedSelf performSelector:action];
    } forRequest:WebKit::InteractionInformationRequest(WebCore::roundedIntPoint(location))];
}

- (void)selectFormAccessoryPickerRow:(NSInteger)rowIndex
{
#if HAVE(PEPPER_UI_CORE)
    if ([_presentedFullScreenInputViewController isKindOfClass:[WKSelectMenuListViewController class]])
        [(WKSelectMenuListViewController *)_presentedFullScreenInputViewController.get() selectItemAtIndex:rowIndex];
#else
    if ([_inputPeripheral isKindOfClass:[WKFormSelectControl class]])
        [(WKFormSelectControl *)_inputPeripheral selectRow:rowIndex inComponent:0 extendingSelection:NO];
#endif
}

- (BOOL)selectFormAccessoryHasCheckedItemAtRow:(long)rowIndex
{
#if !HAVE(PEPPER_UI_CORE)
    if ([_inputPeripheral isKindOfClass:[WKFormSelectControl self]])
        return [(WKFormSelectControl *)_inputPeripheral selectFormAccessoryHasCheckedItemAtRow:rowIndex];
#endif
    return NO;
}

- (void)setSelectedColorForColorPicker:(UIColor *)color
{
    if ([_inputPeripheral isKindOfClass:[WKFormColorControl class]])
        [(WKFormColorControl *)_inputPeripheral selectColor:color];
}

- (NSString *)textContentTypeForTesting
{
#if HAVE(PEPPER_UI_CORE)
    if ([_presentedFullScreenInputViewController isKindOfClass:[WKTextInputListViewController class]])
        return [(WKTextInputListViewController *)_presentedFullScreenInputViewController textInputContext].textContentType;
#if HAVE(QUICKBOARD_CONTROLLER)
    if (_presentedQuickboardController)
        return [_presentedQuickboardController textInputContext].textContentType;
#endif // HAVE(QUICKBOARD_CONTROLLER)
#endif // HAVE(PEPPER_UI_CORE)
#if USE(BROWSERENGINEKIT)
    if (!self._requiresLegacyTextInputTraits)
        return self.extendedTraitsDelegate.textContentType;
#endif
    return self.textInputTraits.textContentType;
}

- (NSString *)selectFormPopoverTitle
{
    if (![_inputPeripheral isKindOfClass:[WKFormSelectControl class]])
        return nil;

    return [(WKFormSelectControl *)_inputPeripheral selectFormPopoverTitle];
}

- (NSString *)formInputLabel
{
#if HAVE(PEPPER_UI_CORE)
    return [self inputLabelTextForViewController:_presentedFullScreenInputViewController.get()];
#else
    return nil;
#endif
}

- (void)setTimePickerValueToHour:(NSInteger)hour minute:(NSInteger)minute
{
#if HAVE(PEPPER_UI_CORE)
    if ([_presentedFullScreenInputViewController isKindOfClass:[WKTimePickerViewController class]])
        [(WKTimePickerViewController *)_presentedFullScreenInputViewController.get() setHour:hour minute:minute];
#else
    if ([_inputPeripheral isKindOfClass:[WKDateTimeInputControl class]])
        [(WKDateTimeInputControl *)_inputPeripheral.get() setTimePickerHour:hour minute:minute];
#endif
}

- (double)timePickerValueHour
{
#if !PLATFORM(WATCHOS)
    if ([_inputPeripheral isKindOfClass:[WKDateTimeInputControl class]])
        return [(WKDateTimeInputControl *)_inputPeripheral.get() timePickerValueHour];
#endif
    return -1;
}

- (double)timePickerValueMinute
{
#if !PLATFORM(WATCHOS)
    if ([_inputPeripheral isKindOfClass:[WKDateTimeInputControl class]])
        return [(WKDateTimeInputControl *)_inputPeripheral.get() timePickerValueMinute];
#endif
    return -1;
}

- (NSDictionary *)_contentsOfUserInterfaceItem:(NSString *)userInterfaceItem
{
    if ([userInterfaceItem isEqualToString:@"actionSheet"])
        return @{ userInterfaceItem: [_actionSheetAssistant currentlyAvailableActionTitles] };

#if HAVE(LINK_PREVIEW)
    if ([userInterfaceItem isEqualToString:@"contextMenu"]) {
        auto itemTitles = adoptNS([NSMutableArray<NSString *> new]);
        [self.contextMenuInteraction updateVisibleMenuWithBlock:[&itemTitles](UIMenu *menu) -> UIMenu * {
            for (UIMenuElement *child in menu.children)
                [itemTitles addObject:child.title];
            return menu;
        }];
        if (self._shouldUseContextMenus) {
            return @{ userInterfaceItem: @{
                @"url": _positionInformation.url.isValid() ? WTF::userVisibleString(_positionInformation.url.createNSURL().get()) : @"",
                @"isLink": [NSNumber numberWithBool:_positionInformation.isLink],
                @"isImage": [NSNumber numberWithBool:_positionInformation.isImage],
                @"imageURL": _positionInformation.imageURL.isValid() ? WTF::userVisibleString(_positionInformation.imageURL.createNSURL().get()) : @"",
                @"items": itemTitles.get()
            } };
        }
        NSString *url = [_previewItemController previewData][UIPreviewDataLink];
        return @{ userInterfaceItem: @{
            @"url": url,
            @"isLink": [NSNumber numberWithBool:_positionInformation.isLink],
            @"isImage": [NSNumber numberWithBool:_positionInformation.isImage],
            @"imageURL": _positionInformation.imageURL.isValid() ? WTF::userVisibleString(_positionInformation.imageURL.createNSURL().get()) : @"",
            @"items": itemTitles.get()
        } };
    }
#endif

#if ENABLE(MEDIA_CONTROLS_CONTEXT_MENUS) && USE(UICONTEXTMENU)
    if ([userInterfaceItem isEqualToString:@"mediaControlsContextMenu"])
        return @{ userInterfaceItem: [_actionSheetAssistant currentlyAvailableMediaControlsContextMenuItems] };
#endif // ENABLE(MEDIA_CONTROLS_CONTEXT_MENUS) && USE(UICONTEXTMENU)

    if ([userInterfaceItem isEqualToString:@"fileUploadPanelMenu"]) {
        if (!_fileUploadPanel)
            return @{ userInterfaceItem: @[] };
        return @{ userInterfaceItem: [_fileUploadPanel currentAvailableActionTitles] };
    }

    if ([userInterfaceItem isEqualToString:@"selectMenu"]) {
        if (auto *menuItemTitles = [self.selectControl menuItemTitles])
            return @{ userInterfaceItem: menuItemTitles };
        return @{ userInterfaceItem: @[] };
    }

    return nil;
}

- (void)_dismissContactPickerWithContacts:(NSArray *)contacts
{
#if HAVE(CONTACTSUI)
    [_contactPicker dismissWithContacts:contacts];
#endif
}

#if ENABLE(MODEL_PROCESS)
- (void)_simulateModelInteractionPanGestureBeginAtPoint:(CGPoint)hitPoint
{
    [self modelInteractionPanGestureDidBeginAtPoint:hitPoint];
}

- (void)_simulateModelInteractionPanGestureUpdateAtPoint:(CGPoint)hitPoint
{
    [self modelInteractionPanGestureDidUpdateWithPoint:hitPoint];
}

- (NSDictionary *)_stageModeInfoForTesting
{
    if (!_stageModeSession)
        return @{ };

    return @{
        @"awaitingResult" : @(_stageModeSession->isPreparingForInteraction),
        @"hitTestSuccessful" : @(_stageModeSession->nodeID.has_value()),
    };
}
#endif

- (UITapGestureRecognizer *)singleTapGestureRecognizer
{
    return _singleTapGestureRecognizer.get();
}

- (void)_simulateSelectionStart
{
    _usingGestureForSelection = YES;
    _lastSelectionDrawingInfo.type = WebKit::WKSelectionDrawingInfo::SelectionType::Range;
}

#if ENABLE(ACCESSIBILITY_ANIMATION_CONTROL)
- (BOOL)_allowAnimationControls
{
    return self.webView._allowAnimationControls;
}
#endif

- (void)dismissFormAccessoryView
{
#if !PLATFORM(WATCHOS)
    if (auto dateInput = self.dateTimeInputControl; [dateInput dismissWithAnimationForTesting])
        return;
#endif
    [self accessoryDone];
}

- (void)_selectDataListOption:(NSInteger)optionIndex
{
    [_dataListSuggestionsControl didSelectOptionAtIndex:optionIndex];
}

- (void)_setDataListSuggestionsControl:(WKDataListSuggestionsControl *)control
{
    _dataListSuggestionsControl = control;
}

- (BOOL)isShowingDataListSuggestions
{
    return [_dataListSuggestionsControl isShowingSuggestions];
}

- (UIWKTextInteractionAssistant *)textInteractionAssistant
{
    return [_textInteractionWrapper textInteractionAssistant];
}

@end

#if HAVE(LINK_PREVIEW)

static NSString *previewIdentifierForElementAction(_WKElementAction *action)
{
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    switch (action.type) {
    case _WKElementActionTypeOpen:
        return WKPreviewActionItemIdentifierOpen;
    case _WKElementActionTypeCopy:
        return WKPreviewActionItemIdentifierCopy;
#if !defined(TARGET_OS_IOS) || TARGET_OS_IOS
    case _WKElementActionTypeAddToReadingList:
        return WKPreviewActionItemIdentifierAddToReadingList;
#endif
    case _WKElementActionTypeShare:
        return WKPreviewActionItemIdentifierShare;
    default:
        return nil;
    }
ALLOW_DEPRECATED_DECLARATIONS_END
    ASSERT_NOT_REACHED();
    return nil;
}

@implementation WKContentView (WKInteractionPreview)

#if USE(UICONTEXTMENU)
- (UIContextMenuInteraction *)contextMenuInteraction
{
    return [_textInteractionWrapper contextMenuInteraction];
}
#endif // USE(UICONTEXTMENU)

- (void)_registerPreview
{
    if (!self.webView.allowsLinkPreview)
        return;

#if USE(UICONTEXTMENU)
    if (self._shouldUseContextMenus) {
        _contextMenuHasRequestedLegacyData = NO;
        [_textInteractionWrapper setExternalContextMenuInteractionDelegate:self];

        if (id<_UIClickInteractionDriving> driver = self.webView.configuration._clickInteractionDriverForTesting)
            [self.contextMenuInteraction presentationInteraction].overrideDrivers = @[driver];
        return;
    }
#endif // USE(UICONTEXTMENU)

    _previewItemController = adoptNS([[UIPreviewItemController alloc] initWithView:self]);
    [_previewItemController setDelegate:self];
    _previewGestureRecognizer = _previewItemController.get().presentationGestureRecognizer;
    if ([_previewItemController respondsToSelector:@selector(presentationSecondaryGestureRecognizer)])
        _previewSecondaryGestureRecognizer = _previewItemController.get().presentationSecondaryGestureRecognizer;
}

- (void)_unregisterPreview
{
#if USE(UICONTEXTMENU)
    if (self._shouldUseContextMenus)
        return;
#endif

    [_previewItemController setDelegate:nil];
    _previewGestureRecognizer = nil;
    _previewSecondaryGestureRecognizer = nil;
    _previewItemController = nil;
}

#if USE(UICONTEXTMENU)

static bool needsDeprecatedPreviewAPI(id<WKUIDelegate> delegate)
{
    // FIXME: Replace these with booleans in UIDelegate.h.
    // Note that we explicitly do not test for @selector(_webView:contextMenuDidEndForElement:)
    // and @selector(webView:contextMenuWillPresentForElement) since the methods are used by MobileSafari
    // to manage state despite the app not moving to the new API.

    return delegate
    && ![delegate respondsToSelector:@selector(_webView:contextMenuConfigurationForElement:completionHandler:)]
    && ![delegate respondsToSelector:@selector(webView:contextMenuConfigurationForElement:completionHandler:)]
    && ![delegate respondsToSelector:@selector(_webView:contextMenuForElement:willCommitWithAnimator:)]
    && ![delegate respondsToSelector:@selector(webView:contextMenuForElement:willCommitWithAnimator:)]
    && ![delegate respondsToSelector:@selector(_webView:contextMenuWillPresentForElement:)]
    && ![delegate respondsToSelector:@selector(webView:contextMenuDidEndForElement:)]
    && ([delegate respondsToSelector:@selector(webView:shouldPreviewElement:)]
        || [delegate respondsToSelector:@selector(webView:previewingViewControllerForElement:defaultActions:)]
        || [delegate respondsToSelector:@selector(webView:commitPreviewingViewController:)]
        || [delegate respondsToSelector:@selector(_webView:previewViewControllerForURL:defaultActions:elementInfo:)]
        || [delegate respondsToSelector:@selector(_webView:previewViewControllerForURL:)]
        || [delegate respondsToSelector:@selector(_webView:previewViewControllerForImage:alternateURL:defaultActions:elementInfo:)]);
}

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
static NSArray<WKPreviewAction *> *wkLegacyPreviewActionsFromElementActions(NSArray<_WKElementAction *> *elementActions, _WKActivatedElementInfo *elementInfo)
{
    NSMutableArray<WKPreviewAction *> *previewActions = [NSMutableArray arrayWithCapacity:[elementActions count]];
    for (_WKElementAction *elementAction in elementActions) {
        WKPreviewAction *previewAction = [WKPreviewAction actionWithIdentifier:previewIdentifierForElementAction(elementAction) title:elementAction.title style:UIPreviewActionStyleDefault handler:^(UIPreviewAction *, UIViewController *) {
            [elementAction runActionWithElementInfo:elementInfo];
        }];
        previewAction.image = [_WKElementAction imageForElementActionType:elementAction.type];
        [previewActions addObject:previewAction];
    }
    return previewActions;
}

static UIAction *uiActionForLegacyPreviewAction(UIPreviewAction *previewAction, UIViewController *previewViewController)
{
    // UIPreviewActionItem.image is SPI, so no external clients will be able
    // to provide glyphs for actions <rdar://problem/50151855>.
    // However, they should migrate to the new API.

    return [UIAction actionWithTitle:previewAction.title image:previewAction.image identifier:nil handler:^(UIAction *action) {
        previewAction.handler(previewAction, previewViewController);
    }];
}
ALLOW_DEPRECATED_DECLARATIONS_END

static NSArray<UIMenuElement *> *menuElementsFromLegacyPreview(UIViewController *previewViewController)
{
    if (!previewViewController)
        return nil;

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    NSArray<id<UIPreviewActionItem>> *previewActions = previewViewController.previewActionItems;
    if (!previewActions || ![previewActions count])
        return nil;

    auto actions = [NSMutableArray arrayWithCapacity:previewActions.count];

    for (UIPreviewAction *previewAction in previewActions)
        [actions addObject:uiActionForLegacyPreviewAction(previewAction, previewViewController)];
ALLOW_DEPRECATED_DECLARATIONS_END

    return actions;
}

static NSMutableArray<UIMenuElement *> *menuElementsFromDefaultActions(const RetainPtr<NSArray>& defaultElementActions, RetainPtr<_WKActivatedElementInfo> elementInfo)
{
    if (!defaultElementActions || !defaultElementActions.get().count)
        return nil;

    auto actions = [NSMutableArray arrayWithCapacity:defaultElementActions.get().count];

    for (_WKElementAction *elementAction in defaultElementActions.get())
        [actions addObject:[elementAction uiActionForElementInfo:elementInfo.get()]];

    return actions;
}

static UIMenu *menuFromLegacyPreviewOrDefaultActions(UIViewController *previewViewController, const RetainPtr<NSArray>& defaultElementActions, RetainPtr<_WKActivatedElementInfo> elementInfo, NSString *title = nil)
{
    auto actions = menuElementsFromLegacyPreview(previewViewController);
    if (!actions)
        actions = menuElementsFromDefaultActions(defaultElementActions, elementInfo);

    return [UIMenu menuWithTitle:title children:actions];
}

- (void)assignLegacyDataForContextMenuInteraction
{
    ASSERT(!_contextMenuHasRequestedLegacyData);
    if (_contextMenuHasRequestedLegacyData)
        return;
    _contextMenuHasRequestedLegacyData = YES;

    if (!_webView)
        return;
    auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate);
    if (!uiDelegate)
        return;

    const auto& url = _positionInformation.url;

    _page->startInteractionWithPositionInformation(_positionInformation);

    RetainPtr<UIViewController> previewViewController;

    auto elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithInteractionInformationAtPosition:_positionInformation isUsingAlternateURLForImage:NO userInfo:nil]);

    ASSERT_IMPLIES(_positionInformation.isImage, _positionInformation.image);
    if (_positionInformation.isLink) {
        _longPressCanClick = NO;

        RetainPtr<NSArray<_WKElementAction *>> defaultActionsFromAssistant = [_actionSheetAssistant defaultActionsForLinkSheet:elementInfo.get()];

        // FIXME: Animated images go here.

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate respondsToSelector:@selector(webView:previewingViewControllerForElement:defaultActions:)]) {
            auto defaultActions = wkLegacyPreviewActionsFromElementActions(defaultActionsFromAssistant.get(), elementInfo.get());
            auto previewElementInfo = adoptNS([[WKPreviewElementInfo alloc] _initWithLinkURL:url.createNSURL().get()]);
            // FIXME: Clients using this legacy API will always show their previewViewController and ignore _showLinkPreviews.
            previewViewController = [uiDelegate webView:self.webView previewingViewControllerForElement:previewElementInfo.get() defaultActions:defaultActions];
        } else if ([uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForURL:defaultActions:elementInfo:)])
            previewViewController = [uiDelegate _webView:self.webView previewViewControllerForURL:url.createNSURL().get() defaultActions:defaultActionsFromAssistant.get() elementInfo:elementInfo.get()];
        else if ([uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForURL:)])
            previewViewController = [uiDelegate _webView:self.webView previewViewControllerForURL:url.createNSURL().get()];
ALLOW_DEPRECATED_DECLARATIONS_END

        // Previously, UIPreviewItemController would detect the case where there was no previewViewController
        // and create one. We need to replicate this code for the new API.
        if (!previewViewController || [url.createNSURL() iTunesStoreURL]) {
            auto ddContextMenuActionClass = PAL::getDDContextMenuActionClass();
            BEGIN_BLOCK_OBJC_EXCEPTIONS
            NSDictionary *context = [self dataDetectionContextForPositionInformation:_positionInformation];
            RetainPtr<UIContextMenuConfiguration> dataDetectorsResult = [ddContextMenuActionClass contextMenuConfigurationForURL:url.createNSURL().get() identifier:_positionInformation.dataDetectorIdentifier.createNSString().get() selectedText:self.selectedText results:_positionInformation.dataDetectorResults.get() inView:self context:context menuIdentifier:nil];
            if (_showLinkPreviews && dataDetectorsResult && dataDetectorsResult.get().previewProvider)
                _contextMenuLegacyPreviewController = dataDetectorsResult.get().previewProvider();
            if (dataDetectorsResult && dataDetectorsResult.get().actionProvider) {
                auto menuElements = menuElementsFromDefaultActions(defaultActionsFromAssistant, elementInfo);
                _contextMenuLegacyMenu = dataDetectorsResult.get().actionProvider(menuElements);
            }
            END_BLOCK_OBJC_EXCEPTIONS
            return;
        }

        _contextMenuLegacyMenu = menuFromLegacyPreviewOrDefaultActions(previewViewController.get(), defaultActionsFromAssistant, elementInfo);

    } else if (_positionInformation.isImage && _positionInformation.image) {
        RetainPtr nsURL = url.createNSURL();
        RetainPtr<NSDictionary> imageInfo;
        auto cgImage = _positionInformation.image->makeCGImageCopy();
        auto uiImage = adoptNS([[UIImage alloc] initWithCGImage:cgImage.get()]);

        if ([uiDelegate respondsToSelector:@selector(_webView:alternateURLFromImage:userInfo:)]) {
            NSDictionary *userInfo;
            nsURL = [uiDelegate _webView:self.webView alternateURLFromImage:uiImage.get() userInfo:&userInfo];
            imageInfo = userInfo;
        }

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate respondsToSelector:@selector(_webView:willPreviewImageWithURL:)])
            [uiDelegate _webView:self.webView willPreviewImageWithURL:_positionInformation.imageURL.createNSURL().get()];

        RetainPtr<NSArray<_WKElementAction *>> defaultActionsFromAssistant = [_actionSheetAssistant defaultActionsForImageSheet:elementInfo.get()];

        if (imageInfo && [uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForImage:alternateURL:defaultActions:elementInfo:)])
            previewViewController = [uiDelegate _webView:self.webView previewViewControllerForImage:uiImage.get() alternateURL:nsURL.get() defaultActions:defaultActionsFromAssistant.get() elementInfo:elementInfo.get()];
        else
            previewViewController = adoptNS([[WKImagePreviewViewController alloc] initWithCGImage:cgImage defaultActions:defaultActionsFromAssistant.get() elementInfo:elementInfo.get()]);
ALLOW_DEPRECATED_DECLARATIONS_END

        _contextMenuLegacyMenu = menuFromLegacyPreviewOrDefaultActions(previewViewController.get(), defaultActionsFromAssistant, elementInfo, _positionInformation.title.createNSString().get());
    }

    _contextMenuLegacyPreviewController = WTFMove(previewViewController);
}

- (UIContextMenuConfiguration *)contextMenuInteraction:(UIContextMenuInteraction *)interaction configurationForMenuAtLocation:(CGPoint)location
{
    RetainPtr<UIContextMenuConfiguration> configuration;
#if USE(BROWSERENGINEKIT)
    if (self._shouldUseUIContextMenuAsyncConfiguration) {
        auto asyncConfiguration = adoptNS([[BEContextMenuConfiguration alloc] init]);
        [self _internalContextMenuInteraction:interaction configurationForMenuAtLocation:location completion:[asyncConfiguration](UIContextMenuConfiguration *finalConfiguration) {
            [asyncConfiguration fulfillUsingConfiguration:finalConfiguration];
        }];
        configuration = asyncConfiguration.get();
    }
#endif // USE(BROWSERENGINEKIT)
    return configuration.autorelease();
}

- (void)_internalContextMenuInteraction:(UIContextMenuInteraction *)interaction configurationForMenuAtLocation:(CGPoint)location completion:(void(^)(UIContextMenuConfiguration *))completion
{
    _useContextMenuInteractionDismissalPreview = YES;

    if (!_webView)
        return completion(nil);

    if (!self.webView.configuration._longPressActionsEnabled)
        return completion(nil);

    auto getConfigurationAndContinue = [weakSelf = WeakObjCPtr<WKContentView>(self), interaction = retainPtr(interaction), completion = makeBlockPtr(completion)] (WebKit::ProceedWithTextSelectionInImage proceedWithTextSelectionInImage) {
        auto strongSelf = weakSelf.get();
        if (!strongSelf || proceedWithTextSelectionInImage == WebKit::ProceedWithTextSelectionInImage::Yes) {
            completion(nil);
            return;
        }

        strongSelf->_showLinkPreviews = true;
        if (NSNumber *value = [[NSUserDefaults standardUserDefaults] objectForKey:webkitShowLinkPreviewsPreferenceKey])
            strongSelf->_showLinkPreviews = value.boolValue;

        WebKit::InteractionInformationRequest request { WebCore::roundedIntPoint([interaction locationInView:strongSelf.get()]) };
        request.includeSnapshot = true;
        request.includeLinkIndicator = true;
        request.linkIndicatorShouldHaveLegacyMargins = ![strongSelf _shouldUseContextMenus];
        request.gatherAnimations = [strongSelf->_webView _allowAnimationControls];

        [strongSelf doAfterPositionInformationUpdate:[weakSelf = WeakObjCPtr<WKContentView>(strongSelf.get()), completion] (WebKit::InteractionInformationAtPosition) {
            if (auto strongSelf = weakSelf.get())
                [strongSelf continueContextMenuInteraction:completion.get()];
            else
                completion(nil);
        } forRequest:request];
    };

#if ENABLE(IMAGE_ANALYSIS)
    [self _doAfterPendingImageAnalysis:getConfigurationAndContinue];
#else
    getConfigurationAndContinue(WebKit::ProceedWithTextSelectionInImage::No);
#endif
}

- (UIAction *)placeholderForDynamicallyInsertedImageAnalysisActions
{
#if ENABLE(IMAGE_ANALYSIS)
    switch (_dynamicImageAnalysisContextMenuState) {
    case WebKit::DynamicImageAnalysisContextMenuState::NotWaiting:
        return nil;
    case WebKit::DynamicImageAnalysisContextMenuState::WaitingForImageAnalysis:
    case WebKit::DynamicImageAnalysisContextMenuState::WaitingForVisibleMenu: {
        // FIXME: This placeholder item warrants its own unique item identifier; however, to ensure that internal clients
        // that already allow image analysis items (e.g. Mail) continue to allow this placeholder item, we reuse an existing
        // image analysis identifier.
        RetainPtr placeholder = [UIAction actionWithTitle:@"" image:nil identifier:elementActionTypeToUIActionIdentifier(_WKElementActionTypeRevealImage) handler:^(UIAction *) { }];
        [placeholder setAttributes:UIMenuElementAttributesHidden];
        return placeholder.autorelease();
    }
    }
#endif // ENABLE(IMAGE_ANALYSIS)
    return nil;
}

- (void)continueContextMenuInteraction:(void(^)(UIContextMenuConfiguration *))continueWithContextMenuConfiguration
{
    if (!self.window)
        return continueWithContextMenuConfiguration(nil);

    if (!_positionInformation.touchCalloutEnabled)
        return continueWithContextMenuConfiguration(nil);

    if (!_positionInformation.isLink && !_positionInformation.isImage && !_positionInformation.isAttachment && !self.positionInformationHasImageOverlayDataDetector)
        return continueWithContextMenuConfiguration(nil);

    URL linkURL = _positionInformation.url;

    if (_positionInformation.isLink && linkURL.isEmpty())
        return continueWithContextMenuConfiguration(nil);

    auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate);

    if (needsDeprecatedPreviewAPI(uiDelegate)) {
        if (_positionInformation.isLink) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if ([uiDelegate respondsToSelector:@selector(webView:shouldPreviewElement:)]) {
                auto previewElementInfo = adoptNS([[WKPreviewElementInfo alloc] _initWithLinkURL:linkURL.createNSURL().get()]);
                if (![uiDelegate webView:self.webView shouldPreviewElement:previewElementInfo.get()])
                    return continueWithContextMenuConfiguration(nil);
            }
ALLOW_DEPRECATED_DECLARATIONS_END

            // FIXME: Support JavaScript urls here. But make sure they don't show a preview.
            // <rdar://problem/50572283>
            if (!linkURL.protocolIsInHTTPFamily()) {
#if ENABLE(DATA_DETECTION)
                if (!WebCore::DataDetection::canBePresentedByDataDetectors(linkURL))
                    return continueWithContextMenuConfiguration(nil);
#endif
            }
        }

        _contextMenuLegacyPreviewController = nullptr;
        _contextMenuLegacyMenu = nullptr;
        _contextMenuHasRequestedLegacyData = NO;
        _contextMenuActionProviderDelegateNeedsOverride = NO;
        _contextMenuIsUsingAlternateURLForImage = NO;

        UIContextMenuActionProvider actionMenuProvider = [weakSelf = WeakObjCPtr<WKContentView>(self)] (NSArray<UIMenuElement *> *) -> UIMenu * {
            auto strongSelf = weakSelf.get();
            if (!strongSelf)
                return nil;
            if (!strongSelf->_contextMenuHasRequestedLegacyData)
                [strongSelf assignLegacyDataForContextMenuInteraction];

            return strongSelf->_contextMenuLegacyMenu.get();
        };

        UIContextMenuContentPreviewProvider contentPreviewProvider = [weakSelf = WeakObjCPtr<WKContentView>(self)] () -> UIViewController * {
            auto strongSelf = weakSelf.get();
            if (!strongSelf)
                return nil;
            if (!strongSelf->_contextMenuHasRequestedLegacyData)
                [strongSelf assignLegacyDataForContextMenuInteraction];

            return strongSelf->_contextMenuLegacyPreviewController.get();
        };

        _page->startInteractionWithPositionInformation(_positionInformation);

        continueWithContextMenuConfiguration([UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:contentPreviewProvider actionProvider:actionMenuProvider]);
        return;
    }

#if ENABLE(DATA_DETECTION)
    if ([linkURL.createNSURL() iTunesStoreURL]) {
        [self continueContextMenuInteractionWithDataDetectors:continueWithContextMenuConfiguration];
        return;
    }
#endif

    auto completionBlock = makeBlockPtr([continueWithContextMenuConfiguration = makeBlockPtr(continueWithContextMenuConfiguration), linkURL = WTFMove(linkURL), weakSelf = WeakObjCPtr<WKContentView>(self)] (UIContextMenuConfiguration *configurationFromWKUIDelegate) mutable {
        auto strongSelf = weakSelf.get();
        if (!strongSelf) {
            continueWithContextMenuConfiguration(nil);
            return;
        }

        if (configurationFromWKUIDelegate) {
            strongSelf->_page->startInteractionWithPositionInformation(strongSelf->_positionInformation);
            strongSelf->_contextMenuActionProviderDelegateNeedsOverride = YES;
            continueWithContextMenuConfiguration(configurationFromWKUIDelegate);
            return;
        }

        bool canShowHTTPLinkOrDataDetectorPreview = ([&] {
            if (linkURL.protocolIsInHTTPFamily())
                return true;

            if (WebCore::DataDetection::canBePresentedByDataDetectors(linkURL))
                return true;

            if ([strongSelf positionInformationHasImageOverlayDataDetector])
                return true;

            return false;
        })();

        ASSERT_IMPLIES(strongSelf->_positionInformation.isImage, strongSelf->_positionInformation.image);
        if (strongSelf->_positionInformation.isImage && strongSelf->_positionInformation.image && !canShowHTTPLinkOrDataDetectorPreview) {
            auto cgImage = strongSelf->_positionInformation.image->makeCGImageCopy();

            strongSelf->_contextMenuActionProviderDelegateNeedsOverride = NO;

            auto elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithInteractionInformationAtPosition:strongSelf->_positionInformation isUsingAlternateURLForImage:strongSelf->_contextMenuIsUsingAlternateURLForImage userInfo:nil]);

            UIContextMenuActionProvider actionMenuProvider = [weakSelf, elementInfo] (NSArray<UIMenuElement *> *) -> UIMenu * {
                auto strongSelf = weakSelf.get();
                if (!strongSelf)
                    return nil;

                RetainPtr<NSArray<_WKElementAction *>> defaultActionsFromAssistant = [strongSelf->_actionSheetAssistant defaultActionsForImageSheet:elementInfo.get()];
                auto actions = menuElementsFromDefaultActions(defaultActionsFromAssistant, elementInfo);
#if ENABLE(IMAGE_ANALYSIS)
                if (auto *placeholder = [strongSelf placeholderForDynamicallyInsertedImageAnalysisActions])
                    [actions addObject:placeholder];
                else if (UIMenu *menu = [strongSelf machineReadableCodeSubMenuForImageContextMenu])
                    [actions addObject:menu];
#endif // ENABLE(IMAGE_ANALYSIS)
                return [UIMenu menuWithTitle:strongSelf->_positionInformation.title.createNSString().get() children:actions];
            };

            UIContextMenuContentPreviewProvider contentPreviewProvider = [weakSelf, cgImage, elementInfo] () -> UIViewController * {
                auto strongSelf = weakSelf.get();
                if (!strongSelf)
                    return nil;

                auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>([strongSelf webViewUIDelegate]);
                if ([uiDelegate respondsToSelector:@selector(_webView:contextMenuContentPreviewForElement:)]) {
                    if (UIViewController *previewViewController = [uiDelegate _webView:[strongSelf webView] contextMenuContentPreviewForElement:strongSelf->_contextMenuElementInfo.get()])
                        return previewViewController;
                }

                return adoptNS([[WKImagePreviewViewController alloc] initWithCGImage:cgImage defaultActions:nil elementInfo:elementInfo.get()]).autorelease();
            };

            continueWithContextMenuConfiguration([UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:contentPreviewProvider actionProvider:actionMenuProvider]);
            return;
        }

        // At this point we have an object we might want to show a context menu for, but the
        // client was unable to handle it. Before giving up, we ask DataDetectors.

        strongSelf->_contextMenuElementInfo = nil;

#if ENABLE(DATA_DETECTION)
        // FIXME: Support JavaScript urls here. But make sure they don't show a preview.
        // <rdar://problem/50572283>
        if (!canShowHTTPLinkOrDataDetectorPreview) {
            continueWithContextMenuConfiguration(nil);
            return;
        }

        [strongSelf continueContextMenuInteractionWithDataDetectors:continueWithContextMenuConfiguration.get()];
        return;
#else
        continueWithContextMenuConfiguration(nil);
#endif
    });

    _contextMenuActionProviderDelegateNeedsOverride = NO;
    _contextMenuIsUsingAlternateURLForImage = NO;
    _contextMenuElementInfo = wrapper(API::ContextMenuElementInfo::create(_positionInformation, nil));

    if (_positionInformation.isImage && _positionInformation.url.isNull() && [uiDelegate respondsToSelector:@selector(_webView:alternateURLFromImage:userInfo:)]) {
        UIImage *uiImage = [[_contextMenuElementInfo _activatedElementInfo] image];
        NSDictionary *userInfo = nil;
        NSURL *nsURL = [uiDelegate _webView:self.webView alternateURLFromImage:uiImage userInfo:&userInfo];
        _positionInformation.url = nsURL;
        if (nsURL)
            _contextMenuIsUsingAlternateURLForImage = YES;
        _contextMenuElementInfo = wrapper(API::ContextMenuElementInfo::create(_positionInformation, userInfo));
    }

    if (_positionInformation.isLink && [uiDelegate respondsToSelector:@selector(webView:contextMenuConfigurationForElement:completionHandler:)]) {
        auto checker = WebKit::CompletionHandlerCallChecker::create(uiDelegate, @selector(webView:contextMenuConfigurationForElement:completionHandler:));
        [uiDelegate webView:self.webView contextMenuConfigurationForElement:_contextMenuElementInfo.get() completionHandler:makeBlockPtr([completionBlock = WTFMove(completionBlock), checker = WTFMove(checker)] (UIContextMenuConfiguration *configuration) {
            if (checker->completionHandlerHasBeenCalled())
                return;
            checker->didCallCompletionHandler();
            completionBlock(configuration);
        }).get()];
    } else if ([uiDelegate respondsToSelector:@selector(_webView:contextMenuConfigurationForElement:completionHandler:)]) {
        auto checker = WebKit::CompletionHandlerCallChecker::create(uiDelegate, @selector(_webView:contextMenuConfigurationForElement:completionHandler:));
        [uiDelegate _webView:self.webView contextMenuConfigurationForElement:_contextMenuElementInfo.get() completionHandler:makeBlockPtr([completionBlock = WTFMove(completionBlock), checker = WTFMove(checker)] (UIContextMenuConfiguration *configuration) {
            if (checker->completionHandlerHasBeenCalled())
                return;
            checker->didCallCompletionHandler();
            completionBlock(configuration);
        }).get()];
    } else
        completionBlock(nil);
}

#if ENABLE(DATA_DETECTION)

- (void)continueContextMenuInteractionWithDataDetectors:(void(^)(UIContextMenuConfiguration *))continueWithContextMenuConfiguration
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    auto ddContextMenuActionClass = PAL::getDDContextMenuActionClass();
    auto context = retainPtr([self dataDetectionContextForPositionInformation:_positionInformation]);
    RetainPtr<UIContextMenuConfiguration> configurationFromDataDetectors;

    if (self.positionInformationHasImageOverlayDataDetector) {
        DDScannerResult *scannerResult = [_positionInformation.dataDetectorResults firstObject];
        configurationFromDataDetectors = [ddContextMenuActionClass contextMenuConfigurationWithResult:scannerResult.coreResult inView:self context:context.get() menuIdentifier:nil];
    } else {
        configurationFromDataDetectors = [ddContextMenuActionClass contextMenuConfigurationForURL:_positionInformation.url.createNSURL().get() identifier:_positionInformation.dataDetectorIdentifier.createNSString().get() selectedText:[self selectedText] results:_positionInformation.dataDetectorResults.get() inView:self context:context.get() menuIdentifier:nil];
        _page->startInteractionWithPositionInformation(_positionInformation);
    }

    _contextMenuActionProviderDelegateNeedsOverride = YES;
    continueWithContextMenuConfiguration(configurationFromDataDetectors.get());
    END_BLOCK_OBJC_EXCEPTIONS
}

#endif // ENABLE(DATA_DETECTION)

- (NSArray<UIMenuElement *> *)_contextMenuInteraction:(UIContextMenuInteraction *)interaction overrideSuggestedActionsForConfiguration:(UIContextMenuConfiguration *)configuration
{
    // If we're here we're in the legacy path, which ignores the suggested actions anyway.
    if (!_contextMenuActionProviderDelegateNeedsOverride)
        return nil;

    auto *suggestedActions = [_actionSheetAssistant suggestedActionsForContextMenuWithPositionInformation:_positionInformation];
    if (auto *placeholder = self.placeholderForDynamicallyInsertedImageAnalysisActions)
        [suggestedActions addObject:placeholder];
    return suggestedActions;
}

- (UITargetedPreview *)contextMenuInteraction:(UIContextMenuInteraction *)interaction configuration:(UIContextMenuConfiguration *)configuration highlightPreviewForItemWithIdentifier:(id<NSCopying>)identifier
{
    [self _startSuppressingSelectionAssistantForReason:WebKit::InteractionIsHappening];
    [self _cancelTouchEventGestureRecognizer];
    return [self _createTargetedContextMenuHintPreviewIfPossible];
}

- (void)contextMenuInteraction:(UIContextMenuInteraction *)interaction willDisplayMenuForConfiguration:(UIContextMenuConfiguration *)configuration animator:(id<UIContextMenuInteractionAnimating>)animator
{
    if (!_webView)
        return;

    _page->willBeginContextMenuInteraction();
    _isDisplayingContextMenuWithAnimation = YES;
    [animator addCompletion:[weakSelf = WeakObjCPtr<WKContentView>(self)] {
        if (auto strongSelf = weakSelf.get()) {
            ASSERT_IMPLIES(strongSelf->_isDisplayingContextMenuWithAnimation, [strongSelf->_contextMenuHintContainerView window]);
            strongSelf->_isDisplayingContextMenuWithAnimation = NO;
            [strongSelf->_webView _didShowContextMenu];
#if ENABLE(IMAGE_ANALYSIS)
            switch (strongSelf->_dynamicImageAnalysisContextMenuState) {
            case WebKit::DynamicImageAnalysisContextMenuState::NotWaiting:
            case WebKit::DynamicImageAnalysisContextMenuState::WaitingForImageAnalysis:
                break;
            case WebKit::DynamicImageAnalysisContextMenuState::WaitingForVisibleMenu:
                [strongSelf _insertDynamicImageAnalysisContextMenuItemsIfPossible];
                break;
            }
#endif // ENABLE(IMAGE_ANALYSIS)
        }
    }];

    auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate);
    if (!uiDelegate)
        return;

    if ([uiDelegate respondsToSelector:@selector(webView:contextMenuWillPresentForElement:)])
        [uiDelegate webView:self.webView contextMenuWillPresentForElement:_contextMenuElementInfo.get()];
    else if ([uiDelegate respondsToSelector:@selector(_webView:contextMenuWillPresentForElement:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        [uiDelegate _webView:self.webView contextMenuWillPresentForElement:_contextMenuElementInfo.get()];
ALLOW_DEPRECATED_DECLARATIONS_END
    }
}

- (UITargetedPreview *)contextMenuInteraction:(UIContextMenuInteraction *)interaction configuration:(UIContextMenuConfiguration *)configuration dismissalPreviewForItemWithIdentifier:(id<NSCopying>)identifier
{
    if (!_useContextMenuInteractionDismissalPreview) {
        _contextMenuInteractionTargetedPreview = nil;
        return nil;
    }

    return std::exchange(_contextMenuInteractionTargetedPreview, nil).autorelease();
}

- (void)contextMenuInteraction:(UIContextMenuInteraction *)interaction willPerformPreviewActionForMenuWithConfiguration:(UIContextMenuConfiguration *)configuration animator:(id<UIContextMenuInteractionCommitAnimating>)animator
{
    if (!_webView)
        return;

    _useContextMenuInteractionDismissalPreview = NO;

    auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate);
    if (!uiDelegate)
        return;

    if (needsDeprecatedPreviewAPI(uiDelegate)) {

        if (_positionInformation.isImage) {
            if ([uiDelegate respondsToSelector:@selector(_webView:commitPreviewedImageWithURL:)]) {
                const auto& imageURL = _positionInformation.imageURL;
                if (imageURL.isEmpty() || !(imageURL.protocolIsInHTTPFamily() || imageURL.protocolIs("data"_s)))
                    return;
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
                [uiDelegate _webView:self.webView commitPreviewedImageWithURL:imageURL.createNSURL().get()];
ALLOW_DEPRECATED_DECLARATIONS_END
            }
            return;
        }

        if ([uiDelegate respondsToSelector:@selector(webView:commitPreviewingViewController:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if (auto viewController = _contextMenuLegacyPreviewController.get())
                [uiDelegate webView:self.webView commitPreviewingViewController:viewController];
ALLOW_DEPRECATED_DECLARATIONS_END
            return;
        }

        if ([uiDelegate respondsToSelector:@selector(_webView:commitPreviewedViewController:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            if (auto viewController = _contextMenuLegacyPreviewController.get())
                [uiDelegate _webView:self.webView commitPreviewedViewController:viewController];
ALLOW_DEPRECATED_DECLARATIONS_END
            return;
        }

        return;
    }

    if ([uiDelegate respondsToSelector:@selector(webView:contextMenuForElement:willCommitWithAnimator:)])
        [uiDelegate webView:self.webView contextMenuForElement:_contextMenuElementInfo.get() willCommitWithAnimator:animator];
    else if ([uiDelegate respondsToSelector:@selector(_webView:contextMenuForElement:willCommitWithAnimator:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        [uiDelegate _webView:self.webView contextMenuForElement:_contextMenuElementInfo.get() willCommitWithAnimator:animator];
ALLOW_DEPRECATED_DECLARATIONS_END
    }

#if defined(DD_CONTEXT_MENU_SPI_VERSION) && DD_CONTEXT_MENU_SPI_VERSION >= 2
    if ([configuration isKindOfClass:PAL::getDDContextMenuConfigurationClass()]) {
        DDContextMenuConfiguration *ddConfiguration = static_cast<DDContextMenuConfiguration *>(configuration);

        BOOL shouldExpandPreview = NO;
        RetainPtr<UIViewController> presentedViewController;

#if defined(DD_CONTEXT_MENU_SPI_VERSION) && DD_CONTEXT_MENU_SPI_VERSION >= 3
        shouldExpandPreview = !!ddConfiguration.interactionViewControllerProvider;
        if (shouldExpandPreview)
            presentedViewController = ddConfiguration.interactionViewControllerProvider();
#else
        shouldExpandPreview = ddConfiguration.expandPreviewOnInteraction;
        presentedViewController = animator.previewViewController;
#endif

        if (shouldExpandPreview) {
            animator.preferredCommitStyle = UIContextMenuInteractionCommitStylePop;
            _useContextMenuInteractionDismissalPreview = YES;

            // We will re-present modally on the same VC that is currently presenting the preview in a context menu.
            RetainPtr<UIViewController> presentingViewController = animator.previewViewController.presentingViewController;

            [animator addAnimations:^{
                [presentingViewController presentViewController:presentedViewController.get() animated:NO completion:nil];
            }];
            return;
        }

        if (NSURL *interactionURL = ddConfiguration.interactionURL) {
            animator.preferredCommitStyle = UIContextMenuInteractionCommitStylePop;
            _useContextMenuInteractionDismissalPreview = YES;

            [animator addAnimations:^{
                [[UIApplication sharedApplication] openURL:interactionURL withCompletionHandler:nil];
            }];
            return;
        }
    }
#endif
}

- (void)contextMenuInteraction:(UIContextMenuInteraction *)interaction willEndForConfiguration:(UIContextMenuConfiguration *)configuration animator:(id<UIContextMenuInteractionAnimating>)animator
{
    if (!_webView)
        return;

    _page->didEndContextMenuInteraction();
    // FIXME: This delegate is being called more than once by UIKit. <rdar://problem/51550291>
    // This conditional avoids the WKUIDelegate being called twice too.
    if (_contextMenuElementInfo) {
        auto uiDelegate = static_cast<id<WKUIDelegatePrivate>>(self.webView.UIDelegate);
        if ([uiDelegate respondsToSelector:@selector(webView:contextMenuDidEndForElement:)])
            [uiDelegate webView:self.webView contextMenuDidEndForElement:_contextMenuElementInfo.get()];
        else if ([uiDelegate respondsToSelector:@selector(_webView:contextMenuDidEndForElement:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            [uiDelegate _webView:self.webView contextMenuDidEndForElement:_contextMenuElementInfo.get()];
ALLOW_DEPRECATED_DECLARATIONS_END
        }
    }

    _page->stopInteraction();

    _contextMenuLegacyPreviewController = nullptr;
    _contextMenuLegacyMenu = nullptr;
    _contextMenuHasRequestedLegacyData = NO;
    _contextMenuElementInfo = nullptr;

    auto contextFinalizer = [weakSelf = WeakObjCPtr<WKContentView>(self)] () {
        auto strongSelf = weakSelf.get();
        if (!strongSelf)
            return;

        strongSelf->_isDisplayingContextMenuWithAnimation = NO;
        [strongSelf _removeContextMenuHintContainerIfPossible];
        [strongSelf->_webView _didDismissContextMenu];

        [strongSelf _stopSuppressingSelectionAssistantForReason:WebKit::InteractionIsHappening];
    };

    if (animator)
        [animator addCompletion:contextFinalizer];
    else
        contextFinalizer();
}

#endif // USE(UICONTEXTMENU)

- (BOOL)_interactionShouldBeginFromPreviewItemController:(UIPreviewItemController *)controller forPosition:(CGPoint)position
{
    if (!_longPressCanClick)
        return NO;

    WebKit::InteractionInformationRequest request(WebCore::roundedIntPoint(position));
    request.includeSnapshot = true;
    request.includeLinkIndicator = true;
    request.linkIndicatorShouldHaveLegacyMargins = !self._shouldUseContextMenus;
    request.gatherAnimations = [self.webView _allowAnimationControls];
    if (![self ensurePositionInformationIsUpToDate:request])
        return NO;
    if (!_positionInformation.isLink && !_positionInformation.isImage && !_positionInformation.isAttachment)
        return NO;

    const URL& linkURL = _positionInformation.url;
    if (_positionInformation.isLink) {
        id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate respondsToSelector:@selector(webView:shouldPreviewElement:)]) {
            auto previewElementInfo = adoptNS([[WKPreviewElementInfo alloc] _initWithLinkURL:linkURL.createNSURL().get()]);
            return [uiDelegate webView:self.webView shouldPreviewElement:previewElementInfo.get()];
        }
ALLOW_DEPRECATED_DECLARATIONS_END
        if (linkURL.isEmpty())
            return NO;
        if (linkURL.protocolIsInHTTPFamily())
            return YES;
#if ENABLE(DATA_DETECTION)
        if (WebCore::DataDetection::canBePresentedByDataDetectors(linkURL))
            return YES;
#endif
        return NO;
    }
    return YES;
}

- (NSDictionary *)_dataForPreviewItemController:(UIPreviewItemController *)controller atPosition:(CGPoint)position type:(UIPreviewItemType *)type
{
    *type = UIPreviewItemTypeNone;

    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    BOOL supportsImagePreview = [uiDelegate respondsToSelector:@selector(_webView:commitPreviewedImageWithURL:)];
    BOOL canShowImagePreview = _positionInformation.isImage && supportsImagePreview;
    BOOL canShowLinkPreview = _positionInformation.isLink || canShowImagePreview;
    BOOL useImageURLForLink = NO;
    BOOL respondsToAttachmentListForWebViewSourceIsManaged = [uiDelegate respondsToSelector:@selector(_attachmentListForWebView:sourceIsManaged:)];
    BOOL supportsAttachmentPreview = ([uiDelegate respondsToSelector:@selector(_attachmentListForWebView:)] || respondsToAttachmentListForWebViewSourceIsManaged)
        && [uiDelegate respondsToSelector:@selector(_webView:indexIntoAttachmentListForElement:)];
    BOOL canShowAttachmentPreview = (_positionInformation.isAttachment || _positionInformation.isImage) && supportsAttachmentPreview;
    BOOL isDataDetectorLink = NO;
#if ENABLE(DATA_DETECTION)
    isDataDetectorLink = _positionInformation.isDataDetectorLink;
#endif

    if (canShowImagePreview && _positionInformation.isAnimatedImage) {
        canShowImagePreview = NO;
        canShowLinkPreview = YES;
        useImageURLForLink = YES;
    }

    if (!canShowLinkPreview && !canShowImagePreview && !canShowAttachmentPreview)
        return nil;

    const URL& linkURL = _positionInformation.url;
    if (!useImageURLForLink && (linkURL.isEmpty() || (!linkURL.protocolIsInHTTPFamily() && !isDataDetectorLink))) {
        if (canShowLinkPreview && !canShowImagePreview)
            return nil;
        canShowLinkPreview = NO;
    }

    auto dataForPreview = adoptNS([[NSMutableDictionary alloc] init]);
    if (canShowLinkPreview) {
        *type = UIPreviewItemTypeLink;
        if (useImageURLForLink)
            dataForPreview.get()[UIPreviewDataLink] = _positionInformation.imageURL.createNSURL().get();
        else
            dataForPreview.get()[UIPreviewDataLink] = linkURL.createNSURL().get();
#if ENABLE(DATA_DETECTION)
        if (isDataDetectorLink) {
            NSDictionary *context = nil;
            if ([uiDelegate respondsToSelector:@selector(_dataDetectionContextForWebView:)])
                context = [uiDelegate _dataDetectionContextForWebView:self.webView];

            DDDetectionController *controller = [PAL::getDDDetectionControllerClass() sharedController];
            NSDictionary *newContext = nil;
            RetainPtr<NSMutableDictionary> extendedContext;
            DDResultRef ddResult = [controller resultForURL:dataForPreview.get()[UIPreviewDataLink] identifier:_positionInformation.dataDetectorIdentifier.createNSString().get() selectedText:[self selectedText] results:_positionInformation.dataDetectorResults.get() context:context extendedContext:&newContext];
            if (ddResult)
                dataForPreview.get()[UIPreviewDataDDResult] = (__bridge id)ddResult;
            if (!_positionInformation.textBefore.isEmpty() || !_positionInformation.textAfter.isEmpty()) {
                extendedContext = adoptNS([@{
                    PAL::get_DataDetectorsUI_kDataDetectorsLeadingText() : _positionInformation.textBefore.createNSString().get(),
                    PAL::get_DataDetectorsUI_kDataDetectorsTrailingText() : _positionInformation.textAfter.createNSString().get(),
                } mutableCopy]);
                
                if (newContext)
                    [extendedContext addEntriesFromDictionary:newContext];
                newContext = extendedContext.get();
            }
            if (newContext)
                dataForPreview.get()[UIPreviewDataDDContext] = newContext;
        }
#endif // ENABLE(DATA_DETECTION)
    } else if (canShowImagePreview) {
        *type = UIPreviewItemTypeImage;
        dataForPreview.get()[UIPreviewDataLink] = _positionInformation.imageURL.createNSURL().get();
    } else if (canShowAttachmentPreview) {
        *type = UIPreviewItemTypeAttachment;
        auto element = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeAttachment URL:linkURL.createNSURL().get() image:nil information:_positionInformation]);
        NSUInteger index = [uiDelegate _webView:self.webView indexIntoAttachmentListForElement:element.get()];
        if (index != NSNotFound) {
            BOOL sourceIsManaged = NO;
            if (respondsToAttachmentListForWebViewSourceIsManaged)
                dataForPreview.get()[UIPreviewDataAttachmentList] = [uiDelegate _attachmentListForWebView:self.webView sourceIsManaged:&sourceIsManaged];
            else
                dataForPreview.get()[UIPreviewDataAttachmentList] = [uiDelegate _attachmentListForWebView:self.webView];
            dataForPreview.get()[UIPreviewDataAttachmentIndex] = [NSNumber numberWithUnsignedInteger:index];
            dataForPreview.get()[UIPreviewDataAttachmentListIsContentManaged] = [NSNumber numberWithBool:sourceIsManaged];
        }
    }

    return dataForPreview.autorelease();
}

- (CGRect)_presentationRectForPreviewItemController:(UIPreviewItemController *)controller
{
    return _positionInformation.bounds;
}

- (UIViewController *)_presentedViewControllerForPreviewItemController:(UIPreviewItemController *)controller
{
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    
    [_webView _didShowContextMenu];

    NSURL *targetURL = controller.previewData[UIPreviewDataLink];
    URL coreTargetURL = targetURL;
    bool isValidURLForImagePreview = !coreTargetURL.isEmpty() && (coreTargetURL.protocolIsInHTTPFamily() || coreTargetURL.protocolIs("data"_s));

    if ([_previewItemController type] == UIPreviewItemTypeLink) {
        _longPressCanClick = NO;
        _page->startInteractionWithPositionInformation(_positionInformation);

        // Treat animated images like a link preview
        if (isValidURLForImagePreview && _positionInformation.isAnimatedImage) {
            auto animatedImageElementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeImage URL:targetURL imageURL:nil information:_positionInformation]);

            if ([uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForAnimatedImageAtURL:defaultActions:elementInfo:imageSize:)]) {
                RetainPtr<NSArray> actions = [_actionSheetAssistant defaultActionsForImageSheet:animatedImageElementInfo.get()];
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
                return [uiDelegate _webView:self.webView previewViewControllerForAnimatedImageAtURL:targetURL defaultActions:actions.get() elementInfo:animatedImageElementInfo.get() imageSize:_positionInformation.image->size()];
ALLOW_DEPRECATED_DECLARATIONS_END
            }
        }

        auto elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeLink URL:targetURL imageURL:nil information:_positionInformation]);

        auto actions = [_actionSheetAssistant defaultActionsForLinkSheet:elementInfo.get()];
        if ([uiDelegate respondsToSelector:@selector(webView:previewingViewControllerForElement:defaultActions:)]) {
            auto previewActions = adoptNS([[NSMutableArray alloc] init]);
            for (_WKElementAction *elementAction in actions.get()) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
                WKPreviewAction *previewAction = [WKPreviewAction actionWithIdentifier:previewIdentifierForElementAction(elementAction) title:[elementAction title] style:UIPreviewActionStyleDefault handler:^(UIPreviewAction *, UIViewController *) {
                    [elementAction runActionWithElementInfo:elementInfo.get()];
                }];
ALLOW_DEPRECATED_DECLARATIONS_END
                [previewActions addObject:previewAction];
            }
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            auto previewElementInfo = adoptNS([[WKPreviewElementInfo alloc] _initWithLinkURL:targetURL]);
            if (UIViewController *controller = [uiDelegate webView:self.webView previewingViewControllerForElement:previewElementInfo.get() defaultActions:previewActions.get()])
                return controller;
ALLOW_DEPRECATED_DECLARATIONS_END
        }

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForURL:defaultActions:elementInfo:)])
            return [uiDelegate _webView:self.webView previewViewControllerForURL:targetURL defaultActions:actions.get() elementInfo:elementInfo.get()];

        if ([uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForURL:)])
            return [uiDelegate _webView:self.webView previewViewControllerForURL:targetURL];
ALLOW_DEPRECATED_DECLARATIONS_END

        return nil;
    }

    if ([_previewItemController type] == UIPreviewItemTypeImage) {
        if (!isValidURLForImagePreview)
            return nil;

        RetainPtr<NSURL> alternateURL = targetURL;
        RetainPtr<NSDictionary> imageInfo;
        RetainPtr<CGImageRef> cgImage = _positionInformation.image->makeCGImageCopy();
        RetainPtr<UIImage> uiImage = adoptNS([[UIImage alloc] initWithCGImage:cgImage.get()]);
        if ([uiDelegate respondsToSelector:@selector(_webView:alternateURLFromImage:userInfo:)]) {
            NSDictionary *userInfo;
            alternateURL = [uiDelegate _webView:self.webView alternateURLFromImage:uiImage.get() userInfo:&userInfo];
            imageInfo = userInfo;
        }

        auto elementInfo = adoptNS([[_WKActivatedElementInfo alloc] _initWithType:_WKActivatedElementTypeImage URL:alternateURL.get() imageURL:nil userInfo:imageInfo.get() information:_positionInformation]);
        _page->startInteractionWithPositionInformation(_positionInformation);

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        if ([uiDelegate respondsToSelector:@selector(_webView:willPreviewImageWithURL:)])
            [uiDelegate _webView:self.webView willPreviewImageWithURL:targetURL];
ALLOW_DEPRECATED_DECLARATIONS_END

        auto defaultActions = [_actionSheetAssistant defaultActionsForImageSheet:elementInfo.get()];
        if (imageInfo && [uiDelegate respondsToSelector:@selector(_webView:previewViewControllerForImage:alternateURL:defaultActions:elementInfo:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            UIViewController *previewViewController = [uiDelegate _webView:self.webView previewViewControllerForImage:uiImage.get() alternateURL:alternateURL.get() defaultActions:defaultActions.get() elementInfo:elementInfo.get()];
ALLOW_DEPRECATED_DECLARATIONS_END
            if (previewViewController)
                return previewViewController;
        }

        return adoptNS([[WKImagePreviewViewController alloc] initWithCGImage:cgImage defaultActions:defaultActions elementInfo:elementInfo]).autorelease();
    }

    return nil;
}

- (void)_previewItemController:(UIPreviewItemController *)controller commitPreview:(UIViewController *)viewController
{
    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
    if ([_previewItemController type] == UIPreviewItemTypeImage) {
        if ([uiDelegate respondsToSelector:@selector(_webView:commitPreviewedImageWithURL:)]) {
            const URL& imageURL = _positionInformation.imageURL;
            if (imageURL.isEmpty() || !(imageURL.protocolIsInHTTPFamily() || imageURL.protocolIs("data"_s)))
                return;
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
            [uiDelegate _webView:self.webView commitPreviewedImageWithURL:imageURL.createNSURL().get()];
ALLOW_DEPRECATED_DECLARATIONS_END
            return;
        }
        return;
    }

    if ([uiDelegate respondsToSelector:@selector(webView:commitPreviewingViewController:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        [uiDelegate webView:self.webView commitPreviewingViewController:viewController];
ALLOW_DEPRECATED_DECLARATIONS_END
        return;
    }

    if ([uiDelegate respondsToSelector:@selector(_webView:commitPreviewedViewController:)]) {
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
        [uiDelegate _webView:self.webView commitPreviewedViewController:viewController];
ALLOW_DEPRECATED_DECLARATIONS_END
        return;
    }

}

- (void)_interactionStartedFromPreviewItemController:(UIPreviewItemController *)controller
{
    [self _removeDefaultGestureRecognizers];

    [self _cancelInteraction];
}

- (void)_interactionStoppedFromPreviewItemController:(UIPreviewItemController *)controller
{
    [self _addDefaultGestureRecognizers];

    if (![_actionSheetAssistant isShowingSheet])
        _page->stopInteraction();
}

- (void)_previewItemController:(UIPreviewItemController *)controller didDismissPreview:(UIViewController *)viewController committing:(BOOL)committing
{
    id<WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>([_webView UIDelegate]);
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    if ([uiDelegate respondsToSelector:@selector(_webView:didDismissPreviewViewController:committing:)])
        [uiDelegate _webView:self.webView didDismissPreviewViewController:viewController committing:committing];
    else if ([uiDelegate respondsToSelector:@selector(_webView:didDismissPreviewViewController:)])
        [uiDelegate _webView:self.webView didDismissPreviewViewController:viewController];
ALLOW_DEPRECATED_DECLARATIONS_END

    [_webView _didDismissContextMenu];
}

- (UIImage *)_presentationSnapshotForPreviewItemController:(UIPreviewItemController *)controller
{
    if (!_positionInformation.textIndicator->contentImage())
        return nullptr;

    auto nativeImage = _positionInformation.textIndicator->contentImage()->nativeImage();
    if (!nativeImage)
        return nullptr;

    return adoptNS([[UIImage alloc] initWithCGImage:nativeImage->platformImage().get()]).autorelease();
}

- (NSArray *)_presentationRectsForPreviewItemController:(UIPreviewItemController *)controller
{
    if (_positionInformation.textIndicator->contentImage()) {
        auto origin = _positionInformation.textIndicator->textBoundingRectInRootViewCoordinates().location();
        return createNSArray(_positionInformation.textIndicator->textRectsInBoundingRectCoordinates(), [&] (CGRect rect) {
            return [NSValue valueWithCGRect:CGRectOffset(rect, origin.x(), origin.y())];
        }).autorelease();
    } else {
        float marginInPx = 4 * _page->deviceScaleFactor();
        return @[[NSValue valueWithCGRect:CGRectInset(_positionInformation.bounds, -marginInPx, -marginInPx)]];
    }
}

- (void)_previewItemControllerDidCancelPreview:(UIPreviewItemController *)controller
{
    _longPressCanClick = NO;
    
    [_webView _didDismissContextMenu];
}

@end

#endif // HAVE(LINK_PREVIEW)

// UITextRange and UITextPosition implementations for WK2
// FIXME: Move these out into separate files.

@implementation WKTextRange (UITextInputAdditions)

- (BOOL)_isCaret
{
    return self.empty;
}

- (BOOL)_isRanged
{
    return !self.empty;
}

@end

@implementation WKTextRange

+ (WKTextRange *)textRangeWithState:(BOOL)isNone isRange:(BOOL)isRange isEditable:(BOOL)isEditable startRect:(CGRect)startRect endRect:(CGRect)endRect selectionRects:(NSArray *)selectionRects selectedTextLength:(NSUInteger)selectedTextLength
{
    auto range = adoptNS([[WKTextRange alloc] init]);
    [range setIsNone:isNone];
    [range setIsRange:isRange];
    [range setIsEditable:isEditable];
    [range setStartRect:startRect];
    [range setEndRect:endRect];
    [range setSelectedTextLength:selectedTextLength];
    [range setSelectionRects:selectionRects];
    return range.autorelease();
}

- (void)dealloc
{
    [_selectionRects release];
    [super dealloc];
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"%@(%p) - start:%@, end:%@", [self class], self, NSStringFromCGRect(self.startRect), NSStringFromCGRect(self.endRect)];
}

- (WKTextPosition *)start
{
    WKTextPosition *position = [WKTextPosition textPositionWithRect:self.startRect];
    OptionSet anchors { WebKit::TextPositionAnchor::Start };
    if (self.isEmpty)
        anchors.add(WebKit::TextPositionAnchor::End);
    position.anchors = anchors;
    return position;
}

- (WKTextPosition *)end
{
    WKTextPosition *position = [WKTextPosition textPositionWithRect:self.endRect];
    OptionSet anchors { WebKit::TextPositionAnchor::End };
    if (self.isEmpty)
        anchors.add(WebKit::TextPositionAnchor::Start);
    position.anchors = anchors;
    return position;
}

- (BOOL)isEmpty
{
    return !self.isRange;
}

// FIXME: Overriding isEqual: without overriding hash will cause trouble if this ever goes into an NSSet or is the key in an NSDictionary,
// since two equal items could have different hashes.
- (BOOL)isEqual:(id)other
{
    if (![other isKindOfClass:[WKTextRange class]])
        return NO;

    WKTextRange *otherRange = (WKTextRange *)other;

    if (self == other)
        return YES;

    // FIXME: Probably incorrect for equality to ignore so much of the object state.
    // It ignores isNone, isEditable, selectedTextLength, and selectionRects.

    if (self.isRange) {
        if (!otherRange.isRange)
            return NO;
        return CGRectEqualToRect(self.startRect, otherRange.startRect) && CGRectEqualToRect(self.endRect, otherRange.endRect);
    } else {
        if (otherRange.isRange)
            return NO;
        // FIXME: Do we need to check isNone here?
        return CGRectEqualToRect(self.startRect, otherRange.startRect);
    }
}

@end

@implementation WKTextPosition

@synthesize positionRect = _positionRect;

+ (WKTextPosition *)textPositionWithRect:(CGRect)positionRect
{
    auto pos = adoptNS([[WKTextPosition alloc] init]);
    [pos setPositionRect:positionRect];
    return pos.autorelease();
}

// FIXME: Overriding isEqual: without overriding hash will cause trouble if this ever goes into a NSSet or is the key in an NSDictionary,
// since two equal items could have different hashes.
- (BOOL)isEqual:(id)other
{
    if (![other isKindOfClass:[WKTextPosition class]])
        return NO;

    return CGRectEqualToRect(self.positionRect, ((WKTextPosition *)other).positionRect);
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<WKTextPosition: %p, {%@}>", self, NSStringFromCGRect(self.positionRect)];
}

@end

#if HAVE(UIFINDINTERACTION)

@implementation WKFoundTextRange

+ (WKFoundTextRange *)foundTextRangeWithWebFoundTextRange:(WebKit::WebFoundTextRange)webRange
{
    RetainPtr range = WTF::switchOn(webRange.data,
        [] (const WebKit::WebFoundTextRange::DOMData& domData) -> RetainPtr<WKFoundTextRange> {
            RetainPtr foundDOMTextRange = adoptNS([[WKFoundDOMTextRange alloc] init]);
            [foundDOMTextRange setLocation:domData.location];
            [foundDOMTextRange setLength:domData.length];
            return foundDOMTextRange;
        },
        [] (const WebKit::WebFoundTextRange::PDFData& pdfData) -> RetainPtr<WKFoundTextRange> {
            RetainPtr foundPDFTextRange = adoptNS([[WKFoundPDFTextRange alloc] init]);
            [foundPDFTextRange setStartPage:pdfData.startPage];
            [foundPDFTextRange setStartPageOffset:pdfData.startOffset];
            [foundPDFTextRange setEndPage:pdfData.endPage];
            [foundPDFTextRange setEndPageOffset:pdfData.endOffset];
            return foundPDFTextRange;
        }
    );

    [range setFrameIdentifier:webRange.frameIdentifier.createNSString().get()];
    [range setOrder:webRange.order];
    return range.autorelease();
}

- (void)dealloc
{
    [_frameIdentifier release];
    [super dealloc];
}

- (BOOL)isEmpty
{
    return NO;
}

- (WebKit::WebFoundTextRange)webFoundTextRange
{
    return { };
}

@end

@implementation WKFoundTextPosition
@end

@implementation WKFoundDOMTextRange

- (WKFoundDOMTextPosition *)start
{
    WKFoundDOMTextPosition *position = [WKFoundDOMTextPosition textPositionWithOffset:self.location order:self.order];
    return position;
}

- (WKFoundDOMTextPosition *)end
{
    WKFoundDOMTextPosition *position = [WKFoundDOMTextPosition textPositionWithOffset:(self.location + self.length) order:self.order];
    return position;
}

- (WebKit::WebFoundTextRange)webFoundTextRange
{
    WebKit::WebFoundTextRange::DOMData data { self.location, self.length };
    return { data, self.frameIdentifier, self.order };
}

@end

@implementation WKFoundDOMTextPosition

+ (WKFoundDOMTextPosition *)textPositionWithOffset:(NSUInteger)offset order:(NSUInteger)order
{
    RetainPtr pos = adoptNS([[WKFoundDOMTextPosition alloc] init]);
    [pos setOffset:offset];
    [pos setOrder:order];
    return pos.autorelease();
}

@end

@implementation WKFoundPDFTextRange

- (WKFoundPDFTextPosition *)start
{
    WKFoundPDFTextPosition *position = [WKFoundPDFTextPosition textPositionWithPage:self.startPage offset:self.startPageOffset];
    return position;
}

- (WKFoundPDFTextPosition *)end
{
    WKFoundPDFTextPosition *position = [WKFoundPDFTextPosition textPositionWithPage:self.endPage offset:self.endPageOffset];
    return position;
}

- (WebKit::WebFoundTextRange)webFoundTextRange
{
    WebKit::WebFoundTextRange::PDFData data { self.startPage, self.startPageOffset, self.endPage, self.endPageOffset };
    return { data, self.frameIdentifier, self.order };
}

@end

@implementation WKFoundPDFTextPosition

+ (WKFoundPDFTextPosition *)textPositionWithPage:(NSUInteger)page offset:(NSUInteger)offset
{
    RetainPtr pos = adoptNS([[WKFoundPDFTextPosition alloc] init]);
    [pos setPage:page];
    [pos setOffset:offset];
    return pos.autorelease();
}

@end

#endif

@implementation WKAutocorrectionRects

+ (WKAutocorrectionRects *)autocorrectionRectsWithFirstCGRect:(CGRect)firstRect lastCGRect:(CGRect)lastRect
{
    auto rects = adoptNS([[WKAutocorrectionRects alloc] init]);
    [rects setFirstRect:firstRect];
    [rects setLastRect:lastRect];
    return rects.autorelease();
}

@end

@implementation WKAutocorrectionContext

+ (WKAutocorrectionContext *)emptyAutocorrectionContext
{
    return [self autocorrectionContextWithWebContext:WebKit::WebAutocorrectionContext { }];
}

+ (WKAutocorrectionContext *)autocorrectionContextWithWebContext:(const WebKit::WebAutocorrectionContext&)webCorrection
{
    auto correction = adoptNS([[WKAutocorrectionContext alloc] init]);
    [correction setContextBeforeSelection:nsStringNilIfEmpty(webCorrection.contextBefore)];
    [correction setSelectedText:nsStringNilIfEmpty(webCorrection.selectedText)];
    [correction setMarkedText:nsStringNilIfEmpty(webCorrection.markedText)];
    [correction setContextAfterSelection:nsStringNilIfEmpty(webCorrection.contextAfter)];
    [correction setRangeInMarkedText:webCorrection.selectedRangeInMarkedText];
    return correction.autorelease();
}

@end

#endif // PLATFORM(IOS_FAMILY)
