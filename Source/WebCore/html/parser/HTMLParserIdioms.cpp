/*
 * Copyright (C) 2010-2023 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "HTMLParserIdioms.h"

#include "Decimal.h"
#include "QualifiedName.h"
#include <limits>
#include <wtf/MathExtras.h>
#include <wtf/URL.h>
#include <wtf/Vector.h>
#include <wtf/dtoa.h>
#include <wtf/text/ParsingUtilities.h>

#if PLATFORM(COCOA)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

namespace WebCore {

String serializeForNumberType(const Decimal& number)
{
    if (number.isZero()) {
        // Decimal::toString appends exponent, e.g. "0e-18"
        return number.isNegative() ? "-0"_s : "0"_s;
    }
    return number.toString();
}

String serializeForNumberType(double number)
{
    // According to HTML5, "the best representation of the number n as a floating
    // point number" is a string produced by applying ToString() to n.
    return String::number(number);
}

Decimal parseToDecimalForNumberType(StringView string, const Decimal& fallbackValue)
{
    // https://html.spec.whatwg.org/#floating-point-numbers and parseToDoubleForNumberType
    if (string.isEmpty())
        return fallbackValue;

    // String::toDouble() accepts leading + and whitespace characters, which are not valid here.
    const char16_t firstCharacter = string[0];
    if (firstCharacter != '-' && firstCharacter != '.' && !isASCIIDigit(firstCharacter))
        return fallbackValue;

    const Decimal value = Decimal::fromString(string);
    if (!value.isFinite())
        return fallbackValue;

    // Numbers are considered finite IEEE 754 Double-precision floating point values.
    const Decimal doubleMax = Decimal::doubleMax();
    if (value < -doubleMax || value > doubleMax)
        return fallbackValue;

    // We return +0 for -0 case.
    return value.isZero() ? Decimal(0) : value;
}

Decimal parseToDecimalForNumberType(StringView string)
{
    return parseToDecimalForNumberType(string, Decimal::nan());
}

double parseToDoubleForNumberType(StringView string, double fallbackValue)
{
    // https://html.spec.whatwg.org/#floating-point-numbers
    if (string.isEmpty())
        return fallbackValue;

    // String::toDouble() accepts leading + and whitespace characters, which are not valid here.
    char16_t firstCharacter = string[0];
    if (firstCharacter != '-' && firstCharacter != '.' && !isASCIIDigit(firstCharacter))
        return fallbackValue;

    bool allowStringsThatEndWithFullStop = false;
#if PLATFORM(COCOA)
    if (!linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::DoesNotParseStringEndingWithFullStopAsFloatingPointNumber))
        allowStringsThatEndWithFullStop = true;
#endif

    if (string.endsWith('.') && !allowStringsThatEndWithFullStop)
        return fallbackValue;

    bool valid = false;
    double value = string.toDouble(valid);
    if (!valid)
        return fallbackValue;

    // NaN and infinity are considered valid by StringView::toDouble, but not valid here.
    if (!std::isfinite(value))
        return fallbackValue;

    // Numbers are considered finite IEEE 754 Double-precision floating point values.
    ASSERT(-std::numeric_limits<double>::max() <= value || value < std::numeric_limits<double>::max());

    // The following expression converts -0 to +0.
    return value ? value : 0;
}

double parseToDoubleForNumberType(StringView string)
{
    return parseToDoubleForNumberType(string, std::numeric_limits<double>::quiet_NaN());
}

template <typename CharacterType>
static Expected<int, HTMLIntegerParsingError> parseHTMLIntegerInternal(std::span<const CharacterType> data)
{
    skipWhile<isASCIIWhitespace>(data);

    if (data.empty())
        return makeUnexpected(HTMLIntegerParsingError::Other);

    bool isNegative = false;
    if (skipExactly(data, '-'))
        isNegative = true;
    else
        skipExactly(data, '+');

    if (data.empty() || !isASCIIDigit(data[0]))
        return makeUnexpected(HTMLIntegerParsingError::Other);

    constexpr int intMax = std::numeric_limits<int>::max();
    constexpr int base = 10;
    constexpr int maxMultiplier = intMax / base;

    unsigned result = 0;
    do {
        int digitValue = consume(data) - '0';

        if (result > maxMultiplier || (result == maxMultiplier && digitValue > (intMax % base) + isNegative))
            return makeUnexpected(isNegative ? HTMLIntegerParsingError::NegativeOverflow : HTMLIntegerParsingError::PositiveOverflow);

        result = base * result + digitValue;
    } while (!data.empty() && isASCIIDigit(data[0]));

    return isNegative ? -result : result;
}

// https://html.spec.whatwg.org/multipage/infrastructure.html#rules-for-parsing-integers
Expected<int, HTMLIntegerParsingError> parseHTMLInteger(StringView input)
{
    if (input.isEmpty())
        return makeUnexpected(HTMLIntegerParsingError::Other);

    if (input.is8Bit()) [[likely]]
        return parseHTMLIntegerInternal(input.span8());

    return parseHTMLIntegerInternal(input.span16());
}

// https://html.spec.whatwg.org/multipage/infrastructure.html#rules-for-parsing-non-negative-integers
Expected<unsigned, HTMLIntegerParsingError> parseHTMLNonNegativeInteger(StringView input)
{
    auto optionalSignedResult = parseHTMLInteger(input);
    if (!optionalSignedResult)
        return makeUnexpected(WTFMove(optionalSignedResult.error()));

    if (optionalSignedResult.value() < 0)
        return makeUnexpected(HTMLIntegerParsingError::NegativeOverflow);

    return static_cast<unsigned>(optionalSignedResult.value());
}

template <typename CharacterType>
static std::optional<int> parseValidHTMLNonNegativeIntegerInternal(std::span<const CharacterType> data)
{
    // A string is a valid non-negative integer if it consists of one or more ASCII digits.
    for (auto character : data) {
        if (!isASCIIDigit(character))
            return std::nullopt;
    }

    auto optionalSignedValue = parseHTMLIntegerInternal(data);
    if (!optionalSignedValue || optionalSignedValue.value() < 0)
        return std::nullopt;

    return optionalSignedValue.value();
}

// https://html.spec.whatwg.org/#valid-non-negative-integer
std::optional<int> parseValidHTMLNonNegativeInteger(StringView input)
{
    if (input.isEmpty())
        return std::nullopt;

    if (input.is8Bit()) [[likely]]
        return parseValidHTMLNonNegativeIntegerInternal(input.span8());
    return parseValidHTMLNonNegativeIntegerInternal(input.span16());
}

template <typename CharacterType>
static std::optional<double> parseValidHTMLFloatingPointNumberInternal(std::span<const CharacterType> characters)
{
    ASSERT(!characters.empty());

    // parseDouble() allows the string to start with a '+' or to end with a '.' but those
    // are not valid floating point numbers as per HTML.
    if (characters.front() == '+' || characters.back() == '.')
        return std::nullopt;

    size_t parsedLength = 0;
    double number = parseDouble(characters, parsedLength);
    return parsedLength == characters.size() && std::isfinite(number) ? number : std::optional<double>();
}

// https://html.spec.whatwg.org/#valid-floating-point-number
std::optional<double> parseValidHTMLFloatingPointNumber(StringView input)
{
    if (input.isEmpty())
        return std::nullopt;
    if (input.is8Bit()) [[likely]]
        return parseValidHTMLFloatingPointNumberInternal(input.span8());
    return parseValidHTMLFloatingPointNumberInternal(input.span16());
}

template <typename CharacterType>
static double parseHTMLFloatingPointNumberValueInternal(std::span<const CharacterType> data, size_t length, double fallbackValue)
{
    auto position = data;
    size_t leadingSpacesLength = 0;
    while (leadingSpacesLength < length && isASCIIWhitespace(position[leadingSpacesLength]))
        ++leadingSpacesLength;

    skip(position, leadingSpacesLength);
    if (leadingSpacesLength == length || (position[0] != '+' && position[0] != '-' && position[0] != '.' && !isASCIIDigit(position[0])))
        return fallbackValue;

    size_t parsedLength;
    double number = parseDouble(position.first(length - leadingSpacesLength), parsedLength);

    // The following expression converts -0 to +0.
    return number ? number : 0;
}

// https://html.spec.whatwg.org/#rules-for-parsing-floating-point-number-values
double parseHTMLFloatingPointNumberValue(StringView input, double fallbackValue)
{
    if (input.is8Bit()) [[likely]]
        return parseHTMLFloatingPointNumberValueInternal(input.span8(), input.length(), fallbackValue);

    return parseHTMLFloatingPointNumberValueInternal(input.span16(), input.length(), fallbackValue);
}

template<typename CharacterType>
static inline bool isHTMLSpaceOrDelimiter(CharacterType character)
{
    return isASCIIWhitespace(character) || character == ',' || character == ';';
}

static inline bool isNumberStart(char16_t character)
{
    return isASCIIDigit(character) || character == '.' || character == '-';
}

template<typename CharacterType>
static inline bool isHTMLSpaceOrDelimiterOrNumberStart(CharacterType character)
{
    return isHTMLSpaceOrDelimiter(character) || isNumberStart(character);
}

// https://html.spec.whatwg.org/multipage/infrastructure.html#rules-for-parsing-floating-point-number-values
template <typename CharacterType>
static Vector<double> parseHTMLListOfOfFloatingPointNumberValuesInternal(std::span<const CharacterType> data)
{
    Vector<double> numbers;

    // This skips past any leading delimiters.
    skipWhile<isHTMLSpaceOrDelimiter>(data);

    while (!data.empty()) {
        // This skips past leading garbage.
        skipUntil<isHTMLSpaceOrDelimiterOrNumberStart>(data);

        auto numberStart = data;
        skipUntil<isHTMLSpaceOrDelimiter>(data);

        size_t parsedLength = 0;
        double number = parseDouble(numberStart.first(data.data() - numberStart.data()), parsedLength);
        numbers.append(parsedLength > 0 && std::isfinite(number) ? number : 0);

        // This skips past the delimiter.
        skipWhile<isHTMLSpaceOrDelimiter>(data);
    }

    return numbers;
}

Vector<double> parseHTMLListOfOfFloatingPointNumberValues(StringView input)
{
    if (input.is8Bit()) [[likely]]
        return parseHTMLListOfOfFloatingPointNumberValuesInternal(input.span8());
    return parseHTMLListOfOfFloatingPointNumberValuesInternal(input.span16());
}

static bool threadSafeEqual(const StringImpl& a, const StringImpl& b)
{
    if (&a == &b)
        return true;
    if (a.hash() != b.hash())
        return false;
    return equal(a, b);
}

bool threadSafeMatch(const QualifiedName& a, const QualifiedName& b)
{
    return threadSafeEqual(*a.localName().impl(), *b.localName().impl());
}

String parseCORSSettingsAttribute(const AtomString& value)
{
    if (value.isNull())
        return String();
    if (equalLettersIgnoringASCIICase(value, "use-credentials"_s))
        return "use-credentials"_s;
    return "anonymous"_s;
}

template<typename CharacterType>
static bool isASCIIDigitOrPeriod(CharacterType character)
{
    return isASCIIDigit(character) || character == '.';
}

template<typename CharacterType>
static bool isSemicolonOrComma(CharacterType character)
{
    return character == ';' || character == ',';
}

// https://html.spec.whatwg.org/multipage/semantics.html#attr-meta-http-equiv-refresh
template <typename CharacterType>
static bool parseHTTPRefreshInternal(std::span<const CharacterType> data, double& parsedDelay, String& parsedURL)
{
    skipWhile<isASCIIWhitespace>(data);

    unsigned time = 0;

    auto numberStart = data;
    skipWhile<isASCIIDigit>(data);

    StringView timeString(numberStart.first(data.data() - numberStart.data()));
    if (timeString.isEmpty()) {
        if (data.empty() || data[0] != '.')
            return false;
    } else {
        auto optionalNumber = parseHTMLNonNegativeInteger(timeString);
        if (!optionalNumber)
            return false;
        time = optionalNumber.value();
    }

    skipWhile<isASCIIDigitOrPeriod>(data);

    if (data.empty()) {
        parsedDelay = time;
        return true;
    }

    if (data[0] != ';' && data[0] != ',' && !isASCIIWhitespace(data[0]))
        return false;

    parsedDelay = time;

    skipWhile<isASCIIWhitespace>(data);

    skipExactly<isSemicolonOrComma>(data);

    skipWhile<isASCIIWhitespace>(data);

    if (data.empty())
        return true;

    if (data[0] == 'U' || data[0] == 'u') {
        StringView url(data);

        skip(data, 1);

        if (!skipExactly(data, 'R') && !skipExactly(data, 'r')) {
            parsedURL = url.toString();
            return true;
        }

        if (!skipExactly(data, 'L') && !skipExactly(data, 'l')) {
            parsedURL = url.toString();
            return true;
        }

        skipWhile<isASCIIWhitespace>(data);

        if (!skipExactly(data, '=')) {
            parsedURL = url.toString();
            return true;
        }

        skipWhile<isASCIIWhitespace>(data);
    }

    CharacterType quote;
    if (!data.empty() && (data[0] == '\'' || data[0] == '"'))
        quote = consume(data);
    else
        quote = '\0';

    StringView url(data);

    if (quote != '\0') {
        size_t index = url.find(quote);
        if (index != notFound)
            url = url.left(index);
    }

    parsedURL = url.toString();
    return true;
}

bool parseMetaHTTPEquivRefresh(StringView input, double& delay, String& url)
{
    if (input.is8Bit()) [[likely]]
        return parseHTTPRefreshInternal(input.span8(), delay, url);
    return parseHTTPRefreshInternal(input.span16(), delay, url);
}

// https://html.spec.whatwg.org/#rules-for-parsing-a-hash-name-reference
AtomString parseHTMLHashNameReference(StringView usemap)
{
    size_t numberSignIndex = usemap.find('#');
    if (numberSignIndex == notFound)
        return nullAtom();
    return usemap.substring(numberSignIndex + 1).toAtomString();
}

struct HTMLDimensionParsingResult {
    double number;
    unsigned parsedLength;
};

template <typename CharacterType>
static std::optional<HTMLDimensionParsingResult> parseHTMLDimensionNumber(std::span<const CharacterType> data)
{
    if (data.empty() || !data.data())
        return std::nullopt;

    const auto* begin = data.data();
    skipWhile<isASCIIWhitespace>(data);
    if (data.empty())
        return std::nullopt;

    auto start = data;
    skipWhile<isASCIIDigit>(data);
    if (start.data() == data.data())
        return std::nullopt;

    if (skipExactly(data, '.'))
        skipWhile<isASCIIDigit>(data);

    size_t parsedLength = 0;
    double number = parseDouble(start.first(data.data() - start.data()), parsedLength);
    if (!(parsedLength && std::isfinite(number)))
        return std::nullopt;

    HTMLDimensionParsingResult result;
    result.number = number;
    result.parsedLength = data.data() - begin;
    return result;
}

enum class IsMultiLength : bool { No, Yes };
static std::optional<HTMLDimension> parseHTMLDimensionInternal(StringView dimensionString, IsMultiLength isMultiLength)
{
    std::optional<HTMLDimensionParsingResult> result;
    if (dimensionString.is8Bit())
        result = parseHTMLDimensionNumber(dimensionString.span8());
    else
        result = parseHTMLDimensionNumber(dimensionString.span16());
    if (!result)
        return std::nullopt;

    // The relative_length is not supported, here to make sure number + * does not map to number
    if (isMultiLength == IsMultiLength::Yes && result->parsedLength < dimensionString.length() && dimensionString[result->parsedLength] == '*')
        return std::nullopt;

    HTMLDimension dimension;
    dimension.number = result->number;
    dimension.type = HTMLDimension::Type::Pixel;
    if (result->parsedLength < dimensionString.length() && dimensionString[result->parsedLength] == '%')
        dimension.type = HTMLDimension::Type::Percentage;
    return dimension;
}

std::optional<HTMLDimension> parseHTMLDimension(StringView dimensionString)
{
    return parseHTMLDimensionInternal(dimensionString, IsMultiLength::No);
}

std::optional<HTMLDimension> parseHTMLMultiLength(StringView multiLengthString)
{
    return parseHTMLDimensionInternal(multiLengthString, IsMultiLength::Yes);
}

}
