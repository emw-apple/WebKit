/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
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

#if PLATFORM(MAC)

#include "CorrectionPanel.h"
#include "PageClientImplCocoa.h"
#include "WebFullScreenManagerProxy.h"
#include <WebCore/DOMPasteAccess.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/WeakObjCPtr.h>

@class WKEditorUndoTarget;
@class WKView;

namespace WebCore {
class AlternativeTextUIController;
struct DragItem;
struct PromisedAttachmentInfo;
}

namespace WebKit {

enum class ColorControlSupportsAlpha : bool;

class RemoteLayerTreeNode;
class WebViewImpl;

class PageClientImpl final : public PageClientImplCocoa
#if ENABLE(FULLSCREEN_API)
    , public WebFullScreenManagerProxyClient
#endif
    {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(PageClientImpl);
#if ENABLE(FULLSCREEN_API)
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(PageClientImpl);
#endif
public:
    PageClientImpl(NSView *, WKWebView *);
    virtual ~PageClientImpl();

    // FIXME: Eventually WebViewImpl should become the PageClient.
    void setImpl(WebViewImpl&);

    void viewWillMoveToAnotherWindow();

private:
    // PageClient
    Ref<DrawingAreaProxy> createDrawingAreaProxy(WebProcessProxy&) override;
    void setViewNeedsDisplay(const WebCore::Region&) override;
    void requestScroll(const WebCore::FloatPoint& scrollPosition, const WebCore::IntPoint& scrollOrigin, WebCore::ScrollIsAnimated) override;
    WebCore::FloatPoint viewScrollPosition() override;

    WebCore::IntSize viewSize() override;
    bool isViewWindowActive() override;
    bool isViewFocused() override;
    bool isActiveViewVisible() override;
    bool isMainViewVisible() override;
    bool canTakeForegroundAssertions() override { return true; };
    bool isViewVisibleOrOccluded() override;
    bool isViewInWindow() override;
    bool isVisuallyIdle() override;
    WebCore::DestinationColorSpace colorSpace() override;
    void setRemoteLayerTreeRootNode(RemoteLayerTreeNode*) override;
    CALayer *acceleratedCompositingRootLayer() const override;
    CALayer *headerBannerLayer() const override;
    CALayer *footerBannerLayer() const override;

    void processDidExit() override;
    void processWillSwap() override;
    void pageClosed() override;
    void didRelaunchProcess() override;
    void preferencesDidChange() override;
    void toolTipChanged(const String& oldToolTip, const String& newToolTip) override;
    void didCommitLoadForMainFrame(const String& mimeType, bool useCustomContentProvider) override;
    void didFinishLoadingDataForCustomContentProvider(const String& suggestedFilename, std::span<const uint8_t>) override;
    void didChangeContentSize(const WebCore::IntSize&) override;
    void setCursor(const WebCore::Cursor&) override;
    void setCursorHiddenUntilMouseMoves(bool) override;

    void registerEditCommand(Ref<WebEditCommandProxy>&&, UndoOrRedo) override;
    void clearAllEditCommands() override;
    bool canUndoRedo(UndoOrRedo) override;
    void executeUndoRedo(UndoOrRedo) override;
    bool executeSavedCommandBySelector(const String& selector) override;
    void startDrag(const WebCore::DragItem&, WebCore::ShareableBitmap::Handle&& image, const std::optional<WebCore::NodeIdentifier>&) override;
    void setPromisedDataForImage(const String& pasteboardName, Ref<WebCore::FragmentedSharedBuffer>&& imageBuffer, const String& filename, const String& extension, const String& title,
        const String& url, const String& visibleURL, RefPtr<WebCore::FragmentedSharedBuffer>&& archiveBuffer, const String& originIdentifier) override;
    void updateSecureInputState() override;
    void resetSecureInputState() override;
    void notifyInputContextAboutDiscardedComposition() override;
    void selectionDidChange() override;
    void showBrowsingWarning(const BrowsingWarning&, CompletionHandler<void(Variant<WebKit::ContinueUnsafeLoad, URL>&&)>&&) override;
    void clearBrowsingWarning() override;
    void clearBrowsingWarningIfForMainFrameNavigation() override;
    bool hasBrowsingWarning() const override;

    void didChangeLocalInspectorAttachment() final;

    bool showShareSheet(WebCore::ShareDataWithParsedURL&&, WTF::CompletionHandler<void(bool)>&&) override;

#if HAVE(DIGITAL_CREDENTIALS_UI)
    void showDigitalCredentialsPicker(const WebCore::DigitalCredentialsRequestData&, WTF::CompletionHandler<void(Expected<WebCore::DigitalCredentialsResponseData, WebCore::ExceptionData>&&)>&&) override;
    void dismissDigitalCredentialsPicker(WTF::CompletionHandler<void(bool)>&&) override;
#endif

    WebCore::FloatRect convertToDeviceSpace(const WebCore::FloatRect&) override;
    WebCore::FloatRect convertToUserSpace(const WebCore::FloatRect&) override;
    WebCore::IntPoint screenToRootView(const WebCore::IntPoint&) override;
    WebCore::IntPoint rootViewToScreen(const WebCore::IntPoint&) override;
    WebCore::IntRect rootViewToScreen(const WebCore::IntRect&) override;
#if PLATFORM(MAC)
    WebCore::IntRect rootViewToWindow(const WebCore::IntRect&) override;
#endif
    WebCore::IntPoint accessibilityScreenToRootView(const WebCore::IntPoint&) override;
    WebCore::IntRect rootViewToAccessibilityScreen(const WebCore::IntRect&) override;

    void pinnedStateWillChange() final;
    void pinnedStateDidChange() final;
        
    void drawPageBorderForPrinting(WebCore::FloatSize&&) final;

    CGRect boundsOfLayerInLayerBackedWindowCoordinates(CALayer *) const override;

    void doneWithKeyEvent(const NativeWebKeyboardEvent&, bool wasEventHandled) override;

#if ENABLE(IMAGE_ANALYSIS)
    void requestTextRecognition(const URL& imageURL, WebCore::ShareableBitmap::Handle&& imageData, const String& sourceLanguageIdentifier, const String& targetLanguageIdentifier, CompletionHandler<void(WebCore::TextRecognitionResult&&)>&&) override;
    void computeHasVisualSearchResults(const URL&, WebCore::ShareableBitmap&, CompletionHandler<void(bool)>&&) override;
#endif

    RefPtr<WebPopupMenuProxy> createPopupMenuProxy(WebPageProxy&) override;
#if ENABLE(CONTEXT_MENUS)
    Ref<WebContextMenuProxy> createContextMenuProxy(WebPageProxy&, FrameInfoData&&, ContextMenuContextData&&, const UserData&) override;
    void didShowContextMenu() override;
    void didDismissContextMenu() override;
#endif

    RefPtr<WebColorPicker> createColorPicker(WebPageProxy&, const WebCore::Color& initialColor, const WebCore::IntRect&, ColorControlSupportsAlpha, Vector<WebCore::Color>&&) override;

    RefPtr<WebDataListSuggestionsDropdown> createDataListSuggestionsDropdown(WebPageProxy&) override;

    RefPtr<WebDateTimePicker> createDateTimePicker(WebPageProxy&) override;

    Ref<WebCore::ValidationBubble> createValidationBubble(String&& message, const WebCore::ValidationBubble::Settings&) final;

    CALayer *textIndicatorInstallationLayer() override;

    void enterAcceleratedCompositingMode(const LayerTreeContext&) override;
    void exitAcceleratedCompositingMode() override;
    void updateAcceleratedCompositingMode(const LayerTreeContext&) override;
    void didFirstLayerFlush(const LayerTreeContext&) override;

    RefPtr<ViewSnapshot> takeViewSnapshot(std::optional<WebCore::IntRect>&&) override;
    RefPtr<ViewSnapshot> takeViewSnapshot(std::optional<WebCore::IntRect>&&, ForceSoftwareCapturingViewportSnapshot) override;
    void wheelEventWasNotHandledByWebCore(const NativeWebWheelEvent&) override;
#if ENABLE(MAC_GESTURE_EVENTS)
    void gestureEventWasNotHandledByWebCore(const NativeWebGestureEvent&) override;
#endif

    void accessibilityWebProcessTokenReceived(std::span<const uint8_t>, pid_t) override;

    void makeFirstResponder() override;
    void assistiveTechnologyMakeFirstResponder() override;
    void setShouldSuppressFirstResponderChanges(bool shouldSuppress) override { m_shouldSuppressFirstResponderChanges = shouldSuppress; }

    void didPerformDictionaryLookup(const WebCore::DictionaryPopupInfo&) override;

    void showCorrectionPanel(WebCore::AlternativeTextType, const WebCore::FloatRect& boundingBoxOfReplacedString, const String& replacedString, const String& replacementString, const Vector<String>& alternativeReplacementStrings) override;
    void dismissCorrectionPanel(WebCore::ReasonForDismissingAlternativeText) override;
    String dismissCorrectionPanelSoon(WebCore::ReasonForDismissingAlternativeText) override;
    void recordAutocorrectionResponse(WebCore::AutocorrectionResponse, const String& replacedString, const String& replacementString) override;

    void recommendedScrollbarStyleDidChange(WebCore::ScrollbarStyle) override;

    void intrinsicContentSizeDidChange(const WebCore::IntSize& intrinsicContentSize) override;

    void showDictationAlternativeUI(const WebCore::FloatRect& boundingBoxOfDictatedText, WebCore::DictationContext) final;

    void setEditableElementIsFocused(bool) override;

    void scrollingNodeScrollViewDidScroll(WebCore::ScrollingNodeID) override;

    void registerInsertionUndoGrouping() override;

    void createPDFHUD(PDFPluginIdentifier, WebCore::FrameIdentifier, const WebCore::IntRect&) override;
    void updatePDFHUDLocation(PDFPluginIdentifier, const WebCore::IntRect&) override;
    void removePDFHUD(PDFPluginIdentifier) override;
    void removeAllPDFHUDs() override;

#if ENABLE(FULLSCREEN_API)
    WebFullScreenManagerProxyClient& fullScreenManagerProxyClient() final;
#endif

#if ENABLE(FULLSCREEN_API)
    // WebFullScreenManagerProxyClient
    void closeFullScreenManager() override;
    bool isFullScreen() override;
    void enterFullScreen(WebCore::FloatSize, CompletionHandler<void(bool)>&&) override;
    void exitFullScreen(CompletionHandler<void()>&&) override;
    void beganEnterFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void(bool)>&&) override;
    void beganExitFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void()>&&) override;
#endif

    void navigationGestureDidBegin() override;
    void navigationGestureWillEnd(bool willNavigate, WebBackForwardListItem&) override;
    void navigationGestureDidEnd(bool willNavigate, WebBackForwardListItem&) override;
    void navigationGestureDidEnd() override;
    void willRecordNavigationSnapshot(WebBackForwardListItem&) override;
    void didRemoveNavigationGestureSnapshot() override;

    void willBeginViewGesture() final;
    void didEndViewGesture() final;

    void requestDOMPasteAccess(WebCore::DOMPasteAccessCategory, WebCore::DOMPasteRequiresInteraction, const WebCore::IntRect&, const String&, CompletionHandler<void(WebCore::DOMPasteAccessResponse)>&&) final;

    void makeViewBlank(bool) final;

    NSView *activeView() const;
    NSWindow *activeWindow() const;
    NSView *viewForPresentingRevealPopover() const override { return activeView(); }

    void didStartProvisionalLoadForMainFrame() override;
    void didFirstVisuallyNonEmptyLayoutForMainFrame() override;
    void didFinishNavigation(API::Navigation*) override;
    void didFailNavigation(API::Navigation*) override;
    void didSameDocumentNavigationForMainFrame(SameDocumentNavigationType) override;
    void handleControlledElementIDResponse(const String&) override;

    void didPerformImmediateActionHitTest(const WebHitTestResultData&, bool contentPreventsDefault, API::Object*) override;
    NSObject *immediateActionAnimationControllerForHitTestResult(RefPtr<API::HitTestResult>, uint64_t, RefPtr<API::Object>) override;

    void videoControlsManagerDidChange() override;

    void showPlatformContextMenu(NSMenu *, WebCore::IntPoint) override;

    void didChangeBackgroundColor() override;

    void startWindowDrag() override;

    WebCore::UserInterfaceLayoutDirection userInterfaceLayoutDirection() override;
    bool effectiveAppearanceIsDark() const override;
    bool effectiveUserInterfaceLevelIsElevated() const override;

    bool useFormSemanticContext() const override;

    bool isTextRecognitionInFullscreenVideoEnabled() const final { return true; }
    void beginTextRecognitionForVideoInElementFullscreen(WebCore::ShareableBitmap::Handle&&, WebCore::FloatRect) final;
    void cancelTextRecognitionForVideoInElementFullscreen() final;

#if ENABLE(DRAG_SUPPORT)
    void didPerformDragOperation(bool handled) final;
#endif

    RetainPtr<NSView> inspectorAttachmentView() override;
    _WKRemoteObjectRegistry *remoteObjectRegistry() override;

#if ENABLE(WIRELESS_PLAYBACK_TARGET)
    WebCore::WebMediaSessionManager& mediaSessionManager() final;
#endif

    void refView() override;
    void derefView() override;

    void pageDidScroll(const WebCore::IntPoint&) override;
    void didRestoreScrollPosition() override;
    bool windowIsFrontWindowUnderMouse(const NativeWebMouseEvent&) override;

    std::optional<float> computeAutomaticTopObscuredInset() override;

    void takeFocus(WebCore::FocusDirection) override;

    void performSwitchHapticFeedback() final;

#if HAVE(APP_ACCENT_COLORS)
    WebCore::Color accentColor() override;
#if PLATFORM(MAC)
    bool appUsesCustomAccentColor() override;
#endif
#endif

#if HAVE(TRANSLATION_UI_SERVICES) && ENABLE(CONTEXT_MENUS)
    bool canHandleContextMenuTranslation() const override;
    void handleContextMenuTranslation(const WebCore::TranslationContextMenuInfo&) override;
#endif

#if ENABLE(WRITING_TOOLS) && ENABLE(CONTEXT_MENUS)
    bool canHandleContextMenuWritingTools() const override;
    void handleContextMenuWritingTools(WebCore::WritingTools::RequestedTool, WebCore::IntRect) override;
#endif

#if ENABLE(DATA_DETECTION)
    void handleClickForDataDetectionResult(const WebCore::DataDetectorElementInfo&, const WebCore::IntPoint&) final;
#endif
        
    void requestScrollToRect(const WebCore::FloatRect& targetRect, const WebCore::FloatPoint& origin) override;

#if ENABLE(VIDEO_PRESENTATION_MODE)
    void didEnterFullscreen() final { }
    void didExitFullscreen() final { }
    void didCleanupFullscreen() final { }
#endif

    CheckedPtr<WebViewImpl> checkedImpl() const { return m_impl.get(); }

    bool isViewVisible(NSView *, NSWindow *);

    WeakObjCPtr<NSView> m_view;
    WeakPtr<WebViewImpl> m_impl;
#if USE(AUTOCORRECTION_PANEL)
    CorrectionPanel m_correctionPanel;
#endif

    bool m_shouldSuppressFirstResponderChanges { false };
};

} // namespace WebKit

#endif // PLATFORM(MAC)
