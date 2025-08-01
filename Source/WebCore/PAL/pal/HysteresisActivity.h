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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <pal/ExportMacros.h>
#include <wtf/RunLoop.h>
#include <wtf/Seconds.h>
#include <wtf/TZoneMalloc.h>

namespace PAL {

static constexpr Seconds defaultHysteresisDuration { 5_s };

enum class HysteresisState : bool { Started, Stopped };

class HysteresisActivity {
    WTF_MAKE_TZONE_ALLOCATED_EXPORT(HysteresisActivity, PAL_EXPORT);
public:
    explicit HysteresisActivity(Function<void(HysteresisState)>&& callback = [](HysteresisState) { }, Seconds hysteresisSeconds = defaultHysteresisDuration)
        : m_callback(WTFMove(callback))
        , m_hysteresisSeconds(hysteresisSeconds)
        , m_timer(RunLoop::mainSingleton(), "HysteresisActivity::Timer"_s, [this] { m_callback(HysteresisState::Stopped); })
    {
    }

    void start()
    {
        if (m_active)
            return;

        m_active = true;

        if (m_timer.isActive())
            m_timer.stop();
        else
            m_callback(HysteresisState::Started);
    }

    void stop()
    {
        if (!m_active)
            return;

        m_active = false;
        m_timer.startOneShot(m_hysteresisSeconds);
    }

    void cancel()
    {
        m_active = false;
        if (m_timer.isActive())
            m_timer.stop();
    }

    void impulse()
    {
        if (m_active)
            return;

        if (state() == HysteresisState::Stopped) {
            m_active = true;
            m_callback(HysteresisState::Started);
            m_active = false;
        }

        m_timer.startOneShot(m_hysteresisSeconds);
    }

    HysteresisState state() const
    {
        return m_active || m_timer.isActive() ? HysteresisState::Started : HysteresisState::Stopped;
    }
    
private:
    Function<void(HysteresisState)> m_callback;
    Seconds m_hysteresisSeconds;
    RunLoop::Timer m_timer;
    bool m_active { false };
};

} // namespace PAL
