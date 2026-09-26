#include "Fx.h"

namespace sonora
{

void ThreeBandEq::prepare(double sampleRate)
{
    rate = sampleRate;
    reset();
    updateFilters();
}

void ThreeBandEq::setParams(const EqParams& params) { current = params; updateFilters(); }

void ThreeBandEq::updateFilters()
{
    // Note: the legacy factories take a LINEAR gain factor, not dB.
    const auto lowGain = juce::Decibels::decibelsToGain(current.low);
    const auto midGain = juce::Decibels::decibelsToGain(current.mid);
    const auto highGain = juce::Decibels::decibelsToGain(current.high);
    lowL.setCoefficients(juce::IIRCoefficients::makeLowShelf(rate, 220.0, 0.7, lowGain));
    lowR.setCoefficients(juce::IIRCoefficients::makeLowShelf(rate, 220.0, 0.7, lowGain));
    midL.setCoefficients(juce::IIRCoefficients::makePeakFilter(rate, current.midFreq, 1.0, midGain));
    midR.setCoefficients(juce::IIRCoefficients::makePeakFilter(rate, current.midFreq, 1.0, midGain));
    highL.setCoefficients(juce::IIRCoefficients::makeHighShelf(rate, 5200.0, 0.7, highGain));
    highR.setCoefficients(juce::IIRCoefficients::makeHighShelf(rate, 5200.0, 0.7, highGain));
}

void ThreeBandEq::process(float* left, float* right, int count)
{
    if (!current.enabled || count <= 0)
        return;
    lowL.processSamples(left, count);
    midL.processSamples(left, count);
    highL.processSamples(left, count);
    lowR.processSamples(right, count);
    midR.processSamples(right, count);
    highR.processSamples(right, count);
}

void ThreeBandEq::reset()
{
    lowL.reset(); lowR.reset(); midL.reset(); midR.reset(); highL.reset(); highR.reset();
}

void Compressor::prepare(double sampleRate)
{
    rate = sampleRate;
    makeup.reset(rate, 0.02);
    makeup.setCurrentAndTargetValue(1.0f);
    setParams(current);
    reset();
}

void Compressor::setParams(const CompParams& params)
{
    current = params;
    attackCoeff = std::exp(-1.0 / (rate * current.attackMs / 1000.0));
    releaseCoeff = std::exp(-1.0 / (rate * current.releaseMs / 1000.0));
    // Unity gain at threshold: makeup compensates half the static curve at
    // 0 dBFS, so compression audibly tames hot signals instead of restoring them.
    const auto staticGainDb = current.thresholdDb * (1.0f - 1.0f / current.ratio);
    makeup.setTargetValue(static_cast<float>(juce::Decibels::decibelsToGain(-staticGainDb * 0.5f)));
}

void Compressor::process(float* left, float* right, int count)
{
    if (!current.enabled || count <= 0)
        return;
    const auto threshold = static_cast<double>(juce::Decibels::decibelsToGain(current.thresholdDb));
    for (int i = 0; i < count; ++i)
    {
        const auto peak = std::max(std::abs(left[i]), std::abs(right[i]));
        const auto coeff = peak > envelope ? attackCoeff : releaseCoeff;
        envelope = coeff * envelope + (1.0 - coeff) * peak;
        double gain = 1.0;
        if (envelope > threshold && envelope > 0.0)
        {
            const auto overDb = juce::Decibels::gainToDecibels(static_cast<float>(envelope / threshold));
            gain = juce::Decibels::decibelsToGain(overDb * (1.0f / current.ratio - 1.0f));
        }
        const auto smoothed = makeup.getNextValue();
        left[i] = static_cast<float>(left[i] * gain * smoothed);
        right[i] = static_cast<float>(right[i] * gain * smoothed);
    }
}

void Compressor::reset() { envelope = 0.0; }

void TempoDelay::prepare(double sampleRate)
{
    rate = sampleRate;
    line.setSize(2, static_cast<int>(rate * 1.05) + 64, false, true, false);
    for (auto* smoother : { &timeSmooth, &feedbackSmooth, &mixSmooth })
    {
        smoother->reset(rate, 0.02);
    }
    setParams(current);
    timeSmooth.setCurrentAndTargetValue(current.timeMs);
    feedbackSmooth.setCurrentAndTargetValue(current.feedback);
    mixSmooth.setCurrentAndTargetValue(current.mix);
    reset();
}

void TempoDelay::setParams(const DelayParams& params)
{
    current = params;
    timeSmooth.setTargetValue(current.timeMs);
    feedbackSmooth.setTargetValue(current.feedback);
    mixSmooth.setTargetValue(current.mix);
}

void TempoDelay::process(float* left, float* right, int count)
{
    if (!current.enabled || count <= 0)
        return;
    const int size = line.getNumSamples();
    auto* bufL = line.getWritePointer(0);
    auto* bufR = line.getWritePointer(1);
    for (int i = 0; i < count; ++i)
    {
        const auto delaySamples = timeSmooth.getNextValue() * static_cast<float>(rate) / 1000.0f;
        const auto delayInt = juce::jlimit(1, size - 1, static_cast<int>(delaySamples));
        const auto readPos = (writePos - delayInt + size) % size;
        const auto wetL = bufL[readPos], wetR = bufR[readPos];
        const auto feedback = feedbackSmooth.getNextValue();
        const auto mix = mixSmooth.getNextValue();
        bufL[writePos] = left[i] + wetL * feedback;
        bufR[writePos] = right[i] + wetR * feedback;
        left[i] = left[i] * (1.0f - 0.5f * mix) + wetL * mix;
        right[i] = right[i] * (1.0f - 0.5f * mix) + wetR * mix;
        writePos = (writePos + 1) % size;
    }
}

void TempoDelay::reset()
{
    line.clear();
    writePos = 0;
}

void SimpleReverb::prepare(double sampleRate)
{
    reverb.setSampleRate(sampleRate);
    mixSmooth.reset(sampleRate, 0.03);
    setParams(current);
    mixSmooth.setCurrentAndTargetValue(current.mix);
    reset();
}

void SimpleReverb::setParams(const ReverbParams& params)
{
    current = params;
    juniper.roomSize = current.size;
    juniper.damping = current.damping;
    juniper.wetLevel = 1.0f;
    juniper.dryLevel = 0.0f;
    juniper.width = 1.0f;
    juniper.freezeMode = 0.0f;
    reverb.setParameters(juniper);
    mixSmooth.setTargetValue(current.mix);
}

void SimpleReverb::process(float* left, float* right, int count)
{
    if (!current.enabled || count <= 0)
        return;
    int done = 0;
    while (done < count)
    {
        const int chunk = std::min(2048, count - done);
        for (int i = 0; i < chunk; ++i)
        {
            wetL[i] = left[done + i];
            wetR[i] = right[done + i];
        }
        reverb.processStereo(wetL, wetR, chunk);
        for (int i = 0; i < chunk; ++i)
        {
            const auto mix = mixSmooth.getNextValue();
            left[done + i] = left[done + i] * (1.0f - 0.5f * mix) + wetL[i] * mix;
            right[done + i] = right[done + i] * (1.0f - 0.5f * mix) + wetR[i] * mix;
        }
        done += chunk;
    }
}

void SimpleReverb::reset() { reverb.reset(); }

void BrickLimiter::prepare(double sampleRate)
{
    rate = sampleRate;
    setParams(current);
    reset();
}

void BrickLimiter::setParams(const LimiterParams& params)
{
    current = params;
    ceiling = juce::Decibels::decibelsToGain(current.ceilingDb);
    releaseCoeff = std::exp(-1.0 / (rate * current.releaseMs / 1000.0));
}

void BrickLimiter::process(float* left, float* right, int count)
{
    if (count <= 0)
        return;
    if (!current.enabled)
        return;
    float worst = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const auto peak = std::max(std::abs(left[i]), std::abs(right[i]));
        const auto coeff = peak > envelope ? 0.0 : releaseCoeff;
        envelope = coeff * envelope + (1.0 - coeff) * peak;
        double gain = 1.0;
        if (envelope > ceiling && envelope > 0.0)
            gain = ceiling / envelope;
        left[i] = static_cast<float>(left[i] * gain);
        right[i] = static_cast<float>(right[i] * gain);
        worst = std::min(worst, static_cast<float>(20.0 * std::log10(gain + 1e-9)));
    }
    reductionDb.store(worst);
}

void BrickLimiter::reset() { envelope = 0.0; reductionDb.store(0.0f); }

void Overdrive::prepare(double sampleRate)
{
    rate = sampleRate;
    amountSmooth.reset(sampleRate, 0.03);
    amountSmooth.setCurrentAndTargetValue(current.enabled ? current.amount : 0.0f);
    setParams(current);
    reset();
}

void Overdrive::setParams(const DriveParams& params)
{
    current = params;
    amountSmooth.setTargetValue(params.enabled ? params.amount : 0.0f);
    toneCoeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * params.tone / static_cast<float>(rate));
}

void Overdrive::process(float* left, float* right, int count)
{
    // Fully bypassed (and settled) at zero: untouched samples.
    if (!amountSmooth.isSmoothing() && amountSmooth.getTargetValue() <= 0.0f)
        return;
    for (int i = 0; i < count; ++i)
    {
        const float amount = amountSmooth.getNextValue();
        // 0..+50 dB pre-gain on a perceptual (exponential) curve. Output is
        // normalized around a typical instrument level, so low drive stays
        // near unity and high drive compresses into a square at similar loudness.
        const float gain = std::pow(10.0f, amount * 2.5f);
        const float norm = 0.12f / std::tanh(gain * 0.12f);
        // Crossfade from dry so turning drive up from zero never clicks.
        const float wet = std::min(1.0f, amount * 20.0f);
        const float shapedL = std::tanh(left[i] * gain) * norm;
        const float shapedR = std::tanh(right[i] * gain) * norm;
        // Two cascaded one-poles: 12 dB/oct tone roll-off, like an amp's tone stack.
        toneL += toneCoeff * (shapedL - toneL);
        toneR += toneCoeff * (shapedR - toneR);
        tone2L += toneCoeff * (toneL - tone2L);
        tone2R += toneCoeff * (toneR - tone2R);
        left[i] += wet * (tone2L - left[i]);
        right[i] += wet * (tone2R - right[i]);
    }
}

void Overdrive::reset() { toneL = toneR = tone2L = tone2R = 0.0f; }

void StereoChorus::prepare(double sampleRate)
{
    rate = sampleRate;
    const auto size = static_cast<std::size_t>(sampleRate * 0.05) + 4;
    lineL.assign(size, 0.0f);
    lineR.assign(size, 0.0f);
    mixSmooth.reset(sampleRate, 0.03);
    mixSmooth.setCurrentAndTargetValue(current.enabled ? current.mix : 0.0f);
    reset();
}

void StereoChorus::setParams(const ChorusParams& params)
{
    current = params;
    mixSmooth.setTargetValue(params.enabled ? params.mix : 0.0f);
}

void StereoChorus::process(float* left, float* right, int count)
{
    if (lineL.empty() || (!mixSmooth.isSmoothing() && mixSmooth.getTargetValue() <= 0.0f))
        return;
    const auto size = static_cast<int>(lineL.size());
    const double increment = juce::MathConstants<double>::twoPi * current.rate / rate;
    const double depthSeconds = 0.006 * current.depth;
    auto read = [&](const std::vector<float>& line, double offset) {
        const double delay = rate * (0.012 + depthSeconds * std::sin(lfo + offset));
        double position = write - delay;
        while (position < 0.0)
            position += size;
        const int a = static_cast<int>(position) % size;
        const int b = (a + 1) % size;
        const float frac = static_cast<float>(position - std::floor(position));
        return line[static_cast<std::size_t>(a)] * (1.0f - frac) + line[static_cast<std::size_t>(b)] * frac;
    };
    for (int i = 0; i < count; ++i)
    {
        lineL[static_cast<std::size_t>(write)] = left[i];
        lineR[static_cast<std::size_t>(write)] = right[i];
        const float mix = mixSmooth.getNextValue();
        const float wetL = read(lineL, 0.0), wetR = read(lineR, juce::MathConstants<double>::halfPi);
        left[i] = left[i] * (1.0f - 0.5f * mix) + wetL * 0.7f * mix;
        right[i] = right[i] * (1.0f - 0.5f * mix) + wetR * 0.7f * mix;
        write = (write + 1) % size;
        lfo += increment;
        if (lfo >= juce::MathConstants<double>::twoPi)
            lfo -= juce::MathConstants<double>::twoPi;
    }
}

void StereoChorus::reset()
{
    std::fill(lineL.begin(), lineL.end(), 0.0f);
    std::fill(lineR.begin(), lineR.end(), 0.0f);
    write = 0;
    lfo = 0.0;
}

void TrackChain::prepare(double sampleRate)
{
    drive.prepare(sampleRate);
    chorus.prepare(sampleRate);
    eq.prepare(sampleRate);
    comp.prepare(sampleRate);
    delay.prepare(sampleRate);
    reverb.prepare(sampleRate);
    setParams(current);
}

void TrackChain::setParams(const TrackFx& params)
{
    current = params;
    drive.setParams(params.drive);
    chorus.setParams(params.chorus);
    eq.setParams(params.eq);
    comp.setParams(params.comp);
    delay.setParams(params.delay);
    reverb.setParams(params.reverb);
}

void TrackChain::process(juce::AudioBuffer<float>& buffer, int start, int count)
{
    if (count <= 0 || buffer.getNumChannels() < 2)
        return;
    drive.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
    eq.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
    comp.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
    chorus.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
    delay.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
    reverb.process(buffer.getWritePointer(0, start), buffer.getWritePointer(1, start), count);
}

void TrackChain::reset()
{
    drive.reset();
    chorus.reset();
    eq.reset();
    comp.reset();
    delay.reset();
    reverb.reset();
}
}
