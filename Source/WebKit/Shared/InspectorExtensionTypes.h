/*
 * Copyright (C) 2020-2021 Apple Inc. All rights reserved.
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

#include <wtf/Forward.h>

#if ENABLE(INSPECTOR_EXTENSIONS)

namespace WebKit {
class JavaScriptEvaluationResult;
}

namespace WebCore {
struct ExceptionDetails;
}

namespace Inspector {
enum class ExtensionError : uint8_t;

using ExtensionTabID = WTF::String;
using ExtensionID = WTF::String;
using ExtensionVoidResult = Expected<void, ExtensionError>;
using ExtensionEvaluationResult = Expected<Expected<WebKit::JavaScriptEvaluationResult, std::optional<WebCore::ExceptionDetails>>, ExtensionError>;

enum class ExtensionAppearance : bool {
    Light,
    Dark
};

enum class ExtensionError : uint8_t {
    ContextDestroyed,
    InternalError,
    InvalidRequest,
    RegistrationFailed,
    NotImplemented,
};

WTF::String extensionErrorToString(ExtensionError);

} // namespace Inspector

#endif // ENABLE(INSPECTOR_EXTENSIONS)
