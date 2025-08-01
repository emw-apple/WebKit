/*
 * Copyright (C) 2010-2021 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2011 Motorola Mobility, Inc. All rights reserved.
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

#include "APIObject.h"
#include "Connection.h"
#include "DebuggableInfoData.h"
#include "MessageReceiver.h"
#include "WebInspectorBackendProxy.h"
#include "WebInspectorUtilities.h"
#include "WebPageProxy.h"
#include "WebPageProxyIdentifier.h"
#include <JavaScriptCore/InspectorFrontendChannel.h>
#include <WebCore/Color.h>
#include <WebCore/FloatRect.h>
#include <WebCore/InspectorBackendClient.h>
#include <WebCore/InspectorFrontendClient.h>
#include <wtf/CheckedPtr.h>
#include <wtf/Forward.h>
#include <wtf/RefPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>


#if PLATFORM(MAC)
#include "WKGeometry.h"
#include <WebCore/IntRect.h>
#include <wtf/HashMap.h>
#include <wtf/RetainPtr.h>
#include <wtf/RunLoop.h>

OBJC_CLASS NSString;
OBJC_CLASS NSURL;
OBJC_CLASS NSView;
OBJC_CLASS NSWindow;
OBJC_CLASS WKWebInspectorUIProxyObjCAdapter;
OBJC_CLASS WKInspectorViewController;
#elif PLATFORM(WIN)
#include "WebView.h"
#elif PLATFORM(GTK)
#include <wtf/glib/GWeakPtr.h>
#elif PLATFORM(WPE)
#include "WPEWebView.h"
#include <wtf/glib/GRefPtr.h>
typedef struct _WPEToplevel WPEToplevel;
#endif

namespace WebCore {
class CertificateInfo;
}

namespace API {
class InspectorClient;
}

namespace WebKit {

class WebFrameProxy;
class WebInspectorUIProxyClient;
class WebPreferences;
#if ENABLE(INSPECTOR_EXTENSIONS)
class WebInspectorUIExtensionControllerProxy;
#endif
class WebInspectorBackendProxy;

enum class AttachmentSide : uint8_t {
    Bottom,
    Right,
    Left,
};

class WebInspectorUIProxy
    : public API::ObjectImpl<API::Object::Type::Inspector>
    , public IPC::MessageReceiver
    , public Inspector::FrontendChannel
#if PLATFORM(WIN)
    , public WebCore::WindowMessageListener
#endif
{
    friend class WebInspectorBackendProxy;
public:
    static Ref<WebInspectorUIProxy> create(WebPageProxy& inspectedPage)
    {
        return adoptRef(*new WebInspectorUIProxy(inspectedPage));
    }

    explicit WebInspectorUIProxy(WebPageProxy&);
    virtual ~WebInspectorUIProxy();

    void ref() const final { API::ObjectImpl<API::Object::Type::Inspector>::ref(); }
    void deref() const final { API::ObjectImpl<API::Object::Type::Inspector>::deref(); }

    void invalidate();

    API::InspectorClient& inspectorClient() { return *m_inspectorClient; }
    void setInspectorClient(std::unique_ptr<API::InspectorClient>&&);

    // Public APIs
    WebPageProxy* inspectedPage() const { return m_inspectedPage.get(); }
    RefPtr<WebPageProxy> protectedInspectedPage() const { return m_inspectedPage.get(); }
    WebPageProxy* inspectorPage() const { return m_inspectorPage.get(); }
    RefPtr<WebPageProxy> protectedInspectorPage() const { return m_inspectorPage.get(); }

#if ENABLE(INSPECTOR_EXTENSIONS)
    WebInspectorUIExtensionControllerProxy* extensionController() const { return m_extensionController.get(); }
    RefPtr<WebInspectorUIExtensionControllerProxy> protectedExtensionController() const;
#endif

    bool isConnected() const { return !!m_inspectorPage; }
    bool isVisible() const { return m_isVisible; }
    bool isFront();

    void connect();

    void show();
    void hide();
    void close();
    void closeForCrash();
    void reopen();
    void resetState();

    void reset();
    void updateForNewPageProcess(WebPageProxy&);

#if PLATFORM(MAC)
    enum class InspectionTargetType { Local, Remote };
    static RetainPtr<NSWindow> createFrontendWindow(NSRect savedWindowFrame, InspectionTargetType, WebPageProxy* inspectedPage = nullptr);
    static void showSavePanel(NSWindow *, NSURL *, Vector<WebCore::InspectorFrontendClient::SaveData>&&, bool forceSaveAs, CompletionHandler<void(NSURL *)>&&);

    void didBecomeActive();

    void updateInspectorWindowTitle() const;
    void inspectedViewFrameDidChange(CGFloat = 0);
    void windowFrameDidChange();
    void windowFullScreenDidChange();

    void closeFrontendPage();
    void closeFrontendAfterInactivityTimerFired();

    void attachmentViewDidChange(NSView *oldView, NSView *newView);
    void attachmentWillMoveFromWindow(NSWindow *oldWindow);
    void attachmentDidMoveToWindow(NSWindow *newWindow);

    const WebCore::FloatRect& sheetRect() const { return m_sheetRect; }
#endif

#if PLATFORM(WIN)
    static void showSavePanelForSingleFile(HWND, Vector<WebCore::InspectorFrontendClient::SaveData>&&);
#endif

#if PLATFORM(GTK)
    GtkWidget* inspectorView() const { return m_inspectorView.get(); };
    void setClient(std::unique_ptr<WebInspectorUIProxyClient>&&);
#endif

    void showConsole();
    void showResources();
    void showMainResourceForFrame(WebCore::FrameIdentifier);
    void openURLExternally(const String& url);
    void revealFileExternally(const String& path);

    AttachmentSide attachmentSide() const { return m_attachmentSide; }
    bool isAttached() const { return m_isAttached; }
    void attachRight();
    void attachLeft();
    void attachBottom();
    void attach(AttachmentSide = AttachmentSide::Bottom);
    void detach();

    void setAttachedWindowHeight(unsigned);
    void setAttachedWindowWidth(unsigned);

    void setSheetRect(const WebCore::FloatRect&);

    void startWindowDrag();

    bool isProfilingPage() const { return m_isProfilingPage; }
    void togglePageProfiling();

    bool isElementSelectionActive() const { return m_elementSelectionActive; }
    void toggleElementSelection();

    bool isUnderTest() const { return m_underTest; }
    void markAsUnderTest() { m_underTest = true; }

    void setDiagnosticLoggingAvailable(bool);

    // Provided by platform WebInspectorUIProxy implementations.
    static String inspectorPageURL();
    static String inspectorTestPageURL();
    static bool isMainOrTestInspectorPage(const URL&);
    static DebuggableInfoData infoForLocalDebuggable();

    static const unsigned minimumWindowWidth;
    static const unsigned minimumWindowHeight;

    static const unsigned initialWindowWidth;
    static const unsigned initialWindowHeight;

    // Testing methods.
    void evaluateInFrontendForTesting(const String&);

private:
    const RefPtr<WebInspectorBackendProxy> m_backend;

    void createFrontendPage();
    void closeFrontendPageAndWindow();

    void dispatchDidChangeLocalInspectorAttachment();

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) override;

    // Inspector::FrontendChannel
    void sendMessageToFrontend(const String& message) override;
    ConnectionType connectionType() const override { return ConnectionType::Local; }

    RefPtr<WebPageProxy> platformCreateFrontendPage();
    void platformCreateFrontendWindow();
    void platformCloseFrontendPageAndWindow();

    void platformDidCloseForCrash();
    void platformInvalidate();
    void platformResetState();
    void platformBringToFront();
    void platformBringInspectedPageToFront();
    void platformHide();
    bool platformIsFront();
    void platformAttachAvailabilityChanged(bool);
    void platformSetForcedAppearance(WebCore::InspectorFrontendClient::Appearance);
    void platformOpenURLExternally(const String&);
    void platformInspectedURLChanged(const String&);
    void platformShowCertificate(const WebCore::CertificateInfo&);
    void platformAttach();
    void platformDetach();
    void platformSetAttachedWindowHeight(unsigned);
    void platformSetAttachedWindowWidth(unsigned);
    void platformSetSheetRect(const WebCore::FloatRect&);
    void platformStartWindowDrag();
    void platformRevealFileExternally(const String&);
    void platformSave(Vector<WebCore::InspectorFrontendClient::SaveData>&&, bool forceSaveAs);
    void platformLoad(const String& path, CompletionHandler<void(const String&)>&&);
    void platformPickColorFromScreen(CompletionHandler<void(const std::optional<WebCore::Color>&)>&&);

#if PLATFORM(MAC) || PLATFORM(GTK) || PLATFORM(WIN)
    bool platformCanAttach(bool webProcessCanAttach);
#elif PLATFORM(WPE)
    bool platformCanAttach(bool) { return false; }
#else
    bool platformCanAttach(bool webProcessCanAttach) { return webProcessCanAttach; }
#endif

    // Called by WebInspectorBackendProxy and WebInspectorUIProxy messages
    void requestOpenLocalInspectorFrontend();
    void setFrontendConnection(IPC::Connection::Handle&&);

    void openLocalInspectorFrontend();
    void sendMessageToBackend(const String&);
    void frontendLoaded();
    void didClose();
    void bringToFront();
    void bringInspectedPageToFront();
    void attachAvailabilityChanged(bool);
    void setForcedAppearance(WebCore::InspectorFrontendClient::Appearance);
    void effectiveAppearanceDidChange(WebCore::InspectorFrontendClient::Appearance);
    void inspectedURLChanged(const String&);
    void showCertificate(const WebCore::CertificateInfo&);
    void setInspectorPageDeveloperExtrasEnabled(bool);
    void elementSelectionChanged(bool);
    void timelineRecordingChanged(bool);

    void setDeveloperPreferenceOverride(WebCore::InspectorBackendClient::DeveloperPreference, std::optional<bool>);
#if ENABLE(INSPECTOR_NETWORK_THROTTLING)
    void setEmulatedConditions(std::optional<int64_t>&& bytesPerSecondLimit);
#endif

    void save(Vector<WebCore::InspectorFrontendClient::SaveData>&&, bool forceSaveAs);
    void load(const String& path, CompletionHandler<void(const String&)>&&);
    void pickColorFromScreen(CompletionHandler<void(const std::optional<WebCore::Color>&)>&&);

    bool canAttach() const { return m_canAttach; }
    bool shouldOpenAttached();

    void open();

    unsigned inspectionLevel() const;

    WebPreferences& inspectorPagePreferences() const;
    Ref<WebPreferences> protectedInspectorPagePreferences() const;

#if PLATFORM(MAC)
    void applyForcedAppearance();
#endif

#if PLATFORM(GTK) || PLATFORM(WPE)
    void updateInspectorWindowTitle() const;
#endif

#if PLATFORM(WIN)
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    bool registerWindowClass();
    void windowReceivedMessage(HWND, UINT, WPARAM, LPARAM) override;
#endif

    WeakPtr<WebPageProxy> m_inspectedPage;
    WeakPtr<WebPageProxy> m_inspectorPage;
    std::unique_ptr<API::InspectorClient> m_inspectorClient;
    WebPageProxyIdentifier m_inspectedPageIdentifier;

#if ENABLE(INSPECTOR_EXTENSIONS)
    RefPtr<WebInspectorUIExtensionControllerProxy> m_extensionController;
#endif
    
    bool m_underTest { false };
    bool m_isVisible { false };
    bool m_isAttached { false };
    bool m_canAttach { false };
    bool m_isProfilingPage { false };
    bool m_showMessageSent { false };
    bool m_ignoreFirstBringToFront { false };
    bool m_elementSelectionActive { false };
    bool m_ignoreElementSelectionChange { false };
    bool m_isActiveFrontend { false };
    bool m_isOpening { false };
    bool m_closing { false };

    AttachmentSide m_attachmentSide { AttachmentSide::Bottom };

#if PLATFORM(MAC)
    RetainPtr<WKInspectorViewController> m_inspectorViewController;
    RetainPtr<NSWindow> m_inspectorWindow;
    RetainPtr<WKWebInspectorUIProxyObjCAdapter> m_objCAdapter;
    HashMap<String, RetainPtr<NSURL>> m_suggestedToActualURLMap;
    RunLoop::Timer m_closeFrontendAfterInactivityTimer;
    String m_urlString;
    WebCore::FloatRect m_sheetRect;
    WebCore::InspectorFrontendClient::Appearance m_frontendAppearance { WebCore::InspectorFrontendClient::Appearance::System };
    bool m_isObservingContentLayoutRect { false };
#elif PLATFORM(GTK)
    std::unique_ptr<WebInspectorUIProxyClient> m_client;
    GWeakPtr<GtkWidget> m_inspectorView;
    GWeakPtr<GtkWidget> m_inspectorWindow;
    GtkWidget* m_headerBar { nullptr };
    String m_inspectedURLString;
#elif PLATFORM(WPE)
    RefPtr<WKWPE::View> m_inspectorView;
    GRefPtr<WPEToplevel> m_inspectorWindow;
#elif PLATFORM(WIN)
    HWND m_inspectedViewWindow { nullptr };
    HWND m_inspectedViewParentWindow { nullptr };
    HWND m_inspectorViewWindow { nullptr };
    HWND m_inspectorDetachWindow { nullptr };
    RefPtr<WebView> m_inspectorView;
#endif
};

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::WebInspectorUIProxy)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::Inspector; }
SPECIALIZE_TYPE_TRAITS_END()
