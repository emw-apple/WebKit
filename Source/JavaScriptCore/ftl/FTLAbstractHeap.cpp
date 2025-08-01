/*
 * Copyright (C) 2013-2023 Apple Inc. All rights reserved.
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
#include "FTLAbstractHeap.h"

#if ENABLE(FTL_JIT)

#include "FTLAbbreviatedTypes.h"
#include "FTLOutput.h"
#include "FTLTypedPointer.h"
#include "JSCJSValueInlines.h"
#include "Options.h"
#include "StructureRareDataInlines.h"
#include <wtf/TZoneMallocInlines.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC { namespace FTL {

WTF_MAKE_TZONE_ALLOCATED_IMPL(AbstractHeap);

AbstractHeap::AbstractHeap(AbstractHeap* parent, const char* heapName, ptrdiff_t offset)
    : m_offset(offset)
    , m_heapName(heapName)
{
    changeParent(parent);
}

void AbstractHeap::changeParent(AbstractHeap* parent)
{
    if (m_parent) {
        bool result = m_parent->m_children.removeFirst(this);
        RELEASE_ASSERT(result);
    }

    m_parent = parent;

    if (parent) {
        ASSERT(!m_parent->m_children.contains(this));
        m_parent->m_children.append(this);
    }
}

void AbstractHeap::compute(unsigned begin)
{
    // This recursively computes the ranges of the tree. This solves the following constraints
    // in linear time:
    //
    // - A node's end is greater than its begin.
    // - A node's begin is greater than or equal to its parent's begin.
    // - A node's end is less than or equal to its parent's end.
    // - The ranges are as small as possible.
    //
    // It's OK to recurse because we keep the depth of our abstract heap hierarchy fairly sane.
    // I think that it gets 4 deep at most.

    if (m_children.isEmpty()) {
        // Must special-case leaves so that they use just one slot on the number line.
        m_range = B3::HeapRange(begin);
        return;
    }

    unsigned current = begin;
    for (AbstractHeap* child : m_children) {
        child->compute(current);
        current = child->range().end();
    }

    m_range = B3::HeapRange(begin, current);
}

void AbstractHeap::shallowDump(PrintStream& out) const
{
    out.print(heapName(), "(", m_offset, ")");
    if (m_range)
        out.print("<", m_range, ">");
}

void AbstractHeap::dump(PrintStream& out) const
{
    shallowDump(out);
    if (m_parent)
        out.print("->", *m_parent);
}

void AbstractHeap::deepDump(PrintStream& out, unsigned indent) const
{
    auto printIndent = [&] () {
        for (unsigned i = indent; i--;)
            out.print("    ");
    };

    printIndent();
    shallowDump(out);

    if (m_children.isEmpty()) {
        out.print("\n");
        return;
    }

    out.print(":\n");
    for (AbstractHeap* child : m_children)
        child->deepDump(out, indent + 1);
}

void AbstractHeap::badRangeError() const
{
    dataLogLn("Heap does not have range: ", *this);
    RELEASE_ASSERT_NOT_REACHED();
}

IndexedAbstractHeap::IndexedAbstractHeap(AbstractHeap* parent, const char* heapName, ptrdiff_t offset, size_t elementSize)
    : m_heapForAnyIndex(parent, heapName)
    , m_heapNameLength(strlen(heapName))
    , m_offset(offset)
    , m_elementSize(elementSize)
{
}

IndexedAbstractHeap::~IndexedAbstractHeap() = default;

TypedPointer IndexedAbstractHeap::baseIndex(Output& out, LValue base, LValue index, JSValue indexAsConstant, ptrdiff_t offset, LValue mask)
{
    if (indexAsConstant.isInt32())
        return out.address(base, at(indexAsConstant.asInt32()), offset);

    if (mask)
        index = out.bitAnd(mask, index);
    LValue result = out.add(base, out.mul(index, out.constIntPtr(m_elementSize)));
    
    return TypedPointer(atAnyIndex(), out.addPtr(result, m_offset + offset));
}

const AbstractHeap& IndexedAbstractHeap::atSlow(ptrdiff_t index)
{
    ASSERT(static_cast<size_t>(index) >= m_smallIndices.size());
    
    auto& field = m_largeIndices.add(index, nullptr).iterator->value;
    if (!field) {
        field = makeUnique<AbstractHeap>();
        initialize(*field, index);
    }

    return *field;
}

void IndexedAbstractHeap::initialize(AbstractHeap& field, ptrdiff_t signedIndex)
{
    // Build up a name of the form:
    //
    //    heapName_hexIndex
    //
    // or:
    //
    //    heapName_neg_hexIndex
    //
    // For example if you access an indexed heap called FooBar at index 5, you'll
    // get:
    //
    //    FooBar_5
    //
    // Or if you access an indexed heap called Blah at index -10, you'll get:
    //
    //    Blah_neg_A
    //
    // This naming convention comes from our previous use of LLVM. It's not clear that we need
    // it anymore, though it is sort of nifty. Basically, B3 doesn't need string names for
    // abstract heaps, but the fact that we have a reasonably efficient way to always name the
    // heaps will probably come in handy for debugging.
    
    static const char* negSplit = "_neg_";
    static const char* posSplit = "_";
    
    bool negative;
    size_t index;
    if (signedIndex < 0) {
        negative = true;
        index = -signedIndex;
    } else {
        negative = false;
        index = signedIndex;
    }
    
    for (unsigned power = 4; power <= sizeof(void*) * 8; power += 4) {
        if (isGreaterThanNonZeroPowerOfTwo(index, power))
            continue;
        
        unsigned numHexlets = power >> 2;
        
        size_t stringLength = m_heapNameLength + (negative ? strlen(negSplit) : strlen(posSplit)) + numHexlets;
        std::span<char> characters;
        m_largeIndexNames.append(CString::newUninitialized(stringLength, characters));
        
        memcpy(characters.data(), m_heapForAnyIndex.heapName(), m_heapNameLength);
        if (negative)
            memcpy(characters.data() + m_heapNameLength, negSplit, strlen(negSplit));
        else
            memcpy(characters.data() + m_heapNameLength, posSplit, strlen(posSplit));
        
        size_t accumulator = index;
        for (unsigned i = 0; i < numHexlets; ++i) {
            characters[stringLength - i - 1] = lowerNibbleToASCIIHexDigit(accumulator);
            accumulator >>= 4;
        }
        
        field.initialize(&m_heapForAnyIndex, characters.data(), m_offset + signedIndex * m_elementSize);
        return;
    }
    
    RELEASE_ASSERT_NOT_REACHED();
}

void IndexedAbstractHeap::dump(PrintStream& out)
{
    out.print("Indexed:", atAnyIndex());
}

NumberedAbstractHeap::NumberedAbstractHeap(AbstractHeap* heap, const char* heapName)
    : m_indexedHeap(heap, heapName, 0, 1)
{
}

NumberedAbstractHeap::~NumberedAbstractHeap() = default;

void NumberedAbstractHeap::dump(PrintStream& out)
{
    out.print("Numbered: ", atAnyNumber());
}

AbsoluteAbstractHeap::AbsoluteAbstractHeap(AbstractHeap* heap, const char* heapName)
    : m_indexedHeap(heap, heapName, 0, 1)
{
}

AbsoluteAbstractHeap::~AbsoluteAbstractHeap() = default;

void AbsoluteAbstractHeap::dump(PrintStream& out)
{
    out.print("Absolute:", atAnyAddress());
}

} } // namespace JSC::FTL

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(FTL_JIT)
