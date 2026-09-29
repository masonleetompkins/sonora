#include "DrumSequencer.h"
#include "NeonTheme.h"

namespace sonora
{
DrumSequencer::DrumSequencer()
{
    setWantsKeyboardFocus(true);
}

juce::Rectangle<float> DrumSequencer::grid() const
{
    return { 116.0f, 30.0f, static_cast<float>(getWidth() - 118), static_cast<float>(getHeight() - 32) };
}

int DrumSequencer::padAt(float y) const
{
    return juce::jlimit(0, drumPads - 1, static_cast<int>((y - grid().getY()) / (grid().getHeight() / drumPads)));
}

int DrumSequencer::stepAt(float x) const
{
    return juce::jlimit(0, gridSteps - 1, static_cast<int>((x - grid().getX()) / (grid().getWidth() / gridSteps)));
}

void DrumSequencer::paint(juce::Graphics& g)
{
    if (ui::uiDark)
    {
        g.fillAll(juce::Colour(0xff0c111b));
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffffff), 0, 0,
                                               ui::background, 0, static_cast<float>(getHeight()), false));
        g.fillAll();
    }
    ui::caption(g, "SOUND / PAD", { 8, 3, 104, 23 }, ui::muted, 9);
    const auto area = grid();
    const auto row = area.getHeight() / drumPads;
    const auto column = area.getWidth() / gridSteps;
    const auto currentStep = playhead < 0 ? -1 : static_cast<int>(playhead / stepTicks);
    for (int pad = 0; pad < drumPads; ++pad)
    {
        const auto y = area.getY() + static_cast<float>(pad) * row;
        const auto accent = pad < 2 ? ui::violet : ui::blue;
        const bool firing = currentStep >= 0 && currentStep < gridSteps
            && pattern.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(currentStep)] > 0;
        g.setColour(firing ? accent.withMultipliedBrightness(ui::uiDark ? 0.25f : 0.85f) : ui::panel);
        g.fillRoundedRectangle(4.0f, y + 3.0f, 104.0f, row - 6.0f, 5.0f);
        g.setColour(firing ? ui::background : ui::muted);
        g.fillEllipse(11, y + row / 2 - 2, 4, 4);
        g.setFont(ui::font(11.0f, true));
        // Name keeps its own colour: when the pad fires the label row above
        // shares the fill colour, which would make the name vanish into it.
        g.setColour(firing ? (ui::uiDark ? ui::background : ui::text) : ui::muted);
        g.drawText(drumNames[static_cast<std::size_t>(pad)], 23, static_cast<int>(y), 82,
                   static_cast<int>(row), juce::Justification::centredLeft);
        for (int step = 0; step < gridSteps; ++step)
        {
            const auto x = area.getX() + static_cast<float>(step) * column;
            const auto velocity = pattern.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)];
            const auto bounds = juce::Rectangle<float>(x + 1.5f, y + 5.0f, column - 3.0f, row - 10.0f);
            auto colour = velocity > 0 ? accent.withMultipliedBrightness(ui::uiDark ? 0.48f + 0.52f * static_cast<float>(velocity) / 127.0f : 0.95f)
                                        : ui::uiDark ? juce::Colour((step / 4) % 2 == 0 ? 0xff1d293d : 0xff141f30)
                                                     : ui::background.darker((step / 4) % 2 == 0 ? 0.08f : 0.02f);
            if (step == currentStep && velocity > 0)
                colour = colour.brighter(0.4f);
            if (velocity > 0)
            {
                g.setColour(colour.withAlpha(0.09f));
                g.fillRoundedRectangle(bounds.expanded(1.5f), 4);
            }
            g.setGradientFill(juce::ColourGradient(colour, bounds.getTopLeft(), colour.darker(0.33f), bounds.getBottomLeft(), false));
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(velocity > 0 ? accent.withAlpha(0.5f) : ui::border.withAlpha(0.5f));
            g.drawRoundedRectangle(bounds, 3.0f, 0.7f);
            if (velocity > 0)
            {
                g.setColour(ui::background.withAlpha(0.48f));
                const auto height = (bounds.getHeight() - 6.0f) * static_cast<float>(velocity) / 127.0f;
                g.fillRect(bounds.getX() + 3.0f, bounds.getBottom() - height - 3.0f, 2.0f, height);
            }
            if (step == currentStep)
            {
                g.setColour(ui::violet.brighter(0.5f));
                g.drawRoundedRectangle(bounds, 2.5f, 1.5f);
            }
        }
    }
    for (int step = 0; step < gridSteps; step += 4)
    {
        const auto x = area.getX() + static_cast<float>(step) * column;
        g.setColour(step % 16 == 0 ? ui::violet : ui::muted);
        g.setFont(ui::font(10.0f, step % 16 == 0));
        g.drawText(juce::String(step / 16 + 1) + "." + juce::String((step / 4) % 4 + 1),
                   static_cast<int>(x) + 2, 2, 42, 22, juce::Justification::centredLeft);
        if (step % 16 == 0)
        {
            g.setColour(ui::violet.withAlpha(0.3f));
            g.drawVerticalLine(static_cast<int>(x) - 1, area.getY(), area.getBottom());
        }
    }
}

void DrumSequencer::setPlayhead(double tick, bool playing)
{
    const double next = playing ? tick : -1.0;
    if (next < 0.0 && playhead < 0.0)
        return;
    // Only the active step column (and pad firing dots) changes per frame.
    const auto area = grid();
    const auto column = area.getWidth() / gridSteps;
    const auto strip = [&](double t) {
        const auto step = juce::jlimit(0, gridSteps - 1, static_cast<int>(t / stepTicks));
        const auto x = area.getX() + static_cast<float>(step) * column;
        return juce::Rectangle<int>(static_cast<int>(x) - 3, static_cast<int>(area.getY()) - 26,
                                    static_cast<int>(column) + 6, static_cast<int>(area.getHeight()) + 26);
    };
    const auto labels = juce::Rectangle<int>(0, static_cast<int>(area.getY()), 112,
                                             static_cast<int>(area.getHeight()));
    if (playhead >= 0.0)
    {
        repaint(strip(playhead));
        repaint(labels);
    }
    playhead = next;
    if (playhead >= 0.0)
    {
        repaint(strip(playhead));
        repaint(labels);
    }
    else
        repaint();
}

void DrumSequencer::paintStep(const juce::MouseEvent& event)
{
    if (!grid().contains(event.position))
        return;
    auto& value = pattern.steps[static_cast<std::size_t>(padAt(event.position.y))]
                               [static_cast<std::size_t>(stepAt(event.position.x))];
    if (value == drawVelocity)
        return;
    value = drawVelocity;
    if (onPreview)
        onPreview(pattern);
    repaint();
}

void DrumSequencer::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.position.y < grid().getY())
        return;
    const auto pad = padAt(event.position.y);
    if (event.position.x < grid().getX())
    {
        if (onAudition)
            onAudition(pad);
        return;
    }
    if (!grid().contains(event.position))
        return;
    const auto current = pattern.steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(stepAt(event.position.x))];
    drawVelocity = event.mods.isRightButtonDown() || current > 0 ? 0 : 100;
    if (onGestureBegin)
        onGestureBegin();
    gesture = true;
    paintStep(event);
}

void DrumSequencer::mouseDrag(const juce::MouseEvent& event)
{
    if (gesture)
        paintStep(event);
}

void DrumSequencer::mouseUp(const juce::MouseEvent&)
{
    if (gesture && onGestureEnd)
        onGestureEnd();
    gesture = false;
}

void DrumSequencer::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!grid().contains(event.position) || std::abs(wheel.deltaY) < 0.00001f)
        return;
    auto& velocity = pattern.steps[static_cast<std::size_t>(padAt(event.position.y))]
                                   [static_cast<std::size_t>(stepAt(event.position.x))];
    if (velocity == 0)
        return;
    if (onGestureBegin)
        onGestureBegin();
    velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, velocity + (wheel.deltaY > 0 ? 5 : -5)));
    if (onPreview)
        onPreview(pattern);
    if (onGestureEnd)
        onGestureEnd();
    repaint();
}
}
