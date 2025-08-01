/*
 * Copyright (C) 2013 Adobe Systems Incorporated. All rights reserved.
 * Copyright (C) 2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FloatRoundedRect.h"

#include "Path.h"
#include <algorithm>
#include <numbers>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(FloatRoundedRect);
WTF_MAKE_TZONE_ALLOCATED_IMPL(FloatRoundedRect::Radii);

FloatRoundedRect::FloatRoundedRect(const LayoutRoundedRect& rect)
    : m_rect(rect.rect())
    , m_radii(rect.radii())
{
}

FloatRoundedRect::FloatRoundedRect(float x, float y, float width, float height)
    : m_rect(x, y, width, height)
{
}

FloatRoundedRect::FloatRoundedRect(const FloatRect& rect, const Radii& radii)
    : m_rect(rect)
    , m_radii(radii)
{
}

FloatRoundedRect::FloatRoundedRect(const FloatRect& rect, const FloatSize& topLeft, const FloatSize& topRight, const FloatSize& bottomLeft, const FloatSize& bottomRight)
    : m_rect(rect)
    , m_radii(topLeft, topRight, bottomLeft, bottomRight)
{
}

bool FloatRoundedRect::Radii::isZero() const
{
    return m_topLeft.isZero() && m_topRight.isZero() && m_bottomLeft.isZero() && m_bottomRight.isZero();
}

bool FloatRoundedRect::Radii::hasEvenCorners() const
{
    return areEssentiallyEqual(m_topLeft, m_topRight)
        && areEssentiallyEqual(m_topLeft, m_bottomLeft)
        && areEssentiallyEqual(m_topLeft, m_bottomRight);
}

bool FloatRoundedRect::Radii::isUniformCornerRadius() const
{
    return WTF::areEssentiallyEqual(m_topLeft.width(), m_topLeft.height()) && hasEvenCorners();
}

void FloatRoundedRect::Radii::scale(float factor)
{
    scale(factor, factor);
}

void FloatRoundedRect::Radii::scale(float horizontalFactor, float verticalFactor)
{
    if (horizontalFactor == 1 && verticalFactor == 1)
        return;

    // If either radius on a corner becomes zero, reset both radii on that corner.
    m_topLeft.scale(horizontalFactor, verticalFactor);
    if (!m_topLeft.width() || !m_topLeft.height())
        m_topLeft = FloatSize();
    m_topRight.scale(horizontalFactor, verticalFactor);
    if (!m_topRight.width() || !m_topRight.height())
        m_topRight = FloatSize();
    m_bottomLeft.scale(horizontalFactor, verticalFactor);
    if (!m_bottomLeft.width() || !m_bottomLeft.height())
        m_bottomLeft = FloatSize();
    m_bottomRight.scale(horizontalFactor, verticalFactor);
    if (!m_bottomRight.width() || !m_bottomRight.height())
        m_bottomRight = FloatSize();
}

void FloatRoundedRect::Radii::expand(float topWidth, float bottomWidth, float leftWidth, float rightWidth)
{
    if (m_topLeft.width() > 0 && m_topLeft.height() > 0) {
        m_topLeft.setWidth(std::max<float>(0, m_topLeft.width() + leftWidth));
        m_topLeft.setHeight(std::max<float>(0, m_topLeft.height() + topWidth));
    }
    if (m_topRight.width() > 0 && m_topRight.height() > 0) {
        m_topRight.setWidth(std::max<float>(0, m_topRight.width() + rightWidth));
        m_topRight.setHeight(std::max<float>(0, m_topRight.height() + topWidth));
    }
    if (m_bottomLeft.width() > 0 && m_bottomLeft.height() > 0) {
        m_bottomLeft.setWidth(std::max<float>(0, m_bottomLeft.width() + leftWidth));
        m_bottomLeft.setHeight(std::max<float>(0, m_bottomLeft.height() + bottomWidth));
    }
    if (m_bottomRight.width() > 0 && m_bottomRight.height() > 0) {
        m_bottomRight.setWidth(std::max<float>(0, m_bottomRight.width() + rightWidth));
        m_bottomRight.setHeight(std::max<float>(0, m_bottomRight.height() + bottomWidth));
    }
}

void FloatRoundedRect::Radii::expandEvenIfZero(float size)
{
    auto expand = [&](auto& corner) {
        corner.setWidth(std::max(0.f, corner.width() + size));
        corner.setHeight(std::max(0.f, corner.height() + size));
    };

    expand(m_topLeft);
    expand(m_topRight);
    expand(m_bottomLeft);
    expand(m_bottomRight);
}

static inline float cornerRectIntercept(float y, const FloatRect& cornerRect)
{
    ASSERT(cornerRect.height() > 0);
    return cornerRect.width() * sqrt(1 - (y * y) / (cornerRect.height() * cornerRect.height()));
}

bool FloatRoundedRect::xInterceptsAtY(float y, float& minXIntercept, float& maxXIntercept) const
{
    if (y < rect().y() || y >  rect().maxY())
        return false;

    if (!isRounded()) {
        minXIntercept = rect().x();
        maxXIntercept = rect().maxX();
        return true;
    }

    const FloatRect& topLeftRect = topLeftCorner();
    const FloatRect& bottomLeftRect = bottomLeftCorner();

    if (!topLeftRect.isEmpty() && y >= topLeftRect.y() && y < topLeftRect.maxY())
        minXIntercept = topLeftRect.maxX() - cornerRectIntercept(topLeftRect.maxY() - y, topLeftRect);
    else if (!bottomLeftRect.isEmpty() && y >= bottomLeftRect.y() && y <= bottomLeftRect.maxY())
        minXIntercept =  bottomLeftRect.maxX() - cornerRectIntercept(y - bottomLeftRect.y(), bottomLeftRect);
    else
        minXIntercept = m_rect.x();

    const FloatRect& topRightRect = topRightCorner();
    const FloatRect& bottomRightRect = bottomRightCorner();

    if (!topRightRect.isEmpty() && y >= topRightRect.y() && y <= topRightRect.maxY())
        maxXIntercept = topRightRect.x() + cornerRectIntercept(topRightRect.maxY() - y, topRightRect);
    else if (!bottomRightRect.isEmpty() && y >= bottomRightRect.y() && y <= bottomRightRect.maxY())
        maxXIntercept = bottomRightRect.x() + cornerRectIntercept(y - bottomRightRect.y(), bottomRightRect);
    else
        maxXIntercept = m_rect.maxX();

    return true;
}

bool FloatRoundedRect::isRenderable() const
{
    return m_radii.topLeft().width() >= 0 && m_radii.topLeft().height() >= 0
        && m_radii.bottomLeft().width() >= 0 && m_radii.bottomLeft().height() >= 0
        && m_radii.topRight().width() >= 0 && m_radii.topRight().height() >= 0
        && m_radii.bottomRight().width() >= 0 && m_radii.bottomRight().height() >= 0
        && m_radii.topLeft().width() + m_radii.topRight().width() <= m_rect.width()
        && m_radii.bottomLeft().width() + m_radii.bottomRight().width() <= m_rect.width()
        && m_radii.topLeft().height() + m_radii.bottomLeft().height() <= m_rect.height()
        && m_radii.topRight().height() + m_radii.bottomRight().height() <= m_rect.height();
}

void FloatRoundedRect::inflateWithRadii(float size)
{
    FloatRect old = m_rect;

    m_rect.inflate(size);
    // Considering the inflation factor of shorter size to scale the radii seems appropriate here
    float factor;
    if (m_rect.width() < m_rect.height())
        factor = old.width() ? m_rect.width() / old.width() : 0;
    else
        factor = old.height() ? m_rect.height() / old.height() : 0;

    m_radii.scale(factor);
}

void FloatRoundedRect::adjustRadii()
{
    float maxRadiusWidth = std::max(m_radii.topLeft().width() + m_radii.topRight().width(), m_radii.bottomLeft().width() + m_radii.bottomRight().width());
    float maxRadiusHeight = std::max(m_radii.topLeft().height() + m_radii.bottomLeft().height(), m_radii.topRight().height() + m_radii.bottomRight().height());

    if (maxRadiusWidth <= 0 || maxRadiusHeight <= 0) {
        m_radii.scale(0.0f);
        return;
    }
    float widthRatio = m_rect.width() / maxRadiusWidth;
    float heightRatio = m_rect.height() / maxRadiusHeight;
    m_radii.scale(widthRatio < heightRatio ? widthRatio : heightRatio);
}

// This is conservative; it does not test intrusion into the corner rects.
bool FloatRoundedRect::intersectionIsRectangular(const FloatRect& rect) const
{
    return !(rect.intersects(topLeftCorner()) || rect.intersects(topRightCorner()) || rect.intersects(bottomLeftCorner()) || rect.intersects(bottomRightCorner()));
}

Path FloatRoundedRect::path() const
{
    Path path;
    path.addRoundedRect(*this);
    return path;
}

Region approximateAsRegion(const FloatRoundedRect& roundedRect, unsigned stepLength)
{
    Region region;

    if (roundedRect.isEmpty())
        return region;

    auto rect = LayoutRect(roundedRect.rect());
    region.unite(enclosingIntRect(rect));

    if (!roundedRect.isRounded())
        return region;

    auto& radii = roundedRect.radii();

    auto makeIntRect = [] (LayoutPoint a, LayoutPoint b) {
        return enclosingIntRect(LayoutRect {
            LayoutPoint { std::min(a.x(), b.x()), std::min(a.y(), b.y()) },
            LayoutPoint { std::max(a.x(), b.x()), std::max(a.y(), b.y()) }
        });
    };

    auto subtractCornerRects = [&] (LayoutPoint corner, LayoutPoint ellipsisCenter, FloatSize axes, double fromAngle) {
        double toAngle = fromAngle + std::numbers::pi / 2;

        // Substract more rects for longer, more rounded arcs.
        auto arcLengthFactor = roundToInt(std::min(axes.width(), axes.height()));
        auto count = (arcLengthFactor + (stepLength / 2)) / stepLength;

        constexpr auto maximumCount = 20u;
        count = std::min(maximumCount, count);

        for (auto i = 0u; i < count; ++i) {
            auto angle = fromAngle + (i + 1) * (toAngle - fromAngle) / (count + 1);
            auto ellipsisPoint = LayoutPoint { axes.width() * cos(angle), axes.height() * sin(angle) };
            auto cornerRect = makeIntRect(corner, ellipsisCenter + ellipsisPoint);
            region.subtract(cornerRect);
        }
    };

    {
        auto corner = rect.maxXMaxYCorner();
        auto axes = radii.bottomRight();
        auto ellipsisCenter = LayoutPoint(corner.x() - axes.width(), corner.y() - axes.height());
        subtractCornerRects(corner, ellipsisCenter, axes, 0);
    }

    {
        auto corner = rect.minXMaxYCorner();
        auto axes = radii.bottomLeft();
        auto ellipsisCenter = LayoutPoint(corner.x() + axes.width(), corner.y() - axes.height());
        subtractCornerRects(corner, ellipsisCenter, axes, std::numbers::pi / 2);
    }

    {
        auto corner = rect.minXMinYCorner();
        auto axes = radii.topLeft();
        auto ellipsisCenter = LayoutPoint(corner.x() + axes.width(), corner.y() + axes.height());
        subtractCornerRects(corner, ellipsisCenter, axes, std::numbers::pi);
    }

    {
        auto corner = rect.maxXMinYCorner();
        auto axes = radii.topRight();
        auto ellipsisCenter = LayoutPoint(corner.x() - axes.width(), corner.y() + axes.height());
        subtractCornerRects(corner, ellipsisCenter, axes, std::numbers::pi * 3 / 2);
    }

    return region;
}

TextStream& operator<<(TextStream& ts, const FloatRoundedRect& roundedRect)
{
    ts << roundedRect.rect();
    ts.dumpProperty("top-left"_s, roundedRect.radii().topLeft());
    ts.dumpProperty("top-right"_s, roundedRect.radii().topRight());
    ts.dumpProperty("bottom-left"_s, roundedRect.radii().bottomLeft());
    ts.dumpProperty("bottom-right"_s, roundedRect.radii().bottomRight());
    return ts;
}

} // namespace WebCore
