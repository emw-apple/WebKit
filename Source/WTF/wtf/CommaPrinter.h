/*
 * Copyright (C) 2013, 2016 Apple Inc. All rights reserved.
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

#include <wtf/PrintStream.h>
#include <wtf/text/ASCIILiteral.h>

namespace WTF {

class CommaPrinter final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(CommaPrinter);
public:
    CommaPrinter(ASCIILiteral comma = ", "_s, ASCIILiteral start = ""_s)
        : m_comma(comma)
        , m_start(start)
        , m_didPrint(false)
    {
    }
    
    void dump(PrintStream& out) const
    {
        if (!m_didPrint) {
            out.print(m_start);
            m_didPrint = true;
            return;
        }
        
        out.print(m_comma);
    }
    
    bool didPrint() const { return m_didPrint; }
    
private:
    ASCIILiteral m_comma;
    ASCIILiteral m_start;
    mutable bool m_didPrint;
};

} // namespace WTF

using WTF::CommaPrinter;
