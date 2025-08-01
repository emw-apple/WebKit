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

#if ENABLE(JIT)

#include "JITPlan.h"
#include <wtf/AutomaticThread.h>

namespace JSC {

class JITWorklist;
class Safepoint;

class JITWorklistThread final : public AutomaticThread {
    class WorkScope;

    friend class Safepoint;
    friend class WorkScope;
    friend class JITWorklist;

public:
    JITWorklistThread(const AbstractLocker&, JITWorklist&);

    ASCIILiteral name() const final;

    const Safepoint* safepoint() const { return m_safepoint; }

private:
    PollResult poll(const AbstractLocker&) final;
    WorkResult work() final;

    void threadDidStart() final;

    void threadIsStopping(const AbstractLocker&) final;

    Lock m_rightToRun;
    JITWorklist& m_worklist;
    RefPtr<JITPlan> m_plan { nullptr };
    unsigned m_planLoad { 0 };
    Safepoint* m_safepoint { nullptr };
};

} // namespace JSC

#endif // ENABLE(JIT)
