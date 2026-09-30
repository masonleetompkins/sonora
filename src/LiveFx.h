#pragma once

// Live MIDI FX for MiniLab playing: arpeggiator + one-finger chords applied
// to the synth target's live notes in the audio engine. The processor is
// sample-clocked and header-only so unit tests can drive it directly.

#include "Pattern.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

namespace sonora
{

struct FxInput
{
    int offset = 0; // samples from block start
    bool on = true;
    int pitch = 60;
    int velocity = 100;
};

struct FxEvent
{
    int offset = 0;
    bool on = true;
    int pitch = 60;
    int velocity = 100;
};

// Sample-clocked live FX. Feed each block's live note on/offs with absolute
// sample positions; the returned events carry block-relative offsets, offs
// ordered before ons at the same offset.
class LiveArp
{
public:
    void setParams(LiveFx params)
    {
        if (!(params.valid()))
            params = LiveFx {};
        if (params.arp == fx.arp && params.rate == fx.rate && params.octaves == fx.octaves
            && params.latch == fx.latch && params.chordOn == fx.chordOn && params.chord == fx.chord)
            return;
        fx = params;
        poolIndex = 0;
        pingDown = false;
    }

    void setSeed(std::uint32_t seed) { rng.seed(seed); }

    std::vector<FxEvent> releaseAll(int atOffset = 0)
    {
        std::vector<FxEvent> out;
        if (sounding >= lowestPitch)
            out.push_back({ atOffset, false, sounding, 0 });
        sounding = -1;
        for (const auto& [pitch, _] : directDown)
            out.push_back({ atOffset, false, pitch, 0 });
        directDown.clear();
        return out;
    }

    void reset(std::int64_t position = 0)
    {
        physical.clear();
        held.clear();
        directDown.clear();
        sounding = -1;
        poolIndex = 0;
        pingDown = false;
        nextStep = position;
    }

    std::vector<FxEvent> process(std::int64_t blockStart, int blockLength,
                                 const std::vector<FxInput>& inputs,
                                 double sampleRate, double bpm)
    {
        std::vector<FxEvent> out;
        if (sampleRate <= 0.0 || bpm <= 0.0 || blockLength <= 0)
            return out;
        const auto blockEnd = blockStart + blockLength;
        // Ingest input first (live blocks are ~10ms; pool snapshots per step
        // would add complexity for no audible gain).
        for (const auto& in : inputs)
        {
            const int pitch = std::clamp(in.pitch, lowestPitch, highestPitch);
            if (in.on && in.velocity > 0)
                noteOn(pitch, std::clamp(in.velocity, 1, 127), out, std::clamp(in.offset, 0, blockLength - 1));
            else
                noteOff(pitch, out, std::clamp(in.offset, 0, blockLength - 1));
        }
        if (!fx.active())
            return out;
        if (fx.arp == ArpMode::Off)
            return out; // chord-direct mode answers inline above.
        const auto stepSamples = static_cast<std::int64_t>(
            sampleRate * 60.0 * arpStepBeats(fx.rate) / bpm);
        if (stepSamples <= 0)
            return out;
        if (nextStep < blockStart - stepSamples * 64)
            nextStep = blockStart; // clock jumped; resync instead of bursting.
        while (nextStep < blockEnd)
        {
            const int at = static_cast<int>(std::max<std::int64_t>(nextStep, blockStart) - blockStart);
            const auto pool = buildPool();
            if (!pool.empty())
            {
                poolIndex %= pool.size();
                const int next = sounding < lowestPitch ? start(pool) : pick(pool);
                if (next != sounding)
                {
                    if (sounding >= lowestPitch)
                        out.push_back({ at, false, sounding, 0 });
                    sounding = next;
                    out.push_back({ at, true, next, poolVelocity(pool, next) });
                }
            }
            else if (sounding >= lowestPitch)
            {
                out.push_back({ at, false, sounding, 0 });
                sounding = -1;
            }
            nextStep += stepSamples;
        }
        std::stable_sort(out.begin(), out.end(), [](const FxEvent& a, const FxEvent& b) {
            if (a.offset != b.offset)
                return a.offset < b.offset;
            return !a.on && b.on; // offs first: no voice stealing at step lines.
        });
        return out;
    }

private:
    struct PoolNote
    {
        int pitch = 60;
        int velocity = 100;
    };

    void noteOn(int pitch, int velocity, std::vector<FxEvent>& out, int at)
    {
        if (physical.empty() && fx.latch)
            held.clear(); // fresh take after a full release.
        physical[pitch] = velocity;
        held[pitch] = velocity;
        if (fx.arp == ArpMode::Off && fx.chordOn)
        {
            for (const int p : chordPitches(pitch, fx.chord))
            {
                out.push_back({ at, true, p, velocity });
                directDown[p] = velocity;
            }
        }
    }

    void noteOff(int pitch, std::vector<FxEvent>& out, int at)
    {
        physical.erase(pitch);
        if (fx.arp == ArpMode::Off && fx.chordOn)
        {
            for (const int p : chordPitches(pitch, fx.chord))
            {
                // Another held root may still cover this tone.
                bool covered = false;
                for (const auto& [other, _] : held)
                {
                    if (other == pitch)
                        continue;
                    const auto tones = chordPitches(other, fx.chord);
                    if (std::find(tones.begin(), tones.end(), p) != tones.end())
                    {
                        covered = true;
                        break;
                    }
                }
                if (!covered)
                {
                    out.push_back({ at, false, p, 0 });
                    directDown.erase(p);
                }
            }
        }
        if (!fx.latch)
            held.erase(pitch);
        if (held.empty() && sounding >= lowestPitch)
        {
            // Answer immediately instead of waiting for the next step.
            out.push_back({ at, false, sounding, 0 });
            sounding = -1;
        }
    }

    std::vector<PoolNote> buildPool() const
    {
        std::vector<PoolNote> pool;
        std::vector<int> roots;
        for (const auto& [pitch, _] : held)
            roots.push_back(pitch);
        std::sort(roots.begin(), roots.end());
        for (const int root : roots)
        {
            const int velocity = held.at(root);
            if (fx.chordOn)
            {
                for (const int p : chordPitches(root, fx.chord))
                    addPoolTone(pool, p, velocity);
            }
            else
                addPoolTone(pool, root, velocity);
        }
        if (pool.empty())
            return pool;
        const std::size_t base = pool.size();
        for (int octave = 1; octave < fx.octaves; ++octave)
            for (std::size_t i = 0; i < base; ++i)
                addPoolTone(pool, pool[i].pitch + 12 * octave, pool[i].velocity);
        return pool;
    }

    static void addPoolTone(std::vector<PoolNote>& pool, int pitch, int velocity)
    {
        if (pitch < lowestPitch || pitch > highestPitch)
            return;
        for (const auto& note : pool)
            if (note.pitch == pitch)
                return;
        pool.push_back({ pitch, velocity });
    }

    int start(const std::vector<PoolNote>& pool)
    {
        const std::size_t n = pool.size();
        if (fx.arp == ArpMode::Down)
            poolIndex = n - 1;
        else if (fx.arp == ArpMode::Random)
            poolIndex = std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
        else
            poolIndex = 0;
        pingDown = false;
        return pool[poolIndex].pitch;
    }

    int pick(const std::vector<PoolNote>& pool)
    {
        const std::size_t n = pool.size();
        if (n == 1)
            return pool.front().pitch;
        switch (fx.arp)
        {
            case ArpMode::Up:
                poolIndex = (poolIndex + 1) % n;
                return pool[poolIndex].pitch;
            case ArpMode::Down:
                poolIndex = (poolIndex + n - 1) % n;
                return pool[poolIndex].pitch;
            case ArpMode::UpDown:
                if (n == 2)
                {
                    poolIndex = (poolIndex + 1) % n;
                    return pool[poolIndex].pitch;
                }
                if (!pingDown)
                {
                    if (++poolIndex >= n - 1)
                    {
                        poolIndex = n - 1;
                        pingDown = true;
                    }
                    return pool[poolIndex].pitch;
                }
                if (poolIndex <= 1)
                {
                    poolIndex = 0;
                    pingDown = false;
                    return pool.front().pitch;
                }
                return pool[--poolIndex].pitch;
            case ArpMode::Random:
            {
                std::uniform_int_distribution<std::size_t> pick(0, n - 1);
                poolIndex = pick(rng);
                return pool[poolIndex].pitch;
            }
            case ArpMode::Off:
            case ArpMode::numModes: break;
        }
        return pool.front().pitch;
    }

    static int poolVelocity(const std::vector<PoolNote>& pool, int pitch)
    {
        for (const auto& note : pool)
            if (note.pitch == pitch)
                return note.velocity;
        return 100;
    }

    LiveFx fx;
    std::map<int, int> physical; // actually-down pitches and velocities.
    std::map<int, int> held; // arp pool sources (latch keeps these past release).
    std::map<int, int> directDown; // chord-direct sounding tones.
    int sounding = -1; // current arp pitch, or -1.
    std::size_t poolIndex = 0;
    bool pingDown = false;
    std::int64_t nextStep = 0;
    std::mt19937 rng { std::random_device {}() };
};

} // namespace sonora
