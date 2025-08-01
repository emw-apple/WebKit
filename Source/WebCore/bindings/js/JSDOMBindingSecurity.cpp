/*
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2004-2020 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Samuel Weinig <sam@webkit.org>
 *  Copyright (C) 2013 Michael Pruett <michael@68k.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "JSDOMBindingSecurity.h"

#include "DocumentInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "HTTPParsers.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMWindowBase.h"
#include "LocalDOMWindow.h"
#include "LocalFrame.h"
#include "SecurityOrigin.h"
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
using namespace JSC;

void printErrorMessageForFrame(LocalFrame* frame, const String& message)
{
    if (!frame)
        return;
    frame->document()->window()->printErrorMessage(message);
}

// FIXME: Refactor to share code with LocalDOMWindow::crossDomainAccessErrorMessage.
static String remoteFrameAccessError(JSC::JSGlobalObject* lexicalGlobalObject)
{
    auto& active = activeDOMWindow(*lexicalGlobalObject);
    Ref activeOrigin = active.document()->securityOrigin();
    return makeString("Blocked a frame with origin \""_s, activeOrigin->toString(), "\" from accessing a cross-origin frame. Protocols, domains, and ports must match."_s);
}

// FIXME: Refactor to share more code with canAccessDocument.
static void reportErrorAccessingRemoteFrame(JSC::JSGlobalObject* lexicalGlobalObject, SecurityReportingOption reportingOption)
{
    switch (reportingOption) {
    case ThrowSecurityError: {
        VM& vm = lexicalGlobalObject->vm();
        auto scope = DECLARE_THROW_SCOPE(vm);
        throwSecurityError(*lexicalGlobalObject, scope, remoteFrameAccessError(lexicalGlobalObject));
        break;
    }
    case LogSecurityError:
        // FIXME: Implement.
        break;
    case DoNotReportSecurityError:
        break;
    }
}

static inline bool canAccessDocument(JSC::JSGlobalObject* lexicalGlobalObject, Document* targetDocument, SecurityReportingOption reportingOption)
{
    if (!targetDocument)
        return false;

    if (auto* templateHost = targetDocument->templateDocumentHost())
        targetDocument = templateHost;

    auto& active = activeDOMWindow(*lexicalGlobalObject);

    if (active.document()->protectedSecurityOrigin()->isSameOriginDomain(targetDocument->securityOrigin()))
        return true;

    switch (reportingOption) {
    case ThrowSecurityError: {
        VM& vm = lexicalGlobalObject->vm();
        auto scope = DECLARE_THROW_SCOPE(vm);
        throwSecurityError(*lexicalGlobalObject, scope, targetDocument->window()->crossDomainAccessErrorMessage(active, IncludeTargetOrigin::No));
        break;
    }
    case LogSecurityError:
        printErrorMessageForFrame(targetDocument->frame(), targetDocument->window()->crossDomainAccessErrorMessage(active, IncludeTargetOrigin::Yes));
        break;
    case DoNotReportSecurityError:
        break;
    }

    return false;
}

bool BindingSecurity::shouldAllowAccessToFrame(JSGlobalObject& lexicalGlobalObject, LocalFrame& frame, String& message)
{
    if (BindingSecurity::shouldAllowAccessToFrame(&lexicalGlobalObject, &frame, DoNotReportSecurityError))
        return true;
    message = frame.document()->window()->crossDomainAccessErrorMessage(activeDOMWindow(lexicalGlobalObject), IncludeTargetOrigin::No);
    return false;
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSGlobalObject& lexicalGlobalObject, LocalDOMWindow* globalObject, String& message)
{
    return globalObject && shouldAllowAccessToDOMWindow(lexicalGlobalObject, *globalObject, message);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSGlobalObject& lexicalGlobalObject, LocalDOMWindow& globalObject, String& message)
{
    VM& vm = lexicalGlobalObject.vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

    bool shouldAllowAccess = BindingSecurity::shouldAllowAccessToDOMWindow(&lexicalGlobalObject, globalObject, DoNotReportSecurityError);
    EXCEPTION_ASSERT_UNUSED(scope, !scope.exception());
    if (shouldAllowAccess)
        return true;
    message = globalObject.crossDomainAccessErrorMessage(activeDOMWindow(lexicalGlobalObject), IncludeTargetOrigin::No);
    return false;
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject* lexicalGlobalObject, LocalDOMWindow& target, SecurityReportingOption reportingOption)
{
    return canAccessDocument(lexicalGlobalObject, target.document(), reportingOption);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject* lexicalGlobalObject, LocalDOMWindow* target, SecurityReportingOption reportingOption)
{
    return target && shouldAllowAccessToDOMWindow(lexicalGlobalObject, *target, reportingOption);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject* lexicalGlobalObject, DOMWindow* window, SecurityReportingOption reportingOption)
{
    auto* localWindow = dynamicDowncast<LocalDOMWindow>(window);
    if (window && !localWindow) {
        reportErrorAccessingRemoteFrame(lexicalGlobalObject, reportingOption);
        return false;
    }
    return shouldAllowAccessToDOMWindow(lexicalGlobalObject, localWindow, reportingOption);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject& lexicalGlobalObject, DOMWindow* window, String& message)
{
    auto* localWindow = dynamicDowncast<LocalDOMWindow>(window);
    if (window && !localWindow) {
        message = remoteFrameAccessError(&lexicalGlobalObject);
        return false;
    }
    return shouldAllowAccessToDOMWindow(lexicalGlobalObject, localWindow, message);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject* lexicalGlobalObject, DOMWindow& window, SecurityReportingOption reportingOption)
{
    return shouldAllowAccessToDOMWindow(lexicalGlobalObject, &window, reportingOption);
}

bool BindingSecurity::shouldAllowAccessToDOMWindow(JSC::JSGlobalObject& lexicalGlobalObject, DOMWindow& window, String& message)
{
    return shouldAllowAccessToDOMWindow(lexicalGlobalObject, &window, message);
}

bool BindingSecurity::shouldAllowAccessToFrame(JSC::JSGlobalObject* lexicalGlobalObject, LocalFrame* target, SecurityReportingOption reportingOption)
{
    return target && canAccessDocument(lexicalGlobalObject, target->document(), reportingOption);
}

bool BindingSecurity::shouldAllowAccessToNode(JSC::JSGlobalObject& lexicalGlobalObject, Node* target)
{
    return !target || canAccessDocument(&lexicalGlobalObject, &target->document(), LogSecurityError);
}

} // namespace WebCore
