/**
 * Copyright (C) 2005-2022 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "RenderButton.h"

#include "Document.h"
#include "GraphicsContext.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "LayoutIntegrationLineLayout.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderChildIterator.h"
#include "RenderElementInlines.h"
#include "RenderStyleSetters.h"
#include "RenderTheme.h"
#include "RenderTreeBuilder.h"
#include "StyleInheritedData.h"
#include <wtf/TZoneMallocInlines.h>

#if PLATFORM(IOS_FAMILY)
#include "RenderThemeIOS.h"
#endif

namespace WebCore {

using namespace HTMLNames;

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderButton);

RenderButton::RenderButton(HTMLFormControlElement& element, RenderStyle&& style)
    : RenderFlexibleBox(Type::Button, element, WTFMove(style))
{
    ASSERT(isRenderButton());
}

RenderButton::~RenderButton() = default;

HTMLFormControlElement& RenderButton::formControlElement() const
{
    return downcast<HTMLFormControlElement>(nodeForNonAnonymous());
}

bool RenderButton::canBeSelectionLeaf() const
{
    return formControlElement().hasEditableStyle();
}

bool RenderButton::hasLineIfEmpty() const
{
    return is<HTMLInputElement>(formControlElement());
}

void RenderButton::setInnerRenderer(RenderBlock& innerRenderer)
{
    ASSERT(!m_inner.get());
    m_inner = innerRenderer;
    updateAnonymousChildStyle(m_inner->mutableStyle());

    if (m_inner && m_inner->layoutBox()) {
        if (auto* inlineFormattingContextRoot = dynamicDowncast<RenderBlockFlow>(*m_inner); inlineFormattingContextRoot && inlineFormattingContextRoot->inlineLayout())
            inlineFormattingContextRoot->inlineLayout()->rootStyleWillChange(*inlineFormattingContextRoot, inlineFormattingContextRoot->style());
        if (auto* lineLayout = LayoutIntegration::LineLayout::containing(*m_inner))
            lineLayout->styleWillChange(*m_inner, m_inner->style(), StyleDifference::Layout);
        LayoutIntegration::LineLayout::updateStyle(*m_inner);
        for (auto& child : childrenOfType<RenderText>(*m_inner))
            LayoutIntegration::LineLayout::updateStyle(child);
    }
}

void RenderButton::updateAnonymousChildStyle(RenderStyle& childStyle) const
{
    childStyle.setFlexGrow(1.0f);
    // min-inline-size: 0; is needed for correct shrinking.
    // Use margin-block:auto instead of align-items:center to get safe centering, i.e.
    // when the content overflows, treat it the same as align-items: flex-start.
    if (isHorizontalWritingMode()) {
        childStyle.setMinWidth(0_css_px);
        childStyle.setMarginTop(CSS::Keyword::Auto { });
        childStyle.setMarginBottom(CSS::Keyword::Auto { });
    } else {
        childStyle.setMinHeight(0_css_px);
        childStyle.setMarginLeft(CSS::Keyword::Auto { });
        childStyle.setMarginRight(CSS::Keyword::Auto { });
    }
    childStyle.setTextBoxTrim(style().textBoxTrim());
}

void RenderButton::updateFromElement()
{
    // If we're an input element, we may need to change our button text.
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(formControlElement())) {
        String value = input->valueWithDefault();
        setText(value);
    }
}

void RenderButton::setText(const String& str)
{
    if (!m_buttonText && str.isEmpty())
        return;

    if (!m_buttonText) {
        auto newButtonText = createRenderer<RenderTextFragment>(document(), str);
        m_buttonText = *newButtonText;
        // FIXME: This mutation should go through the normal RenderTreeBuilder path.
        if (RenderTreeBuilder::current())
            RenderTreeBuilder::current()->attach(*this, WTFMove(newButtonText));
        else
            RenderTreeBuilder(*document().renderView()).attach(*this, WTFMove(newButtonText));
        return;
    }

    if (!str.isEmpty()) {
        m_buttonText->setText(str.impl());
        return;
    }
    if (RenderTreeBuilder::current())
        RenderTreeBuilder::current()->destroy(*m_buttonText);
    else
        RenderTreeBuilder(*document().renderView()).destroy(*m_buttonText);
}

String RenderButton::text() const
{
    if (m_buttonText)
        return m_buttonText->text();
    return { };
}

bool RenderButton::canHaveGeneratedChildren() const
{
    // Input elements can't have generated children, but button elements can. We'll
    // write the code assuming any other button types that might emerge in the future
    // can also have children.
    return !is<HTMLInputElement>(formControlElement());
}

LayoutRect RenderButton::controlClipRect(const LayoutPoint& additionalOffset) const
{
    // Clip to the padding box to at least give content the extra padding space.
    return LayoutRect(additionalOffset.x() + borderLeft(), additionalOffset.y() + borderTop(), width() - borderLeft() - borderRight(), height() - borderTop() - borderBottom());
}

#if PLATFORM(IOS_FAMILY)
void RenderButton::layout()
{
    RenderFlexibleBox::layout();

    // FIXME: We should not be adjusting styles during layout. See <rdar://problem/7675493>.
    RenderThemeIOS::adjustRoundBorderRadius(mutableStyle(), *this);
}
#endif

// Only clip overflow on input elements, to match other browsers.
bool RenderButton::hasControlClip() const
{
    return is<HTMLInputElement>(formControlElement());
}

} // namespace WebCore
