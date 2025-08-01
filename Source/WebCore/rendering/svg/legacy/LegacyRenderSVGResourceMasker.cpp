/*
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
 * Copyright (C) 2022-2023 Apple Inc. All rights reserved.
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
 */

#include "config.h"
#include "LegacyRenderSVGResourceMasker.h"

#include "ContainerNodeInlines.h"
#include "Element.h"
#include "ElementChildIteratorInlines.h"
#include "FloatPoint.h"
#include "Image.h"
#include "IntRect.h"
#include "LegacyRenderSVGResourceMaskerInlines.h"
#include "SVGRenderStyle.h"
#include "SVGRenderingContext.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LegacyRenderSVGResourceMasker);

LegacyRenderSVGResourceMasker::LegacyRenderSVGResourceMasker(SVGMaskElement& element, RenderStyle&& style)
    : LegacyRenderSVGResourceContainer(Type::LegacySVGResourceMasker, element, WTFMove(style))
{
}

LegacyRenderSVGResourceMasker::~LegacyRenderSVGResourceMasker() = default;

void LegacyRenderSVGResourceMasker::removeAllClientsFromCache()
{
    m_maskContentBoundaries.fill(FloatRect { });
    m_masker.clear();
}

void LegacyRenderSVGResourceMasker::removeClientFromCache(RenderElement& client)
{
    m_masker.remove(client);
}

auto LegacyRenderSVGResourceMasker::applyResource(RenderElement& renderer, const RenderStyle&, GraphicsContext*& context, OptionSet<RenderSVGResourceMode> resourceMode) -> OptionSet<ApplyResult>
{
    ASSERT(context);
    ASSERT_UNUSED(resourceMode, !resourceMode);

    bool missingMaskerData = !m_masker.contains(renderer);
    if (missingMaskerData)
        m_masker.set(renderer, makeUnique<MaskerData>());

    MaskerData* maskerData = m_masker.get(renderer);
    AffineTransform absoluteTransform = SVGRenderingContext::calculateTransformationToOutermostCoordinateSystem(renderer);
    // FIXME: This needs to be bounding box and should not use repaint rect.
    // https://bugs.webkit.org/show_bug.cgi?id=278551
    FloatRect repaintRect = renderer.repaintRectInLocalCoordinates(RepaintRectCalculation::Accurate);

    // Ignore 2D rotation, as it doesn't affect the size of the mask.
    FloatSize scale(absoluteTransform.xScale(), absoluteTransform.yScale());

    // Determine scale factor for the mask. The size of intermediate ImageBuffers shouldn't be bigger than kMaxFilterSize.
    ImageBuffer::sizeNeedsClamping(repaintRect.size(), scale);

    if (!maskerData->maskImage && !repaintRect.isEmpty()) {
        auto maskColorSpace = DestinationColorSpace::SRGB();
        auto drawColorSpace = DestinationColorSpace::SRGB();

        if (style().svgStyle().colorInterpolation() == ColorInterpolation::LinearRGB) {
#if USE(CG) || USE(SKIA)
            maskColorSpace = DestinationColorSpace::LinearSRGB();
#endif
            drawColorSpace = DestinationColorSpace::LinearSRGB();
        }
        // FIXME (149470): This image buffer should not be unconditionally unaccelerated. Making it match the context breaks alpha masking, though.
        maskerData->maskImage = context->createScaledImageBuffer(repaintRect, scale, maskColorSpace, RenderingMode::Unaccelerated);
        if (!maskerData->maskImage)
            return { };

        if (!drawContentIntoMaskImage(maskerData, drawColorSpace, &renderer))
            maskerData->maskImage = nullptr;
    }

    if (!maskerData->maskImage)
        return { };

    SVGRenderingContext::clipToImageBuffer(*context, repaintRect, scale, maskerData->maskImage, missingMaskerData);
    return { ApplyResult::ResourceApplied };
}

bool LegacyRenderSVGResourceMasker::drawContentIntoMaskImage(MaskerData* maskerData, const DestinationColorSpace& colorSpace, RenderObject* object)
{
    RefPtr maskImage = maskerData->maskImage;
    auto& maskImageContext = maskImage->context();
    auto objectBoundingBox = object->objectBoundingBox();

    if (!drawContentIntoContext(maskImageContext, objectBoundingBox))
        return false;

#if !USE(CG) && !USE(SKIA)
    maskImage->transformToColorSpace(colorSpace);
#else
    UNUSED_PARAM(colorSpace);
#endif

    // Create the luminance mask.
    if (style().svgStyle().maskType() == MaskType::Luminance)
        maskImage->convertToLuminanceMask();

    return true;
}

bool LegacyRenderSVGResourceMasker::drawContentIntoContext(GraphicsContext& context, const FloatRect& objectBoundingBox)
{
    // Eventually adjust the mask image context according to the target objectBoundingBox.
    AffineTransform maskContentTransformation;

    Ref maskElement = this->maskElement();
    if (maskElement->maskContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
        maskContentTransformation.translate(objectBoundingBox.location());
        maskContentTransformation.scale(objectBoundingBox.size());
        context.concatCTM(maskContentTransformation);
    }

    // Draw the content into the ImageBuffer.
    for (Ref child : childrenOfType<SVGElement>(maskElement)) {
        CheckedPtr renderer = child->renderer();
        if (!renderer)
            continue;
        if (renderer->needsLayout())
            return false;
        const CheckedRef style = renderer->style();
        if (style->display() == DisplayType::None || style->usedVisibility() != Visibility::Visible)
            continue;
        SVGRenderingContext::renderSubtreeToContext(context, *renderer, maskContentTransformation);
    }

    return true;
}

bool LegacyRenderSVGResourceMasker::drawContentIntoContext(GraphicsContext& context, const FloatRect& destinationRect, const FloatRect& sourceRect, ImagePaintingOptions options)
{
    GraphicsContextStateSaver stateSaver(context);

    context.setCompositeOperation(options.compositeOperator(), options.blendMode());

    context.translate(destinationRect.location());

    if (destinationRect.size() != sourceRect.size())
        context.scale(destinationRect.size() / sourceRect.size());

    context.translate(-sourceRect.location());

    return drawContentIntoContext(context, { { }, destinationRect.size() });
}

void LegacyRenderSVGResourceMasker::calculateMaskContentRepaintRect(RepaintRectCalculation repaintRectCalculation)
{
    for (RefPtr childNode = maskElement().firstChild(); childNode; childNode = childNode->nextSibling()) {
        CheckedPtr renderer = dynamicDowncast<RenderElement>(childNode->renderer());
        if (!renderer || !childNode->isSVGElement())
            continue;
        const CheckedRef style = renderer->style();
        if (style->display() == DisplayType::None || style->usedVisibility() != Visibility::Visible)
             continue;
        m_maskContentBoundaries[repaintRectCalculation].unite(renderer->localToParentTransform().mapRect(renderer->repaintRectInLocalCoordinates(repaintRectCalculation)));
    }
}

FloatRect LegacyRenderSVGResourceMasker::resourceBoundingBox(const RenderObject& object, RepaintRectCalculation repaintRectCalculation)
{
    FloatRect objectBoundingBox = object.objectBoundingBox();
    Ref maskElement = this->maskElement();
    FloatRect maskBoundaries = SVGLengthContext::resolveRectangle<SVGMaskElement>(maskElement.ptr(), maskElement->maskUnits(), objectBoundingBox);

    // Resource was not layouted yet. Give back clipping rect of the mask.
    if (selfNeedsLayout())
        return maskBoundaries;

    if (m_maskContentBoundaries[repaintRectCalculation].isEmpty())
        calculateMaskContentRepaintRect(repaintRectCalculation);

    FloatRect maskRect = m_maskContentBoundaries[repaintRectCalculation];
    if (maskElement->maskContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
        AffineTransform transform;
        transform.translate(objectBoundingBox.location());
        transform.scale(objectBoundingBox.size());
        maskRect = transform.mapRect(maskRect);
    }

    maskRect.intersect(maskBoundaries);
    return maskRect;
}

} // namespace WebCore
