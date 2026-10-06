#include "MainComponent.h"
#include "SonoraPaths.h"

namespace sonora
{
// Per-track hues keyed by track icon; the palette cycles every 8 icons.
juce::Colour trackColour(int icon)
{
    const juce::Colour palette[] {
        ui::cyan, ui::violet, ui::blue, ui::warn, ui::danger,
        juce::Colour(0xff9dff70), juce::Colour(0xff70e0ff), juce::Colour(0xffff8de0),
    };
    return palette[icon & 7];
}

const char* trackIconName(int icon)
{
    static constexpr const char* names[] {
        "Keys", "Drums", "Bass", "Lead", "Pad", "Strings", "Vox", "FX",
    };
    return names[icon & 7];
}

struct MainComponent::FxBar final : public juce::Component
{
    struct Slot
    {
        const char* name;
        float min, max;
        const char* suffix;
        float skewMid = 0.0f;
    };
    FxBar(std::function<void(int)> targetCb, std::function<void(int)> effectCb,
          std::function<void(bool)> enableCb, std::function<float(int)> getCb,
          std::function<void(int, float)> setCb,
          std::function<void()> dragStartCb, std::function<void()> dragEndCb)
        : onTarget(std::move(targetCb)), onEffect(std::move(effectCb)), onEnable(std::move(enableCb)),
          onGet(std::move(getCb)), onSet(std::move(setCb)),
          onDragStart(std::move(dragStartCb)), onDragEnd(std::move(dragEndCb))
    {
        const char* targets[] { "TRACK", "TRACK", "MASTER" };
        for (int i = 0; i < 3; ++i)
        {
            auto& button = targetButtons[static_cast<std::size_t>(i)];
            addAndMakeVisible(button);
            button.setButtonText(targets[i]);
            button.setWantsKeyboardFocus(false);
            button.setColour(juce::TextButton::buttonOnColourId, ui::blue);
            button.onClick = [this, i] { if (onTarget) onTarget(i); };
            button.setVisible(i != 1);
        }
        const char* effects[] { "DRV", "EQ", "CMP", "CHO", "DLY", "VRB" };
        for (int i = 0; i < numEffects; ++i)
        {
            auto& button = effectButtons[static_cast<std::size_t>(i)];
            addAndMakeVisible(button);
            button.setButtonText(effects[i]);
            button.setWantsKeyboardFocus(false);
            button.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
            button.onClick = [this, i] { if (onEffect) onEffect(i); };
        }
        enable.setButtonText("ON");
        enable.setClickingTogglesState(true);
        enable.setWantsKeyboardFocus(false);
        enable.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
        addAndMakeVisible(enable);
        enable.onClick = [this] { if (onEnable) onEnable(enable.getToggleState()); };
        for (int i = 0; i < 4; ++i)
        {
            auto& knob = knobs[static_cast<std::size_t>(i)];
            auto& label = knobLabels[static_cast<std::size_t>(i)];
            addAndMakeVisible(knob);
            addAndMakeVisible(label);
            knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            knob.setWantsKeyboardFocus(false);
            knob.setColour(juce::Slider::thumbColourId, ui::cyan);
            knob.onDragStart = [this] { if (onDragStart) onDragStart(); };
            knob.onDragEnd = [this] { if (onDragEnd) onDragEnd(); };
            knob.onValueChange = [this, i] {
                if (onSet)
                    onSet(i, static_cast<float>(knobs[static_cast<std::size_t>(i)].getValue()));
                refreshValues();
            };
            label.setFont(ui::font(11.0f, true, 0.08f));
            label.setColour(juce::Label::textColourId, ui::muted);
            label.setJustificationType(juce::Justification::centred);
        }
        addAndMakeVisible(reduction);
        reduction.setFont(ui::font(10.0f, true));
        reduction.setColour(juce::Label::textColourId, ui::muted);
        reduction.setJustificationType(juce::Justification::centredRight);
    }

    static std::vector<Slot> slotsFor(int target, int effect)
    {
        if (target == 2)
            return { { "CEIL", -12.0f, 0.0f, "dB" }, { "REL", 20.0f, 500.0f, "ms" } };
        switch (effect)
        {
            case 0: return { { "DRIVE", 0.0f, 1.0f, "%" }, { "TONE", 800.0f, 16000.0f, "Hz", 4000.0f } };
            case 1: return { { "LOW", -15.0f, 15.0f, "dB" }, { "MID", -15.0f, 15.0f, "dB" },
                             { "MIDF", 200.0f, 8000.0f, "Hz", 1200.0f }, { "HIGH", -15.0f, 15.0f, "dB" } };
            case 2: return { { "THR", -40.0f, 0.0f, "dB" }, { "RATIO", 1.0f, 12.0f, ":1" },
                             { "ATT", 0.5f, 100.0f, "ms" }, { "REL", 20.0f, 1000.0f, "ms" } };
            case 3: return { { "RATE", 0.1f, 5.0f, "Hz", 1.0f }, { "DEPTH", 0.0f, 1.0f, "%" },
                             { "MIX", 0.0f, 1.0f, "%" } };
            case 4: return { { "TIME", 20.0f, 1000.0f, "ms" }, { "FDBK", 0.0f, 0.8f, "" },
                             { "MIX", 0.0f, 1.0f, "%", } };
            default: return { { "SIZE", 0.0f, 1.0f, "" }, { "DAMP", 0.0f, 1.0f, "" }, { "MIX", 0.0f, 1.0f, "%" } };
        }
    }

    void refresh(int target, int effect, bool enabled)
    {
        for (int i = 0; i < 3; ++i)
            targetButtons[static_cast<std::size_t>(i)].setToggleState(i == target, juce::dontSendNotification);
        for (int i = 0; i < numEffects; ++i)
        {
            auto& button = effectButtons[static_cast<std::size_t>(i)];
            button.setVisible(target != 2);
            button.setToggleState(i == effect, juce::dontSendNotification);
        }
        enable.setToggleState(enabled, juce::dontSendNotification);
        enable.setButtonText(enabled ? "ON" : "OFF");
        slots = slotsFor(target, effect);
        for (int i = 0; i < 4; ++i)
        {
            const bool visible = i < static_cast<int>(slots.size());
            knobs[static_cast<std::size_t>(i)].setVisible(visible);
            knobLabels[static_cast<std::size_t>(i)].setVisible(visible);
            if (visible)
            {
                auto& knob = knobs[static_cast<std::size_t>(i)];
                knob.setRange(slots[static_cast<std::size_t>(i)].min, slots[static_cast<std::size_t>(i)].max, 0);
                if (slots[static_cast<std::size_t>(i)].skewMid > 0.0f)
                    knob.setSkewFactorFromMidPoint(slots[static_cast<std::size_t>(i)].skewMid);
                else
                    knob.setSkewFactor(1.0);
            }
        }
        refreshValues();
        resized();
    }

    void refreshValues()
    {
        for (int i = 0; i < static_cast<int>(slots.size()) && i < 4; ++i)
        {
            const auto value = onGet ? onGet(i) : 0.0f;
            knobs[static_cast<std::size_t>(i)].setValue(value, juce::dontSendNotification);
            const auto& slot = slots[static_cast<std::size_t>(i)];
            juce::String text = juce::String(slot.suffix) == "%"
                ? juce::String(static_cast<int>(value * 100.0f)) + "%"
                : juce::String(value, slot.max >= 100.0f ? 0 : 1) + " " + slot.suffix;
            knobLabels[static_cast<std::size_t>(i)].setText(juce::String(slot.name) + "\n" + text.trim(),
                                                            juce::dontSendNotification);
        }
    }

    void setReduction(float db) { reduction.setText(db < -0.05f ? juce::String(db, 1) + " dB GR" : "---", juce::dontSendNotification); }

    void resized() override
    {
        auto row = getLocalBounds().reduced(10, 8);
        auto tabs = row.removeFromLeft(266);
        tabs.removeFromTop(2);
        auto targetRow = tabs.removeFromTop(26);
        for (int i = 0; i < 3; ++i)
        {
            if (i == 1)
                continue;
            targetButtons[static_cast<std::size_t>(i)].setBounds(targetRow.removeFromLeft(112));
            targetRow.removeFromLeft(4);
        }
        auto effectRow = tabs.removeFromTop(28);
        for (int i = 0; i < numEffects; ++i)
        {
            if (effectButtons[static_cast<std::size_t>(i)].isVisible())
            {
                effectButtons[static_cast<std::size_t>(i)].setBounds(effectRow.removeFromLeft(41));
                effectRow.removeFromLeft(3);
            }
        }
        enable.setBounds(row.removeFromLeft(64).removeFromTop(30));
        row.removeFromLeft(8);
        reduction.setBounds(row.removeFromRight(110).removeFromTop(30));
        const int knobWidth = juce::jmax(70, static_cast<int>(row.getWidth()) / 4);
        for (int i = 0; i < 4; ++i)
        {
            if (!knobs[static_cast<std::size_t>(i)].isVisible())
                continue;
            auto cell = row.removeFromLeft(knobWidth);
            knobs[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(cell.getHeight() - 26));
            knobLabels[static_cast<std::size_t>(i)].setBounds(cell);
        }
    }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        ui::caption(g, "INSERT FX", { 18, getHeight() - 22, 140, 16 }, ui::violet, 9.0f);
    }

    std::array<juce::TextButton, 3> targetButtons;
    // Effect tab order mirrors the signal chain; indices are the fxEffect ids.
    static constexpr int numEffects = 6;
    std::array<juce::TextButton, numEffects> effectButtons;
    juce::TextButton enable;
    std::array<juce::Slider, 4> knobs;
    std::array<juce::Label, 4> knobLabels;
    juce::Label reduction;
    std::vector<Slot> slots;
    std::function<void(int)> onTarget, onEffect;
    std::function<void(bool)> onEnable;
    std::function<float(int)> onGet;
    std::function<void(int, float)> onSet;
    std::function<void()> onDragStart, onDragEnd;
};


struct MainComponent::KitPanel final : public juce::Component
{
    KitPanel(std::function<void(int)> loadCb, std::function<void(int)> clearCb,
             std::function<void(int)> auditionCb, std::function<void()> closeCb,
             std::function<void(int)> presetLoadCb, std::function<void()> presetSaveCb,
             std::function<void()> presetDeleteCb)
        : onLoad(std::move(loadCb)), onClear(std::move(clearCb)),
          onAudition(std::move(auditionCb)), onClose(std::move(closeCb)),
          onPresetSave(std::move(presetSaveCb)), onPresetDelete(std::move(presetDeleteCb)),
          onPresetLoad(std::move(presetLoadCb))
    {
        setWantsKeyboardFocus(true);
        for (auto* box : { &presetBox })
        {
            addAndMakeVisible(box);
            box->setColour(juce::ComboBox::backgroundColourId, ui::raised);
            box->setColour(juce::ComboBox::textColourId, ui::text);
            box->setColour(juce::ComboBox::outlineColourId, ui::border);
            box->setWantsKeyboardFocus(false);
            box->onChange = [this] {
                if (onPresetLoad)
                    onPresetLoad(presetBox.getSelectedId());
            };
        }
        presetBox.setTooltip("Factory kits plus your saved presets. Choosing one loads it (undoable).");
        addAndMakeVisible(presetName);
        presetName.setTextToShowWhenEmpty("Preset name...", ui::muted);
        presetName.setColour(juce::TextEditor::backgroundColourId, ui::background);
        presetName.setColour(juce::TextEditor::textColourId, ui::text);
        presetName.setColour(juce::TextEditor::outlineColourId, ui::border);
        presetName.setColour(juce::TextEditor::focusedOutlineColourId, ui::violet);
        presetSave.setButtonText("Save");
        presetDelete.setButtonText("Delete");
        for (auto* button : { &presetSave, &presetDelete })
        {
            addAndMakeVisible(button);
            button->setWantsKeyboardFocus(false);
            button->setColour(juce::TextButton::buttonOnColourId, ui::violet);
        }
        presetSave.onClick = [this] { if (onPresetSave) onPresetSave(); };
        presetDelete.onClick = [this] { if (onPresetDelete) onPresetDelete(); };
        presetSave.setTooltip("Save the current kit (variant + samples) under the name above.");
        presetDelete.setTooltip("Delete the selected user preset. Factory kits cannot be deleted.");
        for (int pad = 0; pad < drumPads; ++pad)
        {
            auto& row = rows[static_cast<std::size_t>(pad)];
            row.name = std::make_unique<juce::Label>();
            row.name->setFont(ui::font(12.0f, true));
            addAndMakeVisible(row.name.get());
            row.load.setButtonText("Load");
            row.clear.setButtonText("Clear");
            row.play.setButtonText("Play");
            row.play.setTriggeredOnMouseDown(true);
            for (auto* button : { &row.load, &row.clear, &row.play })
            {
                addAndMakeVisible(button);
                button->setWantsKeyboardFocus(false);
                button->setColour(juce::TextButton::buttonOnColourId, ui::violet);
            }
            row.load.onClick = [this, pad] { if (onLoad) onLoad(pad); };
            row.clear.onClick = [this, pad] { if (onClear) onClear(pad); };
            row.play.onClick = [this, pad] { if (onAudition) onAudition(pad); };
        }
        close.setButtonText("Done");
        close.setWantsKeyboardFocus(false);
        addAndMakeVisible(close);
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(hint);
        hint.setFont(ui::font(11.0f));
        hint.setColour(juce::Label::textColourId, ui::muted);
        hint.setText("WAV, AIFF, FLAC, MP3, or Ogg up to 10 seconds. Files copy into the project media folder on save.",
                     juce::dontSendNotification);
        setSize(560, 460);
    }

    int getSelectedPresetId() const { return presetBox.getSelectedId(); }
    juce::String getPresetName() const { return presetName.getText().trim(); }
    void setPresetName(const juce::String& name) { presetName.setText(name, juce::dontSendNotification); }

    void refresh(const ProjectState& project, int drumTrack, const juce::File& mediaDir,
                 const std::vector<juce::String>& userPresets, const juce::String& currentPreset)
    {
        presetBox.clear(juce::dontSendNotification);
        for (int variant = 0; variant < numKitVariants; ++variant)
            presetBox.addItem(juce::String(kitVariantName(variant)) + " (factory)", variant + 1);
        for (std::size_t i = 0; i < userPresets.size(); ++i)
            presetBox.addItem(userPresets[i], 100 + static_cast<int>(i));
        if (currentPreset.isNotEmpty())
        {
            for (int index = 0; index < presetBox.getNumItems(); ++index)
            {
                const int id = presetBox.getItemId(index);
                juce::String item = presetBox.getItemText(index);
                if (id >= 1 && id <= numKitVariants)
                    item = item.upToFirstOccurrenceOf(" (", false, false);
                if (item == currentPreset)
                {
                    presetBox.setSelectedId(id, juce::dontSendNotification);
                    break;
                }
            }
        }
        for (int pad = 0; pad < drumPads; ++pad)
        {
            auto& row = rows[static_cast<std::size_t>(pad)];
            const auto file = padSampleName(project.tracks[static_cast<std::size_t>(drumTrack)].padSamples[static_cast<std::size_t>(pad)]);
            const bool custom = file.isNotEmpty();
            const bool missing = custom && !mediaDir.getChildFile(file).existsAsFile()
                && !sessionDir().getChildFile(file).existsAsFile();
            row.name->setText(juce::String(drumNames[static_cast<std::size_t>(pad)]) + "   /   "
                + (custom ? file + (missing ? "   (missing)" : "") : "starter kit"),
                juce::dontSendNotification);
            row.name->setColour(juce::Label::textColourId, missing ? ui::danger : custom ? ui::violet : ui::text);
            row.clear.setEnabled(custom);
        }
        resized();
    }

    void resized() override
    {
        presetBox.setBounds(20, 14, 170, 28);
        presetName.setBounds(198, 14, 150, 28);
        presetSave.setBounds(356, 14, 60, 28);
        presetDelete.setBounds(424, 14, 60, 28);
        int y = 54;
        for (int pad = 0; pad < drumPads; ++pad)
        {
            auto& row = rows[static_cast<std::size_t>(pad)];
            row.name->setBounds(20, y, 300, 28);
            row.load.setBounds(330, y, 60, 28);
            row.clear.setBounds(398, y, 60, 28);
            row.play.setBounds(466, y, 60, 28);
            y += 36;
        }
        hint.setBounds(20, y + 2, 520, 40);
        close.setBounds(446, y + 48, 80, 30);
    }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        ui::caption(g, "DRUM KIT  /  PER-PAD SAMPLES", { 20, getHeight() - 52, 400, 20 }, ui::text, 13.0f);
    }

    struct Row
    {
        std::unique_ptr<juce::Label> name;
        juce::TextButton load, clear, play;
    };
    std::array<Row, drumPads> rows;
    juce::ComboBox presetBox;
    juce::TextEditor presetName;
    juce::TextButton presetSave, presetDelete;
    juce::TextButton close;
    juce::Label hint;
    std::function<void(int)> onLoad, onClear, onAudition;
    std::function<void()> onClose, onPresetSave, onPresetDelete;
    std::function<void(int)> onPresetLoad;
};

// Sound designer for the editable synth (instrument 0). Edits stream straight
// to the engine, so held notes and the performance keyboard play the new sound
// immediately; drag gestures and combo changes each form one undo step.
struct MainComponent::SynthPanel final : public juce::Component
{
    struct Knob
    {
        const char* group;
        const char* name;
        float SynthParams::*field;
        float min, max, skewMid;
        const char* suffix;
        int x, y;
    };
    static const std::vector<Knob>& layout()
    {
        static const std::vector<Knob> knobs {
            { "OSC 2", "LEVEL", &SynthParams::mix2, 0.0f, 1.0f, 0.0f, "%", 470, 72 },
            { "OSC 2", "SEMI", &SynthParams::semis2, -24.0f, 24.0f, 0.0f, "st", 540, 72 },
            { "OSC 2", "FINE", &SynthParams::detune2, -50.0f, 50.0f, 0.0f, "ct", 610, 72 },
            { "CHARACTER", "DRIVE", &SynthParams::drive, 0.0f, 1.0f, 0.0f, "%", 700, 72 },
            { "CHARACTER", "CHORUS", &SynthParams::chorus, 0.0f, 1.0f, 0.0f, "%", 770, 72 },
            { "CHARACTER", "LEVEL", &SynthParams::level, 0.0f, 1.5f, 0.0f, "%", 840, 72 },
            { "FILTER", "CUTOFF", &SynthParams::cutoff, 40.0f, 20000.0f, 1000.0f, "Hz", 24, 212 },
            { "FILTER", "RESO", &SynthParams::resonance, 0.0f, 0.95f, 0.0f, "%", 94, 212 },
            { "FILTER", "ENV", &SynthParams::envAmount, -4.0f, 4.0f, 0.0f, "oct", 164, 212 },
            { "FILTER ENVELOPE", "ATTACK", &SynthParams::filterAttack, 0.001f, 5.0f, 0.3f, "s", 264, 212 },
            { "FILTER ENVELOPE", "DECAY", &SynthParams::filterDecay, 0.001f, 5.0f, 0.5f, "s", 334, 212 },
            { "FILTER ENVELOPE", "SUSTAIN", &SynthParams::filterSustain, 0.0f, 1.0f, 0.0f, "%", 404, 212 },
            { "FILTER ENVELOPE", "RELEASE", &SynthParams::filterRelease, 0.001f, 5.0f, 0.5f, "s", 474, 212 },
            { "LFO", "RATE", &SynthParams::lfoRate, 0.1f, 20.0f, 4.0f, "Hz", 574, 212 },
            { "LFO", "VIBRATO", &SynthParams::lfoPitch, 0.0f, 2.0f, 0.0f, "st", 644, 212 },
            { "LFO", "WOBBLE", &SynthParams::lfoFilter, 0.0f, 4.0f, 0.0f, "oct", 714, 212 },
            { "AMP ENVELOPE", "ATTACK", &SynthParams::attack, 0.001f, 5.0f, 0.3f, "s", 24, 346 },
            { "AMP ENVELOPE", "DECAY", &SynthParams::decay, 0.001f, 5.0f, 0.5f, "s", 94, 346 },
            { "AMP ENVELOPE", "SUSTAIN", &SynthParams::sustain, 0.0f, 1.0f, 0.0f, "%", 164, 346 },
            { "AMP ENVELOPE", "RELEASE", &SynthParams::release, 0.001f, 5.0f, 0.5f, "s", 234, 346 },
        };
        return knobs;
    }

    SynthPanel(std::function<void(const SynthParams&)> changeCb, std::function<void()> dragStartCb,
               std::function<void()> dragEndCb, std::function<void(int)> patchCb, std::function<void()> closeCb)
        : onChange(std::move(changeCb)), onDragStart(std::move(dragStartCb)), onDragEnd(std::move(dragEndCb)),
          onPatch(std::move(patchCb)), onClose(std::move(closeCb))
    {
        addAndMakeVisible(patchBox);
        patchBox.setTextWhenNothingSelected("Custom sound");
        for (std::size_t i = 0; i < synthPatches().size(); ++i)
            patchBox.addItem(synthPatches()[i].name, static_cast<int>(i) + 1);
        patchBox.setWantsKeyboardFocus(false);
        patchBox.setTooltip("Start from a factory sound, then shape it with the knobs (undoable).");
        patchBox.onChange = [this] {
            const int id = patchBox.getSelectedId();
            if (id > 0 && onPatch)
                onPatch(id - 1);
        };
        for (auto* box : { &wave1, &wave2 })
        {
            addAndMakeVisible(box);
            for (int wave = 0; wave < numSynthWaves; ++wave)
                box->addItem(synthWaveName(wave), wave + 1);
            box->setWantsKeyboardFocus(false);
            box->onChange = [this] {
                if (loading)
                    return;
                params.wave = std::max(0, wave1.getSelectedId() - 1);
                params.wave2 = std::max(0, wave2.getSelectedId() - 1);
                if (onDragStart) onDragStart();
                publish();
                if (onDragEnd) onDragEnd();
            };
        }
        wave1.setTooltip("Main oscillator shape. Sine is pure; triangle soft; saw bright; square hollow.");
        wave2.setTooltip("Second oscillator shape. Raise OSC 2 LEVEL to blend it in.");
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
            slider.setDoubleClickReturnValue(true, static_cast<double>(SynthParams {}.*spec[i].field));
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
        close.setButtonText("Done");
        close.setWantsKeyboardFocus(false);
        addAndMakeVisible(close);
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(hint);
        hint.setFont(ui::font(11.0f));
        hint.setColour(juce::Label::textColourId, ui::muted);
        hint.setText("Play the keys or your MIDI controller while you tweak. Double-click a knob to reset it. "
                     "TRACK effects below add EQ, compression, delay, and reverb.",
                     juce::dontSendNotification);
        setSize(920, 470);
    }

    void refresh(const SynthParams& value, const juce::String& trackName)
    {
        loading = true;
        params = value;
        title = trackName;
        wave1.setSelectedId(params.wave + 1, juce::dontSendNotification);
        wave2.setSelectedId(params.wave2 + 1, juce::dontSendNotification);
        int patch = 0;
        for (std::size_t i = 0; i < synthPatches().size(); ++i)
            if (synthPatches()[i].params == params)
                patch = static_cast<int>(i) + 1;
        patchBox.setSelectedId(patch, juce::dontSendNotification);
        for (std::size_t i = 0; i < knobs.size(); ++i)
            knobs[i].slider->setValue(params.*layout()[i].field, juce::dontSendNotification);
        refreshLabels();
        loading = false;
        repaint();
    }

    void publish()
    {
        refreshLabels();
        int patch = 0;
        for (std::size_t i = 0; i < synthPatches().size(); ++i)
            if (synthPatches()[i].params == params)
                patch = static_cast<int>(i) + 1;
        patchBox.setSelectedId(patch, juce::dontSendNotification);
        repaint(previewArea());
        if (onChange)
            onChange(params);
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
            else if (suffix == "Hz" && value >= 1000.0f)
                text = juce::String(value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz";
            else if (suffix == "st" || suffix == "ct" || suffix == "oct")
                text = (value > 0.0f ? "+" : "") + juce::String(value, suffix == "oct" ? 1 : 0) + " " + suffix;
            else
                text = juce::String(value, value >= 100.0f ? 0 : 1) + " " + suffix;
            knobs[i].label->setText(juce::String(spec.name) + "\n" + text, juce::dontSendNotification);
        }
    }

    juce::Rectangle<int> previewArea() const { return { 24, 72, 286, 96 }; }

    void resized() override
    {
        patchBox.setBounds(getWidth() - 360, 16, 200, 28);
        close.setBounds(getWidth() - 100, 16, 76, 28);
        wave1.setBounds(330, 88, 120, 26);
        wave2.setBounds(330, 130, 120, 26);
        const auto& spec = layout();
        for (std::size_t i = 0; i < knobs.size(); ++i)
        {
            knobs[i].slider->setBounds(spec[i].x + 4, spec[i].y, 56, 56);
            knobs[i].label->setBounds(spec[i].x - 4, spec[i].y + 56, 72, 26);
        }
        hint.setBounds(330, 350, getWidth() - 354, 60);
    }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        ui::caption(g, "SYNTH ENGINE  /  " + title.toUpperCase(), { 24, 16, 460, 26 }, ui::text, 13.0f);
        ui::caption(g, "PATCH", { getWidth() - 420, 16, 56, 28 }, ui::muted, 9.0f);
        ui::caption(g, "WAVEFORM", { 24, 52, 200, 16 }, ui::cyan, 9.0f);
        ui::caption(g, "OSC 1", { 330, 72, 120, 16 }, ui::cyan, 9.0f);
        ui::caption(g, "OSC 2", { 330, 114, 120, 16 }, ui::cyan, 9.0f);
        juce::String last;
        const auto& spec = layout();
        for (const auto& knob : spec)
            if (last != knob.group)
            {
                last = knob.group;
                ui::caption(g, knob.group, { knob.x + 4, knob.y - 20, 220, 16 }, ui::cyan, 9.0f);
            }
        // One and a half cycles of the combined oscillators, through the drive.
        const auto area = previewArea().toFloat();
        g.setColour(ui::background);
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(ui::border);
        g.drawRoundedRectangle(area, 8.0f, 1.0f);
        g.drawHorizontalLine(static_cast<int>(area.getCentreY()), area.getX() + 6, area.getRight() - 6);
        auto shape = [](int wave, double t) {
            t -= std::floor(t);
            switch (wave)
            {
                case WaveSine: return std::sin(t * juce::MathConstants<double>::twoPi);
                case WaveTriangle: return 4.0 * std::abs(t - 0.5) - 1.0;
                case WaveSaw: return 0.8 * (2.0 * t - 1.0);
                case WavePulse: return 0.7 * ((t < 0.25 ? 1.0 : -1.0) + 0.5);
                case WaveNoise:
                {
                    // Fixed pseudo-random shape: a preview of "noisy", not the audio.
                    const auto cell = static_cast<std::uint32_t>(t * 48.0);
                    std::uint32_t h = cell * 2654435761u;
                    h ^= h >> 15;
                    return 0.5 * (static_cast<double>(h & 0xFFFF) / 32768.0 - 1.0);
                }
                default: return t < 0.5 ? 0.6 : -0.6;
            }
        };
        const double ratio = std::pow(2.0, (params.semis2 + params.detune2 / 100.0) / 12.0);
        const float driveGain = 1.0f + params.drive * 24.0f;
        std::vector<float> points;
        const int n = static_cast<int>(area.getWidth()) - 12;
        float maxAbs = 1.0e-6f;
        for (int i = 0; i <= n; ++i)
        {
            const double t = 1.5 * i / n;
            auto v = static_cast<float>(shape(params.wave, t) + params.mix2 * shape(params.wave2, t * ratio));
            if (params.drive > 0.0f)
                v = std::tanh(v * driveGain);
            points.push_back(v);
            maxAbs = std::max(maxAbs, std::abs(v));
        }
        juce::Path path;
        const float scale = (area.getHeight() * 0.5f - 10.0f) / std::max(1.0f, maxAbs);
        for (int i = 0; i <= n; ++i)
        {
            const float x = area.getX() + 6.0f + static_cast<float>(i);
            const float y = area.getCentreY() - points[static_cast<std::size_t>(i)] * scale;
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        g.setColour(ui::cyan);
        g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
    }

    struct KnobUi
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };
    SynthParams params;
    juce::String title;
    bool loading = false;
    std::vector<KnobUi> knobs;
    juce::ComboBox patchBox, wave1, wave2;
    juce::TextButton close;
    juce::Label hint;
    std::function<void(const SynthParams&)> onChange;
    std::function<void()> onDragStart, onDragEnd;
    std::function<void(int)> onPatch;
    std::function<void()> onClose;
};

void MainComponent::refreshSynthPanel()
{
    if (synthPanel == nullptr)
        return;
    const auto& track = project.tracks[static_cast<std::size_t>(selectedTrack)];
    synthPanel->refresh(track.synth, track.trackName());
}

juce::File MainComponent::favoritesFile()
{
    return audioSettingsFile().getSiblingFile("favorites.json");
}

void MainComponent::refreshInstrumentButton()
{
    const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    instrumentChoice.setText(describeTrackInstrument(project.tracks[sel]), juce::dontSendNotification);
}

void MainComponent::showInstrumentBrowser()
{
    const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    if (audioSelected || track.kind != TrackKind::Synth)
        return;
    auto browser = std::make_unique<InstrumentBrowser>(buildPickerEntries(listSampleLibrary(sampleLibraryDir())),
                                                       favorites, currentPickerKey(track));
    browser->onPick = [this](const PickerEntry& entry) { applyPickerEntry(entry); };
    browser->onFavoritesChanged = [this](const Favorites& updated) {
        favorites = updated;
        const auto saved = favorites.save(favoritesFile());
        if (saved.failed())
            status.setText("Could not save favorites: " + saved.getErrorMessage(), juce::dontSendNotification);
    };
    juce::Component::SafePointer<InstrumentBrowser> shown(browser.get());
    juce::CallOutBox::launchAsynchronously(std::move(browser), instrumentChoice.getScreenBounds(), this);
    juce::MessageManager::callAsync([shown] {
        if (shown != nullptr)
            shown->focusSearch();
    });
}

void MainComponent::applyPickerEntry(const PickerEntry& entry)
{
    switch (entry.kind)
    {
        case PickerEntry::Kind::Instrument: setTrackInstrument(entry.index); break;
        case PickerEntry::Kind::Patch: applySynthPatch(entry.index); break;
        case PickerEntry::Kind::Sample: useSample(entry.sample); break;
    }
}

void MainComponent::applySynthPatch(int patch)
{
    if (patch < 0 || patch >= static_cast<int>(synthPatches().size()))
        return;
    auto& track = project.tracks[static_cast<std::size_t>(selectedTrack)];
    if (track.kind != TrackKind::Synth)
        return;
    beginEdit();
    track.synth = synthPatches()[static_cast<std::size_t>(patch)].params;
    track.instrumentPreset = 0;
    if (isGeneratedTrackName(track.trackName()))
        track.setTrackName(synthPatches()[static_cast<std::size_t>(patch)].name);
    projectChanged();
    endEdit();
    refreshSynthPanel();
    repaint();
}

// Runs one AI request off the message thread. The job posts its own result
// back to the message thread (callAsync) when it finishes.
// Linux-sized worker stacks: macOS defaults secondary threads to 512 KB, and
// these jobs hold ProjectState (~250 KB) copies on the stack.
static constexpr size_t workerStackBytes = 16 * 1024 * 1024;

struct MainComponent::BackgroundWorker final : public juce::Thread
{
    explicit BackgroundWorker(std::function<void(const std::atomic<bool>*)> jobIn,
                              const char* name = "Sonora background job")
        : juce::Thread(name, workerStackBytes), job(std::move(jobIn))
    {
    }
    void run() override { job(&cancel); }
    std::function<void(const std::atomic<bool>*)> job;
    std::atomic<bool> cancel { false };
};

std::vector<juce::File> MainComponent::sampleSearchDirs() const
{
    std::vector<juce::File> dirs;
    if (projectFile != juce::File())
        dirs.push_back(mediaDirFor(projectFile));
    dirs.push_back(sessionDir());
    dirs.push_back(sampleLibraryDir());
    return dirs;
}

juce::String MainComponent::sampleSignature(int track) const
{
    const auto& state = project.tracks[static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1))];
    if (state.kind != TrackKind::Synth || !isSamplerInstrument(state.instrumentPreset) || state.samplerFile[0] == '\0')
        return {};
    // The resolved path and file stamp are part of the identity, so a file
    // that appears later (after a save gathers it) or changes reloads.
    const auto file = resolveSampleFile(state.samplerFileName(), sampleSearchDirs());
    return state.samplerFileName() + "|" + file.getFullPathName() + "|" + juce::String(file.getSize()) + "|"
        + juce::String(file.getLastModificationTime().toMilliseconds());
}

void MainComponent::refreshSampleData()
{
    if (sampleLoadWorker != nullptr)
        return;
    for (int track = 0; track < maxTracks; ++track)
    {
        const auto index = static_cast<std::size_t>(track);
        const auto signature = sampleSignature(track);
        if (signature == lastSampleSignature[index])
            continue;
        if (signature.isEmpty())
        {
            // Not a sampler track (any more): unload and free after the grace period.
            lastSampleSignature[index] = signature;
            sampleOverview[index] = {};
            auto* retired = engine.retireSampleData(track, nullptr);
            const auto* owned = sampleStorage[index].release();
            if (retired != nullptr)
                juce::Timer::callAfterDelay(600, [retired] { delete retired; });
            else
                delete owned;
            if (samplerPanel != nullptr && samplerPanel->isVisible() && track == selectedTrack)
                refreshSamplerPanel(false);
            continue;
        }
        const auto file = resolveSampleFile(project.tracks[index].samplerFileName(), sampleSearchDirs());
        auto safe = juce::Component::SafePointer<MainComponent>(this);
        auto job = [safe, track, file, signature](const std::atomic<bool>* cancel) {
            auto data = loadSampleData(file, cancel);
            SampleOverview overview;
            if (data != nullptr)
                overview = makeOverview(*data, 600);
            auto shared = std::make_shared<std::unique_ptr<SampleData>>(std::move(data));
            juce::MessageManager::callAsync([safe, track, shared, overview, signature]() mutable {
                if (safe != nullptr)
                    safe->sampleLoadFinished(track, std::move(*shared), std::move(overview), signature);
            });
        };
        sampleLoadWorker = std::make_unique<BackgroundWorker>(std::move(job), "Sonora sample loader");
        sampleLoadWorker->startThread();
        return; // one at a time; the timer comes back for the next stale track
    }
}

void MainComponent::sampleLoadFinished(int track, std::unique_ptr<SampleData> data, SampleOverview overview,
                                       const juce::String& signature)
{
    if (sampleLoadWorker != nullptr)
    {
        sampleLoadWorker->stopThread(2000);
        sampleLoadWorker.reset();
    }
    const auto index = static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1));
    if (signature != sampleSignature(track))
        return; // superseded; the timer relaunches for the current state
    lastSampleSignature[index] = signature;
    sampleOverview[index] = overview;
    // A missing or unreadable file installs "no sample": silence, never a stale sound.
    auto* retired = engine.retireSampleData(track, data.get());
    const auto* owned = sampleStorage[index].release();
    sampleStorage[index] = std::move(data);
    if (retired != nullptr)
        juce::Timer::callAfterDelay(600, [retired] { delete retired; });
    else
        delete owned;
    // A sound the user just chose adopts its own pitch as the root key. This
    // rides on the choice's undo step (the earlier state has the old root).
    if (adoptSampleRoot[index])
    {
        adoptSampleRoot[index] = false;
        if (overview.loaded && overview.detectedRoot >= 0
            && project.tracks[index].sampler.rootNote != overview.detectedRoot)
        {
            project.tracks[index].sampler.rootNote = overview.detectedRoot;
            projectChanged();
            status.setText("Root key set to " + juce::MidiMessage::getMidiNoteName(overview.detectedRoot, true, true, 4)
                               + " from the sample's pitch.",
                           juce::dontSendNotification);
        }
    }
    if (!overview.loaded && project.tracks[index].samplerFile[0] != '\0')
        status.setText("Could not load \"" + project.tracks[index].samplerFileName()
                           + "\". Is the file still in your sample library?",
                       juce::dontSendNotification);
    if (samplerPanel != nullptr && samplerPanel->isVisible() && track == selectedTrack)
        refreshSamplerPanel(false);
}

void MainComponent::refreshSamplerPanel(bool relist)
{
    if (samplerPanel == nullptr)
        return;
    const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    const auto& track = project.tracks[sel];
    if (relist)
    {
        const auto library = listSampleLibrary(sampleLibraryDir());
        samplerPanel->refresh(track.sampler, track.trackName(), track.samplerFileName(), sampleOverview[sel], &library);
    }
    else
        samplerPanel->refresh(track.sampler, track.trackName(), track.samplerFileName(), sampleOverview[sel]);
}

void MainComponent::toggleEditorPanel()
{
    if (audioSelected)
        return;
    const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    const bool sampler = isSamplerInstrument(track.instrumentPreset);
    juce::Component* panel = sampler ? static_cast<juce::Component*>(samplerPanel.get())
                                     : static_cast<juce::Component*>(synthPanel.get());
    if (panel == nullptr)
        return;
    if (panel->isVisible())
    {
        panel->setVisible(false);
        return;
    }
    for (juce::Component* other : std::initializer_list<juce::Component*> { kitPanel.get(), synthPanel.get(), samplerPanel.get() })
        if (other != nullptr)
            other->setVisible(false);
    if (sampler)
        refreshSamplerPanel(true);
    else
        refreshSynthPanel();
    panel->setVisible(true);
    panel->toFront(false);
}

void MainComponent::setTrackInstrument(int choice)
{
    auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    if (audioSelected || track.kind != TrackKind::Synth || !validInstrument(choice) || track.instrumentPreset == choice)
        return;
    beginEdit();
    if (isGeneratedTrackName(track.trackName()))
        track.setTrackName(instruments[static_cast<std::size_t>(choice)].name);
    track.instrumentPreset = choice;
    engine.keyboardState.allNotesOff(0);
    projectChanged();
    endEdit();
    repaint();
    // A Sampler with nothing loaded has nothing to play: open the editor so
    // the first step (add or pick a sound) is right there.
    if (isSamplerInstrument(choice) && track.samplerFile[0] == '\0')
        toggleEditorPanel();
}

void MainComponent::useSample(const juce::String& libraryName)
{
    auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    if (audioSelected || track.kind != TrackKind::Synth || libraryName.isEmpty()
        || !isSafeSampleName(libraryName.toRawUTF8()) || libraryName.length() >= samplerFileCapacity)
        return;
    const bool newFile = track.samplerFileName() != libraryName;
    beginEdit();
    track.instrumentPreset = samplerInstrument;
    track.setSamplerFileName(libraryName);
    if (newFile)
    {
        // Markers belong to the old file; the rest of the sound design stays.
        track.sampler.start = 0.0f;
        track.sampler.end = 1.0f;
        track.sampler.loopStart = 0.0f;
        track.sampler.loopEnd = 1.0f;
    }
    if (isGeneratedTrackName(track.trackName()))
        track.setTrackName(sampleDisplayName(libraryName).substring(0, 40));
    adoptSampleRoot[static_cast<std::size_t>(selectedTrack)] = newFile;
    engine.keyboardState.allNotesOff(0);
    projectChanged();
    endEdit();
    refreshSamplerPanel(false);
    repaint();
}

void MainComponent::addSoundsToSampler()
{
    if (dialogPending)
        return;
    dialogPending = true;
    chooser = std::make_unique<juce::FileChooser>("Add sounds to the sampler",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems
                             | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& selected) {
            if (safe == nullptr)
                return;
            safe->dialogPending = false;
            const auto library = sampleLibraryDir();
            juce::StringArray added;
            constexpr int maxFiles = 400;
            auto importOne = [&](const juce::File& file, const juce::File& into) {
                if (added.size() >= maxFiles)
                    return;
                const auto name = importSampleToLibrary(file, into);
                if (name.isNotEmpty())
                    added.add(into.getChildFile(name).getRelativePathFrom(library).replaceCharacter('\\', '/'));
            };
            for (const auto& picked : selected.getResults())
            {
                if (picked.isDirectory())
                {
                    // A folder keeps its name inside the library, so packs stay together.
                    const auto into = library.getChildFile(picked.getFileName().retainCharacters(
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()#").trim());
                    for (const auto& entry : juce::RangedDirectoryIterator(picked, true, "*", juce::File::findFiles))
                        if (isSampleFile(entry.getFile()))
                            importOne(entry.getFile(), into);
                }
                else if (picked.existsAsFile())
                    importOne(picked, library);
            }
            if (added.isEmpty())
            {
                safe->showError("No audio files were added. Use WAV, AIFF, FLAC, OGG or MP3 files.");
                return;
            }
            safe->status.setText("Added " + juce::String(added.size()) + (added.size() == 1 ? " sound" : " sounds")
                                     + " to the sample library.",
                                 juce::dontSendNotification);
            const auto& track = safe->project.tracks[static_cast<std::size_t>(safe->selectedTrack)];
            if (!safe->audioSelected && track.kind == TrackKind::Synth)
                safe->useSample(added[0]);
            safe->refreshSamplerPanel(true);
        });
}

void MainComponent::sendTakeToSampler(std::uint32_t takeId)
{
    const AudioTakeMeta* take = nullptr;
    for (int i = 0; i < project.takeCount; ++i)
        if (project.takes[static_cast<std::size_t>(i)].id == takeId)
            take = &project.takes[static_cast<std::size_t>(i)];
    if (take == nullptr || take->offline)
        return;
    const auto source = resolveTakeFile(*take);
    if (!source.existsAsFile())
    {
        showError("That take's audio file could not be found.");
        return;
    }
    // Stage under a readable name, then add it to the library like any sound.
    const auto staged = sessionDir().getNonexistentChildFile("Take " + juce::String(takeId), source.getFileExtension());
    if (!source.copyFileTo(staged))
    {
        showError("Could not copy the take.");
        return;
    }
    const auto name = importSampleToLibrary(staged);
    staged.deleteFile();
    if (name.isEmpty())
    {
        showError("Could not add the take to the sample library.");
        return;
    }
    // A new Sampler track keeps the existing instruments untouched.
    int free = -1;
    for (int track = 0; track < maxTracks && free < 0; ++track)
        if (project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::None)
            free = track;
    if (free >= 0)
    {
        addTrackOfKind(TrackKind::Synth);
        selectChannel(free);
    }
    else if (project.tracks[static_cast<std::size_t>(selectedTrack)].kind != TrackKind::Synth || audioSelected)
    {
        showError("The track list is full. Select an instrument track to replace its sound, or delete a track.");
        return;
    }
    useSample(name);
    status.setText("Take " + juce::String(takeId) + " is now a sampler instrument: play it on the keys.",
                   juce::dontSendNotification);
    toggleEditorPanel();
}

void MainComponent::detectSamplerRoot()
{
    const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    auto& track = project.tracks[sel];
    if (track.kind != TrackKind::Synth || !isSamplerInstrument(track.instrumentPreset))
        return;
    const int detected = sampleOverview[sel].detectedRoot;
    if (detected < 0)
    {
        status.setText("I could not hear a steady pitch in this sound. Set the root key by hand.",
                       juce::dontSendNotification);
        return;
    }
    beginEdit();
    track.sampler.rootNote = detected;
    projectChanged();
    endEdit();
    refreshSamplerPanel(false);
    status.setText("Root key set to " + juce::MidiMessage::getMidiNoteName(detected, true, true, 4) + ".",
                   juce::dontSendNotification);
}

bool MainComponent::sidebarOpen() const { return aiSidebar != nullptr && aiSidebar->isVisible(); }

void MainComponent::toggleAiSidebar()
{
    if (aiSidebar == nullptr)
        return;
    const bool opening = !aiSidebar->isVisible();
    if (opening)
    {
        claudeAvailable = ai::findClaudeExecutable() != juce::File();
        for (juce::Component* panel : std::initializer_list<juce::Component*> { kitPanel.get(), synthPanel.get(), samplerPanel.get() })
            if (panel != nullptr)
                panel->setVisible(false);
    }
    else if (dictationRecording.exchange(false))
    {
        dictationRecorder.stop();
        dictationFile.deleteFile();
        dictationFile = juce::File();
        aiSidebar->setVoiceRecording(false);
    }
    aiSidebar->setVisible(opening);
    demo.setToggleState(opening, juce::dontSendNotification);
    refreshAiSidebar();
    resized();
    repaint();
    if (opening)
    {
        aiSidebar->toFront(false);
        aiSidebar->focusInput();
    }
    else
        grabKeyboardFocus(); // keys must reach the main view again, not a hidden editor
}

void MainComponent::refreshAiSidebar()
{
    if (!sidebarOpen())
        return;
    const juce::String reason = !claudeAvailable
        ? "Claude Code was not found. Install it and run `claude` once in a terminal to sign in."
        : juce::String();
    const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    const auto& track = project.tracks[sel];
    int tracks = 0;
    for (const auto& state : project.tracks)
        tracks += state.kind != TrackKind::None ? 1 : 0;
    // The agent works on the whole project whatever view is showing; the
    // detail line just says what "this track" means right now.
    juce::String detail = juce::String(tracks) + (tracks == 1 ? " track" : " tracks") + "  /  "
        + juce::String(project.song.sections) + " parts  /  " + (project.songMode ? "Song view" : "Loop view");
    if (!audioSelected && track.kind != TrackKind::None)
        detail << "  /  selected: " << track.trackName();
    aiSidebar->setContext("Sonora agent", detail, AiSidebar::Mode::Agent, claudeAvailable, reason);
}

void MainComponent::startAiJob(std::function<void(const std::atomic<bool>*)> job)
{
    assistantWorker = std::make_unique<BackgroundWorker>(std::move(job));
    assistantStartedAt = juce::Time::getMillisecondCounter();
    aiSidebar->setBusy(true);
    aiSidebar->setStatus("Thinking...");
    assistantWorker->startThread();
}

void MainComponent::finishAiJob()
{
    transcribing = false;
    if (assistantWorker != nullptr)
    {
        assistantWorker->stopThread(2000);
        assistantWorker.reset();
    }
    assistantStartedAt = 0;
    if (aiSidebar != nullptr)
        aiSidebar->setBusy(false);
}

std::array<int, maxTracks> MainComponent::visibleLoopSlots() const
{
    std::array<int, maxTracks> slots {};
    const bool inPart = editPart >= 0 && editPart < project.song.sections;
    for (int t = 0; t < maxTracks; ++t)
    {
        const auto i = static_cast<std::size_t>(t);
        slots[i] = inPart ? project.song.slots[static_cast<std::size_t>(editPart)][i]
                          : (project.tracks[i].kind == TrackKind::Drums ? trackDrumSlot[i] : trackMelodySlot[i]);
    }
    return slots;
}

void MainComponent::sendToAssistant(const juce::String& text)
{
    if (assistantWorker != nullptr || text.trim().isEmpty())
        return;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    ai::AgentRequest request;
    request.message = text;
    request.project = project;
    request.selectedTrack = selectedTrack;
    request.loopSlot = visibleLoopSlots();
    request.editPart = editPart;
    request.songView = project.songMode;
    request.playing = engine.isPlaying();
    request.library = listSampleLibrary(sampleLibraryDir());
    request.history = chatHistory;
    chatHistory.push_back({ true, text });
    aiSidebar->addMessage({ AiSidebar::Message::Role::User, text });
    // The actions name tracks by position. If the track list changes while
    // Claude works, they would land on the wrong tracks: remember who was where.
    std::array<juce::uint32, maxTracks> ids {};
    for (int t = 0; t < maxTracks; ++t)
        ids[static_cast<std::size_t>(t)] = project.tracks[static_cast<std::size_t>(t)].id;
    startAiJob([safe, request, ids](const std::atomic<bool>* cancel) {
        auto result = ai::runAgent(request, cancel);
        juce::MessageManager::callAsync([safe, result, ids] {
            if (safe != nullptr)
                safe->agentFinished(result, ids);
        });
    });
}

void MainComponent::agentFinished(const ai::AgentResult& result, const std::array<juce::uint32, maxTracks>& ids)
{
    finishAiJob();
    if (aiSidebar == nullptr)
        return;
    using Role = AiSidebar::Message::Role;
    if (!result.ok())
    {
        aiSidebar->addMessage({ result.error == "Cancelled." ? Role::Info : Role::Error, result.error });
        return;
    }
    if (result.reply.isNotEmpty())
        aiSidebar->addMessage({ Role::Assistant, result.reply });
    juce::String historyText = result.reply;
    if (!result.actions.empty())
    {
        for (int t = 0; t < maxTracks; ++t)
            if (project.tracks[static_cast<std::size_t>(t)].id != ids[static_cast<std::size_t>(t)])
            {
                aiSidebar->addMessage({ Role::Error, "The track list changed while I was working, so nothing was applied. "
                                                     "Ask again for the current tracks." });
                chatHistory.push_back({ false, historyText + " [nothing applied: tracks changed]" });
                return;
            }
        agent::Context context;
        context.selectedTrack = selectedTrack;
        context.loopSlot = visibleLoopSlots();
        context.library = listSampleLibrary(sampleLibraryDir());
        std::array<TrackKind, maxTracks> kindsBefore {};
        for (int t = 0; t < maxTracks; ++t)
            kindsBefore[static_cast<std::size_t>(t)] = project.tracks[static_cast<std::size_t>(t)].kind;
        // One undo step for everything the agent did.
        beginEdit();
        const auto report = agent::applyActions(project, result.actions, context);
        if (report.changed)
        {
            projectChanged();
            // Show a newly added track unless the agent chose what to show.
            bool selects = false;
            for (const auto& effect : report.effects)
                selects = selects || effect.kind == agent::UiEffect::Kind::SelectTrack;
            if (!selects)
                for (int t = 0; t < maxTracks; ++t)
                    if (kindsBefore[static_cast<std::size_t>(t)] == TrackKind::None
                        && project.tracks[static_cast<std::size_t>(t)].kind != TrackKind::None)
                    {
                        selectChannel(t);
                        break;
                    }
            selectChannel(audioSelected ? -1 : selectedTrack);
            resized();
            repaint();
        }
        endEdit();
        runAgentEffects(report.effects);
        // What happened, in the producer's terms.
        juce::StringArray lines = report.done;
        juce::String summary;
        if (!lines.isEmpty())
        {
            const int shown = std::min(lines.size(), 14);
            summary << "Applied " << lines.size() << (lines.size() == 1 ? " change" : " changes") << ":\n";
            for (int i = 0; i < shown; ++i)
                summary << "- " << lines[i] << "\n";
            if (lines.size() > shown)
                summary << "- ...and " << (lines.size() - shown) << " more.\n";
            if (report.changed)
                summary << "Undo (Ctrl+Z) takes all of it back in one step.";
        }
        juce::StringArray skipped = report.failed;
        skipped.addArray(result.unreadable);
        if (!skipped.isEmpty())
        {
            summary << (summary.isEmpty() ? "" : "\n") << "Skipped " << skipped.size()
                    << (skipped.size() == 1 ? " action" : " actions") << ":\n";
            for (int i = 0; i < std::min(skipped.size(), 8); ++i)
                summary << "- " << skipped[i] << "\n";
        }
        if (summary.isNotEmpty())
            aiSidebar->addMessage({ report.failed.isEmpty() && result.unreadable.isEmpty() ? Role::Info : Role::Error,
                                    summary.trimEnd() });
        if (!lines.isEmpty())
        {
            status.setText("AI agent: " + lines[0] + (lines.size() > 1 ? " (+" + juce::String(lines.size() - 1) + " more)" : juce::String()),
                           juce::dontSendNotification);
            historyText += " [" + juce::String(lines.size()) + " changes applied"
                + (skipped.isEmpty() ? juce::String() : ", " + juce::String(skipped.size()) + " skipped") + "]";
        }
        else if (!skipped.isEmpty())
            historyText += " [nothing applied: " + juce::String(skipped.size()) + " skipped]";
    }
    chatHistory.push_back({ false, historyText });
}

// App-level requests ride the same buttons and dialogs a person would use, so
// existing confirmations (discarding unsaved work) still apply.
void MainComponent::runAgentEffects(const std::vector<agent::UiEffect>& effects)
{
    using K = agent::UiEffect::Kind;
    for (const auto& effect : effects)
    {
        switch (effect.kind)
        {
            case K::Play:
                if (effect.number >= 0)
                {
                    songStartPart = std::clamp(effect.number, 0, project.song.sections - 1);
                    engine.setSongStartSection(songStartPart);
                    if (!project.songMode)
                        setSongView(true);
                    engine.stop();
                }
                engine.setPlaying(true);
                break;
            case K::Stop: stop.onClick(); break;
            case K::Panic: panic.onClick(); break;
            case K::SetView:
                if (effect.text == "loop")
                {
                    setMixerView(false);
                    setSongView(false);
                }
                else if (effect.text == "song")
                    setSongView(true);
                else if (effect.text == "mixer")
                    setMixerView(true);
                else if (effect.text == "automation")
                {
                    if (!automationVisible)
                        autoButton.onClick();
                }
                else if (effect.text == "soundeditor")
                {
                    setMixerView(false);
                    setSongView(false);
                    const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
                    const bool editorOpen = (samplerPanel != nullptr && samplerPanel->isVisible())
                        || (synthPanel != nullptr && synthPanel->isVisible());
                    if (!editorOpen && track.kind == TrackKind::Synth)
                        toggleEditorPanel();
                }
                else if (effect.text == "kiteditor")
                {
                    setMixerView(false);
                    setSongView(false);
                    if (kitPanel != nullptr && !kitPanel->isVisible() && project.tracks[static_cast<std::size_t>(drumEditTrack())].kind == TrackKind::Drums)
                        kitButton.onClick();
                }
                break;
            case K::SelectTrack: selectChannel(effect.number); break;
            case K::EditSection: handleArrangementAction({ ArrangementAction::Kind::EditPart, effect.number }); break;
            case K::Save: save.onClick(); break;
            case K::Export: exportButton.onClick(); break;
            case K::Undo: undoEdit(); break;
            case K::Redo: redoEdit(); break;
            case K::NewProject: newProject.onClick(); break;
            case K::OpenProject: open.onClick(); break;
            case K::ToggleRecord: toggleRecord(); break;
            case K::TakeToSampler: sendTakeToSampler(static_cast<std::uint32_t>(effect.number)); break;
            case K::DeleteTake: deleteTake(static_cast<std::uint32_t>(effect.number)); break;
        }
    }
}

void MainComponent::refreshKitPanel()
{
    if (kitPanel == nullptr || !kitPanel->isVisible())
        return;
    const juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    kitPanel->refresh(project, drumEditTrack(), media, listKitPresets(kitsDir()), lastPresetName);
}

void MainComponent::loadPresetSelection(int id)
{
    if (id >= 1 && id <= numKitVariants)
    {
        beginEdit();
        project.tracks[static_cast<std::size_t>(drumEditTrack())].kitVariant = id - 1;
        lastPresetName = kitVariantName(id - 1);
        projectChanged();
        endEdit();
        refreshKitPanel();
        return;
    }
    if (id >= 100)
    {
        const auto presets = listKitPresets(kitsDir());
        const auto index = static_cast<std::size_t>(id - 100);
        if (index >= presets.size())
            return;
        KitPreset preset;
        const auto result = loadKitPreset(kitsDir(), presets[index], preset);
        if (result.failed())
        {
            showError(result.getErrorMessage());
            return;
        }
        beginEdit();
        project.tracks[static_cast<std::size_t>(drumEditTrack())].kitVariant = preset.variant;
        for (int pad = 0; pad < drumPads; ++pad)
        {
            // Stage preset samples locally; the next save collects them.
            const auto& file = preset.files[static_cast<std::size_t>(pad)];
            if (file.isEmpty())
            {
                setPadSampleName(project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)], {});
                continue;
            }
            const auto source = kitsDir().getChildFile(presets[index]).getChildFile(file);
            const auto stem = source.getFileNameWithoutExtension();
            auto target = sessionDir().getNonexistentChildFile("kit-" + stem, source.getFileExtension());
            if (source.copyFileTo(target))
                setPadSampleName(project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)], target.getFileName());
        }
        lastPresetName = preset.name;
        if (kitPanel != nullptr)
            kitPanel->setPresetName(preset.name);
        projectChanged();
        endEdit();
        refreshKitPanel();
    }
}

void MainComponent::saveKitAsPreset()
{
    if (kitPanel == nullptr)
        return;
    const auto name = kitPanel->getPresetName();
    std::array<juce::String, drumPads> files {};
    for (int pad = 0; pad < drumPads; ++pad)
        files[static_cast<std::size_t>(pad)] = padSampleName(project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)]);
    const juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    const auto result = saveKitPreset(kitsDir(), name, project.tracks[static_cast<std::size_t>(drumEditTrack())].kitVariant, files,
        [&](const juce::String& file) {
            auto found = media.getChildFile(file);
            return found.existsAsFile() ? found : sessionDir().getChildFile(file);
        });
    if (result.failed())
    {
        showError(result.getErrorMessage());
        return;
    }
    lastPresetName = sanitizePresetName(name);
    refreshKitPanel();
}

void MainComponent::deleteSelectedPreset()
{
    if (kitPanel == nullptr)
        return;
    const int id = kitPanel->getSelectedPresetId();
    if (id < 100)
    {
        showError("Factory kits cannot be deleted.");
        return;
    }
    const auto presets = listKitPresets(kitsDir());
    const auto index = static_cast<std::size_t>(id - 100);
    if (index >= presets.size())
        return;
    const auto result = deleteKitPreset(kitsDir(), presets[index]);
    if (result.failed())
    {
        showError(result.getErrorMessage());
        return;
    }
    if (lastPresetName == presets[index])
        lastPresetName.clear();
    refreshKitPanel();
}

void MainComponent::loadPadSample(int pad)
{
    if (pad < 0 || pad >= drumPads || dialogPending)
        return;
    dialogPending = true;
    chooser = std::make_unique<juce::FileChooser>("Load drum sample",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent>(this), pad](const juce::FileChooser& selected) {
            if (safe == nullptr)
                return;
            safe->dialogPending = false;
            const auto file = selected.getResult();
            if (file == juce::File() || !file.existsAsFile())
                return;
            // Stage in the session folder now; collectSamples() gathers the
            // kit into the media folder on the next project save.
            auto target = sessionDir().getNonexistentChildFile(
                "pad-" + juce::String(pad) + "-" + file.getFileNameWithoutExtension(), file.getFileExtension());
            if (!file.copyFileTo(target))
            {
                safe->showError("Could not stage " + file.getFileName() + ".");
                return;
            }
            safe->beginEdit();
            setPadSampleName(safe->project.tracks[static_cast<std::size_t>(safe->drumEditTrack())].padSamples[static_cast<std::size_t>(pad)], target.getFileName());
            safe->lastPresetName.clear();
            safe->projectChanged();
            safe->endEdit();
            safe->refreshKitPanel();
        });
}

void MainComponent::clearPadSample(int pad)
{
    if (pad < 0 || pad >= drumPads)
        return;
    beginEdit();
    setPadSampleName(project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)], {});
    lastPresetName.clear();
    projectChanged();
    endEdit();
    refreshKitPanel();
}

void MainComponent::collectSamples(const juce::File& destination)
{
    const auto media = mediaDirFor(destination);
    media.createDirectory();
    // Sampler sounds come from the shared library; a copy travels with the
    // project (same relative name, so references stay valid) so it opens
    // anywhere. Media is searched first, so the project copy wins afterwards.
    for (const auto& track : project.tracks)
    {
        if (track.kind != TrackKind::Synth || !isSamplerInstrument(track.instrumentPreset)
            || track.samplerFile[0] == '\0')
            continue;
        const auto name = track.samplerFileName();
        const auto target = media.getChildFile(name);
        if (target.existsAsFile())
            continue;
        const auto source = resolveSampleFile(name, { sessionDir(), sampleLibraryDir() });
        if (source.existsAsFile() && target.getParentDirectory().createDirectory().wasOk())
            source.copyFileTo(target);
    }
    beginEdit();
    for (int pad = 0; pad < drumPads; ++pad)
    {
        auto& slot = project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)];
        const auto name = padSampleName(slot);
        if (name.isEmpty() || media.getChildFile(name).existsAsFile())
            continue;
        auto source = sessionDir().getChildFile(name);
        if (!source.existsAsFile())
            source = media.getChildFile(name);
        if (!source.existsAsFile() || source.getParentDirectory() == media)
            continue; // missing files fall back to the starter at load
        auto target = media.getNonexistentChildFile("pad-" + juce::String(pad), source.getFileExtension());
        if (source.copyFileTo(target))
            setPadSampleName(slot, target.getFileName());
    }
    projectChanged();
    endEdit();
}

juce::String MainComponent::bankSignature(int track) const
{
    const auto t = static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1));
    juce::String signature = juce::String(currentRate(), 0) + "|"
        + juce::String(project.tracks[t].kitVariant) + "|";
    for (int pad = 0; pad < drumPads; ++pad)
        signature += padSampleName(project.tracks[t].padSamples[static_cast<std::size_t>(pad)]) + ";";
    return signature;
}

void MainComponent::refreshPadBank()
{
    // Sample files load on a worker for the same reason as takes: decoding
    // and resampling on the message thread froze the UI.
    const int track = drumEditTrack();
    const auto trackIndex = static_cast<std::size_t>(track);
    if (bankLoadWorker != nullptr || bankSignature(track) == lastBankSignature[trackIndex])
        return;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    const juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    const double rate = currentRate();
    const int variant = project.tracks[trackIndex].kitVariant;
    std::array<juce::String, drumPads> files {};
    for (int pad = 0; pad < drumPads; ++pad)
        files[static_cast<std::size_t>(pad)] = padSampleName(project.tracks[trackIndex].padSamples[static_cast<std::size_t>(pad)]);
    const auto signature = bankSignature(track);
    auto job = [safe, track, files, media, rate, variant, signature](const std::atomic<bool>* cancel) {
        auto bank = loadSampleBank(files, media, rate, variant,
                                   [cancel](double) { return cancel == nullptr || !cancel->load(); });
        auto shared = std::make_shared<std::unique_ptr<SampleBank>>(std::move(bank));
        juce::MessageManager::callAsync([safe, track, shared, signature]() {
            if (safe != nullptr)
                safe->bankLoadFinished(track, std::move(*shared), signature);
        });
    };
    bankLoadWorker = std::make_unique<BackgroundWorker>(std::move(job), "Sonora bank loader");
    bankLoadWorker->startThread();
}

void MainComponent::bankLoadFinished(int track, std::unique_ptr<SampleBank> bank, const juce::String& signature)
{
    if (bankLoadWorker != nullptr)
    {
        bankLoadWorker->stopThread(2000);
        bankLoadWorker.reset();
    }
    const auto trackIndex = static_cast<std::size_t>(std::clamp(track, 0, maxTracks - 1));
    if (bank == nullptr || signature != bankSignature(track))
        return; // superseded or failed; the timer relaunches for current state
    lastBankSignature[trackIndex] = signature;
    auto* retired = engine.retirePadBank(static_cast<int>(trackIndex), bank.get());
    // release() transfers ownership of the old bank to the grace-period
    // callback. Assignment would destroy it before the audio thread lets go.
    const auto* owned = bankStorage[trackIndex].release();
    bankStorage[trackIndex] = std::move(bank);
    if (retired != nullptr)
        juce::Timer::callAfterDelay(600, [retired] { delete retired; });
    else
        delete owned;
}


struct MainComponent::AudioView final : public juce::Component
{
    // Miniature lane overview for comping: peaks for one take, brighter when
    // selected, cyan-edged while soloed.
    struct WaveStrip : public juce::Component
    {
        void setWave(std::vector<float> values, bool isSelected, bool isSolo)
        {
            peaks = std::move(values);
            selected = isSelected;
            solo = isSolo;
            repaint();
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(ui::raised);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
            if (peaks.empty())
                return;
            g.setColour((selected ? ui::text : ui::blue).withAlpha(selected ? 0.85f : 0.55f));
            const int buckets = static_cast<int>(peaks.size());
            for (int x = 0; x < getWidth(); ++x)
            {
                const auto peak = peaks[static_cast<std::size_t>(x * buckets / getWidth())];
                const auto h = std::max(1.0f, peak * (getHeight() / 2 - 1));
                const auto cy = getHeight() / 2;
                g.fillRect(x, cy - static_cast<int>(h), 1, static_cast<int>(h) * 2);
            }
            if (solo)
            {
                g.setColour(ui::cyan);
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f, 0.5f), 3.0f, 1.5f);
            }
        }
        std::vector<float> peaks;
        bool selected = false, solo = false;
    };

    AudioView(std::function<void(int)> inputModeCb, std::function<void(bool)> monitorCb,
              std::function<void(std::uint32_t)> muteCb, std::function<void(std::uint32_t)> deleteCb,
              std::function<void(std::uint32_t)> selectCb, std::function<void()> pitchChangedCb,
              std::function<void()> analyzeCb, std::function<void()> applyCb,
              std::function<void(std::uint32_t)> soloCb, std::function<void(std::uint32_t)> keepCb,
              std::function<void(std::uint32_t, float)> stretchCb, std::function<void()> stretchBeginCb,
              std::function<void()> stretchEndCb, std::function<void(std::uint32_t)> toSamplerCb)
        : onInputMode(std::move(inputModeCb)), onMonitor(std::move(monitorCb)),
          onMuteTake(std::move(muteCb)), onDeleteTake(std::move(deleteCb)),
          onSelectTake(std::move(selectCb)), onPitchChanged(std::move(pitchChangedCb)),
          onAnalyze(std::move(analyzeCb)), onApply(std::move(applyCb)),
          onSoloTake(std::move(soloCb)), onKeepTake(std::move(keepCb)),
          onStretchTake(std::move(stretchCb)), onStretchBegin(std::move(stretchBeginCb)),
          onStretchEnd(std::move(stretchEndCb)), onToSampler(std::move(toSamplerCb))
    {
        setWantsKeyboardFocus(true);
        inputMode.addItem("Input 1 (mono)", 1);
        inputMode.addItem("Input 2 (mono)", 2);
        inputMode.addItem("Stereo in", 3);
        inputMode.setSelectedId(1, juce::dontSendNotification);
        inputMode.setColour(juce::ComboBox::backgroundColourId, ui::raised);
        inputMode.setColour(juce::ComboBox::textColourId, ui::text);
        inputMode.setColour(juce::ComboBox::outlineColourId, ui::border);
        inputMode.setWantsKeyboardFocus(false);
        addAndMakeVisible(inputMode);
        inputMode.onChange = [this] { if (onInputMode) onInputMode(inputMode.getSelectedId() - 1); };
        monitor.setButtonText("Monitor");
        monitor.setClickingTogglesState(true);
        monitor.setWantsKeyboardFocus(false);
        monitor.setColour(juce::TextButton::buttonOnColourId, ui::blue);
        addAndMakeVisible(monitor);
        monitor.onClick = [this] { if (onMonitor) onMonitor(monitor.getToggleState()); };
        addAndMakeVisible(status);
        status.setFont(ui::font(11.0f));
        status.setColour(juce::Label::textColourId, ui::muted);
        addAndMakeVisible(takeHint);
        takeHint.setFont(ui::font(11.0f));
        takeHint.setColour(juce::Label::textColourId, ui::muted);
        const char* keys[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i)
            pitchKey.addItem(keys[i], i + 1);
        pitchKey.setSelectedId(1, juce::dontSendNotification);
        pitchScale.addItem("Chromatic", 1);
        pitchScale.addItem("Major", 2);
        pitchScale.addItem("Minor", 3);
        pitchScale.setSelectedId(1, juce::dontSendNotification);
        for (auto* box : { &pitchKey, &pitchScale })
        {
            addAndMakeVisible(box);
            box->setColour(juce::ComboBox::backgroundColourId, ui::raised);
            box->setColour(juce::ComboBox::textColourId, ui::text);
            box->setColour(juce::ComboBox::outlineColourId, ui::border);
            box->setWantsKeyboardFocus(false);
            box->onChange = [this] { if (onPitchChanged) onPitchChanged(); };
        }
        pitchAmount.setRange(0.0, 100.0, 1.0);
        pitchAmount.setValue(100.0, juce::dontSendNotification);
        pitchSpeed.setRange(5.0, 300.0, 1.0);
        pitchSpeed.setSkewFactorFromMidPoint(40.0);
        pitchSpeed.setValue(40.0, juce::dontSendNotification);
        for (auto* slider : { &pitchAmount, &pitchSpeed })
        {
            addAndMakeVisible(slider);
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
            slider->setColour(juce::Slider::thumbColourId, ui::violet);
            slider->setColour(juce::Slider::textBoxTextColourId, ui::text);
            slider->setWantsKeyboardFocus(false);
            slider->onValueChange = [this] { if (onPitchChanged) onPitchChanged(); };
        }
        pitchAmount.setTextValueSuffix(" %");
        pitchSpeed.setTextValueSuffix(" ms");
        stretchLabel.setText("Stretch", juce::dontSendNotification);
        stretchLabel.setFont(ui::font(12.0f, true));
        stretchLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(stretchLabel);
        stretch.setRange(50.0, 200.0, 1.0);
        stretch.setValue(100.0, juce::dontSendNotification);
        stretch.setSliderStyle(juce::Slider::LinearHorizontal);
        stretch.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
        stretch.setColour(juce::Slider::thumbColourId, ui::cyan);
        stretch.setColour(juce::Slider::textBoxTextColourId, ui::text);
        stretch.setWantsKeyboardFocus(false);
        stretch.setTooltip("Time-stretch the selected take without changing its pitch (Rubber Band).");
        addAndMakeVisible(stretch);
        stretch.onValueChange = [this] {
            if (onStretchTake) onStretchTake(stretchTake, static_cast<float>(stretch.getValue()) / 100.0f);
        };
        stretch.onDragStart = [this] { if (onStretchBegin) onStretchBegin(); };
        stretch.onDragEnd = [this] { if (onStretchEnd) onStretchEnd(); };
        addAndMakeVisible(stretchValue);
        stretchValue.setFont(ui::font(11.0f));
        stretchValue.setColour(juce::Label::textColourId, ui::muted);
        addAndMakeVisible(toSampler);
        toSampler.setWantsKeyboardFocus(false);
        toSampler.setTooltip("Turn this take into a sampler instrument on a new track: play it on the keys, "
                             "loop it, trim it, tune it.");
        toSampler.onClick = [this] { if (onToSampler && stretchTake != 0) onToSampler(stretchTake); };
        analyze.setButtonText("Analyze");
        apply.setButtonText("Tune take");
        apply.setColour(juce::TextButton::buttonOnColourId, ui::violet);
        for (auto* button : { &analyze, &apply })
        {
            addAndMakeVisible(button);
            button->setWantsKeyboardFocus(false);
        }
        analyze.onClick = [this] { if (onAnalyze) onAnalyze(); };
        apply.onClick = [this] { if (onApply) onApply(); };
        addAndMakeVisible(pitchStatus);
        pitchStatus.setFont(ui::font(10.0f));
        pitchStatus.setColour(juce::Label::textColourId, ui::muted);
    }

    int getPitchKey() const { return pitchKey.getSelectedId() - 1; }
    int getPitchScale() const { return pitchScale.getSelectedId() - 1; }
    float getPitchAmount() const { return static_cast<float>(pitchAmount.getValue() / 100.0); }
    float getPitchSpeed() const { return static_cast<float>(pitchSpeed.getValue()); }

    void refresh(const ProjectState& project, int numInputs, int latencySamples,
                 std::uint32_t selected, bool isRecording, const juce::String& deviceName,
                 const juce::String& extra)
    {
        juce::String signature;
        for (int i = 0; i < project.takeCount; ++i)
        {
            const auto& take = project.takes[static_cast<std::size_t>(i)];
            signature += juce::String(take.id) + (take.mute ? "m" : "") + (take.solo ? "s" : "")
                + (take.offline ? "x" : "") + juce::String(take.startTick) + ";";
        }
        signature += juce::String(selected) + (isRecording ? "R" : "");
        if (signature != lastSignature)
        {
            lastSignature = signature;
            takeRows.clear();
            for (int i = 0; i < project.takeCount; ++i)
            {
                const auto& take = project.takes[static_cast<std::size_t>(i)];
                Row row;
                row.id = take.id;
                row.isSelected = take.id == selected;
                row.isSolo = take.solo;
                row.name = std::make_unique<juce::Label>();
                row.name->setFont(ui::font(12.0f, true));
                row.name->setColour(juce::Label::textColourId,
                    take.offline ? ui::danger : take.id == selected ? ui::cyan : ui::text);
                const auto bars = juce::String(take.startTick / 3840 + 1) + "." + juce::String((take.startTick / 960) % 4 + 1);
                const auto seconds = juce::String(take.frames * take.stretch / 48000.0, 1);
                row.name->setText("Take " + juce::String(take.id) + "   @ bar " + bars + "   " + seconds + " s   "
                    + (take.channels == 2 ? "stereo" : "mono") + (take.offline ? "   (file missing)" : "")
                    + (take.solo ? "   SOLO" : ""),
                    juce::dontSendNotification);
                addAndMakeVisible(row.name.get());
                row.solo = std::make_unique<juce::TextButton>("Solo");
                row.solo->setClickingTogglesState(true);
                row.solo->setToggleState(take.solo, juce::dontSendNotification);
                row.solo->setWantsKeyboardFocus(false);
                row.solo->setColour(juce::TextButton::buttonOnColourId, ui::cyan);
                row.solo->setTooltip("Solo this take in SONG mode. Combines with other solos; wins over mute.");
                addAndMakeVisible(row.solo.get());
                row.solo->onClick = [this, id = take.id] { if (onSoloTake) onSoloTake(id); };
                row.keep = std::make_unique<juce::TextButton>("Keep");
                row.keep->setWantsKeyboardFocus(false);
                row.keep->setTooltip("Comp choice: keep only this take, mute the rest (undoable).");
                addAndMakeVisible(row.keep.get());
                row.keep->onClick = [this, id = take.id] { if (onKeepTake) onKeepTake(id); };
                row.mute = std::make_unique<juce::TextButton>(take.mute ? "Muted" : "Mute");
                row.mute->setClickingTogglesState(true);
                row.mute->setToggleState(take.mute, juce::dontSendNotification);
                row.mute->setWantsKeyboardFocus(false);
                row.mute->setColour(juce::TextButton::buttonOnColourId, ui::danger);
                addAndMakeVisible(row.mute.get());
                row.mute->onClick = [this, id = take.id] { if (onMuteTake) onMuteTake(id); };
                row.remove = std::make_unique<juce::TextButton>("Delete");
                row.remove->setWantsKeyboardFocus(false);
                row.remove->setColour(juce::TextButton::buttonOnColourId, ui::danger);
                addAndMakeVisible(row.remove.get());
                row.remove->onClick = [this, id = take.id] { if (onDeleteTake) onDeleteTake(id); };
                row.wave = std::make_unique<WaveStrip>();
                addAndMakeVisible(row.wave.get());
                if (const auto* found = findLane(row.id))
                    row.wave->setWave(*found, row.isSelected, row.isSolo);
                takeRows.push_back(std::move(row));
            }
            applyTakeWaves();
            resized();
        }
        juce::String info;
        if (numInputs <= 0)
            info = "No inputs open on " + (deviceName.isEmpty() ? juce::String("the current device")
                : "\"" + deviceName + "\"") + " — press REC or enable Monitor to open them.";
        else
            info = "\"" + deviceName + "\": " + juce::String(numInputs)
                + (numInputs == 1 ? " input" : " inputs")
                + "   /   input latency " + juce::String(latencySamples) + " samples (auto-compensated)";
        if (extra.isNotEmpty())
            info += "   /   " + extra;
        status.setText(info, juce::dontSendNotification);
        // Stretch control follows the selected take.
        stretchTake = 0;
        float stretchRatio = 1.0f;
        int stretchFrames = 0;
        for (int i = 0; i < project.takeCount; ++i)
        {
            const auto& take = project.takes[static_cast<std::size_t>(i)];
            if (take.id == selected)
            {
                stretchTake = take.id;
                stretchRatio = take.stretch;
                stretchFrames = take.frames;
                break;
            }
        }
        stretch.setValue(stretchRatio * 100.0, juce::dontSendNotification);
        stretch.setEnabled(stretchTake != 0);
        toSampler.setEnabled(stretchTake != 0);
        stretchValue.setText(stretchTake == 0 ? juce::String("--")
                             : "x" + juce::String(stretchRatio, 2) + "  ("
                                 + juce::String(stretchFrames * stretchRatio / 48000.0, 1) + " s)",
                             juce::dontSendNotification);
        takeHint.setText(project.takeCount == 0
            ? (isRecording ? "Recording... press Stop to finish the take."
                           : "Press REC to record from the song start. Takes play back in SONG mode.")
            : "Click a take to inspect it. Solo auditions lanes (wins over mute); Keep mutes the rest. Delete key removes the selected take.",
            juce::dontSendNotification);
        repaint();
    }

    void setWave(std::vector<float> peaks, int frames, std::uint32_t takeId)
    {
        wavePeaks = std::move(peaks);
        waveFrames = frames;
        waveTake = takeId;
        repaint(waveArea());
    }

    void setPitchDisplay(std::vector<float> detectedHz, std::vector<float> targetHz,
                         int hopSamples, double rate)
    {
        pitchDetected = std::move(detectedHz);
        pitchTarget = std::move(targetHz);
        pitchHop = hopSamples;
        pitchRate = rate;
        pitchStatus.setText(!pitchDetected.empty() ? "VIEW" : "--", juce::dontSendNotification);
        repaint(waveArea());
    }

    void setInputLevel(float level)
    {
        if (std::abs(level - inputLevel) < 0.004f)
            return;
        inputLevel = level;
        repaint(meterArea());
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        // Clicking a row (label or lane strip) selects its take.
        for (const auto& row : takeRows)
        {
            const bool onLabel = row.name != nullptr && row.name->getBounds().contains(event.getPosition());
            const bool onStrip = row.wave != nullptr && row.wave->getBounds().contains(event.getPosition());
            if (onLabel || onStrip)
            {
                if (onSelectTake)
                    onSelectTake(row.id);
                return;
            }
        }
    }

    juce::Rectangle<int> waveArea() const
    {
        return { 12, waveTop, getWidth() - 24, std::max(70, getHeight() - waveTop - 8) };
    }

    juce::Rectangle<int> meterArea() const
    {
        return { getWidth() - 40, 48, 24, takeRows.empty() ? 60 : static_cast<int>(takeRows.size()) * 50 + 24 };
    }

    void resized() override
    {
        inputMode.setBounds(12, 10, 170, 30);
        monitor.setBounds(192, 10, 90, 30);
        status.setBounds(292, 10, getWidth() - 304, 30);
        int y = 52;
        for (auto& row : takeRows)
        {
            row.name->setBounds(12, y, getWidth() - 352, 24);
            row.solo->setBounds(getWidth() - 328, y, 64, 24);
            row.keep->setBounds(getWidth() - 258, y, 60, 24);
            row.mute->setBounds(getWidth() - 192, y, 76, 24);
            row.remove->setBounds(getWidth() - 110, y, 82, 24);
            row.wave->setBounds(12, y + 26, getWidth() - 40, 20);
            y += 50;
        }
        takeHint.setBounds(12, y + 2, getWidth() - 200, 22);
        y += 26;
        pitchKey.setBounds(12, y, 64, 26);
        pitchScale.setBounds(84, y, 108, 26);
        pitchAmount.setBounds(200, y, 150, 26);
        pitchSpeed.setBounds(358, y, 150, 26);
        analyze.setBounds(getWidth() - 268, y, 76, 26);
        apply.setBounds(getWidth() - 184, y, 92, 26);
        pitchStatus.setBounds(getWidth() - 84, y, 72, 26);
        y += 30;
        stretchLabel.setBounds(12, y, 64, 26);
        stretch.setBounds(84, y, 200, 26);
        stretchValue.setBounds(292, y, 220, 26);
        toSampler.setBounds(getWidth() - 184, y, 160, 26);
        waveTop = y + 32;
    }

    int waveTop = 200;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c111b));
        ui::caption(g, "INPUT", { 12, 44, 120, 16 }, ui::muted, 9.0f);
        // Input meter.
        const auto meter = meterArea();
        g.setColour(ui::border.withAlpha(0.6f));
        g.fillRoundedRectangle(meter.toFloat(), 4.0f);
        const auto fill = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(inputLevel, -60.0f) + 60.0f) / 60.0f);
        g.setGradientFill(juce::ColourGradient(ui::cyan, meter.getX(), meter.getBottom(),
                                              ui::danger, meter.getX(), meter.getY(), false));
        g.fillRoundedRectangle(juce::Rectangle<float>(meter.getX(), meter.getBottom() - fill * meter.getHeight(),
                                                      meter.getWidth(), fill * meter.getHeight()), 4.0f);
        // Waveform card.
        const auto wave = waveArea();
        ui::surface(g, wave.toFloat(), 9.0f);
        if (wavePeaks.empty())
        {
            ui::caption(g, "NO TAKE SELECTED", wave.reduced(20, 0).withTrimmedBottom(100), ui::muted, 11.0f);
            g.setColour(ui::muted);
            g.setFont(ui::font(11.0f));
            g.drawText("Record a take, then click it above to inspect.", wave.reduced(20, 0).withTrimmedTop(60),
                       juce::Justification::centredLeft);
            return;
        }
        ui::caption(g, "TAKE " + juce::String(waveTake) + "  /  " + juce::String(waveFrames / 48000.0, 1) + " S",
                    wave.reduced(16, 0).withTrimmedBottom(wave.getHeight() - 30), ui::cyan, 10.0f);
        if (!pitchDetected.empty())
            ui::caption(g, "CYAN DETECTED  /  VIOLET TARGET",
                        wave.reduced(16, 0).withTrimmedTop(16).withTrimmedBottom(wave.getHeight() - 44),
                        ui::muted, 9.0f);
        const auto plot = wave.reduced(16, 40).withTrimmedBottom(10);
        g.setColour(ui::blue.withAlpha(0.9f));
        const int buckets = static_cast<int>(wavePeaks.size());
        for (int x = 0; x < plot.getWidth(); ++x)
        {
            const auto peak = wavePeaks[static_cast<std::size_t>(x * buckets / plot.getWidth())];
            const auto h = std::max(1.0f, peak * (plot.getHeight() / 2 - 2));
            const auto cy = plot.getCentreY();
            g.fillRect(static_cast<float>(plot.getX() + x), cy - h, 1.0f, h * 2.0f);
        }
        // Pitch contour overlay, log-mapped over a 55-880 Hz vocal range.
        const auto toY = [&](float hz) {
            const float t = juce::jlimit(0.0f, 1.0f,
                std::log(hz / 55.0f) / std::log(880.0f / 55.0f));
            return plot.getBottom() - t * plot.getHeight();
        };
        const auto drawContour = [&](const std::vector<float>& curve, juce::Colour) {
            const int frames = static_cast<int>(curve.size());
            if (frames == 0)
                return;
            float lastX = -1.0f, lastY = 0.0f;
            for (int x = 0; x < plot.getWidth(); ++x)
            {
                const float hz = curve[static_cast<std::size_t>(x * frames / plot.getWidth())];
                if (hz <= 0.0f)
                {
                    lastX = -1.0f;
                    continue;
                }
                const float px = static_cast<float>(plot.getX() + x);
                const float py = toY(hz);
                if (lastX >= 0.0f)
                    g.drawLine(lastX, lastY, px, py, 1.6f);
                else
                    g.fillRect(px, py, 1.6f, 1.6f);
                lastX = px;
                lastY = py;
            }
        };
        g.setColour(ui::cyan.brighter(0.3f));
        drawContour(pitchDetected, ui::cyan);
        g.setColour(ui::violet.brighter(0.3f));
        drawContour(pitchTarget, ui::violet);
    }

    void setTakeWaves(std::vector<std::uint32_t> ids, std::vector<std::vector<float>> peaks)
    {
        laneIds = std::move(ids);
        lanePeaks = std::move(peaks);
        applyTakeWaves();
    }

    const std::vector<float>* findLane(std::uint32_t id) const
    {
        for (std::size_t i = 0; i < laneIds.size() && i < lanePeaks.size(); ++i)
            if (laneIds[i] == id)
                return &lanePeaks[i];
        return nullptr;
    }

    void applyTakeWaves()
    {
        for (auto& row : takeRows)
        {
            if (row.wave == nullptr)
                continue;
            const auto* found = findLane(row.id);
            row.wave->setWave(found != nullptr ? *found : std::vector<float> {}, row.isSelected,
                              row.isSolo);
        }
    }

    struct Row
    {
        std::uint32_t id = 0;
        bool isSelected = false, isSolo = false;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::TextButton> solo, keep, mute, remove;
        std::unique_ptr<WaveStrip> wave;
    };
    juce::ComboBox inputMode, pitchKey, pitchScale;
    juce::Slider pitchAmount, pitchSpeed, stretch;
    juce::Label stretchLabel, stretchValue;
    std::uint32_t stretchTake = 0;
    juce::TextButton monitor, analyze, apply;
    juce::Label status, takeHint, pitchStatus;
    std::vector<Row> takeRows;
    std::vector<float> wavePeaks, pitchDetected, pitchTarget;
    int pitchHop = 256;
    double pitchRate = 48000.0;
    int waveFrames = 0;
    std::uint32_t waveTake = 0;
    float inputLevel = 0.0f;
    juce::String lastSignature = "none";
    std::vector<std::uint32_t> laneIds;
    std::vector<std::vector<float>> lanePeaks;
    std::function<void(int)> onInputMode;
    std::function<void(bool)> onMonitor;
    std::function<void(std::uint32_t)> onMuteTake, onDeleteTake, onSelectTake;
    std::function<void()> onPitchChanged, onAnalyze, onApply;
    std::function<void(std::uint32_t)> onSoloTake, onKeepTake;
    std::function<void(std::uint32_t, float)> onStretchTake;
    std::function<void()> onStretchBegin, onStretchEnd;
    std::function<void(std::uint32_t)> onToSampler;
    juce::TextButton toSampler { "Play as instrument" };
};

static int activeInputCount(juce::AudioDeviceManager& manager)
{
    if (auto* device = manager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

void MainComponent::ensureAudioInputs()
{
    if (ideaKind == TrackKind::Synth && activeInputCount(deviceManager) > 0)
        return;
    setAudioChannels(2, 2);
}

// Key popup: song root, scale, and whether new notes snap to it.
struct KeyPanel : public juce::Component
{
    KeyPanel(std::function<void(int, int)> changeCb, std::function<void(bool)> snapCb)
        : onChange(std::move(changeCb)), onSnap(std::move(snapCb))
    {
        addAndMakeVisible(rootLabel);
        rootLabel.setText("Root", juce::dontSendNotification);
        rootLabel.setFont(ui::font(12.0f, true));
        rootLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(root);
        for (int key = 0; key < 12; ++key)
            root.addItem(keyName(key), key + 1);
        root.onChange = [this] { publish(); };
        addAndMakeVisible(scaleLabel);
        scaleLabel.setText("Scale", juce::dontSendNotification);
        scaleLabel.setFont(ui::font(12.0f, true));
        scaleLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(scaleBox);
        for (int s = 0; s < static_cast<int>(MusicScale::numScales); ++s)
            scaleBox.addItem(scaleName(static_cast<MusicScale>(s)), s + 1);
        scaleBox.onChange = [this] { publish(); };
        addAndMakeVisible(snap);
        snap.setButtonText("Snap new notes to key");
        snap.setClickingTogglesState(true);
        snap.onClick = [this] {
            if (onSnap) onSnap(snap.getToggleState());
        };
        snap.setTooltip("Drawn notes and chord stamps land on scale tones.");
        setSize(280, 150);
    }
    void refresh(int key, int scale, bool snapOn)
    {
        root.setSelectedId(std::clamp(key, 0, 11) + 1, juce::dontSendNotification);
        scaleBox.setSelectedId(std::clamp(scale, 0, static_cast<int>(MusicScale::numScales) - 1) + 1,
                               juce::dontSendNotification);
        snap.setToggleState(snapOn, juce::dontSendNotification);
    }
    void publish()
    {
        if (onChange) onChange(root.getSelectedId() - 1, scaleBox.getSelectedId() - 1);
    }
    void resized() override
    {
        rootLabel.setBounds(16, 10, 120, 20);
        root.setBounds(16, 32, 120, 28);
        scaleLabel.setBounds(144, 10, 120, 20);
        scaleBox.setBounds(144, 32, 120, 28);
        snap.setBounds(16, 70, 248, 28);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "SONG KEY", { 16, 108, 200, 18 }, ui::muted, 9.0f);
    }
    juce::Label rootLabel, scaleLabel;
    juce::ComboBox root, scaleBox;
    juce::TextButton snap { "Snap" };
    std::function<void(int, int)> onChange;
    std::function<void(bool)> onSnap;
};

// Chord picker: choosing a type arms the piano roll for one-finger stamps.
struct ChordPanel : public juce::Component
{
    explicit ChordPanel(std::function<void(int)> pickCb) : onPick(std::move(pickCb))
    {
        for (int c = 0; c < static_cast<int>(ChordType::numChords); ++c)
        {
            auto& button = chords[static_cast<std::size_t>(c)];
            addAndMakeVisible(button);
            button.setButtonText(chordName(static_cast<ChordType>(c)));
            button.setTooltip("Stamp this chord with one click, then keep clicking for a progression.");
            button.onClick = [this, c] { if (onPick) onPick(c); };
        }
        setSize(280, 150);
    }
    void refresh(bool armed) { repaint(); juce::ignoreUnused(armed); }
    void resized() override
    {
        for (int c = 0; c < static_cast<int>(ChordType::numChords); ++c)
            chords[static_cast<std::size_t>(c)].setBounds(16 + (c % 4) * 64, 14 + (c / 4) * 36, 60, 30);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "CHORD  /  CLICK THE ROLL TO STAMP", { 16, 88, 248, 18 }, ui::muted, 9.0f);
    }
    std::array<juce::TextButton, static_cast<std::size_t>(ChordType::numChords)> chords;
    std::function<void(int)> onPick;
};

// Arpeggiator + live chord FX for MiniLab playing on the selected track.
// Changes apply immediately (each one is undoable); the engine picks them up
// with the next project submit.
struct ArpPanel : public juce::Component
{
    explicit ArpPanel(std::function<void(LiveFx)> changeCb) : onChange(std::move(changeCb))
    {
        addAndMakeVisible(modeLabel);
        modeLabel.setText("Arp mode", juce::dontSendNotification);
        modeLabel.setFont(ui::font(12.0f, true));
        modeLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(mode);
        for (int m = 0; m < static_cast<int>(ArpMode::numModes); ++m)
            mode.addItem(arpModeName(static_cast<ArpMode>(m)), m + 1);
        mode.onChange = [this] { publish(); };
        addAndMakeVisible(rateLabel);
        rateLabel.setText("Rate", juce::dontSendNotification);
        rateLabel.setFont(ui::font(12.0f, true));
        rateLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(rate);
        for (int r = 0; r < static_cast<int>(ArpRate::numRates); ++r)
            rate.addItem(arpRateName(static_cast<ArpRate>(r)), r + 1);
        rate.onChange = [this] { publish(); };
        addAndMakeVisible(octavesLabel);
        octavesLabel.setText("Octaves", juce::dontSendNotification);
        octavesLabel.setFont(ui::font(12.0f, true));
        octavesLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(octaves);
        for (int o = 1; o <= 3; ++o)
            octaves.addItem(juce::String(o), o);
        octaves.onChange = [this] { publish(); };
        addAndMakeVisible(liveChordLabel);
        liveChordLabel.setText("Live chord", juce::dontSendNotification);
        liveChordLabel.setFont(ui::font(12.0f, true));
        liveChordLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(liveChord);
        liveChord.addItem("Off", 1);
        for (int c = 0; c < static_cast<int>(ChordType::numChords); ++c)
            liveChord.addItem(chordName(static_cast<ChordType>(c)), c + 2);
        liveChord.onChange = [this] { publish(); };
        addAndMakeVisible(latch);
        latch.setButtonText("Latch (arp holds after release)");
        latch.setClickingTogglesState(true);
        latch.setTooltip("Released notes keep arpeggiating until you play a fresh chord.");
        latch.onClick = [this] { publish(); };
        setSize(280, 208);
    }
    void refresh(const LiveFx& fx)
    {
        mode.setSelectedId(static_cast<int>(fx.arp) + 1, juce::dontSendNotification);
        rate.setSelectedId(static_cast<int>(fx.rate) + 1, juce::dontSendNotification);
        octaves.setSelectedId(std::clamp(fx.octaves, 1, 3), juce::dontSendNotification);
        liveChord.setSelectedId(fx.chordOn ? static_cast<int>(fx.chord) + 2 : 1, juce::dontSendNotification);
        latch.setToggleState(fx.latch, juce::dontSendNotification);
    }
    void publish()
    {
        if (!onChange)
            return;
        LiveFx fx;
        fx.arp = static_cast<ArpMode>(std::clamp(mode.getSelectedId() - 1, 0,
                                                static_cast<int>(ArpMode::numModes) - 1));
        fx.rate = static_cast<ArpRate>(std::clamp(rate.getSelectedId() - 1, 0,
                                                 static_cast<int>(ArpRate::numRates) - 1));
        fx.octaves = std::clamp(octaves.getSelectedId(), 1, 3);
        const int chordId = liveChord.getSelectedId();
        fx.chordOn = chordId > 1;
        fx.chord = static_cast<ChordType>(std::clamp(chordId - 2, 0,
                                                    static_cast<int>(ChordType::numChords) - 1));
        fx.latch = latch.getToggleState();
        onChange(fx);
    }
    void resized() override
    {
        modeLabel.setBounds(16, 10, 120, 20);
        mode.setBounds(16, 32, 120, 28);
        rateLabel.setBounds(144, 10, 120, 20);
        rate.setBounds(144, 32, 120, 28);
        octavesLabel.setBounds(16, 68, 120, 20);
        octaves.setBounds(16, 90, 120, 28);
        liveChordLabel.setBounds(144, 68, 120, 20);
        liveChord.setBounds(144, 90, 120, 28);
        latch.setBounds(16, 128, 248, 28);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "LIVE ARP + CHORD  /  MINILAB KEYS", { 16, 164, 248, 18 }, ui::muted, 9.0f);
    }
    juce::Label modeLabel, rateLabel, octavesLabel, liveChordLabel;
    juce::ComboBox mode, rate, octaves, liveChord;
    juce::ToggleButton latch;
    std::function<void(LiveFx)> onChange;
};

// Velocity ramp: fade loop velocities from one level to another.
struct RampPanel : public juce::Component
{
    explicit RampPanel(std::function<void(int, int)> applyCb) : onApply(std::move(applyCb))
    {
        addAndMakeVisible(fromLabel);
        fromLabel.setText("First note", juce::dontSendNotification);
        fromLabel.setFont(ui::font(12.0f, true));
        fromLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(from);
        from.setRange(1.0, 127.0, 1.0);
        from.setTextValueSuffix("");
        addAndMakeVisible(toLabel);
        toLabel.setText("Last note", juce::dontSendNotification);
        toLabel.setFont(ui::font(12.0f, true));
        toLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(to);
        to.setRange(1.0, 127.0, 1.0);
        addAndMakeVisible(apply);
        apply.setButtonText("Apply ramp");
        apply.setTooltip("Set velocities across the loop, first note to last (undoable).");
        apply.onClick = [this] {
            if (onApply) onApply(static_cast<int>(from.getValue()), static_cast<int>(to.getValue()));
        };
        from.setValue(70.0, juce::dontSendNotification);
        to.setValue(110.0, juce::dontSendNotification);
        setSize(280, 168);
    }
    void resized() override
    {
        fromLabel.setBounds(16, 10, 120, 20);
        from.setBounds(16, 32, 120, 28);
        toLabel.setBounds(144, 10, 120, 20);
        to.setBounds(144, 32, 120, 28);
        apply.setBounds(16, 72, 248, 30);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "VELOCITY RAMP", { 16, 110, 200, 18 }, ui::muted, 9.0f);
    }
    juce::Label fromLabel, toLabel;
    juce::Slider from, to;
    juce::TextButton apply;
    std::function<void(int, int)> onApply;
};

// Mixer: one strip per used track (fader, pan, mute/solo, two sends) plus
// the delay/reverb return strips. Dumb view: setState rebuilds or refreshes,
// every gesture leaves through callbacks so the owner can apply undo.
struct MixerState
{
    std::array<TrackMix, maxTracks> mix {};
    std::array<juce::String, maxTracks> names;
    std::array<juce::Colour, maxTracks> colours;
    std::array<bool, maxTracks> used {}, drums {};
    ProjectSends sends;
};

class MixerView final : public juce::Component
{
public:
    std::function<void(int, float)> onVolume, onPan, onSendDelay, onSendReverb;
    std::function<void(int)> onMute, onSolo;
    std::function<void(int, float)> onReturn;
    std::function<void(float)> onDelayTime, onReverbSize;
    std::function<void()> onGestureBegin, onGestureEnd;

    void setState(const MixerState& next)
    {
        juce::String signature;
        for (int t = 0; t < maxTracks; ++t)
            if (next.used[static_cast<std::size_t>(t)])
                signature += juce::String(t) + (next.drums[static_cast<std::size_t>(t)] ? "d" : "");
        if (signature != lastSignature)
        {
            lastSignature = signature;
            strips.clear();
            for (int t = 0; t < maxTracks; ++t)
            {
                if (!next.used[static_cast<std::size_t>(t)])
                    continue;
                auto strip = std::make_unique<Strip>();
                strip->track = t;
                strip->drums = next.drums[static_cast<std::size_t>(t)];
                addAndMakeVisible(strip->name.get());
                strip->name->setText(next.names[static_cast<std::size_t>(t)], juce::dontSendNotification);
                strip->name->setFont(ui::font(12.0f, true));
                strip->name->setColour(juce::Label::textColourId,
                                       next.colours[static_cast<std::size_t>(t)]);
                strip->name->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(strip->mute.get());
                strip->mute->setButtonText("M");
                strip->mute->setClickingTogglesState(true);
                strip->mute->setWantsKeyboardFocus(false);
                strip->mute->setColour(juce::TextButton::buttonOnColourId, ui::danger);
                strip->mute->onClick = [this, t] { if (onMute) onMute(t); };
                addAndMakeVisible(strip->solo.get());
                strip->solo->setButtonText("S");
                strip->solo->setClickingTogglesState(true);
                strip->solo->setWantsKeyboardFocus(false);
                strip->solo->setColour(juce::TextButton::buttonOnColourId, ui::cyan);
                strip->solo->onClick = [this, t] { if (onSolo) onSolo(t); };
                wireFader(strip->volume, 0.0, 150.0, true,
                          [this, t](float v) { if (onVolume) onVolume(t, v / 100.0f); });
                wireFader(strip->pan, -100.0, 100.0, false,
                          [this, t](float v) { if (onPan) onPan(t, v / 100.0f); });
                wireFader(strip->sendDelay, 0.0, 100.0, false,
                          [this, t](float v) { if (onSendDelay) onSendDelay(t, v / 100.0f); });
                wireFader(strip->sendReverb, 0.0, 100.0, false,
                          [this, t](float v) { if (onSendReverb) onSendReverb(t, v / 100.0f); });
                for (auto [box, text] : { std::pair<juce::Label*, const char*> { strip->panLabel.get(), "PAN" },
                                                 { strip->dlyLabel.get(), "DLY" }, { strip->rvbLabel.get(), "RVB" } })
                {
                    box->setText(text, juce::dontSendNotification);
                    box->setFont(ui::font(9.0f, true));
                    box->setColour(juce::Label::textColourId, ui::muted);
                    addAndMakeVisible(box);
                }
                addAndMakeVisible(strip->volume.get());
                addAndMakeVisible(strip->pan.get());
                addAndMakeVisible(strip->sendDelay.get());
                addAndMakeVisible(strip->sendReverb.get());
                strips.push_back(std::move(strip));
            }
            returns.clear();
            for (int bus = 0; bus < 2; ++bus)
            {
                auto ret = std::make_unique<Return>();
                ret->bus = bus;
                addAndMakeVisible(ret->name.get());
                ret->name->setText(bus == 0 ? "DELAY" : "REVERB", juce::dontSendNotification);
                ret->name->setFont(ui::font(12.0f, true));
                ret->name->setColour(juce::Label::textColourId, ui::violet);
                ret->name->setJustificationType(juce::Justification::centred);
                wireFader(ret->level, 0.0, 150.0, true,
                          [this, bus](float v) { if (onReturn) onReturn(bus, v / 100.0f); });
                wireFader(ret->tone, bus == 0 ? 20.0 : 0.0, bus == 0 ? 1000.0 : 100.0, false,
                          [this, bus](float v) {
                              if (bus == 0)
                              {
                                  if (onDelayTime) onDelayTime(v);
                              }
                              else if (onReverbSize)
                                  onReverbSize(v / 100.0f);
                          });
                ret->toneLabel->setText(bus == 0 ? "MS" : "SIZE", juce::dontSendNotification);
                ret->toneLabel->setFont(ui::font(9.0f, true));
                ret->toneLabel->setColour(juce::Label::textColourId, ui::muted);
                addAndMakeVisible(ret->toneLabel.get());
                addAndMakeVisible(ret->level.get());
                addAndMakeVisible(ret->tone.get());
                returns.push_back(std::move(ret));
            }
            resized();
        }
        for (auto& strip : strips)
        {
            const auto& mix = next.mix[static_cast<std::size_t>(strip->track)];
            strip->mute->setToggleState(mix.mute, juce::dontSendNotification);
            strip->solo->setToggleState(mix.solo, juce::dontSendNotification);
            strip->volume->setValue(mix.volume * 100.0, juce::dontSendNotification);
            strip->pan->setValue(mix.pan * 100.0, juce::dontSendNotification);
            strip->sendDelay->setValue(mix.sendDelay * 100.0, juce::dontSendNotification);
            strip->sendReverb->setValue(mix.sendReverb * 100.0, juce::dontSendNotification);
        }
        for (auto& ret : returns)
        {
            const bool delay = ret->bus == 0;
            ret->level->setValue((delay ? next.sends.delayReturn : next.sends.reverbReturn) * 100.0,
                                 juce::dontSendNotification);
            ret->tone->setValue(delay ? next.sends.delay.timeMs : next.sends.reverb.size * 100.0,
                                juce::dontSendNotification);
        }
        repaint();
    }

    void resized() override
    {
        const int count = static_cast<int>(strips.size() + returns.size());
        if (count == 0)
            return;
        const float w = std::min(116.0f, static_cast<float>(getWidth()) / static_cast<float>(count));
        int x = 4;
        for (auto& strip : strips)
        {
            const int sw = static_cast<int>(w) - 8;
            strip->name->setBounds(x, 6, sw, 20);
            strip->mute->setBounds(x, 30, (sw - 4) / 2, 24);
            strip->solo->setBounds(x + (sw - 4) / 2 + 4, 30, (sw - 4) / 2, 24);
            strip->volume->setBounds(x, 58, sw, 150);
            strip->panLabel->setBounds(x, 212, 30, 34);
            strip->pan->setBounds(x + 30, 212, sw - 30, 34);
            strip->dlyLabel->setBounds(x, 250, 30, 34);
            strip->sendDelay->setBounds(x + 30, 250, sw - 30, 34);
            strip->rvbLabel->setBounds(x, 288, 30, 34);
            strip->sendReverb->setBounds(x + 30, 288, sw - 30, 34);
            x += static_cast<int>(w);
        }
        for (auto& ret : returns)
        {
            const int sw = static_cast<int>(w) - 8;
            ret->name->setBounds(x, 6, sw, 20);
            ret->level->setBounds(x, 58, sw, 150);
            ret->toneLabel->setBounds(x, 212, 34, 34);
            ret->tone->setBounds(x + 34, 212, sw - 34, 34);
            x += static_cast<int>(w);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c111b));
        ui::caption(g, "MIXER  /  FADER  PAN  SENDS  RETURNS", { 12, getHeight() - 22, 400, 18 }, ui::muted, 10.0f);
        int x = 4;
        const int count = static_cast<int>(strips.size() + returns.size());
        if (count == 0)
        {
            g.setColour(ui::muted);
            g.setFont(ui::font(12.0f));
            g.drawText("No tracks yet.", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const float w = std::min(116.0f, static_cast<float>(getWidth()) / static_cast<float>(count));
        for (std::size_t i = 0; i < strips.size() + returns.size(); ++i)
        {
            ui::surface(g, juce::Rectangle<float>(static_cast<float>(x), 0.0f, w - 4.0f,
                                                  static_cast<float>(getHeight() - 26)).toFloat(), 8.0f);
            x += static_cast<int>(w);
        }
    }

private:
    struct Strip
    {
        int track = 0;
        bool drums = false;
        std::unique_ptr<juce::Label> name = std::make_unique<juce::Label>();
        std::unique_ptr<juce::TextButton> mute = std::make_unique<juce::TextButton>();
        std::unique_ptr<juce::TextButton> solo = std::make_unique<juce::TextButton>();
        std::unique_ptr<juce::Slider> volume = std::make_unique<juce::Slider>();
        std::unique_ptr<juce::Label> panLabel = std::make_unique<juce::Label>();
        std::unique_ptr<juce::Slider> pan = std::make_unique<juce::Slider>();
        std::unique_ptr<juce::Label> dlyLabel = std::make_unique<juce::Label>();
        std::unique_ptr<juce::Slider> sendDelay = std::make_unique<juce::Slider>();
        std::unique_ptr<juce::Label> rvbLabel = std::make_unique<juce::Label>();
        std::unique_ptr<juce::Slider> sendReverb = std::make_unique<juce::Slider>();
    };
    struct Return
    {
        int bus = 0;
        std::unique_ptr<juce::Label> name = std::make_unique<juce::Label>();
        std::unique_ptr<juce::Slider> level = std::make_unique<juce::Slider>();
        std::unique_ptr<juce::Label> toneLabel = std::make_unique<juce::Label>();
        std::unique_ptr<juce::Slider> tone = std::make_unique<juce::Slider>();
    };
    void wireFader(std::unique_ptr<juce::Slider>& slider, double min, double max, bool vertical,
                   std::function<void(float)> change)
    {
        slider->setSliderStyle(vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
        slider->setRange(min, max, min < 0.0 ? 1.0 : (max > 200.0 ? 1.0 : 0.5));
        if (vertical)
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 18);
        else
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
        slider->setColour(juce::Slider::thumbColourId, ui::cyan);
        slider->setColour(juce::Slider::textBoxTextColourId, ui::text);
        slider->setWantsKeyboardFocus(false);
        slider->onValueChange = [change, slider = slider.get()] {
            change(static_cast<float>(slider->getValue()));
        };
        slider->onDragStart = [this] { if (onGestureBegin) onGestureBegin(); };
        slider->onDragEnd = [this] { if (onGestureEnd) onGestureEnd(); };
    }

    std::vector<std::unique_ptr<Strip>> strips;
    std::vector<std::unique_ptr<Return>> returns;
    juce::String lastSignature = "none";
};

// Automation lane editor: point curves per loop for mix/FX targets. Click
// adds a point, dragging moves it (order preserved), right-click deletes.
// All mutations leave through onLaneChange so the owner applies undo.
class AutomationView final : public juce::Component
{
public:
    std::function<void(AutomationTarget)> onTargetChange;
    std::function<void(AutomationLane)> onLaneChange;
    std::function<void()> onGestureBegin, onGestureEnd;

    AutomationView()
    {
        for (int t = 0; t < static_cast<int>(AutomationTarget::numTargets); ++t)
        {
            auto& tab = tabs[static_cast<std::size_t>(t)];
            addAndMakeVisible(tab);
            tab.setButtonText(automationTargetName(static_cast<AutomationTarget>(t)));
            tab.setWantsKeyboardFocus(false);
            tab.onClick = [this, t] {
                if (onTargetChange) onTargetChange(static_cast<AutomationTarget>(t));
            };
        }
        addAndMakeVisible(clear);
        clear.setButtonText("Clear");
        clear.setWantsKeyboardFocus(false);
        clear.setTooltip("Delete every point in this lane (undoable).");
        clear.onClick = [this] {
            if (onLaneChange) onLaneChange(AutomationLane {});
        };
        addAndMakeVisible(readout);
        readout.setFont(ui::font(11.0f));
        readout.setColour(juce::Label::textColourId, ui::muted);
        readout.setJustificationType(juce::Justification::centredRight);
    }

    void setTarget(AutomationTarget next)
    {
        target = next < AutomationTarget::numTargets ? next : AutomationTarget::Volume;
        for (int t = 0; t < static_cast<int>(AutomationTarget::numTargets); ++t)
            tabs[static_cast<std::size_t>(t)].setToggleState(
                static_cast<AutomationTarget>(t) == target, juce::dontSendNotification);
        selected = -1;
        repaint();
    }

    void setLane(const AutomationLane& next, int playTick, bool running)
    {
        lane = next.count <= maxAutomationPoints ? next : AutomationLane {};
        playhead = playTick;
        playing = running;
        if (selected >= lane.count)
            selected = -1;
        refreshReadout();
        repaint(canvasArea().toNearestInt());
    }

    void resized() override
    {
        const int tabW = 92;
        int y = 6;
        for (auto& tab : tabs)
        {
            tab.setBounds(8, y, tabW, 22);
            y += 26;
        }
        clear.setBounds(8, y + 2, tabW, 22);
        readout.setBounds(getWidth() - 220, getHeight() - 22, 212, 18);
    }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "AUTOMATION  /  CLICK ADD  DRAG MOVE  RIGHT-CLICK DELETE",
                    { 108, 8, 400, 16 }, ui::muted, 9.0f);
        const auto canvas = canvasArea();
        g.setColour(juce::Colour(0xff0c111b));
        g.fillRoundedRectangle(canvas, 6.0f);
        const auto [lo, hi] = automationRange(target);
        // Center line for bipolar targets.
        if (lo < 0.0f)
        {
            const auto cy = valueY(0.0f);
            g.setColour(ui::border.withAlpha(0.7f));
            g.drawHorizontalLine(static_cast<int>(cy), canvas.getX() + 4.0f, canvas.getRight() - 4.0f);
        }
        // Playhead.
        if (playing && playhead >= 0)
        {
            g.setColour(ui::cyan.withAlpha(0.5f));
            const auto x = tickX(playhead);
            g.drawVerticalLine(static_cast<int>(x), canvas.getY() + 4.0f, canvas.getBottom() - 4.0f);
        }
        // Curve.
        g.setColour(ui::cyan.withAlpha(0.9f));
        const auto point = [&](int i) {
            const auto& p = lane.points[static_cast<std::size_t>(i)];
            return juce::Point<float>(tickX(p.tick), valueY(p.value));
        };
        if (lane.count == 1)
        {
            const auto dot = point(0);
            g.fillEllipse(dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);
            g.drawHorizontalLine(static_cast<int>(dot.y), canvas.getX() + 4.0f, canvas.getRight() - 4.0f);
        }
        else
        {
            juce::Path curve;
            curve.startNewSubPath(juce::Point<float>(canvas.getX() + 4.0f, point(0).y));
            curve.lineTo(point(0));
            for (int i = 1; i < lane.count; ++i)
                curve.lineTo(point(i));
            curve.lineTo(juce::Point<float>(canvas.getRight() - 4.0f, point(lane.count - 1).y));
            g.strokePath(curve, juce::PathStrokeType(1.5f));
        }
        for (int i = 0; i < lane.count; ++i)
        {
            const auto c = point(i);
            g.setColour(i == selected ? ui::text : ui::cyan);
            g.fillEllipse(c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
            g.setColour(juce::Colour(0xff0c111b));
            g.fillEllipse(c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
        }
        if (lane.count == 0)
        {
            g.setColour(ui::muted.withAlpha(0.7f));
            g.setFont(ui::font(11.0f));
            g.drawText("No automation: this loop uses the mixer and FX settings.",
                       canvas.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!canvasArea().contains(event.position))
            return;
        if (onGestureBegin) onGestureBegin();
        gestureOpen = true;
        const int hit = pointAt(event.position);
        if (event.mods.isRightButtonDown())
        {
            if (hit >= 0)
            {
                auto next = lane;
                for (int i = hit; i + 1 < next.count; ++i)
                    next.points[static_cast<std::size_t>(i)] = next.points[static_cast<std::size_t>(i + 1)];
                --next.count;
                selected = -1;
                publish(next);
            }
            endGesture();
            return;
        }
        if (hit >= 0)
        {
            selected = hit;
            refreshReadout();
            repaint(canvasArea().toNearestInt());
            return;
        }
        if (lane.count >= maxAutomationPoints)
        {
            endGesture();
            return;
        }
        auto next = lane;
        const AutomationPoint made { xTick(event.position.x), yValue(event.position.y) };
        int at = next.count;
        for (int i = 0; i < next.count; ++i)
            if (made.tick < next.points[static_cast<std::size_t>(i)].tick)
            {
                at = i;
                break;
            }
        for (int i = next.count; i > at; --i)
            next.points[static_cast<std::size_t>(i)] = next.points[static_cast<std::size_t>(i - 1)];
        next.points[static_cast<std::size_t>(at)] = made;
        ++next.count;
        selected = at;
        publish(next);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!gestureOpen || selected < 0 || selected >= lane.count)
            return;
        const auto [lo, hi] = automationRange(target);
        const int prevTick = selected > 0 ? lane.points[static_cast<std::size_t>(selected - 1)].tick : 0;
        const int nextTick = selected + 1 < lane.count ? lane.points[static_cast<std::size_t>(selected + 1)].tick
                                                       : patternTicks;
        auto next = lane;
        auto& point = next.points[static_cast<std::size_t>(selected)];
        point.tick = std::clamp(xTick(event.position.x), prevTick, nextTick);
        point.value = std::clamp(yValue(event.position.y), lo, hi);
        publish(next);
    }

    void mouseUp(const juce::MouseEvent&) override { endGesture(); }

private:
    juce::Rectangle<float> canvasArea() const
    {
        return { 108.0f, 28.0f, static_cast<float>(getWidth()) - 118, static_cast<float>(getHeight()) - 56 };
    }
    float tickX(int tick) const
    {
        const auto canvas = canvasArea();
        return canvas.getX() + 4.0f
            + static_cast<float>(tick) / static_cast<float>(patternTicks) * (canvas.getWidth() - 8.0f);
    }
    float valueY(float value) const
    {
        const auto canvas = canvasArea();
        const auto [lo, hi] = automationRange(target);
        const float t = hi > lo ? (value - lo) / (hi - lo) : 0.0f;
        return canvas.getBottom() - 4.0f - std::clamp(t, 0.0f, 1.0f) * (canvas.getHeight() - 8.0f);
    }
    int xTick(float x) const
    {
        const auto canvas = canvasArea();
        const float t = (x - canvas.getX() - 4.0f) / (canvas.getWidth() - 8.0f);
        return std::clamp(static_cast<int>(std::round(t * patternTicks)), 0, patternTicks);
    }
    float yValue(float y) const
    {
        const auto canvas = canvasArea();
        const auto [lo, hi] = automationRange(target);
        const float t = (canvas.getBottom() - 4.0f - y) / (canvas.getHeight() - 8.0f);
        const float v = lo + std::clamp(t, 0.0f, 1.0f) * (hi - lo);
        return target == AutomationTarget::Volume ? std::round(v * 100.0f) / 100.0f
                                                  : std::round(v * 200.0f) / 200.0f;
    }
    int pointAt(juce::Point<float> p) const
    {
        int best = -1;
        float bestDist = 9.0f;
        for (int i = 0; i < lane.count; ++i)
        {
            const auto& point = lane.points[static_cast<std::size_t>(i)];
            const float dist = p.getDistanceFrom({ tickX(point.tick), valueY(point.value) });
            if (dist < bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }
        return best;
    }
    void publish(const AutomationLane& next)
    {
        lane = next;
        if (selected >= lane.count)
            selected = -1;
        refreshReadout();
        repaint(canvasArea().toNearestInt());
        if (onLaneChange) onLaneChange(next);
    }
    void endGesture()
    {
        if (gestureOpen)
        {
            gestureOpen = false;
            if (onGestureEnd) onGestureEnd();
        }
    }
    void refreshReadout()
    {
        if (selected >= 0 && selected < lane.count)
        {
            const auto& point = lane.points[static_cast<std::size_t>(selected)];
            readout.setText("bar " + juce::String(point.tick / (patternTicks / 4) + 1) + "  /  "
                                + juce::String(point.value, 2),
                            juce::dontSendNotification);
        }
        else
            readout.setText(juce::String(lane.count) + (lane.count == 1 ? " point" : " points"),
                            juce::dontSendNotification);
    }

    std::array<juce::TextButton, static_cast<std::size_t>(AutomationTarget::numTargets)> tabs;
    juce::TextButton clear;
    juce::Label readout;
    AutomationTarget target = AutomationTarget::Volume;
    AutomationLane lane;
    int selected = -1;
    int playhead = -1;
    bool playing = false, gestureOpen = false;
};

// Groove popup: per-track swing plus destructive quantize/humanize for
// the selected loop. Lives in a CallOutBox so the toolbar stays compact.
struct GroovePanel : public juce::Component
{
    GroovePanel(std::function<void(float)> swingCb, std::function<void()> swingDragStartCb,
                std::function<void()> swingDragEndCb, std::function<void(float)> quantizeCb,
                std::function<void(float)> humanizeCb)
        : onSwing(std::move(swingCb)), onQuantize(std::move(quantizeCb)),
          onHumanize(std::move(humanizeCb)), onSwingDragStart(std::move(swingDragStartCb)),
          onSwingDragEnd(std::move(swingDragEndCb))
    {
        addAndMakeVisible(swingLabel);
        swingLabel.setText("Swing", juce::dontSendNotification);
        swingLabel.setFont(ui::font(12.0f, true));
        swingLabel.setColour(juce::Label::textColourId, ui::text);
        addAndMakeVisible(swing);
        swing.setRange(0.0, 75.0, 0.5);
        swing.setTextValueSuffix(" %");
        swing.setTooltip("Delay off-beat 16ths. 0 is straight; 15-30 is the classic pocket.");
        swing.onDragStart = [this] { if (onSwingDragStart) onSwingDragStart(); };
        swing.onDragEnd = [this] { if (onSwingDragEnd) onSwingDragEnd(); };
        swing.onValueChange = [this] {
            if (onSwing) onSwing(static_cast<float>(swing.getValue()) / 100.0f);
        };
        addAndMakeVisible(quantizeStrength);
        quantizeStrength.setRange(0.0, 100.0, 1.0);
        quantizeStrength.setTextValueSuffix(" %");
        quantizeStrength.setTooltip("How far notes move toward the grid.");
        addAndMakeVisible(quantize);
        quantize.setButtonText("Quantize");
        quantize.setTooltip("Snap this loop's notes to the 16th grid (undoable).");
        quantize.onClick = [this] {
            if (onQuantize) onQuantize(static_cast<float>(quantizeStrength.getValue()) / 100.0f);
        };
        addAndMakeVisible(humanizeAmount);
        humanizeAmount.setRange(0.0, 100.0, 1.0);
        humanizeAmount.setTextValueSuffix(" %");
        humanizeAmount.setTooltip("How much timing and velocity wander.");
        addAndMakeVisible(humanize);
        humanize.setButtonText("Humanize");
        humanize.setTooltip("Loosen timing and velocities with a fresh random feel each press (undoable).");
        humanize.onClick = [this] {
            if (onHumanize) onHumanize(static_cast<float>(humanizeAmount.getValue()) / 100.0f);
        };
        setSize(280, 196);
    }
    void refresh(float swingValue, bool drums)
    {
        swing.setValue(swingValue * 100.0, juce::dontSendNotification);
        quantizeStrength.setValue(100.0, juce::dontSendNotification);
        humanizeAmount.setValue(30.0, juce::dontSendNotification);
        // Drum grids are already quantized; only timing swing and velocity
        // wander apply there.
        quantizeStrength.setVisible(!drums);
        quantize.setVisible(!drums);
        resized();
    }
    void resized() override
    {
        swingLabel.setBounds(16, 10, 248, 20);
        swing.setBounds(16, 32, 248, 28);
        int y = 70;
        if (quantize.isVisible())
        {
            quantizeStrength.setBounds(16, y, 140, 28);
            quantize.setBounds(164, y, 100, 28);
            y += 38;
        }
        humanizeAmount.setBounds(16, y, 140, 28);
        humanize.setBounds(164, y, 100, 28);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 10.0f);
        ui::caption(g, "GROOVE  /  SELECTED TRACK", { 16, 208 - 22, 200, 18 }, ui::muted, 9.0f);
    }
    juce::Label swingLabel;
    juce::Slider swing, quantizeStrength, humanizeAmount;
    juce::TextButton quantize, humanize;
    std::function<void(float)> onSwing, onQuantize, onHumanize;
    std::function<void()> onSwingDragStart, onSwingDragEnd;
};

MainComponent::MainComponent()
{
    setLookAndFeel(&theme);
    setWantsKeyboardFocus(true);
    // The sender thread only ever touches the output through sendMiniLabNow
    // under the output lock, so a wedged device stalls that thread — never
    // the UI.
    miniLabSender = std::make_unique<MiniLabSender>(
        [safe = juce::Component::SafePointer<MainComponent>(this)](const minilab::Bytes& bytes) {
            if (safe != nullptr)
                safe->sendMiniLabNow(bytes);
        });
    title.setText("SONORA", juce::dontSendNotification);
    title.setFont(ui::font(30.0f, true, 0.22f));
    subtitle.setFont(ui::font(12.0f));
    subtitle.setColour(juce::Label::textColourId, ui::muted);
    status.setFont(ui::font(11.0f));
    status.setColour(juce::Label::textColourId, ui::muted);
    position.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 25.0f, juce::Font::plain)));
    position.setColour(juce::Label::textColourId, ui::cyan);
    outputMeter.setFont(ui::font(12.0f, true));
    description.setText("Draw: click + drag  |  Move: drag note  |  Resize: right edge / Shift-drag  |  Delete: right-click  |  Velocity: scroll", juce::dontSendNotification);
    description.setFont(ui::font(11.0f));
    description.setColour(juce::Label::textColourId, ui::muted);
    for (auto* component : std::initializer_list<juce::Component*> {
             &title, &subtitle, &description, &status, &position, &audioSettings,
             &panic, &keyboard, &pianoRoll, &play, &stop, &record, &ideaButton, &undo, &redo, &newProject,
             &open, &save, &saveAs, &exportButton, &clear, &demo, &tempo, &drumSequencer,
             &audioTab, &mute, &solo, &trackVolume, &repeatBar, &kitButton, &outputMeter,
             &loopView, &songView, &mixView, &duplicatePattern, &themeButton, &grooveButton,
             &keyButton, &chordButton, &arpButton, &rampButton, &autoButton })
        addAndMakeVisible(component);
    for (auto* component : std::initializer_list<juce::Component*> { &songTemplate, &partChoice, &partTrackOn, &partHint })
        addChildComponent(component);
    arrangement = std::make_unique<ArrangementView>();
    addChildComponent(arrangement.get());
    arrangement->onAction = [this](const ArrangementAction& action) { handleArrangementAction(action); };
    arrangement->onGestureBegin = [this] { beginEdit(); };
    arrangement->onGestureEnd = [this] { endEdit(); };
    mixer = std::make_unique<MixerView>();
    addChildComponent(mixer.get());
    auto mixEdit = [this](int track, auto&& change) {
        if (track < 0 || track >= maxTracks)
            return;
        const bool own = !editing;
        if (own)
            beginEdit();
        change(project.tracks[static_cast<std::size_t>(track)].mix);
        projectChanged();
        if (own)
            endEdit();
    };
    mixer->onVolume = [this, mixEdit](int track, float v) {
        mixEdit(track, [v](TrackMix& mix) { mix.volume = std::clamp(v, 0.0f, 1.5f); });
    };
    mixer->onPan = [this, mixEdit](int track, float v) {
        mixEdit(track, [v](TrackMix& mix) { mix.pan = std::clamp(v, -1.0f, 1.0f); });
    };
    mixer->onSendDelay = [this, mixEdit](int track, float v) {
        mixEdit(track, [v](TrackMix& mix) { mix.sendDelay = std::clamp(v, 0.0f, 1.0f); });
    };
    mixer->onSendReverb = [this, mixEdit](int track, float v) {
        mixEdit(track, [v](TrackMix& mix) { mix.sendReverb = std::clamp(v, 0.0f, 1.0f); });
    };
    auto mixOwn = [this](auto&& change) {
        const bool own = !editing;
        if (own)
            beginEdit();
        change();
        projectChanged();
        if (own)
            endEdit();
    };
    mixer->onMute = [this, mixOwn](int track) {
        if (track < 0 || track >= maxTracks)
            return;
        mixOwn([this, track] {
            auto& mix = project.tracks[static_cast<std::size_t>(track)].mix;
            mix.mute = !mix.mute;
        });
    };
    mixer->onSolo = [this, mixOwn](int track) {
        if (track < 0 || track >= maxTracks)
            return;
        mixOwn([this, track] {
            auto& mix = project.tracks[static_cast<std::size_t>(track)].mix;
            mix.solo = !mix.solo;
        });
    };
    mixer->onReturn = [this, mixOwn](int bus, float v) {
        mixOwn([this, bus, v] {
            if (bus == 0)
                project.sends.delayReturn = std::clamp(v, 0.0f, 1.5f);
            else
                project.sends.reverbReturn = std::clamp(v, 0.0f, 1.5f);
        });
    };
    mixer->onDelayTime = [this, mixOwn](float ms) {
        mixOwn([this, ms] { project.sends.delay.timeMs = std::clamp(ms, 20.0f, 1000.0f); });
    };
    mixer->onReverbSize = [this, mixOwn](float size) {
        mixOwn([this, size] { project.sends.reverb.size = std::clamp(size, 0.0f, 1.0f); });
    };
    mixer->onGestureBegin = [this] { beginEdit(); };
    mixer->onGestureEnd = [this] { endEdit(); };
    for (auto& button : trackButtons)
        addAndMakeVisible(button);
    addAndMakeVisible(addTrack);
    addAndMakeVisible(instrumentChoice);
    // The selector shows the current sound; clicking opens the browser.
    instrumentChoice.onOpen = [this] { showInstrumentBrowser(); };
    favorites.load(favoritesFile());
    instrumentChoice.setTooltip(engine.instrumentsAvailable()
        ? "Choose this track's sound: search instruments, synth patches and your own samples; star your favorites. "
          "Notes stay the same; TRACK effects shape the selected sound."
        : "Sound bank missing: reinstall GeneralUser-GS.sf2 to enable sampled instruments. The synth and sampler still work.");
    for (auto& tab : patternTabs)
        addAndMakeVisible(tab);
    fxBar = std::make_unique<FxBar>(
        [this](int target) { fxTarget = target; refreshFxBar(); },
        [this](int effect) { fxEffect = effect; refreshFxBar(); },
        [this](bool enabled) { beginEdit(); setFxEnabled(enabled); projectChanged(); endEdit(); },
        [this](int slot) { return getFxParam(slot); },
        [this](int slot, float value) { setFxParam(slot, value); projectChanged(); },
        [this] { beginEdit(); },
        [this] { endEdit(); });
    addAndMakeVisible(fxBar.get());
    kitPanel = std::make_unique<KitPanel>(
        [this](int pad) { loadPadSample(pad); },
        [this](int pad) { clearPadSample(pad); },
        [this](int pad) { auditionPad(pad); },
        [this] {
            if (kitPanel != nullptr)
                kitPanel->setVisible(false);
        },
        [this](int id) { loadPresetSelection(id); },
        [this] { saveKitAsPreset(); },
        [this] { deleteSelectedPreset(); });
    addAndMakeVisible(kitPanel.get());
    // NB: addAndMakeVisible shows the component, so hide it afterwards.
    kitPanel->setVisible(false);
    synthPanel = std::make_unique<SynthPanel>(
        [this](const SynthParams& params) {
            auto& track = project.tracks[static_cast<std::size_t>(selectedTrack)];
            if (track.kind != TrackKind::Synth || track.synth == params)
                return;
            // Wheel/double-click edits arrive without a drag: own the undo step.
            const bool ownGesture = !editing;
            if (ownGesture) beginEdit();
            track.synth = params;
            projectChanged();
            if (ownGesture) endEdit();
        },
        [this] { beginEdit(); },
        [this] { endEdit(); },
        [this](int patch) { applySynthPatch(patch); },
        [this] { synthPanel->setVisible(false); });
    addAndMakeVisible(synthPanel.get());
    synthPanel->setVisible(false);
    samplerPanel = std::make_unique<SamplerPanel>(
        [this](const SamplerParams& params) {
            auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
            if (track.kind != TrackKind::Synth || !isSamplerInstrument(track.instrumentPreset)
                || track.sampler == params || !params.valid())
                return;
            // Wheel/double-click edits arrive without a drag: own the undo step.
            const bool ownGesture = !editing;
            if (ownGesture) beginEdit();
            track.sampler = params;
            projectChanged();
            if (ownGesture) endEdit();
        },
        [this] { beginEdit(); },
        [this] { endEdit(); },
        [this] { addSoundsToSampler(); },
        [this](const juce::String& name) { useSample(name); },
        [] {
            const auto folder = sampleLibraryDir();
            folder.createDirectory();
            folder.startAsProcess();
        },
        [this] { detectSamplerRoot(); },
        [this] { samplerPanel->setVisible(false); });
    addAndMakeVisible(samplerPanel.get());
    samplerPanel->setVisible(false);
    addAndMakeVisible(editSynth);
    editSynth.setWantsKeyboardFocus(false);
    editSynth.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    editSynth.setTooltip("Open this track's sound editor: the synth engine (waveforms, filter, envelopes, LFO, drive, chorus, patches) or the sampler (trim, loop, root key, envelope).");
    editSynth.onClick = [this] { toggleEditorPanel(); };
    kitButton.onClick = [this] {
        if (kitPanel == nullptr)
            return;
        kitPanel->setVisible(!kitPanel->isVisible());
        if (kitPanel->isVisible())
        {
            refreshKitPanel();
            kitPanel->toFront(false);
        }
    };
    kitButton.setTooltip("Per-pad drum samples: load custom WAV/AIFF/FLAC/MP3/OGG or restore starters.");
    exportButton.setColour(juce::TextButton::buttonOnColourId, ui::violet);
    exportButton.onClick = [this] { exportAudio(); };
    exportButton.setTooltip("Bounce the loop or song to a stereo WAV file (Ctrl+E).");
    themeButton.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    themeButton.onClick = [this] {
        // Shift-click returns to the system (Omarchy) theme.
        if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
            themeMode = omarchy::ThemeMode::System;
        else if (themeMode == omarchy::ThemeMode::Light
                 || (themeMode == omarchy::ThemeMode::System && !ui::uiDark))
            themeMode = omarchy::ThemeMode::Dark;
        else
            themeMode = omarchy::ThemeMode::Light;
        saveUiSettings();
        applyOmarchyTheme(true);
    };

    // Keep transport shortcuts focused on the editor after toolbar clicks.
    for (auto* button : std::initializer_list<juce::Button*> {
             &play, &stop, &record, &ideaButton, &panic, &audioSettings, &undo, &redo,
             &newProject, &open, &save, &saveAs, &exportButton, &clear, &demo, &duplicatePattern,
             &audioTab, &mute, &solo, &repeatBar, &kitButton,
             &loopView, &songView, &mixView, &partTrackOn, &themeButton, &grooveButton,
             &keyButton, &chordButton, &arpButton, &rampButton, &autoButton })
        button->setWantsKeyboardFocus(false);
    for (int i = 0; i < numPatterns; ++i)
    {
        auto& tab = patternTabs[static_cast<std::size_t>(i)];
        tab.setButtonText(juce::String::charToString(static_cast<char>('A' + i)));
        tab.setClickingTogglesState(true);
        tab.setWantsKeyboardFocus(false);
        tab.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
        tab.onClick = [this, i] { selectPattern(i); };
        tab.setTooltip("Edit pattern slot. Loop mode previews the selected slot.");
    }
    grooveButton.setTooltip("Groove for the selected track: swing playback, quantize or humanize the loop (undoable).");
    keyButton.setTooltip("Song key and scale. New notes snap to the key; the AI writes in key too.");
    keyButton.onClick = [this] {
        auto panel = std::make_unique<KeyPanel>(
            [this](int key, int scale) {
                beginEdit();
                project.musicKey = std::clamp(key, 0, 11);
                project.musicScale = static_cast<MusicScale>(std::clamp(
                    scale, 0, static_cast<int>(MusicScale::numScales) - 1));
                refreshKeyButton();
                projectChanged();
                endEdit();
            },
            [this](bool snap) {
                snapScale = snap;
                pianoRoll.setScale(project.musicKey, project.musicScale, snap);
            });
        panel->refresh(project.musicKey, static_cast<int>(project.musicScale), snapScale);
        juce::CallOutBox::launchAsynchronously(std::move(panel), keyButton.getScreenBounds(), this);
    };
    chordButton.setClickingTogglesState(true);
    chordButton.setTooltip("One-finger chords: pick a type, then click the piano roll to stamp it. Click again to disarm.");
    chordButton.onClick = [this] {
        if (pianoRoll.isChordArmed())
        {
            pianoRoll.setChordArmed(false, ChordType::Major);
            chordButton.setToggleState(false, juce::dontSendNotification);
            chordButton.setButtonText("Chord");
            return;
        }
        auto panel = std::make_unique<ChordPanel>([this](int chord) {
            const auto type = static_cast<ChordType>(std::clamp(
                chord, 0, static_cast<int>(ChordType::numChords) - 1));
            pianoRoll.setChordArmed(true, type);
            chordButton.setToggleState(true, juce::dontSendNotification);
            chordButton.setButtonText(juce::String(chordName(type)));
        });
        panel->refresh(pianoRoll.isChordArmed());
        juce::CallOutBox::launchAsynchronously(std::move(panel), chordButton.getScreenBounds(), this);
    };
    rampButton.setTooltip("Velocity ramp: fade the loop's velocities from quiet to loud (undoable).");
    rampButton.onClick = [this] {
        auto panel = std::make_unique<RampPanel>([this](int startVel, int endVel) {
            applyVelocityRampToSelected(startVel, endVel);
        });
        juce::CallOutBox::launchAsynchronously(std::move(panel), rampButton.getScreenBounds(), this);
    };
    arpButton.setTooltip("Arpeggiator and live chords for MiniLab playing on this track (undoable).");
    arpButton.onClick = [this] {
        const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
        auto panel = std::make_unique<ArpPanel>([this](LiveFx fx) {
            const auto target = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
            auto& track = project.tracks[target];
            if (track.kind != TrackKind::Synth || audioSelected)
                return;
            beginEdit();
            track.liveFx = fx;
            refreshArpButton();
            projectChanged();
            endEdit();
        });
        panel->refresh(project.tracks[sel].liveFx);
        juce::CallOutBox::launchAsynchronously(std::move(panel), arpButton.getScreenBounds(), this);
    };
    automationLane = std::make_unique<AutomationView>();
    addChildComponent(automationLane.get());
    automationLane->onTargetChange = [this](AutomationTarget target) {
        automationTarget = target < AutomationTarget::numTargets ? target : AutomationTarget::Volume;
        refreshAutomation();
    };
    automationLane->onLaneChange = [this](AutomationLane lane) {
        const auto sel = std::clamp(selectedTrack, 0, maxTracks - 1);
        auto& track = project.tracks[static_cast<std::size_t>(sel)];
        if (track.kind == TrackKind::None || audioSelected)
            return;
        const int slot = std::clamp(drumsSelected ? trackDrumSlot[static_cast<std::size_t>(sel)]
                                                  : trackMelodySlot[static_cast<std::size_t>(sel)],
                                    0, numPatterns - 1);
        const bool own = !editing;
        if (own)
            beginEdit();
        track.automation[static_cast<std::size_t>(slot)][static_cast<std::size_t>(automationTarget)] = lane;
        projectChanged();
        if (own)
            endEdit();
    };
    automationLane->onGestureBegin = [this] { beginEdit(); };
    automationLane->onGestureEnd = [this] { endEdit(); };
    autoButton.setTooltip("Automation lanes: draw volume, pan, send, and FX curves for this loop (undoable).");
    autoButton.onClick = [this] {
        automationVisible = !automationVisible;
        updateSongControls();
        resized();
        repaint();
    };
    grooveButton.onClick = [this] {
        auto panel = std::make_unique<GroovePanel>(
            [this](float value) { setTrackSwing(value); },
            [this] { beginEdit(); },
            [this] { endEdit(); },
            [this](float strength) { applyQuantize(strength); },
            [this](float amount) { applyHumanize(amount); });
        const auto sel = static_cast<std::size_t>(selectedTrack);
        panel->refresh(project.tracks[sel].swing, drumsSelected);
        juce::CallOutBox::launchAsynchronously(std::move(panel), grooveButton.getScreenBounds(), this);
    };
    duplicatePattern.setTooltip("Copy the selected pattern into the next slot and edit it (undoable).");
    duplicatePattern.onClick = [this] {
        beginEdit();
        const auto sel = static_cast<std::size_t>(selectedTrack);
        if (drumsSelected)
        {
            const int next = (trackDrumSlot[sel] + 1) % numPatterns;
            project.tracks[sel].drumPatterns[static_cast<std::size_t>(next)] = project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])];
            trackDrumSlot[sel] = next;
        }
        else
        {
            const int next = (trackMelodySlot[sel] + 1) % numPatterns;
            project.tracks[sel].melodies[static_cast<std::size_t>(next)] = project.tracks[sel].melodies[static_cast<std::size_t>(trackMelodySlot[sel])];
            trackMelodySlot[sel] = next;
        }
        // Working on a song part: that part now uses the new copy.
        if (editPart >= 0 && editPart < project.song.sections)
            project.song.slots[static_cast<std::size_t>(editPart)][sel] = static_cast<std::uint8_t>(
                drumsSelected ? trackDrumSlot[sel] : trackMelodySlot[sel]);
        projectChanged();
        endEdit();
    };
    for (auto* button : { &loopView, &songView, &mixView })
    {
        button->setClickingTogglesState(false);
        button->setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    }
    loopView.onClick = [this] { setSongView(false); };
    songView.onClick = [this] { setSongView(true); };
    mixView.onClick = [this] {
        if (audioSelected)
            selectChannel(selectedTrack);
        setMixerView(!mixerVisible);
    };
    mixView.setTooltip("Mixer: track faders, pan, mute/solo, and the delay/reverb sends. Playback keeps running.");
    loopView.setTooltip("Loop view: write and edit 4-bar patterns. Playback repeats the loop.");
    songView.setTooltip("Song view: arrange your loops into a song (intro, verse, chorus...). Playback plays the song.");
    songTemplate.setTextWhenNothingSelected("Song structure...");
    for (int i = 0; i < static_cast<int>(SongTemplate::numTemplates); ++i)
        songTemplate.addItem(songTemplateName(static_cast<SongTemplate>(i)), i + 1);
    songTemplate.setTooltip("Build a whole song structure in one step from your loops (undoable).");
    songTemplate.onChange = [this] {
        const int id = songTemplate.getSelectedId();
        if (id <= 0)
            return;
        const auto tpl = static_cast<SongTemplate>(id - 1);
        beginEdit();
        project.song = buildSongFromTemplate(project, tpl);
        songStartPart = 0;
        editPart = -1;
        engine.stop();
        projectChanged();
        endEdit();
        songTemplate.setSelectedId(0, juce::dontSendNotification);
        status.setText("Song structure built: verses use loop A, choruses/drops B, bridges/builds C. "
                       "Write those loops in Loop view; Undo restores the old arrangement.",
                       juce::dontSendNotification);
    };
    partChoice.setTooltip("Pick the song part you are working on: the loop plays exactly that part, "
                          "and pattern tabs choose which loop this track uses in it.");
    partChoice.onChange = [this] {
        const int id = partChoice.getSelectedId();
        editPart = id >= 2 ? id - 2 : -1;
        projectChanged();
    };
    partTrackOn.setClickingTogglesState(true);
    partTrackOn.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    partTrackOn.setTooltip("Whether the selected track plays in this song part.");
    partTrackOn.onClick = [this] {
        if (editPart < 0 || editPart >= project.song.sections)
            return;
        beginEdit();
        project.song.trackOn[static_cast<std::size_t>(editPart)][static_cast<std::size_t>(selectedTrack)]
            = partTrackOn.getToggleState();
        projectChanged();
        endEdit();
    };
    partHint.setFont(ui::font(11.0f));
    partHint.setColour(juce::Label::textColourId, ui::muted);

    constexpr char padKeys[] = "QWERASDF";
    for (int pad = 0; pad < drumPads; ++pad)
    {
        auto& button = padButtons[static_cast<std::size_t>(pad)];
        addAndMakeVisible(button);
        button.setButtonText(juce::String(drumNames[static_cast<std::size_t>(pad)]).toUpperCase());
        button.getProperties().set("role", "pad");
        button.getProperties().set("shortcut", juce::String::charToString(padKeys[pad]));
        button.setWantsKeyboardFocus(false);
        button.setTriggeredOnMouseDown(true);
        button.onClick = [this, pad] { auditionPad(pad); };
        button.setColour(juce::TextButton::buttonOnColourId, pad < 2 ? ui::violet : ui::blue);
    }
    for (int track = 0; track < maxTracks; ++track)
    {
        auto& button = trackButtons[static_cast<std::size_t>(track)];
        button.getProperties().set("role", "trackCompact");
        button.setTooltip("Click to select; right-click for track options. Drag to reorder (or Alt+Up/Down).");
        button.setClickingTogglesState(true);
        button.setWantsKeyboardFocus(false);
        button.onClick = [this, track] { selectTrackIndex(track); };
        button.addMouseListener(this, false);
    }
    addTrack.setButtonText("+ / Menu");
    addTrack.setTooltip("Add a synth or drum track (up to 8).");
    addTrack.setWantsKeyboardFocus(false);
    addTrack.onClick = [this] { showAddTrackMenu(); };
    audioTab.setTooltip("Record audio and manage recorded takes. Instrument tracks remain available above.");
    audioTab.getProperties().set("detail", "RECORDER / TAKES");
    audioTab.setColour(juce::TextButton::buttonOnColourId, ui::blue);
    record.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    record.setClickingTogglesState(true);
    record.setTooltip("Record: starts the song from the top (or punches in while playing). Press again to punch out, Stop to finish the take.");
    ideaButton.setClickingTogglesState(true);
    ideaButton.setColour(juce::TextButton::buttonOnColourId, ui::violet);
    ideaButton.setTooltip("Capture an idea, not a take. Shift+Record (MiniLab: Shift+Pad 7) starts; "
                          "Shift+Stop (Shift+Pad 5) finishes. Play keys/pads or hum into a mic; "
                          "Sonora fits the idea to four bars with the AI assistant.");
    play.getProperties().set("role", "primary");
    save.getProperties().set("role", "primary");
    panic.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    mute.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    solo.setColour(juce::TextButton::buttonOnColourId, ui::violet);
    audioTab.onClick = [this] { selectChannel(-1); };
    mute.setClickingTogglesState(true);
    solo.setClickingTogglesState(true);
    mute.onClick = [this] { beginEdit(); selectedMix().mute = mute.getToggleState(); projectChanged(); endEdit(); };
    solo.onClick = [this] { beginEdit(); selectedMix().solo = solo.getToggleState(); projectChanged(); endEdit(); };
    trackVolume.setRange(0, 150, 1);
    trackVolume.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    trackVolume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 25);
    trackVolume.setTextValueSuffix(" %");
    trackVolume.setTooltip("Selected track volume. Mute and Solo also apply to pad/keyboard audition.");
    trackVolume.onDragStart = [this] { beginEdit(); };
    trackVolume.onDragEnd = [this] { endEdit(); };
    trackVolume.onValueChange = [this] {
        const bool ownGesture = !editing;
        if (ownGesture) beginEdit();
        selectedMix().volume = static_cast<float>(trackVolume.getValue() / 100.0);
        projectChanged();
        if (ownGesture) endEdit();
    };

    tempo.setRange(40.0, 240.0, 1.0);
    tempo.setSliderStyle(juce::Slider::IncDecButtons);
    tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 85, 32);
    tempo.setTextValueSuffix(" BPM");
    tempo.setValue(project.bpm, juce::dontSendNotification);
    tempo.onValueChange = [this] {
        beginEdit();
        project.bpm = tempo.getValue();
        projectChanged();
        endEdit();
    };
    play.onClick = [this] {
        if (recording)
        {
            finalizeTake(); // punch out, keep playing
            return;
        }
        engine.setPlaying(!engine.isPlaying());
    };
    stop.onClick = [this] {
        if (ideaRecording.load())
            finishIdeaRecord();
        if (recording)
            finalizeTake();
        engine.stop();
        engine.keyboardState.allNotesOff(0);
    };
    record.onClick = [this] { toggleRecord(); };
    ideaButton.onClick = [this] { toggleIdeaRecord(); };
    panic.onClick = [this] { engine.stop(); engine.keyboardState.allNotesOff(0); engine.panic(); };
    audioSettings.onClick = [this] { openAudioSettings(); };
    undo.onClick = [this] { undoEdit(); };
    redo.onClick = [this] { redoEdit(); };
    newProject.onClick = [this] { confirmDiscard([this] { resetProject(); }); };
    open.onClick = [this] { confirmDiscard([this] { openProject(); }); };
    save.onClick = [this] { saveProject(false); };
    saveAs.onClick = [this] { saveProject(true); };
    clear.onClick = [this] {
        beginEdit();
        const auto sel = static_cast<std::size_t>(selectedTrack);
        if (drumsSelected) project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])] = {};
        else if (!audioSelected) project.tracks[sel].melodies[static_cast<std::size_t>(trackMelodySlot[sel])] = {};
        projectChanged();
        endEdit();
    };
    demo.setButtonText("AI assistant");
    demo.setClickingTogglesState(false);
    demo.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    demo.onClick = [this] { toggleAiSidebar(); };
    demo.setTooltip("Open the AI sidebar (Ctrl+I): chat with Claude to write or change this track's "
                    "melody or beat. It hears the other tracks in the section you're working on.");
    aiSidebar = std::make_unique<AiSidebar>();
    addChildComponent(aiSidebar.get());
    aiSidebar->onSend = [this](const juce::String& text) { sendToAssistant(text); };
    aiSidebar->onVoice = [this] { toggleDictation(); };
    aiSidebar->onCancel = [this] { if (assistantWorker != nullptr) assistantWorker->cancel.store(true); };
    aiSidebar->onClose = [this] { toggleAiSidebar(); };
    aiSidebar->onNewChat = [this] {
        if (assistantWorker != nullptr)
            return;
        chatHistory.clear();
        aiSidebar->clearMessages();
    };
    repeatBar.onClick = [this] {
        beginEdit();
        const auto sel = static_cast<std::size_t>(selectedTrack);
        for (auto& row : project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])].steps)
            for (int step = 16; step < gridSteps; ++step)
                row[static_cast<std::size_t>(step)] = row[static_cast<std::size_t>(step % 16)];
        projectChanged();
        endEdit();
    };
    repeatBar.setTooltip("Copy the first drum bar to bars 2-4. Undo restores the previous pattern.");
    play.setTooltip("Space: play / stop. Playback starts at bar one.");
    panic.setTooltip("Stop transport and silence all voices.");
    pianoRoll.onGestureBegin = [this] { beginEdit(); };
    pianoRoll.onPreview = [this](const Pattern& value) {
        project.tracks[static_cast<std::size_t>(selectedTrack)].melodies[static_cast<std::size_t>(trackMelodySlot[static_cast<std::size_t>(selectedTrack)])] = value;
        projectChanged();
    };
    pianoRoll.onGestureEnd = [this] { endEdit(); };
    drumSequencer.onGestureBegin = [this] { beginEdit(); };
    drumSequencer.onPreview = [this](const DrumPattern& value) {
        project.tracks[static_cast<std::size_t>(selectedTrack)].drumPatterns[static_cast<std::size_t>(trackDrumSlot[static_cast<std::size_t>(selectedTrack)])] = value;
        projectChanged();
    };
    drumSequencer.onGestureEnd = [this] { endEdit(); };
    drumSequencer.onAudition = [this](int pad) { auditionPad(pad); };
    audioView = std::make_unique<AudioView>(
        [this](int mode) { inputMode = mode; },
        [this](bool monitor) {
            if (monitor)
                ensureAudioInputs();
            monitorInputs = monitor;
            if (!monitor) inputPeak.store(0.0f);
        },
        [this](std::uint32_t id) {
            beginEdit();
            for (int i = 0; i < project.takeCount; ++i)
            {
                auto& take = project.takes[static_cast<std::size_t>(i)];
                if (take.id == id)
                    take.mute = !take.mute;
            }
            projectChanged();
            endEdit();
        },
        [this](std::uint32_t id) { deleteTake(id); },
        [this](std::uint32_t id) {
            selectedTake = id;
            rebuildWaveCache();
            refreshAudioView();
            refreshPitchDisplay();
            analyzeTake(id);
        },
        [this] {
            if (audioView == nullptr)
                return;
            pitchSettings.pitchScale.key = audioView->getPitchKey();
            pitchSettings.pitchScale.scale = audioView->getPitchScale();
            pitchSettings.amount = audioView->getPitchAmount();
            pitchSettings.speedMs = audioView->getPitchSpeed();
            if (pitchSettings.valid())
                refreshPitchDisplay();
        },
        [this] { analyzeTake(selectedTake); },
        [this] { applyPitch(); },
        [this](std::uint32_t id) {
            beginEdit();
            for (int i = 0; i < project.takeCount; ++i)
            {
                auto& take = project.takes[static_cast<std::size_t>(i)];
                if (take.id == id)
                    take.solo = !take.solo;
            }
            projectChanged();
            endEdit();
        },
        [this](std::uint32_t id) {
            beginEdit();
            for (int i = 0; i < project.takeCount; ++i)
            {
                auto& take = project.takes[static_cast<std::size_t>(i)];
                take.solo = false;
                take.mute = take.id != id;
            }
            projectChanged();
            endEdit();
        },
        [this](std::uint32_t id, float ratio) {
            const float clamped = std::clamp(ratio, 0.5f, 2.0f);
            const bool own = !editing;
            if (own)
                beginEdit();
            for (int i = 0; i < project.takeCount; ++i)
            {
                auto& take = project.takes[static_cast<std::size_t>(i)];
                if (take.id == id)
                    take.stretch = clamped;
            }
            projectChanged();
            if (own)
            {
                endEdit();
                refreshTakes();
            }
        },
        [this] { beginEdit(); },
        [this] {
            endEdit();
            refreshTakes();
        },
        [this](std::uint32_t id) { sendTakeToSampler(id); });
    addAndMakeVisible(audioView.get());
    keyboard.setAvailableRange(lowestPitch, highestPitch);
    keyboard.setLowestVisibleKey(36);
    keyboard.setKeyWidth(34.0f);
    refreshKeyboardColours();
#if defined(__APPLE__)
    recoveryFile = sonora::sonoraSupportDir().getChildFile("recovery.sonora.json");
#else
    auto stateRoot = juce::SystemStats::getEnvironmentVariable("XDG_STATE_HOME", {});
    if (stateRoot.isEmpty() || !juce::File::isAbsolutePath(stateRoot))
        stateRoot = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                        .getChildFile(".local/state").getFullPathName();
    recoveryFile = juce::File(stateRoot).getChildFile("sonora/recovery.sonora.json");
#endif
    setSize(1440, 900);
    loadUiSettings();
    applyOmarchyTheme(true); // theme first paint matches the desktop
    projectChanged();
    selectChannel(0);
    // Restore the previous audio/MIDI setup before opening channels; a
    // missing or stale file falls back to fresh defaults silently.
    std::unique_ptr<juce::XmlElement> savedAudio;
    if (audioSettingsFile().existsAsFile())
        savedAudio = juce::XmlDocument::parse(audioSettingsFile());
#if defined(__APPLE__)
    // macOS: never restore an input at launch. A saved input on a different
    // device than the output (hearing aids out + MacBook mic in) makes JUCE run
    // both through its device combiner: ~300 ms of extra FIFO latency, plus
    // clock drift between the two devices that garbles playback. A same-device
    // input on Bluetooth hearing aids forces the 8 kHz headset profile instead.
    // Inputs still open on demand (Audio tab, monitoring, REC).
    if (savedAudio != nullptr)
    {
        if (savedAudio->hasAttribute("audioDeviceName"))
        {
            savedAudio->setAttribute("audioOutputDeviceName", savedAudio->getStringAttribute("audioDeviceName"));
            savedAudio->removeAttribute("audioDeviceName");
        }
        savedAudio->removeAttribute("audioInputDeviceName");
        savedAudio->removeAttribute("audioDeviceInChans");
        deviceManager.initialise(0, 2, savedAudio.get(), true);
    }
#else
    if (savedAudio != nullptr)
        deviceManager.initialise(2, 2, savedAudio.get(), true);
#endif
    deviceManager.addChangeListener(this);
    // Output-only by default: requesting inputs at startup would wake
    // Bluetooth headset mics (hearing aids) and force the whole system into a
    // low-quality bidirectional profile. Inputs open on demand when the user
    // opens the Audio tab, enables monitoring, or presses REC — unless a
    // restored setup already had them.
    setAudioChannels(activeInputCount(deviceManager) > 0 ? 2 : 0, 2);
    // Registered after the player's callback, so outputs already hold the
    // engine mix when our input tap runs on the same audio thread.
    deviceManager.addAudioCallback(this);
    // One router for all MIDI inputs; auto-connect enables hardware below.
    deviceManager.addMidiInputDeviceCallback({}, this);
    autoConnectMidi();
    for (auto& value : knobValues)
        value.store(-1);
    appliedKnobValues.fill(-1);
    startTimerHz(30);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr)
            safe->offerRecovery();
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (dictationRecording.exchange(false))
        dictationRecorder.stop();
    dictationFile.deleteFile();
    if (ideaRecording.exchange(false) && ideaHasAudio)
        ideaRecorder.stop();
    ideaFile.deleteFile();
    if (miniLabOut != nullptr)
        miniLabSend(minilab::disconnectDaw());
    if (miniLabSender != nullptr)
        miniLabSender->shutdown();
    {
        const juce::ScopedLock lock(miniLabOutLock);
        miniLabOut.reset();
    }
    if (assistantWorker != nullptr)
    {
        assistantWorker->cancel.store(true);
        assistantWorker->stopThread(5000);
        assistantWorker.reset();
    }
    for (auto* worker : { &takeLoadWorker, &bankLoadWorker, &sampleLoadWorker })
        if (worker->get() != nullptr)
        {
            (*worker)->cancel.store(true);
            (*worker)->signalThreadShouldExit();
            (*worker)->stopThread(5000);
            worker->reset();
        }
    delete audioDialog.getComponent();
    chooser.reset();
    if (recording)
        finalizeTake();
    deviceManager.removeAudioCallback(this);
    deviceManager.removeMidiInputDeviceCallback({}, this);
    deviceManager.removeChangeListener(this);
    saveAudioSettings();
    shutdownAudio();
    engine.retireTakeSet(nullptr);
    takeStorage.reset();
    for (int track = 0; track < maxTracks; ++track)
        engine.retirePadBank(track, nullptr);
    for (auto& bank : bankStorage)
        bank.reset();
    for (int track = 0; track < maxTracks; ++track)
        engine.retireSampleData(track, nullptr);
    for (auto& sample : sampleStorage)
        sample.reset();
    setLookAndFeel(nullptr);
}

void MainComponent::prepareToPlay(int, double sampleRate)
{
    engine.prepare(sampleRate);
    takesRate = 0.0; // device rate may have changed; reload takes on the timer
}
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& block) { engine.process(block); }

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&)
{
    if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0)
        return;
    // The player's callback runs first and leaves the engine mix in the
    // outputs; this tap only observes inputs and optionally adds monitoring.
    const float* sources[2] = { nullptr, nullptr };
    int mapped = 0;
    if (inputChannelData != nullptr && numInputChannels > 0
        && (monitorInputs || recording || ideaRecording.load() || dictationRecording.load()))
    {
        if (inputMode == 2 && numInputChannels > 1)
        {
            sources[0] = inputChannelData[0];
            sources[1] = inputChannelData[1];
            mapped = 2;
        }
        else
        {
            const int channel = (inputMode == 1 && numInputChannels > 1) ? 1 : 0;
            sources[0] = inputChannelData[channel];
            mapped = 1;
        }
        float peak = 0.0f;
        for (int ch = 0; ch < mapped; ++ch)
            for (int i = 0; i < numSamples; ++i)
                peak = std::max(peak, std::abs(sources[ch][i]));
        // Only raise the meter here; the message-thread timer applies decay.
        float previous = inputPeak.load(std::memory_order_relaxed);
        while (peak > previous && !inputPeak.compare_exchange_weak(previous, peak))
        {
        }
        if (recording)
            recorder.push(sources, mapped, numSamples);
        if (ideaRecording.load(std::memory_order_relaxed) && ideaHasAudio)
            ideaRecorder.push(sources, 1, numSamples);
        if (dictationRecording.load(std::memory_order_relaxed))
            dictationRecorder.push(sources, 1, numSamples);
    }
    else
    {
        inputPeak.store(0.0f, std::memory_order_relaxed);
    }
    // Software monitoring joins the finished mix (through the limiter).
    if (mapped > 0 && monitorInputs)
    {
        juce::AudioBuffer<float> outputs(outputChannelData, numOutputChannels, numSamples);
        for (int i = 0; i < numSamples; ++i)
            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                const float dry = mapped == 2 ? sources[channel % 2][i] : sources[0][i];
                outputs.addSample(channel, i, dry * 0.5f);
            }
    }
}
void MainComponent::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    lastMidiMillis.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    const bool fromMcu = source != nullptr && mcuDeviceIds.contains(source->getIdentifier());
    if (fromMcu)
    {
        // MCU ports speak Mackie Control, not notes: only the four transport
        // notes act, everything else is swallowed so faders/encoders never
        // trigger the synth. Transport runs on the message thread.
        const auto action = midi::mcuTransportAction(message.getNoteNumber(),
            message.isNoteOn() && message.getVelocity() > 0);
        if (action == midi::McuAction::None)
            return;
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<MainComponent>(this), action] {
                if (safe == nullptr)
                    return;
                switch (action)
                {
                    case midi::McuAction::Play:
                        if (!safe->engine.isPlaying())
                            safe->engine.setPlaying(true);
                        break;
                    case midi::McuAction::Stop:
                        if (safe->ideaRecording.load())
                        {
                            safe->finishIdeaRecord();
                            break;
                        }
                        if (safe->recording)
                            safe->finalizeTake();
                        safe->engine.stop();
                        safe->engine.keyboardState.allNotesOff(0);
                        break;
                    // MiniLab transport Record is Shift+Pad 7: idea capture.
                    // The on-screen REC button is deliberately independent.
                    case midi::McuAction::Record: safe->toggleIdeaRecord(); break;
                    case midi::McuAction::ToggleLoop:
                        safe->setSongView(!safe->project.songMode);
                        break;
                    case midi::McuAction::None: break;
                }
            });
        return;
    }
    if (message.isSysEx())
    {
        minilab::Bytes bytes(message.getRawData(), message.getRawData() + message.getRawDataSize());
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this), bytes] {
            if (safe != nullptr)
                safe->handleMiniLabSysex(bytes);
        });
        return;
    }
    if (ideaRecording.load(std::memory_order_acquire) && message.isNoteOnOrOff())
    {
        const auto seconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - ideaStarted;
        if (seconds >= 0 && seconds <= 65.0)
        {
            const juce::ScopedLock lock(ideaMutex);
            if (ideaEvents.size() < 4096)
                ideaEvents.push_back({ seconds, message.getNoteNumber(), message.getVelocity(), message.getChannel(),
                                       message.isNoteOn() && message.getVelocity() > 0 });
        }
    }
    // MiniLab encoders drive the selected instrument's effect knobs instead
    // of reaching the synth (keeps CC 74/71 from also altering sampled voices).
    if (message.isController() && message.getChannel() != 10)
    {
        const int knob = knobIndexForController(message.getControllerNumber());
        if (knob >= 0)
        {
            knobValues[static_cast<std::size_t>(knob)].store(message.getControllerValue(), std::memory_order_relaxed);
            knobSerial.fetch_add(1, std::memory_order_release);
            return;
        }
    }
    engine.midiCollector.handleIncomingMidiMessage(source, message);
}

void MainComponent::applyKnobChanges()
{
    const auto t = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    auto& track = project.tracks[t];
    if (track.kind == TrackKind::None)
        return;
    const auto map = knobMapFor(track);
    int turned = -1;
    for (int knob = 0; knob < 8; ++knob)
    {
        const int value = knobValues[static_cast<std::size_t>(knob)].load(std::memory_order_relaxed);
        if (value < 0 || value == appliedKnobValues[static_cast<std::size_t>(knob)])
            continue;
        appliedKnobValues[static_cast<std::size_t>(knob)] = value;
        if (turned < 0 && !knobGesture && !editing)
        {
            beginEdit();
            knobGesture = true;
        }
        applyKnob(track, map[static_cast<std::size_t>(knob)].target, static_cast<float>(value) / 127.0f);
        turned = knob;
    }
    if (turned < 0)
        return;
    announceKnobTurn(track, turned);
}

void MainComponent::announceKnobTurn(Track& track, int turned)
{
    const auto map = knobMapFor(track);
    const auto now = juce::Time::getMillisecondCounter();
    knobIdleUntil = now + 600;
    knobHighlightUntil = now + 1500;
    knobScreenUntil = now + 2000;
    lastKnob = turned;
    refreshMiniLabDisplay(false);
    projectChanged();
    if (synthPanel != nullptr && synthPanel->isVisible())
        refreshSynthPanel();
    status.setText("Knob " + juce::String(turned + 1) + "  /  " + track.trackName() + "  /  "
                   + map[static_cast<std::size_t>(turned)].label + " "
                   + knobValueText(track, map[static_cast<std::size_t>(turned)].target),
                   juce::dontSendNotification);
    repaint(knobStripArea());
}

void MainComponent::selectKnob(int knob)
{
    selectedKnob = std::clamp(knob, 0, 7);
    const auto t = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    auto& track = project.tracks[t];
    if (track.kind == TrackKind::None)
    {
        status.setText("Knob " + juce::String(selectedKnob + 1) + "  /  no track", juce::dontSendNotification);
        repaint(knobStripArea());
        return;
    }
    const auto map = knobMapFor(track);
    lastKnob = selectedKnob;
    knobHighlightUntil = juce::Time::getMillisecondCounter() + 1500;
    status.setText("Knob " + juce::String(selectedKnob + 1) + "  /  " + track.trackName() + "  /  "
                   + map[static_cast<std::size_t>(selectedKnob)].label + " "
                   + knobValueText(track, map[static_cast<std::size_t>(selectedKnob)].target),
                   juce::dontSendNotification);
    repaint(knobStripArea());
}

void MainComponent::nudgeKnob(int direction)
{
    const auto t = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    auto& track = project.tracks[t];
    if (track.kind == TrackKind::None)
        return;
    const auto map = knobMapFor(track);
    const auto target = map[static_cast<std::size_t>(selectedKnob)].target;
    const float stepped = juce::jlimit(0.0f, 1.0f,
        knobPosition(track, target) + 0.04f * static_cast<float>(direction));
    if (!knobGesture && !editing)
    {
        beginEdit();
        knobGesture = true;
    }
    applyKnob(track, target, stepped);
    announceKnobTurn(track, selectedKnob);
}

juce::Rectangle<int> MainComponent::knobStripArea() const
{
    return { 540, getHeight() - 172, contentWidth() - 580, 30 };
}

juce::Rectangle<float> MainComponent::knobChipRect(int knob) const
{
    const auto area = knobStripArea();
    const float chip = static_cast<float>(area.getWidth()) / 8.0f;
    return { static_cast<float>(area.getX()) + chip * static_cast<float>(knob),
             static_cast<float>(area.getY()) + 4.0f, chip - 6.0f, 22.0f };
}

void MainComponent::paintKnobStrip(juce::Graphics& g)
{
    if (audioSelected)
        return;
    const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    if (track.kind == TrackKind::None)
        return;
    const auto area = knobStripArea();
    const auto map = knobMapFor(track);
    const bool highlight = juce::Time::getMillisecondCounter() < knobHighlightUntil;
    const auto accent = trackColour(track.icon);
    ui::caption(g, "KNOBS", { area.getX() - 50, area.getY() + 3, 46, 20 }, ui::muted, 9.0f);
    for (int knob = 0; knob < 8; ++knob)
    {
        const auto& slot = map[static_cast<std::size_t>(knob)];
        const auto cell = knobChipRect(knob);
        const bool hot = highlight && knob == lastKnob;
        if (hot)
            ui::glow(g, cell, accent, 5.0f, 0.30f);
        g.setColour((hot ? accent.withAlpha(0.18f) : ui::raised.withAlpha(0.6f)));
        g.fillRoundedRectangle(cell, 5.0f);
        if (knob == selectedKnob)
        {
            g.setColour(accent.withAlpha(0.9f));
            g.drawRoundedRectangle(cell, 5.0f, 1.5f);
        }
        const float knobAt = juce::jlimit(0.0f, 1.0f, knobPosition(track, slot.target));
        g.setColour(accent.withAlpha(hot ? 0.95f : 0.55f));
        g.fillRoundedRectangle(cell.getX() + 4.0f, cell.getBottom() - 4.0f, (cell.getWidth() - 8.0f) * knobAt, 2.0f, 1.0f);
        juce::String text = juce::String(knob + 1) + " " + juce::String(slot.label).toUpperCase();
        if (cell.getWidth() > 118.0f)
            text += "  " + knobValueText(track, slot.target);
        ui::caption(g, text, cell.reduced(6.0f, 1.0f).withTrimmedBottom(4.0f).toNearestInt(),
                    hot ? ui::text : ui::muted, 8.5f);
    }
}

void MainComponent::autoConnectMidi()
{
    std::vector<midi::MidiPort> available;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        available.push_back({ device.identifier, device.name });
    std::vector<juce::String> enabled;
    for (const auto& device : available)
        if (deviceManager.isMidiInputDeviceEnabled(device.identifier))
            enabled.push_back(device.identifier);
    const auto result = midi::midiAutoConnect(available, enabled);
    for (const auto& id : result.enableIds)
        deviceManager.setMidiInputDeviceEnabled(id, true);
    mcuDeviceIds.clear();
    for (const auto& id : result.mcuIds)
        mcuDeviceIds.add(id);
    // The Arturia pad map follows MiniLab presence among enabled inputs.
    if (result.miniLabPresent != lastMiniLab)
    {
        lastMiniLab = result.miniLabPresent;
        engine.setArturiaPadMap(result.miniLabPresent);
    }
    std::vector<midi::MidiPort> active;
    for (const auto& device : available)
        if (deviceManager.isMidiInputDeviceEnabled(device.identifier))
            active.push_back(device);
    openMiniLabOutput();
    const auto summary = midi::midiStatusText(active);
    if (summary != midiStatusText)
    {
        midiStatusText = summary;
        updateTrackControls(); // status bar picks the new text up this tick
    }
}

void MainComponent::openMiniLabOutput()
{
    juce::String wanted;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
    {
        const auto name = device.name.toLowerCase();
        if (name.contains("minilab") && name.contains("midi") && !name.contains("thru")
            && !name.contains("mcu") && !name.contains("alv"))
            wanted = device.identifier;
    }
    if (wanted == miniLabOutId)
        return;
    if (miniLabOut != nullptr)
        miniLabSend(minilab::disconnectDaw());
    {
        const juce::ScopedLock lock(miniLabOutLock);
        miniLabOut.reset();
    }
    miniLabOutId = wanted;
    miniLabMode = MiniLabMode::Unknown;
    miniLabTop.clear();
    miniLabBottom.clear();
    miniLabPadRgb = 0;
    if (wanted.isEmpty())
        return;
    {
        const juce::ScopedLock lock(miniLabOutLock);
        miniLabOut = juce::MidiOutput::openDevice(wanted);
    }
    // Identify first; the reply triggers the DAW connect handshake.
    if (miniLabOut != nullptr)
        miniLabSend(minilab::deviceInquiry());
}

void MainComponent::miniLabSend(const minilab::Bytes& bytes)
{
    if (miniLabOut == nullptr || miniLabSender == nullptr)
        return;
    miniLabSender->post(bytes);
}

void MainComponent::sendMiniLabNow(const minilab::Bytes& bytes)
{
    if (bytes.size() < 2 || bytes.front() != 0xF0 || bytes.back() != 0xF7)
        return;
    const juce::ScopedLock lock(miniLabOutLock);
    if (miniLabOut != nullptr)
        miniLabOut->sendMessageNow(juce::MidiMessage::createSysExMessage(bytes.data() + 1,
                                                                        static_cast<int>(bytes.size()) - 2));
}

void MainComponent::handleMiniLabSysex(const minilab::Bytes& bytes)
{
    using minilab::Reply;
    switch (minilab::classify(bytes.data(), static_cast<int>(bytes.size())))
    {
        case Reply::DeviceIdentity:
        case Reply::DawModeChanged:
            miniLabSend(minilab::connectDaw());
            miniLabSend(minilab::requestPadBank());
            miniLabSend(minilab::requestMode());
            break;
        case Reply::DawMode:
            miniLabMode = MiniLabMode::Daw;
            refreshMiniLabDisplay(true);
            status.setText("MiniLab 3 connected in DAW mode: knob names now show on its screen.",
                           juce::dontSendNotification);
            break;
        case Reply::ArturiaMode:
        case Reply::ArturiaModeChanged:
            miniLabMode = MiniLabMode::Arturia;
            if (!miniLabHintShown)
            {
                miniLabHintShown = true;
                status.setText("Tip: press Shift + Pad 3 on the MiniLab (DAW mode) to see knob names on its screen.",
                               juce::dontSendNotification);
            }
            break;
        case Reply::PadBankA:
        case Reply::PadBankB:
            miniLabPadBank = minilab::classify(bytes.data(), static_cast<int>(bytes.size())) == Reply::PadBankB
                ? minilab::padBankB : minilab::padBankA;
            miniLabPadRgb = 0;
            refreshMiniLabDisplay(true);
            break;
        case Reply::OtherState:
        case Reply::None:
            break;
    }
}

void MainComponent::refreshMiniLabDisplay(bool force)
{
    if (miniLabOut == nullptr || miniLabMode != MiniLabMode::Daw)
        return;
    const auto now = juce::Time::getMillisecondCounter();
    // The MiniLab replaces idle host screens with its screensaver; replay
    // the current frame once a minute to keep it.
    if (now - miniLabRefreshedAt > 60000)
        force = true;
    const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
    std::pair<juce::String, juce::String> screen;
    if (audioSelected || track.kind == TrackKind::None)
        screen = { "AUDIO", "Recorder / takes" };
    else if (now < knobScreenUntil && lastKnob >= 0)
        screen = minilab::knobScreen(track, lastKnob);
    else
        screen = minilab::trackScreen(track);
    if (force || screen.first != miniLabTop || screen.second != miniLabBottom)
    {
        miniLabTop = screen.first;
        miniLabBottom = screen.second;
        miniLabSend(minilab::screenMessage(screen.first, screen.second));
    }
    const auto rgb = static_cast<std::uint32_t>(trackColour(track.icon).getARGB()) & 0xFFFFFFu;
    if (force || rgb != miniLabPadRgb)
    {
        miniLabPadRgb = rgb;
        miniLabSend(minilab::padBankMessage(minilab::padBankA, rgb, 0.35f));
        miniLabSend(minilab::padBankMessage(minilab::padBankB, rgb, 0.35f));
    }
    if (force)
        miniLabRefreshedAt = now;
}

void MainComponent::audioDeviceError(const juce::String& message)
{
    // Arrives on the audio thread: park the transport on the message thread.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<MainComponent>(this), message] {
            if (safe == nullptr)
                return;
            if (safe->recording)
                safe->finalizeTake();
            safe->engine.stop();
            safe->engine.keyboardState.allNotesOff(0);
            safe->audioErrorMessage = message;
        });
}

juce::File MainComponent::audioSettingsFile()
{
#if defined(__APPLE__)
    return sonora::sonoraSupportDir().getChildFile("audio.xml");
#else
    auto root = juce::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME", {});
    if (root.isEmpty() || !juce::File::isAbsolutePath(root))
        root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".config").getFullPathName();
    return juce::File(root).getChildFile("sonora/audio.xml");
#endif
}

juce::File MainComponent::uiSettingsFile()
{
    return audioSettingsFile().getSiblingFile("ui.json");
}

void MainComponent::loadUiSettings()
{
    const auto file = uiSettingsFile();
    if (!file.existsAsFile())
        return;
    themeMode = omarchy::themeModeFromString(juce::JSON::parse(file.loadFileAsString()).getProperty("theme", "system").toString());
}

void MainComponent::saveUiSettings()
{
    const auto file = uiSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText("{\"theme\":\"" + juce::String(omarchy::themeModeName(themeMode)) + "\"}\n");
}

void MainComponent::refreshThemeButton()
{
    // Glyph shows the current look; the tooltip names the action.
    const bool light = !ui::uiDark;
    themeButton.setButtonText(light ? juce::String::charToString(0x2600) : juce::String::charToString(0x263e));
    juce::String mode = themeMode == omarchy::ThemeMode::System ? "System (Omarchy)"
        : themeMode == omarchy::ThemeMode::Light ? "Light" : "Dark";
    themeButton.setTooltip("Theme: " + mode + ". Click: switch Light/Dark. Shift-click: follow the system theme.");
}

void MainComponent::saveAudioSettings()
{
    const auto file = audioSettingsFile();
    file.getParentDirectory().createDirectory();
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo(file);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        // A fresh successful setup clears a previous device error; every
        // setup change persists for the next launch.
        if (deviceManager.getCurrentAudioDevice() != nullptr)
            audioErrorMessage.clear();
        saveAudioSettings();
    }
}



void MainComponent::beginEdit()
{
    if (!editing)
    {
        editStart = project;
        editing = true;
    }
}

void MainComponent::endEdit()
{
    if (editing && !(editStart == project))
    {
        if (undoStack.size() >= 100)
            undoStack.erase(undoStack.begin());
        undoStack.push_back(editStart);
        redoStack.clear();
    }
    editing = false;
}

void MainComponent::refreshFxBar()
{
    if (fxBar != nullptr)
        fxBar->refresh(fxTarget, fxTarget == 2 ? 0 : fxEffect, getFxEnabled());
}

namespace
{
// fxEffect ids: 0 drive, 1 EQ, 2 compressor, 3 chorus, 4 delay, 5 reverb.
float* fxSlot(TrackFx& fx, int effect, int slot)
{
    switch (effect)
    {
        case 0: return slot == 0 ? &fx.drive.amount : &fx.drive.tone;
        case 1: return slot == 0 ? &fx.eq.low : slot == 1 ? &fx.eq.mid : slot == 2 ? &fx.eq.midFreq : &fx.eq.high;
        case 2: return slot == 0 ? &fx.comp.thresholdDb : slot == 1 ? &fx.comp.ratio
                     : slot == 2 ? &fx.comp.attackMs : &fx.comp.releaseMs;
        case 3: return slot == 0 ? &fx.chorus.rate : slot == 1 ? &fx.chorus.depth : &fx.chorus.mix;
        case 4: return slot == 0 ? &fx.delay.timeMs : slot == 1 ? &fx.delay.feedback : &fx.delay.mix;
        default: return slot == 0 ? &fx.reverb.size : slot == 1 ? &fx.reverb.damping : &fx.reverb.mix;
    }
}

bool* fxEnable(TrackFx& fx, int effect)
{
    switch (effect)
    {
        case 0: return &fx.drive.enabled;
        case 1: return &fx.eq.enabled;
        case 2: return &fx.comp.enabled;
        case 3: return &fx.chorus.enabled;
        case 4: return &fx.delay.enabled;
        default: return &fx.reverb.enabled;
    }
}
}

float MainComponent::getFxParam(int slot) const
{
    if (fxTarget == 2)
        return slot == 0 ? project.master.ceilingDb : project.master.releaseMs;
    auto fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    return *fxSlot(fx, fxEffect, slot);
}

void MainComponent::setFxParam(int slot, float value)
{
    if (fxTarget == 2)
    {
        if (slot == 0) project.master.ceilingDb = value;
        else project.master.releaseMs = value;
        return;
    }
    *fxSlot(project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx, fxEffect, slot) = value;
}

bool MainComponent::getFxEnabled() const
{
    if (fxTarget == 2)
        return project.master.enabled;
    auto fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    return *fxEnable(fx, fxEffect);
}

void MainComponent::setFxEnabled(bool enabled)
{
    if (fxTarget == 2)
    {
        project.master.enabled = enabled;
        return;
    }
    *fxEnable(project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx, fxEffect) = enabled;
}

void MainComponent::projectChanged()
{
    ++revision;
    if (project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::None)
        for (int track = 0; track < maxTracks; ++track)
            if (project.tracks[static_cast<std::size_t>(track)].kind != TrackKind::None)
            {
                selectedTrack = track;
                break;
            }
    if (editPart >= project.song.sections)
        editPart = -1;
    songStartPart = std::clamp(songStartPart, 0, project.song.sections - 1);
    unsigned loopMask = ~0u;
    if (editPart >= 0)
    {
        loopMask = 0;
        for (int track = 0; track < maxTracks; ++track)
        {
            const auto slot = project.song.slots[static_cast<std::size_t>(editPart)][static_cast<std::size_t>(track)];
            trackMelodySlot[static_cast<std::size_t>(track)] = slot;
            trackDrumSlot[static_cast<std::size_t>(track)] = slot;
            if (project.song.trackOn[static_cast<std::size_t>(editPart)][static_cast<std::size_t>(track)])
                loopMask |= 1u << track;
        }
    }
    engine.setLoopTrackMask(loopMask);
    engine.setSongStartSection(songStartPart);
    const auto sel = static_cast<std::size_t>(selectedTrack);
    const bool selDrums = project.tracks[sel].kind == TrackKind::Drums;
    instrumentChoice.setVisible(!audioSelected && project.tracks[sel].kind == TrackKind::Synth);
    const bool editableSynth = !audioSelected && project.tracks[sel].kind == TrackKind::Synth
        && project.tracks[sel].instrumentPreset == 0;
    const bool editableSampler = !audioSelected && project.tracks[sel].kind == TrackKind::Synth
        && isSamplerInstrument(project.tracks[sel].instrumentPreset);
    editSynth.setVisible(editableSynth || editableSampler);
    editSynth.setButtonText(editableSampler ? "Edit sampler" : "Edit sound");
    if (synthPanel != nullptr && synthPanel->isVisible())
    {
        if (editableSynth)
            synthPanel->refresh(project.tracks[sel].synth, project.tracks[sel].trackName());
        else
            synthPanel->setVisible(false);
    }
    if (samplerPanel != nullptr && samplerPanel->isVisible())
    {
        if (editableSampler)
            refreshSamplerPanel(false);
        else
            samplerPanel->setVisible(false);
    }
    refreshInstrumentButton();
    drumsSelected = selDrums && !audioSelected;
    pianoRoll.setPattern(project.tracks[sel].kind == TrackKind::Synth
                             ? project.tracks[sel].melodies[static_cast<std::size_t>(trackMelodySlot[sel])]
                             : project.tracks[0].melodies[static_cast<std::size_t>(trackMelodySlot[0])]);
    drumSequencer.setPattern(project.tracks[sel].kind == TrackKind::Drums
                                 ? project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])]
                                 : project.tracks[1].drumPatterns[static_cast<std::size_t>(trackDrumSlot[1])]);
    updateTrackControls();
    refreshFxBar();
    for (int i = 0; i < numPatterns; ++i)
    {
        const int slot = selDrums ? trackDrumSlot[sel] : trackMelodySlot[sel];
        patternTabs[static_cast<std::size_t>(i)].setToggleState(i == slot, juce::dontSendNotification);
        patternTabs[static_cast<std::size_t>(i)].setVisible(!audioSelected);
    }
    duplicatePattern.setVisible(!audioSelected);
    grooveButton.setVisible(!audioSelected);
    keyButton.setVisible(!audioSelected);
    chordButton.setVisible(!audioSelected && !drumsSelected);
    arpButton.setVisible(!audioSelected && !drumsSelected);
    rampButton.setVisible(!audioSelected && !drumsSelected);
    autoButton.setVisible(!audioSelected);
    for (int track = 0; track < maxTracks; ++track)
        engine.setLoopSelection(track, trackMelodySlot[static_cast<std::size_t>(track)],
                                trackDrumSlot[static_cast<std::size_t>(track)]);
    engine.setLoopSelection(selectedTrack, trackMelodySlot[sel], trackDrumSlot[sel]);
    tempo.setValue(project.bpm, juce::dontSendNotification);
    refreshKeyButton();
    refreshArpButton();
    pendingPublish = !engine.submit(project);
    int totalNotes = 0, totalHits = 0;
    for (const auto& track : project.tracks)
    {
        for (const auto& pattern : track.melodies)
            totalNotes += pattern.count;
        for (const auto& drums : track.drumPatterns)
            totalHits += drums.hitCount();
    }
    subtitle.setText((projectFile == juce::File() ? "Untitled" : projectFile.getFileName())
                     + (dirty() ? " *" : "") + "   /   " + juce::String(totalNotes) + " notes + "
                     + juce::String(totalHits) + " hits + "
                     + juce::String(project.takeCount) + " takes",
                     juce::dontSendNotification);
    refreshAudioView();
    updateSongControls();
    refreshAiSidebar();
}

void MainComponent::undoEdit()
{
    if (editing || undoStack.empty())
        return;
    redoStack.push_back(project);
    project = undoStack.back();
    undoStack.pop_back();
    projectChanged();
    selectChannel(audioSelected ? -1 : selectedTrack);
}

void MainComponent::redoEdit()
{
    if (editing || redoStack.empty())
        return;
    undoStack.push_back(project);
    project = redoStack.back();
    redoStack.pop_back();
    projectChanged();
    selectChannel(audioSelected ? -1 : selectedTrack);
}

void MainComponent::selectTrack(bool drums)
{
    // Legacy two-channel shortcut: first track of the requested kind.
    for (int track = 0; track < maxTracks; ++track)
        if (project.tracks[static_cast<std::size_t>(track)].kind
            == (drums ? TrackKind::Drums : TrackKind::Synth))
        {
            selectTrackIndex(track);
            return;
        }
}

void MainComponent::setTrackSwing(float swing)
{
    const auto sel = static_cast<std::size_t>(selectedTrack);
    auto& track = project.tracks[sel];
    if (track.kind == TrackKind::None)
        return;
    const bool ownGesture = !editing;
    if (ownGesture)
        beginEdit();
    track.swing = std::clamp(swing, 0.0f, maxSwing);
    projectChanged();
    if (ownGesture)
        endEdit();
}

void MainComponent::applyQuantize(float strength)
{
    const auto sel = static_cast<std::size_t>(selectedTrack);
    auto& track = project.tracks[sel];
    if (track.kind != TrackKind::Synth || audioSelected)
        return;
    beginEdit();
    quantizePattern(track.melodies[static_cast<std::size_t>(trackMelodySlot[sel])],
                    strength);
    projectChanged();
    endEdit();
}

void MainComponent::applyVelocityRampToSelected(int startVel, int endVel)
{
    const auto sel = static_cast<std::size_t>(selectedTrack);
    auto& track = project.tracks[sel];
    if (track.kind != TrackKind::Synth || audioSelected)
        return;
    beginEdit();
    applyVelocityRamp(track.melodies[static_cast<std::size_t>(trackMelodySlot[sel])], startVel, endVel);
    projectChanged();
    endEdit();
}

void MainComponent::refreshKeyButton()
{
    keyButton.setButtonText("Key: " + juce::String(keyName(project.musicKey)) + " "
                            + scaleName(project.musicScale));
    pianoRoll.setScale(project.musicKey, project.musicScale, snapScale);
}

void MainComponent::refreshAutomation()
{
    if (automationLane == nullptr)
        return;
    const auto sel = std::clamp(selectedTrack, 0, maxTracks - 1);
    const auto& track = project.tracks[static_cast<std::size_t>(sel)];
    const int slot = std::clamp(drumsSelected ? trackDrumSlot[static_cast<std::size_t>(sel)]
                                              : trackMelodySlot[static_cast<std::size_t>(sel)],
                                0, numPatterns - 1);
    automationLane->setTarget(automationTarget);
    automationLane->setLane(track.automation[static_cast<std::size_t>(slot)]
                                              [static_cast<std::size_t>(automationTarget)],
                            static_cast<int>(std::fmod(engine.getTickPosition(), patternTicks)),
                            engine.isPlaying());
}

void MainComponent::refreshArpButton()
{
    const auto sel = static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1));
    const auto& fx = project.tracks[sel].liveFx;
    arpButton.setToggleState(fx.active(), juce::dontSendNotification);
    if (!fx.active())
    {
        arpButton.setButtonText("Arp");
        return;
    }
    juce::String text = juce::String(fx.arp == ArpMode::Off ? "Chord" : arpModeName(fx.arp));
    if (fx.arp != ArpMode::Off)
        text += juce::String(" ") + arpRateName(fx.rate);
    if (fx.chordOn && fx.arp != ArpMode::Off)
        text += "+" + juce::String(chordName(fx.chord));
    else if (fx.chordOn)
        text += juce::String(" ") + chordName(fx.chord);
    arpButton.setButtonText(text);
}

void MainComponent::applyHumanize(float amount)
{
    const auto sel = static_cast<std::size_t>(selectedTrack);
    auto& track = project.tracks[sel];
    if (track.kind == TrackKind::None || audioSelected)
        return;
    beginEdit();
    std::random_device seedSource;
    const auto seed = seedSource();
    if (track.kind == TrackKind::Drums)
        humanizeDrums(track.drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])], amount, seed);
    else
        humanizePattern(track.melodies[static_cast<std::size_t>(trackMelodySlot[sel])], amount, amount, seed);
    projectChanged();
    endEdit();
}

void MainComponent::selectTrackIndex(int track)
{
    if (track < 0 || track >= maxTracks)
        return;
    if (project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::None)
        return;
    selectedTrack = track;
    selectChannel(track);
}

void MainComponent::selectChannel(int channel)
{
    const bool audio = channel == -1;
    if (!audio)
    {
        if (channel < 0 || channel >= maxTracks)
            return;
        if (project.tracks[static_cast<std::size_t>(channel)].kind == TrackKind::None)
            return;
        selectedTrack = channel;
    }
    const bool drums = !audio
        && project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::Drums;
    drumsSelected = drums;
    audioSelected = audio;
    if (audio)
        ensureAudioInputs(); // inputs open only when recording becomes possible
    pianoRoll.setVisible(!drums && !audio);
    drumSequencer.setVisible(drums);
    audioView->setVisible(audio);
    keyboard.setVisible(!drums && !audio);
    if (fxBar != nullptr)
        fxBar->setVisible(!audio);
    repeatBar.setVisible(drums);
    kitButton.setVisible(drums);
    for (auto& button : padButtons)
        button.setVisible(drums);
    const bool showMixer = !audio;
    mute.setVisible(showMixer);
    solo.setVisible(showMixer);
    trackVolume.setVisible(showMixer);
    clear.setVisible(!audio);
    demo.setVisible(!audio);
    duplicatePattern.setVisible(!audio);
    grooveButton.setVisible(!audio);
    keyButton.setVisible(!audio);
    chordButton.setVisible(!audio && !drums);
    arpButton.setVisible(!audio && !drums);
    rampButton.setVisible(!audio && !drums);
    autoButton.setVisible(!audio);
    kitButton.setVisible(drumsSelected);
    for (int i = 0; i < numPatterns; ++i)
    {
        auto& tab = patternTabs[static_cast<std::size_t>(i)];
        tab.setVisible(!audio);
        const int slot = drums ? trackDrumSlot[static_cast<std::size_t>(selectedTrack)]
                               : trackMelodySlot[static_cast<std::size_t>(selectedTrack)];
        tab.setToggleState(i == slot, juce::dontSendNotification);
    }
    refreshTrackList();
    audioTab.setToggleState(audio, juce::dontSendNotification);

    description.setText(audio ? "REC: record from song start (or punch in)  |  Takes play in SONG mode  |  Click a take to inspect  |  Delete key removes it"
        : drums ? "Toggle: click  |  Paint: drag  |  Erase: right-drag  |  Velocity: scroll  |  Audition: pad names / QWER ASDF"
        : "Draw: click + drag  |  Move: drag note  |  Resize: right edge / Shift-drag  |  Delete: right-click  |  Velocity: scroll",
        juce::dontSendNotification);
    updateTrackControls();
    refreshAudioView();
    projectChanged();
    resized();
    repaint();
    if (isShowing())
    {
        if (audio) audioView->grabKeyboardFocus();
        else if (drums) drumSequencer.grabKeyboardFocus();
        else pianoRoll.grabKeyboardFocus();
    }
}

void MainComponent::selectPattern(int slot)
{
    if (slot < 0 || slot >= numPatterns || audioSelected)
        return;
    if (editPart >= 0 && editPart < project.song.sections)
    {
        beginEdit();
        project.song.slots[static_cast<std::size_t>(editPart)][static_cast<std::size_t>(selectedTrack)]
            = static_cast<std::uint8_t>(slot);
        projectChanged();
        endEdit();
        return;
    }
    if (drumsSelected)
        trackDrumSlot[static_cast<std::size_t>(selectedTrack)] = slot;
    else
        trackMelodySlot[static_cast<std::size_t>(selectedTrack)] = slot;
    projectChanged();
}

void MainComponent::refreshTrackList()
{
    for (int track = 0; track < maxTracks; ++track)
    {
        auto& button = trackButtons[static_cast<std::size_t>(track)];
        const auto& state = project.tracks[static_cast<std::size_t>(track)];
        const bool used = state.kind != TrackKind::None;
        button.setVisible(used);
        if (!used)
            continue;
        juce::String label = juce::String(track + 1).paddedLeft('0', 2) + "  " + state.trackName();
        if (track == dragHover && dragTrack >= 0 && dragTrack != dragHover)
            label = juce::String::charToString(0x00bb) + " " + label;
        if (state.mix.mute)
            label += " [M]";
        else if (state.mix.solo)
            label += " [S]";
        button.setButtonText(label);
        button.setToggleState(track == selectedTrack && !audioSelected, juce::dontSendNotification);
        button.setColour(juce::TextButton::buttonOnColourId, trackColour(state.icon));
    }
}

void MainComponent::showAddTrackMenu()
{
    juce::PopupMenu menu;
    int usable = 0;
    for (const auto& track : project.tracks)
        usable += track.kind == TrackKind::None ? 0 : 1;
    menu.addItem(1, "Add synth track", usable < maxTracks);
    menu.addItem(2, "Add drum track", usable < maxTracks);
    menu.addSeparator();
    juce::PopupMenu icons;
    const int currentIcon = !audioSelected
        ? project.tracks[static_cast<std::size_t>(selectedTrack)].icon & 7 : -1;
    for (int icon = 0; icon < 8; ++icon)
        icons.addItem(10 + icon, trackIconName(icon), true, icon == currentIcon);
    menu.addSubMenu("Icon / colour", icons, !audioSelected);
    menu.addItem(20, "Move selected up", !audioSelected && selectedTrack > 0);
    menu.addItem(21, "Move selected down", !audioSelected && selectedTrack < maxTracks - 1
                     && project.tracks[static_cast<std::size_t>(selectedTrack + 1)].kind != TrackKind::None);
    menu.addItem(3, "Rename selected track", !audioSelected);
    menu.addItem(4, "Delete selected track", !audioSelected && usable > 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrack),
                       [this](int result) {
                           if (result == 1)
                               addTrackOfKind(TrackKind::Synth);
                           else if (result == 2)
                               addTrackOfKind(TrackKind::Drums);
                           else if (result >= 10 && result < 18)
                               setTrackIcon(selectedTrack, result - 10);
                           else if (result == 20 || result == 21)
                           {
                               beginEdit();
                               moveTrack(selectedTrack, selectedTrack + (result == 20 ? -1 : 1));
                               projectChanged();
                               endEdit();
                               selectChannel(selectedTrack);
                           }
                           else if (result == 3)
                               renameTrack(selectedTrack);
                           else if (result == 4)
                               deleteTrack(selectedTrack);
                       });
}

void MainComponent::showTrackMenu(int track)
{
    selectTrackIndex(track);
    showAddTrackMenu();
}

void MainComponent::renameTrack(int track)
{
    if (track < 0 || track >= maxTracks
        || project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::None)
        return;
    auto* window = new juce::AlertWindow("Rename track", "Track name.", juce::MessageBoxIconType::QuestionIcon);
    window->addTextEditor("name", project.tracks[static_cast<std::size_t>(track)].trackName());
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    hostModal(window);
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([this, track, window](int result) {
                                if (result != 1)
                                    return;
                                const auto name = window->getTextEditorContents("name").trim();
                                if (name.isEmpty())
                                    return;
                                beginEdit();
                                project.tracks[static_cast<std::size_t>(track)].setTrackName(name);
                                projectChanged();
                                endEdit();
                            }),
                            true);
}

void MainComponent::addTrackOfKind(TrackKind kind)
{
    int slot = -1;
    for (int track = 0; track < maxTracks; ++track)
        if (project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::None)
        {
            slot = track;
            break;
        }
    if (slot < 0)
    {
        showError("Track list is full (8 tracks).");
        return;
    }
    beginEdit();
    auto& state = project.tracks[static_cast<std::size_t>(slot)];
    state = Track {};
    state.id = nextTrackId(project);
    state.kind = kind;
    state.icon = static_cast<std::uint8_t>(kind == TrackKind::Drums ? 1 : 0);
    state.setTrackName(kind == TrackKind::Drums ? "Drums " + juce::String(slot + 1)
                                                : "Keys " + juce::String(slot + 1));
    projectChanged();
    endEdit();
    selectTrackIndex(slot);
    resized();
}

void MainComponent::deleteTrack(int track)
{
    if (track < 0 || track >= maxTracks)
        return;
    int usable = 0;
    for (const auto& state : project.tracks)
        usable += state.kind == TrackKind::None ? 0 : 1;
    if (usable <= 1)
    {
        showError("A project needs at least one track.");
        return;
    }
    beginEdit();
    project.tracks[static_cast<std::size_t>(track)] = Track {};
    for (int candidate = 0; candidate < maxTracks; ++candidate)
        if (project.tracks[static_cast<std::size_t>(candidate)].kind != TrackKind::None)
        {
            selectedTrack = candidate;
            break;
        }
    projectChanged();
    endEdit();
    selectChannel(selectedTrack);
}

void MainComponent::setTrackIcon(int track, int icon)
{
    if (track < 0 || track >= maxTracks
        || project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::None)
        return;
    beginEdit();
    project.tracks[static_cast<std::size_t>(track)].icon = static_cast<std::uint8_t>(icon & 7);
    projectChanged();
    endEdit();
}

void MainComponent::moveTrack(int from, int to)
{
    from = std::clamp(from, 0, maxTracks - 1);
    to = std::clamp(to, 0, maxTracks - 1);
    if (!moveTrackState(project, from, to))
        return;
    const auto shiftUi = [&](int a, int b) {
        std::swap(trackMelodySlot[static_cast<std::size_t>(a)],
                  trackMelodySlot[static_cast<std::size_t>(b)]);
        std::swap(trackDrumSlot[static_cast<std::size_t>(a)],
                  trackDrumSlot[static_cast<std::size_t>(b)]);
    };
    if (from < to)
        for (int i = from; i < to; ++i)
            shiftUi(i, i + 1);
    else
        for (int i = from; i > to; --i)
            shiftUi(i, i - 1);
    selectedTrack = to;
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    screenKnob = -1;
    if (event.eventComponent == this && !audioSelected)
    {
        const auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
        if (track.kind != TrackKind::None)
            for (int knob = 0; knob < 8; ++knob)
                if (knobChipRect(knob).contains(event.position))
                {
                    screenKnob = knob;
                    screenKnobStartY = event.position.y;
                    screenKnobStart = knobPosition(track, knobMapFor(track)[static_cast<std::size_t>(knob)].target);
                    screenKnobEditing = false;
                }
    }
    dragTrack = dragHover = -1;
    for (int track = 0; track < maxTracks; ++track)
        if (event.eventComponent == &trackButtons[static_cast<std::size_t>(track)]
            && project.tracks[static_cast<std::size_t>(track)].kind != TrackKind::None)
        {
            if (event.mods.isPopupMenu())
            {
                showTrackMenu(track);
                return;
            }
            dragTrack = dragHover = track;
            dragStartPos = event.getPosition();
        }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (screenKnob >= 0)
    {
        // Vertical drag over ~150px sweeps the whole range (up = more).
        auto& track = project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))];
        if (track.kind == TrackKind::None)
        {
            screenKnob = -1;
            return;
        }
        const auto map = knobMapFor(track);
        const auto target = map[static_cast<std::size_t>(screenKnob)].target;
        if (!screenKnobEditing && !editing)
        {
            beginEdit();
            screenKnobEditing = true;
        }
        const float value = juce::jlimit(0.0f, 1.0f,
            screenKnobStart + static_cast<float>(screenKnobStartY - event.position.y) / 150.0f);
        applyKnob(track, target, value);
        // Keep the absolute hardware knobs in sync so the next physical turn
        // continues from here instead of jumping back to a stale position.
        knobValues[static_cast<std::size_t>(screenKnob)].store(static_cast<int>(std::round(value * 127.0f)));
        appliedKnobValues[static_cast<std::size_t>(screenKnob)] = knobValues[static_cast<std::size_t>(screenKnob)].load();
        const auto now = juce::Time::getMillisecondCounter();
        knobHighlightUntil = now + 1500;
        knobScreenUntil = now + 2000;
        lastKnob = screenKnob;
        refreshMiniLabDisplay(false);
        projectChanged();
        if (synthPanel != nullptr && synthPanel->isVisible())
            refreshSynthPanel();
        status.setText("Knob " + juce::String(screenKnob + 1) + "  /  " + track.trackName() + "  /  "
                       + map[static_cast<std::size_t>(screenKnob)].label + " "
                       + knobValueText(track, target),
                       juce::dontSendNotification);
        repaint(knobStripArea());
        return;
    }
    if (dragTrack < 0 || dragStartPos.getDistanceFrom(event.getPosition()) < 6)
        return;
    const auto pos = event.getEventRelativeTo(this).getPosition();
    for (int track = 0; track < maxTracks; ++track)
    {
        const auto& button = trackButtons[static_cast<std::size_t>(track)];
        if (button.isVisible() && button.getBounds().contains(pos))
            dragHover = track;
    }
    refreshTrackList();
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (screenKnob >= 0)
    {
        screenKnob = -1;
        if (screenKnobEditing)
        {
            screenKnobEditing = false;
            endEdit();
        }
        return;
    }
    const int from = dragTrack, to = dragHover;
    dragTrack = dragHover = -1;
    refreshTrackList();
    // Deferred past the button's own click so selection settles first.
    if (from >= 0 && to >= 0 && from != to)
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<MainComponent>(this), from, to] {
                if (safe == nullptr)
                    return;
                safe->beginEdit();
                safe->moveTrack(from, to);
                safe->projectChanged();
                safe->endEdit();
                safe->selectChannel(to);
            });
}

void MainComponent::refreshAudioView()
{
    if (audioView == nullptr || !audioSelected)
        return;
    auto* device = deviceManager.getCurrentAudioDevice();
    const int numInputs = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;
    const int latency = currentInputLatency();
    const juce::String deviceName = device != nullptr ? juce::String(device->getName()).trim() : juce::String();
    juce::String extra;
    if (recording)
        extra = "REC " + juce::String(recordChannels == 2 ? "stereo" : "mono");
    audioView->refresh(project, numInputs, latency, selectedTake, recording, deviceName, extra);
}

void MainComponent::updateTrackControls()
{
    const auto& mix = selectedMix();
    mute.setToggleState(mix.mute, juce::dontSendNotification);
    solo.setToggleState(mix.solo, juce::dontSendNotification);
    trackVolume.setValue(static_cast<double>(mix.volume) * 100.0, juce::dontSendNotification);
    trackVolume.setColour(juce::Slider::thumbColourId, drumsSelected ? ui::violet : ui::cyan);
    refreshTrackList();
    audioTab.setButtonText("Audio" + juce::String(project.takeCount > 0 ? " [" + juce::String(project.takeCount) + "]" : "")
        + juce::String(recording ? " REC" : ""));
    repaint(20, 208, 204, getHeight() - 266);
}

void MainComponent::auditionPad(int pad)
{
    int target = -1;
    if (!audioSelected
        && project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::Drums)
        target = selectedTrack;
    for (int track = 0; target < 0 && track < maxTracks; ++track)
        if (project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::Drums)
            target = track;
    if (target >= 0 && engine.auditionDrum(target, pad))
    {
        padFlashes[static_cast<std::size_t>(pad)] = 4;
        padButtons[static_cast<std::size_t>(pad)].setToggleState(true, juce::dontSendNotification);
    }
}

double MainComponent::currentRate() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 48000.0;
}

int MainComponent::currentInputLatency() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputLatencyInSamples();
    return 0;
}

void MainComponent::toggleRecord()
{
    if (ideaRecording.load() || assistantWorker != nullptr)
    {
        showError("Finish the idea first, then start an audio take.");
        return;
    }
    if (recording)
    {
        finalizeTake(); // punch out, keep playing
        return;
    }
    ensureAudioInputs();
    if (activeInputCount(deviceManager) <= 0)
    {
        showError("No audio inputs on the current device. Choose an input-capable device in Audio / MIDI first.");
        return;
    }
    if (project.takeCount >= maxTakes)
    {
        showError("The take list is full (8 takes). Delete a take before recording.");
        return;
    }
    // Takes only play back in song mode, so recording implies it.
    if (!project.songMode)
    {
        beginEdit();
        project.songMode = true;
        projectChanged();
        endEdit();
    }
    const int wanted = inputMode == 2 ? 2 : 1;
    recordChannels = std::min(wanted, activeInputCount(deviceManager));
    recordFile = sessionDir().getNonexistentChildFile("take-" + juce::String(nextTakeId), ".wav");
    if (!recorder.start(recordFile, currentRate(), recordChannels))
    {
        showError("Could not start recording to " + recordFile.getFullPathName());
        return;
    }
    const double framesPerTick = currentRate() * 60.0 / (project.bpm * ticksPerQuarter);
    const int compensation = latencyCompensationTicks(currentInputLatency(), framesPerTick);
    if (!engine.isPlaying())
    {
        engine.stop(); // rewind to the top before a fresh take
        // Negative start trims the input-latency delay out of the file head.
        recordStartTick = -compensation;
        engine.setPlaying(true);
    }
    else
    {
        recordStartTick = static_cast<int>(engine.getTickPosition()) - compensation;
    }
    recording = true;
    record.setToggleState(true, juce::dontSendNotification);
    record.setButtonText("STOP TAKE");
    refreshAudioView();
}

void MainComponent::toggleIdeaRecord()
{
    if (ideaRecording.load())
    {
        finishIdeaRecord();
        return;
    }
    if (recording || assistantWorker != nullptr)
    {
        ideaButton.setToggleState(false, juce::dontSendNotification);
        showError("Finish the audio take or AI request before capturing an idea.");
        return;
    }
    if (audioSelected || project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::None)
    {
        ideaButton.setToggleState(false, juce::dontSendNotification);
        showError("Select an instrument or drum track first.");
        return;
    }
    const auto sel = static_cast<std::size_t>(selectedTrack);
    ideaTrackId = project.tracks[sel].id;
    ideaKind = project.tracks[sel].kind;
    ideaSlot = project.songMode ? project.song.slots[static_cast<std::size_t>(songStartPart)][sel]
                  : editPart >= 0 ? project.song.slots[static_cast<std::size_t>(editPart)][sel]
                                  : (drumsSelected ? trackDrumSlot[sel] : trackMelodySlot[sel]);
    {
        const juce::ScopedLock lock(ideaMutex);
        ideaEvents.clear();
    }
    // Opening the mic is intentional here; merely playing Sonora still keeps
    // inputs off to avoid changing a Bluetooth headset's audio profile.
    if (ideaKind == TrackKind::Synth)
        ensureAudioInputs();
    ideaRate = currentRate();
    ideaHasAudio = false;
    if (ideaKind == TrackKind::Synth && activeInputCount(deviceManager) > 0)
    {
        ideaFile = sessionDir().getNonexistentChildFile("idea", ".wav");
        ideaHasAudio = ideaRecorder.start(ideaFile, ideaRate, 1);
    }
    ideaStarted = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    ideaRecording.store(true, std::memory_order_release);
    ideaButton.setToggleState(true, juce::dontSendNotification);
    ideaButton.setButtonText("STOP IDEA");
    status.setText("Capturing an idea for loop "
        + juce::String::charToString(static_cast<juce::juce_wchar>('A' + ideaSlot))
        + (ideaKind == TrackKind::Drums ? "... play pads." : "... play keys or hum into the mic.")
        + " Shift+Stop to turn it into a four-bar loop.", juce::dontSendNotification);
}

void MainComponent::finishIdeaRecord()
{
    if (!ideaRecording.exchange(false))
        return;
    ideaButton.setToggleState(false, juce::dontSendNotification);
    ideaButton.setButtonText("Idea REC");
    const double length = std::min(65.0, juce::Time::getMillisecondCounterHiRes() / 1000.0 - ideaStarted);
    const int audioFrames = ideaHasAudio ? ideaRecorder.stop() : 0;
    std::vector<IdeaEvent> captured;
    {
        const juce::ScopedLock lock(ideaMutex);
        captured.swap(ideaEvents);
    }
    const auto file = ideaFile;
    ideaFile = juce::File();
    const auto rate = ideaRate;
    const auto state = project;
    const auto trackId = ideaTrackId;
    const auto slot = ideaSlot;
    const auto kind = ideaKind;
    const int track = selectedTrack;
    if (length < 0.1)
    {
        file.deleteFile();
        showError("Idea was too short. Try playing or humming for a moment.");
        return;
    }
    status.setText("Shaping your idea into a four-bar loop...", juce::dontSendNotification);
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    startAiJob([safe, captured = std::move(captured), state, trackId, slot, kind, track,
                length, file, audioFrames, rate](const std::atomic<bool>* cancel) mutable {
        auto raw = interpretIdea(captured, kind, state.bpm, length);
        if (raw.count(kind) == 0 && kind == TrackKind::Synth && audioFrames >= 4096 && file.existsAsFile())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if (reader != nullptr && reader->numChannels > 0)
            {
                const int frames = static_cast<int>(std::min<std::int64_t>(reader->lengthInSamples,
                                             static_cast<std::int64_t>(rate * 65)));
                juce::AudioBuffer<float> audio(1, frames);
                if (reader->read(&audio, 0, frames, 0, true, false))
                    raw = interpretAudioIdea(audio.getReadPointer(0), frames, reader->sampleRate, state.bpm);
            }
        }
        file.deleteFile();
        ai::AssistantResult refined;
        if (raw.count(kind) > 0 && !cancel->load())
        {
            ai::AssistantRequest request;
            request.project = state;
            request.track = track;
            request.melodySlots[static_cast<std::size_t>(track)] = slot;
            request.drumSlots[static_cast<std::size_t>(track)] = slot;
            request.part = -1;
            auto& target = request.project.tracks[static_cast<std::size_t>(track)];
            if (kind == TrackKind::Drums) target.drumPatterns[static_cast<std::size_t>(slot)] = raw.drums;
            else target.melodies[static_cast<std::size_t>(slot)] = raw.melody;
            request.message = "This is a short idea I just played or hummed, not a finished take. "
                "The target's current pattern in the context is my captured idea. Preserve its recognizable "
                "pitch contour and rhythm, quantize and develop it into a musical four-bar loop that fits the "
                "other tracks. Return the complete edited pattern; do not ignore the idea.";
            refined = ai::runAssistant(request, cancel);
        }
        juce::MessageManager::callAsync([safe, raw, refined, trackId, slot] {
            if (safe != nullptr)
                safe->ideaFinished(raw, refined, trackId, slot);
        });
    });
}

void MainComponent::ideaFinished(IdeaResult raw, ai::AssistantResult refined, std::uint32_t trackId, int slot)
{
    finishAiJob();
    int track = -1;
    for (int i = 0; i < maxTracks; ++i)
        if (project.tracks[static_cast<std::size_t>(i)].id == trackId)
            track = i;
    if (track < 0)
    {
        status.setText("Idea capture finished, but its track was removed; nothing was changed.", juce::dontSendNotification);
        return;
    }
    const auto kind = project.tracks[static_cast<std::size_t>(track)].kind;
    if (kind != ideaKind)
    {
        status.setText("The idea's track changed instrument type; nothing was overwritten.", juce::dontSendNotification);
        return;
    }
    if (raw.count(kind) == 0)
    {
        showError(kind == TrackKind::Drums
            ? "No pad hits captured. Play the MiniLab pads during Idea REC."
            : "No notes detected. Play the MiniLab keys or hum a clear single-note idea into the mic.");
        return;
    }
    if (refined.ok() && refined.changed && refined.drums == (kind == TrackKind::Drums) && refined.count() > 0)
    {
        if (kind == TrackKind::Drums) raw.drums = refined.drumPattern;
        else raw.melody = refined.pattern;
    }
    beginEdit();
    auto& target = project.tracks[static_cast<std::size_t>(track)];
    if (kind == TrackKind::Drums) target.drumPatterns[static_cast<std::size_t>(slot)] = raw.drums;
    else target.melodies[static_cast<std::size_t>(slot)] = raw.melody;
    projectChanged();
    endEdit();
    status.setText("Idea shaped into loop " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot))
        + ": " + juce::String(raw.count(kind)) + (kind == TrackKind::Drums ? " hits" : " notes")
        + (refined.ok() && refined.changed ? " (AI arranged)." : " (quantized locally; AI unavailable).")
        + " Undo restores the previous loop.", juce::dontSendNotification);
}

void MainComponent::toggleDictation()
{
    if (aiSidebar == nullptr || !sidebarOpen())
        return;
    if (dictationRecording.exchange(false))
    {
        aiSidebar->setVoiceRecording(false);
        const int frames = dictationRecorder.stop();
        const auto file = dictationFile;
        dictationFile = juce::File();
        if (frames <= 0)
        {
            file.deleteFile();
            aiSidebar->setStatus("I didn't hear speech. Check your mic and try again.");
            return;
        }
        auto safe = juce::Component::SafePointer<MainComponent>(this);
        transcribing = true;
        startAiJob([safe, file](const std::atomic<bool>* cancel) {
            auto transcript = ai::transcribeDictation(file, cancel);
            file.deleteFile();
            juce::MessageManager::callAsync([safe, transcript] {
                if (safe == nullptr)
                    return;
                safe->finishAiJob();
                if (transcript.ok())
                {
                    safe->aiSidebar->appendDictation(transcript.text);
                    safe->aiSidebar->setStatus("Dictated locally. Review the text, then press Send.");
                }
                else
                    safe->aiSidebar->setStatus(transcript.error);
            });
        });
        aiSidebar->setStatus("Transcribing locally with Voxtype...");
        return;
    }
    if (assistantWorker != nullptr || recording || ideaRecording.load())
    {
        aiSidebar->setStatus("Finish the current recording or AI response before dictating.");
        return;
    }
    ensureAudioInputs();
    if (activeInputCount(deviceManager) <= 0)
    {
        aiSidebar->setStatus("No mic input available. Choose one in Audio / MIDI first.");
        return;
    }
    dictationFile = sessionDir().getNonexistentChildFile("assistant-voice", ".wav");
    if (!dictationRecorder.start(dictationFile, currentRate(), 1))
    {
        aiSidebar->setStatus("Could not start the microphone.");
        return;
    }
    dictationStarted = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    dictationRecording.store(true, std::memory_order_release);
    aiSidebar->setVoiceRecording(true);
}

void MainComponent::finalizeTake()
{
    if (!recording)
        return;
    recording = false;
    record.setToggleState(false, juce::dontSendNotification);
    record.setButtonText("REC");
    const int frames = recorder.stop();
    const int overruns = recorder.getOverruns();
    if (frames < 4800)
    {
        recordFile.deleteFile();
        showError(frames < 0 ? "Recording failed." : "Take too short — discarded.");
        refreshAudioView();
        return;
    }
    if (project.takeCount >= maxTakes)
    {
        recordFile.deleteFile();
        showError("The take list is full (8 takes). Delete a take first.");
        refreshAudioView();
        return;
    }
    beginEdit();
    // A fresh take joins the mix audibly: any comping solos are spent.
    for (int i = 0; i < project.takeCount; ++i)
        project.takes[static_cast<std::size_t>(i)].solo = false;
    AudioTakeMeta meta;
    meta.id = nextTakeId++;
    meta.setFileName(recordFile.getFileName());
    meta.startTick = recordStartTick;
    meta.frames = frames;
    meta.gain = 1.0f;
    meta.mute = false;
    meta.channels = recordChannels;
    meta.offline = false;
    project.takes[static_cast<std::size_t>(project.takeCount++)] = meta;
    projectChanged();
    endEdit();
    selectedTake = meta.id;
    refreshTakes();
    rebuildWaveCache();
    refreshAudioView();
    refreshPitchDisplay();
    analyzeTake(meta.id);
    if (overruns > 0)
        showError("Take kept, but the disk writer dropped " + juce::String(overruns) + " buffer(s) — raise the audio buffer size.");
}

void MainComponent::deleteTake(std::uint32_t id)
{
    int index = -1;
    for (int i = 0; i < project.takeCount; ++i)
        if (project.takes[static_cast<std::size_t>(i)].id == id)
            index = i;
    if (index < 0)
        return;
    beginEdit();
    for (int i = index; i < project.takeCount - 1; ++i)
        project.takes[static_cast<std::size_t>(i)] = project.takes[static_cast<std::size_t>(i + 1)];
    project.takes[static_cast<std::size_t>(--project.takeCount)] = {};
    if (selectedTake == id)
        selectedTake = 0;
    pitchContours.erase(id);
    projectChanged();
    endEdit();
    refreshTakes();
    rebuildWaveCache();
    refreshAudioView();
}

juce::File MainComponent::resolveTakeFile(const AudioTakeMeta& take) const
{
    if (projectFile != juce::File())
    {
        const auto inMedia = mediaDirFor(projectFile).getChildFile(take.fileName());
        if (inMedia.existsAsFile())
            return inMedia;
    }
    const auto inSession = sessionDir().getChildFile(take.fileName());
    if (inSession.existsAsFile())
        return inSession;
    return juce::File();
}

juce::String MainComponent::takeSignature() const
{
    juce::String signature = juce::String(currentRate(), 0) + "|";
    for (int i = 0; i < project.takeCount; ++i)
    {
        const auto& take = project.takes[static_cast<std::size_t>(i)];
        signature += juce::String(take.id) + ":" + take.fileName() + ":" + juce::String(take.startTick)
            + ":" + juce::String(take.frames) + ":" + juce::String(take.gain, 2)
            + ":" + juce::String(take.stretch, 3)
            + (take.mute ? "m" : "") + (take.offline ? "x" : "") + ";";
    }
    return signature;
}

void MainComponent::refreshTakes()
{
    // Take audio loads on a worker: reading and resampling whole takes on the
    // message thread froze the UI for long recordings. A load already in
    // flight is left to finish; its result is dropped if stale, and the timer
    // relaunches for whatever is current.
    if (takeLoadWorker != nullptr || takeSignature() == lastTakesSignature)
        return;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto metas = project.takes;
    const int count = project.takeCount;
    const juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    const double rate = currentRate();
    const auto signature = takeSignature();
    takesRevision = revision;
    takesRate = rate;
    auto job = [safe, metas, count, media, rate, signature](const std::atomic<bool>* cancel) {
        auto set = loadTakes(metas, count, media, rate,
                             [cancel](double) { return cancel == nullptr || !cancel->load(); });
        auto shared = std::make_shared<std::unique_ptr<TakeSet>>(std::move(set));
        juce::MessageManager::callAsync([safe, shared, signature]() {
            if (safe != nullptr)
                safe->takeLoadFinished(std::move(*shared), signature);
        });
    };
    takeLoadWorker = std::make_unique<BackgroundWorker>(std::move(job), "Sonora take loader");
    takeLoadWorker->startThread();
}

void MainComponent::takeLoadFinished(std::unique_ptr<TakeSet> set, const juce::String& signature)
{
    if (takeLoadWorker != nullptr)
    {
        takeLoadWorker->stopThread(2000);
        takeLoadWorker.reset();
    }
    if (set == nullptr || signature != takeSignature())
        return; // superseded or failed; the timer relaunches for current state
    lastTakesSignature = signature;
    // The engine borrows the raw pointer; ownership stays here. The previously
    // installed set may still be read by an audio block in flight, so delete
    // it after a grace period on the message thread. Each installed set is
    // retired exactly once, so delayed deletes never overlap.
    auto* retired = engine.retireTakeSet(set.get());
    const auto* owned = takeStorage.release();
    takeStorage = std::move(set);
    if (retired != nullptr)
        juce::Timer::callAfterDelay(600, [retired] { delete retired; });
    else
        delete owned;
    rebuildWaveCache();
}

void MainComponent::rebuildWaveCache()
{
    waveCache = {};
    if (takeStorage == nullptr)
    {
        if (audioView != nullptr)
        {
            audioView->setWave({}, 0, 0);
            audioView->setTakeWaves({}, {});
        }
        return;
    }
    constexpr int laneBuckets = 96;
    for (const auto& take : takeStorage->takes)
    {
        if (take.audio.getNumSamples() <= 0)
            continue;
        const int frames = take.audio.getNumSamples();
        const int channels = take.audio.getNumChannels();
        auto fill = [&](std::vector<float>& peaks, int buckets) {
            peaks.assign(static_cast<std::size_t>(buckets), 0.0f);
            for (int b = 0; b < buckets; ++b)
            {
                const int from = b * frames / buckets;
                const int to = (b + 1) * frames / buckets;
                float peak = 0.0f;
                for (int i = from; i < to; i += 7)
                    for (int ch = 0; ch < channels; ++ch)
                        peak = std::max(peak, std::abs(take.audio.getSample(ch, i)));
                peaks[static_cast<std::size_t>(b)] = peak;
            }
        };
        waveCache.laneIds.push_back(take.id);
        waveCache.lanePeaks.emplace_back();
        fill(waveCache.lanePeaks.back(), laneBuckets);
        if (take.id != selectedTake || selectedTake == 0)
            continue;
        waveCache.takeId = take.id;
        waveCache.frames = frames;
        fill(waveCache.peaks, 256);
    }
    if (audioView != nullptr)
    {
        audioView->setWave(waveCache.peaks, waveCache.frames, waveCache.takeId);
        audioView->setTakeWaves(waveCache.laneIds, waveCache.lanePeaks);
    }
}

void MainComponent::collectTakes(const juce::File& destination)
{
    if (project.takeCount == 0)
        return;
    const auto media = mediaDirFor(destination);
    media.createDirectory();
    beginEdit();
    for (int i = 0; i < project.takeCount; ++i)
    {
        auto& take = project.takes[static_cast<std::size_t>(i)];
        if (media.getChildFile(take.fileName()).existsAsFile())
            continue;
        const auto source = resolveTakeFile(take);
        if (source == juce::File())
        {
            take.offline = true;
            continue;
        }
        auto target = media.getNonexistentChildFile("take-" + juce::String(take.id), ".wav");
        if (source.copyFileTo(target))
        {
            take.setFileName(target.getFileName());
            take.offline = false;
        }
        else
            take.offline = true;
    }
    projectChanged();
    endEdit();
}

void MainComponent::resolveTakes(const juce::File& source)
{
    nextTakeId = 1;
    if (source == juce::File())
    {
        selectedTake = 0;
        return;
    }
    const auto media = mediaDirFor(source);
    for (int i = 0; i < project.takeCount; ++i)
    {
        auto& take = project.takes[static_cast<std::size_t>(i)];
        take.offline = !media.getChildFile(take.fileName()).existsAsFile();
        nextTakeId = std::max(nextTakeId, take.id + 1);
    }
    selectedTake = project.takeCount > 0 ? project.takes[0].id : 0;
}

struct MainComponent::ExportWorker final : public juce::ThreadWithProgressWindow
{
    ExportWorker(ExportJob jobIn, juce::File fileIn, juce::Component::SafePointer<MainComponent> ownerIn)
        : ThreadWithProgressWindow("Exporting WAV", true, true), job(std::move(jobIn)),
          file(std::move(fileIn)), owner(ownerIn) {}
    void run() override
    {
        result = OfflineExport::render(job, [this](double fraction) {
            setProgress(fraction);
            return !threadShouldExit();
        });
        if (result.ok())
            writeStatus = OfflineExport::writeWav(file, result, job.bitDepth);
    }
    void threadComplete(bool userPressedCancel) override
    {
        if (owner == nullptr)
            return;
        if (userPressedCancel)
            owner->showError("Export cancelled.");
        else if (!result.ok())
            owner->showError(result.error);
        else if (writeStatus.failed())
            owner->showError(writeStatus.getErrorMessage());
        else
            owner->showDialog(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::InfoIcon,
                "Export complete",
                file.getFileName() + "  /  "
                + juce::String(result.audio.getNumSamples() / result.sampleRate, 1) + " s  /  "
                + juce::String(juce::Decibels::gainToDecibels(result.peak, -80.0f), 1) + " dB peak"
                + (result.clipped ? "  (CLIPPED: lower track volumes or enable normalization)"
                                  : result.normalized ? "  (normalized)" : "")));
        owner->exportFinished();
    }
    ExportJob job;
    juce::File file;
    ExportResult result;
    juce::Result writeStatus = juce::Result::ok();
    juce::Component::SafePointer<MainComponent> owner;
};

struct MainComponent::ExportPanel final : public juce::Component
{
    ExportPanel(ExportJob& job, std::function<void()> bounceCb, std::function<void()> cancelCb)
        : onBounce(std::move(bounceCb)), onCancel(std::move(cancelCb))
    {
        setWantsKeyboardFocus(true);
        range.addItem("4-bar loop", 1);
        range.addItem("Full song", 2);
        range.setSelectedId(job.songRange ? 2 : 1, juce::dontSendNotification);
        for (auto* box : { &range, &rate, &depth })
        {
            addAndMakeVisible(box);
            box->setColour(juce::ComboBox::backgroundColourId, ui::raised);
            box->setColour(juce::ComboBox::textColourId, ui::text);
            box->setColour(juce::ComboBox::outlineColourId, ui::border);
            box->setWantsKeyboardFocus(false);
        }
        rate.addItem("44100 Hz", 44100);
        rate.addItem("48000 Hz", 48000);
        rate.addItem("96000 Hz", 96000);
        rate.setSelectedId(static_cast<int>(job.sampleRate), juce::dontSendNotification);
        depth.addItem("16-bit", 16);
        depth.addItem("24-bit", 24);
        depth.setSelectedId(job.bitDepth, juce::dontSendNotification);
        normalize.setButtonText("Normalize to -0.1 dBFS");
        normalize.setToggleState(job.normalize, juce::dontSendNotification);
        normalize.setColour(juce::ToggleButton::textColourId, ui::text);
        normalize.setColour(juce::ToggleButton::tickColourId, ui::cyan);
        normalize.setWantsKeyboardFocus(false);
        addAndMakeVisible(normalize);
        tail.setRange(0.0, 3.0, 0.1);
        tail.setValue(job.tailSeconds, juce::dontSendNotification);
        tail.setSliderStyle(juce::Slider::LinearHorizontal);
        tail.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
        tail.setTextValueSuffix(" s tail");
        tail.setColour(juce::Slider::thumbColourId, ui::cyan);
        tail.setColour(juce::Slider::textBoxTextColourId, ui::text);
        tail.setWantsKeyboardFocus(false);
        addAndMakeVisible(tail);
        addAndMakeVisible(hint);
        hint.setFont(ui::font(11.0f));
        hint.setColour(juce::Label::textColourId, ui::muted);
        hint.setText("Loop bounces one 4-bar pattern. Song bounces the arrangement plus tail.",
                     juce::dontSendNotification);
        bounce.setButtonText("Bounce...");
        cancel.setButtonText("Cancel");
        bounce.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
        bounce.getProperties().set("role", "primary");
        bounce.setWantsKeyboardFocus(false);
        cancel.setWantsKeyboardFocus(false);
        addAndMakeVisible(bounce);
        addAndMakeVisible(cancel);
        bounce.onClick = [this] { if (onBounce) onBounce(); };
        cancel.onClick = [this] { if (onCancel) onCancel(); };
        range.onChange = [&] { job.songRange = range.getSelectedId() == 2; };
        rate.onChange = [&] { job.sampleRate = static_cast<double>(rate.getSelectedId()); };
        depth.onChange = [&] { job.bitDepth = depth.getSelectedId(); };
        normalize.onClick = [&] { job.normalize = normalize.getToggleState(); };
        tail.onValueChange = [&] { job.tailSeconds = tail.getValue(); };
        setSize(460, 300);
    }
    void resized() override
    {
        range.setBounds(20, 20, 200, 30);
        rate.setBounds(240, 20, 200, 30);
        depth.setBounds(20, 64, 200, 30);
        tail.setBounds(240, 64, 200, 30);
        normalize.setBounds(20, 112, 420, 28);
        hint.setBounds(20, 148, 420, 44);
        bounce.setBounds(228, 248, 100, 32);
        cancel.setBounds(340, 248, 100, 32);
    }
    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        ui::caption(g, "EXPORT WAV", { 20, 244, 190, 40 }, ui::text, 14.0f);
        ui::caption(g, "RANGE", { 20, 4, 200, 16 });
        ui::caption(g, "SAMPLE RATE", { 240, 4, 200, 16 });
        ui::caption(g, "BIT DEPTH", { 20, 52, 200, 12 });
        ui::caption(g, "RING-OUT TAIL (SONG)", { 240, 52, 200, 12 });
    }
    juce::ComboBox range, rate, depth;
    juce::ToggleButton normalize;
    juce::Slider tail;
    juce::Label hint;
    juce::TextButton bounce, cancel;
    std::function<void()> onBounce, onCancel;
};

struct MainComponent::PitchWorker final : public juce::ThreadWithProgressWindow
{
    enum class Mode { Analyze, Apply };
    PitchWorker(Mode modeIn, juce::AudioBuffer<float> audioIn, double rateIn, AudioTakeMeta sourceIn,
                CorrectionSettings settingsIn, juce::Component::SafePointer<MainComponent> ownerIn,
                juce::File outFileIn = {})
        : ThreadWithProgressWindow(modeIn == Mode::Analyze ? "Analyzing pitch" : "Tuning take", true, true),
          mode(modeIn), audio(std::move(audioIn)), outFile(std::move(outFileIn)), rate(rateIn), source(sourceIn),
          settings(settingsIn), owner(ownerIn) {}
    void run() override
    {
        // Mono mix for detection; per-channel PSOLA shares one trajectory.
        mono.assign(static_cast<std::size_t>(audio.getNumSamples()), 0.0f);
        const int channels = audio.getNumChannels();
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            double sum = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                sum += audio.getSample(ch, i);
            mono[static_cast<std::size_t>(i)] = static_cast<float>(sum / channels);
        }
        YinDetector detector(rate);
        contour = detector.analyze(mono.data(), static_cast<int>(mono.size()), [this](double fraction) {
            setProgress(mode == Mode::Analyze ? fraction : fraction * 0.4);
            return !threadShouldExit();
        });
        if (threadShouldExit() || contour.empty())
            return;
        if (mode == Mode::Apply)
        {
            trajectory = correctionTrajectory(contour, settings);
            tuned.setSize(channels, audio.getNumSamples(), false, true, false);
            for (int ch = 0; ch < channels; ++ch)
            {
                psolaShift(audio.getReadPointer(ch), audio.getNumSamples(), contour, trajectory,
                           tuned.getWritePointer(ch));
                setProgress(0.4 + 0.6 * (ch + 1) / channels);
                if (threadShouldExit())
                    return;
            }
            // The tuned WAV is written here on the worker: writing the whole
            // take on the message thread froze the UI for long takes.
            if (!threadShouldExit() && outFile != juce::File())
            {
                std::unique_ptr<juce::FileOutputStream> stream(new juce::FileOutputStream(outFile));
                juce::WavAudioFormat format;
                std::unique_ptr<juce::AudioFormatWriter> writer(stream->openedOk()
                    ? format.createWriterFor(stream.release(), rate, static_cast<unsigned>(channels), 24, {}, 0)
                    : nullptr);
                wroteOk = writer != nullptr && writer->writeFromAudioSampleBuffer(tuned, 0, tuned.getNumSamples());
                if (!wroteOk || threadShouldExit())
                    outFile.deleteFile();
            }
        }
    }
    void threadComplete(bool userPressedCancel) override
    {
        if (owner == nullptr)
            return;
        if (userPressedCancel || contour.empty())
        {
            if (!userPressedCancel)
                owner->showError("Pitch analysis found no voiced audio in this take.");
            owner->pitchFinished();
            return;
        }
        if (mode == Mode::Analyze)
        {
            owner->pitchContours[source.id] = contour;
            owner->refreshPitchDisplay();
        }
        else if (wroteOk)
            owner->finishTunedTake(source, tuned.getNumSamples(), tuned.getNumChannels(), rate, contour);
        else
            owner->showError("Could not write the tuned take.");
        owner->pitchFinished();
    }
    Mode mode;
    juce::AudioBuffer<float> audio, tuned;
    juce::File outFile;
    bool wroteOk = false;
    std::vector<float> mono, trajectory;
    double rate = 48000.0;
    AudioTakeMeta source;
    CorrectionSettings settings;
    PitchContour contour;
    juce::Component::SafePointer<MainComponent> owner;
};

void MainComponent::pitchFinished()
{
    pitchWorker.reset();
    refreshPitchDisplay();
}

const PreloadedTake* MainComponent::findLoadedTake(std::uint32_t id) const
{
    if (takeStorage == nullptr)
        return nullptr;
    for (const auto& take : takeStorage->takes)
        if (take.id == id)
            return &take;
    return nullptr;
}

void MainComponent::analyzeTake(std::uint32_t id)
{
    if (pitchWorker != nullptr || id == 0)
        return;
    if (pitchContours.find(id) != pitchContours.end())
    {
        refreshPitchDisplay();
        return;
    }
    const auto* loaded = findLoadedTake(id);
    if (loaded == nullptr || loaded->audio.getNumSamples() <= 0)
    {
        showError("Load the take first (select the project rate and wait a moment).");
        return;
    }
    AudioTakeMeta meta;
    for (int i = 0; i < project.takeCount; ++i)
        if (project.takes[static_cast<std::size_t>(i)].id == id)
            meta = project.takes[static_cast<std::size_t>(i)];
    pitchWorker = std::make_unique<PitchWorker>(PitchWorker::Mode::Analyze, loaded->audio,
        takesRate > 0.0 ? takesRate : currentRate(), meta, pitchSettings, this);
    pitchWorker->launchThread();
}

void MainComponent::refreshPitchDisplay()
{
    if (audioView == nullptr)
        return;
    const auto contour = pitchContours.find(selectedTake);
    if (contour == pitchContours.end() || contour->second.empty())
    {
        audioView->setPitchDisplay({}, {}, 256, currentRate());
        return;
    }
    const auto trajectory = correctionTrajectory(contour->second, pitchSettings);
    std::vector<float> detected, target;
    detected.reserve(contour->second.frames.size());
    target.reserve(contour->second.frames.size());
    for (std::size_t i = 0; i < contour->second.frames.size(); ++i)
    {
        const float hz = contour->second.frames[i].voiced ? contour->second.frames[i].f0Hz : 0.0f;
        detected.push_back(hz);
        target.push_back(hz > 0.0f ? static_cast<float>(hz * std::pow(2.0, trajectory[i] / 12.0)) : 0.0f);
    }
    audioView->setPitchDisplay(std::move(detected), std::move(target),
                               contour->second.hopSamples, contour->second.sampleRate);
}

void MainComponent::applyPitch()
{
    if (pitchWorker != nullptr || selectedTake == 0)
        return;
    const auto* loaded = findLoadedTake(selectedTake);
    if (loaded == nullptr || loaded->audio.getNumSamples() <= 0)
    {
        showError("No take audio available to tune.");
        return;
    }
    if (project.takeCount >= maxTakes)
    {
        showError("The take list is full (8 takes). Delete a take first.");
        return;
    }
    AudioTakeMeta meta;
    for (int i = 0; i < project.takeCount; ++i)
        if (project.takes[static_cast<std::size_t>(i)].id == selectedTake)
            meta = project.takes[static_cast<std::size_t>(i)];
    const auto outFile = sessionDir().getNonexistentChildFile("take-" + juce::String(nextTakeId) + "-tuned", ".wav");
    meta.setFileName(outFile.getFileName());
    pitchWorker = std::make_unique<PitchWorker>(PitchWorker::Mode::Apply, loaded->audio,
        takesRate > 0.0 ? takesRate : currentRate(), meta, pitchSettings, this, outFile);
    pitchWorker->launchThread();
}

void MainComponent::finishTunedTake(const AudioTakeMeta& source, int frames, int channels,
                                    double rate, const PitchContour& contour)
{
    juce::ignoreUnused(rate);
    if (frames <= 0 || project.takeCount >= maxTakes)
        return;
    beginEdit();
    AudioTakeMeta meta;
    meta.id = nextTakeId++;
    meta.setFileName(source.fileName());
    meta.startTick = source.startTick;
    meta.frames = frames;
    meta.gain = source.gain;
    meta.mute = false;
    meta.channels = channels;
    project.takes[static_cast<std::size_t>(project.takeCount++)] = meta;
    pitchContours[meta.id] = contour;
    projectChanged();
    endEdit();
    selectedTake = meta.id;
    refreshTakes();
    rebuildWaveCache();
    refreshAudioView();
}

void MainComponent::refreshKeyboardColours()
{
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, ui::raised.brighter(0.06f));
    // Black keys stay dark in both modes so the keyboard keeps its shape.
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId,
                       ui::uiDark ? ui::background : juce::Colour(0xff2a2f3a));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, ui::background);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, ui::cyan.withAlpha(0.65f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ui::cyan.withAlpha(0.16f));
    keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, ui::muted);
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void MainComponent::applyOmarchyTheme(bool force)
{
    // Text follows the display scale (Omarchy monitor scale, e.g. 1.25), with
    // a SONORA_UI_SCALE override for personal preference. Checked on every
    // theme pass so docking/changing displays updates the whole UI.
    float scale = 1.0f;
#if !defined(__APPLE__)
    // On macOS display->scale is the Retina backing factor (2.0), which the OS
    // already applies; using it here doubled every font and broke the layout.
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = static_cast<float>(display->scale);
#endif
    const auto override = juce::SystemStats::getEnvironmentVariable("SONORA_UI_SCALE", {});
    if (override.getDoubleValue() > 0.0)
        scale = static_cast<float>(override.getDoubleValue());
    scale = std::clamp(scale, 1.0f, 2.0f);
    const auto palette = themeMode == omarchy::ThemeMode::Light ? omarchy::sonoraLightPalette()
        : themeMode == omarchy::ThemeMode::Dark ? omarchy::sonoraDarkPalette()
                                                : omarchy::loadOmarchyPalette();
    const auto fingerprint = omarchy::themeFingerprint() * 3u + static_cast<unsigned>(themeMode);
    if (!force && fingerprint == themeFingerprint && std::abs(ui::uiScale - scale) < 1.0e-6f)
        return;
    ui::setScale(scale);
    themeFingerprint = fingerprint;
    ui::applyPalette(palette);
    theme.applyPalette();
    refreshThemeButton();
    refreshKeyboardColours();
    repaint();
    pianoRoll.repaint();
    drumSequencer.repaint();
    if (fxBar != nullptr)
        fxBar->repaint();
    if (audioView != nullptr)
        audioView->repaint();
    if (kitPanel != nullptr && kitPanel->isVisible())
        kitPanel->repaint();
    if (exportPanel != nullptr && exportPanel->isVisible())
        exportPanel->repaint();
    keyboard.repaint();
}

void MainComponent::releaseResources() { engine.release(); }

void MainComponent::exportAudio()
{
    if (exportPanel != nullptr)
    {
        exportPanel->setVisible(!exportPanel->isVisible());
        return;
    }
    ExportJob job;
    job.project = project;
    job.songRange = project.songMode;
    if (projectFile != juce::File())
        job.mediaDir = mediaDirFor(projectFile);
    else
        job.mediaDir = sessionDir();
    job.sampleDirs = { sessionDir(), sampleLibraryDir() };
    pendingJob = job;
    exportPanel = std::make_unique<ExportPanel>(pendingJob,
        [this] {
            if (exportPanel == nullptr)
                return;
            // Panel writes directly into `pendingJob` through its control callbacks.
            ExportJob frozen = pendingJob;
            exportPanel->setVisible(false);
            chooser = std::make_unique<juce::FileChooser>("Export WAV",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Sonora bounce.wav"), "*.wav");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                [safe = juce::Component::SafePointer<MainComponent>(this), frozen](const juce::FileChooser& selected) mutable {
                    if (safe == nullptr || selected.getResult() == juce::File())
                        return;
                    if (safe->exportWorker != nullptr)
                        return;
                    safe->exportWorker = std::make_unique<ExportWorker>(frozen, selected.getResult(), safe);
                    safe->exportWorker->launchThread();
                });
        },
        [this] { exportPanel->setVisible(false); });
    addAndMakeVisible(exportPanel.get());
    resized();
    exportPanel->toFront(false);
}

void MainComponent::exportFinished()
{
    exportWorker.reset();
}

void MainComponent::showError(const juce::String& message)
{
    showDialog(juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon, "Sonora", message));
}

void MainComponent::showDialog(const juce::MessageBoxOptions& options, std::function<void(int)> callback)
{
    juce::AlertWindow::showAsync(options.withParentComponent(this), std::move(callback));
}

void MainComponent::hostModal(juce::Component* dialog)
{
    addAndMakeVisible(dialog);
    dialog->setCentrePosition(getLocalBounds().getCentre());
    dialog->toFront(true);
}

void MainComponent::saveProject(bool choosePath, std::function<void()> after)
{
    if (dialogPending)
        return;
    auto write = [safe = juce::Component::SafePointer<MainComponent>(this), after](const juce::File& file) {
        if (safe == nullptr)
            return;
        if (safe->recording)
            safe->finalizeTake();
        // Takes and kit samples live in sidecar media folders; gather them
        // before writing JSON.
        safe->collectTakes(file);
        safe->collectSamples(file);
        const auto result = ProjectIO::save(file, safe->project);
        if (result.failed())
        {
            safe->showError(result.getErrorMessage());
            return;
        }
        safe->projectFile = file;
        safe->savedProject = safe->project;
        safe->recoveredUnsaved = false;
        safe->recoveryFile.deleteFile();
        safe->projectChanged();
        if (after)
            after();
    };
    if (!choosePath && projectFile != juce::File())
    {
        write(projectFile);
        return;
    }
    dialogPending = true;
    chooser = std::make_unique<juce::FileChooser>("Save Sonora project",
        projectFile == juce::File() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                     .getChildFile("Untitled.sonora.json") : projectFile,
        "*.sonora.json");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainComponent>(this), write](const juce::FileChooser& selected) {
            if (safe == nullptr)
                return;
            safe->dialogPending = false;
            if (selected.getResult() != juce::File())
                write(selected.getResult());
        });
}

void MainComponent::openProject()
{
    if (dialogPending)
        return;
    dialogPending = true;
    chooser = std::make_unique<juce::FileChooser>("Open Sonora project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.sonora.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& selected) {
            if (safe == nullptr)
                return;
            safe->dialogPending = false;
            const auto file = selected.getResult();
            if (file == juce::File())
                return;
            ProjectState candidate;
            const auto result = ProjectIO::load(file, candidate);
            if (result.failed())
            {
                safe->showError(result.getErrorMessage());
                return;
            }
            safe->engine.stop();
            if (safe->recording)
                safe->finalizeTake();
            safe->project = safe->savedProject = candidate;
            safe->recoveredUnsaved = false;
            safe->projectFile = file;
            safe->undoStack.clear();
            safe->redoStack.clear();
            safe->recoveryFile.deleteFile();
            safe->resolveTakes(file);
            safe->pitchContours.clear();
            safe->lastTakesSignature.clear(); // force take reload on the timer
            safe->trackMelodySlot.fill(0);
            safe->trackDrumSlot.fill(0);
            safe->projectChanged();
            safe->selectChannel(safe->selectedTrack);
            safe->refreshTakes();
        });
}

void MainComponent::resetProject()
{
    if (recording)
        finalizeTake();
    engine.stop();
    project = savedProject = defaultProject();
    selectedTrack = 0;
    trackMelodySlot.fill(0);
    trackDrumSlot.fill(0);
    recoveredUnsaved = false;
    projectFile = juce::File();
    undoStack.clear();
    redoStack.clear();
    recoveryFile.deleteFile();
    nextTakeId = 1;
    selectedTake = 0;
    pitchContours.clear();
    lastTakesSignature.clear();
    projectChanged();
    selectChannel(0);
    refreshTakes();
}

void MainComponent::confirmDiscard(std::function<void()> action)
{
    if (dialogPending)
        return;
    if (!dirty())
    {
        action();
        return;
    }
    dialogPending = true;
    showDialog(juce::MessageBoxOptions::makeOptionsYesNoCancel(juce::MessageBoxIconType::QuestionIcon,
        "Save your changes?", "This project has unsaved changes.", "Save", "Discard", "Cancel"),
            [safe = juce::Component::SafePointer<MainComponent>(this), action](int answer) {
                if (safe == nullptr)
                    return;
                safe->dialogPending = false;
                if (answer == 1)
                    safe->saveProject(false, action);
                else if (answer == 2)
                    action();
            });
}

void MainComponent::requestClose(std::function<void()> callback)
{
    if (recording)
        finalizeTake();
    confirmDiscard([this, callback] { recoveryFile.deleteFile(); callback(); });
}

void MainComponent::offerRecovery()
{
    if (!recoveryFile.existsAsFile())
        return;
    dialogPending = true;
    showDialog(juce::MessageBoxOptions::makeOptionsOkCancel(juce::MessageBoxIconType::QuestionIcon,
        "Recover unsaved work?", "Sonora found a recovery snapshot from an earlier session.", "Recover", "Discard"),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int answer) {
            if (safe == nullptr)
                return;
            safe->dialogPending = false;
            if (answer == 1)
            {
                ProjectState recovered;
                const auto result = ProjectIO::load(safe->recoveryFile, recovered);
                if (result.failed())
                {
                    safe->recoveredRevision = safe->revision;
                    safe->showError(result.getErrorMessage());
                }
                else
                {
                    safe->project = recovered;
                    safe->recoveredUnsaved = true;
                    safe->resolveTakes(safe->projectFile);
                    safe->pitchContours.clear();
                    safe->lastTakesSignature.clear();
                    safe->projectChanged();
                    safe->refreshTakes();
                }
            }
            else
                safe->recoveryFile.deleteFile();
        });
}

void MainComponent::openAudioSettings()
{
    if (audioDialog != nullptr)
    {
        audioDialog->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio & MIDI settings";
    options.dialogBackgroundColour = ui::panel;
    options.content.setOwned(new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2,
                                                                   true, false, true, false));
    options.content->setSize(580, 480);
    options.useNativeTitleBar = false;
    options.resizable = false;
    auto* dialog = options.create();
    hostModal(dialog);
    dialog->enterModalState(true, nullptr, true);
    audioDialog = dialog;
}

void MainComponent::timerCallback()
{
    if (dictationRecording.load() && juce::Time::getMillisecondCounterHiRes() / 1000.0 - dictationStarted >= 60.0)
        toggleDictation();
    if (ideaRecording.load() && juce::Time::getMillisecondCounterHiRes() / 1000.0 - ideaStarted >= 60.0)
        finishIdeaRecord();
    refreshMiniLabDisplay(false);
    if (assistantStartedAt != 0 && aiSidebar != nullptr && timerTicks % 15 == 0)
        aiSidebar->setStatus((transcribing ? "Transcribing locally...  " : "Working on it...  ")
                             + juce::String((juce::Time::getMillisecondCounter() - assistantStartedAt) / 1000) + " s");
    {
        const auto serial = knobSerial.load(std::memory_order_acquire);
        if (serial != lastKnobSerial)
        {
            lastKnobSerial = serial;
            applyKnobChanges();
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (knobGesture && now > knobIdleUntil)
        {
            knobGesture = false;
            endEdit();
        }
        if (lastKnob >= 0 && knobHighlightUntil != 0 && now > knobHighlightUntil)
        {
            knobHighlightUntil = 0;
            repaint(knobStripArea());
        }
    }
    if (pendingPublish)
        pendingPublish = !engine.submit(project);
    const bool running = engine.isPlaying();
    if (running != lastRunning)
    {
        lastRunning = running;
        play.setButtonText(running ? "PLAYING" : "PLAY");
        play.setToggleState(running, juce::dontSendNotification);
    }
    pianoRoll.setPlayhead(std::fmod(engine.getTickPosition(), patternTicks), running);
    drumSequencer.setPlayhead(std::fmod(engine.getTickPosition(), patternTicks), running);
    if (automationLane != nullptr && automationLane->isVisible() && running)
        refreshAutomation();
    // Highlight keys held on the MiniLab (or virtual keyboard) in the roll.
    {
        std::vector<int> held;
        for (int pitch = 0; pitch < 128; ++pitch)
            for (int channel = 1; channel <= 16; ++channel)
                if (engine.keyboardState.isNoteOn(channel, pitch))
                {
                    held.push_back(pitch);
                    break;
                }
        pianoRoll.setLiveNotes(held);
    }
    const auto tick = static_cast<int>(engine.getTickPosition());
    const auto currentSection = juce::jlimit(0, maxSections - 1, tick / patternTicks);
    if (arrangement != nullptr && arrangement->isVisible()
        && (running || running != lastSectionRunning || currentSection != lastSection))
        refreshArrangement();
    lastSection = currentSection;
    lastSectionRunning = running;
    meterPeak = std::max(engine.getOutputPeak(), meterPeak * 0.94f);
    outputMeter.setText(meterPeak >= 1.0f ? "OUT  CLIP" : "OUT  "
        + juce::String(juce::Decibels::gainToDecibels(meterPeak, -80.0f), 1) + " dB",
        juce::dontSendNotification);
    outputMeter.setColour(juce::Label::textColourId, meterPeak >= 1.0f ? ui::danger : ui::cyan);
    const auto drumStep = running ? juce::jlimit(0, gridSteps - 1,
        static_cast<int>(std::fmod(engine.getTickPosition(), patternTicks)) / stepTicks) : -1;
    for (int pad = 0; pad < drumPads; ++pad)
    {
        auto& flash = padFlashes[static_cast<std::size_t>(pad)];
        if (flash > 0) --flash;
        const auto sel = static_cast<std::size_t>(selectedTrack);
        const auto& drumMix = project.tracks[sel].mix;
        bool anySolo = false;
        for (const auto& track : project.tracks)
            anySolo = anySolo || (track.kind != TrackKind::None && track.mix.solo);
        if (drumStep >= 0 && drumStep != previousDrumStep && !drumMix.mute
            && (drumMix.solo || !anySolo)
            && project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])].steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(drumStep)] > 0)
            flash = 4;
        padButtons[static_cast<std::size_t>(pad)].setToggleState(flash > 0, juce::dontSendNotification);
    }
    previousDrumStep = drumStep;
    // Meter bar + readout at 30Hz was repainting constantly; decay fast to
    // silence and only repaint while there is visible activity.
    if (meterPeak > 0.001f || engine.getOutputPeak() > 0.001f)
        repaint(20, getHeight() - 156, 204, 95);
    const auto positionText = juce::String(tick / 3840 + 1) + " : " + juce::String((tick / 960) % 4 + 1)
                     + " : " + juce::String((tick / 240) % 4 + 1)
                     + (project.songMode ? "  S" + juce::String(currentSection + 1) + "/" + juce::String(project.song.sections) : "");
    if (positionText != lastPositionText)
    {
        lastPositionText = positionText;
        position.setText(positionText, juce::dontSendNotification);
    }
    const bool canUndo = !undoStack.empty() && !editing;
    const bool canRedo = !redoStack.empty() && !editing;
    if (canUndo != lastCanUndo) { lastCanUndo = canUndo; undo.setEnabled(canUndo); }
    if (canRedo != lastCanRedo) { lastCanRedo = canRedo; redo.setEnabled(canRedo); }
    if (fxBar != nullptr)
    {
        const auto reduction = engine.getMasterReductionDb();
        if (std::abs(reduction - lastReductionDb) > 0.05f)
        {
            lastReductionDb = reduction;
            fxBar->setReduction(reduction);
        }
    }
    // REC button follows the recorder, not just the transport.
    if (record.getToggleState() != recording)
        record.setToggleState(recording, juce::dontSendNotification);
    if (!recording && record.getButtonText() != "REC")
        record.setButtonText("REC");
    // A song that ends itself mid-take finalizes the recording automatically.
    if (recording && !engine.isPlaying())
        finalizeTake();
    if (recording != lastRecordingShown)
    {
        lastRecordingShown = recording;
        updateTrackControls();
    }
    // Input meter decay happens here; the audio thread only raises the peak.
    if (audioView != nullptr && audioSelected)
    {
        const float level = inputPeak.load(std::memory_order_relaxed);
        inputPeak.store(level * 0.82f, std::memory_order_relaxed);
        audioView->setInputLevel(level);
    }
    // Reload preloaded takes when the project or the device rate changed.
    if (!recording)
        refreshTakes();
    refreshPadBank();
    refreshSampleData();
    // Live theme reload: `omarchy theme set` repaints the whole studio.
    if (timerTicks % 120 == 0)
        applyOmarchyTheme();
    if (++timerTicks % 15 == 0)
    {
        if (timerTicks % 60 == 0)
            autoConnectMidi(); // hotplug scan: new controllers just work
        const int xruns = deviceManager.getXRunCount();
        const juce::String xrunText = xruns > 0 ? "   /   XRUN " + juce::String(xruns) : "";
        const bool midiLive = juce::Time::getMillisecondCounter() - lastMidiMillis.load() < 2000;
        const juce::String midiText = "MIDI: " + midiStatusText + (midiLive ? " ●" : "");
        if (ideaRecording.load())
        {
            status.setText("IDEA REC  " + juce::String(static_cast<int>(juce::Time::getMillisecondCounterHiRes() / 1000.0 - ideaStarted))
                           + "s  /  Shift+Stop to shape into four bars  /  " + midiText, juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, ui::violet);
        }
        else if (!audioErrorMessage.isEmpty())
        {
            status.setText("AUDIO ERROR: " + audioErrorMessage + " — reopen Audio / MIDI to recover.",
                           juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, ui::danger);
        }
        else if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            status.setText(device->getName() + "   /   " + juce::String(device->getCurrentSampleRate(), 0)
                + " Hz   /   " + juce::String(device->getCurrentBufferSizeSamples()) + " samples   /   CPU "
                + juce::String(deviceManager.getCpuUsage() * 100.0, 1) + "%" + xrunText + "   /   " + midiText,
                juce::dontSendNotification);
            status.setColour(juce::Label::textColourId, ui::muted);
        }
        else
            status.setText("No audio device - choose an output in Audio / MIDI.   /   " + midiText,
                           juce::dontSendNotification);
    }
    if (timerTicks % 60 == 0 && !editing && !dialogPending && dirty() && revision != recoveredRevision)
    {
        auto result = recoveryFile.getParentDirectory().createDirectory();
        if (result.wasOk())
            result = ProjectIO::save(recoveryFile, project);
        recoveredRevision = revision;
        if (result.failed())
            showError("Recovery snapshot could not be saved: " + result.getErrorMessage());
    }
    else if (timerTicks % 60 == 0 && !editing && !dialogPending && !dirty() && revision != recoveredRevision)
    {
        recoveryFile.deleteFile();
        recoveredRevision = revision;
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    if (key == juce::KeyPress::escapeKey && exportPanel != nullptr && exportPanel->isVisible())
    {
        exportPanel->setVisible(false);
        grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && sidebarOpen())
    {
        toggleAiSidebar();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && samplerPanel != nullptr && samplerPanel->isVisible())
    {
        samplerPanel->setVisible(false);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && synthPanel != nullptr && synthPanel->isVisible())
    {
        synthPanel->setVisible(false);
        grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && kitPanel != nullptr && kitPanel->isVisible())
    {
        kitPanel->setVisible(false);
        grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        if (recording)
            finalizeTake();
        engine.setPlaying(!engine.isPlaying());
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::deleteKey && audioSelected && selectedTake != 0 && !recording)
    {
        deleteTake(selectedTake);
        return true;
    }
    if (!audioSelected && pianoRoll.isVisible())
    {
        if (key.getKeyCode() == '[') { pianoRoll.setViewBase(pianoRoll.getViewBase() - 12); return true; }
        if (key.getKeyCode() == ']') { pianoRoll.setViewBase(pianoRoll.getViewBase() + 12); return true; }
    }
    if (key.getModifiers().isAltDown()
        && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
        && !audioSelected)
    {
        const int to = selectedTrack + (key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
        if (to >= 0 && to < maxTracks
            && project.tracks[static_cast<std::size_t>(to)].kind != TrackKind::None)
        {
            beginEdit();
            moveTrack(selectedTrack, to);
            projectChanged();
            endEdit();
            selectChannel(to);
            return true;
        }
    }
    if (key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == '1') { selectTrack(false); return true; }
        if (key.getKeyCode() == '2') { selectTrack(true); return true; }
        if (key.getKeyCode() == '3') { selectChannel(-1); return true; }
        if (key.getKeyCode() == 'S') { saveProject(key.getModifiers().isShiftDown()); return true; }
        if (key.getKeyCode() == 'E') { exportAudio(); return true; }
        if (key.getKeyCode() == 'I') { toggleAiSidebar(); return true; }
        if (key.getKeyCode() == 'O') { confirmDiscard([this] { openProject(); }); return true; }
        if (key.getKeyCode() == 'N') { confirmDiscard([this] { resetProject(); }); return true; }
        if (key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown()) redoEdit(); else undoEdit();
            return true;
        }
    }
    else if (key.getModifiers().isAltDown() && !audioSelected
             && key.getKeyCode() >= '1' && key.getKeyCode() <= '8')
    {
        selectTrackIndex(key.getKeyCode() - '1');
        return true;
    }
    else if (!key.getModifiers().isAltDown() && !key.getModifiers().isCommandDown()
             && !key.getModifiers().isCtrlDown() && !audioSelected
             && key.getKeyCode() >= '1' && key.getKeyCode() <= '8')
    {
        selectKnob(key.getKeyCode() - '1');
        return true;
    }
    else if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown()
             && !key.getModifiers().isAltDown()
             && (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey))
    {
        nudgeKnob(key.getKeyCode() == juce::KeyPress::upKey ? 1 : -1);
        return true;
    }
    else if (drumsSelected && !key.getModifiers().isAltDown())
    {
        const auto pad = juce::String("QWERASDF").indexOfChar(static_cast<juce::juce_wchar>(key.getKeyCode()));
        if (pad >= 0)
        {
            auditionPad(pad);
            return true;
        }
    }
    else if (!drumsSelected && !key.getModifiers().isCommandDown()
             && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown())
    {
        // Computer-key piano: home row plays white keys from C4, Q row plays
        // the black keys above, piano-style. Live voices, same as the
        // on-screen keyboard (channel 1).
        const int pitch = pianoPitchForKey(key.getKeyCode());
        if (pitch >= 0)
        {
            // Always consume piano keys, including OS auto-repeat while held:
            // an unhandled repeat makes macOS play its alert blip over the note.
            if (heldKeys.find(key.getKeyCode()) == heldKeys.end())
            {
                heldKeys[key.getKeyCode()] = pitch;
                engine.keyboardState.noteOn(1, pitch, 0.8f);
            }
            return true;
        }
    }
    return false;
}

bool MainComponent::keyStateChanged(bool)
{
    const auto before = heldKeys.size();
    releaseDeadKeys();
    return heldKeys.size() != before;
}

void MainComponent::focusLost(juce::Component::FocusChangeType)
{
    releaseAllKeys();
}

int MainComponent::pianoPitchForKey(int keyCode)
{
    int code = keyCode;
    if (code >= 'a' && code <= 'z')
        code -= ('a' - 'A');
    switch (code)
    {
        case 'A': return 60; case 'S': return 62; case 'D': return 64;
        case 'F': return 65; case 'G': return 67; case 'H': return 69;
        case 'J': return 71; case 'K': return 72; case 'L': return 74;
        case ';': return 76;
        case 'W': return 61; case 'E': return 63; case 'T': return 66;
        case 'Y': return 68; case 'U': return 70; case 'O': return 73;
        case 'P': return 75;
        default: return -1;
    }
}

void MainComponent::releaseDeadKeys()
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (juce::KeyPress::isKeyCurrentlyDown(it->first))
            ++it;
        else
        {
            engine.keyboardState.noteOff(1, it->second, 0.0f);
            it = heldKeys.erase(it);
        }
    }
}

void MainComponent::releaseAllKeys()
{
    if (heldKeys.empty())
        return;
    engine.keyboardState.allNotesOff(1);
    heldKeys.clear();
}

void MainComponent::paint(juce::Graphics& g)
{
    const auto w = static_cast<float>(contentWidth()), h = static_cast<float>(getHeight());
    const auto accent = audioSelected ? ui::blue : trackColour(project.tracks[static_cast<std::size_t>(selectedTrack)].icon);
    g.fillAll(ui::background);
    // Ambient wash fading into the theme background, plus accent light behind
    // the header and editor. Indigo depth when dark, cool paper glow when light.
    const auto washTop = ui::uiDark ? ui::background.brighter(0.12f).interpolatedWith(juce::Colour(0xff232a55), 0.5f)
                                    : ui::background.interpolatedWith(juce::Colour(0xffdbe7f2), 0.55f);
    g.setGradientFill(juce::ColourGradient(washTop, 0, 0, ui::background, 0, h * 0.6f, false));
    g.fillRect(0.0f, 0.0f, w, h);
    g.setGradientFill(juce::ColourGradient(ui::cyan.withAlpha(0.10f), w * 0.5f, 0,
                                           juce::Colours::transparentBlack, w * 0.5f, 260, false));
    g.fillRect(0.0f, 0.0f, w, 260.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, 0, h * 0.35f,
                                           ui::violet.withAlpha(0.08f), w * 0.72f, h * 0.75f, false));
    g.fillRect(0.0f, h * 0.35f, w, h * 0.65f);
    g.setGradientFill(juce::ColourGradient(ui::raised, 80, 0,
                                          ui::background, w * 0.7f, 180, false));
    g.fillRect(0.0f, 0.0f, w, 100.0f);
    g.setColour(ui::border.withAlpha(0.6f));
    g.drawHorizontalLine(94, 24, w - 24);
    g.setGradientFill(juce::ColourGradient(ui::cyan, 24, 94, ui::violet.withAlpha(0.1f), w, 94, false));
    g.fillRect(24.0f, 94.0f, w - 48, 1.0f);

    // A small signal-mark gives the app its own identity without external assets.
    for (int i = 0; i < 6; ++i)
    {
        const auto height = 12.0f + 22.0f * std::abs(std::sin(static_cast<float>(i) * 0.9f));
        g.setColour(ui::cyan.interpolatedWith(ui::blue, static_cast<float>(i) / 6));
        g.fillRoundedRectangle(29.0f + static_cast<float>(i) * 6, 45 - height / 2, 3.5f, height, 1.5f);
    }
    ui::caption(g, "SOUND / SEQUENCE / CREATE", { 82, 63, 300, 16 }, ui::muted, 9.0f);

    ui::surface(g, { 20, 110, w - 40, 80 });
    ui::caption(g, "TRANSPORT", { 38, 118, 174, 15 }, ui::muted, 9.0f);
    ui::caption(g, "TEMPO", { 358, 118, 120, 15 }, ui::muted, 9.0f);
    ui::caption(g, "POSITION  /  BAR . BEAT . STEP", { 510, 118, 190, 15 }, ui::muted, 9.0f);
    ui::caption(g, "VIEW", { 704, 118, 160, 15 }, ui::muted, 9.0f);
    for (float x : { 354.0f, 496.0f })
    {
        g.setColour(ui::border);
        g.drawVerticalLine(static_cast<int>(x), 128, 173);
    }

    ui::surface(g, { 20, 208, 204, h - 266 });
    ui::caption(g, "TRACKS", { 38, 222, 170, 20 });
    if (!audioSelected)
        ui::caption(g, "SELECTED TRACK LEVEL", { 38, 550, 170, 16 }, accent, 9.0f);
    ui::caption(g, "MASTER OUTPUT", { 38, getHeight() - 147, 170, 20 });
    const auto meter = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(meterPeak, -60.0f) + 60) / 60);
    for (int i = 0; i < 18; ++i)
    {
        const auto colour = i > 15 ? ui::danger : i > 12 ? ui::violet : ui::cyan;
        g.setColour(static_cast<float>(i) / 18 < meter ? colour : ui::border.withAlpha(0.65f));
        g.fillRoundedRectangle(38 + static_cast<float>(i) * 9.2f, h - 119, 6.5f, 16, 1.5f);
    }

    ui::surface(g, { 240, 208, w - 264, h - 394 });
    g.setColour(accent);
    g.fillRoundedRectangle(256, 228, 3, 18, 1.5f);
    if (project.songMode)
    {
        ui::caption(g, "SONG", { 270, 220, 120, 26 }, ui::cyan, 15.0f);
        ui::caption(g, juce::String(project.song.sections) + " parts  /  " + juce::String(project.song.sections * 4)
                        + " bars  /  " + juce::String(project.song.songTicks() / 960.0 * 60.0 / project.bpm, 0) + " s",
                    { 340, 220, 260, 26 }, ui::muted, 10.0f);
    }
    else
    {
        ui::caption(g, audioSelected ? "AUDIO TAKES" : project.tracks[static_cast<std::size_t>(selectedTrack)].trackName(),
                    { 270, 220, 206, 26 }, accent, 15.0f);
        if (!audioSelected && !drumsSelected)
            ui::caption(g, "INSTRUMENT", { 484, 220, 92, 26 }, ui::muted, 9.0f);
        if (!audioSelected)
        {
            ui::caption(g, "PATTERN", { 256, 258, 72, 24 }, ui::muted, 9.0f);
            ui::caption(g, "WORKING ON", { 256, 294, 116, 24 }, ui::muted, 9.0f);
        }
    }
    g.setColour(ui::border.withAlpha(0.7f));
    g.drawHorizontalLine(project.songMode ? 254 : audioSelected ? 250 : 346, 254, w - 38);
    ui::surface(g, { 240, h - 172, w - 264, 114 });
    ui::caption(g, audioSelected ? "TAKE INSPECTOR" : drumsSelected ? "PERFORMANCE PADS" : "PERFORMANCE KEYS",
                { 256, getHeight() - 169, 280, 22 }, accent, 9.0f);
    paintKnobStrip(g);
    g.setColour(ui::border.withAlpha(0.5f));
    g.drawHorizontalLine(getHeight() - 44, 24, w - 24);
    ui::caption(g, "SONORA  /  NATIVE AUDIO", { contentWidth() - 237, getHeight() - 34, 211, 20 }, ui::muted, 9.0f);
}

void MainComponent::resized()
{
    title.setBounds(78, 22, 254, 40);
    subtitle.setBounds(350, 63, contentWidth() - 380, 24);
    auto fileRow = juce::Rectangle<int>(contentWidth() - 548, 27, 524, 34);
    for (auto* button : { &newProject, &open, &save, &saveAs, &exportButton })
    {
        button->setBounds(fileRow.removeFromLeft(98));
        fileRow.removeFromLeft(7);
    }
    themeButton.setBounds(contentWidth() - 590, 27, 34, 34);
    play.setBounds(38, 138, 92, 34);
    stop.setBounds(138, 138, 56, 34);
    record.setBounds(202, 138, 58, 34);
    ideaButton.setBounds(266, 138, 80, 34);
    tempo.setBounds(358, 139, 128, 32);
    position.setBounds(506, 134, 186, 42);
    loopView.setBounds(700, 138, 56, 34);
    songView.setBounds(758, 138, 56, 34);
    mixView.setBounds(816, 138, 48, 34);
    undo.setBounds(contentWidth() - 174, 218, 62, 28);
    redo.setBounds(contentWidth() - 104, 218, 62, 28);
    const int instrumentWidth = std::clamp(contentWidth() - 900, 160, 240);
    instrumentChoice.setBounds(580, 218, instrumentWidth, 28);
    editSynth.setBounds(588 + instrumentWidth, 218, 104, 28);
    panic.setBounds(872, 138, 72, 34);
    audioSettings.setBounds(contentWidth() - 180, 138, 142, 34);
    int row = 0;
    for (int track = 0; track < maxTracks; ++track)
        if (project.tracks[static_cast<std::size_t>(track)].kind != TrackKind::None)
            trackButtons[static_cast<std::size_t>(track)].setBounds(34, 250 + row++ * 28, 176, 26);
    audioTab.setBounds(34, 482, 94, 28);
    addTrack.setBounds(132, 482, 78, 28);
    mute.setBounds(38, 516, 78, 30);
    solo.setBounds(126, 516, 78, 30);
    trackVolume.setBounds(44, 568, 156, 54);
    trackVolume.setSliderStyle(juce::Slider::LinearHorizontal);
    clear.setBounds(contentWidth() - 106, 256, 68, 28);
    if (project.songMode)
        demo.setBounds(contentWidth() - 174 - 136, 218, 128, 28);
    else
        demo.setBounds(contentWidth() - 242, 256, 128, 28);
    kitButton.setBounds(contentWidth() - 322, 256, 72, 28);
    description.setBounds(256, audioSelected ? getHeight() - 132 : 324, contentWidth() - 294, 18);
    repeatBar.setBounds(contentWidth() - 466, 256, 136, 28);
    partChoice.setBounds(354, 292, 300, 28);
    partTrackOn.setBounds(662, 292, 150, 28);
    partHint.setBounds(partTrackOn.isVisible() ? 820 : 662, 292,
                       std::max(120, contentWidth() - (partTrackOn.isVisible() ? 820 : 662) - 200), 28);
    songTemplate.setBounds(620, 218, std::clamp(contentWidth() - 174 - 136 - 620 - 12, 160, 340), 28);
    if (arrangement != nullptr)
        arrangement->setBounds(254, 262, contentWidth() - 292, getHeight() - 530);
    if (mixer != nullptr)
        mixer->setBounds(254, 262, contentWidth() - 292, getHeight() - 530);
    for (int i = 0; i < numPatterns; ++i)
        patternTabs[static_cast<std::size_t>(i)].setBounds(330 + i * 36, 256, 32, 28);
    duplicatePattern.setBounds(480, 256, 60, 28);
    grooveButton.setBounds(546, 256, 70, 28);
    keyButton.setBounds(622, 256, 96, 28);
    chordButton.setBounds(724, 256, 64, 28);
    arpButton.setBounds(794, 256, 60, 28);
    rampButton.setBounds(860, 256, 60, 28);
    autoButton.setBounds(926, 256, 56, 28);
    const bool laneShown = automationLane != nullptr && automationLane->isVisible();
    const int editorHeight = getHeight() - 634 - (laneShown ? 158 : 0);
    pianoRoll.setBounds(254, 354, contentWidth() - 292, editorHeight);
    drumSequencer.setBounds(pianoRoll.getBounds());
    if (automationLane != nullptr)
        automationLane->setBounds(254, 354 + editorHeight + 8, contentWidth() - 292, 150);
    if (audioView != nullptr)
        audioView->setBounds(254, 258, contentWidth() - 292, getHeight() - 444);
    if (fxBar != nullptr)
        fxBar->setBounds(240, getHeight() - 280, contentWidth() - 264, 100);
    keyboard.setBounds(256, getHeight() - 140, contentWidth() - 296, 68);
    keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / 21.0f);
    const auto padWidth = (contentWidth() - 288) / drumPads;
    for (int pad = 0; pad < drumPads; ++pad)
        padButtons[static_cast<std::size_t>(pad)].setBounds(254 + pad * padWidth,
            getHeight() - 144, padWidth - 6, 74);
    status.setBounds(24, getHeight() - 36, contentWidth() - 282, 24);
    outputMeter.setBounds(34, getHeight() - 98, 174, 24);
    if (exportPanel != nullptr)
        exportPanel->setBounds((contentWidth() - 460) / 2, (getHeight() - 300) / 2, 460, 300);
    if (kitPanel != nullptr)
        kitPanel->setBounds((contentWidth() - 560) / 2, (getHeight() - 460) / 2, 560, 460);
    if (aiSidebar != nullptr)
        aiSidebar->setBounds(getWidth() - sidebarWidth, 12, sidebarWidth - 12, getHeight() - 58);
    if (synthPanel != nullptr)
        synthPanel->setBounds((contentWidth() - 920) / 2, std::max(200, (getHeight() - 470) / 2 - 60), 920, 470);
    if (samplerPanel != nullptr)
        samplerPanel->setBounds((contentWidth() - 920) / 2, std::max(200, (getHeight() - 470) / 2 - 60), 920, 470);
}

void MainComponent::setSongView(bool song)
{
    mixerVisible = false;
    if (project.songMode != song)
    {
        beginEdit();
        project.songMode = song;
        engine.stop();
        projectChanged();
        endEdit();
    }
    // Re-apply the editor for the current selection (Song view hides it).
    selectChannel(audioSelected && !song ? -1 : selectedTrack);
    updateSongControls();
    resized();
    repaint();
}

void MainComponent::setMixerView(bool show)
{
    if (mixerVisible == show)
    {
        updateSongControls();
        return;
    }
    mixerVisible = show;
    // Re-apply the editor, then the mixer branch of updateSongControls hides
    // it again when needed. Playback is untouched so mixing stays live.
    selectChannel(audioSelected ? -1 : selectedTrack);
    updateSongControls();
    resized();
    repaint();
}

void MainComponent::refreshMixer()
{
    if (mixer == nullptr)
        return;
    MixerState view;
    for (int track = 0; track < maxTracks; ++track)
    {
        const auto t = static_cast<std::size_t>(track);
        const auto& state = project.tracks[t];
        view.used[t] = state.kind != TrackKind::None;
        view.drums[t] = state.kind == TrackKind::Drums;
        view.names[t] = state.trackName();
        view.colours[t] = trackColour(state.icon);
        view.mix[t] = state.mix;
    }
    view.sends = project.sends;
    mixer->setState(view);
}

void MainComponent::refreshArrangement()
{
    if (arrangement == nullptr)
        return;
    ArrangementState view;
    view.song = project.song;
    view.musicKey = project.musicKey;
    for (int track = 0; track < maxTracks; ++track)
    {
        const auto t = static_cast<std::size_t>(track);
        const auto& state = project.tracks[t];
        view.used[t] = state.kind != TrackKind::None;
        view.drums[t] = state.kind == TrackKind::Drums;
        view.names[t] = state.trackName();
        view.colours[t] = trackColour(state.icon);
        for (int slot = 0; slot < numPatterns; ++slot)
            view.contentCount[t][static_cast<std::size_t>(slot)] = state.kind == TrackKind::Drums
                ? state.drumPatterns[static_cast<std::size_t>(slot)].hitCount()
                : state.melodies[static_cast<std::size_t>(slot)].count;
    }
    view.selected = songStartPart;
    view.playTick = engine.isPlaying() && project.songMode ? engine.getTickPosition() : -1.0;
    arrangement->setState(view);
}

void MainComponent::handleArrangementAction(const ArrangementAction& action)
{
    using K = ArrangementAction::Kind;
    auto& song = project.song;
    auto edit = [this](auto&& change) {
        const bool own = !editing;
        if (own)
            beginEdit();
        change();
        projectChanged();
        if (own)
            endEdit();
    };
    auto partLabel = [&song](int s) {
        return juce::String(songPartName(song.parts[static_cast<std::size_t>(s)])) + " (bars "
            + juce::String(s * 4 + 1) + "-" + juce::String(s * 4 + 4) + ")";
    };
    const auto s = static_cast<std::size_t>(std::max(0, action.section));
    const auto t = static_cast<std::size_t>(std::clamp(action.track, 0, maxTracks - 1));
    switch (action.kind)
    {
        case K::SetCell:
            edit([&] {
                song.trackOn[s][t] = action.on;
                song.slots[s][t] = static_cast<std::uint8_t>(std::clamp(action.value, 0, numPatterns - 1));
            });
            break;
        case K::SelectSection:
            songStartPart = action.section;
            engine.setSongStartSection(songStartPart);
            if (engine.isPlaying() && project.songMode)
            {
                engine.stop();
                engine.setPlaying(true);
            }
            refreshArrangement();
            status.setText("Song plays from " + partLabel(action.section) + ". Press Play (Space).",
                           juce::dontSendNotification);
            break;
        case K::EditPart:
            editPart = action.section;
            setSongView(false);
            break;
        case K::EditLoop:
            if (project.tracks[t].kind == TrackKind::None)
                break;
            selectedTrack = action.track;
            editPart = action.section;
            if (editPart < 0)
            {
                trackMelodySlot[t] = std::clamp(action.value, 0, numPatterns - 1);
                trackDrumSlot[t] = trackMelodySlot[t];
            }
            setSongView(false);
            break;
        case K::SetPartType:
            edit([&] { song.parts[s] = static_cast<SongPart>(std::clamp(action.value, 0, static_cast<int>(SongPart::numParts) - 1)); });
            break;
        case K::SetChord:
        {
            const int root = action.value / 16 - 1;
            const auto type = static_cast<ChordType>(std::clamp(action.value % 16, 0,
                                                               static_cast<int>(ChordType::numChords) - 1));
            edit([&] { song.chords[s] = { std::clamp(root, -1, 11), type }; });
            const auto chord = song.chords[s];
            const auto shift = chordTranspose(chord, project.musicKey);
            status.setText(chord.set()
                               ? "Bars " + juce::String(action.section * 4 + 1) + "-" + juce::String(action.section * 4 + 4)
                                 + " follow " + chordLabel(chord) + " (loops transpose "
                                 + (shift >= 0 ? "+" : "") + juce::String(shift) + " semitones)."
                               : "Chord cleared: bars " + juce::String(action.section * 4 + 1) + "-"
                                 + juce::String(action.section * 4 + 4) + " play as written.",
                           juce::dontSendNotification);
            refreshArrangement();
            break;
        }
        case K::Duplicate:
            edit([&] { song.duplicateSection(action.section); });
            break;
        case K::Insert:
            edit([&] { song.insertSection(action.section + 1); });
            break;
        case K::Remove:
            edit([&] { song.removeSection(action.section); });
            if (editPart == action.section)
                editPart = -1;
            else if (editPart > action.section)
                --editPart;
            if (songStartPart > action.section)
                --songStartPart;
            projectChanged();
            break;
        case K::Move:
            edit([&] { song.moveSection(action.section, action.value); });
            if (songStartPart == action.section)
                songStartPart = action.value;
            projectChanged();
            break;
        case K::Add:
            if (song.sections >= maxSections)
            {
                showError("Songs can have up to " + juce::String(maxSections) + " parts (" + juce::String(maxSections * 4)
                          + " bars).");
                break;
            }
            edit([&] { song.insertSection(song.sections, song.sections - 1); });
            status.setText("Added part " + juce::String(song.sections) + " as a copy of the one before. "
                           "Right-click its header to name it.", juce::dontSendNotification);
            break;
        case K::MakeUnique:
        {
            bool made = false;
            edit([&] {
                made = project.tracks[t].kind == TrackKind::Drums
                    ? makeDrumSectionUnique(song, project.tracks[t].drumPatterns, action.section, action.track)
                    : makeSectionUnique(song, project.tracks[t].melodies, action.section, action.track);
            });
            if (!made)
                showError("This part already has its own copy, or all four loops on this track are in use.");
            break;
        }
    }
}

void MainComponent::updateSongControls()
{
    const bool song = project.songMode;
    loopView.setToggleState(!song, juce::dontSendNotification);
    songView.setToggleState(song, juce::dontSendNotification);
    mixView.setToggleState(mixerVisible && !audioSelected, juce::dontSendNotification);
    autoButton.setToggleState(automationVisible && !audioSelected && !song, juce::dontSendNotification);
    const bool showAutomation = automationVisible && !audioSelected && !song;
    if (automationLane != nullptr)
        automationLane->setVisible(showAutomation);
    if (arrangement == nullptr)
        return;
    arrangement->setVisible(song);
    songTemplate.setVisible(song);
    if (song)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &pianoRoll, &drumSequencer, audioView.get(), &duplicatePattern, &clear, &kitButton,
                 &repeatBar, &description, &instrumentChoice, &editSynth, &partChoice, &partTrackOn, &partHint,
                 automationLane.get() })
            if (c != nullptr)
                c->setVisible(false);
        for (auto& tab : patternTabs)
            tab.setVisible(false);
        demo.setVisible(true); // AI composer stays reachable in Song view
        refreshArrangement();
        return;
    }
    const bool trackEditor = !audioSelected;
    partChoice.setVisible(trackEditor);
    partHint.setVisible(trackEditor);
    partChoice.clear(juce::dontSendNotification);
    partChoice.addItem("Free loop (no song part)", 1);
    for (int s = 0; s < project.song.sections; ++s)
        partChoice.addItem(juce::String(s + 1) + "   " + songPartName(project.song.parts[static_cast<std::size_t>(s)])
                               + "   (bars " + juce::String(s * 4 + 1) + "-" + juce::String(s * 4 + 4) + ")",
                           s + 2);
    partChoice.setSelectedId(editPart >= 0 ? editPart + 2 : 1, juce::dontSendNotification);
    const auto sel = static_cast<std::size_t>(selectedTrack);
    const bool inPart = editPart >= 0 && editPart < project.song.sections;
    partTrackOn.setVisible(trackEditor && inPart);
    juce::String hint;
    if (inPart)
    {
        const auto e = static_cast<std::size_t>(editPart);
        const int slot = project.song.slots[e][sel];
        partTrackOn.setToggleState(project.song.trackOn[e][sel], juce::dontSendNotification);
        juce::StringArray others;
        for (const auto part : sectionsSharingSlot(project.song, selectedTrack, slot))
            if (part != editPart + 1)
                others.add(juce::String(part));
        hint = "This track plays loop " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot))
            + " here" + (others.isEmpty() ? juce::String(".") : ", also used in parts " + others.joinIntoString(", ") + ".");
    }
    else
        hint = "Each track loops its own chosen pattern. Pick a part to hear and edit it in context.";
    partHint.setText(hint, juce::dontSendNotification);
    const int hintX = partTrackOn.isVisible() ? 820 : 662;
    partHint.setBounds(hintX, 292, std::max(120, getWidth() - hintX - 200), 28);
    if (showAutomation)
        refreshAutomation();
    // Mixer replaces the editor (and the song board) but never the transport.
    const bool showMixer = mixerVisible && !audioSelected;
    if (mixer != nullptr)
        mixer->setVisible(showMixer);
    if (arrangement != nullptr && showMixer)
        arrangement->setVisible(false);
    if (showMixer)
    {
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &pianoRoll, &drumSequencer, &duplicatePattern, &clear, &kitButton,
                 &repeatBar, &description, &instrumentChoice, &editSynth, &partChoice, &partTrackOn,
                 &partHint, &grooveButton, &keyButton, &chordButton, &arpButton, &rampButton,
                 automationLane.get() })
            if (c != nullptr)
                c->setVisible(false);
        for (auto& tab : patternTabs)
            tab.setVisible(false);
        for (auto& button : padButtons)
            button.setVisible(false);
        refreshMixer();
    }
}
}
