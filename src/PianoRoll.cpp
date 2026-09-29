#include "PianoRoll.h"
#include "NeonTheme.h"
#include <algorithm>

namespace sonora
{
PianoRoll::PianoRoll()
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus(true);
    for (auto* button : { &octaveDown, &octaveUp })
    {
        addAndMakeVisible(button);
        button->setWantsKeyboardFocus(false);
        button->setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    }
    octaveDown.setTooltip("Show the octave below ( [ )");
    octaveUp.setTooltip("Show the octave above ( ] )");
    octaveDown.onClick = [this] { setViewBase(viewBase - 12); };
    octaveUp.onClick = [this] { setViewBase(viewBase + 12); };
}

void PianoRoll::resized()
{
    octaveDown.setBounds(getWidth() - 116, 2, 54, 24);
    octaveUp.setBounds(getWidth() - 58, 2, 54, 24);
}

juce::Rectangle<float> PianoRoll::scrollTrack() const
{
    const auto area = grid();
    return { area.getRight() + 4.0f, area.getY(), 12.0f, area.getHeight() };
}

float PianoRoll::scrollThumbH() const
{
    return std::max(30.0f, scrollTrack().getHeight() * windowRows / 128.0f);
}

void PianoRoll::setViewBase(int pitch)
{
    const int next = juce::jlimit(0, 127 - windowRows + 1, pitch);
    if (next == viewBase)
        return;
    viewBase = next;
    repaint();
}

void PianoRoll::setLiveNotes(const std::vector<int>& notes)
{
    if (notes == liveNotes)
        return;
    liveNotes = notes;
    // Keep played notes on screen: follow MiniLab octave switches.
    for (int pitch : liveNotes)
    {
        if (pitch < viewBase)
            setViewBase(pitch - pitch % 12);
        else if (pitch > viewTop())
            setViewBase(pitch - pitch % 12 - windowRows + 12);
    }
    repaint();
}

void PianoRoll::setPattern(const Pattern& value)
{
    pattern = value;
    // Jump the window to the music when nothing is visible (e.g. pattern or
    // track switch); live playing re-follows via setLiveNotes.
    bool anyVisible = pattern.count == 0;
    for (int i = 0; i < pattern.count && !anyVisible; ++i)
    {
        const int pitch = pattern.notes[static_cast<std::size_t>(i)].pitch;
        anyVisible = pitch >= viewBase && pitch <= viewTop();
    }
    if (!anyVisible && pattern.count > 0)
    {
        const int pitch = pattern.notes[0].pitch;
        viewBase = juce::jlimit(0, 127 - windowRows + 1, pitch - pitch % 12);
    }
    repaint();
}

juce::Rectangle<float> PianoRoll::grid() const
{
    return { 52.0f, 32.0f, static_cast<float>(getWidth() - 54 - 16), static_cast<float>(getHeight() - 34) };
}

juce::Rectangle<float> PianoRoll::noteBounds(const Note& n) const
{
    const auto area = grid();
    const float row = area.getHeight() / windowRows;
    return { area.getX() + area.getWidth() * static_cast<float>(n.start) / patternTicks,
             area.getY() + static_cast<float>(viewTop() - n.pitch) * row,
             area.getWidth() * static_cast<float>(n.duration) / patternTicks, row };
}

int PianoRoll::pitchAt(float y) const
{
    return juce::jlimit(viewBase, viewTop(),
        viewTop() - static_cast<int>((y - grid().getY()) / (grid().getHeight() / windowRows)));
}

int PianoRoll::stepAt(float x) const
{
    return juce::jlimit(0, gridSteps - 1,
        static_cast<int>((x - grid().getX()) / (grid().getWidth() / gridSteps)));
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto area = grid();
    const auto row = area.getHeight() / windowRows;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff111a2e), 0, 0,
                                           juce::Colour(0xff0c111b), 0, static_cast<float>(getHeight()), false));
    g.fillAll();
    ui::caption(g, "KEY", { 5, 4, 44, 22 }, ui::muted, 9.0f);
    for (int pitch = viewBase; pitch <= viewTop(); ++pitch)
    {
        const int key = pitch % 12;
        const bool black = key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
        const auto y = area.getY() + static_cast<float>(viewTop() - pitch) * row;
        g.setColour(juce::Colour(black ? 0xff0c1420 : 0xff111c29));
        g.fillRect(area.getX(), y, area.getWidth(), row - 1.0f);
        g.setColour(black ? ui::background : ui::raised);
        g.fillRoundedRectangle(2, y + 0.5f, black ? 37.0f : 45.0f, row - 1.0f, 2);
        if (key == 0)
        {
            g.setColour(ui::cyan.withAlpha(0.14f));
            g.drawHorizontalLine(static_cast<int>(y + row - 1), area.getX(), area.getRight());
        }
        const bool live = std::find(liveNotes.begin(), liveNotes.end(), pitch) != liveNotes.end();
        if (live)
        {
            g.setColour(ui::cyan.withAlpha(0.35f));
            g.fillRect(area.getX(), y, area.getWidth(), row - 1.0f);
            g.setColour(ui::cyan);
            g.fillRoundedRectangle(2, y + 0.5f, 45.0f, row - 1.0f, 2);
        }
        g.setColour(key == 0 ? ui::cyan : live ? ui::text : ui::muted);
        const auto label = juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
        g.setFont(ui::font(10.0f, key == 0));
        g.drawText(label, 2, static_cast<int>(y), 42, static_cast<int>(row), juce::Justification::centred);
    }
    ui::caption(g, juce::MidiMessage::getMidiNoteName(viewBase + windowRows - 1, true, true, 4) + " - "
                   + juce::MidiMessage::getMidiNoteName(viewBase, true, true, 4),
                 { getWidth() - 240, 4, 120, 22 }, ui::muted, 9.0f);
    // Slim scrollbar: thumb position mirrors the octave window.
    {
        const auto track = scrollTrack();
        const float thumbH = scrollThumbH();
        const float travel = track.getHeight() - thumbH;
        const float thumbY = track.getY() + (1.0f - static_cast<float>(viewBase) / (127 - windowRows + 1)) * travel;
        g.setColour(ui::raised);
        g.fillRoundedRectangle(track, 6.0f);
        g.setColour((scrollDragging ? ui::cyan : ui::cyan.withAlpha(0.55f)));
        g.fillRoundedRectangle(juce::Rectangle<float>(track.getX(), thumbY, track.getWidth(), thumbH), 6.0f);
    }
    for (int step = 0; step <= gridSteps; ++step)
    {
        const auto x = area.getX() + static_cast<float>(step) * area.getWidth() / gridSteps;
        g.setColour(juce::Colour(step % 16 == 0 ? 0xff3a5268 : step % 4 == 0 ? 0xff26374b : 0xff182737));
        g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
        if (step < gridSteps && step % 4 == 0)
        {
            g.setColour(step % 16 == 0 ? ui::cyan : ui::muted);
            g.setFont(ui::font(10.0f, step % 16 == 0));
            g.drawText(juce::String(step / 16 + 1) + "." + juce::String((step / 4) % 4 + 1),
                       static_cast<int>(x) + 5, 3, 40, 22, juce::Justification::centredLeft);
        }
    }
    for (int i = 0; i < pattern.count; ++i)
    {
        const auto& n = pattern.notes[static_cast<std::size_t>(i)];
        if (n.pitch < viewBase || n.pitch > viewTop())
            continue;
        const auto rect = noteBounds(n).reduced(1.2f);
        const bool sounding = playhead >= n.start && playhead < n.start + n.duration;
        const auto colour = sounding ? ui::cyan.brighter(0.45f)
            : ui::cyan.interpolatedWith(ui::blue, 0.3f).withMultipliedBrightness(0.58f + 0.42f * static_cast<float>(n.velocity) / 127.0f);
        g.setColour(colour.withAlpha(sounding ? 0.16f : 0.065f));
        g.fillRoundedRectangle(rect.expanded(2), 4);
        g.setGradientFill(juce::ColourGradient(colour, rect.getTopLeft(), colour.darker(0.4f), rect.getBottomLeft(), false));
        g.fillRoundedRectangle(rect, 2.5f);
        g.setColour(colour.brighter(0.4f).withAlpha(0.8f));
        g.drawRoundedRectangle(rect, 2.5f, 0.6f);
        g.setColour(ui::background.withAlpha(0.65f));
        g.fillRect(rect.getRight() - 4, rect.getY() + 3, 1.0f, std::max(1.0f, rect.getHeight() - 6));
        if (rect.getWidth() > 40 && rect.getHeight() > 11)
        {
            g.setFont(ui::font(9, true));
            g.drawText(juce::MidiMessage::getMidiNoteName(n.pitch, true, true, 4),
                       rect.reduced(5, 0), juce::Justification::centredLeft);
        }
    }
    if (playhead >= 0.0)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float>(playhead / patternTicks);
        g.setGradientFill(juce::ColourGradient(ui::cyan.withAlpha(0.0f), x - 18, 0,
                                              ui::cyan.withAlpha(0.13f), x, 0, false));
        g.fillRect(x - 18, area.getY(), 18.0f, area.getHeight());
        g.setGradientFill(juce::ColourGradient(ui::cyan.brighter(0.5f), x, area.getY(),
                                               ui::violet.brighter(0.4f), x, area.getBottom(), false));
        g.fillRect(x, area.getY(), 1.5f, area.getHeight());
        juce::Path marker;
        marker.addTriangle(x - 4, area.getY() - 7, x + 5, area.getY() - 7, x + 0.5f, area.getY());
        g.fillPath(marker);
    }
    if (pattern.count == 0)
    {
        const auto card = juce::Rectangle<float>(306, 68).withCentre(area.getCentre());
        ui::surface(g, card, 9);
        ui::caption(g, "YOUR NEXT IDEA STARTS HERE", card.toNearestInt().reduced(20, 0).withTrimmedBottom(25), ui::cyan, 11);
        g.setColour(ui::muted);
        g.setFont(ui::font(11));
        g.drawText("Draw a note, or ask the AI assistant.", card.toNearestInt().reduced(20, 0).withTrimmedTop(24),
                   juce::Justification::centredLeft);
    }
}

void PianoRoll::setPlayhead(double tick, bool playing)
{
    const double next = playing ? tick : -1.0;
    if (next < 0.0 && playhead < 0.0)
        return;
    // Only the playhead strip (plus the glow around sounding notes) changes
    // per frame; repainting the whole grid 30x/sec wasted most UI time.
    const auto area = grid();
    const auto strip = [&](double t) {
        const auto x = area.getX() + area.getWidth() * static_cast<float>(t / patternTicks);
        return juce::Rectangle<int>(static_cast<int>(x) - 22, static_cast<int>(area.getY()) - 8,
                                    26, static_cast<int>(area.getHeight()) + 8);
    };
    if (playhead >= 0.0)
        repaint(strip(playhead));
    playhead = next;
    if (playhead >= 0.0)
        repaint(strip(playhead));
    else
        repaint();
}

void PianoRoll::publish()
{
    if (onPreview)
        onPreview(pattern);
    repaint();
}

void PianoRoll::mouseDown(const juce::MouseEvent& event)
{
    if (scrollTrack().expanded(3.0f, 0.0f).contains(event.position))
    {
        // Grab the thumb where pressed so it doesn't jump.
        const float thumbH = scrollThumbH();
        const float travel = scrollTrack().getHeight() - thumbH;
        const float thumbY = scrollTrack().getY()
            + (1.0f - static_cast<float>(viewBase) / (127 - windowRows + 1)) * travel;
        scrollGrab = event.position.y - thumbY;
        scrollDragging = true;
        repaint();
        return;
    }
    if (!grid().contains(event.position))
        return;
    grabKeyboardFocus();
    if (onGestureBegin)
        onGestureBegin();
    gesture = true;
    original = pattern;
    anchorStep = stepAt(event.position.x);
    anchorPitch = pitchAt(event.position.y);
    selected = pattern.noteAt(anchorPitch,
        static_cast<int>((event.position.x - grid().getX()) * patternTicks / grid().getWidth()));
    if (event.mods.isRightButtonDown())
    {
        pattern.erase(selected);
        selected = -1;
        publish();
        return;
    }
    if (selected < 0)
    {
        if (pattern.count == Pattern::capacity)
            return;
        std::uint32_t nextId = 1;
        while (std::any_of(pattern.notes.begin(), pattern.notes.begin() + pattern.count,
                          [nextId](const Note& note) { return note.id == nextId; }))
            ++nextId;
        selected = pattern.count++;
        pattern.notes[static_cast<std::size_t>(selected)] = { nextId, anchorStep * stepTicks, stepTicks, anchorPitch, 100 };
        resizing = true;
    }
    else
        resizing = event.mods.isShiftDown()
            || event.position.x >= noteBounds(pattern.notes[static_cast<std::size_t>(selected)]).getRight() - 5.0f;
    anchor = pattern.notes[static_cast<std::size_t>(selected)];
    original = pattern;
    publish();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event)
{
    if (scrollDragging)
    {
        const float thumbH = scrollThumbH();
        const float travel = scrollTrack().getHeight() - thumbH;
        const float ratio = travel > 0.0f
            ? 1.0f - (event.position.y - scrollGrab - scrollTrack().getY()) / travel : 1.0f;
        setViewBase(static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, ratio) * (127 - windowRows + 1))));
        return;
    }
    if (!gesture || selected < 0)
        return;
    auto candidate = original;
    auto& n = candidate.notes[static_cast<std::size_t>(selected)];
    if (resizing)
        n.duration = juce::jlimit(stepTicks, patternTicks - n.start,
                                 (stepAt(event.position.x) + 1) * stepTicks - n.start);
    else
    {
        n.start = juce::jlimit(0, patternTicks - n.duration,
                              anchor.start + (stepAt(event.position.x) - anchorStep) * stepTicks);
        n.pitch = juce::jlimit(0, 127, anchor.pitch + pitchAt(event.position.y) - anchorPitch);
    }
    if (candidate.valid())
    {
        pattern = candidate;
        publish();
    }
}

void PianoRoll::mouseUp(const juce::MouseEvent&)
{
    if (scrollDragging)
    {
        scrollDragging = false;
        repaint();
        return;
    }
    if (gesture && onGestureEnd)
        onGestureEnd();
    gesture = false;
    selected = -1;
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!grid().contains(event.position) || std::abs(wheel.deltaY) < 0.00001f)
        return;
    const auto index = pattern.noteAt(pitchAt(event.position.y), stepAt(event.position.x) * stepTicks);
    if (index < 0)
    {
        // Empty grid scrolls vertically (wheel up reveals higher notes).
        setViewBase(viewBase + (wheel.deltaY > 0 ? 4 : -4));
        return;
    }
    if (onGestureBegin)
        onGestureBegin();
    auto& n = pattern.notes[static_cast<std::size_t>(index)];
    n.velocity = juce::jlimit(1, 127, n.velocity + (wheel.deltaY > 0 ? 5 : -5));
    publish();
    if (onGestureEnd)
        onGestureEnd();
}
}
