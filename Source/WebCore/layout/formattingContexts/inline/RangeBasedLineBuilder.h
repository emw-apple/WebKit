/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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

#include "AbstractLineBuilder.h"
#include "TextOnlySimpleLineBuilder.h"

namespace WebCore {
namespace Layout {

class InlineContentBreaker;

class RangeBasedLineBuilder final : public AbstractLineBuilder {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(RangeBasedLineBuilder);
public:
    RangeBasedLineBuilder(InlineFormattingContext&, HorizontalConstraints rootHorizontalConstraints, const InlineContentCache::InlineItems&);
    LineLayoutResult layoutInlineContent(const LineInput&, const std::optional<PreviousLine>&) final;

    static bool isEligibleForRangeInlineLayout(const InlineFormattingContext&, const InlineContentCache::InlineItems&, const PlacedFloats&);

private:
    TextOnlySimpleLineBuilder m_textOnlySimpleLineBuilder;
    size_t m_inlineBoxCount { 0 };
};

}
}
