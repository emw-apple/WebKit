/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "HTMLTokenizer.h"
#include "SegmentedString.h"
#include <pal/text/TextEncoding.h>
#include <wtf/TZoneMalloc.h>

namespace PAL {
class TextCodec;
}

namespace WebCore {

class HTMLMetaCharsetParser {
    WTF_MAKE_TZONE_ALLOCATED(HTMLMetaCharsetParser);
    WTF_MAKE_NONCOPYABLE(HTMLMetaCharsetParser);
public:
    HTMLMetaCharsetParser();

    // Returns true if done checking, regardless whether an encoding is found.
    bool checkForMetaCharset(std::span<const uint8_t>);

    const PAL::TextEncoding& encoding() { return m_encoding; }

    // The returned encoding might not be valid.
    static PAL::TextEncoding encodingFromMetaAttributes(std::span<const std::pair<StringView, StringView>>);

private:
    bool processMeta(HTMLToken&);

    HTMLTokenizer m_tokenizer;
    const std::unique_ptr<PAL::TextCodec> m_codec;
    SegmentedString m_input;
    bool m_inHeadSection { true };
    bool m_doneChecking { false };
    PAL::TextEncoding m_encoding;
};

} // namespace WebCore
