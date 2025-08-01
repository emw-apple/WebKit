/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <wtf/FastMalloc.h>
#include <wtf/JSONValues.h>
#include <wtf/Seconds.h>
#include <wtf/text/WTFString.h>

#include <cmath>
#include <limits>
#include <math.h>
#include <stdint.h>

namespace WTF {

class PrintStream;

class WTF_EXPORT_PRIVATE MediaTime final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(MediaTime);
public:
    enum {
        Valid = 1 << 0,
        HasBeenRounded = 1 << 1,
        PositiveInfinite = 1 << 2,
        NegativeInfinite = 1 << 3,
        Indefinite = 1 << 4,
        DoubleValue = 1 << 5,
    };

    constexpr MediaTime();
    constexpr MediaTime(int64_t value, uint32_t scale, uint8_t flags = Valid);
    MediaTime(const MediaTime&) = default;

    static MediaTime createWithFloat(float floatTime);
    static MediaTime createWithFloat(float floatTime, uint32_t timeScale);
    static MediaTime createWithDouble(double doubleTime);
    static MediaTime createWithDouble(double doubleTime, uint32_t timeScale);
    static MediaTime createWithSeconds(Seconds seconds) { return createWithDouble(seconds.value()); }

    float toFloat() const;
    double toDouble() const;
    int64_t toMicroseconds() const;

    MediaTime& operator=(const MediaTime&) = default;
    MediaTime& operator+=(const MediaTime& rhs) { return *this = *this + rhs; }
    MediaTime& operator-=(const MediaTime& rhs) { return *this = *this - rhs; }
    MediaTime operator+(const MediaTime& rhs) const;
    MediaTime operator-(const MediaTime& rhs) const;
    MediaTime operator-() const;
    MediaTime operator*(int32_t) const;
    bool operator!() const;
    explicit operator bool() const;

    WTF_EXPORT_PRIVATE friend std::weak_ordering operator<=>(const MediaTime&, const MediaTime&);
    friend bool operator==(const MediaTime& a, const MediaTime& b) { return is_eq(a <=> b); }
    bool isBetween(const MediaTime&, const MediaTime&) const;

    bool isValid() const { return m_timeFlags & Valid; }
    bool isInvalid() const { return !isValid(); }
    bool hasBeenRounded() const { return m_timeFlags & HasBeenRounded; }
    bool isPositiveInfinite() const { return m_timeFlags & PositiveInfinite; }
    bool isNegativeInfinite() const { return m_timeFlags & NegativeInfinite; }
    bool isIndefinite() const { return m_timeFlags & Indefinite; }
    bool isFinite() const { return !isInvalid() && !isIndefinite() && !isPositiveInfinite() && !isNegativeInfinite(); }
    bool hasDoubleValue() const { return m_timeFlags & DoubleValue; }
    uint8_t timeFlags() const { return m_timeFlags; }

    static const MediaTime& zeroTime();
    static const MediaTime& invalidTime();
    static const MediaTime& positiveInfiniteTime();
    static const MediaTime& negativeInfiniteTime();
    static const MediaTime& indefiniteTime();

    const int64_t& timeValue() const { return m_timeValue; }
    const uint32_t& timeScale() const { return m_timeScale; }

    void dump(PrintStream& out) const;
    String toString() const;
    String toJSONString() const;
    Ref<JSON::Object> toJSONObject() const;

    // Make the following casts errors:
    operator double() const = delete;
    MediaTime(double) = delete;
    operator int() const = delete;
    MediaTime(int) = delete;

    friend WTF_EXPORT_PRIVATE MediaTime abs(const MediaTime& rhs);

    static const uint32_t DefaultTimeScale = 10000000;
    static const uint32_t MaximumTimeScale;

    enum class RoundingFlags {
        HalfAwayFromZero = 0,
        TowardZero,
        AwayFromZero,
        TowardPositiveInfinity,
        TowardNegativeInfinity,
    };

    MediaTime toTimeScale(uint32_t, RoundingFlags = RoundingFlags::HalfAwayFromZero) const;
    MediaTime isolatedCopy() const;

private:
    void setTimeScale(uint32_t, RoundingFlags = RoundingFlags::HalfAwayFromZero);

    union {
        int64_t m_timeValue;
        double m_timeValueAsDouble;
    };
    uint32_t m_timeScale;
    uint8_t m_timeFlags;
};

constexpr MediaTime::MediaTime()
    : m_timeValue(0)
    , m_timeScale(DefaultTimeScale)
    , m_timeFlags(Valid)
{
}

constexpr MediaTime::MediaTime(int64_t value, uint32_t scale, uint8_t flags)
    : m_timeValue(value)
    , m_timeScale(scale)
    , m_timeFlags(flags)
{
    if (scale || !(flags & Valid))
        return;

    if (value < 0) {
        // Negative infinite time
        m_timeValue = -1;
        m_timeScale = 1;
        m_timeFlags = NegativeInfinite | Valid;
    } else {
        // Positive infinite time
        m_timeValue = 0;
        m_timeScale = 1;
        m_timeFlags = PositiveInfinite | Valid;
    }
}

inline MediaTime operator*(int32_t lhs, const MediaTime& rhs) { return rhs.operator*(lhs); }

WTF_EXPORT_PRIVATE extern MediaTime abs(const MediaTime& rhs);

struct WTF_EXPORT_PRIVATE MediaTimeRange {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(MediaTimeRange);

    String toJSONString() const;

    const MediaTime start;
    const MediaTime end;
};

template<typename> struct LogArgument;

template<> struct LogArgument<MediaTime> {
    static String toString(const MediaTime& time) { return time.toJSONString(); }
};
template<> struct LogArgument<MediaTimeRange> {
    static String toString(const MediaTimeRange& range) { return range.toJSONString(); }
};

WTF_EXPORT_PRIVATE TextStream& operator<<(TextStream&, const MediaTime&);

}

using WTF::MediaTime;
using WTF::MediaTimeRange;
using WTF::abs;
