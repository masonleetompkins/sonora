#pragma once
#include "Pattern.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
// Two-octave window onto the full MIDI range with octave-shift buttons,
// auto-follow of live-played notes, and live-note highlighting.
class PianoRoll final : public juce::Component
{
public:
    PianoRoll();
    void setPattern(const Pattern& value);
    void setPlayhead(double tick, bool playing);
    void setViewBase(int pitch);
    int getViewBase() const { return viewBase; }
    void setLiveNotes(const std::vector<int>& notes);
    std::function<void(const Pattern&)> onPreview;
    std::function<void()> onGestureBegin, onGestureEnd;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr int windowRows = 24;
    int viewTop() const { return viewBase + windowRows - 1; }
    juce::Rectangle<float> grid() const;
    juce::Rectangle<float> noteBounds(const Note&) const;
    int pitchAt(float y) const;
    int stepAt(float x) const;
    void publish();
    Pattern pattern, original;
    Note anchor;
    int selected = -1, anchorStep = 0, anchorPitch = 0;
    bool resizing = false, gesture = false;
    double playhead = -1.0;
    int viewBase = 48;
    std::vector<int> liveNotes;
    juce::TextButton octaveDown { "- oct" }, octaveUp { "+ oct" };
};
}
