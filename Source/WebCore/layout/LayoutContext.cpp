/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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
#include "LayoutContext.h"

#include "BlockFormattingContext.h"
#include "BlockFormattingState.h"
#include "LayoutBox.h"
#include "LayoutBoxGeometry.h"
#include "LayoutElementBox.h"
#include "LayoutPhase.h"
#include "LayoutTreeBuilder.h"
#include "PathOperation.h"
#include "RenderStyleConstants.h"
#include "RenderStyleSetters.h"
#include "RenderView.h"
#include "TableFormattingContext.h"
#include "TableFormattingState.h"
#include "TableWrapperBlockFormattingContext.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {
namespace Layout {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LayoutContext);

LayoutContext::LayoutContext(LayoutState& layoutState)
    : m_layoutState(layoutState)
{
}

void LayoutContext::layout(const LayoutSize& rootContentBoxSize)
{
    // Set the geometry on the root.
    // Note that we never layout the root box. It has to have an already computed geometry (in case of ICB, it's the view geometry).
    // ICB establishes the initial BFC, but it does not live in a formatting context and while a non-ICB root(subtree layout) has to have a formatting context,
    // we could not lay it out even if we wanted to since it's outside of this LayoutContext.
    auto& boxGeometry = m_layoutState->geometryForRootBox();
    boxGeometry.setHorizontalMargin({ });
    boxGeometry.setVerticalMargin({ });
    boxGeometry.setBorder({ });
    boxGeometry.setPadding({ });
    boxGeometry.setTopLeft({ });
    boxGeometry.setContentBoxHeight(rootContentBoxSize.height());
    boxGeometry.setContentBoxWidth(rootContentBoxSize.width());

    auto scope = PhaseScope { Phase::Type::Layout };
    layoutFormattingContextSubtree(m_layoutState->root());
}


void LayoutContext::layoutFormattingContextSubtree(const ElementBox& formattingContextRoot)
{
    UNUSED_PARAM(formattingContextRoot);
}

LayoutState& LayoutContext::layoutState()
{
    return m_layoutState.get();
}

std::unique_ptr<FormattingContext> LayoutContext::createFormattingContext(const ElementBox& formattingContextRoot, LayoutState& layoutState)
{
    ASSERT(formattingContextRoot.establishesFormattingContext());

    if (formattingContextRoot.establishesBlockFormattingContext()) {
        ASSERT(!formattingContextRoot.establishesInlineFormattingContext());
        auto& blockFormattingState = layoutState.ensureBlockFormattingState(formattingContextRoot);
        if (formattingContextRoot.isTableWrapperBox())
            return makeUnique<TableWrapperBlockFormattingContext>(formattingContextRoot, blockFormattingState);
        return makeUnique<BlockFormattingContext>(formattingContextRoot, blockFormattingState);
    }

    if (formattingContextRoot.establishesTableFormattingContext()) {
        auto& tableFormattingState = layoutState.ensureTableFormattingState(formattingContextRoot);
        return makeUnique<TableFormattingContext>(formattingContextRoot, tableFormattingState);
    }

    CRASH();
}

}
}

