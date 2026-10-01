#include "TimeStretch.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <vector>

namespace sonora
{
juce::AudioBuffer<float> stretchAudio(const juce::AudioBuffer<float>& input, double sampleRate,
                                      double timeRatio)
{
    juce::AudioBuffer<float> output;
    const int channels = input.getNumChannels();
    const int frames = input.getNumSamples();
    if (channels <= 0 || frames <= 0 || sampleRate <= 0.0 || timeRatio < 0.1 || timeRatio > 4.0)
        return output;
    if (std::abs(timeRatio - 1.0) < 1.0e-9)
    {
        output.makeCopyOf(input);
        return output;
    }
    const int useChannels = std::min(std::max(channels, 0), 2);
    try
    {
        RubberBand::RubberBandStretcher stretcher(static_cast<size_t>(sampleRate),
                                                  static_cast<size_t>(useChannels),
                                                  RubberBand::RubberBandStretcher::OptionProcessOffline,
                                                  timeRatio, 1.0);
        stretcher.setMaxProcessSize(8192);
        std::vector<std::vector<float>> planar(static_cast<std::size_t>(useChannels));
        std::vector<float*> inPtrs(static_cast<std::size_t>(useChannels));
        for (int ch = 0; ch < useChannels; ++ch)
        {
            planar[static_cast<std::size_t>(ch)].assign(input.getReadPointer(ch),
                                                        input.getReadPointer(ch) + frames);
            inPtrs[static_cast<std::size_t>(ch)] = planar[static_cast<std::size_t>(ch)].data();
        }
        // Offline mode needs the whole input studied before processing.
        const size_t block = 4096;
        for (int done = 0; done < frames;)
        {
            const int count = std::min<int>(static_cast<int>(block), frames - done);
            std::vector<const float*> chunk(static_cast<std::size_t>(useChannels));
            for (int ch = 0; ch < useChannels; ++ch)
                chunk[static_cast<std::size_t>(ch)] = inPtrs[static_cast<std::size_t>(ch)] + done;
            done += count;
            stretcher.study(chunk.data(), static_cast<size_t>(count), done >= frames);
        }
        size_t fed = 0;
        std::vector<float> out(static_cast<std::size_t>(useChannels) * 8192, 0.0f);
        std::vector<float*> outPtrs(static_cast<std::size_t>(useChannels), nullptr);
        std::vector<std::vector<float>> received(static_cast<std::size_t>(useChannels));
        bool finalFed = false;
        while (!finalFed || stretcher.available() > 0)
        {
            if (!finalFed)
            {
                const size_t want = std::min<size_t>(stretcher.getSamplesRequired(), block);
                const size_t have = static_cast<size_t>(frames) - fed;
                const size_t count = std::min(want, have);
                // getSamplesRequired() may return 0 when it wants a flush.
                std::vector<const float*> chunk(static_cast<std::size_t>(useChannels));
                for (int ch = 0; ch < useChannels; ++ch)
                    chunk[static_cast<std::size_t>(ch)] = inPtrs[static_cast<std::size_t>(ch)] + fed;
                const bool last = count == have;
                stretcher.process(chunk.data(), count, last);
                fed += count;
                finalFed = last;
            }
            const int avail = stretcher.available();
            if (avail <= 0)
            {
                if (finalFed)
                    break;
                continue;
            }
            const int take = std::min(avail, 8192);
            for (int ch = 0; ch < useChannels; ++ch)
                outPtrs[static_cast<std::size_t>(ch)] = out.data()
                    + static_cast<std::size_t>(ch) * 8192;
            const int pulled = stretcher.retrieve(outPtrs.data(), static_cast<size_t>(take));
            for (int ch = 0; ch < useChannels; ++ch)
                received[static_cast<std::size_t>(ch)].insert(
                    received[static_cast<std::size_t>(ch)].end(), outPtrs[static_cast<std::size_t>(ch)],
                    outPtrs[static_cast<std::size_t>(ch)] + pulled);
        }
        const int total = received.empty() ? 0 : static_cast<int>(received[0].size());
        if (total <= 0)
            return output;
        output.setSize(useChannels, total, false, false, true);
        for (int ch = 0; ch < useChannels; ++ch)
            output.copyFrom(ch, 0, received[static_cast<std::size_t>(ch)].data(), total);
    }
    catch (...)
    {
        output = {};
    }
    return output;
}
} // namespace sonora
