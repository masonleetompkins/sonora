#include "PitchCorrect.h"
#include <algorithm>

namespace sonora
{
double PitchScale::quantize(double midiNote) const
{
    if (scale == 0)
        return std::round(midiNote);
    constexpr int majorSteps[] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int minorSteps[] { 0, 2, 3, 5, 7, 8, 10 };
    const int* steps = scale == 1 ? majorSteps : minorSteps;
    double best = std::round(midiNote);
    double bestDistance = 100.0;
    for (int degree = 0; degree < 7; ++degree)
    {
        // Nearest octave of this scale degree's pitch class to the input.
        const int pitchClass = (key + steps[degree]) % 12;
        const double candidate = std::round((midiNote - pitchClass) / 12.0) * 12.0 + pitchClass;
        const double distance = std::abs(candidate - midiNote);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

YinDetector::YinDetector(double sampleRate, int windowSize, int hopSize,
                         float minHzIn, float maxHzIn, float thresholdIn)
    : rate(sampleRate), window(windowSize), hop(hopSize),
      minHz(minHzIn), maxHz(maxHzIn), threshold(thresholdIn)
{
}

PitchContour YinDetector::analyze(const float* mono, int numSamples,
                                  std::function<bool(double)> progress) const
{
    PitchContour contour;
    contour.hopSamples = hop;
    contour.sampleRate = rate;
    if (mono == nullptr || numSamples < window || rate <= 0.0)
        return contour;
    const int minTau = std::max(2, static_cast<int>(rate / maxHz));
    const int maxTau = std::min(window / 2, static_cast<int>(rate / minHz));
    const int count = (numSamples - window) / hop + 1;
    contour.frames.reserve(static_cast<std::size_t>(count));
    std::vector<double> difference(static_cast<std::size_t>(maxTau) + 1);
    std::vector<double> cmnd(static_cast<std::size_t>(maxTau) + 1);
    for (int frame = 0; frame < count; ++frame)
    {
        if (progress && (frame % 64 == 0) && !progress(static_cast<double>(frame) / count))
        {
            contour.frames.clear();
            return contour;
        }
        const float* block = mono + frame * hop;
        double rms = 0.0;
        for (int i = 0; i < window; ++i)
            rms += block[i] * block[i];
        rms = std::sqrt(rms / window);
        PitchFrame result;
        if (rms > 1.0e-4)
        {
            for (int tau = 0; tau <= maxTau; ++tau)
            {
                double sum = 0.0;
                for (int i = 0; i + tau < window; ++i)
                {
                    const double delta = block[i] - block[i + tau];
                    sum += delta * delta;
                }
                difference[static_cast<std::size_t>(tau)] = sum;
            }
            double running = 0.0;
            cmnd[0] = 1.0;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                running += difference[static_cast<std::size_t>(tau)];
                cmnd[static_cast<std::size_t>(tau)] = running > 0.0
                    ? difference[static_cast<std::size_t>(tau)] * tau / running : 1.0;
            }
            int tau = minTau;
            while (tau < maxTau && cmnd[static_cast<std::size_t>(tau)] >= threshold)
                ++tau;
            if (tau < maxTau)
            {
                while (tau + 1 <= maxTau && cmnd[static_cast<std::size_t>(tau + 1)] < cmnd[static_cast<std::size_t>(tau)])
                    ++tau;
                double bestTau = tau;
                if (tau > 0 && tau < maxTau)
                {
                    const double y0 = cmnd[static_cast<std::size_t>(tau - 1)];
                    const double y1 = cmnd[static_cast<std::size_t>(tau)];
                    const double y2 = cmnd[static_cast<std::size_t>(tau + 1)];
                    const double denominator = y0 + y2 - 2.0 * y1;
                    if (std::abs(denominator) > 1e-9)
                        bestTau = tau + 0.5 * (y0 - y2) / denominator;
                }
                const double hz = rate / bestTau;
                if (hz >= minHz && hz <= maxHz)
                {
                    result.f0Hz = static_cast<float>(hz);
                    result.confidence = static_cast<float>(1.0 - cmnd[static_cast<std::size_t>(tau)]);
                    result.voiced = true;
                }
            }
        }
        contour.frames.push_back(result);
    }
    return contour;
}

std::vector<float> correctionTrajectory(const PitchContour& contour, const CorrectionSettings& settings)
{
    std::vector<float> trajectory(contour.frames.size(), 0.0f);
    if (contour.frames.empty())
        return trajectory;
    // Retune smoothing: one-pole lowpass on the shift with the speed time constant.
    const double hopsPerSecond = contour.sampleRate / contour.hopSamples;
    const double alpha = 1.0 - std::exp(-1.0 / (hopsPerSecond * settings.speedMs / 1000.0));
    double smoothed = 0.0;
    bool haveSmooth = false;
    for (std::size_t i = 0; i < contour.frames.size(); ++i)
    {
        const auto& frame = contour.frames[i];
        double target = 0.0;
        if (frame.voiced && frame.f0Hz > 0.0f)
        {
            const double detected = hzToMidi(frame.f0Hz);
            const double snapped = settings.pitchScale.quantize(detected);
            target = (snapped - detected) * settings.amount;
        }
        if (!haveSmooth)
        {
            smoothed = target;
            haveSmooth = true;
        }
        else
            smoothed += alpha * (target - smoothed);
        // Unvoiced hops glide back to zero so consonants are never shifted.
        trajectory[i] = frame.voiced ? static_cast<float>(smoothed) : 0.0f;
        if (!frame.voiced)
        {
            smoothed = 0.0;
            haveSmooth = false;
        }
    }
    return trajectory;
}

void psolaShift(const float* input, int numSamples, const PitchContour& contour,
                const std::vector<float>& shiftSemitones, float* output)
{
    if (input == nullptr || output == nullptr || numSamples <= 0)
        return;
    std::fill(output, output + numSamples, 0.0f);
    if (contour.frames.empty() || shiftSemitones.size() != contour.frames.size())
    {
        std::copy(input, input + numSamples, output);
        return;
    }
    const double rate = contour.sampleRate;
    const int hop = contour.hopSamples;
    std::vector<double> overlap(static_cast<std::size_t>(numSamples), 0.0);
    double inPos = 0.0, outPos = 0.0;
    auto periodAt = [&](double sample) {
        const int frame = std::clamp(static_cast<int>(sample / hop), 0,
                                     static_cast<int>(contour.frames.size()) - 1);
        const auto& info = contour.frames[static_cast<std::size_t>(frame)];
        if (!info.voiced || info.f0Hz <= 0.0f)
            return 0.0;
        return rate / info.f0Hz;
    };
    auto shiftAt = [&](double sample) {
        const int frame = std::clamp(static_cast<int>(sample / hop), 0,
                                     static_cast<int>(shiftSemitones.size()) - 1);
        return static_cast<double>(shiftSemitones[static_cast<std::size_t>(frame)]);
    };
    while (outPos < numSamples - 1 && inPos < numSamples - 1)
    {
        const double period = periodAt(inPos);
        if (period < 20.0 || period > 2000.0)
        {
            // Unvoiced: copy through untouched to preserve consonants.
            const int chunk = std::min({ hop, numSamples - static_cast<int>(outPos),
                                         numSamples - static_cast<int>(inPos) });
            if (chunk <= 0)
                break;
            for (int i = 0; i < chunk; ++i)
                output[static_cast<int>(outPos) + i] += input[static_cast<int>(inPos) + i];
            outPos += chunk;
            inPos += chunk;
            continue;
        }
        const double ratio = std::pow(2.0, shiftAt(inPos) / 12.0);
        const double synthPeriod = std::clamp(period / std::max(0.25, std::min(4.0, ratio)), 20.0, 2000.0);
        const int center = static_cast<int>(std::llround(inPos));
        const int halfLength = static_cast<int>(std::llround(period));
        const int outCenter = static_cast<int>(std::llround(outPos));
        // Triangular window over two pitch periods, normalized by overlap below.
        for (int offset = -halfLength; offset <= halfLength; ++offset)
        {
            const int inIndex = center + offset;
            const int outIndex = outCenter + offset;
            if (inIndex < 0 || inIndex >= numSamples || outIndex < 0 || outIndex >= numSamples)
                continue;
            const double weight = 1.0 - std::abs(static_cast<double>(offset)) / (halfLength + 1.0);
            output[outIndex] += static_cast<float>(input[inIndex] * weight);
            overlap[static_cast<std::size_t>(outIndex)] += weight;
        }
        outPos += synthPeriod;
        inPos += period;
    }
    for (int i = 0; i < numSamples; ++i)
        if (overlap[static_cast<std::size_t>(i)] > 1e-6)
            output[i] = static_cast<float>(output[i] / overlap[static_cast<std::size_t>(i)]);
}

std::vector<float> correctTake(const float* mono, int numSamples, double sampleRate,
                               const CorrectionSettings& settings, PitchContour* contourOut,
                               std::function<bool(double)> progress)
{
    std::vector<float> dry(static_cast<std::size_t>(std::max(0, numSamples)), 0.0f);
    if (mono != nullptr && numSamples > 0)
        std::copy(mono, mono + numSamples, dry.begin());
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0 || !settings.valid())
        return dry;
    YinDetector detector(sampleRate);
    auto contour = detector.analyze(mono, numSamples, progress);
    if (contourOut != nullptr)
        *contourOut = contour;
    if (contour.empty())
        return dry;
    const auto trajectory = correctionTrajectory(contour, settings);
    std::vector<float> wet(dry.size(), 0.0f);
    psolaShift(dry.data(), numSamples, contour, trajectory, wet.data());
    return wet;
}
}
