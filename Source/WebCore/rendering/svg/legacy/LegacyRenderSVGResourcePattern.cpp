/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
#include "LegacyRenderSVGResourcePattern.h"

#include "ContainerNodeInlines.h"
#include "ElementChildIteratorInlines.h"
#include "GraphicsContext.h"
#include "LegacyRenderSVGRoot.h"
#include "LocalFrameView.h"
#include "NativeImage.h"
#include "SVGElementTypeHelpers.h"
#include "SVGFitToViewBox.h"
#include "SVGRenderStyle.h"
#include "SVGRenderingContext.h"
#include "SVGResources.h"
#include "SVGResourcesCache.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(LegacyRenderSVGResourcePattern);

LegacyRenderSVGResourcePattern::LegacyRenderSVGResourcePattern(SVGPatternElement& element, RenderStyle&& style)
    : LegacyRenderSVGResourceContainer(Type::LegacySVGResourcePattern, element, WTFMove(style))
{
}

LegacyRenderSVGResourcePattern::~LegacyRenderSVGResourcePattern() = default;

SVGPatternElement& LegacyRenderSVGResourcePattern::patternElement() const
{
    return downcast<SVGPatternElement>(LegacyRenderSVGResourceContainer::element());
}

Ref<SVGPatternElement> LegacyRenderSVGResourcePattern::protectedPatternElement() const
{
    return patternElement();
}

void LegacyRenderSVGResourcePattern::removeAllClientsFromCache()
{
    m_patternMap.clear();
    m_shouldCollectPatternAttributes = true;
}

void LegacyRenderSVGResourcePattern::removeAllClientsFromCacheAndMarkForInvalidationIfNeeded(bool markForInvalidation, SingleThreadWeakHashSet<RenderObject>* visitedRenderers)
{
    removeAllClientsFromCache();
    markAllClientsForInvalidationIfNeeded(markForInvalidation ? RepaintInvalidation : ParentOnlyInvalidation, visitedRenderers);
}

void LegacyRenderSVGResourcePattern::removeClientFromCache(RenderElement& client)
{
    m_patternMap.remove(client);
}

void LegacyRenderSVGResourcePattern::collectPatternAttributes(PatternAttributes& attributes) const
{
    const LegacyRenderSVGResourcePattern* current = this;

    while (current) {
        Ref pattern = current->patternElement();
        pattern->collectPatternAttributes(attributes);

        auto* resources = SVGResourcesCache::cachedResourcesForRenderer(*current);
        ASSERT_IMPLIES(resources && resources->linkedResource(), is<LegacyRenderSVGResourcePattern>(resources->linkedResource()));
        current = resources ? downcast<LegacyRenderSVGResourcePattern>(resources->linkedResource()) : nullptr;
    }
}

PatternData* LegacyRenderSVGResourcePattern::buildPattern(RenderElement& renderer, OptionSet<RenderSVGResourceMode> resourceMode, GraphicsContext& context)
{
    ASSERT(!m_shouldCollectPatternAttributes);

    PatternData* currentData = m_patternMap.get(renderer);
    if (currentData && currentData->pattern)
        return currentData;

    // If we couldn't determine the pattern content element root, stop here.
    if (!m_attributes.patternContentElement())
        return nullptr;

    // An empty viewBox disables rendering.
    if (m_attributes.hasViewBox() && m_attributes.viewBox().isEmpty())
        return nullptr;

    // Compute all necessary transformations to build the tile image & the pattern.
    FloatRect tileBoundaries;
    AffineTransform tileImageTransform;
    if (!buildTileImageTransform(renderer, m_attributes, protectedPatternElement(), tileBoundaries, tileImageTransform))
        return nullptr;

    auto absoluteTransform = SVGRenderingContext::calculateTransformationToOutermostCoordinateSystem(renderer);

    // Ignore 2D rotation, as it doesn't affect the size of the tile.
    FloatSize tileScale(absoluteTransform.xScale(), absoluteTransform.yScale());

    // Scale the tile size to match the scale level of the patternTransform.
    tileScale.scale(static_cast<float>(m_attributes.patternTransform().xScale()), static_cast<float>(m_attributes.patternTransform().yScale()));

    // Build tile image.
    auto tileImage = createTileImage(context, tileBoundaries.size(), tileScale, tileImageTransform, m_attributes);
    if (!tileImage)
        return nullptr;

    auto tileImageSize = tileImage->logicalSize();

    // Compute pattern space transformation.
    auto patternData = makeUnique<PatternData>();
    patternData->transform.translate(tileBoundaries.location());
    patternData->transform.scale(tileBoundaries.size() / tileImageSize);

    AffineTransform patternTransform = m_attributes.patternTransform();
    if (!patternTransform.isInvertible())
        return nullptr;

    if (!patternTransform.isIdentity())
        patternData->transform = patternTransform * patternData->transform;

    // Account for text drawing resetting the context to non-scaled, see SVGInlineTextBox::paintTextWithShadows.
    if (resourceMode.contains(RenderSVGResourceMode::ApplyToText)) {
        auto textScale = computeTextPaintingScale(renderer);
        if (textScale != 1)
            patternData->transform.scale(textScale);
    }

    // Build pattern.
    patternData->pattern = Pattern::create({ tileImage.releaseNonNull() }, { true, true, patternData->transform });

    // Various calls above may trigger invalidations in some fringe cases (ImageBuffer allocation
    // failures in the SVG image cache for example). To avoid having our PatternData deleted by
    // removeAllClientsFromCacheAndMarkForInvalidation(), we only make it visible in the cache at the very end.
    return m_patternMap.set(renderer, WTFMove(patternData)).iterator->value.get();
}

auto LegacyRenderSVGResourcePattern::applyResource(RenderElement& renderer, const RenderStyle& style, GraphicsContext*& context, OptionSet<RenderSVGResourceMode> resourceMode) -> OptionSet<ApplyResult>
{
    ASSERT(context);
    ASSERT(!resourceMode.isEmpty());

    if (m_shouldCollectPatternAttributes) {
        protectedPatternElement()->synchronizeAllAttributes();

        m_attributes = PatternAttributes();
        collectPatternAttributes(m_attributes);
        m_shouldCollectPatternAttributes = false;
    }
    
    // Spec: When the geometry of the applicable element has no width or height and objectBoundingBox is specified,
    // then the given effect (e.g. a gradient or a filter) will be ignored.
    FloatRect objectBoundingBox = renderer.objectBoundingBox();
    if (m_attributes.patternUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX && objectBoundingBox.isEmpty())
        return { };

    PatternData* patternData = buildPattern(renderer, resourceMode, *context);
    if (!patternData)
        return { };

    // Draw pattern
    context->save();

    Ref svgStyle = style.svgStyle();

    if (resourceMode.contains(RenderSVGResourceMode::ApplyToFill)) {
        context->setAlpha(svgStyle->fillOpacity().value.value);
        context->setFillPattern(*patternData->pattern);
        context->setFillRule(svgStyle->fillRule());
    } else if (resourceMode.contains(RenderSVGResourceMode::ApplyToStroke)) {
        if (svgStyle->vectorEffect() == VectorEffect::NonScalingStroke)
            patternData->pattern->setPatternSpaceTransform(transformOnNonScalingStroke(&renderer, patternData->transform));
        context->setAlpha(svgStyle->strokeOpacity().value.value);
        context->setStrokePattern(*patternData->pattern);
        SVGRenderSupport::applyStrokeStyleToContext(*context, style, renderer);
    }

    if (resourceMode.contains(RenderSVGResourceMode::ApplyToText)) {
        if (resourceMode.contains(RenderSVGResourceMode::ApplyToFill)) {
            context->setTextDrawingMode(TextDrawingMode::Fill);

#if USE(CG)
            context->applyFillPattern();
#endif
        } else if (resourceMode.contains(RenderSVGResourceMode::ApplyToStroke)) {
            context->setTextDrawingMode(TextDrawingMode::Stroke);

#if USE(CG)
            context->applyStrokePattern();
#endif
        }
    }

    return { ApplyResult::ResourceApplied };
}

void LegacyRenderSVGResourcePattern::postApplyResource(RenderElement&, GraphicsContext*& context, OptionSet<RenderSVGResourceMode> resourceMode, const Path* path, const RenderElement* shape)
{
    ASSERT(context);
    ASSERT(!resourceMode.isEmpty());
    fillAndStrokePathOrShape(*context, resourceMode, path, shape);
    context->restore();
}

static inline FloatRect calculatePatternBoundaries(const PatternAttributes& attributes,
                                                   const FloatRect& objectBoundingBox,
                                                   const SVGPatternElement& patternElement)
{
    return SVGLengthContext::resolveRectangle(&patternElement, attributes.patternUnits(), objectBoundingBox, attributes.x(), attributes.y(), attributes.width(), attributes.height());
}

bool LegacyRenderSVGResourcePattern::buildTileImageTransform(RenderElement& renderer,
                                                       const PatternAttributes& attributes,
                                                       const SVGPatternElement& patternElement,
                                                       FloatRect& patternBoundaries,
                                                       AffineTransform& tileImageTransform) const
{
    FloatRect objectBoundingBox = renderer.objectBoundingBox();
    patternBoundaries = calculatePatternBoundaries(attributes, objectBoundingBox, patternElement); 
    if (patternBoundaries.width() <= 0 || patternBoundaries.height() <= 0)
        return false;

    AffineTransform viewBoxCTM = SVGFitToViewBox::viewBoxToViewTransform(attributes.viewBox(), attributes.preserveAspectRatio(), patternBoundaries.width(), patternBoundaries.height());

    // Apply viewBox/objectBoundingBox transformations.
    if (!viewBoxCTM.isIdentity())
        tileImageTransform = viewBoxCTM;
    else if (attributes.patternContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX)
        tileImageTransform.scale(objectBoundingBox.width(), objectBoundingBox.height());

    return true;
}

RefPtr<ImageBuffer> LegacyRenderSVGResourcePattern::createTileImage(GraphicsContext& context, const FloatSize& size, const FloatSize& scale, const AffineTransform& tileImageTransform, const PatternAttributes& attributes) const
{
    // This is equivalent to making createImageBuffer() use roundedIntSize().
    auto roundedUnscaledImageBufferSize = [](const FloatSize& size, const FloatSize& scale) -> FloatSize {
        auto scaledSize = size * scale;
        return size - (expandedIntSize(scaledSize) - roundedIntSize(scaledSize)) * (scaledSize - flooredIntSize(scaledSize)) / scale;
    };

    auto tileSize = roundedUnscaledImageBufferSize(size, scale);

    // FIXME: Use createImageBuffer(rect, scale), delete the above calculations and fix 'tileImageTransform'
    auto tileImage = context.createScaledImageBuffer(tileSize, scale);
    if (!tileImage)
        return nullptr;

    GraphicsContext& tileImageContext = tileImage->context();

    // Apply tile image transformations.
    if (!tileImageTransform.isIdentity())
        tileImageContext.concatCTM(tileImageTransform);

    AffineTransform contentTransformation;
    if (attributes.patternContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX)
        contentTransformation = tileImageTransform;

    // Draw the content into the ImageBuffer.
    for (auto& child : childrenOfType<SVGElement>(Ref { *attributes.patternContentElement() })) {
        if (!child.renderer())
            continue;
        if (child.renderer()->needsLayout())
            return nullptr;
        SVGRenderingContext::renderSubtreeToContext(tileImageContext, *child.renderer(), contentTransformation);
    }

    return tileImage;
}

}
