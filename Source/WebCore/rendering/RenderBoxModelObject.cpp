/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2005 Allan Sandfeld Jensen (kde@carewolf.com)
 *           (C) 2005, 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2005-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2010-2018 Google Inc. All rights reserved.
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
#include "RenderBoxModelObject.h"

#include "BitmapImage.h"
#include "BorderEdge.h"
#include "BorderPainter.h"
#include "BorderShape.h"
#include "CachedImage.h"
#include "ColorBlending.h"
#include "Document.h"
#include "FloatRoundedRect.h"
#include "GeometryUtilities.h"
#include "GraphicsContext.h"
#include "HTMLImageElement.h"
#include "HTMLNames.h"
#include "ImageBuffer.h"
#include "ImageQualityController.h"
#include "InlineIteratorInlineBox.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Path.h"
#include "RenderBlock.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderElementInlines.h"
#include "RenderFlexibleBox.h"
#include "RenderFragmentContainer.h"
#include "RenderInline.h"
#include "RenderLayer.h"
#include "RenderLayerBacking.h"
#include "RenderLayerCompositor.h"
#include "RenderLayerScrollableArea.h"
#include "RenderMultiColumnFlow.h"
#include "RenderObjectInlines.h"
#include "RenderTable.h"
#include "RenderTableRow.h"
#include "RenderText.h"
#include "RenderTextFragment.h"
#include "RenderTreeBuilder.h"
#include "RenderView.h"
#include "ScrollingConstraints.h"
#include "Settings.h"
#include "Styleable.h"
#include "TextBoxPainter.h"
#include "TransformState.h"
#include <wtf/NeverDestroyed.h>
#if ASSERT_ENABLED
#include <wtf/SetForScope.h>
#endif
#include <wtf/TZoneMallocInlines.h>

#if PLATFORM(IOS_FAMILY)
#include <wtf/RuntimeApplicationChecks.h>
#endif

namespace WebCore {

using namespace HTMLNames;

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderBoxModelObject);

// The HashMap for storing continuation pointers.
// An inline can be split with blocks occuring in between the inline content.
// When this occurs we need a pointer to the next object. We can basically be
// split into a sequence of inlines and blocks. The continuation will either be
// an anonymous block (that houses other blocks) or it will be an inline flow.
// <b><i><p>Hello</p></i></b>. In this example the <i> will have a block as
// its continuation but the <b> will just have an inline as its continuation.
RenderBoxModelObject::ContinuationChainNode::ContinuationChainNode(RenderBoxModelObject& renderer)
    : renderer(renderer)
{
}

RenderBoxModelObject::ContinuationChainNode::~ContinuationChainNode()
{
    if (next) {
        ASSERT(previous);
        ASSERT(next->previous == this);
        next->previous = previous;
    }
    if (previous) {
        ASSERT(previous->next == this);
        previous->next = next;
    }
}

void RenderBoxModelObject::ContinuationChainNode::insertAfter(ContinuationChainNode& after)
{
    ASSERT(!previous);
    ASSERT(!next);
    if ((next = after.next)) {
        ASSERT(next->previous == &after);
        next->previous = this;
    }
    previous = &after;
    after.next = this;
}

using ContinuationChainNodeMap = SingleThreadWeakHashMap<const RenderBoxModelObject, std::unique_ptr<RenderBoxModelObject::ContinuationChainNode>>;

static ContinuationChainNodeMap& continuationChainNodeMap()
{
    static NeverDestroyed<ContinuationChainNodeMap> map;
    return map;
}

using FirstLetterRemainingTextMap = SingleThreadWeakHashMap<const RenderBoxModelObject, SingleThreadWeakPtr<RenderTextFragment>>;

static FirstLetterRemainingTextMap& firstLetterRemainingTextMap()
{
    static NeverDestroyed<FirstLetterRemainingTextMap> map;
    return map;
}

void RenderBoxModelObject::styleWillChange(StyleDifference diff, const RenderStyle& newStyle)
{
    const RenderStyle* oldStyle = hasInitializedStyle() ? &style() : nullptr;

    if (Style::AnchorPositionEvaluator::isAnchor(newStyle))
        view().registerAnchor(*this);
    else if (oldStyle && Style::AnchorPositionEvaluator::isAnchor(*oldStyle))
        view().unregisterAnchor(*this);

    RenderLayerModelObject::styleWillChange(diff, newStyle);
}

void RenderBoxModelObject::setSelectionState(HighlightState state)
{
    if (state == HighlightState::Inside && selectionState() != HighlightState::None)
        return;

    if ((state == HighlightState::Start && selectionState() == HighlightState::End)
        || (state == HighlightState::End && selectionState() == HighlightState::Start))
        RenderLayerModelObject::setSelectionState(HighlightState::Both);
    else
        RenderLayerModelObject::setSelectionState(state);

    // FIXME: We should consider whether it is OK propagating to ancestor RenderInlines.
    // This is a workaround for http://webkit.org/b/32123
    // The containing block can be null in case of an orphaned tree.
    RenderBlock* containingBlock = this->containingBlock();
    if (containingBlock && !containingBlock->isRenderView())
        containingBlock->setSelectionState(state);
}

void RenderBoxModelObject::contentChanged(ContentChangeType changeType)
{
    if (!hasLayer())
        return;

    layer()->contentChanged(changeType);
}

bool RenderBoxModelObject::hasAcceleratedCompositing() const
{
    return view().compositor().hasAcceleratedCompositing();
}

RenderBoxModelObject::RenderBoxModelObject(Type type, Element& element, RenderStyle&& style, OptionSet<TypeFlag> baseTypeFlags, TypeSpecificFlags typeSpecificFlags)
    : RenderLayerModelObject(type, element, WTFMove(style), baseTypeFlags | TypeFlag::IsBoxModelObject, typeSpecificFlags)
{
    ASSERT(isRenderBoxModelObject());
}

RenderBoxModelObject::RenderBoxModelObject(Type type, Document& document, RenderStyle&& style, OptionSet<TypeFlag> baseTypeFlags, TypeSpecificFlags typeSpecificFlags)
    : RenderLayerModelObject(type, document, WTFMove(style), baseTypeFlags | TypeFlag::IsBoxModelObject, typeSpecificFlags)
{
    ASSERT(isRenderBoxModelObject());
}

RenderBoxModelObject::~RenderBoxModelObject()
{
    // Do not add any code here. Add it to willBeDestroyed() instead.
    ASSERT(!continuation());
}

void RenderBoxModelObject::willBeDestroyed()
{
    if (!renderTreeBeingDestroyed())
        view().imageQualityController().rendererWillBeDestroyed(*this);

    RenderLayerModelObject::willBeDestroyed();
}

bool RenderBoxModelObject::hasVisibleBoxDecorationStyle() const
{
    return hasBackground() || style().hasVisibleBorderDecoration() || style().hasUsedAppearance() || style().hasBoxShadow();
}

void RenderBoxModelObject::updateFromStyle()
{
    RenderLayerModelObject::updateFromStyle();

    // Set the appropriate bits for a box model object.  Since all bits are cleared in styleWillChange,
    // we only check for bits that could possibly be set to true.
    const auto& styleToUse = style();
    setHasVisibleBoxDecorations(hasVisibleBoxDecorationStyle());
    setInline(styleToUse.isDisplayInlineType());
    setPositionState(styleToUse.position());
    setHorizontalWritingMode(styleToUse.writingMode().isHorizontal());
    setPaintContainmentApplies(shouldApplyPaintContainment());
    if (writingMode().isBlockFlipped())
        view().frameView().setHasFlippedBlockRenderers(true);
}

static LayoutSize accumulateInFlowPositionOffsets(const RenderBoxModelObject& child)
{
    if (!child.isAnonymousBlock() || !child.isInFlowPositioned())
        return LayoutSize();
    LayoutSize offset;
    for (RenderElement* parent = downcast<RenderBlock>(child).inlineContinuation(); parent; parent = parent->parent()) {
        auto* parentRenderInline = dynamicDowncast<RenderInline>(*parent);
        if (!parentRenderInline)
            break;
        if (parent->isInFlowPositioned())
            offset += parentRenderInline->offsetForInFlowPosition();
    }
    return offset;
}
    
static inline bool isOutOfFlowPositionedWithImplicitHeight(const RenderBoxModelObject& child)
{
    return child.isOutOfFlowPositioned() && !child.style().logicalTop().isAuto() && !child.style().logicalBottom().isAuto();
}
    
RenderBlock* RenderBoxModelObject::containingBlockForAutoHeightDetectionGeneric(const auto& logicalHeight) const
{
    // For percentage heights: The percentage is calculated with respect to the
    // height of the generated box's containing block. If the height of the
    // containing block is not specified explicitly (i.e., it depends on content
    // height), and this element is not absolutely positioned, the used height is
    // calculated as if 'auto' was specified.
    if (!logicalHeight.isPercentOrCalculated() || isOutOfFlowPositioned())
        return nullptr;

    // Anonymous block boxes are ignored when resolving percentage values that
    // would refer to it: the closest non-anonymous ancestor box is used instead.
    auto* cb = containingBlock();
    while (cb && cb->isAnonymousForPercentageResolution() && !is<RenderView>(cb))
        cb = cb->containingBlock();
    if (!cb)
        return nullptr;

    // Matching RenderBox::percentageLogicalHeightIsResolvable() by
    // ignoring table cell's attribute value, where it says that table cells
    // violate what the CSS spec says to do with heights. Basically we don't care
    // if the cell specified a height or not.
    if (cb->isRenderTableCell())
        return nullptr;

    // Match RenderBox::availableLogicalHeightUsing by special casing the layout
    // view. The available height is taken from the frame.
    if (cb->isRenderView())
        return nullptr;

    if (isOutOfFlowPositionedWithImplicitHeight(*cb))
        return nullptr;

    return cb;
}

RenderBlock* RenderBoxModelObject::containingBlockForAutoHeightDetection(const Style::PreferredSize& logicalHeight) const
{
    return containingBlockForAutoHeightDetectionGeneric(logicalHeight);
}

RenderBlock* RenderBoxModelObject::containingBlockForAutoHeightDetection(const Style::MinimumSize& logicalHeight) const
{
    return containingBlockForAutoHeightDetectionGeneric(logicalHeight);
}

RenderBlock* RenderBoxModelObject::containingBlockForAutoHeightDetection(const Style::MaximumSize& logicalHeight) const
{
    return containingBlockForAutoHeightDetectionGeneric(logicalHeight);
}

DecodingMode RenderBoxModelObject::decodingModeForImageDraw(const Image& image, const PaintInfo& paintInfo) const
{
    // Some document types force synchronous decoding.
    if (document().isImageDocument())
        return DecodingMode::Synchronous;

    // A PaintBehavior may force synchronous decoding.
    if (paintInfo.paintBehavior.contains(PaintBehavior::Snapshotting))
        return DecodingMode::Synchronous;


    auto* bitmapImage = dynamicDowncast<BitmapImage>(image);
    if (!bitmapImage)
        return DecodingMode::Synchronous;

    auto defaultDecodingMode = [&]() -> DecodingMode {
        if (paintInfo.paintBehavior.contains(PaintBehavior::ForceSynchronousImageDecode))
            return DecodingMode::Synchronous;

        // First tile paint.
        if (paintInfo.paintBehavior.contains(PaintBehavior::DefaultAsynchronousImageDecode)) {
            // No image has been painted in this element yet and it should not flicker with previous painting.
            auto observer = bitmapImage->imageObserver();
            bool mayOverlapOtherClients = observer && observer->numberOfClients() > 1 && bitmapImage->currentFrameDecodingOptions().decodingMode() == DecodingMode::Asynchronous;
            if (element() && !element()->hasEverPaintedImages() && !mayOverlapOtherClients)
                return DecodingMode::Asynchronous;
        }

        // FIXME: Calling isVisibleInViewport() is not cheap. Find a way to make this faster.
        return isVisibleInViewport() ? DecodingMode::Synchronous : DecodingMode::Asynchronous;
    };

    if (RefPtr imgElement = dynamicDowncast<HTMLImageElement>(element())) {
        // <img decoding="sync"> forces synchronous decoding.
        if (imgElement->decodingMode() == DecodingMode::Synchronous)
            return DecodingMode::Synchronous;

        // <img decoding="async"> forces asynchronous decoding but make sure this
        // will not cause flickering.
        if (imgElement->decodingMode() == DecodingMode::Asynchronous) {
            if (bitmapImage->isAsyncDecodingEnabledForTesting() || bitmapImage->isAnimated())
                return DecodingMode::Asynchronous;

            // Choose a decodingMode such that the image does not flicker.
            return defaultDecodingMode();
        }
    }

    // isAsyncDecodingEnabledForTesting() forces async image decoding regardless of the size.
    if (bitmapImage->isAsyncDecodingEnabledForTesting())
        return DecodingMode::Asynchronous;

    // Animated image case.
    if (bitmapImage->isAnimated()) {
        if (bitmapImage->isLargeForDecoding() && settings().animatedImageAsyncDecodingEnabled())
            return DecodingMode::Asynchronous;
        return DecodingMode::Synchronous;
    }

    // Large image case.
    if (!(bitmapImage->isLargeForDecoding() && settings().largeImageAsyncDecodingEnabled()))
        return DecodingMode::Synchronous;

    // Choose a decodingMode such that the image does not flicker.
    return defaultDecodingMode();
}

LayoutSize RenderBoxModelObject::relativePositionOffset() const
{
    auto* containingBlock = this->containingBlock();

    auto& style = this->style();
    auto& left = style.left();
    auto& right = style.right();
    auto& top = style.top();
    auto& bottom = style.bottom();

    auto offset = accumulateInFlowPositionOffsets(*this);
    auto topFixed = top.tryFixed();
    auto leftFixed = left.tryFixed();
    if (topFixed && leftFixed && bottom.isAuto() && right.isAuto() && containingBlock->writingMode().isAnyLeftToRight()) {
        offset.expand(leftFixed->value, topFixed->value);
        return offset;
    }

    // Objects that shrink to avoid floats normally use available line width when computing containing block width. However
    // in the case of relative positioning using percentages, we can't do this. The offset should always be resolved using the
    // available width of the containing block. Therefore we don't use containingBlockLogicalWidthForContent() here, but instead explicitly
    // call availableWidth on our containing block.
    if (!left.isAuto() || !right.isAuto()) {
        auto availableWidth = [&] {
            auto* renderBox = dynamicDowncast<RenderBox>(*this);
            if (!renderBox || !renderBox->isGridItem())
                return containingBlock->contentBoxWidth();
            // For grid items the containing block is the grid area, so offsets should be resolved against that.
            auto containingBlockContentWidth = renderBox->gridAreaContentWidth(containingBlock->writingMode());
            if (!containingBlockContentWidth || !*containingBlockContentWidth) {
                ASSERT_NOT_REACHED();
                return containingBlock->contentBoxWidth();
            }
            return **containingBlockContentWidth;
        };
        if (!left.isAuto()) {
            if (!right.isAuto() && !containingBlock->writingMode().isAnyLeftToRight())
                offset.setWidth(-Style::evaluate(right, !right.isFixed() ? availableWidth() : 0_lu));
            else
                offset.expand(Style::evaluate(left, !left.isFixed() ? availableWidth() : 0_lu), 0_lu);
        } else if (!right.isAuto())
            offset.expand(-Style::evaluate(right, !right.isFixed() ? availableWidth() : 0_lu), 0_lu);
    }

    // If the containing block of a relatively positioned element does not
    // specify a height, a percentage top or bottom offset should be resolved as
    // auto. An exception to this is if the containing block has the WinIE quirk
    // where <html> and <body> assume the size of the viewport. In this case,
    // calculate the percent offset based on this height.
    // See <https://bugs.webkit.org/show_bug.cgi?id=26396>.
    // Another exception is a grid item, as the containing block is the grid area:
    // https://drafts.csswg.org/css-grid/#grid-item-sizing
    if (top.isAuto() && bottom.isAuto())
        return offset;

    auto containingBlockHasDefiniteHeight = !containingBlock->hasAutoHeightOrContainingBlockWithAutoHeight() || containingBlock->stretchesToViewport();
    auto availableHeight = [&] {
        auto* renderBox = dynamicDowncast<RenderBox>(*this);
        if (!renderBox || !renderBox->isGridItem())
            return containingBlock->contentBoxHeight();
        // For grid items the containing block is the grid area, so offsets should be resolved against that.
        auto containingBlockContentHeight = renderBox->gridAreaContentHeight(containingBlock->style().writingMode());
        if (!containingBlockContentHeight || !*containingBlockContentHeight) {
            ASSERT_NOT_REACHED();
            return containingBlock->contentBoxHeight();
        }
        return **containingBlockContentHeight;
    };
    if (!top.isAuto() && (!top.isPercentOrCalculated() || containingBlockHasDefiniteHeight)) {
        // FIXME: The computation of the available height is repeated later for "bottom".
        // We could refactor this and move it to some common code for both ifs, however moving it outside of the ifs
        // is not possible as it'd cause performance regressions.
        offset.expand(0_lu, Style::evaluate(top, !top.isFixed() ? availableHeight() : 0_lu));
    } else if (!bottom.isAuto() && (!bottom.isPercentOrCalculated() || containingBlockHasDefiniteHeight)) {
        // FIXME: Check comment above for "top", it applies here too.
        offset.expand(0_lu, -Style::evaluate(bottom, !bottom.isFixed() ? availableHeight() : 0_lu));
    }
    return offset;
}

LayoutPoint RenderBoxModelObject::adjustedPositionRelativeToOffsetParent(const LayoutPoint& startPoint) const
{
    // If the element is the HTML body element or doesn't have a parent
    // return 0 and stop this algorithm.
    if (isBody() || !parent())
        return LayoutPoint();

    LayoutPoint referencePoint = startPoint;
    
    // If the offsetParent of the element is null, or is the HTML body element,
    // return the distance between the canvas origin and the left border edge 
    // of the element and stop this algorithm.
    if (const RenderBoxModelObject* offsetParent = this->offsetParent()) {
        if (auto* renderBox = dynamicDowncast<RenderBox>(*offsetParent); renderBox && !offsetParent->isBody() && !is<RenderTable>(*offsetParent))
            referencePoint.move(-renderBox->borderLeft(), -renderBox->borderTop());
        else if (auto* renderInline = dynamicDowncast<RenderInline>(*offsetParent)) {
            // Inside inline formatting context both inflow and statically positioned out-of-flow boxes are positioned relative to the root block container.
            auto topLeft = renderInline->firstInlineBoxTopLeft();
            if (isOutOfFlowPositioned()) {
                auto& outOfFlowStyle = style();
                ASSERT(containingBlock());
                auto isHorizontalWritingMode = containingBlock() ? containingBlock()->writingMode().isHorizontal() : true;
                if (!outOfFlowStyle.hasStaticInlinePosition(isHorizontalWritingMode))
                    topLeft.setX(LayoutUnit { });
                if (!outOfFlowStyle.hasStaticBlockPosition(isHorizontalWritingMode))
                    topLeft.setY(LayoutUnit { });
            }
            referencePoint.move(-topLeft.x(), -topLeft.y());
        }

        if (!isOutOfFlowPositioned() || enclosingFragmentedFlow()) {
            if (isRelativelyPositioned())
                referencePoint.move(relativePositionOffset());
            else if (isStickilyPositioned())
                referencePoint.move(stickyPositionOffset());
            
            // CSS regions specification says that region flows should return the body element as their offsetParent.
            // Since we will bypass the body’s renderer anyway, just end the loop if we encounter a region flow (named flow thread).
            // See http://dev.w3.org/csswg/css-regions/#cssomview-offset-attributes
            auto* ancestor = parent();
            while (ancestor != offsetParent) {
                // FIXME: What are we supposed to do inside SVG content?
                
                if (auto* renderMultiColumnFlow = dynamicDowncast<RenderMultiColumnFlow>(*ancestor)) {
                    // We need to apply a translation based off what region we are inside.
                    if (auto* fragment = renderMultiColumnFlow->physicalTranslationFromFlowToFragment(referencePoint))
                        referencePoint.moveBy(fragment->topLeftLocation());
                } else if (!isOutOfFlowPositioned()) {
                    if (auto* renderBox = dynamicDowncast<RenderBox>(*ancestor); renderBox && !is<RenderTableRow>(*ancestor))
                        referencePoint.moveBy(renderBox->topLeftLocation());
                }
                
                ancestor = ancestor->parent();
            }
            
            if (auto* renderBox = dynamicDowncast<RenderBox>(*offsetParent); renderBox && offsetParent->isBody() && !offsetParent->isPositioned())
                referencePoint.moveBy(renderBox->topLeftLocation());
        }
    }

    return referencePoint;
}

std::pair<const RenderBox&, const RenderLayer*> RenderBoxModelObject::enclosingClippingBoxForStickyPosition() const
{
    ASSERT(isStickilyPositioned());
    RenderLayer* clipLayer = hasLayer() ? layer()->enclosingOverflowClipLayer(ExcludeSelf) : nullptr;
    const RenderBox& box = clipLayer ? downcast<RenderBox>(clipLayer->renderer()) : view();
    return { box, clipLayer };
}

void RenderBoxModelObject::computeStickyPositionConstraints(StickyPositionViewportConstraints& constraints, const FloatRect& constrainingRect) const
{
    constraints.setConstrainingRectAtLastLayout(constrainingRect);

    // Do not use anonymous containing blocks to determine sticky constraints. We want the size
    // of the first true containing block, because that is what imposes the limitation on the
    // movement of stickily positioned items.
    RenderBlock* containingBlock = this->containingBlock();
    while (containingBlock && (!is<RenderBlock>(*containingBlock) || containingBlock->isAnonymousBlock()))
        containingBlock = containingBlock->containingBlock();
    ASSERT(containingBlock);

    auto [enclosingClippingBox, enclosingClippingLayer] = enclosingClippingBoxForStickyPosition();

    LayoutRect containerContentRect;
    if (!enclosingClippingLayer || (containingBlock != &enclosingClippingBox)) {
        // In this case either the scrolling element is the view or there is another containing block in
        // the hierarchy between this stickily positioned item and its scrolling ancestor. In both cases,
        // we use the content box rectangle of the containing block, which is what should constrain the
        // movement.
        containerContentRect = containingBlock->computedCSSContentBoxRect();
    } else {
        containerContentRect = containingBlock->layoutOverflowRect();
        containerContentRect.contract(LayoutBoxExtent {
            containingBlock->computedCSSPaddingTop(), containingBlock->computedCSSPaddingRight(),
            containingBlock->computedCSSPaddingBottom(), containingBlock->computedCSSPaddingLeft() });
    }

    LayoutUnit maxWidth = containingBlock->contentBoxLogicalWidth();

    // Sticky positioned element ignore any override logical width on the containing block (as they don't call
    // containingBlockLogicalWidthForContent). It's unclear whether this is totally fine.
    LayoutBoxExtent minMargin(
        Style::evaluateMinimum(style().marginTop(), maxWidth),
        Style::evaluateMinimum(style().marginRight(), maxWidth),
        Style::evaluateMinimum(style().marginBottom(), maxWidth),
        Style::evaluateMinimum(style().marginLeft(), maxWidth)
    );

    // Compute the container-relative area within which the sticky element is allowed to move.
    containerContentRect.contract(minMargin);

    // Finally compute container rect relative to the scrolling ancestor. We pass an empty
    // mode here, because sticky positioning should ignore transforms.
    FloatRect containerRectRelativeToScrollingAncestor = containingBlock->localToContainerQuad(FloatRect(containerContentRect), &enclosingClippingBox, { } /* ignore transforms */).boundingBox();
    if (enclosingClippingLayer) {
        FloatPoint containerLocationRelativeToScrollingAncestor = containerRectRelativeToScrollingAncestor.location() -
            FloatSize(enclosingClippingBox.borderLeft() + enclosingClippingBox.paddingLeft(),
            enclosingClippingBox.borderTop() + enclosingClippingBox.paddingTop());
        if (&enclosingClippingBox != containingBlock) {
            if (auto* scrollableArea = enclosingClippingLayer->scrollableArea())
                containerLocationRelativeToScrollingAncestor += scrollableArea->scrollOffset();
        }
        containerRectRelativeToScrollingAncestor.setLocation(containerLocationRelativeToScrollingAncestor);
    }
    constraints.setContainingBlockRect(containerRectRelativeToScrollingAncestor);

    // Now compute the sticky box rect, also relative to the scrolling ancestor.
    LayoutRect stickyBoxRect = frameRectForStickyPositioning();

    // Ideally, it would be possible to call this->localToContainerQuad to determine the frame
    // rectangle in the coordinate system of the scrolling ancestor, but localToContainerQuad
    // itself depends on sticky positioning! Instead, start from the parent but first adjusting
    // the rectangle for the writing mode of this stickily-positioned element. We also pass an
    // empty mode here because sticky positioning should ignore transforms.
    //
    // FIXME: It would also be nice to not have to call localToContainerQuad again since we
    // have already done a similar call to move from the containing block to the scrolling
    // ancestor above, but localToContainerQuad takes care of a lot of complex situations
    // involving inlines, tables, and transformations.
    if (CheckedPtr parentBox = dynamicDowncast<RenderBox>(*parent()))
        parentBox->flipForWritingMode(stickyBoxRect);
    auto stickyBoxRelativeToScrollingAncestor = parent()->localToContainerQuad(FloatRect(stickyBoxRect), &enclosingClippingBox, { } /* ignore transforms */).boundingBox();

    if (enclosingClippingLayer) {
        stickyBoxRelativeToScrollingAncestor.move(-FloatSize(enclosingClippingBox.borderLeft() + enclosingClippingBox.paddingLeft(),
            enclosingClippingBox.borderTop() + enclosingClippingBox.paddingTop()));

        if (&enclosingClippingBox != parent()) {
            if (auto* scrollableArea = enclosingClippingLayer->scrollableArea())
                stickyBoxRelativeToScrollingAncestor.moveBy(scrollableArea->scrollOffset());
        }
    }
    constraints.setStickyBoxRect(stickyBoxRelativeToScrollingAncestor);

    if (!style().left().isAuto()) {
        constraints.setLeftOffset(Style::evaluate(style().left(), constrainingRect.width()));
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeLeft);
    }

    if (!style().right().isAuto()) {
        constraints.setRightOffset(Style::evaluate(style().right(), constrainingRect.width()));
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeRight);
    }

    if (!style().top().isAuto()) {
        constraints.setTopOffset(Style::evaluate(style().top(), constrainingRect.height()));
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeTop);
    }

    if (!style().bottom().isAuto()) {
        constraints.setBottomOffset(Style::evaluate(style().bottom(), constrainingRect.height()));
        constraints.addAnchorEdge(ViewportConstraints::AnchorEdgeBottom);
    }
}

FloatRect RenderBoxModelObject::constrainingRectForStickyPosition() const
{
    RenderLayer* enclosingClippingLayer = hasLayer() ? layer()->enclosingOverflowClipLayer(ExcludeSelf) : nullptr;

    if (enclosingClippingLayer) {
        RenderBox& enclosingClippingBox = downcast<RenderBox>(enclosingClippingLayer->renderer());
        LayoutRect clipRect = enclosingClippingBox.overflowClipRect({ });
        clipRect.contract(LayoutSize(enclosingClippingBox.paddingLeft() + enclosingClippingBox.paddingRight(),
            enclosingClippingBox.paddingTop() + enclosingClippingBox.paddingBottom()));

        FloatRect constrainingRect = enclosingClippingBox.localToContainerQuad(FloatRect(clipRect), &view()).boundingBox();

        auto* scrollableArea = enclosingClippingLayer->scrollableArea();
        FloatPoint scrollOffset;
        if (scrollableArea)
            scrollOffset = FloatPoint() + scrollableArea->scrollOffset();

        float scrollbarOffset = 0;
        if (scrollableArea && enclosingClippingBox.hasLayer() && enclosingClippingBox.shouldPlaceVerticalScrollbarOnLeft())
            scrollbarOffset = scrollableArea->verticalScrollbarWidth(OverlayScrollbarSizeRelevancy::IgnoreOverlayScrollbarSize, isHorizontalWritingMode());

        constrainingRect.setLocation(FloatPoint(scrollOffset.x() + scrollbarOffset, scrollOffset.y()));
        return constrainingRect;
    }
    
    return view().frameView().rectForFixedPositionLayout();
}

LayoutSize RenderBoxModelObject::stickyPositionOffset() const
{
    FloatRect constrainingRect = constrainingRectForStickyPosition();
    StickyPositionViewportConstraints constraints;
    computeStickyPositionConstraints(constraints, constrainingRect);
    
    // The sticky offset is physical, so we can just return the delta computed in absolute coords (though it may be wrong with transforms).
    return LayoutSize(constraints.computeStickyOffset(constrainingRect));
}

LayoutSize RenderBoxModelObject::offsetForInFlowPosition() const
{
    if (isRelativelyPositioned())
        return relativePositionOffset();

    if (isStickilyPositioned())
        return stickyPositionOffset();

    return LayoutSize();
}

LayoutUnit RenderBoxModelObject::offsetLeft() const
{
    // Note that RenderInline and RenderBox override this to pass a different
    // startPoint to adjustedPositionRelativeToOffsetParent.
    return adjustedPositionRelativeToOffsetParent(LayoutPoint()).x();
}

LayoutUnit RenderBoxModelObject::offsetTop() const
{
    // Note that RenderInline and RenderBox override this to pass a different
    // startPoint to adjustedPositionRelativeToOffsetParent.
    return adjustedPositionRelativeToOffsetParent(LayoutPoint()).y();
}

InterpolationQuality RenderBoxModelObject::chooseInterpolationQuality(GraphicsContext& context, Image& image, const void* layer, const LayoutSize& size) const
{
    return view().imageQualityController().chooseInterpolationQuality(context, const_cast<RenderBoxModelObject*>(this), image, layer, size);
}

void RenderBoxModelObject::paintMaskForTextFillBox(GraphicsContext& context, const FloatRect& paintRect, const InlineIterator::InlineBoxIterator& inlineBox, const LayoutRect& scrolledPaintRect)
{
    // Now add the text to the clip. We do this by painting using a special paint phase that signals to
    // the painter it should just modify the clip.
    PaintInfo maskInfo(context, LayoutRect { paintRect }, PaintPhase::TextClip, PaintBehavior::ForceBlackText);
    if (inlineBox) {
        auto paintOffset = scrolledPaintRect.location() - toLayoutSize(LayoutPoint(inlineBox->visualRectIgnoringBlockDirection().location()));

        for (auto box = inlineBox->firstLeafBox(), end = inlineBox->endLeafBox(); box != end; box.traverseLineRightwardOnLine()) {
            if (!box->isText())
                continue;
            TextBoxPainter { box->modernPath().inlineContent(), box->modernPath().box(), box->modernPath().box().style(), maskInfo, paintOffset }.paint();
        }
        return;
    }

    auto* renderBox = dynamicDowncast<RenderBox>(*this);
    auto localOffset = renderBox ? renderBox->locationOffset() : LayoutSize();
    paint(maskInfo, scrolledPaintRect.location() - localOffset);
}

static inline LayoutUnit resolveWidthForRatio(LayoutUnit height, const LayoutSize& intrinsicRatio)
{
    return height * intrinsicRatio.width() / intrinsicRatio.height();
}

static inline LayoutUnit resolveHeightForRatio(LayoutUnit width, const LayoutSize& intrinsicRatio)
{
    return width * intrinsicRatio.height() / intrinsicRatio.width();
}

static inline LayoutSize resolveAgainstIntrinsicWidthOrHeightAndRatio(const LayoutSize& size, const LayoutSize& intrinsicRatio, LayoutUnit useWidth, LayoutUnit useHeight)
{
    if (intrinsicRatio.isEmpty()) {
        if (useWidth)
            return LayoutSize(useWidth, size.height());
        return LayoutSize(size.width(), useHeight);
    }

    if (useWidth)
        return LayoutSize(useWidth, resolveHeightForRatio(useWidth, intrinsicRatio));
    return LayoutSize(resolveWidthForRatio(useHeight, intrinsicRatio), useHeight);
}

static inline LayoutSize resolveAgainstIntrinsicRatio(const LayoutSize& size, const LayoutSize& intrinsicRatio)
{
    // Two possible solutions: (size.width(), solutionHeight) or (solutionWidth, size.height())
    // "... must be assumed to be the largest dimensions..." = easiest answer: the rect with the largest surface area.

    LayoutUnit solutionWidth = resolveWidthForRatio(size.height(), intrinsicRatio);
    LayoutUnit solutionHeight = resolveHeightForRatio(size.width(), intrinsicRatio);
    if (solutionWidth <= size.width()) {
        if (solutionHeight <= size.height()) {
            // If both solutions fit, choose the one covering the larger area.
            LayoutUnit areaOne = solutionWidth * size.height();
            LayoutUnit areaTwo = size.width() * solutionHeight;
            if (areaOne < areaTwo)
                return LayoutSize(size.width(), solutionHeight);
            return LayoutSize(solutionWidth, size.height());
        }

        // Only the first solution fits.
        return LayoutSize(solutionWidth, size.height());
    }

    // Only the second solution fits, assert that.
    ASSERT(solutionHeight <= size.height());
    return LayoutSize(size.width(), solutionHeight);
}

LayoutSize RenderBoxModelObject::calculateImageIntrinsicDimensions(StyleImage* image, const LayoutSize& positioningAreaSize, ScaleByUsedZoom scaleByUsedZoom) const
{
    // A generated image without a fixed size, will always return the container size as intrinsic size.
    if (!image->imageHasNaturalDimensions())
        return LayoutSize(positioningAreaSize.width(), positioningAreaSize.height());

    Length intrinsicWidth;
    Length intrinsicHeight;
    FloatSize intrinsicRatio;
    image->computeIntrinsicDimensions(this, intrinsicWidth, intrinsicHeight, intrinsicRatio);

    ASSERT(!intrinsicWidth.isPercentOrCalculated());
    ASSERT(!intrinsicHeight.isPercentOrCalculated());

    LayoutSize resolvedSize(intrinsicWidth.value(), intrinsicHeight.value());
    LayoutSize minimumSize(resolvedSize.width() > 0 ? 1 : 0, resolvedSize.height() > 0 ? 1 : 0);

    if (scaleByUsedZoom == ScaleByUsedZoom::Yes)
        resolvedSize.scale(style().usedZoom());
    resolvedSize.clampToMinimumSize(minimumSize);

    if (!resolvedSize.isEmpty())
        return resolvedSize;

    // If the image has one of either an intrinsic width or an intrinsic height:
    // * and an intrinsic aspect ratio, then the missing dimension is calculated from the given dimension and the ratio.
    // * and no intrinsic aspect ratio, then the missing dimension is assumed to be the size of the rectangle that
    //   establishes the coordinate system for the 'background-position' property.
    if (resolvedSize.width() > 0 || resolvedSize.height() > 0)
        return resolveAgainstIntrinsicWidthOrHeightAndRatio(positioningAreaSize, LayoutSize(intrinsicRatio), resolvedSize.width(), resolvedSize.height());

    // If the image has no intrinsic dimensions and has an intrinsic ratio the dimensions must be assumed to be the
    // largest dimensions at that ratio such that neither dimension exceeds the dimensions of the rectangle that
    // establishes the coordinate system for the 'background-position' property.
    if (!intrinsicRatio.isEmpty())
        return resolveAgainstIntrinsicRatio(positioningAreaSize, LayoutSize(intrinsicRatio));

    // If the image has no intrinsic ratio either, then the dimensions must be assumed to be the rectangle that
    // establishes the coordinate system for the 'background-position' property.
    return positioningAreaSize;
}

bool RenderBoxModelObject::fixedBackgroundPaintsInLocalCoordinates() const
{
    if (!isDocumentElementRenderer())
        return false;

    if (view().frameView().paintBehavior().contains(PaintBehavior::FlattenCompositingLayers))
        return false;

    RenderLayer* rootLayer = view().layer();
    if (!rootLayer || !rootLayer->isComposited())
        return false;

    return rootLayer->backing()->backgroundLayerPaintsFixedRootBackground();
}

bool RenderBoxModelObject::borderObscuresBackgroundEdge(const FloatSize& contextScale) const
{
    auto edges = borderEdges(style(), document().deviceScaleFactor());

    for (auto side : allBoxSides) {
        auto& currEdge = edges.at(side);
        // FIXME: for vertical text
        float axisScale = (side == BoxSide::Top || side == BoxSide::Bottom) ? contextScale.height() : contextScale.width();
        if (!currEdge.obscuresBackgroundEdge(axisScale))
            return false;
    }

    return true;
}

bool RenderBoxModelObject::borderObscuresBackground() const
{
    if (!style().hasBorder())
        return false;

    // Bail if we have any border-image for now. We could look at the image alpha to improve this.
    if (style().borderImage().image())
        return false;

    auto edges = borderEdges(style(), document().deviceScaleFactor());

    for (auto side : allBoxSides) {
        if (!edges.at(side).obscuresBackground())
            return false;
    }

    return true;
}

BorderShape RenderBoxModelObject::borderShapeForContentClipping(const LayoutRect& borderBoxRect, RectEdges<bool> closedEdges) const
{
    auto borderWidths = this->borderWidths();
    auto padding = this->padding();

    auto contentBoxInsets = RectEdges<LayoutUnit> {
        borderWidths.top() + padding.top(),
        borderWidths.right() + padding.right(),
        borderWidths.bottom() + padding.bottom(),
        borderWidths.left() + padding.left(),
    };

    return BorderShape::shapeForBorderRect(style(), borderBoxRect, contentBoxInsets, closedEdges);
}

LayoutUnit RenderBoxModelObject::containingBlockLogicalWidthForContent() const
{
    if (auto* containingBlock = this->containingBlock())
        return containingBlock->contentBoxLogicalWidth();
    return { };
}

RenderBoxModelObject* RenderBoxModelObject::continuation() const
{
    if (!hasContinuationChainNode())
        return nullptr;

    auto& continuationChainNode = *continuationChainNodeMap().get(*this);
    if (!continuationChainNode.next)
        return nullptr;
    return continuationChainNode.next->renderer.get();
}

RenderInline* RenderBoxModelObject::inlineContinuation() const
{
    if (!hasContinuationChainNode())
        return nullptr;

    for (auto* next = continuationChainNodeMap().get(*this)->next; next; next = next->next) {
        if (auto* renderInline = dynamicDowncast<RenderInline>(*next->renderer))
            return renderInline;
    }
    return nullptr;
}

void RenderBoxModelObject::forRendererAndContinuations(RenderBoxModelObject& renderer, const std::function<void(RenderBoxModelObject&)>& function)
{
    function(renderer);
    if (!renderer.hasContinuationChainNode())
        return;

    for (auto* next = continuationChainNodeMap().get(renderer)->next; next; next = next->next) {
        if (!next->renderer)
            continue;
        function(*next->renderer);
    }
}

RenderBoxModelObject::ContinuationChainNode* RenderBoxModelObject::continuationChainNode() const
{
    return continuationChainNodeMap().get(*this);
}

void RenderBoxModelObject::insertIntoContinuationChainAfter(RenderBoxModelObject& afterRenderer)
{
    ASSERT(isContinuation());
    ASSERT(!continuationChainNodeMap().contains(*this));

    auto& after = afterRenderer.ensureContinuationChainNode();
    ensureContinuationChainNode().insertAfter(after);
}

void RenderBoxModelObject::removeFromContinuationChain()
{
    ASSERT(hasContinuationChainNode());
    ASSERT(continuationChainNodeMap().contains(*this));
    setHasContinuationChainNode(false);
    continuationChainNodeMap().remove(*this);
}

auto RenderBoxModelObject::ensureContinuationChainNode() -> ContinuationChainNode&
{
    setHasContinuationChainNode(true);
    return *continuationChainNodeMap().ensure(*this, [&] {
        return makeUnique<ContinuationChainNode>(*this);
    }).iterator->value;
}

RenderTextFragment* RenderBoxModelObject::firstLetterRemainingText() const
{
    if (!isFirstLetter())
        return nullptr;
    return firstLetterRemainingTextMap().get(*this).get();
}

void RenderBoxModelObject::setFirstLetterRemainingText(RenderTextFragment& remainingText)
{
    ASSERT(isFirstLetter());
    firstLetterRemainingTextMap().set(*this, remainingText);
}

void RenderBoxModelObject::clearFirstLetterRemainingText()
{
    ASSERT(isFirstLetter());
    firstLetterRemainingTextMap().remove(*this);
}

void RenderBoxModelObject::mapAbsoluteToLocalPoint(OptionSet<MapCoordinatesMode> mode, TransformState& transformState) const
{
    RenderElement* container = this->container();
    if (!container)
        return;
    
    container->mapAbsoluteToLocalPoint(mode, transformState);

    LayoutSize containerOffset = offsetFromContainer(*container, LayoutPoint());

    pushOntoTransformState(transformState, mode, nullptr, container, containerOffset, false);
}

bool RenderBoxModelObject::hasRunningAcceleratedAnimations() const
{
    if (auto styleable = Styleable::fromRenderer(*this))
        return styleable->hasRunningAcceleratedAnimations();
    return false;
}

void RenderBoxModelObject::collectAbsoluteQuadsForContinuation(Vector<FloatQuad>& quads, bool* wasFixed) const
{
    ASSERT(continuation());
    for (auto* nextInContinuation = this->continuation(); nextInContinuation; nextInContinuation = nextInContinuation->continuation()) {
        if (auto blockBox = dynamicDowncast<RenderBlock>(*nextInContinuation); blockBox && blockBox->height() && blockBox->width()) {
            // For blocks inside inlines, we include margins so that we run right up to the inline boxes
            // above and below us (thus getting merged with them to form a single irregular shape).
            auto logicalRect = FloatRect { 0, -blockBox->collapsedMarginBefore(), blockBox->width(),
                blockBox->height() + blockBox->collapsedMarginBefore() + blockBox->collapsedMarginAfter() };
            nextInContinuation->absoluteQuadsIgnoringContinuation(logicalRect, quads, wasFixed);
            continue;
        }
        nextInContinuation->absoluteQuadsIgnoringContinuation({ }, quads, wasFixed);
    }
}

void RenderBoxModelObject::applyTransform(TransformationMatrix&, const RenderStyle&, const FloatRect&, OptionSet<RenderStyle::TransformOperationOption>) const
{
    // applyTransform() is only used through RenderLayer*, which only invokes this for RenderBox derived renderers, thus not for
    // RenderInline/RenderLineBreak - the other two renderers that inherit from RenderBoxModelObject.
    ASSERT_NOT_REACHED();
}

bool RenderBoxModelObject::requiresLayer() const
{
    return isDocumentElementRenderer() || isPositioned() || createsGroup() || hasTransformRelatedProperty() || hasHiddenBackface() || hasReflection() || requiresRenderingConsolidationForViewTransition() || isRenderViewTransitionCapture();
}

void RenderBoxModelObject::removeOutOfFlowBoxesIfNeededOnStyleChange(RenderBlock& delegateBlock, const RenderStyle& oldStyle, const RenderStyle& newStyle)
{
    auto wasContainingBlockForFixedContent = canContainFixedPositionObjects(&oldStyle);
    auto wasContainingBlockForAbsoluteContent = canContainAbsolutelyPositionedObjects(&oldStyle);
    auto isContainingBlockForFixedContent = canContainFixedPositionObjects(&newStyle);
    auto isContainingBlockForAbsoluteContent = canContainAbsolutelyPositionedObjects(&newStyle);

    // FIXME: If an inline becomes a containing block, but the delegate was already one (or vice-versa),
    // then we don't really need to remove the out-of-flows from the delegate only for them to be re-added
    // to the same spot. We would need to correctly mark for layout instead though.

    if ((wasContainingBlockForFixedContent && !isContainingBlockForFixedContent) || (wasContainingBlockForAbsoluteContent && !isContainingBlockForAbsoluteContent)) {
        // We are no longer the containing block for out-of-flow descendants.
        delegateBlock.removeOutOfFlowBoxes({ }, RenderBlock::ContainingBlockState::NewContainingBlock);
    }

    if (!wasContainingBlockForFixedContent && isContainingBlockForFixedContent) {
        // We are a new containing block for all out-of-flow boxes. Find first ancestor that has our fixed positioned boxes and remove them.
        // They will be inserted into our positioned objects list during their static position layout.
        if (CheckedPtr containingBlock = RenderObject::containingBlockForPositionType(PositionType::Fixed, *this))
            containingBlock->removeOutOfFlowBoxes(&delegateBlock,  RenderBlock::ContainingBlockState::NewContainingBlock);
    }

    if (!wasContainingBlockForAbsoluteContent && isContainingBlockForAbsoluteContent) {
        // We are a new containing block for absolute positioning.
        // Remove our absolutely positioned descendants from their current containing block.
        // They will be inserted into our positioned objects list during layout.
        if (CheckedPtr containingBlock = RenderObject::containingBlockForPositionType(PositionType::Absolute, *this))
            containingBlock->removeOutOfFlowBoxes(&delegateBlock,  RenderBlock::ContainingBlockState::NewContainingBlock);
    }
}

} // namespace WebCore
