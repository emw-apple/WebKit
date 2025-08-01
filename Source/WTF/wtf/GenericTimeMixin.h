/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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

#include <wtf/Seconds.h>

namespace WTF {

template<typename DerivedTime>
class GenericTimeMixin {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(GenericTimeMixin);
public:
    // Call this if you know for sure that the double represents the time according to the
    // same time source as DerivedTime. It must be in seconds.
    static constexpr DerivedTime fromRawSeconds(double value)
    {
        return DerivedTime(value);
    }

    static constexpr DerivedTime infinity() { return fromRawSeconds(std::numeric_limits<double>::infinity()); }
    static constexpr DerivedTime nan() { return fromRawSeconds(std::numeric_limits<double>::quiet_NaN()); }

    bool isNaN() const { return std::isnan(m_value); }
    bool isInfinity() const { return std::isinf(m_value); }
    bool isFinite() const { return std::isfinite(m_value); }

    constexpr Seconds secondsSinceEpoch() const { return Seconds(m_value); }

    explicit constexpr operator bool() const { return !!m_value; }

    constexpr DerivedTime operator+(Seconds other) const
    {
        return fromRawSeconds(m_value + other.value());
    }

    constexpr DerivedTime operator-(Seconds other) const
    {
        return fromRawSeconds(m_value - other.value());
    }

    Seconds operator%(Seconds other) const
    {
        return Seconds { fmod(m_value, other.value()) };
    }

    // Time is a scalar and scalars can be negated as this could arise from algebraic
    // transformations. So, we allow it.
    constexpr DerivedTime operator-() const
    {
        return fromRawSeconds(-m_value);
    }

    DerivedTime operator+=(Seconds other)
    {
        return *static_cast<DerivedTime*>(this) = *static_cast<DerivedTime*>(this) + other;
    }

    DerivedTime operator-=(Seconds other)
    {
        return *static_cast<DerivedTime*>(this) = *static_cast<DerivedTime*>(this) - other;
    }

    constexpr Seconds operator-(DerivedTime other) const
    {
        return Seconds(m_value - other.m_value);
    }

    friend constexpr auto operator<=>(GenericTimeMixin, GenericTimeMixin) = default;

    DerivedTime isolatedCopy() const
    {
        return *static_cast<const DerivedTime*>(this);
    }

    static constexpr DerivedTime timePointFromNow(Seconds relativeTimeFromNow)
    {
        if (relativeTimeFromNow.isInfinity())
            return DerivedTime::fromRawSeconds(relativeTimeFromNow.value());
        return DerivedTime::now() + relativeTimeFromNow;
    }

protected:
    // This is the epoch. So, x.secondsSinceEpoch() should be the same as x - DerivedTime().
    constexpr GenericTimeMixin() = default;

    constexpr GenericTimeMixin(double rawValue)
        : m_value(rawValue)
    {
    }

    double m_value { 0 };
};

} // namespace WTF
