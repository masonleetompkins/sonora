#pragma once
#include "NeonTheme.h"
#include "Sampler.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
// Sampler editor: the track's audio file as a waveform with draggable trim and
// loop markers, play modes, root/tune, and an envelope. Changes stream to the
// engine like the synth panel's, drags form one undo step each.
struct SamplerPanel final : public juce::Component
{
    struct Knob
    {
        const char* group;
        const char* name;
        float SamplerParams::*field;
        float min, max, skewMid;
        const char* suffix;
        int x, y;
    };
    static const std::vector<Knob>& layout()
    {
        static const std::vector<Knob> knobs {
            { "PITCH", "TUNE", &SamplerParams::tune, -100.0f, 100.0f, 0.0f, "ct", 24, 330 },
            { "AMP ENVELOPE", "ATTACK", &SamplerParams::attack, 0.001f, 5.0f, 0.3f, "s", 124, 330 },
            { "AMP ENVELOPE", "DECAY", &SamplerParams::decay, 0.001f, 5.0f, 0.5f, "s", 194, 330 },
            { "AMP ENVELOPE", "SUSTAIN", &SamplerParams::sustain, 0.0f, 1.0f, 0.0f, "%", 264, 330 },
            { "AMP ENVELOPE", "RELEASE", &SamplerParams::release, 0.001f, 5.0f, 0.5f, "s", 334, 330 },
            { "OUTPUT", "LEVEL", &SamplerParams::gain, 0.0f, 2.0f, 0.0f, "%", 434, 330 },
        };
        return knobs;
    }

    SamplerPanel(std::function<void(const SamplerParams&)> changeCb, std::function<void()> dragStartCb,
                 std::function<void()> dragEndCb, std::function<void()> addCb,
                 std::function<void(const juce::String&)> pickCb, std::function<void()> folderCb,
                 std::function<void()> detectCb, std::function<void()> closeCb)
        : onChange(std::move(changeCb)), onDragStart(std::move(dragStartCb)), onDragEnd(std::move(dragEndCb)),
          onAdd(std::move(addCb)), onFolder(std::move(folderCb)), onDetect(std::move(detectCb)),
          onClose(std::move(closeCb)), onPick(std::move(pickCb))
    {
        addAndMakeVisible(libraryBox);
        libraryBox.setTextWhenNothingSelected("Sample library...");
        libraryBox.setWantsKeyboardFocus(false);
        libraryBox.setTooltip("Every sound you have added. Pick one to play it on this track.");
        libraryBox.onChange = [this] {
            const int id = libraryBox.getSelectedId();
            if (!loading && id >= 1 && static_cast<std::size_t>(id) <= names.size() && onPick)
                onPick(names[static_cast<std::size_t>(id) - 1]);
        };
        for (auto* button : { &add, &folder, &detect, &close })
        {
            addAndMakeVisible(button);
            button->setWantsKeyboardFocus(false);
        }
        add.setButtonText("Add sounds...");
        add.setTooltip("Import audio files or a whole folder into the sample library (WAV, AIFF, FLAC, OGG, MP3).");
        add.onClick = [this] { if (onAdd) onAdd(); };
        folder.setButtonText("Library folder");
        folder.setTooltip("Open the sample library in your file manager.");
        folder.onClick = [this] { if (onFolder) onFolder(); };
        detect.setButtonText("Detect root");
        detect.setTooltip("Listen to the sample and set the root key to its pitch.");
        detect.onClick = [this] { if (onDetect) onDetect(); };
        close.setButtonText("Done");
        close.onClick = [this] { if (onClose) onClose(); };
        const struct { juce::ToggleButton* button; const char* text; const char* tip; } toggles[] {
            { &loopToggle, "Loop", "Hold a key to repeat the loop region (drag the green markers)." },
            { &oneShotToggle, "One-shot", "Ignore key release: the sound always plays to its end." },
            { &reverseToggle, "Reverse", "Play the sample backwards." },
            { &keyTrackToggle, "Key track", "On: keys change the pitch. Off: every key plays the sample as recorded." },
        };
        for (const auto& toggle : toggles)
        {
            addAndMakeVisible(toggle.button);
            toggle.button->setButtonText(toggle.text);
            toggle.button->setTooltip(toggle.tip);
            toggle.button->setWantsKeyboardFocus(false);
            toggle.button->onClick = [this] {
                if (loading)
                    return;
                params.loop = loopToggle.getToggleState();
                params.oneShot = oneShotToggle.getToggleState();
                params.reverse = reverseToggle.getToggleState();
                params.keyTrack = keyTrackToggle.getToggleState();
                if (onDragStart) onDragStart();
                publish();
                if (onDragEnd) onDragEnd();
                repaint(waveArea());
            };
        }
        addAndMakeVisible(rootBox);
        for (int note = 0; note < 128; ++note)
            rootBox.addItem(juce::MidiMessage::getMidiNoteName(note, true, true, 4) + "  (" + juce::String(note) + ")",
                            note + 1);
        rootBox.setWantsKeyboardFocus(false);
        rootBox.setTooltip("The key that plays the sample at its recorded pitch (C4 is middle C).");
        rootBox.onChange = [this] {
            if (loading)
                return;
            params.rootNote = std::clamp(rootBox.getSelectedId() - 1, 0, 127);
            if (onDragStart) onDragStart();
            publish();
            if (onDragEnd) onDragEnd();
        };
        const auto& spec = layout();
        knobs.resize(spec.size());
        for (std::size_t i = 0; i < spec.size(); ++i)
        {
            auto& knob = knobs[i];
            knob.slider = std::make_unique<juce::Slider>();
            knob.label = std::make_unique<juce::Label>();
            addAndMakeVisible(knob.slider.get());
            addAndMakeVisible(knob.label.get());
            auto& slider = *knob.slider;
            slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setWantsKeyboardFocus(false);
            slider.setRange(spec[i].min, spec[i].max, 0.0);
            if (spec[i].skewMid > 0.0f)
                slider.setSkewFactorFromMidPoint(spec[i].skewMid);
            slider.setDoubleClickReturnValue(true, static_cast<double>(SamplerParams {}.*spec[i].field));
            slider.setColour(juce::Slider::thumbColourId, ui::cyan);
            slider.onDragStart = [this] { if (onDragStart) onDragStart(); };
            slider.onDragEnd = [this] { if (onDragEnd) onDragEnd(); };
            slider.onValueChange = [this, i] {
                if (loading)
                    return;
                params.*layout()[i].field = static_cast<float>(knobs[i].slider->getValue());
                publish();
            };
            knob.label->setFont(ui::font(11.0f, true, 0.08f));
            knob.label->setColour(juce::Label::textColourId, ui::muted);
            knob.label->setJustificationType(juce::Justification::centred);
            knob.label->setInterceptsMouseClicks(false, false);
        }
        addAndMakeVisible(info);
        info.setFont(ui::font(11.0f));
        info.setColour(juce::Label::textColourId, ui::muted);
        addAndMakeVisible(hint);
        hint.setFont(ui::font(11.0f));
        hint.setColour(juce::Label::textColourId, ui::muted);
        hint.setText("Drag the markers on the waveform to trim (cyan) or set the loop (green). Play the keys or your MIDI "
                     "controller while you edit. Any recorded take can be sent here from the Audio tab.",
                     juce::dontSendNotification);
        setSize(920, 470);
    }

    void refresh(const SamplerParams& value, const juce::String& trackName, const juce::String& file,
                 const SampleOverview& overview, const std::vector<juce::String>* library = nullptr)
    {
        loading = true;
        params = value;
        title = trackName;
        sampleName = file;
        shown = overview;
        if (library != nullptr)
            names = *library;
        libraryBox.clear(juce::dontSendNotification);
        int selected = 0;
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            libraryBox.addItem(names[i], static_cast<int>(i) + 1);
            if (names[i] == file)
                selected = static_cast<int>(i) + 1;
        }
        libraryBox.setSelectedId(selected, juce::dontSendNotification);
        loopToggle.setToggleState(params.loop, juce::dontSendNotification);
        oneShotToggle.setToggleState(params.oneShot, juce::dontSendNotification);
        reverseToggle.setToggleState(params.reverse, juce::dontSendNotification);
        keyTrackToggle.setToggleState(params.keyTrack, juce::dontSendNotification);
        rootBox.setSelectedId(params.rootNote + 1, juce::dontSendNotification);
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i].slider->setValue(params.*layout()[i].field, juce::dontSendNotification);
        refreshLabels();
        juce::String text;
        if (file.isEmpty())
            text = "No sound yet. Add sounds, or pick one from the library.";
        else if (!overview.loaded)
            text = file + "  /  loading... (or the file is missing)";
        else
        {
            text = file + "  /  " + (overview.channels > 1 ? "stereo" : "mono") + "  /  "
                + juce::String(overview.seconds, overview.seconds < 10.0 ? 2 : 1) + " s  /  "
                + juce::String(overview.rate / 1000.0, 1) + " kHz";
            if (overview.detectedRoot >= 0)
                text += "  /  sounds like " + juce::MidiMessage::getMidiNoteName(overview.detectedRoot, true, true, 4);
        }
        info.setText(text, juce::dontSendNotification);
        detect.setEnabled(overview.loaded);
        loading = false;
        repaint();
    }

    void refreshLabels()
    {
        for (std::size_t i = 0; i < knobs.size(); ++i)
        {
            const auto& spec = layout()[i];
            const float value = params.*spec.field;
            const juce::String suffix(spec.suffix);
            juce::String text;
            if (suffix == "%")
                text = juce::String(juce::roundToInt(value * 100.0f)) + "%";
            else if (suffix == "s")
                text = value < 1.0f ? juce::String(juce::roundToInt(value * 1000.0f)) + " ms" : juce::String(value, 2) + " s";
            else
                text = (value > 0.0f ? "+" : "") + juce::String(value, 0) + " " + suffix;
            knobs[i].label->setText(juce::String(spec.name) + "\n" + text, juce::dontSendNotification);
        }
    }

    juce::Rectangle<int> waveArea() const { return { 24, 64, getWidth() - 48, 150 }; }

    void resized() override
    {
        close.setBounds(getWidth() - 100, 16, 76, 28);
        folder.setBounds(getWidth() - 214, 16, 106, 28);
        add.setBounds(getWidth() - 336, 16, 114, 28);
        libraryBox.setBounds(getWidth() - 576, 16, 232, 28);
        info.setBounds(24, 218, getWidth() - 48, 20);
        loopToggle.setBounds(24, 250, 90, 26);
        oneShotToggle.setBounds(120, 250, 110, 26);
        reverseToggle.setBounds(236, 250, 100, 26);
        keyTrackToggle.setBounds(342, 250, 110, 26);
        rootBox.setBounds(540, 250, 150, 28);
        detect.setBounds(698, 250, 110, 28);
        const auto& spec = layout();
        for (std::size_t i = 0; i < knobs.size(); ++i)
        {
            knobs[i].slider->setBounds(spec[i].x + 4, spec[i].y, 56, 56);
            knobs[i].label->setBounds(spec[i].x - 4, spec[i].y + 56, 72, 26);
        }
        hint.setBounds(540, 330, getWidth() - 564, 60);
    }

    float markerX(float fraction) const
    {
        const auto area = waveArea().toFloat();
        return area.getX() + 6.0f + fraction * (area.getWidth() - 12.0f);
    }
    float markerFraction(float x) const
    {
        const auto area = waveArea().toFloat();
        return std::clamp((x - area.getX() - 6.0f) / (area.getWidth() - 12.0f), 0.0f, 1.0f);
    }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        ui::caption(g, "SAMPLER  /  " + title.toUpperCase(), { 24, 16, 420, 26 }, ui::text, 13.0f);
        ui::caption(g, "MODE", { 24, 236, 120, 14 }, ui::cyan, 9.0f);
        ui::caption(g, "ROOT KEY", { 540, 236, 120, 14 }, ui::cyan, 9.0f);
        juce::String last;
        for (const auto& knob : layout())
            if (last != knob.group)
            {
                last = knob.group;
                ui::caption(g, knob.group, { knob.x + 4, knob.y - 20, 220, 16 }, ui::cyan, 9.0f);
            }
        const auto area = waveArea().toFloat();
        g.setColour(ui::background);
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(ui::border);
        g.drawRoundedRectangle(area, 8.0f, 1.0f);
        g.drawHorizontalLine(static_cast<int>(area.getCentreY()), area.getX() + 6, area.getRight() - 6);
        if (!shown.loaded || shown.peaks.empty())
        {
            g.setColour(ui::muted);
            g.setFont(ui::font(12.0f));
            g.drawText(sampleName.isEmpty() ? "Add a sound to start" : "Loading...", area.toNearestInt(),
                       juce::Justification::centred);
            return;
        }
        // Waveform, with the trimmed-away parts dimmed.
        const int buckets = static_cast<int>(shown.peaks.size());
        const float half = area.getHeight() * 0.5f - 8.0f;
        const float x0 = markerX(0.0f), x1 = markerX(1.0f);
        const float trimLeft = markerX(params.start), trimRight = markerX(params.end);
        for (float x = x0; x < x1; x += 1.0f)
        {
            const float f = (x - x0) / (x1 - x0);
            const float peak = shown.peaks[static_cast<std::size_t>(std::min(buckets - 1, static_cast<int>(f * buckets)))];
            const bool inside = x >= trimLeft && x <= trimRight;
            g.setColour((inside ? ui::cyan : ui::muted).withAlpha(inside ? 0.9f : 0.35f));
            const float h = std::max(1.0f, peak * half);
            g.fillRect(x, area.getCentreY() - h, 1.0f, h * 2.0f);
        }
        if (params.loop && !params.reverse && !params.oneShot)
        {
            const float a = markerX(std::max(params.loopStart, params.start)), b = markerX(std::min(params.loopEnd, params.end));
            g.setColour(juce::Colour(0xff4ade80).withAlpha(0.14f));
            g.fillRect(a, area.getY() + 2.0f, std::max(0.0f, b - a), area.getHeight() - 4.0f);
            for (float x : { a, b })
            {
                g.setColour(juce::Colour(0xff4ade80));
                g.fillRect(x - 1.0f, area.getY() + 2.0f, 2.0f, area.getHeight() - 4.0f);
                g.fillRoundedRectangle(x - 5.0f, area.getY() + 2.0f, 10.0f, 10.0f, 2.0f);
            }
        }
        for (float x : { trimLeft, trimRight })
        {
            g.setColour(ui::cyan);
            g.fillRect(x - 1.0f, area.getY() + 2.0f, 2.0f, area.getHeight() - 4.0f);
            g.fillRoundedRectangle(x - 5.0f, area.getBottom() - 12.0f, 10.0f, 10.0f, 2.0f);
        }
    }

    enum class Marker { None, Start, End, LoopStart, LoopEnd };
    Marker markerAt(juce::Point<float> p) const
    {
        const auto area = waveArea().toFloat();
        if (!area.expanded(6.0f).contains(p) || !shown.loaded)
            return Marker::None;
        struct Candidate { Marker marker; float x; bool top; };
        const bool loopShown = params.loop && !params.reverse && !params.oneShot;
        std::vector<Candidate> candidates {
            { Marker::Start, markerX(params.start), false }, { Marker::End, markerX(params.end), false } };
        if (loopShown)
        {
            candidates.push_back({ Marker::LoopStart, markerX(std::max(params.loopStart, params.start)), true });
            candidates.push_back({ Marker::LoopEnd, markerX(std::min(params.loopEnd, params.end)), true });
        }
        Marker best = Marker::None;
        float bestDistance = 9.0f;
        for (const auto& c : candidates)
        {
            // Grab handles by their half of the panel first, then by distance.
            const float distance = std::abs(p.x - c.x) + (c.top == (p.y < area.getCentreY()) ? 0.0f : 3.0f);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = c.marker;
            }
        }
        return best;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragging = markerAt(event.position);
        if (dragging != Marker::None && onDragStart)
            onDragStart();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragging == Marker::None)
            return;
        const float f = markerFraction(event.position.x);
        constexpr float m = SamplerParams::minRegion;
        switch (dragging)
        {
            case Marker::Start: params.start = std::min(f, params.end - m); break;
            case Marker::End: params.end = std::max(f, params.start + m); break;
            case Marker::LoopStart: params.loopStart = std::min(f, params.loopEnd - m); break;
            case Marker::LoopEnd: params.loopEnd = std::max(f, params.loopStart + m); break;
            case Marker::None: break;
        }
        normalizeRegions(params);
        publish();
        repaint(waveArea());
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging != Marker::None && onDragEnd)
            onDragEnd();
        dragging = Marker::None;
    }
    void mouseMove(const juce::MouseEvent& event) override
    {
        setMouseCursor(markerAt(event.position) != Marker::None ? juce::MouseCursor::LeftRightResizeCursor
                                                               : juce::MouseCursor::NormalCursor);
    }

    void publish()
    {
        refreshLabels();
        if (onChange)
            onChange(params);
    }

    struct KnobUi
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };
    SamplerParams params;
    SampleOverview shown;
    juce::String title, sampleName;
    std::vector<juce::String> names;
    bool loading = false;
    Marker dragging = Marker::None;
    std::vector<KnobUi> knobs;
    juce::ComboBox libraryBox, rootBox;
    juce::TextButton add, folder, detect, close;
    juce::ToggleButton loopToggle, oneShotToggle, reverseToggle, keyTrackToggle;
    juce::Label info, hint;
    std::function<void(const SamplerParams&)> onChange;
    std::function<void()> onDragStart, onDragEnd, onAdd, onFolder, onDetect, onClose;
    std::function<void(const juce::String&)> onPick;
};
}
