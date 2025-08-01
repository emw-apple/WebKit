/*
 * Copyright (C) 2003-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
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

#include "config.h"
#include "GraphicsContextCG.h"

#if USE(CG)

#include "AffineTransform.h"
#include "CGSubimageCacheWithTimer.h"
#include "CGUtilities.h"
#include "DisplayListRecorder.h"
#include "FloatConversion.h"
#include "Gradient.h"
#include "ImageBuffer.h"
#include "ImageOrientation.h"
#include "Logging.h"
#include "Path.h"
#include "PathCG.h"
#include "Pattern.h"
#include "ShadowBlur.h"
#include "Timer.h"
#include <pal/spi/cg/CoreGraphicsSPI.h>
#include <wtf/MathExtras.h>
#include <wtf/RetainPtr.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GraphicsContextCG);

static void setCGFillColor(CGContextRef context, const Color& color, const DestinationColorSpace& colorSpace)
{
    CGContextSetFillColorWithColor(context, cachedSDRCGColorForColorspace(color, colorSpace).get());
}

inline CGAffineTransform getUserToBaseCTM(CGContextRef context)
{
    return CGAffineTransformConcat(CGContextGetCTM(context), CGAffineTransformInvert(CGContextGetBaseCTM(context)));
}

static InterpolationQuality coreInterpolationQuality(CGContextRef context)
{
    switch (CGContextGetInterpolationQuality(context)) {
    case kCGInterpolationDefault:
        return InterpolationQuality::Default;
    case kCGInterpolationNone:
        return InterpolationQuality::DoNotInterpolate;
    case kCGInterpolationLow:
        return InterpolationQuality::Low;
    case kCGInterpolationMedium:
        return InterpolationQuality::Medium;
    case kCGInterpolationHigh:
        return InterpolationQuality::High;
    }
    return InterpolationQuality::Default;
}

static CGTextDrawingMode cgTextDrawingMode(TextDrawingModeFlags mode)
{
    bool fill = mode.contains(TextDrawingMode::Fill);
    bool stroke = mode.contains(TextDrawingMode::Stroke);
    if (fill && stroke)
        return kCGTextFillStroke;
    if (fill)
        return kCGTextFill;
    return kCGTextStroke;
}

static CGBlendMode selectCGBlendMode(CompositeOperator compositeOperator, BlendMode blendMode)
{
    switch (blendMode) {
    case BlendMode::Normal:
        switch (compositeOperator) {
        case CompositeOperator::Clear:
            return kCGBlendModeClear;
        case CompositeOperator::Copy:
            return kCGBlendModeCopy;
        case CompositeOperator::SourceOver:
            return kCGBlendModeNormal;
        case CompositeOperator::SourceIn:
            return kCGBlendModeSourceIn;
        case CompositeOperator::SourceOut:
            return kCGBlendModeSourceOut;
        case CompositeOperator::SourceAtop:
            return kCGBlendModeSourceAtop;
        case CompositeOperator::DestinationOver:
            return kCGBlendModeDestinationOver;
        case CompositeOperator::DestinationIn:
            return kCGBlendModeDestinationIn;
        case CompositeOperator::DestinationOut:
            return kCGBlendModeDestinationOut;
        case CompositeOperator::DestinationAtop:
            return kCGBlendModeDestinationAtop;
        case CompositeOperator::XOR:
            return kCGBlendModeXOR;
        case CompositeOperator::PlusDarker:
            return kCGBlendModePlusDarker;
        case CompositeOperator::PlusLighter:
            return kCGBlendModePlusLighter;
        case CompositeOperator::Difference:
            return kCGBlendModeDifference;
        }
        break;
    case BlendMode::Multiply:
        return kCGBlendModeMultiply;
    case BlendMode::Screen:
        return kCGBlendModeScreen;
    case BlendMode::Overlay:
        return kCGBlendModeOverlay;
    case BlendMode::Darken:
        return kCGBlendModeDarken;
    case BlendMode::Lighten:
        return kCGBlendModeLighten;
    case BlendMode::ColorDodge:
        return kCGBlendModeColorDodge;
    case BlendMode::ColorBurn:
        return kCGBlendModeColorBurn;
    case BlendMode::HardLight:
        return kCGBlendModeHardLight;
    case BlendMode::SoftLight:
        return kCGBlendModeSoftLight;
    case BlendMode::Difference:
        return kCGBlendModeDifference;
    case BlendMode::Exclusion:
        return kCGBlendModeExclusion;
    case BlendMode::Hue:
        return kCGBlendModeHue;
    case BlendMode::Saturation:
        return kCGBlendModeSaturation;
    case BlendMode::Color:
        return kCGBlendModeColor;
    case BlendMode::Luminosity:
        return kCGBlendModeLuminosity;
    case BlendMode::PlusDarker:
        return kCGBlendModePlusDarker;
    case BlendMode::PlusLighter:
        return kCGBlendModePlusLighter;
    }

    return kCGBlendModeNormal;
}

static void setCGBlendMode(CGContextRef context, CompositeOperator op, BlendMode blendMode)
{
    CGContextSetBlendMode(context, selectCGBlendMode(op, blendMode));
}

static void setCGContextPath(CGContextRef context, const Path& path)
{
    CGContextBeginPath(context);
    addToCGContextPath(context, path);
}

static void drawPathWithCGContext(CGContextRef context, CGPathDrawingMode drawingMode, const Path& path)
{
    CGContextDrawPathDirect(context, drawingMode, path.platformPath(), nullptr);
}

static RenderingMode renderingModeForCGContext(CGContextRef cgContext, GraphicsContextCG::CGContextSource source)
{
    if (!cgContext)
        return RenderingMode::Unaccelerated;
    auto type = CGContextGetType(cgContext);
    if (type == kCGContextTypeIOSurface || (source == GraphicsContextCG::CGContextFromCALayer && type == kCGContextTypeUnknown))
        return RenderingMode::Accelerated;
    if (type == kCGContextTypePDF)
        return RenderingMode::PDFDocument;
    return RenderingMode::Unaccelerated;
}

static GraphicsContext::IsDeferred isDeferredForCGContext(CGContextRef cgContext)
{
    if (!cgContext || CGContextGetType(cgContext) == kCGContextTypeBitmap)
        return GraphicsContext::IsDeferred::No;
    // Other CGContexts are deferred (iosurface, display list) or potentially deferred.
    return GraphicsContext::IsDeferred::Yes;
}

GraphicsContextCG::GraphicsContextCG(CGContextRef cgContext, CGContextSource source, std::optional<RenderingMode> knownRenderingMode)
    : GraphicsContext(isDeferredForCGContext(cgContext), GraphicsContextState::basicChangeFlags, coreInterpolationQuality(cgContext))
    , m_cgContext(cgContext)
    , m_renderingMode(knownRenderingMode.value_or(renderingModeForCGContext(cgContext, source)))
    , m_isLayerCGContext(source == GraphicsContextCG::CGContextFromCALayer)
{
    if (!cgContext)
        return;
    // Make sure the context starts in sync with our state.
    didUpdateState(m_state);
}

GraphicsContextCG::~GraphicsContextCG() = default;

bool GraphicsContextCG::hasPlatformContext() const
{
    return true;
}

CGContextRef GraphicsContextCG::contextForState() const
{
    ASSERT(m_cgContext);
    return m_cgContext.get();
}

const DestinationColorSpace& GraphicsContextCG::colorSpace() const
{
    if (m_colorSpace)
        return *m_colorSpace;

    auto context = platformContext();
    RetainPtr<CGColorSpaceRef> colorSpace;

    // FIXME: Need to handle kCGContextTypePDF.
    if (CGContextGetType(context) == kCGContextTypeIOSurface)
        colorSpace = CGIOSurfaceContextGetColorSpace(context);
    else if (CGContextGetType(context) == kCGContextTypeBitmap)
        colorSpace = CGBitmapContextGetColorSpace(context);
    else
        colorSpace = adoptCF(CGContextCopyDeviceColorSpace(context));

    // FIXME: Need to ASSERT(colorSpace). For now fall back to sRGB if colorSpace is nil.
    m_colorSpace = colorSpace ? DestinationColorSpace(colorSpace) : DestinationColorSpace::SRGB();
    return *m_colorSpace;
}

void GraphicsContextCG::save(GraphicsContextState::Purpose purpose)
{
    GraphicsContext::save(purpose);
    CGContextSaveGState(contextForState());
}

void GraphicsContextCG::restore(GraphicsContextState::Purpose purpose)
{
    if (!stackSize())
        return;

    GraphicsContext::restore(purpose);
    CGContextRestoreGState(contextForState());
    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::drawNativeImageInternal(NativeImage& nativeImage, const FloatRect& destRect, const FloatRect& srcRect, ImagePaintingOptions options)
{
    auto image = nativeImage.platformImage();
    if (!image)
        return;
    auto imageSize = nativeImage.size();
    if (options.orientation().usesWidthAsHeight())
        imageSize = imageSize.transposedSize();
    auto imageRect = FloatRect { { }, imageSize };
    auto normalizedSrcRect = normalizeRect(srcRect);
    auto normalizedDestRect = normalizeRect(destRect);
    if (!imageRect.intersects(normalizedSrcRect))
        return;

#if !LOG_DISABLED
    MonotonicTime startTime = MonotonicTime::now();
#endif

    auto shouldUseSubimage = [](CGInterpolationQuality interpolationQuality, const FloatRect& destRect, const FloatRect& srcRect, const AffineTransform& transform) -> bool {
        if (interpolationQuality == kCGInterpolationNone)
            return false;
        if (transform.isRotateOrShear())
            return true;
        auto xScale = destRect.width() * transform.xScale() / srcRect.width();
        auto yScale = destRect.height() * transform.yScale() / srcRect.height();
        return !WTF::areEssentiallyEqual(xScale, yScale) || xScale > 1;
    };

    auto getSubimage = [](CGImageRef image, const FloatSize& imageSize, const FloatRect& subimageRect, ImagePaintingOptions options) -> RetainPtr<CGImageRef> {
        auto physicalSubimageRect = subimageRect;

        if (options.orientation() != ImageOrientation::Orientation::None) {
            // subimageRect is in logical coordinates. getSubimage() deals with none-oriented
            // image. We need to convert subimageRect to physical image coordinates.
            if (auto transform = options.orientation().transformFromDefault(imageSize).inverse())
                physicalSubimageRect = transform.value().mapRect(physicalSubimageRect);
        }

#if CACHE_SUBIMAGES
        if (!(CGImageGetCachingFlags(image) & kCGImageCachingTransient))
            return CGSubimageCacheWithTimer::getSubimage(image, physicalSubimageRect);
#endif
        return adoptCF(CGImageCreateWithImageInRect(image, physicalSubimageRect));
    };

#if HAVE(SUPPORT_HDR_DISPLAY_APIS)
    auto setCGDynamicRangeLimitForImage = [](CGContextRef context, CGImageRef image, float dynamicRangeLimit) {
        float edrStrength = dynamicRangeLimit == 1.0 ? 1 : 0;
        float cdrStrength = dynamicRangeLimit == 0.5 ? 1 : 0;
        unsigned averageLightLevel = CGImageGetContentAverageLightLevelNits(image);

        RetainPtr edrStrengthNumber = adoptCF(CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &edrStrength));
        RetainPtr cdrStrengthNumber = adoptCF(CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &cdrStrength));
        RetainPtr averageLightLevelNumber = adoptCF(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &averageLightLevel));

        CFTypeRef toneMappingKeys[] = { kCGContentEDRStrength, kCGContentAverageLightLevel, kCGConstrainedDynamicRange };
        CFTypeRef toneMappingValues[] = { edrStrengthNumber.get(), averageLightLevelNumber.get(), cdrStrengthNumber.get() };

        RetainPtr toneMappingOptions = adoptCF(CFDictionaryCreate(kCFAllocatorDefault, toneMappingKeys, toneMappingValues, sizeof(toneMappingKeys) / sizeof(toneMappingKeys[0]), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

        CGContentToneMappingInfo toneMappingInfo = { kCGToneMappingReferenceWhiteBased, toneMappingOptions.get() };
        CGContextSetContentToneMappingInfo(context, toneMappingInfo);
    };
#endif

    auto context = platformContext();
    CGContextStateSaver stateSaver(context, false);
    auto transform = CGContextGetCTM(context);

    auto subImage = image;

    auto adjustedDestRect = normalizedDestRect;

    if (normalizedSrcRect != imageRect) {
        CGInterpolationQuality interpolationQuality = CGContextGetInterpolationQuality(context);
        auto scale = normalizedDestRect.size() / normalizedSrcRect.size();

        if (shouldUseSubimage(interpolationQuality, normalizedDestRect, normalizedSrcRect, transform)) {
            auto subimageRect = enclosingIntRect(normalizedSrcRect);

            // When the image is scaled using high-quality interpolation, we create a temporary CGImage
            // containing only the portion we want to display. We need to do this because high-quality
            // interpolation smoothes sharp edges, causing pixels from outside the source rect to bleed
            // into the destination rect. See <rdar://problem/6112909>.
            subImage = getSubimage(subImage.get(), imageSize, subimageRect, options);

            auto subPixelPadding = normalizedSrcRect.location() - subimageRect.location();
            adjustedDestRect = { adjustedDestRect.location() - subPixelPadding * scale, subimageRect.size() * scale };
        } else {
            // If the source rect is a subportion of the image, then we compute an inflated destination rect
            // that will hold the entire image and then set a clip to the portion that we want to display.
            adjustedDestRect = { adjustedDestRect.location() - toFloatSize(normalizedSrcRect.location()) * scale, imageSize * scale };
        }

        if (!normalizedDestRect.contains(adjustedDestRect)) {
            stateSaver.save();
            CGContextClipToRect(context, normalizedDestRect);
        }
    }

#if PLATFORM(IOS_FAMILY)
    bool wasAntialiased = CGContextGetShouldAntialias(context);
    // Anti-aliasing is on by default on the iPhone. Need to turn it off when drawing images.
    CGContextSetShouldAntialias(context, false);

    // Align to pixel boundaries
    adjustedDestRect = roundToDevicePixels(adjustedDestRect);
#endif

    auto oldCompositeOperator = compositeOperation();
    auto oldBlendMode = blendMode();
    setCGBlendMode(context, options.compositeOperator(), options.blendMode());

#if HAVE(SUPPORT_HDR_DISPLAY_APIS)
    auto oldHeadroom = CGContextGetEDRTargetHeadroom(context);
    auto oldToneMappingInfo = CGContextGetContentToneMappingInfo(context);

    auto headroom = options.headroom();
    if (headroom == Headroom::FromImage)
        headroom = nativeImage.headroom();
    if (m_maxEDRHeadroom)
        headroom = Headroom(std::min<float>(headroom, *m_maxEDRHeadroom));

    if (nativeImage.headroom() > headroom) {
        LOG_WITH_STREAM(HDR, stream << "GraphicsContextCG::drawNativeImageInternal setEDRTargetHeadroom " << headroom << " max(" << m_maxEDRHeadroom << ")");
        CGContextSetEDRTargetHeadroom(context, headroom);
    }

    if (options.dynamicRangeLimit() == PlatformDynamicRangeLimit::standard() && options.drawsHDRContent() == DrawsHDRContent::Yes)
        setCGDynamicRangeLimitForImage(context, subImage.get(), options.dynamicRangeLimit().value());
#endif

    // Make the origin be at adjustedDestRect.location()
    CGContextTranslateCTM(context, adjustedDestRect.x(), adjustedDestRect.y());
    adjustedDestRect.setLocation(FloatPoint::zero());

    if (options.orientation() != ImageOrientation::Orientation::None) {
        CGContextConcatCTM(context, options.orientation().transformFromDefault(adjustedDestRect.size()));

        // The destination rect will have its width and height already reversed for the orientation of
        // the image, as it was needed for page layout, so we need to reverse it back here.
        if (options.orientation().usesWidthAsHeight())
            adjustedDestRect = adjustedDestRect.transposedRect();
    }

    // Flip the coords.
    CGContextTranslateCTM(context, 0, adjustedDestRect.height());
    CGContextScaleCTM(context, 1, -1);

    // Draw the image.
    CGContextDrawImage(context, adjustedDestRect, subImage.get());

    if (!stateSaver.didSave()) {
        CGContextSetCTM(context, transform);
#if PLATFORM(IOS_FAMILY)
        CGContextSetShouldAntialias(context, wasAntialiased);
#endif
        setCGBlendMode(context, oldCompositeOperator, oldBlendMode);
#if HAVE(SUPPORT_HDR_DISPLAY_APIS)
        CGContextSetContentToneMappingInfo(context, oldToneMappingInfo);
        CGContextSetEDRTargetHeadroom(context, oldHeadroom);
#endif
    }

    LOG_WITH_STREAM(Images, stream << "GraphicsContextCG::drawNativeImageInternal " << image.get() << " size " << imageSize << " into " << destRect << " took " << (MonotonicTime::now() - startTime).milliseconds() << "ms");
}

static void drawPatternCallback(void* info, CGContextRef context)
{
    CGImageRef image = (CGImageRef)info;
    auto rect = cgRoundToDevicePixels(CGContextGetUserSpaceToDeviceSpaceTransform(context), cgImageRect(image));
    CGContextDrawImage(context, rect, image);
}

static void patternReleaseCallback(void* info)
{
    callOnMainThread([image = adoptCF(static_cast<CGImageRef>(info))] { });
}

void GraphicsContextCG::drawPattern(NativeImage& nativeImage, const FloatRect& destRect, const FloatRect& tileRect, const AffineTransform& patternTransform, const FloatPoint& phase, const FloatSize& spacing, ImagePaintingOptions options)
{
    if (!patternTransform.isInvertible())
        return;

    auto image = nativeImage.platformImage();
    auto imageSize = nativeImage.size();

    CGContextRef context = platformContext();
    CGContextStateSaver stateSaver(context);
    CGContextClipToRect(context, destRect);

    setCGBlendMode(context, options.compositeOperator(), options.blendMode());

    CGContextTranslateCTM(context, destRect.x(), destRect.y() + destRect.height());
    CGContextScaleCTM(context, 1, -1);

    // Compute the scaled tile size.
    float scaledTileHeight = tileRect.height() * narrowPrecisionToFloat(patternTransform.d());

    // We have to adjust the phase to deal with the fact we're in Cartesian space now (with the bottom left corner of destRect being
    // the origin).
    float adjustedX = phase.x() - destRect.x() + tileRect.x() * narrowPrecisionToFloat(patternTransform.a()); // We translated the context so that destRect.x() is the origin, so subtract it out.
    float adjustedY = destRect.height() - (phase.y() - destRect.y() + tileRect.y() * narrowPrecisionToFloat(patternTransform.d()) + scaledTileHeight);

    float h = CGImageGetHeight(image.get());

    RetainPtr<CGImageRef> subImage;
    if (tileRect.size() == imageSize)
        subImage = image;
    else {
        // Copying a sub-image out of a partially-decoded image stops the decoding of the original image. It should never happen
        // because sub-images are only used for border-image, which only renders when the image is fully decoded.
        ASSERT(h == imageSize.height());
        subImage = adoptCF(CGImageCreateWithImageInRect(image.get(), tileRect));
    }

    // If we need to paint gaps between tiles because we have a partially loaded image or non-zero spacing,
    // fall back to the less efficient CGPattern-based mechanism.
    float scaledTileWidth = tileRect.width() * narrowPrecisionToFloat(patternTransform.a());
    float w = CGImageGetWidth(image.get());
    if (w == imageSize.width() && h == imageSize.height() && !spacing.width() && !spacing.height()) {
        // FIXME: CG seems to snap the images to integral sizes. When we care (e.g. with border-image-repeat: round),
        // we should tile all but the last, and stretch the last image to fit.
        CGContextDrawTiledImage(context, FloatRect(adjustedX, adjustedY, scaledTileWidth, scaledTileHeight), subImage.get());
    } else {
        static const CGPatternCallbacks patternCallbacks = { 0, drawPatternCallback, patternReleaseCallback };
        CGAffineTransform matrix = CGAffineTransformMake(narrowPrecisionToCGFloat(patternTransform.a()), 0, 0, narrowPrecisionToCGFloat(patternTransform.d()), adjustedX, adjustedY);
        matrix = CGAffineTransformConcat(matrix, CGContextGetCTM(context));
        // The top of a partially-decoded image is drawn at the bottom of the tile. Map it to the top.
        matrix = CGAffineTransformTranslate(matrix, 0, imageSize.height() - h);
        CGImageRef platformImage = CGImageRetain(subImage.get());
        RetainPtr<CGPatternRef> pattern = adoptCF(CGPatternCreate(platformImage, CGRectMake(0, 0, tileRect.width(), tileRect.height()), matrix,
            tileRect.width() + spacing.width() * (1 / narrowPrecisionToFloat(patternTransform.a())),
            tileRect.height() + spacing.height() * (1 / narrowPrecisionToFloat(patternTransform.d())),
            kCGPatternTilingConstantSpacing, true, &patternCallbacks));

        if (!pattern)
            return;

        RetainPtr<CGColorSpaceRef> patternSpace = adoptCF(CGColorSpaceCreatePattern(nullptr));

        CGFloat alpha = 1;
        RetainPtr<CGColorRef> color = adoptCF(CGColorCreateWithPattern(patternSpace.get(), pattern.get(), &alpha));
        CGContextSetFillColorSpace(context, patternSpace.get());

        CGContextSetBaseCTM(context, CGAffineTransformIdentity);
        CGContextSetPatternPhase(context, CGSizeZero);

        CGContextSetFillColorWithColor(context, color.get());
        CGContextFillRect(context, CGContextGetClipBoundingBox(context)); // FIXME: we know the clip; we set it above.
    }
}

// Draws a filled rectangle with a stroked border.
void GraphicsContextCG::drawRect(const FloatRect& rect, float borderThickness)
{
    // FIXME: this function does not handle patterns and gradients like drawPath does, it probably should.
    ASSERT(!rect.isEmpty());

    CGContextRef context = platformContext();

    CGContextFillRect(context, rect);

    if (strokeStyle() != StrokeStyle::NoStroke) {
        // We do a fill of four rects to simulate the stroke of a border.
        Color oldFillColor = fillColor();
        if (oldFillColor != strokeColor())
            setCGFillColor(context, strokeColor(), colorSpace());
        CGRect rects[4] = {
            FloatRect(rect.x(), rect.y(), rect.width(), borderThickness),
            FloatRect(rect.x(), rect.maxY() - borderThickness, rect.width(), borderThickness),
            FloatRect(rect.x(), rect.y() + borderThickness, borderThickness, rect.height() - 2 * borderThickness),
            FloatRect(rect.maxX() - borderThickness, rect.y() + borderThickness, borderThickness, rect.height() - 2 * borderThickness)
        };
        CGContextFillRects(context, rects, 4);
        if (oldFillColor != strokeColor())
            setCGFillColor(context, oldFillColor, colorSpace());
    }
}

// This is only used to draw borders.
void GraphicsContextCG::drawLine(const FloatPoint& point1, const FloatPoint& point2)
{
    if (strokeStyle() == StrokeStyle::NoStroke)
        return;

    float thickness = strokeThickness();
    bool isVerticalLine = (point1.x() + thickness == point2.x());
    float strokeWidth = isVerticalLine ? point2.y() - point1.y() : point2.x() - point1.x();
    if (!thickness || !strokeWidth)
        return;

    CGContextRef context = platformContext();

    StrokeStyle strokeStyle = this->strokeStyle();
    float cornerWidth = 0;
    bool drawsDashedLine = strokeStyle == StrokeStyle::DottedStroke || strokeStyle == StrokeStyle::DashedStroke;

    CGContextStateSaver stateSaver(context, drawsDashedLine);
    if (drawsDashedLine) {
        // Figure out end points to ensure we always paint corners.
        cornerWidth = dashedLineCornerWidthForStrokeWidth(strokeWidth);
        setCGFillColor(context, strokeColor(), colorSpace());
        if (isVerticalLine) {
            CGContextFillRect(context, FloatRect(point1.x(), point1.y(), thickness, cornerWidth));
            CGContextFillRect(context, FloatRect(point1.x(), point2.y() - cornerWidth, thickness, cornerWidth));
        } else {
            CGContextFillRect(context, FloatRect(point1.x(), point1.y(), cornerWidth, thickness));
            CGContextFillRect(context, FloatRect(point2.x() - cornerWidth, point1.y(), cornerWidth, thickness));
        }
        strokeWidth -= 2 * cornerWidth;
        float patternWidth = dashedLinePatternWidthForStrokeWidth(strokeWidth);
        // Check if corner drawing sufficiently covers the line.
        if (strokeWidth <= patternWidth + 1)
            return;

        float patternOffset = dashedLinePatternOffsetForPatternAndStrokeWidth(patternWidth, strokeWidth);
        const CGFloat dashedLine[2] = { static_cast<CGFloat>(patternWidth), static_cast<CGFloat>(patternWidth) };
        CGContextSetLineDash(context, patternOffset, dashedLine, 2);
    }

    auto centeredPoints = centerLineAndCutOffCorners(isVerticalLine, cornerWidth, point1, point2);
    auto p1 = centeredPoints[0];
    auto p2 = centeredPoints[1];

    if (shouldAntialias()) {
#if PLATFORM(IOS_FAMILY)
        // Force antialiasing on for line patterns as they don't look good with it turned off (<rdar://problem/5459772>).
        CGContextSetShouldAntialias(context, strokeStyle == StrokeStyle::DottedStroke || strokeStyle == StrokeStyle::DashedStroke);
#else
        CGContextSetShouldAntialias(context, false);
#endif
    }
    CGContextBeginPath(context);
    CGContextMoveToPoint(context, p1.x(), p1.y());
    CGContextAddLineToPoint(context, p2.x(), p2.y());
    CGContextStrokePath(context);
    if (shouldAntialias())
        CGContextSetShouldAntialias(context, true);
}

void GraphicsContextCG::drawEllipse(const FloatRect& rect)
{
    Path path;
    path.addEllipseInRect(rect);
    drawPath(path);
}

void GraphicsContextCG::applyStrokePattern()
{
    RefPtr strokePattern = this->strokePattern();
    if (!strokePattern)
        return;

    CGContextRef cgContext = platformContext();
    AffineTransform userToBaseCTM = AffineTransform(getUserToBaseCTM(cgContext));

    auto platformPattern = strokePattern->createPlatformPattern(userToBaseCTM);
    if (!platformPattern)
        return;

    RetainPtr<CGColorSpaceRef> patternSpace = adoptCF(CGColorSpaceCreatePattern(0));
    CGContextSetStrokeColorSpace(cgContext, patternSpace.get());

    const CGFloat patternAlpha = 1;
    CGContextSetStrokePattern(cgContext, platformPattern.get(), &patternAlpha);
}

void GraphicsContextCG::applyFillPattern()
{
    RefPtr fillPattern = this->fillPattern();
    if (!fillPattern)
        return;

    CGContextRef cgContext = platformContext();
    AffineTransform userToBaseCTM = AffineTransform(getUserToBaseCTM(cgContext));

    auto platformPattern = fillPattern->createPlatformPattern(userToBaseCTM);
    if (!platformPattern)
        return;

    RetainPtr<CGColorSpaceRef> patternSpace = adoptCF(CGColorSpaceCreatePattern(nullptr));
    CGContextSetFillColorSpace(cgContext, patternSpace.get());

    const CGFloat patternAlpha = 1;
    CGContextSetFillPattern(cgContext, platformPattern.get(), &patternAlpha);
}

static inline bool calculateDrawingMode(const GraphicsContext& context, CGPathDrawingMode& mode)
{
    bool shouldFill = context.fillBrush().isVisible();
    bool shouldStroke = context.strokeBrush().isVisible() || (context.strokeStyle() != StrokeStyle::NoStroke);
    bool useEOFill = context.fillRule() == WindRule::EvenOdd;

    if (shouldFill) {
        if (shouldStroke) {
            if (useEOFill)
                mode = kCGPathEOFillStroke;
            else
                mode = kCGPathFillStroke;
        } else { // fill, no stroke
            if (useEOFill)
                mode = kCGPathEOFill;
            else
                mode = kCGPathFill;
        }
    } else {
        // Setting mode to kCGPathStroke even if shouldStroke is false. In that case, we return false and mode will not be used,
        // but the compiler will not complain about an uninitialized variable.
        mode = kCGPathStroke;
    }

    return shouldFill || shouldStroke;
}

void GraphicsContextCG::drawPath(const Path& path)
{
    if (path.isEmpty())
        return;

    CGContextRef context = platformContext();

    if (fillGradient() || strokeGradient()) {
        // We don't have any optimized way to fill & stroke a path using gradients
        // FIXME: Be smarter about this.
        fillPath(path);
        strokePath(path);
        return;
    }

    if (fillPattern())
        applyFillPattern();
    if (strokePattern())
        applyStrokePattern();

    CGPathDrawingMode drawingMode;
    if (calculateDrawingMode(*this, drawingMode))
        drawPathWithCGContext(context, drawingMode, path);
}

void GraphicsContextCG::fillPath(const Path& path)
{
    if (path.isEmpty())
        return;

    CGContextRef context = platformContext();

    if (RefPtr fillGradient = this->fillGradient()) {
        if (hasDropShadow()) {
            FloatRect rect = path.fastBoundingRect();
            FloatSize layerSize = getCTM().mapSize(rect.size());

            auto layer = adoptCF(CGLayerCreateWithContext(context, layerSize, 0));
            CGContextRef layerContext = CGLayerGetContext(layer.get());

            CGContextScaleCTM(layerContext, layerSize.width() / rect.width(), layerSize.height() / rect.height());
            CGContextTranslateCTM(layerContext, -rect.x(), -rect.y());
            setCGContextPath(layerContext, path);
            CGContextConcatCTM(layerContext, fillGradientSpaceTransform());

            if (fillRule() == WindRule::EvenOdd)
                CGContextEOClip(layerContext);
            else
                CGContextClip(layerContext);

            fillGradient->paint(layerContext);
            CGContextDrawLayerInRect(context, rect, layer.get());
        } else {
            setCGContextPath(context, path);
            CGContextStateSaver stateSaver(context);
            CGContextConcatCTM(context, fillGradientSpaceTransform());

            if (fillRule() == WindRule::EvenOdd)
                CGContextEOClip(context);
            else
                CGContextClip(context);

            fillGradient->paint(*this);
        }

        return;
    }

    if (fillPattern())
        applyFillPattern();

    drawPathWithCGContext(context, fillRule() == WindRule::EvenOdd ? kCGPathEOFill : kCGPathFill, path);
}

void GraphicsContextCG::strokePath(const Path& path)
{
    if (path.isEmpty())
        return;

    CGContextRef context = platformContext();

    if (RefPtr strokeGradient = this->strokeGradient()) {
        if (hasDropShadow()) {
            FloatRect rect = path.fastBoundingRect();
            float lineWidth = strokeThickness();
            float doubleLineWidth = lineWidth * 2;
            float adjustedWidth = ceilf(rect.width() + doubleLineWidth);
            float adjustedHeight = ceilf(rect.height() + doubleLineWidth);

            FloatSize layerSize = getCTM().mapSize(FloatSize(adjustedWidth, adjustedHeight));

            auto layer = adoptCF(CGLayerCreateWithContext(context, layerSize, 0));
            CGContextRef layerContext = CGLayerGetContext(layer.get());
            CGContextSetLineWidth(layerContext, lineWidth);

            // Compensate for the line width, otherwise the layer's top-left corner would be
            // aligned with the rect's top-left corner. This would result in leaving pixels out of
            // the layer on the left and top sides.
            float translationX = lineWidth - rect.x();
            float translationY = lineWidth - rect.y();
            CGContextScaleCTM(layerContext, layerSize.width() / adjustedWidth, layerSize.height() / adjustedHeight);
            CGContextTranslateCTM(layerContext, translationX, translationY);

            setCGContextPath(layerContext, path);
            CGContextReplacePathWithStrokedPath(layerContext);
            CGContextClip(layerContext);
            CGContextConcatCTM(layerContext, strokeGradientSpaceTransform());
            strokeGradient->paint(layerContext);

            float destinationX = roundf(rect.x() - lineWidth);
            float destinationY = roundf(rect.y() - lineWidth);
            CGContextDrawLayerInRect(context, CGRectMake(destinationX, destinationY, adjustedWidth, adjustedHeight), layer.get());
        } else {
            CGContextStateSaver stateSaver(context);
            setCGContextPath(context, path);
            CGContextReplacePathWithStrokedPath(context);
            CGContextClip(context);
            CGContextConcatCTM(context, strokeGradientSpaceTransform());
            strokeGradient->paint(*this);
        }
        return;
    }

    if (strokePattern())
        applyStrokePattern();

    if (auto line = path.singleDataLine()) {
        CGPoint cgPoints[2] { line->start(), line->end() };
        CGContextStrokeLineSegments(context, cgPoints, 2);
        return;
    }

    drawPathWithCGContext(context, kCGPathStroke, path);
}

void GraphicsContextCG::fillRect(const FloatRect& rect, RequiresClipToRect requiresClipToRect)
{
    CGContextRef context = platformContext();

    if (RefPtr fillGradient = this->fillGradient()) {
        fillRect(rect, *fillGradient, fillGradientSpaceTransform(), requiresClipToRect);
        return;
    }

    if (fillPattern())
        applyFillPattern();

    bool drawOwnShadow = canUseShadowBlur();
    CGContextStateSaver stateSaver(context, drawOwnShadow);
    if (drawOwnShadow) {
        clearCGShadow();

        const auto shadow = dropShadow();
        ASSERT(shadow);

        ShadowBlur contextShadow(*shadow, shadowsIgnoreTransforms());
        contextShadow.drawRectShadow(*this, FloatRoundedRect(rect));
    }

    CGContextFillRect(context, rect);
}

void GraphicsContextCG::fillRect(const FloatRect& rect, Gradient& gradient, const AffineTransform& gradientSpaceTransform, RequiresClipToRect requiresClipToRect)
{
    CGContextRef context = platformContext();

    CGContextStateSaver stateSaver(context);
    if (hasDropShadow()) {
        FloatSize layerSize = getCTM().mapSize(rect.size());

        auto layer = adoptCF(CGLayerCreateWithContext(context, layerSize, 0));
        CGContextRef layerContext = CGLayerGetContext(layer.get());

        CGContextScaleCTM(layerContext, layerSize.width() / rect.width(), layerSize.height() / rect.height());
        CGContextTranslateCTM(layerContext, -rect.x(), -rect.y());
        CGContextAddRect(layerContext, rect);
        CGContextClip(layerContext);

        CGContextConcatCTM(layerContext, gradientSpaceTransform);
        gradient.paint(layerContext);
        CGContextDrawLayerInRect(context, rect, layer.get());
    } else {
        if (requiresClipToRect == RequiresClipToRect::Yes)
            CGContextClipToRect(context, rect);

        CGContextConcatCTM(context, gradientSpaceTransform);
        gradient.paint(*this);
    }
}

void GraphicsContextCG::fillRect(const FloatRect& rect, const Color& color)
{
    CGContextRef context = platformContext();
    Color oldFillColor = fillColor();

    if (oldFillColor != color)
        setCGFillColor(context, color, colorSpace());

    bool drawOwnShadow = canUseShadowBlur();
    CGContextStateSaver stateSaver(context, drawOwnShadow);
    if (drawOwnShadow) {
        clearCGShadow();

        const auto shadow = dropShadow();
        ASSERT(shadow);

        ShadowBlur contextShadow(*shadow, shadowsIgnoreTransforms());
        contextShadow.drawRectShadow(*this, FloatRoundedRect(rect));
    }

    CGContextFillRect(context, rect);

    if (drawOwnShadow)
        stateSaver.restore();

    if (oldFillColor != color)
        setCGFillColor(context, oldFillColor, colorSpace());
}

void GraphicsContextCG::fillRoundedRectImpl(const FloatRoundedRect& rect, const Color& color)
{
    CGContextRef context = platformContext();
    Color oldFillColor = fillColor();

    if (oldFillColor != color)
        setCGFillColor(context, color, colorSpace());

    bool drawOwnShadow = canUseShadowBlur();
    CGContextStateSaver stateSaver(context, drawOwnShadow);
    if (drawOwnShadow) {
        clearCGShadow();

        const auto shadow = dropShadow();
        ASSERT(shadow);

        ShadowBlur contextShadow(*shadow, shadowsIgnoreTransforms());
        contextShadow.drawRectShadow(*this, rect);
    }

    const FloatRect& r = rect.rect();
    const FloatRoundedRect::Radii& radii = rect.radii();
    bool equalWidths = (radii.topLeft().width() == radii.topRight().width() && radii.topRight().width() == radii.bottomLeft().width() && radii.bottomLeft().width() == radii.bottomRight().width());
    bool equalHeights = (radii.topLeft().height() == radii.bottomLeft().height() && radii.bottomLeft().height() == radii.topRight().height() && radii.topRight().height() == radii.bottomRight().height());
    bool hasCustomFill = fillGradient() || fillPattern();
    if (!hasCustomFill && equalWidths && equalHeights && radii.topLeft().width() * 2 == r.width() && radii.topLeft().height() * 2 == r.height())
        CGContextFillEllipseInRect(context, r);
    else {
        Path path;
        path.addRoundedRect(rect);
        fillPath(path);
    }

    if (drawOwnShadow)
        stateSaver.restore();

    if (oldFillColor != color)
        setCGFillColor(context, oldFillColor, colorSpace());
}

void GraphicsContextCG::fillRectWithRoundedHole(const FloatRect& rect, const FloatRoundedRect& roundedHoleRect, const Color& color)
{
    CGContextRef context = platformContext();

    Path path;
    path.addRect(rect);

    if (!roundedHoleRect.radii().isZero())
        path.addRoundedRect(roundedHoleRect);
    else
        path.addRect(roundedHoleRect.rect());

    WindRule oldFillRule = fillRule();
    Color oldFillColor = fillColor();

    setFillRule(WindRule::EvenOdd);
    setFillColor(color);

    // fillRectWithRoundedHole() assumes that the edges of rect are clipped out, so we only care about shadows cast around inside the hole.
    bool drawOwnShadow = canUseShadowBlur();
    CGContextStateSaver stateSaver(context, drawOwnShadow);
    if (drawOwnShadow) {
        clearCGShadow();

        const auto shadow = dropShadow();
        ASSERT(shadow);

        ShadowBlur contextShadow(*shadow, shadowsIgnoreTransforms());
        contextShadow.drawInsetShadow(*this, rect, roundedHoleRect);
    }

    fillPath(path);

    if (drawOwnShadow)
        stateSaver.restore();

    setFillRule(oldFillRule);
    setFillColor(oldFillColor);
}

void GraphicsContextCG::resetClip()
{
    CGContextResetClip(platformContext());
}

void GraphicsContextCG::clip(const FloatRect& rect)
{
    CGContextClipToRect(platformContext(), rect);
}

void GraphicsContextCG::clipOut(const FloatRect& rect)
{
    // FIXME: Using CGRectInfinite is much faster than getting the clip bounding box. However, due
    // to <rdar://problem/12584492>, CGRectInfinite can't be used with an accelerated context that
    // has certain transforms that aren't just a translation or a scale. And due to <rdar://problem/14634453>
    // we cannot use it in for a printing context either.
    CGContextRef context = platformContext();
    const AffineTransform& ctm = getCTM();
    bool canUseCGRectInfinite = CGContextGetType(context) != kCGContextTypePDF && (renderingMode() == RenderingMode::Unaccelerated || (!ctm.b() && !ctm.c()));
    CGRect rects[2] = { canUseCGRectInfinite ? CGRectInfinite : CGContextGetClipBoundingBox(context), rect };
    CGContextBeginPath(context);
    CGContextAddRects(context, rects, 2);
    CGContextEOClip(context);
}

void GraphicsContextCG::clipOut(const Path& path)
{
    CGContextRef context = platformContext();
    CGContextBeginPath(context);
    CGContextAddRect(context, CGContextGetClipBoundingBox(context));
    if (!path.isEmpty())
        addToCGContextPath(context, path);
    CGContextEOClip(context);
}

void GraphicsContextCG::clipPath(const Path& path, WindRule clipRule)
{
    CGContextRef context = platformContext();
    if (path.isEmpty())
        CGContextClipToRect(context, CGRectZero);
    else {
        setCGContextPath(context, path);
        if (clipRule == WindRule::EvenOdd)
            CGContextEOClip(context);
        else
            CGContextClip(context);
    }
}

void GraphicsContextCG::clipToImageBuffer(ImageBuffer& imageBuffer, const FloatRect& destRect)
{
    auto nativeImage = imageBuffer.createNativeImageReference();
    if (!nativeImage)
        return;

    // FIXME: This image needs to be grayscale to be used as an alpha mask here.
    CGContextRef context = platformContext();
    CGContextTranslateCTM(context, destRect.x(), destRect.maxY());
    CGContextScaleCTM(context, 1, -1);
    CGContextClipToRect(context, { { }, destRect.size() });
    CGContextClipToMask(context, { { }, destRect.size() }, nativeImage->platformImage().get());
    CGContextScaleCTM(context, 1, -1);
    CGContextTranslateCTM(context, -destRect.x(), -destRect.maxY());
}

IntRect GraphicsContextCG::clipBounds() const
{
    return enclosingIntRect(CGContextGetClipBoundingBox(platformContext()));
}

void GraphicsContextCG::beginTransparencyLayer(float opacity)
{
    GraphicsContext::beginTransparencyLayer(opacity);

    save(GraphicsContextState::Purpose::TransparencyLayer);

    CGContextRef context = platformContext();
    CGContextSetAlpha(context, opacity);
    CGContextBeginTransparencyLayer(context, 0);

    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::beginTransparencyLayer(CompositeOperator, BlendMode)
{
    // Passing state().alpha() to beginTransparencyLayer(opacity) will
    // preserve the current global alpha.
    beginTransparencyLayer(state().alpha());
}

void GraphicsContextCG::endTransparencyLayer()
{
    GraphicsContext::endTransparencyLayer();

    CGContextRef context = platformContext();
    CGContextEndTransparencyLayer(context);

    restore(GraphicsContextState::Purpose::TransparencyLayer);
}

void GraphicsContextCG::setCGShadow(const std::optional<GraphicsDropShadow>& shadow, bool shadowsIgnoreTransforms)
{
    if (!shadow || !shadow->color.isValid() || (shadow->offset.isZero() && !shadow->radius)) {
        clearCGShadow();
        return;
    }

    CGFloat xOffset = shadow->offset.width();
    CGFloat yOffset = shadow->offset.height();
    CGFloat blurRadius = shadow->radius;
    CGContextRef context = platformContext();

    if (!shadowsIgnoreTransforms) {
        CGAffineTransform userToBaseCTM = getUserToBaseCTM(context);

        CGFloat A = userToBaseCTM.a * userToBaseCTM.a + userToBaseCTM.b * userToBaseCTM.b;
        CGFloat B = userToBaseCTM.a * userToBaseCTM.c + userToBaseCTM.b * userToBaseCTM.d;
        CGFloat C = B;
        CGFloat D = userToBaseCTM.c * userToBaseCTM.c + userToBaseCTM.d * userToBaseCTM.d;

        CGFloat smallEigenvalue = narrowPrecisionToCGFloat(sqrt(0.5 * ((A + D) - sqrt(4 * B * C + (A - D) * (A - D)))));

        blurRadius *= smallEigenvalue;

        CGSize offsetInBaseSpace = CGSizeApplyAffineTransform(shadow->offset, userToBaseCTM);

        xOffset = offsetInBaseSpace.width;
        yOffset = offsetInBaseSpace.height;
    }

    // Extreme "blur" values can make text drawing crash or take crazy long times, so clamp
    blurRadius = std::min(blurRadius, narrowPrecisionToCGFloat(1000.0));

    CGContextSetAlpha(context, shadow->opacity);

    auto style = adoptCF(CGStyleCreateShadow2(CGSizeMake(xOffset, yOffset), blurRadius, cachedSDRCGColorForColorspace(shadow->color, colorSpace()).get()));
    CGContextSetStyle(context, style.get());
}

void GraphicsContextCG::clearCGShadow()
{
    CGContextSetStyle(platformContext(), nullptr);
}

void GraphicsContextCG::setCGStyle(const std::optional<GraphicsStyle>& style, bool shadowsIgnoreTransforms)
{
    auto context = platformContext();

    if (!style) {
        CGContextSetStyle(context, nullptr);
        return;
    }

    WTF::switchOn(*style,
        [&] (const GraphicsDropShadow& dropShadow) {
            setCGShadow(dropShadow, shadowsIgnoreTransforms);
        },
        [&] (const GraphicsGaussianBlur& gaussianBlur) {
#if HAVE(CGSTYLE_COLORMATRIX_BLUR)
            ASSERT(gaussianBlur.radius.width() == gaussianBlur.radius.height());

            CGGaussianBlurStyle gaussianBlurStyle = { 1, gaussianBlur.radius.width() };
            auto style = adoptCF(CGStyleCreateGaussianBlur(&gaussianBlurStyle));
            CGContextSetStyle(context, style.get());
#else
            ASSERT_NOT_REACHED();
            UNUSED_PARAM(gaussianBlur);
#endif
        },
        [&] (const GraphicsColorMatrix& colorMatrix) {
#if HAVE(CGSTYLE_COLORMATRIX_BLUR)
            CGColorMatrixStyle cgColorMatrix = { 1, { 0 } };
            for (auto [dst, src] : zippedRange(cgColorMatrix.matrix, colorMatrix.values))
                dst = src;
            auto style = adoptCF(CGStyleCreateColorMatrix(&cgColorMatrix));
            CGContextSetStyle(context, style.get());
#else
            ASSERT_NOT_REACHED();
            UNUSED_PARAM(colorMatrix);
#endif
        }
    );
}

void GraphicsContextCG::didUpdateState(GraphicsContextState& state)
{
    if (!state.changes())
        return;

    auto context = platformContext();

    for (auto change : state.changes()) {
        switch (change) {
        case GraphicsContextState::Change::FillBrush:
            setCGFillColor(context, state.fillBrush().color(), colorSpace());
            break;

        case GraphicsContextState::Change::StrokeThickness:
            CGContextSetLineWidth(context, std::max(state.strokeThickness(), 0.f));
            break;

        case GraphicsContextState::Change::StrokeBrush:
            CGContextSetStrokeColorWithColor(context, cachedSDRCGColorForColorspace(state.strokeBrush().color(), colorSpace()).get());
            break;

        case GraphicsContextState::Change::CompositeMode:
            setCGBlendMode(context, state.compositeMode().operation, state.compositeMode().blendMode);
            break;

        case GraphicsContextState::Change::DropShadow:
            setCGShadow(state.dropShadow(), state.shadowsIgnoreTransforms());
            break;

        case GraphicsContextState::Change::Style:
            setCGStyle(state.style(), state.shadowsIgnoreTransforms());
            break;

        case GraphicsContextState::Change::Alpha:
            CGContextSetAlpha(context, state.alpha());
            break;

        case GraphicsContextState::Change::ImageInterpolationQuality:
            CGContextSetInterpolationQuality(context, toCGInterpolationQuality(state.imageInterpolationQuality()));
            break;

        case GraphicsContextState::Change::TextDrawingMode:
            CGContextSetTextDrawingMode(context, cgTextDrawingMode(state.textDrawingMode()));
            break;

        case GraphicsContextState::Change::ShouldAntialias:
            CGContextSetShouldAntialias(context, state.shouldAntialias());
            break;

        case GraphicsContextState::Change::ShouldSmoothFonts:
            CGContextSetShouldSmoothFonts(context, state.shouldSmoothFonts());
            break;

        default:
            break;
        }
    }

    state.didApplyChanges();
}

void GraphicsContextCG::setMiterLimit(float limit)
{
    CGContextSetMiterLimit(platformContext(), limit);
}

void GraphicsContextCG::clearRect(const FloatRect& r)
{
    CGContextClearRect(platformContext(), r);
}

void GraphicsContextCG::strokeRect(const FloatRect& rect, float lineWidth)
{
    CGContextRef context = platformContext();

    if (RefPtr strokeGradient = this->strokeGradient()) {
        if (hasDropShadow()) {
            const float doubleLineWidth = lineWidth * 2;
            float adjustedWidth = ceilf(rect.width() + doubleLineWidth);
            float adjustedHeight = ceilf(rect.height() + doubleLineWidth);
            FloatSize layerSize = getCTM().mapSize(FloatSize(adjustedWidth, adjustedHeight));

            auto layer = adoptCF(CGLayerCreateWithContext(context, layerSize, 0));

            CGContextRef layerContext = CGLayerGetContext(layer.get());
            CGContextSetLineWidth(layerContext, lineWidth);

            // Compensate for the line width, otherwise the layer's top-left corner would be
            // aligned with the rect's top-left corner. This would result in leaving pixels out of
            // the layer on the left and top sides.
            const float translationX = lineWidth - rect.x();
            const float translationY = lineWidth - rect.y();
            CGContextScaleCTM(layerContext, layerSize.width() / adjustedWidth, layerSize.height() / adjustedHeight);
            CGContextTranslateCTM(layerContext, translationX, translationY);

            CGContextAddRect(layerContext, rect);
            CGContextReplacePathWithStrokedPath(layerContext);
            CGContextClip(layerContext);
            CGContextConcatCTM(layerContext, strokeGradientSpaceTransform());
            strokeGradient->paint(layerContext);

            const float destinationX = roundf(rect.x() - lineWidth);
            const float destinationY = roundf(rect.y() - lineWidth);
            CGContextDrawLayerInRect(context, CGRectMake(destinationX, destinationY, adjustedWidth, adjustedHeight), layer.get());
        } else {
            CGContextStateSaver stateSaver(context);
            setStrokeThickness(lineWidth);
            CGContextAddRect(context, rect);
            CGContextReplacePathWithStrokedPath(context);
            CGContextClip(context);
            CGContextConcatCTM(context, strokeGradientSpaceTransform());
            strokeGradient->paint(*this);
        }
        return;
    }

    if (strokePattern())
        applyStrokePattern();

    // Using CGContextAddRect and CGContextStrokePath to stroke rect rather than
    // convenience functions (CGContextStrokeRect/CGContextStrokeRectWithWidth).
    // The convenience functions currently (in at least OSX 10.9.4) fail to
    // apply some attributes of the graphics state in certain cases
    // (as identified in https://bugs.webkit.org/show_bug.cgi?id=132948)
    CGContextStateSaver stateSaver(context);
    setStrokeThickness(lineWidth);

    CGContextAddRect(context, rect);
    CGContextStrokePath(context);
}

void GraphicsContextCG::setLineCap(LineCap cap)
{
    switch (cap) {
    case LineCap::Butt:
        CGContextSetLineCap(platformContext(), kCGLineCapButt);
        break;
    case LineCap::Round:
        CGContextSetLineCap(platformContext(), kCGLineCapRound);
        break;
    case LineCap::Square:
        CGContextSetLineCap(platformContext(), kCGLineCapSquare);
        break;
    }
}

void GraphicsContextCG::setLineDash(const DashArray& dashes, float dashOffset)
{
    if (dashOffset < 0) {
        float length = 0;
        for (size_t i = 0; i < dashes.size(); ++i)
            length += static_cast<float>(dashes[i]);
        if (length)
            dashOffset = fmod(dashOffset, length) + length;
    }
    auto dashesSpan = dashes.span();
    CGContextSetLineDash(platformContext(), dashOffset, dashesSpan.data(), dashesSpan.size());
}

void GraphicsContextCG::setLineJoin(LineJoin join)
{
    switch (join) {
    case LineJoin::Miter:
        CGContextSetLineJoin(platformContext(), kCGLineJoinMiter);
        break;
    case LineJoin::Round:
        CGContextSetLineJoin(platformContext(), kCGLineJoinRound);
        break;
    case LineJoin::Bevel:
        CGContextSetLineJoin(platformContext(), kCGLineJoinBevel);
        break;
    }
}

void GraphicsContextCG::scale(const FloatSize& size)
{
    CGContextScaleCTM(platformContext(), size.width(), size.height());
    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::rotate(float angle)
{
    CGContextRotateCTM(platformContext(), angle);
    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::translate(float x, float y)
{
    CGContextTranslateCTM(platformContext(), x, y);
    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::concatCTM(const AffineTransform& transform)
{
    CGContextConcatCTM(platformContext(), transform);
    m_userToDeviceTransformKnownToBeIdentity = false;
}

void GraphicsContextCG::setCTM(const AffineTransform& transform)
{
    CGContextSetCTM(platformContext(), transform);
    m_userToDeviceTransformKnownToBeIdentity = false;
}

AffineTransform GraphicsContextCG::getCTM(IncludeDeviceScale includeScale) const
{
    // The CTM usually includes the deviceScaleFactor except in WebKit 1 when the
    // content is non-composited, since the scale factor is integrated at a lower
    // level. To guarantee the deviceScale is included, we can use this CG API.
    if (includeScale == DefinitelyIncludeDeviceScale)
        return CGContextGetUserSpaceToDeviceSpaceTransform(platformContext());

    return CGContextGetCTM(platformContext());
}

FloatRect GraphicsContextCG::roundToDevicePixels(const FloatRect& rect) const
{
    CGAffineTransform deviceMatrix;
    if (!m_userToDeviceTransformKnownToBeIdentity) {
        deviceMatrix = CGContextGetUserSpaceToDeviceSpaceTransform(contextForState());
        if (CGAffineTransformIsIdentity(deviceMatrix))
            m_userToDeviceTransformKnownToBeIdentity = true;
    }
    if (m_userToDeviceTransformKnownToBeIdentity)
        return roundedIntRect(rect);
    return cgRoundToDevicePixelsNonIdentity(deviceMatrix, rect);
}

void GraphicsContextCG::drawLinesForText(const FloatPoint& origin, float thickness, std::span<const FloatSegment> lineSegments, bool isPrinting, bool doubleLines, StrokeStyle strokeStyle)
{
    auto [rects, color] = computeRectsAndStrokeColorForLinesForText(origin, thickness, lineSegments, isPrinting, doubleLines, strokeStyle);
    if (rects.isEmpty())
        return;
    bool changeFillColor = fillColor() != color;
    if (changeFillColor)
        setCGFillColor(platformContext(), color, colorSpace());
    CGContextFillRects(platformContext(), rects.span().data(), rects.size());
    if (changeFillColor)
        setCGFillColor(platformContext(), fillColor(), colorSpace());
}

void GraphicsContextCG::setURLForRect(const URL& link, const FloatRect& destRect)
{
    RetainPtr<CFURLRef> urlRef = link.createCFURL();
    if (!urlRef)
        return;

    CGContextRef context = platformContext();

    FloatRect rect = destRect;
    // Get the bounding box to handle clipping.
    rect.intersect(CGContextGetClipBoundingBox(context));

    CGPDFContextSetURLForRect(context, urlRef.get(), CGRectApplyAffineTransform(rect, CGContextGetCTM(context)));
}

bool GraphicsContextCG::isCALayerContext() const
{
    return m_isLayerCGContext;
}

RenderingMode GraphicsContextCG::renderingMode() const
{
    return m_renderingMode;
}

void GraphicsContextCG::applyDeviceScaleFactor(float deviceScaleFactor)
{
    GraphicsContext::applyDeviceScaleFactor(deviceScaleFactor);

    // CoreGraphics expects the base CTM of a HiDPI context to have the scale factor applied to it.
    // Failing to change the base level CTM will cause certain CG features, such as focus rings,
    // to draw with a scale factor of 1 rather than the actual scale factor.
    CGContextSetBaseCTM(platformContext(), CGAffineTransformScale(CGContextGetBaseCTM(platformContext()), deviceScaleFactor, deviceScaleFactor));
}

void GraphicsContextCG::fillEllipse(const FloatRect& ellipse)
{
    // CGContextFillEllipseInRect only supports solid colors.
    if (fillGradient() || fillPattern()) {
        fillEllipseAsPath(ellipse);
        return;
    }

    CGContextRef context = platformContext();
    CGContextFillEllipseInRect(context, ellipse);
}

void GraphicsContextCG::strokeEllipse(const FloatRect& ellipse)
{
    // CGContextStrokeEllipseInRect only supports solid colors.
    if (strokeGradient() || strokePattern()) {
        strokeEllipseAsPath(ellipse);
        return;
    }

    CGContextRef context = platformContext();
    CGContextStrokeEllipseInRect(context, ellipse);
}

void GraphicsContextCG::beginPage(const IntSize& pageSize)
{
    CGContextRef context = platformContext();

    if (CGContextGetType(context) != kCGContextTypePDF) {
        ASSERT_NOT_REACHED();
        return;
    }

    auto mediaBox = CGRectMake(0, 0, pageSize.width(), pageSize.height());
    auto mediaBoxData = adoptCF(CFDataCreate(nullptr, (const UInt8 *)&mediaBox, sizeof(CGRect)));

    const void* key = kCGPDFContextMediaBox;
    const void* value = mediaBoxData.get();
    auto pageInfo = adoptCF(CFDictionaryCreate(kCFAllocatorDefault, &key, &value, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

    CGPDFContextBeginPage(context, pageInfo.get());
}

void GraphicsContextCG::endPage()
{
    CGContextRef context = platformContext();

    if (CGContextGetType(context) != kCGContextTypePDF) {
        ASSERT_NOT_REACHED();
        return;
    }

    CGPDFContextEndPage(context);
}

bool GraphicsContextCG::supportsInternalLinks() const
{
    return true;
}

void GraphicsContextCG::setDestinationForRect(const String& name, const FloatRect& destRect)
{
    CGContextRef context = platformContext();

    FloatRect rect = destRect;
    rect.intersect(CGContextGetClipBoundingBox(context));

    CGRect transformedRect = CGRectApplyAffineTransform(rect, CGContextGetCTM(context));
    CGPDFContextSetDestinationForRect(context, name.createCFString().get(), transformedRect);
}

void GraphicsContextCG::addDestinationAtPoint(const String& name, const FloatPoint& position)
{
    CGContextRef context = platformContext();
    CGPoint transformedPoint = CGPointApplyAffineTransform(position, CGContextGetCTM(context));
    CGPDFContextAddDestinationAtPoint(context, name.createCFString().get(), transformedPoint);
}

bool GraphicsContextCG::canUseShadowBlur() const
{
    return (renderingMode() == RenderingMode::Unaccelerated) && hasBlurredDropShadow() && !m_state.shadowsIgnoreTransforms();
}

bool GraphicsContextCG::consumeHasDrawn()
{
    bool hasDrawn = m_hasDrawn;
    m_hasDrawn = false;
    return hasDrawn;
}

#if HAVE(SUPPORT_HDR_DISPLAY)
void GraphicsContextCG::setMaxEDRHeadroom(std::optional<float> headroom)
{
    m_maxEDRHeadroom = headroom;
}
#endif


}

#endif
