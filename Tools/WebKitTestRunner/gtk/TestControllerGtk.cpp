/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TestController.h"

#include "PlatformWebView.h"
#include <WebKit/WKTextCheckerGLib.h>
#include <WebKit/WKViewPrivate.h>
#include <gtk/gtk.h>
#include <wtf/Platform.h>
#include <wtf/RunLoop.h>
#include <wtf/WTFProcess.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/Base64.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

#if USE(SKIA)
#include <skia/core/SkData.h>

IGNORE_CLANG_WARNINGS_BEGIN("cast-align")
#include <skia/encode/SkPngEncoder.h>
IGNORE_CLANG_WARNINGS_END
#endif

namespace WTR {

void TestController::notifyDone()
{
    RunLoop::mainSingleton().stop();
}

void TestController::platformInitialize(const Options&)
{
}

void TestController::platformDestroy()
{
}

void TestController::platformRunUntil(bool& done, WTF::Seconds timeout)
{
    bool timedOut = false;
    class TimeoutTimer {
    public:
        TimeoutTimer(WTF::Seconds timeout, bool& timedOut)
            : m_timer(RunLoop::mainSingleton(), "TestController::TimeoutTimer"_s, [&timedOut] {
                timedOut = true;
                RunLoop::mainSingleton().stop();
            })
        {
            m_timer.setPriority(G_PRIORITY_DEFAULT_IDLE);
            if (timeout >= 0_s)
                m_timer.startOneShot(timeout);
        }
    private:
        RunLoop::Timer m_timer;
    } timeoutTimer(timeout, timedOut);

    while (!done && !timedOut)
        RunLoop::mainSingleton().run();
}

static char* getEnvironmentVariableAsUTF8String(const char* variableName)
{
    const char* value = g_getenv(variableName);
    if (!value) {
        fprintf(stderr, "%s environment variable not found\n", variableName);
        exitProcess(0);
    }
    gsize bytesWritten;
    return g_filename_to_utf8(value, -1, 0, &bytesWritten, 0);
}

void TestController::initializeInjectedBundlePath()
{
    GUniquePtr<char> utf8BundlePath(getEnvironmentVariableAsUTF8String("TEST_RUNNER_INJECTED_BUNDLE_FILENAME"));
    m_injectedBundlePath.adopt(WKStringCreateWithUTF8CString(utf8BundlePath.get()));
}

void TestController::initializeTestPluginDirectory()
{
    // Plugins are no longer supported in WebKit (see WKContextSetAdditionalPluginsDirectory()).
}

void TestController::platformInitializeContext()
{
}

void TestController::setHidden(bool hidden)
{
    if (!m_mainWebView)
        return;
    if (hidden)
        gtk_widget_unmap(GTK_WIDGET(m_mainWebView->platformView()));
    else
        gtk_widget_map(GTK_WIDGET(m_mainWebView->platformView()));
}

void TestController::runModal(PlatformWebView*)
{
    // FIXME: Need to implement this to test showModalDialog.
}

void TestController::abortModal()
{
}

WKContextRef TestController::platformContext()
{
    return m_context.get();
}

const char* TestController::platformLibraryPathForTesting()
{
    return nullptr;
}

void TestController::platformConfigureViewForTest(const TestInvocation&)
{
    WKRetainPtr<WKStringRef> appName = adoptWK(WKStringCreateWithUTF8CString("WebKitTestRunnerGTK"));
    WKPageSetApplicationNameForUserAgent(mainWebView()->page(), appName.get());
}

bool TestController::platformResetStateToConsistentValues(const TestOptions&)
{
    if (m_mainWebView) {
        m_mainWebView->dismissAllPopupMenus();
        WKViewSetEditable(m_mainWebView->platformView(), false);
    }

    WKTextCheckerContinuousSpellCheckingEnabledStateChanged(true);
    return true;
}

TestFeatures TestController::platformSpecificFeatureDefaultsForTest(const TestCommand&) const
{
    return { };
}

WKRetainPtr<WKStringRef> TestController::takeViewPortSnapshot()
{
#if USE(CAIRO)
    Vector<uint8_t> output;
    cairo_surface_write_to_png_stream(mainWebView()->windowSnapshotImage(), [](void* output, const unsigned char* data, unsigned length) -> cairo_status_t {
        reinterpret_cast<Vector<uint8_t>*>(output)->append(std::span { reinterpret_cast<const uint8_t*>(data), length });
        return CAIRO_STATUS_SUCCESS;
    }, &output);
    auto uri = makeString("data:image/png;base64,"_s, base64Encoded(output.span()));
#elif USE(SKIA)
    sk_sp<SkImage> image(mainWebView()->windowSnapshotImage());
    auto data = SkPngEncoder::Encode(nullptr, image.get(), { });
    auto uri = makeString("data:image/png;base64,"_s, base64Encoded(std::span { static_cast<const uint8_t*>(data->data()), data->size() }));
#endif
    return adoptWK(WKStringCreateWithUTF8CString(uri.utf8().data()));
}

} // namespace WTR
