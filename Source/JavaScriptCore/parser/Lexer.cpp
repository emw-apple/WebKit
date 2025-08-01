/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2006-2024 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Cameron Zwarich (cwzwarich@uwaterloo.ca)
 *  Copyright (C) 2010 Zoltan Herczeg (zherczeg@inf.u-szeged.hu)
 *  Copyright (C) 2012 Mathias Bynens (mathias@qiwi.be)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "Lexer.h"

#include "BuiltinNames.h"
#include "Identifier.h"
#include "KeywordLookup.h"
#include "Lexer.lut.h"
#include "ParseInt.h"
#include <limits.h>
#include <string.h>
#include <wtf/Assertions.h>
#include <wtf/HexNumber.h>
#include <wtf/dtoa.h>
#include <wtf/text/MakeString.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

constinit const WTF::BitSet<256> whiteSpaceTable = makeLatin1CharacterBitSet(
    [](LChar ch) {
        return ch == ' ' || ch == '\t' || ch == 0xB || ch == 0xC || ch == 0xA0;
    });

bool isLexerKeyword(const Identifier& identifier)
{
    return JSC::mainTable.entry(identifier);
}

enum CharacterType : uint8_t {
    // Types for the main switch

    // The first three types are fixed, and also used for identifying
    // ASCII alpha and alphanumeric characters (see isIdentStart and isIdentPart).
    CharacterLatin1IdentifierStart,
    CharacterZero,
    CharacterNumber,

    // For single-byte characters grandfathered into Other_ID_Continue -- namely just U+00B7 MIDDLE DOT.
    // (http://unicode.org/reports/tr31/#Backward_Compatibility)
    //
    // Character types are divided into two groups depending on whether they can be part of an
    // identifier or not. Those whose type value is less or equal than CharacterOtherIdentifierPart can be
    // part of an identifier. (See the CharacterType definition for more details.)
    CharacterOtherIdentifierPart,
    CharacterBackSlash, // Keep the ordering until this. We use this ordering to detect identifier-part or back-slash quickly.

    CharacterInvalid,
    CharacterLineTerminator,
    CharacterExclamationMark,
    CharacterOpenParen,
    CharacterCloseParen,
    CharacterOpenBracket,
    CharacterCloseBracket,
    CharacterComma,
    CharacterColon,
    CharacterQuestion,
    CharacterTilde,
    CharacterQuote,
    CharacterBackQuote,
    CharacterDot,
    CharacterSlash,
    CharacterSemicolon,
    CharacterOpenBrace,
    CharacterCloseBrace,

    CharacterAdd,
    CharacterSub,
    CharacterMultiply,
    CharacterModulo,
    CharacterAnd,
    CharacterXor,
    CharacterOr,
    CharacterLess,
    CharacterGreater,
    CharacterEqual,

    // Other types (only one so far)
    CharacterWhiteSpace,
    CharacterHash,
    CharacterPrivateIdentifierStart,
    CharacterNonLatin1IdentifierStart,
};

// 256 Latin-1 codes
static constexpr const CharacterType typesOfLatin1Characters[256] = {
/*   0 - Null               */ CharacterInvalid,
/*   1 - Start of Heading   */ CharacterInvalid,
/*   2 - Start of Text      */ CharacterInvalid,
/*   3 - End of Text        */ CharacterInvalid,
/*   4 - End of Transm.     */ CharacterInvalid,
/*   5 - Enquiry            */ CharacterInvalid,
/*   6 - Acknowledgment     */ CharacterInvalid,
/*   7 - Bell               */ CharacterInvalid,
/*   8 - Back Space         */ CharacterInvalid,
/*   9 - Horizontal Tab     */ CharacterWhiteSpace,
/*  10 - Line Feed          */ CharacterLineTerminator,
/*  11 - Vertical Tab       */ CharacterWhiteSpace,
/*  12 - Form Feed          */ CharacterWhiteSpace,
/*  13 - Carriage Return    */ CharacterLineTerminator,
/*  14 - Shift Out          */ CharacterInvalid,
/*  15 - Shift In           */ CharacterInvalid,
/*  16 - Data Line Escape   */ CharacterInvalid,
/*  17 - Device Control 1   */ CharacterInvalid,
/*  18 - Device Control 2   */ CharacterInvalid,
/*  19 - Device Control 3   */ CharacterInvalid,
/*  20 - Device Control 4   */ CharacterInvalid,
/*  21 - Negative Ack.      */ CharacterInvalid,
/*  22 - Synchronous Idle   */ CharacterInvalid,
/*  23 - End of Transmit    */ CharacterInvalid,
/*  24 - Cancel             */ CharacterInvalid,
/*  25 - End of Medium      */ CharacterInvalid,
/*  26 - Substitute         */ CharacterInvalid,
/*  27 - Escape             */ CharacterInvalid,
/*  28 - File Separator     */ CharacterInvalid,
/*  29 - Group Separator    */ CharacterInvalid,
/*  30 - Record Separator   */ CharacterInvalid,
/*  31 - Unit Separator     */ CharacterInvalid,
/*  32 - Space              */ CharacterWhiteSpace,
/*  33 - !                  */ CharacterExclamationMark,
/*  34 - "                  */ CharacterQuote,
/*  35 - #                  */ CharacterHash,
/*  36 - $                  */ CharacterLatin1IdentifierStart,
/*  37 - %                  */ CharacterModulo,
/*  38 - &                  */ CharacterAnd,
/*  39 - '                  */ CharacterQuote,
/*  40 - (                  */ CharacterOpenParen,
/*  41 - )                  */ CharacterCloseParen,
/*  42 - *                  */ CharacterMultiply,
/*  43 - +                  */ CharacterAdd,
/*  44 - ,                  */ CharacterComma,
/*  45 - -                  */ CharacterSub,
/*  46 - .                  */ CharacterDot,
/*  47 - /                  */ CharacterSlash,
/*  48 - 0                  */ CharacterZero,
/*  49 - 1                  */ CharacterNumber,
/*  50 - 2                  */ CharacterNumber,
/*  51 - 3                  */ CharacterNumber,
/*  52 - 4                  */ CharacterNumber,
/*  53 - 5                  */ CharacterNumber,
/*  54 - 6                  */ CharacterNumber,
/*  55 - 7                  */ CharacterNumber,
/*  56 - 8                  */ CharacterNumber,
/*  57 - 9                  */ CharacterNumber,
/*  58 - :                  */ CharacterColon,
/*  59 - ;                  */ CharacterSemicolon,
/*  60 - <                  */ CharacterLess,
/*  61 - =                  */ CharacterEqual,
/*  62 - >                  */ CharacterGreater,
/*  63 - ?                  */ CharacterQuestion,
/*  64 - @                  */ CharacterPrivateIdentifierStart,
/*  65 - A                  */ CharacterLatin1IdentifierStart,
/*  66 - B                  */ CharacterLatin1IdentifierStart,
/*  67 - C                  */ CharacterLatin1IdentifierStart,
/*  68 - D                  */ CharacterLatin1IdentifierStart,
/*  69 - E                  */ CharacterLatin1IdentifierStart,
/*  70 - F                  */ CharacterLatin1IdentifierStart,
/*  71 - G                  */ CharacterLatin1IdentifierStart,
/*  72 - H                  */ CharacterLatin1IdentifierStart,
/*  73 - I                  */ CharacterLatin1IdentifierStart,
/*  74 - J                  */ CharacterLatin1IdentifierStart,
/*  75 - K                  */ CharacterLatin1IdentifierStart,
/*  76 - L                  */ CharacterLatin1IdentifierStart,
/*  77 - M                  */ CharacterLatin1IdentifierStart,
/*  78 - N                  */ CharacterLatin1IdentifierStart,
/*  79 - O                  */ CharacterLatin1IdentifierStart,
/*  80 - P                  */ CharacterLatin1IdentifierStart,
/*  81 - Q                  */ CharacterLatin1IdentifierStart,
/*  82 - R                  */ CharacterLatin1IdentifierStart,
/*  83 - S                  */ CharacterLatin1IdentifierStart,
/*  84 - T                  */ CharacterLatin1IdentifierStart,
/*  85 - U                  */ CharacterLatin1IdentifierStart,
/*  86 - V                  */ CharacterLatin1IdentifierStart,
/*  87 - W                  */ CharacterLatin1IdentifierStart,
/*  88 - X                  */ CharacterLatin1IdentifierStart,
/*  89 - Y                  */ CharacterLatin1IdentifierStart,
/*  90 - Z                  */ CharacterLatin1IdentifierStart,
/*  91 - [                  */ CharacterOpenBracket,
/*  92 - \                  */ CharacterBackSlash,
/*  93 - ]                  */ CharacterCloseBracket,
/*  94 - ^                  */ CharacterXor,
/*  95 - _                  */ CharacterLatin1IdentifierStart,
/*  96 - `                  */ CharacterBackQuote,
/*  97 - a                  */ CharacterLatin1IdentifierStart,
/*  98 - b                  */ CharacterLatin1IdentifierStart,
/*  99 - c                  */ CharacterLatin1IdentifierStart,
/* 100 - d                  */ CharacterLatin1IdentifierStart,
/* 101 - e                  */ CharacterLatin1IdentifierStart,
/* 102 - f                  */ CharacterLatin1IdentifierStart,
/* 103 - g                  */ CharacterLatin1IdentifierStart,
/* 104 - h                  */ CharacterLatin1IdentifierStart,
/* 105 - i                  */ CharacterLatin1IdentifierStart,
/* 106 - j                  */ CharacterLatin1IdentifierStart,
/* 107 - k                  */ CharacterLatin1IdentifierStart,
/* 108 - l                  */ CharacterLatin1IdentifierStart,
/* 109 - m                  */ CharacterLatin1IdentifierStart,
/* 110 - n                  */ CharacterLatin1IdentifierStart,
/* 111 - o                  */ CharacterLatin1IdentifierStart,
/* 112 - p                  */ CharacterLatin1IdentifierStart,
/* 113 - q                  */ CharacterLatin1IdentifierStart,
/* 114 - r                  */ CharacterLatin1IdentifierStart,
/* 115 - s                  */ CharacterLatin1IdentifierStart,
/* 116 - t                  */ CharacterLatin1IdentifierStart,
/* 117 - u                  */ CharacterLatin1IdentifierStart,
/* 118 - v                  */ CharacterLatin1IdentifierStart,
/* 119 - w                  */ CharacterLatin1IdentifierStart,
/* 120 - x                  */ CharacterLatin1IdentifierStart,
/* 121 - y                  */ CharacterLatin1IdentifierStart,
/* 122 - z                  */ CharacterLatin1IdentifierStart,
/* 123 - {                  */ CharacterOpenBrace,
/* 124 - |                  */ CharacterOr,
/* 125 - }                  */ CharacterCloseBrace,
/* 126 - ~                  */ CharacterTilde,
/* 127 - Delete             */ CharacterInvalid,
/* 128 - Cc category        */ CharacterInvalid,
/* 129 - Cc category        */ CharacterInvalid,
/* 130 - Cc category        */ CharacterInvalid,
/* 131 - Cc category        */ CharacterInvalid,
/* 132 - Cc category        */ CharacterInvalid,
/* 133 - Cc category        */ CharacterInvalid,
/* 134 - Cc category        */ CharacterInvalid,
/* 135 - Cc category        */ CharacterInvalid,
/* 136 - Cc category        */ CharacterInvalid,
/* 137 - Cc category        */ CharacterInvalid,
/* 138 - Cc category        */ CharacterInvalid,
/* 139 - Cc category        */ CharacterInvalid,
/* 140 - Cc category        */ CharacterInvalid,
/* 141 - Cc category        */ CharacterInvalid,
/* 142 - Cc category        */ CharacterInvalid,
/* 143 - Cc category        */ CharacterInvalid,
/* 144 - Cc category        */ CharacterInvalid,
/* 145 - Cc category        */ CharacterInvalid,
/* 146 - Cc category        */ CharacterInvalid,
/* 147 - Cc category        */ CharacterInvalid,
/* 148 - Cc category        */ CharacterInvalid,
/* 149 - Cc category        */ CharacterInvalid,
/* 150 - Cc category        */ CharacterInvalid,
/* 151 - Cc category        */ CharacterInvalid,
/* 152 - Cc category        */ CharacterInvalid,
/* 153 - Cc category        */ CharacterInvalid,
/* 154 - Cc category        */ CharacterInvalid,
/* 155 - Cc category        */ CharacterInvalid,
/* 156 - Cc category        */ CharacterInvalid,
/* 157 - Cc category        */ CharacterInvalid,
/* 158 - Cc category        */ CharacterInvalid,
/* 159 - Cc category        */ CharacterInvalid,
/* 160 - Zs category (nbsp) */ CharacterWhiteSpace,
/* 161 - Po category        */ CharacterInvalid,
/* 162 - Sc category        */ CharacterInvalid,
/* 163 - Sc category        */ CharacterInvalid,
/* 164 - Sc category        */ CharacterInvalid,
/* 165 - Sc category        */ CharacterInvalid,
/* 166 - So category        */ CharacterInvalid,
/* 167 - So category        */ CharacterInvalid,
/* 168 - Sk category        */ CharacterInvalid,
/* 169 - So category        */ CharacterInvalid,
/* 170 - Ll category        */ CharacterLatin1IdentifierStart,
/* 171 - Pi category        */ CharacterInvalid,
/* 172 - Sm category        */ CharacterInvalid,
/* 173 - Cf category        */ CharacterInvalid,
/* 174 - So category        */ CharacterInvalid,
/* 175 - Sk category        */ CharacterInvalid,
/* 176 - So category        */ CharacterInvalid,
/* 177 - Sm category        */ CharacterInvalid,
/* 178 - No category        */ CharacterInvalid,
/* 179 - No category        */ CharacterInvalid,
/* 180 - Sk category        */ CharacterInvalid,
/* 181 - Ll category        */ CharacterLatin1IdentifierStart,
/* 182 - So category        */ CharacterInvalid,
/* 183 - Po category        */ CharacterOtherIdentifierPart,
/* 184 - Sk category        */ CharacterInvalid,
/* 185 - No category        */ CharacterInvalid,
/* 186 - Ll category        */ CharacterLatin1IdentifierStart,
/* 187 - Pf category        */ CharacterInvalid,
/* 188 - No category        */ CharacterInvalid,
/* 189 - No category        */ CharacterInvalid,
/* 190 - No category        */ CharacterInvalid,
/* 191 - Po category        */ CharacterInvalid,
/* 192 - Lu category        */ CharacterLatin1IdentifierStart,
/* 193 - Lu category        */ CharacterLatin1IdentifierStart,
/* 194 - Lu category        */ CharacterLatin1IdentifierStart,
/* 195 - Lu category        */ CharacterLatin1IdentifierStart,
/* 196 - Lu category        */ CharacterLatin1IdentifierStart,
/* 197 - Lu category        */ CharacterLatin1IdentifierStart,
/* 198 - Lu category        */ CharacterLatin1IdentifierStart,
/* 199 - Lu category        */ CharacterLatin1IdentifierStart,
/* 200 - Lu category        */ CharacterLatin1IdentifierStart,
/* 201 - Lu category        */ CharacterLatin1IdentifierStart,
/* 202 - Lu category        */ CharacterLatin1IdentifierStart,
/* 203 - Lu category        */ CharacterLatin1IdentifierStart,
/* 204 - Lu category        */ CharacterLatin1IdentifierStart,
/* 205 - Lu category        */ CharacterLatin1IdentifierStart,
/* 206 - Lu category        */ CharacterLatin1IdentifierStart,
/* 207 - Lu category        */ CharacterLatin1IdentifierStart,
/* 208 - Lu category        */ CharacterLatin1IdentifierStart,
/* 209 - Lu category        */ CharacterLatin1IdentifierStart,
/* 210 - Lu category        */ CharacterLatin1IdentifierStart,
/* 211 - Lu category        */ CharacterLatin1IdentifierStart,
/* 212 - Lu category        */ CharacterLatin1IdentifierStart,
/* 213 - Lu category        */ CharacterLatin1IdentifierStart,
/* 214 - Lu category        */ CharacterLatin1IdentifierStart,
/* 215 - Sm category        */ CharacterInvalid,
/* 216 - Lu category        */ CharacterLatin1IdentifierStart,
/* 217 - Lu category        */ CharacterLatin1IdentifierStart,
/* 218 - Lu category        */ CharacterLatin1IdentifierStart,
/* 219 - Lu category        */ CharacterLatin1IdentifierStart,
/* 220 - Lu category        */ CharacterLatin1IdentifierStart,
/* 221 - Lu category        */ CharacterLatin1IdentifierStart,
/* 222 - Lu category        */ CharacterLatin1IdentifierStart,
/* 223 - Ll category        */ CharacterLatin1IdentifierStart,
/* 224 - Ll category        */ CharacterLatin1IdentifierStart,
/* 225 - Ll category        */ CharacterLatin1IdentifierStart,
/* 226 - Ll category        */ CharacterLatin1IdentifierStart,
/* 227 - Ll category        */ CharacterLatin1IdentifierStart,
/* 228 - Ll category        */ CharacterLatin1IdentifierStart,
/* 229 - Ll category        */ CharacterLatin1IdentifierStart,
/* 230 - Ll category        */ CharacterLatin1IdentifierStart,
/* 231 - Ll category        */ CharacterLatin1IdentifierStart,
/* 232 - Ll category        */ CharacterLatin1IdentifierStart,
/* 233 - Ll category        */ CharacterLatin1IdentifierStart,
/* 234 - Ll category        */ CharacterLatin1IdentifierStart,
/* 235 - Ll category        */ CharacterLatin1IdentifierStart,
/* 236 - Ll category        */ CharacterLatin1IdentifierStart,
/* 237 - Ll category        */ CharacterLatin1IdentifierStart,
/* 238 - Ll category        */ CharacterLatin1IdentifierStart,
/* 239 - Ll category        */ CharacterLatin1IdentifierStart,
/* 240 - Ll category        */ CharacterLatin1IdentifierStart,
/* 241 - Ll category        */ CharacterLatin1IdentifierStart,
/* 242 - Ll category        */ CharacterLatin1IdentifierStart,
/* 243 - Ll category        */ CharacterLatin1IdentifierStart,
/* 244 - Ll category        */ CharacterLatin1IdentifierStart,
/* 245 - Ll category        */ CharacterLatin1IdentifierStart,
/* 246 - Ll category        */ CharacterLatin1IdentifierStart,
/* 247 - Sm category        */ CharacterInvalid,
/* 248 - Ll category        */ CharacterLatin1IdentifierStart,
/* 249 - Ll category        */ CharacterLatin1IdentifierStart,
/* 250 - Ll category        */ CharacterLatin1IdentifierStart,
/* 251 - Ll category        */ CharacterLatin1IdentifierStart,
/* 252 - Ll category        */ CharacterLatin1IdentifierStart,
/* 253 - Ll category        */ CharacterLatin1IdentifierStart,
/* 254 - Ll category        */ CharacterLatin1IdentifierStart,
/* 255 - Ll category        */ CharacterLatin1IdentifierStart
};

// This table provides the character that results from \X where X is the index in the table beginning
// with SPACE. A table value of 0 means that more processing needs to be done.
static constexpr const LChar singleCharacterEscapeValuesForASCII[128] = {
/*   0 - Null               */ 0,
/*   1 - Start of Heading   */ 0,
/*   2 - Start of Text      */ 0,
/*   3 - End of Text        */ 0,
/*   4 - End of Transm.     */ 0,
/*   5 - Enquiry            */ 0,
/*   6 - Acknowledgment     */ 0,
/*   7 - Bell               */ 0,
/*   8 - Back Space         */ 0,
/*   9 - Horizontal Tab     */ 0,
/*  10 - Line Feed          */ 0,
/*  11 - Vertical Tab       */ 0,
/*  12 - Form Feed          */ 0,
/*  13 - Carriage Return    */ 0,
/*  14 - Shift Out          */ 0,
/*  15 - Shift In           */ 0,
/*  16 - Data Line Escape   */ 0,
/*  17 - Device Control 1   */ 0,
/*  18 - Device Control 2   */ 0,
/*  19 - Device Control 3   */ 0,
/*  20 - Device Control 4   */ 0,
/*  21 - Negative Ack.      */ 0,
/*  22 - Synchronous Idle   */ 0,
/*  23 - End of Transmit    */ 0,
/*  24 - Cancel             */ 0,
/*  25 - End of Medium      */ 0,
/*  26 - Substitute         */ 0,
/*  27 - Escape             */ 0,
/*  28 - File Separator     */ 0,
/*  29 - Group Separator    */ 0,
/*  30 - Record Separator   */ 0,
/*  31 - Unit Separator     */ 0,
/*  32 - Space              */ ' ',
/*  33 - !                  */ '!',
/*  34 - "                  */ '"',
/*  35 - #                  */ '#',
/*  36 - $                  */ '$',
/*  37 - %                  */ '%',
/*  38 - &                  */ '&',
/*  39 - '                  */ '\'',
/*  40 - (                  */ '(',
/*  41 - )                  */ ')',
/*  42 - *                  */ '*',
/*  43 - +                  */ '+',
/*  44 - ,                  */ ',',
/*  45 - -                  */ '-',
/*  46 - .                  */ '.',
/*  47 - /                  */ '/',
/*  48 - 0                  */ 0,
/*  49 - 1                  */ 0,
/*  50 - 2                  */ 0,
/*  51 - 3                  */ 0,
/*  52 - 4                  */ 0,
/*  53 - 5                  */ 0,
/*  54 - 6                  */ 0,
/*  55 - 7                  */ 0,
/*  56 - 8                  */ 0,
/*  57 - 9                  */ 0,
/*  58 - :                  */ ':',
/*  59 - ;                  */ ';',
/*  60 - <                  */ '<',
/*  61 - =                  */ '=',
/*  62 - >                  */ '>',
/*  63 - ?                  */ '?',
/*  64 - @                  */ '@',
/*  65 - A                  */ 'A',
/*  66 - B                  */ 'B',
/*  67 - C                  */ 'C',
/*  68 - D                  */ 'D',
/*  69 - E                  */ 'E',
/*  70 - F                  */ 'F',
/*  71 - G                  */ 'G',
/*  72 - H                  */ 'H',
/*  73 - I                  */ 'I',
/*  74 - J                  */ 'J',
/*  75 - K                  */ 'K',
/*  76 - L                  */ 'L',
/*  77 - M                  */ 'M',
/*  78 - N                  */ 'N',
/*  79 - O                  */ 'O',
/*  80 - P                  */ 'P',
/*  81 - Q                  */ 'Q',
/*  82 - R                  */ 'R',
/*  83 - S                  */ 'S',
/*  84 - T                  */ 'T',
/*  85 - U                  */ 'U',
/*  86 - V                  */ 'V',
/*  87 - W                  */ 'W',
/*  88 - X                  */ 'X',
/*  89 - Y                  */ 'Y',
/*  90 - Z                  */ 'Z',
/*  91 - [                  */ '[',
/*  92 - \                  */ '\\',
/*  93 - ]                  */ ']',
/*  94 - ^                  */ '^',
/*  95 - _                  */ '_',
/*  96 - `                  */ '`',
/*  97 - a                  */ 'a',
/*  98 - b                  */ 0x08,
/*  99 - c                  */ 'c',
/* 100 - d                  */ 'd',
/* 101 - e                  */ 'e',
/* 102 - f                  */ 0x0C,
/* 103 - g                  */ 'g',
/* 104 - h                  */ 'h',
/* 105 - i                  */ 'i',
/* 106 - j                  */ 'j',
/* 107 - k                  */ 'k',
/* 108 - l                  */ 'l',
/* 109 - m                  */ 'm',
/* 110 - n                  */ 0x0A,
/* 111 - o                  */ 'o',
/* 112 - p                  */ 'p',
/* 113 - q                  */ 'q',
/* 114 - r                  */ 0x0D,
/* 115 - s                  */ 's',
/* 116 - t                  */ 0x09,
/* 117 - u                  */ 0,
/* 118 - v                  */ 0x0B,
/* 119 - w                  */ 'w',
/* 120 - x                  */ 0,
/* 121 - y                  */ 'y',
/* 122 - z                  */ 'z',
/* 123 - {                  */ '{',
/* 124 - |                  */ '|',
/* 125 - }                  */ '}',
/* 126 - ~                  */ '~',
/* 127 - Delete             */ 0
};

template <typename T>
Lexer<T>::Lexer(VM& vm, JSParserBuiltinMode builtinMode, JSParserScriptMode scriptMode)
    : m_positionBeforeLastNewline(0,0,0)
    , m_isReparsingFunction(false)
    , m_vm(vm)
    , m_parsingBuiltinFunction(builtinMode == JSParserBuiltinMode::Builtin || Options::exposePrivateIdentifiers())
    , m_scriptMode(scriptMode)
{
}

static inline JSTokenType tokenTypeForIntegerLikeToken(double doubleValue)
{
    if ((doubleValue || !std::signbit(doubleValue)) && static_cast<int64_t>(doubleValue) == doubleValue)
        return INTEGER;
    return DOUBLE;
}

template <typename T>
Lexer<T>::~Lexer()
{
}

template <typename T>
String Lexer<T>::invalidCharacterMessage() const
{
    switch (m_current) {
    case 0:
        return "Invalid character: '\\0'"_s;
    case 10:
        return "Invalid character: '\\n'"_s;
    case 11:
        return "Invalid character: '\\v'"_s;
    case 13:
        return "Invalid character: '\\r'"_s;
    case 35:
        return "Invalid character: '#'"_s;
    case 64:
        return "Invalid character: '@'"_s;
    case 96:
        return "Invalid character: '`'"_s;
    default:
        return makeString("Invalid character '\\u"_s, hex(m_current, 4, Lowercase), '\'');
    }
}

template <typename T>
ALWAYS_INLINE const T* Lexer<T>::currentSourcePtr() const
{
    ASSERT(m_code <= m_codeEnd);
    return m_code;
}

template <typename T>
void Lexer<T>::setCode(const SourceCode& source, ParserArena* arena)
{
    m_arena = &arena->identifierArena();
    
    m_lineNumber = source.firstLine().oneBasedInt();
    m_lastToken = -1;
    
    StringView sourceString = source.provider()->source();

    if (!sourceString.isNull())
        setCodeStart(sourceString);
    else
        m_codeStart = nullptr;

    m_source = &source;
    m_sourceOffset = source.startOffset();
    m_codeStartPlusOffset = m_codeStart + source.startOffset();
    m_code = m_codeStartPlusOffset;
    m_codeEnd = m_codeStart + source.endOffset();
    m_error = false;
    m_atLineStart = true;
    m_lineStart = m_code;
    m_lexErrorMessage = String();
    m_sourceURLDirective = String();
    m_sourceMappingURLDirective = String();
    
    m_buffer8.reserveInitialCapacity(initialReadBufferCapacity);
    m_buffer16.reserveInitialCapacity(initialReadBufferCapacity);
    m_bufferForRawTemplateString16.reserveInitialCapacity(initialReadBufferCapacity);
    
    if (m_code < m_codeEnd) [[likely]]
        m_current = *m_code;
    else
        m_current = 0;
    ASSERT(currentOffset() == source.startOffset());
}

template <typename T>
template <int shiftAmount> ALWAYS_INLINE void Lexer<T>::internalShift()
{
    m_code += shiftAmount;
    ASSERT(currentOffset() >= currentLineStartOffset());
    m_current = *m_code;
}

template <typename T>
ALWAYS_INLINE void Lexer<T>::shift()
{
    // At one point timing showed that setting m_current to 0 unconditionally was faster than an if-else sequence.
    m_current = 0;
    ++m_code;
    if (m_code < m_codeEnd) [[likely]]
        m_current = *m_code;
}

template <typename T>
ALWAYS_INLINE bool Lexer<T>::atEnd() const
{
    ASSERT(!m_current || m_code < m_codeEnd);
    if (m_current) [[likely]]
        return false;
    if (m_code == m_codeEnd) [[unlikely]]
        return true;
    return false;
}

template <typename T>
ALWAYS_INLINE T Lexer<T>::peek(int offset) const
{
    ASSERT(offset > 0 && offset < 5);
    const T* code = m_code + offset;
    return (code < m_codeEnd) ? *code : 0;
}

struct ParsedUnicodeEscapeValue {
    ParsedUnicodeEscapeValue(char32_t value)
        : m_value(value)
    {
        ASSERT(isValid());
    }

    enum SpecialValueType : char32_t { Incomplete = 0xFFFFFFFEu, Invalid = 0xFFFFFFFFu };
    ParsedUnicodeEscapeValue(SpecialValueType type)
        : m_value(type)
    {
    }

    bool isValid() const { return m_value != Incomplete && m_value != Invalid; }
    bool isIncomplete() const { return m_value == Incomplete; }

    char32_t value() const
    {
        ASSERT(isValid());
        return m_value;
    }

private:
    char32_t m_value;
};

template<typename CharacterType>
ParsedUnicodeEscapeValue Lexer<CharacterType>::parseUnicodeEscape()
{
    if (m_current == '{') {
        shift();
        char32_t codePoint = 0;
        do {
            if (!isASCIIHexDigit(m_current))
                return m_current ? ParsedUnicodeEscapeValue::Invalid : ParsedUnicodeEscapeValue::Incomplete;
            codePoint = (codePoint << 4) | toASCIIHexValue(m_current);
            if (codePoint > UCHAR_MAX_VALUE) {
                // For raw template literal syntax, we consume `NotEscapeSequence`.
                // Here, we consume NotCodePoint's HexDigits.
                //
                // NotEscapeSequence ::
                //     u { [lookahread not one of HexDigit]
                //     u { NotCodePoint
                //     u { CodePoint [lookahead != }]
                //
                // NotCodePoint ::
                //     HexDigits but not if MV of HexDigits <= 0x10FFFF
                //
                // CodePoint ::
                //     HexDigits but not if MV of HexDigits > 0x10FFFF
                shift();
                while (isASCIIHexDigit(m_current))
                    shift();

                return atEnd() ? ParsedUnicodeEscapeValue::Incomplete : ParsedUnicodeEscapeValue::Invalid;
            }
            shift();
        } while (m_current != '}');
        shift();
        return codePoint;
    }

    auto character2 = peek(1);
    auto character3 = peek(2);
    auto character4 = peek(3);
    if (!isASCIIHexDigit(m_current) || !isASCIIHexDigit(character2) || !isASCIIHexDigit(character3) || !isASCIIHexDigit(character4)) [[unlikely]] {
        auto result = (m_code + 4) >= m_codeEnd ? ParsedUnicodeEscapeValue::Incomplete : ParsedUnicodeEscapeValue::Invalid;

        // For raw template literal syntax, we consume `NotEscapeSequence`.
        //
        // NotEscapeSequence ::
        //     u [lookahead not one of HexDigit][lookahead != {]
        //     u HexDigit [lookahead not one of HexDigit]
        //     u HexDigit HexDigit [lookahead not one of HexDigit]
        //     u HexDigit HexDigit HexDigit [lookahead not one of HexDigit]
        while (isASCIIHexDigit(m_current))
            shift();

        return result;
    }

    auto result = convertUnicode(m_current, character2, character3, character4);
    shift();
    shift();
    shift();
    shift();
    return result;
}

template <typename T>
void Lexer<T>::shiftLineTerminator()
{
    ASSERT(isLineTerminator(m_current));

    m_positionBeforeLastNewline = currentPosition();
    T prev = m_current;
    shift();

    if (prev == '\r' && m_current == '\n')
        shift();

    ++m_lineNumber;
    m_lineStart = m_code;
}

template <typename T>
ALWAYS_INLINE bool Lexer<T>::lastTokenWasRestrKeyword() const
{
    return m_lastToken == CONTINUE || m_lastToken == BREAK || m_lastToken == RETURN || m_lastToken == THROW;
}

template <typename T>
ALWAYS_INLINE void Lexer<T>::skipWhitespace()
{
    while (isWhiteSpace(m_current))
        shift();
}

static bool isNonLatin1IdentStart(char32_t c)
{
    return u_hasBinaryProperty(c, UCHAR_ID_START);
}

template<typename CharacterType>
static ALWAYS_INLINE bool isIdentStart(CharacterType c)
{
    static_assert(std::is_same_v<CharacterType, LChar> || std::is_same_v<CharacterType, char32_t>, "Call isSingleCharacterIdentStart for UChars that don't need to check for surrogate pairs");
    if (!isLatin1(c))
        return isNonLatin1IdentStart(c);
    return typesOfLatin1Characters[static_cast<LChar>(c)] == CharacterLatin1IdentifierStart;
}

static ALWAYS_INLINE UNUSED_FUNCTION bool isSingleCharacterIdentStart(char16_t c)
{
    if (isLatin1(c)) [[likely]]
        return isIdentStart(static_cast<LChar>(c));
    return !U16_IS_SURROGATE(c) && isIdentStart(static_cast<char32_t>(c));
}

static ALWAYS_INLINE bool cannotBeIdentStart(LChar c)
{
    return !isIdentStart(c) && c != '\\';
}

static ALWAYS_INLINE bool cannotBeIdentStart(char16_t c)
{
    if (isLatin1(c)) [[likely]]
        return cannotBeIdentStart(static_cast<LChar>(c));
    return Lexer<char16_t>::isWhiteSpace(c) || Lexer<char16_t>::isLineTerminator(c);
}

static NEVER_INLINE bool isNonLatin1IdentPart(char32_t c)
{
    return u_hasBinaryProperty(c, UCHAR_ID_CONTINUE) || c == 0x200C || c == 0x200D;
}

template<typename CharacterType>
static ALWAYS_INLINE bool isIdentPart(CharacterType c)
{
    static_assert(std::is_same_v<CharacterType, LChar> || std::is_same_v<CharacterType, char32_t>, "Call isSingleCharacterIdentPart for UChars that don't need to check for surrogate pairs");
    if (!isLatin1(c))
        return isNonLatin1IdentPart(c);

    // Character types are divided into two groups depending on whether they can be part of an
    // identifier or not. Those whose type value is less or equal than CharacterOtherIdentifierPart can be
    // part of an identifier. (See the CharacterType definition for more details.)
    return typesOfLatin1Characters[static_cast<LChar>(c)] <= CharacterOtherIdentifierPart;
}

static ALWAYS_INLINE bool isSingleCharacterIdentPart(char16_t c)
{
    if (isLatin1(c)) [[likely]]
        return isIdentPart(static_cast<LChar>(c));
    return !U16_IS_SURROGATE(c) && isIdentPart(static_cast<char32_t>(c));
}

static ALWAYS_INLINE bool cannotBeIdentPartOrEscapeStart(LChar c)
{
    return !isIdentPart(c) && c != '\\';
}

// NOTE: This may give give false negatives (for non-ascii) but won't give false posititves.
// This means it can be used to detect the end of a keyword (all keywords are ascii)
static ALWAYS_INLINE bool cannotBeIdentPartOrEscapeStart(char16_t c)
{
    if (isLatin1(c)) [[likely]]
        return cannotBeIdentPartOrEscapeStart(static_cast<LChar>(c));
    return Lexer<char16_t>::isWhiteSpace(c) || Lexer<char16_t>::isLineTerminator(c);
}


template<>
ALWAYS_INLINE char32_t Lexer<LChar>::currentCodePoint() const
{
    return m_current;
}

template<>
ALWAYS_INLINE char32_t Lexer<char16_t>::currentCodePoint() const
{
    ASSERT_WITH_MESSAGE(!isIdentStart(errorCodePoint), "error values shouldn't appear as a valid identifier start code point");
    if (!U16_IS_SURROGATE(m_current))
        return m_current;

    char16_t trail = peek(1);
    if (!U16_IS_LEAD(m_current) || !U16_IS_SURROGATE_TRAIL(trail)) [[unlikely]]
        return errorCodePoint;

    return U16_GET_SUPPLEMENTARY(m_current, trail);
}

template<typename CharacterType>
static inline bool isASCIIDigitOrSeparator(CharacterType character)
{
    return isASCIIDigit(character) || character == '_';
}

template<typename CharacterType>
static inline bool isASCIIHexDigitOrSeparator(CharacterType character)
{
    return isASCIIHexDigit(character) || character == '_';
}

template<typename CharacterType>
static inline bool isASCIIBinaryDigitOrSeparator(CharacterType character)
{
    return isASCIIBinaryDigit(character) || character == '_';
}

template<typename CharacterType>
static inline bool isASCIIOctalDigitOrSeparator(CharacterType character)
{
    return isASCIIOctalDigit(character) || character == '_';
}

static inline LChar singleEscape(int c)
{
    if (c < 128) {
        ASSERT(static_cast<size_t>(c) < std::size(singleCharacterEscapeValuesForASCII));
        return singleCharacterEscapeValuesForASCII[c];
    }
    return 0;
}

template <typename T>
inline void Lexer<T>::record8(int c)
{
    ASSERT(isLatin1(c));
    m_buffer8.append(static_cast<LChar>(c));
}

template <typename T>
inline void Lexer<T>::append8(std::span<const T> span)
{
    size_t currentSize = m_buffer8.size();
    m_buffer8.grow(currentSize + span.size());
    LChar* rawBuffer = m_buffer8.mutableSpan().data() + currentSize;

    for (size_t i = 0; i < span.size(); i++) {
        T c = span[i];
        ASSERT(isLatin1(c));
        rawBuffer[i] = c;
    }
}

template <typename T>
inline void Lexer<T>::append16(std::span<const LChar> span)
{
    size_t currentSize = m_buffer16.size();
    m_buffer16.grow(currentSize + span.size());
    char16_t* rawBuffer = m_buffer16.mutableSpan().data() + currentSize;

    for (size_t i = 0; i < span.size(); i++)
        rawBuffer[i] = span[i];
}

template <typename T>
inline void Lexer<T>::record16(T c)
{
    m_buffer16.append(c);
}

template <typename T>
inline void Lexer<T>::record16(int c)
{
    ASSERT(c >= 0);
    ASSERT(c <= static_cast<int>(USHRT_MAX));
    m_buffer16.append(static_cast<char16_t>(c));
}
    
template<typename CharacterType> inline void Lexer<CharacterType>::recordUnicodeCodePoint(char32_t codePoint)
{
    ASSERT(codePoint <= UCHAR_MAX_VALUE);
    if (U_IS_BMP(codePoint))
        record16(static_cast<char16_t>(codePoint));
    else {
        char16_t codeUnits[2] = { U16_LEAD(codePoint), U16_TRAIL(codePoint) };
        append16(codeUnits);
    }
}

#if ASSERT_ENABLED
bool isSafeBuiltinIdentifier(VM& vm, const Identifier* ident)
{
    if (!ident)
        return true;
    /* Just block any use of suspicious identifiers.  This is intended to
     * be used as a safety net while implementing builtins.
     */
    // FIXME: How can a debug-only assertion be a safety net?
    if (*ident == vm.propertyNames->builtinNames().callPublicName())
        return false;
    if (*ident == vm.propertyNames->builtinNames().applyPublicName())
        return false;
    if (*ident == vm.propertyNames->eval)
        return false;
    if (*ident == vm.propertyNames->Function)
        return false;
    return true;
}
#endif // ASSERT_ENABLED
    
template <>
template <bool shouldCreateIdentifier> ALWAYS_INLINE JSTokenType Lexer<LChar>::parseIdentifier(JSTokenData* tokenData, OptionSet<LexerFlags> lexerFlags, bool strictMode)
{
    tokenData->escaped = false;
    const ptrdiff_t remaining = m_codeEnd - m_code;
    if ((remaining >= maxTokenLength) && !lexerFlags.contains(LexerFlags::IgnoreReservedWords)) {
        JSTokenType keyword = parseKeyword<shouldCreateIdentifier>(tokenData);
        if (keyword != IDENT) {
            ASSERT((!shouldCreateIdentifier) || tokenData->ident);
            return keyword == RESERVED_IF_STRICT && !strictMode ? IDENT : keyword;
        }
    }

    bool isPrivateName = m_current == '#';
    bool isBuiltinName = m_current == '@' && m_parsingBuiltinFunction;
    bool isWellKnownSymbol = false;
    if (isBuiltinName) {
        ASSERT(m_parsingBuiltinFunction);
        shift();
        if (m_current == '@') {
            isWellKnownSymbol = true;
            shift();
        }
    }

    const LChar* identifierStart = currentSourcePtr();

    if (isPrivateName)
        shift();

    ASSERT(isIdentStart(m_current) || m_current == '\\');
    while (isIdentPart(m_current))
        shift();
    
    if (m_current == '\\') [[unlikely]]
        return parseIdentifierSlowCase<shouldCreateIdentifier>(tokenData, lexerFlags, strictMode, identifierStart);

    const Identifier* ident = nullptr;
    
    if (shouldCreateIdentifier || m_parsingBuiltinFunction) {
        std::span identifierSpan { identifierStart, static_cast<size_t>(currentSourcePtr() - identifierStart) };
        if (m_parsingBuiltinFunction && isBuiltinName) {
            if (isWellKnownSymbol)
                ident = &m_arena->makeIdentifier(m_vm, m_vm.propertyNames->builtinNames().lookUpWellKnownSymbol(identifierSpan));
            else
                ident = &m_arena->makeIdentifier(m_vm, m_vm.propertyNames->builtinNames().lookUpPrivateName(identifierSpan));
            if (!ident)
                return INVALID_PRIVATE_NAME_ERRORTOK;
        } else {
            ident = makeIdentifier(identifierSpan);
            if (m_parsingBuiltinFunction) {
                if (!isSafeBuiltinIdentifier(m_vm, ident)) {
                    m_lexErrorMessage = makeString("The use of '"_s, ident->string(), "' is disallowed in builtin functions."_s);
                    return ERRORTOK;
                }
                if (*ident == m_vm.propertyNames->undefinedKeyword)
                    tokenData->ident = &m_vm.propertyNames->undefinedPrivateName;
            }
        }
        tokenData->ident = ident;
    } else
        tokenData->ident = nullptr;

    auto identType = isPrivateName ? PRIVATENAME : IDENT;
    if ((remaining < maxTokenLength) && !lexerFlags.contains(LexerFlags::IgnoreReservedWords)) [[unlikely]] {
        if (!isBuiltinName) {
            ASSERT(shouldCreateIdentifier);
            if (remaining < maxTokenLength) {
                const HashTableValue* entry = JSC::mainTable.entry(*ident);
                ASSERT((remaining < maxTokenLength) || !entry);
                if (!entry)
                    return identType;
                JSTokenType token = static_cast<JSTokenType>(entry->lexerValue());
                return (token != RESERVED_IF_STRICT) || strictMode ? token : identType;
            }
            return identType;
        }
    }

    return identType;
}

template <>
template <bool shouldCreateIdentifier> ALWAYS_INLINE JSTokenType Lexer<char16_t>::parseIdentifier(JSTokenData* tokenData, OptionSet<LexerFlags> lexerFlags, bool strictMode)
{
    ASSERT(!m_parsingBuiltinFunction);
    tokenData->escaped = false;
    const ptrdiff_t remaining = m_codeEnd - m_code;
    if ((remaining >= maxTokenLength) && !lexerFlags.contains(LexerFlags::IgnoreReservedWords)) {
        JSTokenType keyword = parseKeyword<shouldCreateIdentifier>(tokenData);
        if (keyword != IDENT) {
            ASSERT((!shouldCreateIdentifier) || tokenData->ident);
            return keyword == RESERVED_IF_STRICT && !strictMode ? IDENT : keyword;
        }
    }

    bool isPrivateName = m_current == '#';
    const char16_t* identifierStart = currentSourcePtr();

    if (isPrivateName)
        shift();

    char16_t orAllChars = 0;
    ASSERT(isSingleCharacterIdentStart(m_current) || U16_IS_SURROGATE(m_current) || m_current == '\\');
    while (isSingleCharacterIdentPart(m_current)) {
        orAllChars |= m_current;
        shift();
    }
    
    if (U16_IS_SURROGATE(m_current) || m_current == '\\') [[unlikely]]
        return parseIdentifierSlowCase<shouldCreateIdentifier>(tokenData, lexerFlags, strictMode, identifierStart);

    bool isAll8Bit = !(orAllChars & ~0xff);
    const Identifier* ident = nullptr;
    
    if (shouldCreateIdentifier) {
        if (isAll8Bit)
            ident = makeIdentifierLCharFromUChar(std::span { identifierStart, currentSourcePtr() });
        else
            ident = makeIdentifier(std::span { identifierStart, currentSourcePtr() });
        tokenData->ident = ident;
    } else
        tokenData->ident = nullptr;
    
    if (isPrivateName)
        return PRIVATENAME;

    if ((remaining < maxTokenLength) && !lexerFlags.contains(LexerFlags::IgnoreReservedWords)) [[unlikely]] {
        ASSERT(shouldCreateIdentifier);
        if (remaining < maxTokenLength) {
            const HashTableValue* entry = JSC::mainTable.entry(*ident);
            ASSERT((remaining < maxTokenLength) || !entry);
            if (!entry)
                return IDENT;
            JSTokenType token = static_cast<JSTokenType>(entry->lexerValue());
            return (token != RESERVED_IF_STRICT) || strictMode ? token : IDENT;
        }
        return IDENT;
    }

    return IDENT;
}

template<typename CharacterType>
template<bool shouldCreateIdentifier>
JSTokenType Lexer<CharacterType>::parseIdentifierSlowCase(JSTokenData* tokenData, OptionSet<LexerFlags> lexerFlags, bool strictMode, const CharacterType* identifierStart)
{
    ASSERT(U16_IS_SURROGATE(m_current) || m_current == '\\');
    ASSERT(m_buffer16.isEmpty());
    ASSERT(!tokenData->escaped);

    auto identCharsStart = identifierStart;
    bool isPrivateName = *identifierStart == '#';
    if (isPrivateName)
        ++identCharsStart;

    JSTokenType identType = isPrivateName ? PRIVATENAME : IDENT;
    ASSERT(!isPrivateName || identifierStart != currentSourcePtr());

    auto fillBuffer = [&] (bool isStart = false) {
        // \uXXXX unicode characters or Surrogate pairs.
        if (identifierStart != currentSourcePtr())
            m_buffer16.append(std::span(identifierStart, currentSourcePtr() - identifierStart));

        if (m_current == '\\') {
            tokenData->escaped = true;
            shift();
            if (m_current != 'u') [[unlikely]]
                return atEnd() ? UNTERMINATED_IDENTIFIER_ESCAPE_ERRORTOK : INVALID_IDENTIFIER_ESCAPE_ERRORTOK;
            shift();
            auto character = parseUnicodeEscape();
            if (!character.isValid()) [[unlikely]]
                return character.isIncomplete() ? UNTERMINATED_IDENTIFIER_UNICODE_ESCAPE_ERRORTOK : INVALID_IDENTIFIER_UNICODE_ESCAPE_ERRORTOK;
            if (isStart ? !isIdentStart(character.value()) : !isIdentPart(character.value())) [[unlikely]]
                return INVALID_IDENTIFIER_UNICODE_ESCAPE_ERRORTOK;
            if (shouldCreateIdentifier)
                recordUnicodeCodePoint(character.value());
            identifierStart = currentSourcePtr();
            return identType;
        }

        ASSERT(U16_IS_SURROGATE(m_current));
        if (!U16_IS_SURROGATE_LEAD(m_current)) [[unlikely]]
            return INVALID_UNICODE_ENCODING_ERRORTOK;

        char32_t codePoint = currentCodePoint();
        if (codePoint == errorCodePoint) [[unlikely]]
            return INVALID_UNICODE_ENCODING_ERRORTOK;
        if (isStart ? !isNonLatin1IdentStart(codePoint) : !isNonLatin1IdentPart(codePoint)) [[unlikely]]
            return INVALID_IDENTIFIER_UNICODE_ERRORTOK;
        append16({ m_code, 2 });
        shift();
        shift();
        identifierStart = currentSourcePtr();
        return identType;
    };

    JSTokenType type = fillBuffer(identCharsStart == currentSourcePtr());
    if (type & CanBeErrorTokenFlag) [[unlikely]]
        return type;

    while (true) {
        if (isSingleCharacterIdentPart(m_current)) [[likely]] {
            shift();
            continue;
        }
        if (!U16_IS_SURROGATE(m_current) && m_current != '\\')
            break;

        type = fillBuffer();
        if (type & CanBeErrorTokenFlag) [[unlikely]]
            return type;
    }

    const Identifier* ident = nullptr;
    if (shouldCreateIdentifier) {
        if (identifierStart != currentSourcePtr())
            m_buffer16.append(std::span(identifierStart, currentSourcePtr() - identifierStart));
        ident = makeIdentifier(m_buffer16.span());

        tokenData->ident = ident;
    } else
        tokenData->ident = nullptr;

    m_buffer16.shrink(0);

    if (!lexerFlags.contains(LexerFlags::IgnoreReservedWords)) [[likely]] {
        ASSERT(shouldCreateIdentifier);
        const HashTableValue* entry = JSC::mainTable.entry(*ident);
        if (!entry)
            return identType;
        JSTokenType token = static_cast<JSTokenType>(entry->lexerValue());
        if ((token != RESERVED_IF_STRICT) || strictMode)
            return ESCAPED_KEYWORD;
    }

    return identType;
}

static ALWAYS_INLINE bool characterRequiresParseStringSlowCase(LChar character)
{
    return character < 0xE;
}

static ALWAYS_INLINE bool characterRequiresParseStringSlowCase(char16_t character)
{
    return character < 0xE || !isLatin1(character);
}

template <typename T>
template <bool shouldBuildStrings> ALWAYS_INLINE typename Lexer<T>::StringParseResult Lexer<T>::parseString(JSTokenData* tokenData, bool strictMode)
{
    int startingOffset = currentOffset();
    int startingLineStartOffset = currentLineStartOffset();
    int startingLineNumber = lineNumber();
    T stringQuoteCharacter = m_current;
    shift();

    const T* stringStart = currentSourcePtr();

    while (m_current != stringQuoteCharacter) {
        if (m_current == '\\') [[unlikely]] {
            if (stringStart != currentSourcePtr() && shouldBuildStrings)
                append8({ stringStart, currentSourcePtr() });
            shift();

            LChar escape = singleEscape(m_current);

            // Most common escape sequences first.
            if (escape) {
                if (shouldBuildStrings)
                    record8(escape);
                shift();
            } else if (isLineTerminator(m_current)) [[unlikely]]
                shiftLineTerminator();
            else if (m_current == 'x') {
                shift();
                if (!isASCIIHexDigit(m_current) || !isASCIIHexDigit(peek(1))) {
                    m_lexErrorMessage = "\\x can only be followed by a hex character sequence"_s;
                    return (atEnd() || (isASCIIHexDigit(m_current) && (m_code + 1 == m_codeEnd))) ? StringUnterminated : StringCannotBeParsed;
                }
                T prev = m_current;
                shift();
                if (shouldBuildStrings)
                    record8(convertHex(prev, m_current));
                shift();
            } else {
                setOffset(startingOffset, startingLineStartOffset);
                setLineNumber(startingLineNumber);
                m_buffer8.shrink(0);
                return parseStringSlowCase<shouldBuildStrings>(tokenData, strictMode);
            }
            stringStart = currentSourcePtr();
            continue;
        }

        if (characterRequiresParseStringSlowCase(m_current)) [[unlikely]] {
            setOffset(startingOffset, startingLineStartOffset);
            setLineNumber(startingLineNumber);
            m_buffer8.shrink(0);
            return parseStringSlowCase<shouldBuildStrings>(tokenData, strictMode);
        }

        shift();
    }

    if (currentSourcePtr() != stringStart && shouldBuildStrings)
        append8({ stringStart, currentSourcePtr() });
    if (shouldBuildStrings) {
        tokenData->ident = makeIdentifier(m_buffer8.span());
        m_buffer8.shrink(0);
    } else
        tokenData->ident = nullptr;

    return StringParsedSuccessfully;
}

template <typename T>
template <bool shouldBuildStrings> ALWAYS_INLINE auto Lexer<T>::parseComplexEscape(bool strictMode) -> StringParseResult
{
    if (m_current == 'x') {
        shift();
        if (!isASCIIHexDigit(m_current) || !isASCIIHexDigit(peek(1))) {
            // For raw template literal syntax, we consume `NotEscapeSequence`.
            //
            // NotEscapeSequence ::
            //     x [lookahread not one of HexDigit]
            //     x HexDigit [lookahread not one of HexDigit]
            if (isASCIIHexDigit(m_current))
                shift();
            ASSERT(!isASCIIHexDigit(m_current));

            m_lexErrorMessage = "\\x can only be followed by a hex character sequence"_s;
            return atEnd() ? StringUnterminated : StringCannotBeParsed;
        }

        T prev = m_current;
        shift();
        if (shouldBuildStrings)
            record16(convertHex(prev, m_current));
        shift();

        return StringParsedSuccessfully;
    }

    if (m_current == 'u') {
        shift();

        auto character = parseUnicodeEscape();
        if (character.isValid()) {
            if (shouldBuildStrings)
                recordUnicodeCodePoint(character.value());
            return StringParsedSuccessfully;
        }

        m_lexErrorMessage = "\\u can only be followed by a Unicode character sequence"_s;
        return atEnd() ? StringUnterminated : StringCannotBeParsed;
    }

    if (strictMode) {
        if (isASCIIDigit(m_current)) {
            // The only valid numeric escape in strict mode is '\0', and this must not be followed by a decimal digit.
            int character1 = m_current;
            shift();
            if (character1 != '0' || isASCIIDigit(m_current)) {
                // For raw template literal syntax, we consume `NotEscapeSequence`.
                //
                // NotEscapeSequence ::
                //     0 DecimalDigit
                //     DecimalDigit but not 0
                if (character1 == '0')
                    shift();

                m_lexErrorMessage = "The only valid numeric escape in strict mode is '\\0'"_s;
                return atEnd() ? StringUnterminated : StringCannotBeParsed;
            }
            if (shouldBuildStrings)
                record16(0);
            return StringParsedSuccessfully;
        }
    } else {
        if (isASCIIOctalDigit(m_current)) {
            // Octal character sequences
            T character1 = m_current;
            shift();
            if (isASCIIOctalDigit(m_current)) {
                // Two octal characters
                T character2 = m_current;
                shift();
                if (character1 >= '0' && character1 <= '3' && isASCIIOctalDigit(m_current)) {
                    if (shouldBuildStrings)
                        record16((character1 - '0') * 64 + (character2 - '0') * 8 + m_current - '0');
                    shift();
                } else {
                    if (shouldBuildStrings)
                        record16((character1 - '0') * 8 + character2 - '0');
                }
            } else {
                if (shouldBuildStrings)
                    record16(character1 - '0');
            }
            return StringParsedSuccessfully;
        }
    }

    if (!atEnd()) {
        if (shouldBuildStrings)
            record16(m_current);
        shift();
        return StringParsedSuccessfully;
    }

    m_lexErrorMessage = "Unterminated string constant"_s;
    return StringUnterminated;
}

template <typename T>
template <bool shouldBuildStrings> auto Lexer<T>::parseStringSlowCase(JSTokenData* tokenData, bool strictMode) -> StringParseResult
{
    T stringQuoteCharacter = m_current;
    shift();

    const T* stringStart = currentSourcePtr();

    while (m_current != stringQuoteCharacter) {
        if (m_current == '\\') [[unlikely]] {
            if (stringStart != currentSourcePtr() && shouldBuildStrings)
                append16({ stringStart, currentSourcePtr() });
            shift();

            LChar escape = singleEscape(m_current);

            // Most common escape sequences first
            if (escape) {
                if (shouldBuildStrings)
                    record16(escape);
                shift();
            } else if (isLineTerminator(m_current)) [[unlikely]]
                shiftLineTerminator();
            else {
                StringParseResult result = parseComplexEscape<shouldBuildStrings>(strictMode);
                if (result != StringParsedSuccessfully)
                    return result;
            }

            stringStart = currentSourcePtr();
            continue;
        }
        // Fast check for characters that require special handling.
        // Catches 0, \n, and \r as efficiently as possible, and lets through all common ASCII characters.
        static_assert(std::is_unsigned<T>::value, "Lexer expects an unsigned character type");
        if (m_current < 0xE) [[unlikely]] {
            // New-line or end of input is not allowed
            if (atEnd() || m_current == '\r' || m_current == '\n') {
                m_lexErrorMessage = "Unexpected EOF"_s;
                return atEnd() ? StringUnterminated : StringCannotBeParsed;
            }
            // Anything else is just a normal character
        }
        shift();
    }

    if (currentSourcePtr() != stringStart && shouldBuildStrings)
        append16({ stringStart, currentSourcePtr() });
    if (shouldBuildStrings)
        tokenData->ident = makeIdentifier(m_buffer16.span());
    else
        tokenData->ident = nullptr;

    m_buffer16.shrink(0);
    return StringParsedSuccessfully;
}

template <typename T>
typename Lexer<T>::StringParseResult Lexer<T>::parseTemplateLiteral(JSTokenData* tokenData, RawStringsBuildMode rawStringsBuildMode)
{
    bool parseCookedFailed = false;
    const T* stringStart = currentSourcePtr();
    const T* rawStringStart = currentSourcePtr();

    while (m_current != '`') {
        if (m_current == '\\') [[unlikely]] {
            if (stringStart != currentSourcePtr())
                append16({ stringStart, currentSourcePtr() });
            shift();

            LChar escape = singleEscape(m_current);

            // Most common escape sequences first.
            if (escape) {
                record16(escape);
                shift();
            } else if (isLineTerminator(m_current)) [[unlikely]] {
                // Normalize <CR>, <CR><LF> to <LF>.
                if (m_current == '\r') {
                    ASSERT_WITH_MESSAGE(rawStringStart != currentSourcePtr(), "We should have at least shifted the escape.");

                    if (rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings) {
                        m_bufferForRawTemplateString16.append(std::span(rawStringStart, currentSourcePtr() - rawStringStart));
                        m_bufferForRawTemplateString16.append('\n');
                    }

                    shiftLineTerminator();
                    rawStringStart = currentSourcePtr();
                } else
                    shiftLineTerminator();
            } else {
                bool strictMode = true;
                StringParseResult result = parseComplexEscape<true>(strictMode);
                if (result != StringParsedSuccessfully) {
                    if (rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings && result == StringCannotBeParsed)
                        parseCookedFailed = true;
                    else
                        return result;
                }
            }

            stringStart = currentSourcePtr();
            continue;
        }

        if (m_current == '$' && peek(1) == '{')
            break;

        // Fast check for characters that require special handling.
        // Catches 0, \n, \r, 0x2028, and 0x2029 as efficiently
        // as possible, and lets through all common ASCII characters.
        if (((static_cast<unsigned>(m_current) - 0xE) & 0x2000)) [[unlikely]] {
            // End of input is not allowed.
            // Unlike String, line terminator is allowed.
            if (atEnd()) {
                m_lexErrorMessage = "Unexpected EOF"_s;
                return StringUnterminated;
            }

            if (isLineTerminator(m_current)) {
                if (m_current == '\r') {
                    // Normalize <CR>, <CR><LF> to <LF>.
                    if (stringStart != currentSourcePtr())
                        append16({ stringStart, currentSourcePtr() });
                    if (rawStringStart != currentSourcePtr() && rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings)
                        m_bufferForRawTemplateString16.append(std::span(rawStringStart, currentSourcePtr() - rawStringStart));

                    record16('\n');
                    if (rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings)
                        m_bufferForRawTemplateString16.append('\n');
                    shiftLineTerminator();
                    stringStart = currentSourcePtr();
                    rawStringStart = currentSourcePtr();
                } else
                    shiftLineTerminator();
                continue;
            }
            // Anything else is just a normal character
        }

        shift();
    }

    bool isTail = m_current == '`';

    if (currentSourcePtr() != stringStart)
        append16({ stringStart, currentSourcePtr() });
    if (rawStringStart != currentSourcePtr() && rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings)
        m_bufferForRawTemplateString16.append(std::span { rawStringStart, currentSourcePtr() });

    if (!parseCookedFailed)
        tokenData->cooked = makeIdentifier(m_buffer16.span());
    else
        tokenData->cooked = nullptr;

    // Line terminator normalization (e.g. <CR> => <LF>) should be applied to both the raw and cooked representations.
    if (rawStringsBuildMode == RawStringsBuildMode::BuildRawStrings)
        tokenData->raw = makeIdentifier(m_bufferForRawTemplateString16.span());
    else
        tokenData->raw = nullptr;

    tokenData->isTail = isTail;

    m_buffer16.shrink(0);
    m_bufferForRawTemplateString16.shrink(0);

    if (isTail) {
        // Skip `
        shift();
    } else {
        // Skip $ and {
        shift();
        shift();
    }

    return StringParsedSuccessfully;
}

template <typename T>
ALWAYS_INLINE auto Lexer<T>::parseHex() -> std::optional<NumberParseResult>
{
    ASSERT(isASCIIHexDigit(m_current));

    // Optimization: most hexadecimal values fit into 4 bytes.
    uint32_t hexValue = 0;
    int maximumDigits = 7;

    do {
        if (m_current == '_') {
            if (!isASCIIHexDigit(peek(1))) [[unlikely]]
                return std::nullopt;

            shift();
        }

        hexValue = (hexValue << 4) + toASCIIHexValue(m_current);
        shift();
        --maximumDigits;
    } while (isASCIIHexDigitOrSeparator(m_current) && maximumDigits >= 0);

    if (maximumDigits >= 0 && m_current != 'n') [[likely]]
        return NumberParseResult { static_cast<double>(hexValue) };

    // No more place in the hexValue buffer.
    // The values are shifted out and placed into the m_buffer8 vector.
    for (int i = 0; i < 8; ++i) {
         int digit = hexValue >> 28;
         if (digit < 10)
             record8(digit + '0');
         else
             record8(digit - 10 + 'a');
         hexValue <<= 4;
    }

    while (isASCIIHexDigitOrSeparator(m_current)) {
        if (m_current == '_') {
            if (!isASCIIHexDigit(peek(1))) [[unlikely]]
                return std::nullopt;

            shift();
        }

        record8(m_current);
        shift();
    }

    if (m_current == 'n') [[unlikely]]
        return NumberParseResult { makeIdentifier(m_buffer8.span()) };
    
    return NumberParseResult { parseIntOverflow(m_buffer8.span(), 16) };
}

template <typename T>
ALWAYS_INLINE auto Lexer<T>::parseBinary() -> std::optional<NumberParseResult>
{
    ASSERT(isASCIIBinaryDigit(m_current));

    // Optimization: most binary values fit into 4 bytes.
    uint32_t binaryValue = 0;
    const unsigned maximumDigits = 32;
    int digit = maximumDigits - 1;
    // Temporary buffer for the digits. Makes easier
    // to reconstruct the input characters when needed.
    LChar digits[maximumDigits];

    do {
        if (m_current == '_') {
            if (!isASCIIBinaryDigit(peek(1))) [[unlikely]]
                return std::nullopt;

            shift();
        }

        binaryValue = (binaryValue << 1) + (m_current - '0');
        digits[digit] = m_current;
        shift();
        --digit;
    } while (isASCIIBinaryDigitOrSeparator(m_current) && digit >= 0);

    if (!isASCIIDigitOrSeparator(m_current) && digit >= 0 && m_current != 'n') [[likely]]
        return NumberParseResult { static_cast<double>(binaryValue) };

    for (int i = maximumDigits - 1; i > digit; --i)
        record8(digits[i]);

    while (isASCIIBinaryDigitOrSeparator(m_current)) {
        if (m_current == '_') {
            if (!isASCIIBinaryDigit(peek(1))) [[unlikely]]
                return std::nullopt;

            shift();
        }

        record8(m_current);
        shift();
    }

    if (m_current == 'n') [[unlikely]]
        return NumberParseResult { makeIdentifier(m_buffer8.span()) };

    if (isASCIIDigit(m_current))
        return std::nullopt;

    return NumberParseResult { parseIntOverflow(m_buffer8.span(), 2) };
}

template <typename T>
ALWAYS_INLINE auto Lexer<T>::parseOctal() -> std::optional<NumberParseResult>
{
    ASSERT(isASCIIOctalDigit(m_current));
    ASSERT(!m_buffer8.size() || (m_buffer8.size() == 1 && m_buffer8[0] == '0'));
    bool isLegacyLiteral = m_buffer8.size();

    // Optimization: most octal values fit into 4 bytes.
    uint32_t octalValue = 0;
    const unsigned maximumDigits = 10;
    int digit = maximumDigits - 1;
    // Temporary buffer for the digits. Makes easier
    // to reconstruct the input characters when needed.
    LChar digits[maximumDigits];

    do {
        if (m_current == '_') {
            if (!isASCIIOctalDigit(peek(1)) || isLegacyLiteral) [[unlikely]]
                return std::nullopt;

            shift();
        }

        octalValue = octalValue * 8 + (m_current - '0');
        digits[digit] = m_current;
        shift();
        --digit;
    } while (isASCIIOctalDigitOrSeparator(m_current) && digit >= 0);

    if (!isASCIIDigitOrSeparator(m_current) && digit >= 0 && m_current != 'n') [[likely]]
        return NumberParseResult { static_cast<double>(octalValue) };

    for (int i = maximumDigits - 1; i > digit; --i)
         record8(digits[i]);

    while (isASCIIOctalDigitOrSeparator(m_current)) {
        if (m_current == '_') {
            if (!isASCIIOctalDigit(peek(1)) || isLegacyLiteral) [[unlikely]]
                return std::nullopt;

            shift();
        }

        record8(m_current);
        shift();
    }

    if (m_current == 'n') [[unlikely]] {
        if (!isLegacyLiteral)
            return NumberParseResult { makeIdentifier(m_buffer8.span()) };
    }

    if (isASCIIDigit(m_current))
        return std::nullopt;

    return NumberParseResult { parseIntOverflow(m_buffer8.span(), 8) };
}

template <typename T>
ALWAYS_INLINE auto Lexer<T>::parseDecimal() -> std::optional<NumberParseResult>
{
    ASSERT(isASCIIDigit(m_current) || m_buffer8.size());
    bool isLegacyLiteral = m_buffer8.size() && isASCIIDigitOrSeparator(m_current);

    // Optimization: most decimal values fit into 4 bytes.
    uint32_t decimalValue = 0;

    // Since parseOctal may be executed before parseDecimal,
    // the m_buffer8 may hold ascii digits.
    if (!m_buffer8.size()) {
        const unsigned maximumDigits = 10;
        int digit = maximumDigits - 1;
        // Temporary buffer for the digits. Makes easier
        // to reconstruct the input characters when needed.
        LChar digits[maximumDigits];

        do {
            if (m_current == '_') {
                if (!isASCIIDigit(peek(1)) || isLegacyLiteral) [[unlikely]]
                    return std::nullopt;

                shift();
            }

            decimalValue = decimalValue * 10 + (m_current - '0');
            digits[digit] = m_current;
            shift();
            --digit;
        } while (isASCIIDigitOrSeparator(m_current) && digit >= 0);

        if (digit >= 0 && m_current != '.' && !isASCIIAlphaCaselessEqual(m_current, 'e') && m_current != 'n')
            return NumberParseResult { static_cast<double>(decimalValue) };

        for (int i = maximumDigits - 1; i > digit; --i)
            record8(digits[i]);
    }

    while (isASCIIDigitOrSeparator(m_current)) {
        if (m_current == '_') {
            if (!isASCIIDigit(peek(1)) || isLegacyLiteral) [[unlikely]]
                return std::nullopt;

            shift();
        }

        record8(m_current);
        shift();
    }
    
    if (m_current == 'n' && !isLegacyLiteral) [[unlikely]]
        return NumberParseResult { makeIdentifier(m_buffer8.span()) };

    return std::nullopt;
}

template <typename T>
ALWAYS_INLINE bool Lexer<T>::parseNumberAfterDecimalPoint()
{
    ASSERT(isASCIIDigit(m_current));
    record8('.');

    do {
        if (m_current == '_') {
            if (!isASCIIDigit(peek(1))) [[unlikely]]
                return false;

            shift();
        }

        record8(m_current);
        shift();
    } while (isASCIIDigitOrSeparator(m_current));

    return true;
}

template <typename T>
ALWAYS_INLINE bool Lexer<T>::parseNumberAfterExponentIndicator()
{
    record8('e');
    shift();
    if (m_current == '+' || m_current == '-') {
        record8(m_current);
        shift();
    }

    if (!isASCIIDigit(m_current))
        return false;

    do {
        if (m_current == '_') {
            if (!isASCIIDigit(peek(1))) [[unlikely]]
                return false;

            shift();
        }

        record8(m_current);
        shift();
    } while (isASCIIDigitOrSeparator(m_current));

    return true;
}

template <typename T>
ALWAYS_INLINE bool Lexer<T>::parseMultilineComment()
{
    while (true) {
        while (m_current == '*') [[unlikely]] {
            shift();
            if (m_current == '/') {
                shift();
                return true;
            }
        }

        if (atEnd())
            return false;

        if (isLineTerminator(m_current)) {
            shiftLineTerminator();
            m_hasLineTerminatorBeforeToken = true;
        } else
            shift();
    }
}

template <typename T>
ALWAYS_INLINE void Lexer<T>::parseCommentDirective()
{
    // sourceURL and sourceMappingURL directives.
    if (!consume("source"))
        return;

    if (consume("URL=")) {
        m_sourceURLDirective = parseCommentDirectiveValue();
        return;
    }

    if (consume("MappingURL=")) {
        m_sourceMappingURLDirective = parseCommentDirectiveValue();
        return;
    }
}

IGNORE_WARNINGS_BEGIN("unused-but-set-variable")
template<typename CharacterType> ALWAYS_INLINE String Lexer<CharacterType>::parseCommentDirectiveValue()
{
    skipWhitespace();
    char16_t mergedCharacterBits = 0;
    auto stringStart = currentSourcePtr();
    while (!isWhiteSpace(m_current) && !isLineTerminator(m_current) && m_current != '"' && m_current != '\'' && !atEnd()) {
        if constexpr (std::is_same_v<CharacterType, char16_t>)
            mergedCharacterBits |= m_current;
        shift();
    }
    std::span commentDirective { stringStart, currentSourcePtr() };

    skipWhitespace();
    if (!isLineTerminator(m_current) && !atEnd())
        return String();

    if constexpr (std::is_same_v<CharacterType, char16_t>) {
        if (isLatin1(mergedCharacterBits))
            return String::make8Bit(commentDirective);
    }
    return commentDirective;
}
IGNORE_WARNINGS_END

template <typename T>
template <unsigned length>
ALWAYS_INLINE bool Lexer<T>::consume(const char (&input)[length])
{
    unsigned lengthToCheck = length - 1; // Ignore the ending NULL byte in the string literal.

    unsigned i = 0;
    for (; i < lengthToCheck && m_current == input[i]; i++)
        shift();

    return i == lengthToCheck;
}

template <typename T>
bool Lexer<T>::nextTokenIsColon()
{
    const T* code = m_code;
    while (code < m_codeEnd && (isWhiteSpace(*code) || isLineTerminator(*code)))
        code++;
    
    return code < m_codeEnd && *code == ':';
}

template <typename T>
void Lexer<T>::fillTokenInfo(JSToken* tokenRecord, JSTokenType token, int lineNumber, int endOffset, int lineStartOffset, JSTextPosition endPosition)
{
    JSTokenLocation* tokenLocation = &tokenRecord->m_location;
    tokenLocation->line = lineNumber;
    tokenLocation->endOffset = endOffset;
    tokenLocation->lineStartOffset = lineStartOffset;
    ASSERT(tokenLocation->endOffset >= tokenLocation->lineStartOffset);
    tokenRecord->m_endPosition = endPosition;
    m_lastToken = token;
}

template <typename T>
JSTokenType Lexer<T>::lexWithoutClearingLineTerminator(JSToken* tokenRecord, OptionSet<LexerFlags> lexerFlags, bool strictMode)
{
    JSTokenData* tokenData = &tokenRecord->m_data;
    JSTokenLocation* tokenLocation = &tokenRecord->m_location;
    m_lastTokenLocation = JSTokenLocation(tokenRecord->m_location);
    
    ASSERT(!m_error);
    ASSERT(m_buffer8.isEmpty());
    ASSERT(m_buffer16.isEmpty());

    JSTokenType token = ERRORTOK;

start:
    skipWhitespace();

    tokenLocation->startOffset = currentOffset();
    ASSERT(currentOffset() >= currentLineStartOffset());
    tokenRecord->m_startPosition = currentPosition();

    if (atEnd()) {
        token = EOFTOK;
        goto returnToken;
    }

    CharacterType type;
    if (isLatin1(m_current)) [[likely]]
        type = static_cast<CharacterType>(typesOfLatin1Characters[m_current]);
    else {
        char32_t codePoint;
        U16_GET(m_code, 0, 0, m_codeEnd - m_code, codePoint);
        if (isNonLatin1IdentStart(codePoint))
            type = CharacterNonLatin1IdentifierStart;
        else if (isLineTerminator(m_current))
            type = CharacterLineTerminator;
        else
            type = CharacterInvalid;
    }

    switch (type) {
    case CharacterGreater:
        shift();
        if (m_current == '>') {
            shift();
            if (m_current == '>') {
                shift();
                if (m_current == '=') {
                    shift();
                    token = URSHIFTEQUAL;
                    break;
                }
                token = URSHIFT;
                break;
            }
            if (m_current == '=') {
                shift();
                token = RSHIFTEQUAL;
                break;
            }
            token = RSHIFT;
            break;
        }
        if (m_current == '=') {
            shift();
            token = GE;
            break;
        }
        token = GT;
        break;
    case CharacterEqual: {
        if (peek(1) == '>') {
            token = ARROWFUNCTION;
            tokenData->line = lineNumber();
            tokenData->offset = currentOffset();
            tokenData->lineStartOffset = currentLineStartOffset();
            ASSERT(tokenData->offset >= tokenData->lineStartOffset);
            shift();
            shift();
            break;
        }

        shift();
        if (m_current == '=') {
            shift();
            if (m_current == '=') {
                shift();
                token = STREQ;
                break;
            }
            token = EQEQ;
            break;
        }
        token = EQUAL;
        break;
    }
    case CharacterLess:
        shift();
        if (m_current == '!' && peek(1) == '-' && peek(2) == '-') {
            if (m_scriptMode == JSParserScriptMode::Classic) {
                // <!-- marks the beginning of a line comment (for www usage)
                goto inSingleLineComment;
            }
        }
        if (m_current == '<') {
            shift();
            if (m_current == '=') {
                shift();
                token = LSHIFTEQUAL;
                break;
            }
            token = LSHIFT;
            break;
        }
        if (m_current == '=') {
            shift();
            token = LE;
            break;
        }
        token = LT;
        break;
    case CharacterExclamationMark:
        shift();
        if (m_current == '=') {
            shift();
            if (m_current == '=') {
                shift();
                token = STRNEQ;
                break;
            }
            token = NE;
            break;
        }
        token = EXCLAMATION;
        break;
    case CharacterAdd:
        shift();
        if (m_current == '+') {
            shift();
            token = (!m_hasLineTerminatorBeforeToken) ? PLUSPLUS : AUTOPLUSPLUS;
            break;
        }
        if (m_current == '=') {
            shift();
            token = PLUSEQUAL;
            break;
        }
        token = PLUS;
        break;
    case CharacterSub:
        shift();
        if (m_current == '-') {
            shift();
            if ((m_atLineStart || m_hasLineTerminatorBeforeToken) && m_current == '>') {
                if (m_scriptMode == JSParserScriptMode::Classic) {
                    shift();
                    goto inSingleLineComment;
                }
            }
            token = (!m_hasLineTerminatorBeforeToken) ? MINUSMINUS : AUTOMINUSMINUS;
            break;
        }
        if (m_current == '=') {
            shift();
            token = MINUSEQUAL;
            break;
        }
        token = MINUS;
        break;
    case CharacterMultiply:
        shift();
        if (m_current == '=') {
            shift();
            token = MULTEQUAL;
            break;
        }
        if (m_current == '*') {
            shift();
            if (m_current == '=') {
                shift();
                token = POWEQUAL;
                break;
            }
            token = POW;
            break;
        }
        token = TIMES;
        break;
    case CharacterSlash:
        shift();
        if (m_current == '/') {
            shift();
            goto inSingleLineCommentCheckForDirectives;
        }
        if (m_current == '*') {
            shift();
            auto startLineNumber = m_lineNumber;
            auto startLineStartOffset = currentLineStartOffset();
            if (parseMultilineComment())
                goto start;
            m_lexErrorMessage = "Multiline comment was not closed properly"_s;
            token = UNTERMINATED_MULTILINE_COMMENT_ERRORTOK;
            m_error = true;
            fillTokenInfo(tokenRecord, token, startLineNumber, currentOffset(), startLineStartOffset, currentPosition());
            return token;
        }
        if (m_current == '=') {
            shift();
            token = DIVEQUAL;
            break;
        }
        token = DIVIDE;
        break;
    case CharacterAnd:
        shift();
        if (m_current == '&') {
            shift();
            if (m_current == '=') {
                shift();
                token = ANDEQUAL;
                break;
            }
            token = AND;
            break;
        }
        if (m_current == '=') {
            shift();
            token = BITANDEQUAL;
            break;
        }
        token = BITAND;
        break;
    case CharacterXor:
        shift();
        if (m_current == '=') {
            shift();
            token = BITXOREQUAL;
            break;
        }
        token = BITXOR;
        break;
    case CharacterModulo:
        shift();
        if (m_current == '=') {
            shift();
            token = MODEQUAL;
            break;
        }
        token = MOD;
        break;
    case CharacterOr:
        shift();
        if (m_current == '=') {
            shift();
            token = BITOREQUAL;
            break;
        }
        if (m_current == '|') {
            shift();
            if (m_current == '=') {
                shift();
                token = OREQUAL;
                break;
            }
            token = OR;
            break;
        }
        token = BITOR;
        break;
    case CharacterOpenParen:
        token = OPENPAREN;
        tokenData->line = lineNumber();
        tokenData->offset = currentOffset();
        tokenData->lineStartOffset = currentLineStartOffset();
        shift();
        break;
    case CharacterCloseParen:
        token = CLOSEPAREN;
        shift();
        break;
    case CharacterOpenBracket:
        token = OPENBRACKET;
        shift();
        break;
    case CharacterCloseBracket:
        token = CLOSEBRACKET;
        shift();
        break;
    case CharacterComma:
        token = COMMA;
        shift();
        break;
    case CharacterColon:
        token = COLON;
        shift();
        break;
    case CharacterQuestion:
        shift();
        if (m_current == '?') {
            shift();
            if (m_current == '=') {
                shift();
                token = COALESCEEQUAL;
                break;
            }
            token = COALESCE;
            break;
        }
        if (m_current == '.' && !isASCIIDigit(peek(1))) {
            shift();
            token = QUESTIONDOT;
            break;
        }
        token = QUESTION;
        break;
    case CharacterTilde:
        token = TILDE;
        shift();
        break;
    case CharacterSemicolon:
        shift();
        token = SEMICOLON;
        break;
    case CharacterBackQuote:
        shift();
        token = BACKQUOTE;
        break;
    case CharacterOpenBrace:
        tokenData->line = lineNumber();
        tokenData->offset = currentOffset();
        tokenData->lineStartOffset = currentLineStartOffset();
        ASSERT(tokenData->offset >= tokenData->lineStartOffset);
        shift();
        token = OPENBRACE;
        break;
    case CharacterCloseBrace:
        tokenData->line = lineNumber();
        tokenData->offset = currentOffset();
        tokenData->lineStartOffset = currentLineStartOffset();
        ASSERT(tokenData->offset >= tokenData->lineStartOffset);
        shift();
        token = CLOSEBRACE;
        break;
    case CharacterDot:
        shift();
        if (!isASCIIDigit(m_current)) {
            if ((m_current == '.') && (peek(1) == '.')) [[unlikely]] {
                shift();
                shift();
                token = DOTDOTDOT;
                break;
            }
            token = DOT;
            break;
        }
        if (!parseNumberAfterDecimalPoint()) [[unlikely]] {
            m_lexErrorMessage = "Non-number found after decimal point"_s;
            token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
            goto returnError;
        }
        token = DOUBLE;
        if (isASCIIAlphaCaselessEqual(m_current, 'e') && !parseNumberAfterExponentIndicator()) [[unlikely]] {
            m_lexErrorMessage = "Non-number found after exponent indicator"_s;
            token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
            goto returnError;
        }
        size_t parsedLength;
        tokenData->doubleValue = parseDouble(m_buffer8, parsedLength);
        if (token == INTEGER)
            token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);

        if (cannotBeIdentStart(m_current)) [[likely]] {
            m_buffer8.shrink(0);
            break;
        }

        if (isIdentStart(currentCodePoint())) [[unlikely]] {
            m_lexErrorMessage = "No identifiers allowed directly after numeric literal"_s;
            token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
            goto returnError;
        }
        m_buffer8.shrink(0);
        break;
    case CharacterZero:
        shift();
        if (isASCIIAlphaCaselessEqual(m_current, 'x')) {
            if (!isASCIIHexDigit(peek(1))) [[unlikely]] {
                m_lexErrorMessage = "No hexadecimal digits after '0x'"_s;
                token = UNTERMINATED_HEX_NUMBER_ERRORTOK;
                goto returnError;
            }

            // Shift out the 'x' prefix.
            shift();

            auto parseNumberResult = parseHex();
            if (!parseNumberResult)
                tokenData->doubleValue = 0;
            else if (std::holds_alternative<double>(*parseNumberResult))
                tokenData->doubleValue = std::get<double>(*parseNumberResult);
            else {
                token = BIGINT;
                shift();
                tokenData->bigIntString = std::get<const Identifier*>(*parseNumberResult);
                tokenData->radix = 16;
            }

            if (cannotBeIdentStart(m_current)) [[likely]] {
                if (token != BIGINT) [[likely]]
                    token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
                m_buffer8.shrink(0);
                break;
            }

            if (isIdentStart(currentCodePoint())) [[unlikely]] {
                m_lexErrorMessage = "No space between hexadecimal literal and identifier"_s;
                token = UNTERMINATED_HEX_NUMBER_ERRORTOK;
                goto returnError;
            }
            if (token != BIGINT) [[likely]]
                token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
            m_buffer8.shrink(0);
            break;
        }
        if (isASCIIAlphaCaselessEqual(m_current, 'b')) {
            if (!isASCIIBinaryDigit(peek(1))) [[unlikely]] {
                m_lexErrorMessage = "No binary digits after '0b'"_s;
                token = UNTERMINATED_BINARY_NUMBER_ERRORTOK;
                goto returnError;
            }

            // Shift out the 'b' prefix.
            shift();

            auto parseNumberResult = parseBinary();
            if (!parseNumberResult)
                tokenData->doubleValue = 0;
            else if (std::holds_alternative<double>(*parseNumberResult))
                tokenData->doubleValue = std::get<double>(*parseNumberResult);
            else {
                token = BIGINT;
                shift();
                tokenData->bigIntString = std::get<const Identifier*>(*parseNumberResult);
                tokenData->radix = 2;
            }

            if (cannotBeIdentStart(m_current)) [[likely]] {
                if (token != BIGINT) [[likely]]
                    token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
                m_buffer8.shrink(0);
                break;
            }

            if (isIdentStart(currentCodePoint())) [[unlikely]] {
                m_lexErrorMessage = "No space between binary literal and identifier"_s;
                token = UNTERMINATED_BINARY_NUMBER_ERRORTOK;
                goto returnError;
            }
            if (token != BIGINT) [[likely]]
                token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
            m_buffer8.shrink(0);
            break;
        }

        if (isASCIIAlphaCaselessEqual(m_current, 'o')) {
            if (!isASCIIOctalDigit(peek(1))) [[unlikely]] {
                m_lexErrorMessage = "No octal digits after '0o'"_s;
                token = UNTERMINATED_OCTAL_NUMBER_ERRORTOK;
                goto returnError;
            }

            // Shift out the 'o' prefix.
            shift();

            auto parseNumberResult = parseOctal();
            if (!parseNumberResult)
                tokenData->doubleValue = 0;
            else if (std::holds_alternative<double>(*parseNumberResult))
                tokenData->doubleValue = std::get<double>(*parseNumberResult);
            else {
                token = BIGINT;
                shift();
                tokenData->bigIntString = std::get<const Identifier*>(*parseNumberResult);
                tokenData->radix = 8;
            }

            if (cannotBeIdentStart(m_current)) [[likely]] {
                if (token != BIGINT) [[likely]]
                    token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
                m_buffer8.shrink(0);
                break;
            }

            if (isIdentStart(currentCodePoint())) [[unlikely]] {
                m_lexErrorMessage = "No space between octal literal and identifier"_s;
                token = UNTERMINATED_OCTAL_NUMBER_ERRORTOK;
                goto returnError;
            }
            if (token != BIGINT) [[likely]]
                token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
            m_buffer8.shrink(0);
            break;
        }

        if (m_current == '_') [[unlikely]] {
            m_lexErrorMessage = "Numeric literals may not begin with 0_"_s;
            token = UNTERMINATED_OCTAL_NUMBER_ERRORTOK;
            goto returnError;
        }

        record8('0');
        if (strictMode && isASCIIDigit(m_current)) [[unlikely]] {
            m_lexErrorMessage = "Decimal integer literals with a leading zero are forbidden in strict mode"_s;
            token = UNTERMINATED_OCTAL_NUMBER_ERRORTOK;
            goto returnError;
        }
        if (isASCIIOctalDigit(m_current)) {
            auto parseNumberResult = parseOctal();
            if (parseNumberResult && std::holds_alternative<double>(*parseNumberResult)) {
                tokenData->doubleValue = std::get<double>(*parseNumberResult);
                token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
            }
        }
        [[fallthrough]];
    case CharacterNumber:
        if (token != INTEGER && token != DOUBLE) [[likely]] {
            auto parseNumberResult = parseDecimal();
            if (parseNumberResult) {
                if (std::holds_alternative<double>(*parseNumberResult)) {
                    tokenData->doubleValue = std::get<double>(*parseNumberResult);
                    token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
                } else {
                    token = BIGINT;
                    shift();
                    tokenData->bigIntString = std::get<const Identifier*>(*parseNumberResult);
                    tokenData->radix = 10;
                }
            } else {
                token = INTEGER;
                if (m_current == '.') {
                    shift();
                    if (isASCIIDigit(m_current) && !parseNumberAfterDecimalPoint()) [[unlikely]] {
                        m_lexErrorMessage = "Non-number found after decimal point"_s;
                        token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
                        goto returnError;
                    }
                    token = DOUBLE;
                }
                if (isASCIIAlphaCaselessEqual(m_current, 'e') && !parseNumberAfterExponentIndicator()) [[unlikely]] {
                    m_lexErrorMessage = "Non-number found after exponent indicator"_s;
                    token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
                    goto returnError;
                }
                size_t parsedLength;
                tokenData->doubleValue = parseDouble(m_buffer8, parsedLength);
                if (token == INTEGER)
                    token = tokenTypeForIntegerLikeToken(tokenData->doubleValue);
            }
        }

        if (cannotBeIdentStart(m_current)) [[likely]] {
            m_buffer8.shrink(0);
            break;
        }

        if (isIdentStart(currentCodePoint())) [[unlikely]] {
            m_lexErrorMessage = "No identifiers allowed directly after numeric literal"_s;
            token = atEnd() ? UNTERMINATED_NUMERIC_LITERAL_ERRORTOK : INVALID_NUMERIC_LITERAL_ERRORTOK;
            goto returnError;
        }
        m_buffer8.shrink(0);
        break;
    case CharacterQuote: {
        auto startLineNumber = m_lineNumber;
        auto startLineStartOffset = currentLineStartOffset();
        StringParseResult result = StringCannotBeParsed;
        if (lexerFlags.contains(LexerFlags::DontBuildStrings))
            result = parseString<false>(tokenData, strictMode);
        else
            result = parseString<true>(tokenData, strictMode);

        if (result != StringParsedSuccessfully) [[unlikely]] {
            token = result == StringUnterminated ? UNTERMINATED_STRING_LITERAL_ERRORTOK : INVALID_STRING_LITERAL_ERRORTOK;
            m_error = true;
            fillTokenInfo(tokenRecord, token, startLineNumber, currentOffset(), startLineStartOffset, currentPosition());
            return token;
        }
        shift();
        token = STRING;
        m_atLineStart = false;
        fillTokenInfo(tokenRecord, token, startLineNumber, currentOffset(), startLineStartOffset, currentPosition());
        return token;
    }
    case CharacterLatin1IdentifierStart: {
        // We observe one character identifier very frequently because real world web pages are shipping minified JavaScript.
        // This path handles it in a fast path.
        auto nextCharacter = peek(1);
        if (isLatin1(nextCharacter)) [[likely]] {
            // This quickly detects the character is not a part of identifier-part *and* back-slash.
            if (typesOfLatin1Characters[static_cast<LChar>(nextCharacter)] > CharacterBackSlash) {
                const auto character = m_current;
                shift();
                if (lexerFlags.contains(LexerFlags::DontBuildKeywords))
                    tokenData->ident = nullptr;
                else
                    tokenData->ident = makeIdentifier(std::span { &character, 1 });
                token = IDENT;
                break;
            }
        }
        [[fallthrough]];
    }
    case CharacterNonLatin1IdentifierStart: {
        if constexpr (ASSERT_ENABLED) {
            char32_t codePoint;
            U16_GET(m_code, 0, 0, m_codeEnd - m_code, codePoint);
            ASSERT(isIdentStart(codePoint));
        }
        [[fallthrough]];
    }
    case CharacterBackSlash:
        parseIdent:
        if (lexerFlags.contains(LexerFlags::DontBuildKeywords))
            token = parseIdentifier<false>(tokenData, lexerFlags, strictMode);
        else
            token = parseIdentifier<true>(tokenData, lexerFlags, strictMode);
        break;
    case CharacterLineTerminator:
        ASSERT(isLineTerminator(m_current));
        shiftLineTerminator();
        m_atLineStart = true;
        m_hasLineTerminatorBeforeToken = true;
        goto start;
    case CharacterHash: {
        // Hashbang is only permitted at the start of the source text.
        auto next = peek(1);
        if (next == '!' && !currentOffset()) {
            shift();
            shift();
            goto inSingleLineComment;
        }

        bool isValidPrivateName;
        if (isLatin1(next)) [[likely]]
            isValidPrivateName = typesOfLatin1Characters[static_cast<LChar>(next)] == CharacterLatin1IdentifierStart || next == '\\';
        else {
            ASSERT(m_code + 1 < m_codeEnd);
            char32_t codePoint;
            U16_GET(m_code + 1, 0, 0, m_codeEnd - (m_code + 1), codePoint);
            isValidPrivateName = isNonLatin1IdentStart(codePoint);
        }

        if (isValidPrivateName) {
            lexerFlags.remove(LexerFlags::DontBuildKeywords);
            goto parseIdent;
        }
        goto invalidCharacter;
    }
    case CharacterPrivateIdentifierStart:
        if (m_parsingBuiltinFunction)
            goto parseIdent;
        goto invalidCharacter;
    case CharacterOtherIdentifierPart:
    case CharacterInvalid:
        goto invalidCharacter;
    default:
        RELEASE_ASSERT_NOT_REACHED();
        m_lexErrorMessage = "Internal Error"_s;
        token = ERRORTOK;
        goto returnError;
    }

    m_atLineStart = false;
    goto returnToken;

inSingleLineCommentCheckForDirectives:
    // Script comment directives like "//# sourceURL=test.js".
    if ((m_current == '#' || m_current == '@') && isWhiteSpace(peek(1))) [[unlikely]] {
        shift();
        shift();
        parseCommentDirective();
    }
    // Fall through to complete single line comment parsing.

inSingleLineComment:
    {
        auto lineNumber = m_lineNumber;
        auto endOffset = currentOffset();
        auto lineStartOffset = currentLineStartOffset();
        auto endPosition = currentPosition();

        while (!isLineTerminator(m_current)) {
            if (atEnd()) {
                token = EOFTOK;
                fillTokenInfo(tokenRecord, token, lineNumber, endOffset, lineStartOffset, endPosition);
                return token;
            }
            shift();
        }
        shiftLineTerminator();
        m_atLineStart = true;
        m_hasLineTerminatorBeforeToken = true;
        if (!lastTokenWasRestrKeyword())
            goto start;

        token = SEMICOLON;
        fillTokenInfo(tokenRecord, token, lineNumber, endOffset, lineStartOffset, endPosition);
        return token;
    }

returnToken:
    fillTokenInfo(tokenRecord, token, m_lineNumber, currentOffset(), currentLineStartOffset(), currentPosition());
    return token;

invalidCharacter:
    m_lexErrorMessage = invalidCharacterMessage();
    token = ERRORTOK;
    // Falls through to return error.

returnError:
    m_error = true;
    fillTokenInfo(tokenRecord, token, m_lineNumber, currentOffset(), currentLineStartOffset(), currentPosition());
    RELEASE_ASSERT(token & CanBeErrorTokenFlag);
    return token;
}

template <typename T>
static inline void orCharacter(char16_t&, char16_t);

template <>
inline void orCharacter<LChar>(char16_t&, char16_t) { }

template <>
inline void orCharacter<char16_t>(char16_t& orAccumulator, char16_t character)
{
    orAccumulator |= character;
}

template <typename T>
JSTokenType Lexer<T>::scanRegExp(JSToken* tokenRecord, char16_t patternPrefix)
{
    JSTokenData* tokenData = &tokenRecord->m_data;
    ASSERT(m_buffer16.isEmpty());

    bool lastWasEscape = false;
    bool inBrackets = false;
    char16_t charactersOredTogether = 0;

    if (patternPrefix) {
        ASSERT(!isLineTerminator(patternPrefix));
        ASSERT(patternPrefix != '/');
        ASSERT(patternPrefix != '[');
        record16(patternPrefix);
    }

    while (true) {
        if (isLineTerminator(m_current) || atEnd()) {
            m_buffer16.shrink(0);
            JSTokenType token = UNTERMINATED_REGEXP_LITERAL_ERRORTOK;
            fillTokenInfo(tokenRecord, token, m_lineNumber, currentOffset(), currentLineStartOffset(), currentPosition());
            m_error = true;
            m_lexErrorMessage = makeString("Unterminated regular expression literal '"_s, getToken(*tokenRecord), '\'');
            return token;
        }

        T prev = m_current;
        
        shift();

        if (prev == '/' && !lastWasEscape && !inBrackets)
            break;

        record16(prev);
        orCharacter<T>(charactersOredTogether, prev);

        if (lastWasEscape) {
            lastWasEscape = false;
            continue;
        }

        switch (prev) {
        case '[':
            inBrackets = true;
            break;
        case ']':
            inBrackets = false;
            break;
        case '\\':
            lastWasEscape = true;
            break;
        }
    }

    tokenData->pattern = makeRightSizedIdentifier(m_buffer16, charactersOredTogether);
    m_buffer16.shrink(0);

    ASSERT(m_buffer8.isEmpty());
    while (isLatin1(m_current)) [[likely]] {
        if (!isIdentPart(static_cast<LChar>(m_current)))
            break;
        record8(static_cast<LChar>(m_current));
        shift();
    }

    // Normally this would not be a lex error but dealing with surrogate pairs here is annoying and it's going to be an error anyway...
    if (!isLatin1(m_current) && !isWhiteSpace(m_current) && !isLineTerminator(m_current)) [[unlikely]] {
        m_buffer8.shrink(0);
        JSTokenType token = INVALID_IDENTIFIER_UNICODE_ERRORTOK;
        fillTokenInfo(tokenRecord, token, m_lineNumber, currentOffset(), currentLineStartOffset(), currentPosition());
        m_error = true;
        String codePoint = String::fromCodePoint(currentCodePoint());
        if (!codePoint)
            codePoint = "`invalid unicode character`"_s;
        m_lexErrorMessage = makeString("Invalid non-latin character in RexExp literal's flags '"_s, getToken(*tokenRecord), codePoint, '\'');
        return token;
    }

    tokenData->flags = makeIdentifier(m_buffer8.span());
    m_buffer8.shrink(0);

    // Since RegExp always ends with / or flags (IdentifierPart), m_atLineStart always becomes false.
    m_atLineStart = false;

    JSTokenType token = REGEXP;
    fillTokenInfo(tokenRecord, token, m_lineNumber, currentOffset(), currentLineStartOffset(), currentPosition());
    return token;
}

template <typename T>
JSTokenType Lexer<T>::scanTemplateString(JSToken* tokenRecord, RawStringsBuildMode rawStringsBuildMode)
{
    JSTokenData* tokenData = &tokenRecord->m_data;
    ASSERT(!m_error);
    ASSERT(m_buffer16.isEmpty());

    int startingLineStartOffset = currentLineStartOffset();
    int startingLineNumber = lineNumber();

    // Leading backquote ` (for template head) or closing brace } (for template trailing) are already shifted in the previous token scan.
    // So in this re-scan phase, shift() is not needed here.
    StringParseResult result = parseTemplateLiteral(tokenData, rawStringsBuildMode);
    JSTokenType token = ERRORTOK;
    if (result != StringParsedSuccessfully) [[unlikely]] {
        token = result == StringUnterminated ? UNTERMINATED_TEMPLATE_LITERAL_ERRORTOK : INVALID_TEMPLATE_LITERAL_ERRORTOK;
        m_error = true;
    } else
        token = TEMPLATE;

    // Since TemplateString always ends with ` or }, m_atLineStart always becomes false.
    m_atLineStart = false;
    fillTokenInfo(tokenRecord, token, startingLineNumber, currentOffset(), startingLineStartOffset, currentPosition());
    return token;
}

template <typename T>
void Lexer<T>::clear()
{
    m_arena = nullptr;

    Vector<LChar> newBuffer8;
    m_buffer8.swap(newBuffer8);

    Vector<char16_t> newBuffer16;
    m_buffer16.swap(newBuffer16);

    Vector<char16_t> newBufferForRawTemplateString16;
    m_bufferForRawTemplateString16.swap(newBufferForRawTemplateString16);

    m_isReparsingFunction = false;
}

// Instantiate the two flavors of Lexer we need instead of putting most of this file in Lexer.h
template class Lexer<LChar>;
template class Lexer<char16_t>;

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
