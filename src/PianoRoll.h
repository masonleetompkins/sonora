#pragma once
#include "Pattern.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
class PianoRoll final : public juce::Component
{
public:
    PianoRoll();
    void setPattern(const Pattern& value) { pattern = value; repaint(); }
    void setPlayhead(double tick, bool playing);
    std::function<void(const Pattern&)> onPreview;
    std::function<void()> onGestureBegin, onGestureEnd;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
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
};
}
