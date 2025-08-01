/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#include "config.h"

#if ENABLE(WEB_AUDIO)

#include "AudioParam.h"

#include "AudioNode.h"
#include "AudioNodeOutput.h"
#include "AudioUtilities.h"
#include "ExceptionOr.h"
#include "FloatConversion.h"
#include "Logging.h"
#include "VectorMath.h"
#include <algorithm>
#include <wtf/MathExtras.h>
#include <wtf/StdLibExtras.h>

namespace WebCore {

static void replaceNaNValues(std::span<float> values, float defaultValue)
{
    for (auto& value : values) {
        if (std::isnan(value))
            value = defaultValue;
    }
}

AudioParam::AudioParam(BaseAudioContext& context, const String& name, float defaultValue, float minValue, float maxValue, AutomationRate automationRate, AutomationRateMode automationRateMode)
    : AudioSummingJunction(context)
    , m_name(name)
    , m_value(defaultValue)
    , m_defaultValue(defaultValue)
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_automationRate(automationRate)
    , m_automationRateMode(automationRateMode)
    , m_smoothedValue(defaultValue)
    , m_summingBus(AudioBus::create(1, AudioUtilities::renderQuantumSize, false))
#if !RELEASE_LOG_DISABLED
    , m_logger(context.logger())
    , m_logIdentifier(context.nextAudioParameterLogIdentifier())
#endif
{
    ALWAYS_LOG(LOGIDENTIFIER, "name = ", m_name, ", value = ", m_value, ", default = ", m_defaultValue, ", min = ", m_minValue, ", max = ", m_maxValue);
}

float AudioParam::value()
{
    // Update value for timeline.
    if (context() && context()->isAudioThread()) {
        auto timelineValue = m_timeline.valueForContextTime(*context(), m_value, minValue(), maxValue());
        if (timelineValue)
            m_value = *timelineValue;
    }

    return m_value;
}

void AudioParam::setValue(float value)
{
    DEBUG_LOG(LOGIDENTIFIER, value);

    m_value = std::clamp(value, minValue(), maxValue());
}

float AudioParam::valueForBindings() const
{
    ASSERT(isMainThread());
    return m_value;
}

ExceptionOr<void> AudioParam::setValueForBindings(float value)
{
    ASSERT(isMainThread());

    setValue(value);

    if (!context())
        return { };

    auto result = setValueAtTime(m_value, context()->currentTime());
    if (result.hasException())
        return result.releaseException();
    return { };
}

ExceptionOr<void> AudioParam::setAutomationRate(AutomationRate automationRate)
{
    if (m_automationRateMode == AutomationRateMode::Fixed)
        return Exception { ExceptionCode::InvalidStateError, "automationRate cannot be changed for this node"_s };

    m_automationRate = automationRate;
    return { };
}

float AudioParam::smoothedValue()
{
    return m_smoothedValue;
}

bool AudioParam::smooth()
{
    if (!context())
        return true;

    // If values have been explicitly scheduled on the timeline, then use the exact value.
    // Smoothing effectively is performed by the timeline.
    auto timelineValue = m_timeline.valueForContextTime(*context(), m_value, minValue(), maxValue());
    if (timelineValue)
        m_value = *timelineValue;

    if (m_smoothedValue == m_value) {
        // Smoothed value has already approached and snapped to value.
        return true;
    }
    
    if (timelineValue)
        m_smoothedValue = m_value;
    else {
        // Dezipper - exponential approach.
        m_smoothedValue += (m_value - m_smoothedValue) * SmoothingConstant;

        // If we get close enough then snap to actual value.
        if (std::abs(m_smoothedValue - m_value) < SnapThreshold) // FIXME: the threshold needs to be adjustable depending on range - but this is OK general purpose value.
            m_smoothedValue = m_value;
    }

    return false;
}

ExceptionOr<AudioParam&> AudioParam::setValueAtTime(float value, double startTime)
{
    if (!context())
        return *this;

    if (startTime < 0)
        return Exception { ExceptionCode::RangeError, "startTime must be a positive value"_s };

    startTime = std::max(startTime, context()->currentTime());
    auto result = m_timeline.setValueAtTime(value, Seconds { startTime });
    if (result.hasException())
        return result.releaseException();
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::linearRampToValueAtTime(float value, double endTime)
{
    if (!context())
        return *this;

    if (endTime < 0)
        return Exception { ExceptionCode::RangeError, "endTime must be a positive value"_s };

    endTime = std::max(endTime, context()->currentTime());
    auto result = m_timeline.linearRampToValueAtTime(value, Seconds { endTime }, m_value, Seconds { context()->currentTime() });
    if (result.hasException())
        return result.releaseException();
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::exponentialRampToValueAtTime(float value, double endTime)
{
    if (!context())
        return *this;

    if (!value)
        return Exception { ExceptionCode::RangeError, "value cannot be 0"_s };
    if (endTime < 0)
        return Exception { ExceptionCode::RangeError, "endTime must be a positive value"_s };

    endTime = std::max(endTime, context()->currentTime());
    auto result = m_timeline.exponentialRampToValueAtTime(value, Seconds { endTime }, m_value, Seconds { context()->currentTime() });
    if (result.hasException())
        return result.releaseException();
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::setTargetAtTime(float target, double startTime, float timeConstant)
{
    if (!context())
        return *this;

    if (startTime < 0)
        return Exception { ExceptionCode::RangeError, "startTime must be a positive value"_s };
    if (timeConstant < 0)
        return Exception { ExceptionCode::RangeError, "timeConstant must be a positive value"_s };

    startTime = std::max(startTime, context()->currentTime());
    auto result = m_timeline.setTargetAtTime(target, Seconds { startTime }, timeConstant);
    if (result.hasException())
        return result.releaseException();
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::setValueCurveAtTime(Vector<float>&& curve, double startTime, double duration)
{
    if (!context())
        return *this;

    if (curve.size() < 2)
        return Exception { ExceptionCode::InvalidStateError, "Array must have a length of at least 2"_s };
    if (startTime < 0)
        return Exception { ExceptionCode::RangeError, "startTime must be a positive value"_s };
    if (duration <= 0)
        return Exception { ExceptionCode::RangeError, "duration must be a strictly positive value"_s };

    startTime = std::max(startTime, context()->currentTime());
    auto result = m_timeline.setValueCurveAtTime(WTFMove(curve), Seconds { startTime }, Seconds { duration });
    if (result.hasException())
        return result.releaseException();
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::cancelScheduledValues(double cancelTime)
{
    if (cancelTime < 0)
        return Exception { ExceptionCode::RangeError, "cancelTime must be a positive value"_s };

    m_timeline.cancelScheduledValues(Seconds { cancelTime });
    return *this;
}

ExceptionOr<AudioParam&> AudioParam::cancelAndHoldAtTime(double cancelTime)
{
    if (cancelTime < 0)
        return Exception { ExceptionCode::RangeError, "cancelTime must be a positive value"_s };

    auto result = m_timeline.cancelAndHoldAtTime(Seconds { cancelTime });
    if (result.hasException())
        return result.releaseException();

    return *this;
}

bool AudioParam::hasSampleAccurateValues() const
{
    if (numberOfRenderingConnections())
        return true;

    if (!context())
        return false;

    return m_timeline.hasValues(context()->currentSampleFrame(), context()->sampleRate());
}

float AudioParam::finalValue()
{
    float value;
    calculateFinalValues(singleElementSpan(value), false);
    return value;
}

void AudioParam::calculateSampleAccurateValues(std::span<float> values)
{
    bool isSafe = context() && context()->isAudioThread() && !values.empty();
    ASSERT(isSafe);
    if (!isSafe)
        return;

    calculateFinalValues(values, automationRate() == AutomationRate::ARate);
}

void AudioParam::calculateFinalValues(std::span<float> values, bool sampleAccurate)
{
    bool isGood = context() && context()->isAudioThread() && !values.empty();
    ASSERT(isGood);
    if (!isGood)
        return;

    // The calculated result will be the "intrinsic" value summed with all audio-rate connections.

    if (sampleAccurate) {
        // Calculate sample-accurate (a-rate) intrinsic values.
        calculateTimelineValues(values);
    } else {
        // Calculate control-rate (k-rate) intrinsic value.
        auto timelineValue = m_timeline.valueForContextTime(*context(), m_value, minValue(), maxValue());

        if (timelineValue)
            m_value = *timelineValue;
        std::ranges::fill(values, m_value);
    }

    if (!numberOfRenderingConnections())
        return;

    // Now sum all of the audio-rate connections together (unity-gain summing junction).
    // Note that connections would normally be mono, but we mix down to mono if necessary.
    // If we're not sample accurate, we only need one value, so make the summing
    // bus have length 1. When the connections are added in, only the first
    // value will be added. Which is exactly what we want.
    ASSERT(values.size() <= AudioUtilities::renderQuantumSize);
    m_summingBus->setChannelMemory(0, values.first(sampleAccurate ? values.size() : 1));

    for (auto& output : m_renderingOutputs) {
        ASSERT(output);

        // Render audio from this output.
        AudioBus& connectionBus = output->pull(0, AudioUtilities::renderQuantumSize);

        // Sum, with unity-gain.
        m_summingBus->sumFrom(connectionBus);
    }

    // If we're not sample accurate, duplicate the first element of |values| to all of the elements.
    if (!sampleAccurate)
        std::ranges::fill(values.subspan(1), values[0]);

    // As per https://webaudio.github.io/web-audio-api/#computation-of-value, we should replace NaN values
    // with the default value.
    replaceNaNValues(values, m_defaultValue);

    // Clamp values based on range allowed by AudioParam's min and max values.
    VectorMath::clamp(values, minValue(), maxValue(), values);
}

void AudioParam::calculateTimelineValues(std::span<float> values)
{
    if (!context())
        return;

    // Calculate values for this render quantum.
    // Normally numberOfValues will equal AudioUtilities::renderQuantumSize (the render quantum size).
    double sampleRate = context()->sampleRate();
    size_t startFrame = context()->currentSampleFrame();
    size_t endFrame = startFrame + values.size();

    // Note we're running control rate at the sample-rate.
    // Pass in the current value as default value.
    m_value = m_timeline.valuesForFrameRange(startFrame, endFrame, m_value, minValue(), maxValue(), values, sampleRate, sampleRate);
}

void AudioParam::connect(AudioNodeOutput* output)
{
    ASSERT(context());
    ASSERT(context()->isGraphOwner());

    ASSERT(output);
    if (!output)
        return;

    if (!addOutput(*output))
        return;

    INFO_LOG(LOGIDENTIFIER, output->node()->nodeType());
    output->addParam(this);
}

void AudioParam::disconnect(AudioNodeOutput* output)
{
    ASSERT(context());
    ASSERT(context()->isGraphOwner());

    ASSERT(output);
    if (!output)
        return;

    INFO_LOG(LOGIDENTIFIER, output->node()->nodeType());

    if (removeOutput((*output)))
        output->removeParam(this);
}

#if !RELEASE_LOG_DISABLED
WTFLogChannel& AudioParam::logChannel() const
{
    return LogMedia;
}
#endif
    

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
