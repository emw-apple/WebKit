/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003-2021 Apple Inc. All rights reserved.
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

#pragma once

#include "RenderBox.h"

namespace WebCore {

class CSSCounterStyle;
class RenderListItem;
class StyleRuleCounterStyle;

struct ListMarkerTextContent {
    String textWithSuffix;
    uint32_t textWithoutSuffixLength { 0 };
    TextDirection textDirection { TextDirection::LTR };
    bool isEmpty() const
    {
        return textWithSuffix.isEmpty();
    }

    StringView textWithoutSuffix() const LIFETIME_BOUND
    {
        return StringView { textWithSuffix }.left(textWithoutSuffixLength);
    }

    StringView suffix() const LIFETIME_BOUND
    {
        return StringView { textWithSuffix }.substring(textWithoutSuffixLength);
    }
};

// Used to render the list item's marker.
// The RenderListMarker always has to be a child of a RenderListItem.
class RenderListMarker final : public RenderBox {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderListMarker);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderListMarker);
public:
    RenderListMarker(RenderListItem&, RenderStyle&&);
    virtual ~RenderListMarker();

    String textWithoutSuffix() const { return m_textContent.textWithoutSuffix().toString(); };
    String textWithSuffix() const { return m_textContent.textWithSuffix; };

    bool isInside() const;

    void updateInlineMarginsAndContent();

    bool isImage() const final;

    LayoutUnit lineLogicalOffsetForListItem() const { return m_lineLogicalOffsetForListItem; }
    const RenderListItem* listItem() const;

    std::pair<int, int> layoutBounds() const { return m_layoutBounds; }

private:
    void willBeDestroyed() final;
    ASCIILiteral renderName() const final { return "RenderListMarker"_s; }
    void computePreferredLogicalWidths() final;
    bool canHaveChildren() const final { return false; }
    void paint(PaintInfo&, const LayoutPoint&) final;
    void layout() final;
    void imageChanged(WrappedImagePtr, const IntRect*) final;
    LayoutRect selectionRectForRepaint(const RenderLayerModelObject* repaintContainer, bool clipToVisibleContent) final;
    bool canBeSelectionLeaf() const final { return true; }
    void styleWillChange(StyleDifference, const RenderStyle& newStyle) final;
    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) final;
    void computeIntrinsicLogicalWidths(LayoutUnit&, LayoutUnit&) const override { ASSERT_NOT_REACHED(); }
    std::pair<int, int> layoutBoundForTextContent(String) const;

    void element() const = delete;

    void updateInlineMargins();
    void updateContent();
    RenderBox* parentBox(RenderBox&);
    FloatRect relativeMarkerRect();
    LayoutRect localSelectionRect();

    RefPtr<CSSCounterStyle> counterStyle() const;
    bool widthUsesMetricsOfPrimaryFont() const;

    ListMarkerTextContent m_textContent;
    RefPtr<StyleImage> m_image;

    SingleThreadWeakPtr<RenderListItem> m_listItem;
    LayoutUnit m_lineOffsetForListItem;
    LayoutUnit m_lineLogicalOffsetForListItem;
    std::pair<int, int> m_layoutBounds;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderListMarker, isRenderListMarker())
