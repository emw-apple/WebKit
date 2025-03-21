/*
 * Copyright (C) 2014-2024 Apple Inc. All rights reserved.
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

#if ENABLE(DFG_JIT)

#include "DFGBasicBlock.h"

namespace JSC { namespace DFG {

class Graph;

template<typename T>
class BlockMap {
    WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED_TEMPLATE(BlockMap);
public:
    BlockMap()
    {
    }
    
    BlockMap(Graph&);
    
    BlockIndex size() const
    {
        return m_vector.size();
    }
    
    T& atIndex(BlockIndex blockIndex)
    {
        return m_vector[blockIndex];
    }
    
    const T& atIndex(BlockIndex blockIndex) const
    {
        return m_vector[blockIndex];
    }
    
    T& at(BlockIndex blockIndex)
    {
        return m_vector[blockIndex];
    }
    
    const T& at(BlockIndex blockIndex) const
    {
        return m_vector[blockIndex];
    }
    
    T& at(BasicBlock* block)
    {
        return m_vector[block->index];
    }
    
    const T& at(BasicBlock* block) const
    {
        return m_vector[block->index];
    }

    T& operator[](BlockIndex blockIndex)
    {
        return m_vector[blockIndex];
    }
    
    const T& operator[](BlockIndex blockIndex) const
    {
        return m_vector[blockIndex];
    }
    
    T& operator[](BasicBlock* block)
    {
        return m_vector[block->index];
    }
    
    const T& operator[](BasicBlock* block) const
    {
        return m_vector[block->index];
    }

private:
    Vector<T> m_vector;
};

WTF_MAKE_SEQUESTERED_ARENA_ALLOCATED_TEMPLATE_IMPL(template<typename T>, BlockMap<T>);

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
