#pragma once
#include "Pattern.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
class DrumSequencer final : public juce::Component
{
public:
    DrumSequencer();
    void setPattern(const DrumPattern& value) { pattern = value; repaint(); }
    void setPlayhead(double tick, bool playing);
    std::function<void(const DrumPattern&)> onPreview;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void(int)> onAudition;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> grid() const;
    int padAt(float y) const;
    int stepAt(float x) const;
    void paintStep(const juce::MouseEvent&);
    DrumPattern pattern;
    double playhead = -1.0;
    std::uint8_t drawVelocity = 100;
    bool gesture = false;
};
}
