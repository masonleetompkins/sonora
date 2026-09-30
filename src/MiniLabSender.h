#pragma once
#include "MiniLabDisplay.h"
#include <juce_events/juce_events.h>

namespace sonora
{
// Ships MiniLab SysEx from a worker thread so a stalled device can never
// freeze the UI: posting is lock-brief and non-blocking, rapid updates
// coalesce to the latest message (screen/pad state is idempotent), and the
// blocking ALSA write happens off the message thread. The sink defaults to
// the real MIDI output; tests inject a capturing sink.
class MiniLabSender final : private juce::Thread
{
public:
    using Sink = std::function<void(const minilab::Bytes&)>;
    explicit MiniLabSender(Sink sinkIn) : juce::Thread("Sonora MiniLab out"), sink(std::move(sinkIn))
    {
        startThread();
    }
    void post(minilab::Bytes bytes)
    {
        if (bytes.size() < 2 || bytes.front() != 0xF0 || bytes.back() != 0xF7)
            return;
        const juce::ScopedLock lock(mutex);
        latest = std::move(bytes);
        pending = true;
        wake.signal();
    }
    // Stop the thread, delivering anything still queued first. The owner must
    // call this before destroying whatever the sink writes through.
    void shutdown()
    {
        {
            const juce::ScopedLock lock(mutex);
            if (pending && sink)
                sink(latest);
            pending = false;
        }
        signalThreadShouldExit();
        wake.signal();
        stopThread(2000);
    }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            minilab::Bytes job;
            {
                wake.wait(500);
                if (threadShouldExit())
                    return;
                const juce::ScopedLock lock(mutex);
                if (!pending)
                    continue;
                pending = false;
                job = std::move(latest);
            }
            if (sink && !job.empty())
                sink(job);
        }
    }
    Sink sink;
    juce::CriticalSection mutex;
    juce::WaitableEvent wake;
    minilab::Bytes latest;
    bool pending = false;
};
}
