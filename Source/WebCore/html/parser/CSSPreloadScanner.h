/*
 * Copyright (C) 2008, 2013 Apple Inc. All rights reserved.
 * Copyright (C) 2010-2020 Google Inc. All rights reserved.
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

#include "HTMLResourcePreloader.h"
#include "HTMLToken.h"
#include <wtf/Vector.h>

namespace WebCore {

class CSSPreloadScanner {
    WTF_MAKE_NONCOPYABLE(CSSPreloadScanner);
public:
    CSSPreloadScanner();
    ~CSSPreloadScanner();

    void reset();

    void scan(const HTMLToken::DataVector&, PreloadRequestStream&);

private:
    enum State {
        Initial,
        MaybeComment,
        Comment,
        MaybeCommentEnd,
        RuleStart,
        Rule,
        AfterRule,
        RuleValue,
        AfterRuleValue,
        RuleConditions,
        DoneParsingImportRules,
    };

    inline void tokenize(char16_t);
    void emitRule();
    bool hasFinishedRuleValue() const;

    State m_state;
    Vector<char16_t> m_rule;
    Vector<char16_t> m_ruleValue;
    Vector<char16_t> m_ruleConditions;

    // Only non-zero during scan()
    PreloadRequestStream* m_requests;
};

} // namespace WebCore
