/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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
#include "WindowOrWorkerGlobalScope.h"

#include "ExceptionOr.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObject.h"
#include "MessagePort.h"
#include "SerializedScriptValue.h"
#include "StructuredSerializeOptions.h"
#include <JavaScriptCore/Exception.h>
#include <JavaScriptCore/JSCJSValueInlines.h>
#include <wtf/text/Base64.h>

namespace WebCore {

ExceptionOr<String> WindowOrWorkerGlobalScope::btoa(const String& stringToEncode)
{
    if (stringToEncode.isNull())
        return String();

    if (!stringToEncode.containsOnlyLatin1())
        return Exception { ExceptionCode::InvalidCharacterError };

    return base64EncodeToString(byteCast<uint8_t>(stringToEncode.latin1().span()));
}

ExceptionOr<String> WindowOrWorkerGlobalScope::atob(const String& encodedString)
{
    if (encodedString.isNull())
        return String();

    auto decodedData = base64DecodeToString(encodedString, { Base64DecodeOption::ValidatePadding, Base64DecodeOption::IgnoreWhitespace });
    if (decodedData.isNull())
        return Exception { ExceptionCode::InvalidCharacterError };

    return decodedData;
}

void WindowOrWorkerGlobalScope::reportError(JSDOMGlobalObject& globalObject, JSC::JSValue error)
{
    auto& vm = globalObject.vm();
    RELEASE_ASSERT(vm.currentThreadIsHoldingAPILock());
    auto* exception = JSC::jsDynamicCast<JSC::Exception*>(error);
    if (!exception)
        exception = JSC::Exception::create(vm, error);

    reportException(&globalObject, exception);
}

ExceptionOr<JSC::JSValue> WindowOrWorkerGlobalScope::structuredClone(JSDOMGlobalObject& lexicalGlobalObject, JSDOMGlobalObject& relevantGlobalObject, JSC::JSValue value, StructuredSerializeOptions&& options)
{
    Vector<Ref<MessagePort>> ports;
    auto messageData = SerializedScriptValue::create(lexicalGlobalObject, value, WTFMove(options.transfer), ports, SerializationForStorage::No, SerializationContext::WindowPostMessage);
    if (messageData.hasException())
        return messageData.releaseException();

    auto disentangledPorts = MessagePort::disentanglePorts(WTFMove(ports));
    if (disentangledPorts.hasException())
        return disentangledPorts.releaseException();

    Vector<Ref<MessagePort>> entangledPorts;
    if (RefPtr scriptExecutionContext = relevantGlobalObject.scriptExecutionContext())
        entangledPorts = MessagePort::entanglePorts(*scriptExecutionContext, disentangledPorts.releaseReturnValue());

    return messageData.returnValue()->deserialize(lexicalGlobalObject, &relevantGlobalObject, WTFMove(entangledPorts));
}

} // namespace WebCore
