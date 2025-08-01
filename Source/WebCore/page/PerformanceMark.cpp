/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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
#include "PerformanceMark.h"

#include "DOMWrapperWorld.h"
#include "Document.h"
#include "ExceptionOr.h"
#include "LocalDOMWindow.h"
#include "MessagePort.h"
#include "Performance.h"
#include "PerformanceMarkOptions.h"
#include "PerformanceUserTiming.h"
#include "SerializedScriptValue.h"
#include "WorkerGlobalScope.h"
#include <JavaScriptCore/JSCJSValueInlines.h>

namespace WebCore {

static double performanceNow(ScriptExecutionContext& scriptExecutionContext)
{
    // FIXME: We should consider moving the Performance object to be owned by the
    // the ScriptExecutionContext to avoid this.

    if (RefPtr document = dynamicDowncast<Document>(scriptExecutionContext)) {
        if (auto window = document->window())
            return window->performance().now();
    } else if (RefPtr workerGlobal = dynamicDowncast<WorkerGlobalScope>(scriptExecutionContext))
        return workerGlobal->performance().now();

    return 0;
}

ExceptionOr<Ref<PerformanceMark>> PerformanceMark::create(JSC::JSGlobalObject& globalObject, ScriptExecutionContext& scriptExecutionContext, const String& name, std::optional<PerformanceMarkOptions>&& markOptions)
{
    if (is<Document>(scriptExecutionContext) && PerformanceUserTiming::isRestrictedMarkName(name))
        return Exception { ExceptionCode::SyntaxError };

    double startTime;
    JSC::JSValue detail;
    if (markOptions) {
        if (markOptions->startTime) {
            if (*markOptions->startTime < 0)
                return Exception { ExceptionCode::TypeError };
            startTime = *markOptions->startTime;
        } else
            startTime = performanceNow(scriptExecutionContext);
        
        if (markOptions->detail.isUndefined())
            detail = JSC::jsNull();
        else
            detail = markOptions->detail;
    } else {
        startTime = performanceNow(scriptExecutionContext);
        detail = JSC::jsNull();
    }

    Vector<Ref<MessagePort>> ignoredMessagePorts;
    auto serializedDetail = SerializedScriptValue::create(globalObject, detail, { }, ignoredMessagePorts);
    if (serializedDetail.hasException())
        return serializedDetail.releaseException();

    return adoptRef(*new PerformanceMark(name, startTime, serializedDetail.releaseReturnValue()));
}

PerformanceMark::PerformanceMark(const String& name, double startTime, Ref<SerializedScriptValue>&& serializedDetail)
    : PerformanceEntry(name, startTime, startTime)
    , m_serializedDetail(WTFMove(serializedDetail))
{
}

PerformanceMark::~PerformanceMark() = default;

JSC::JSValue PerformanceMark::detail(JSC::JSGlobalObject& globalObject)
{
    return m_serializedDetail->deserialize(globalObject, &globalObject);
}

}
