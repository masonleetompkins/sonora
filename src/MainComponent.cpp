#include "MainComponent.h"

namespace sonora
{
// Per-track hues keyed by track icon; the palette cycles every 8 icons.
juce::Colour trackColour(int icon)
{
    static const juce::Colour palette[] {
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
        const char* targets[] { "MEL", "DRM", "MST" };
        for (int i = 0; i < 3; ++i)
        {
            auto& button = targetButtons[static_cast<std::size_t>(i)];
            addAndMakeVisible(button);
            button.setButtonText(targets[i]);
            button.setWantsKeyboardFocus(false);
            button.setColour(juce::TextButton::buttonOnColourId, ui::blue);
            button.onClick = [this, i] { if (onTarget) onTarget(i); };
        }
        const char* effects[] { "EQ", "CMP", "DLY", "VRB" };
        for (int i = 0; i < 4; ++i)
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
            label.setFont(ui::font(9.0f, true, 0.08f));
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
            case 0: return { { "LOW", -15.0f, 15.0f, "dB" }, { "MID", -15.0f, 15.0f, "dB" },
                             { "MIDF", 200.0f, 8000.0f, "Hz", 1200.0f }, { "HIGH", -15.0f, 15.0f, "dB" } };
            case 1: return { { "THR", -40.0f, 0.0f, "dB" }, { "RATIO", 1.0f, 12.0f, ":1" },
                             { "ATT", 0.5f, 100.0f, "ms" }, { "REL", 20.0f, 1000.0f, "ms" } };
            case 2: return { { "TIME", 20.0f, 1000.0f, "ms" }, { "FDBK", 0.0f, 0.8f, "" },
                             { "MIX", 0.0f, 1.0f, "%", } };
            default: return { { "SIZE", 0.0f, 1.0f, "" }, { "DAMP", 0.0f, 1.0f, "" }, { "MIX", 0.0f, 1.0f, "%" } };
        }
    }

    void refresh(int target, int effect, bool enabled)
    {
        for (int i = 0; i < 3; ++i)
            targetButtons[static_cast<std::size_t>(i)].setToggleState(i == target, juce::dontSendNotification);
        for (int i = 0; i < 4; ++i)
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
        auto tabs = row.removeFromLeft(150);
        tabs.removeFromTop(2);
        auto targetRow = tabs.removeFromTop(26);
        for (int i = 0; i < 3; ++i)
        {
            targetButtons[static_cast<std::size_t>(i)].setBounds(targetRow.removeFromLeft(46));
            targetRow.removeFromLeft(4);
        }
        auto effectRow = tabs.removeFromTop(26);
        for (int i = 0; i < 4; ++i)
        {
            if (effectButtons[static_cast<std::size_t>(i)].isVisible())
            {
                effectButtons[static_cast<std::size_t>(i)].setBounds(effectRow.removeFromLeft(46));
                effectRow.removeFromLeft(4);
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
    std::array<juce::TextButton, 4> effectButtons;
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

void MainComponent::refreshPadBank()
{
    const double rate = currentRate();
    const juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    juce::String signature = juce::String(rate, 0) + "|" + juce::String(project.tracks[static_cast<std::size_t>(drumEditTrack())].kitVariant) + "|";
    std::array<juce::String, drumPads> files {};
    for (int pad = 0; pad < drumPads; ++pad)
    {
        files[static_cast<std::size_t>(pad)] = padSampleName(project.tracks[static_cast<std::size_t>(drumEditTrack())].padSamples[static_cast<std::size_t>(pad)]);
        signature += files[static_cast<std::size_t>(pad)] + ";";
    }
    if (signature == lastBankSignature)
        return;
    lastBankSignature = signature;
    auto bank = loadSampleBank(files, media, rate, project.tracks[static_cast<std::size_t>(drumEditTrack())].kitVariant);
    if (bank == nullptr)
        return;
    auto* retired = engine.retirePadBank(drumEditTrack(), bank.get());
    bankStorage = std::move(bank);
    if (retired != nullptr)
        juce::Timer::callAfterDelay(600, [retired] { delete retired; });
}


struct MainComponent::AudioView final : public juce::Component
{
    AudioView(std::function<void(int)> inputModeCb, std::function<void(bool)> monitorCb,
              std::function<void(std::uint32_t)> muteCb, std::function<void(std::uint32_t)> deleteCb,
              std::function<void(std::uint32_t)> selectCb, std::function<void()> pitchChangedCb,
              std::function<void()> analyzeCb, std::function<void()> applyCb)
        : onInputMode(std::move(inputModeCb)), onMonitor(std::move(monitorCb)),
          onMuteTake(std::move(muteCb)), onDeleteTake(std::move(deleteCb)),
          onSelectTake(std::move(selectCb)), onPitchChanged(std::move(pitchChangedCb)),
          onAnalyze(std::move(analyzeCb)), onApply(std::move(applyCb))
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
            signature += juce::String(take.id) + (take.mute ? "m" : "") + (take.offline ? "x" : "")
                + juce::String(take.startTick) + ";";
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
                row.name = std::make_unique<juce::Label>();
                row.name->setFont(ui::font(12.0f, true));
                row.name->setColour(juce::Label::textColourId,
                    take.offline ? ui::danger : take.id == selected ? ui::cyan : ui::text);
                const auto bars = juce::String(take.startTick / 3840 + 1) + "." + juce::String((take.startTick / 960) % 4 + 1);
                const auto seconds = juce::String(take.frames / 48000.0, 1);
                row.name->setText("Take " + juce::String(take.id) + "   @ bar " + bars + "   " + seconds + " s   "
                    + (take.channels == 2 ? "stereo" : "mono") + (take.offline ? "   (file missing)" : ""),
                    juce::dontSendNotification);
                addAndMakeVisible(row.name.get());
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
                takeRows.push_back(std::move(row));
            }
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
        takeHint.setText(project.takeCount == 0
            ? (isRecording ? "Recording... press Stop to finish the take."
                           : "Press REC to record from the song start. Takes play back in SONG mode.")
            : "Click a take to inspect its waveform. Delete key removes the selected take.",
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
        // Clicking a row selects its take for waveform inspection.
        for (const auto& row : takeRows)
        {
            if (row.name != nullptr && row.name->getBounds().contains(event.getPosition()))
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
        return { getWidth() - 40, 48, 24, takeRows.empty() ? 60 : static_cast<int>(takeRows.size()) * 28 + 24 };
    }

    void resized() override
    {
        inputMode.setBounds(12, 10, 170, 30);
        monitor.setBounds(192, 10, 90, 30);
        status.setBounds(292, 10, getWidth() - 304, 30);
        int y = 52;
        for (auto& row : takeRows)
        {
            row.name->setBounds(12, y, getWidth() - 220, 24);
            row.mute->setBounds(getWidth() - 196, y, 80, 24);
            row.remove->setBounds(getWidth() - 108, y, 80, 24);
            y += 28;
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

    struct Row
    {
        std::uint32_t id = 0;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::TextButton> mute, remove;
    };
    juce::ComboBox inputMode, pitchKey, pitchScale;
    juce::Slider pitchAmount, pitchSpeed;
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
    std::function<void(int)> onInputMode;
    std::function<void(bool)> onMonitor;
    std::function<void(std::uint32_t)> onMuteTake, onDeleteTake, onSelectTake;
    std::function<void()> onPitchChanged, onAnalyze, onApply;
};

static int activeInputCount(juce::AudioDeviceManager& manager)
{
    if (auto* device = manager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

void MainComponent::ensureAudioInputs()
{
    if (activeInputCount(deviceManager) > 0)
        return;
    setAudioChannels(2, 2);
}

MainComponent::MainComponent()
{
    setLookAndFeel(&theme);
    setWantsKeyboardFocus(true);
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
             &panic, &keyboard, &pianoRoll, &play, &stop, &record, &undo, &redo, &newProject,
             &open, &save, &saveAs, &exportButton, &clear, &demo, &tempo, &drumSequencer,
             &audioTab, &mute, &solo, &trackVolume, &repeatBar, &kitButton, &outputMeter,
             &songMode, &addSection, &removeSection, &duplicatePattern })
        addAndMakeVisible(component);
    for (auto& button : trackButtons)
        addAndMakeVisible(button);
    addAndMakeVisible(addTrack);
    for (auto& tab : patternTabs)
        addAndMakeVisible(tab);
    for (auto& button : sectionButtons) addAndMakeVisible(button);
    fxBar = std::make_unique<FxBar>(
        [this](int target) { fxTarget = target; fxEffect = target == 2 ? 0 : fxEffect; refreshFxBar(); },
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

    // Keep transport shortcuts focused on the editor after toolbar clicks.
    for (auto* button : std::initializer_list<juce::Button*> {
             &play, &stop, &record, &panic, &audioSettings, &undo, &redo,
             &newProject, &open, &save, &saveAs, &exportButton, &clear, &demo, &duplicatePattern,
             &audioTab, &mute, &solo, &repeatBar, &kitButton,
             &songMode, &addSection, &removeSection })
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
        projectChanged();
        endEdit();
    };
    songMode.setClickingTogglesState(true);
    songMode.setColour(juce::TextButton::buttonOnColourId, ui::cyan);
    songMode.onClick = [this] {
        beginEdit();
        project.songMode = songMode.getToggleState();
        engine.stop();
        projectChanged();
        endEdit();
    };
    songMode.setTooltip("Loop repeats the 4-bar pattern. Song plays arranged sections in order, then stops.");
    addSection.onClick = [this] {
        if (project.song.sections >= maxSections) return;
        beginEdit();
        ++project.song.sections;
        project.song.trackOn[static_cast<std::size_t>(project.song.sections - 1)][0] = true;
        project.song.trackOn[static_cast<std::size_t>(project.song.sections - 1)][1] = true;
        engine.stop(); projectChanged(); endEdit();
    };
    removeSection.onClick = [this] {
        if (project.song.sections <= 1) return;
        beginEdit();
        --project.song.sections;
        engine.stop(); projectChanged(); endEdit();
    };
    for (int s = 0; s < maxSections; ++s)
    {
        auto& button = sectionButtons[static_cast<std::size_t>(s)];
        button.setClickingTogglesState(true);
        button.setWantsKeyboardFocus(false);
        button.onClick = [this, s] {
            const auto track = selectedTrack;
            const auto t = static_cast<std::size_t>(track);
            const bool drums = project.tracks[t].kind == TrackKind::Drums;
            beginEdit();
            const auto modifiers = juce::ModifierKeys::getCurrentModifiers();
            // Click toggles the section; Shift-click cycles its pattern slot;
            // Ctrl-click detaches it into its own library copy (make unique).
            if (modifiers.isCommandDown())
            {
                bool detached = false;
                if (drums)
                {
                    detached = makeDrumSectionUnique(project.song, project.tracks[t].drumPatterns, s, track);
                    if (detached)
                        trackDrumSlot[t] = project.song.slots[static_cast<std::size_t>(s)][t];
                }
                else
                {
                    detached = makeSectionUnique(project.song, project.tracks[t].melodies, s, track);
                    if (detached)
                        trackMelodySlot[t] = project.song.slots[static_cast<std::size_t>(s)][t];
                }
                if (!detached)
                    showError("Nothing to detach: this section already stands alone, or every slot is in use.");
            }
            else if (modifiers.isShiftDown())
                project.song.slots[static_cast<std::size_t>(s)][t] =
                    static_cast<std::uint8_t>((project.song.slots[static_cast<std::size_t>(s)][t] + 1) % numPatterns);
            else
            {
                auto& flag = project.song.trackOn[static_cast<std::size_t>(s)][t];
                flag = sectionButtons[static_cast<std::size_t>(s)].getToggleState();
            }
            projectChanged(); endEdit();
        };
        button.setTooltip("Click: section on/off. Shift-click: next pattern. Ctrl-click: make unique.");
    }

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
        button.getProperties().set("role", "track");
        button.setClickingTogglesState(true);
        button.setWantsKeyboardFocus(false);
        button.onClick = [this, track] { selectTrackIndex(track); };
        button.addMouseListener(this, false);
    }
    addTrack.setButtonText("+ track");
    addTrack.setTooltip("Add a synth or drum track (up to 8).");
    addTrack.setWantsKeyboardFocus(false);
    addTrack.onClick = [this] { showAddTrackMenu(); };
    audioTab.getProperties().set("role", "track");
    audioTab.getProperties().set("detail", "RECORDER / TAKES");
    audioTab.setColour(juce::TextButton::buttonOnColourId, ui::blue);
    record.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    record.setClickingTogglesState(true);
    record.setTooltip("Record: starts the song from the top (or punches in while playing). Press again to punch out, Stop to finish the take.");
    play.getProperties().set("role", "primary");
    save.getProperties().set("role", "primary");
    panic.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    mute.setColour(juce::TextButton::buttonOnColourId, ui::danger);
    solo.setColour(juce::TextButton::buttonOnColourId, ui::violet);
    audioTab.onClick = [this] { selectChannel(2); };
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
        if (recording)
            finalizeTake();
        engine.stop();
        engine.keyboardState.allNotesOff(0);
    };
    record.onClick = [this] { toggleRecord(); };
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
    demo.onClick = [this] { if (drumsSelected) loadDrumDemo(); else loadDemo(); };
    demo.setTooltip("Replace the selected pattern slot with a four-bar starter (undoable).");
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
        [this] { applyPitch(); });
    addAndMakeVisible(audioView.get());
    keyboard.setAvailableRange(lowestPitch, highestPitch + 12);
    keyboard.setLowestVisibleKey(lowestPitch);
    keyboard.setKeyWidth(34.0f);
    refreshKeyboardColours();
    auto stateRoot = juce::SystemStats::getEnvironmentVariable("XDG_STATE_HOME", {});
    if (stateRoot.isEmpty() || !juce::File::isAbsolutePath(stateRoot))
        stateRoot = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                        .getChildFile(".local/state").getFullPathName();
    recoveryFile = juce::File(stateRoot).getChildFile("sonora/recovery.sonora.json");
    setSize(1440, 900);
    applyOmarchyTheme(true); // theme first paint matches the desktop
    projectChanged();
    selectChannel(0);
    // Restore the previous audio/MIDI setup before opening channels; a
    // missing or stale file falls back to fresh defaults silently.
    std::unique_ptr<juce::XmlElement> savedAudio;
    if (audioSettingsFile().existsAsFile())
        savedAudio = juce::XmlDocument::parse(audioSettingsFile());
    if (savedAudio != nullptr)
        deviceManager.initialise(2, 2, savedAudio.get(), true);
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
    startTimerHz(30);
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr)
            safe->offerRecovery();
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
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
    bankStorage.reset();
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
    if (inputChannelData != nullptr && numInputChannels > 0 && (monitorInputs || recording))
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
                        if (safe->recording)
                            safe->finalizeTake();
                        safe->engine.stop();
                        safe->engine.keyboardState.allNotesOff(0);
                        break;
                    case midi::McuAction::Record: safe->toggleRecord(); break;
                    case midi::McuAction::ToggleLoop:
                        safe->beginEdit();
                        safe->project.songMode = !safe->project.songMode;
                        safe->engine.stop();
                        safe->projectChanged();
                        safe->endEdit();
                        break;
                    case midi::McuAction::None: break;
                }
            });
        return;
    }
    engine.midiCollector.handleIncomingMidiMessage(source, message);
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
    const auto summary = midi::midiStatusText(active);
    if (summary != midiStatusText)
    {
        midiStatusText = summary;
        updateTrackControls(); // status bar picks the new text up this tick
    }
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
    auto root = juce::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME", {});
    if (root.isEmpty() || !juce::File::isAbsolutePath(root))
        root = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".config").getFullPathName();
    return juce::File(root).getChildFile("sonora/audio.xml");
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

float MainComponent::getFxParam(int slot) const
{
    const TrackFx& fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    if (fxTarget == 2)
        return slot == 0 ? project.master.ceilingDb : project.master.releaseMs;
    switch (fxEffect)
    {
        case 0:
            switch (slot)
            {
                case 0: return fx.eq.low;
                case 1: return fx.eq.mid;
                case 2: return fx.eq.midFreq;
                default: return fx.eq.high;
            }
        case 1:
            switch (slot)
            {
                case 0: return fx.comp.thresholdDb;
                case 1: return fx.comp.ratio;
                case 2: return fx.comp.attackMs;
                default: return fx.comp.releaseMs;
            }
        case 2:
            switch (slot)
            {
                case 0: return fx.delay.timeMs;
                case 1: return fx.delay.feedback;
                default: return fx.delay.mix;
            }
        default:
            switch (slot)
            {
                case 0: return fx.reverb.size;
                case 1: return fx.reverb.damping;
                default: return fx.reverb.mix;
            }
    }
}

void MainComponent::setFxParam(int slot, float value)
{
    TrackFx& fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    if (fxTarget == 2)
    {
        if (slot == 0) project.master.ceilingDb = value;
        else project.master.releaseMs = value;
        return;
    }
    switch (fxEffect)
    {
        case 0:
            if (slot == 0) fx.eq.low = value;
            else if (slot == 1) fx.eq.mid = value;
            else if (slot == 2) fx.eq.midFreq = value;
            else fx.eq.high = value;
            break;
        case 1:
            if (slot == 0) fx.comp.thresholdDb = value;
            else if (slot == 1) fx.comp.ratio = value;
            else if (slot == 2) fx.comp.attackMs = value;
            else fx.comp.releaseMs = value;
            break;
        case 2:
            if (slot == 0) fx.delay.timeMs = value;
            else if (slot == 1) fx.delay.feedback = value;
            else fx.delay.mix = value;
            break;
        default:
            if (slot == 0) fx.reverb.size = value;
            else if (slot == 1) fx.reverb.damping = value;
            else fx.reverb.mix = value;
            break;
    }
}

bool MainComponent::getFxEnabled() const
{
    if (fxTarget == 2)
        return project.master.enabled;
    const TrackFx& fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    switch (fxEffect)
    {
        case 0: return fx.eq.enabled;
        case 1: return fx.comp.enabled;
        case 2: return fx.delay.enabled;
        default: return fx.reverb.enabled;
    }
}

void MainComponent::setFxEnabled(bool enabled)
{
    if (fxTarget == 2)
    {
        project.master.enabled = enabled;
        return;
    }
    TrackFx& fx = project.tracks[static_cast<std::size_t>(fxTrackFor(fxTarget == 1))].fx;
    switch (fxEffect)
    {
        case 0: fx.eq.enabled = enabled; break;
        case 1: fx.comp.enabled = enabled; break;
        case 2: fx.delay.enabled = enabled; break;
        default: fx.reverb.enabled = enabled; break;
    }
}

void MainComponent::projectChanged()
{
    ++revision;
    if (project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::None || audioSelected)
        for (int track = 0; track < maxTracks; ++track)
            if (project.tracks[static_cast<std::size_t>(track)].kind != TrackKind::None)
            {
                selectedTrack = track;
                break;
            }
    const auto sel = static_cast<std::size_t>(selectedTrack);
    const bool selDrums = project.tracks[sel].kind == TrackKind::Drums;
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
    for (int track = 0; track < maxTracks; ++track)
        engine.setLoopSelection(track, trackMelodySlot[static_cast<std::size_t>(track)],
                                trackDrumSlot[static_cast<std::size_t>(track)]);
    tempo.setValue(project.bpm, juce::dontSendNotification);
    songMode.setToggleState(project.songMode, juce::dontSendNotification);
    songMode.setButtonText(project.songMode ? "SONG" : "LOOP");
    for (int s = 0; s < maxSections; ++s)
    {
        const auto si = static_cast<std::size_t>(s);
        const bool inSong = s < project.song.sections && !audioSelected;
        auto& button = sectionButtons[si];
        button.setVisible(inSong);
        button.setToggleState(project.song.trackOn[si][sel], juce::dontSendNotification);
        button.setButtonText(
            juce::String(s + 1) + juce::String::charToString(static_cast<char>(
                'A' + project.song.slots[si][sel])));
        button.setColour(juce::TextButton::buttonOnColourId, trackColour(project.tracks[sel].icon));
        const auto sharers = sectionsSharingSlot(project.song, selectedTrack,
            project.song.slots[si][sel]);
        auto sharedText = [](const std::vector<int>& sharers) {
            juce::String text;
            for (const auto section : sharers)
                text += (text.isEmpty() ? "" : ", ") + juce::String(section);
            return sharers.size() > 1 ? "shared by sections " + text : "unique to this section";
        };
        button.setTooltip(
            "Section " + juce::String(s + 1) + " " + project.tracks[sel].trackName()
            + " (" + sharedText(sharers) + "). "
            "Click: on/off. Shift-click: next pattern. Ctrl-click: make unique.");
    }
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
}

void MainComponent::undoEdit()
{
    if (editing || undoStack.empty())
        return;
    redoStack.push_back(project);
    project = undoStack.back();
    undoStack.pop_back();
    projectChanged();
}

void MainComponent::redoEdit()
{
    if (editing || redoStack.empty())
        return;
    undoStack.push_back(project);
    project = redoStack.back();
    redoStack.pop_back();
    projectChanged();
}

void MainComponent::loadDemo()
{
    beginEdit();
    const auto sel = static_cast<std::size_t>(selectedTrack);
    project.tracks[sel].melodies[static_cast<std::size_t>(trackMelodySlot[sel])] = {};
    constexpr int roots[] { 48, 53, 55, 48 };
    constexpr int melody[] { 60, 64, 67, 64, 65, 69, 67, 65, 67, 71, 69, 67, 64, 62, 60, 67 };
    for (int bar = 0; bar < 4; ++bar)
    {
        auto& p = project.tracks[sel].melodies[static_cast<std::size_t>(trackMelodySlot[sel])];
        p.notes[static_cast<std::size_t>(p.count)] = { static_cast<std::uint32_t>(p.count + 1), bar * 3840, 3360, roots[bar], 85 };
        ++p.count;
        for (int beat = 0; beat < 4; ++beat)
        {
            p.notes[static_cast<std::size_t>(p.count)] = { static_cast<std::uint32_t>(p.count + 1),
                bar * 3840 + beat * 960, 720, melody[bar * 4 + beat], beat == 0 ? 110 : 95 };
            ++p.count;
        }
    }
    projectChanged();
    endEdit();
}

void MainComponent::loadDrumDemo()
{
    beginEdit();
    const auto sel = static_cast<std::size_t>(selectedTrack);
    project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])] = {};
    auto hit = [this, sel](int pad, int step, int velocity) {
        project.tracks[sel].drumPatterns[static_cast<std::size_t>(trackDrumSlot[sel])].steps[static_cast<std::size_t>(pad)][static_cast<std::size_t>(step)]
            = static_cast<std::uint8_t>(velocity);
    };
    for (int bar = 0; bar < 4; ++bar)
    {
        const auto base = bar * 16;
        for (int step : { 0, 6, 8 }) hit(0, base + step, step == 0 ? 120 : 100);
        for (int step : { 4, 12 }) hit(1, base + step, 110);
        for (int step = 0; step < 16; step += 2)
            if (step != 14 || bar % 2 == 0)
                hit(2, base + step, step % 4 == 0 ? 92 : 66);
        if (bar % 2 != 0) hit(3, base + 14, 82);
        hit(4, base + 12, 58);
        for (int step : { 3, 7, 11, 15 }) hit(7, base + step, 55);
    }
    hit(5, 62, 95);
    hit(5, 63, 75);
    projectChanged();
    endEdit();
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
    const bool audio = channel == 2;
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
    demo.setButtonText(drums ? "Demo beat" : "Demo melody");
    description.setText(audio ? "REC: record from song start (or punch in)  |  Takes play in SONG mode  |  Click a take to inspect  |  Delete key removes it"
        : drums ? "Toggle: click  |  Paint: drag  |  Erase: right-drag  |  Velocity: scroll  |  Audition: pad names / QWER ASDF"
        : "Draw: click + drag  |  Move: drag note  |  Resize: right edge / Shift-drag  |  Delete: right-click  |  Velocity: scroll",
        juce::dontSendNotification);
    updateTrackControls();
    refreshAudioView();
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
    menu.addSubMenu("Icon / colour", icons);
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
                           else if (result == 20)
                               moveTrack(selectedTrack, selectedTrack - 1);
                           else if (result == 21)
                               moveTrack(selectedTrack, selectedTrack + 1);
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
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([this, track, window](int result) {
                                std::unique_ptr<juce::AlertWindow> deleter(window);
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
    dragTrack = dragHover = -1;
    for (int track = 0; track < maxTracks; ++track)
        if (event.eventComponent == &trackButtons[static_cast<std::size_t>(track)]
            && project.tracks[static_cast<std::size_t>(track)].kind != TrackKind::None)
        {
            dragTrack = dragHover = track;
            dragStartPos = event.getPosition();
        }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
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

void MainComponent::refreshTakes()
{
    const double rate = currentRate();
    juce::String signature = juce::String(rate, 0) + "|";
    for (int i = 0; i < project.takeCount; ++i)
    {
        const auto& take = project.takes[static_cast<std::size_t>(i)];
        signature += juce::String(take.id) + ":" + take.fileName() + ":" + juce::String(take.startTick)
            + ":" + juce::String(take.frames) + ":" + juce::String(take.gain, 2)
            + (take.mute ? "m" : "") + (take.offline ? "x" : "") + ";";
    }
    if (signature == lastTakesSignature)
        return;
    lastTakesSignature = signature;
    takesRevision = revision;
    takesRate = rate;
    juce::File media = projectFile != juce::File() ? mediaDirFor(projectFile) : sessionDir();
    auto set = loadTakes(project.takes, project.takeCount, media, rate);
    if (set == nullptr)
        return; // load cancelled or failed; keep the previous set live
    // The engine borrows the raw pointer; ownership stays here. The previously
    // installed set may still be read by an audio block in flight, so delete
    // it after a grace period on the message thread. Each installed set is
    // retired exactly once, so delayed deletes never overlap.
    auto* retired = engine.retireTakeSet(set.get());
    takeStorage = std::move(set);
    if (retired != nullptr)
        juce::Timer::callAfterDelay(600, [retired] { delete retired; });
    rebuildWaveCache();
}

void MainComponent::rebuildWaveCache()
{
    waveCache = {};
    if (selectedTake == 0 || takeStorage == nullptr)
    {
        if (audioView != nullptr)
            audioView->setWave({}, 0, 0);
        return;
    }
    for (const auto& take : takeStorage->takes)
    {
        if (take.id != selectedTake || take.audio.getNumSamples() <= 0)
            continue;
        constexpr int buckets = 256;
        waveCache.takeId = take.id;
        waveCache.frames = take.audio.getNumSamples();
        waveCache.peaks.assign(buckets, 0.0f);
        const int channels = take.audio.getNumChannels();
        for (int b = 0; b < buckets; ++b)
        {
            const int from = b * waveCache.frames / buckets;
            const int to = (b + 1) * waveCache.frames / buckets;
            float peak = 0.0f;
            for (int i = from; i < to; i += 7)
                for (int ch = 0; ch < channels; ++ch)
                    peak = std::max(peak, std::abs(take.audio.getSample(ch, i)));
            waveCache.peaks[static_cast<std::size_t>(b)] = peak;
        }
    }
    if (audioView != nullptr)
        audioView->setWave(waveCache.peaks, waveCache.frames, waveCache.takeId);
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
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export complete",
                file.getFileName() + "  /  "
                + juce::String(result.audio.getNumSamples() / result.sampleRate, 1) + " s  /  "
                + juce::String(juce::Decibels::gainToDecibels(result.peak, -80.0f), 1) + " dB peak"
                + (result.clipped ? "  (CLIPPED — lower track volumes or enable normalization)"
                                  : result.normalized ? "  (normalized)" : ""));
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
                CorrectionSettings settingsIn, juce::Component::SafePointer<MainComponent> ownerIn)
        : ThreadWithProgressWindow(modeIn == Mode::Analyze ? "Analyzing pitch" : "Tuning take", true, true),
          mode(modeIn), audio(std::move(audioIn)), rate(rateIn), source(sourceIn),
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
        else
            owner->finishTunedTake(source, std::move(tuned), rate, contour);
        owner->pitchFinished();
    }
    Mode mode;
    juce::AudioBuffer<float> audio, tuned;
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
    pitchWorker = std::make_unique<PitchWorker>(PitchWorker::Mode::Apply, loaded->audio,
        takesRate > 0.0 ? takesRate : currentRate(), meta, pitchSettings, this);
    pitchWorker->launchThread();
}

void MainComponent::finishTunedTake(const AudioTakeMeta& source, juce::AudioBuffer<float> tuned,
                                    double rate, const PitchContour& contour)
{
    if (tuned.getNumSamples() <= 0 || project.takeCount >= maxTakes)
        return;
    const auto file = sessionDir().getNonexistentChildFile("take-" + juce::String(nextTakeId) + "-tuned", ".wav");
    {
        auto* stream = new juce::FileOutputStream(file);
        if (!stream->openedOk())
        {
            delete stream;
            showError("Could not write the tuned take.");
            return;
        }
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream, rate,
            static_cast<unsigned>(tuned.getNumChannels()), 24, {}, 0));
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(tuned, 0, tuned.getNumSamples()))
        {
            showError("Could not write the tuned take.");
            return;
        }
    }
    beginEdit();
    AudioTakeMeta meta;
    meta.id = nextTakeId++;
    meta.setFileName(file.getFileName());
    meta.startTick = source.startTick;
    meta.frames = tuned.getNumSamples();
    meta.gain = source.gain;
    meta.mute = false;
    meta.channels = tuned.getNumChannels();
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
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, ui::background);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, ui::background);
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, ui::cyan.withAlpha(0.65f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ui::cyan.withAlpha(0.16f));
    keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, ui::muted);
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
}

void MainComponent::applyOmarchyTheme(bool force)
{
    const auto fingerprint = omarchy::themeFingerprint();
    if (!force && fingerprint == themeFingerprint)
        return;
    themeFingerprint = fingerprint;
    const auto palette = omarchy::loadOmarchyPalette();
    ui::applyPalette(palette);
    theme.applyPalette();
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
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Sonora", message);
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
            safe->projectChanged();
            safe->refreshTakes();
        });
}

void MainComponent::resetProject()
{
    if (recording)
        finalizeTake();
    engine.stop();
    project = savedProject = {};
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
    juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::QuestionIcon,
        "Save your changes?", "This project has unsaved changes.", "Save", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<MainComponent>(this), action](int answer) {
                if (safe == nullptr)
                    return;
                safe->dialogPending = false;
                if (answer == 1)
                    safe->saveProject(false, action);
                else if (answer == 2)
                    action();
            }));
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
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
        "Recover unsaved work?", "Sonora found a recovery snapshot from an earlier session.", "Recover", "Discard", this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<MainComponent>(this)](int answer) {
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
        }));
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
    options.dialogBackgroundColour = juce::Colour(0xff181e2a);
    options.content.setOwned(new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2,
                                                                   true, false, true, false));
    options.content->setSize(580, 480);
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    audioDialog = options.launchAsync();
}

void MainComponent::timerCallback()
{
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
    const auto tick = static_cast<int>(engine.getTickPosition());
    const auto currentSection = juce::jlimit(0, maxSections - 1, tick / patternTicks);
    if (currentSection != lastSection || running != lastSectionRunning)
    {
        lastSection = currentSection;
        lastSectionRunning = running;
        for (int s = 0; s < maxSections; ++s)
        {
            const bool active = running && project.songMode && s == currentSection && s < project.song.sections;
            sectionButtons[static_cast<std::size_t>(s)].setColour(juce::TextButton::buttonColourId,
                active ? trackColour(project.tracks[static_cast<std::size_t>(selectedTrack)].icon)
                               .withMultipliedBrightness(0.35f)
                       : ui::raised);
        }
    }
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
        if (!audioErrorMessage.isEmpty())
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
        return true;
    }
    if (key == juce::KeyPress::escapeKey && kitPanel != nullptr && kitPanel->isVisible())
    {
        kitPanel->setVisible(false);
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
        if (key.getKeyCode() == '3') { selectChannel(2); return true; }
        if (key.getKeyCode() == 'S') { saveProject(key.getModifiers().isShiftDown()); return true; }
        if (key.getKeyCode() == 'E') { exportAudio(); return true; }
        if (key.getKeyCode() == 'O') { confirmDiscard([this] { openProject(); }); return true; }
        if (key.getKeyCode() == 'N') { confirmDiscard([this] { resetProject(); }); return true; }
        if (key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown()) redoEdit(); else undoEdit();
            return true;
        }
    }
    else if (!key.getModifiers().isAltDown() && !audioSelected
             && key.getKeyCode() >= '1' && key.getKeyCode() <= '8')
    {
        selectTrackIndex(key.getKeyCode() - '1');
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
    return false;
}

void MainComponent::paint(juce::Graphics& g)
{
    const auto w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
    const auto accent = audioSelected ? ui::blue : drumsSelected ? ui::violet : ui::cyan;
    g.fillAll(ui::background);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff142337), 80, 0,
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
    ui::caption(g, "TEMPO", { 276, 118, 120, 15 }, ui::muted, 9.0f);
    ui::caption(g, "POSITION  /  BAR . BEAT . STEP", { 454, 118, 222, 15 }, ui::muted, 9.0f);
    ui::caption(g, "ARRANGEMENT", { 790, 118, 220, 15 }, ui::muted, 9.0f);
    for (float x : { 268.0f, 434.0f })
    {
        g.setColour(ui::border);
        g.drawVerticalLine(static_cast<int>(x), 128, 173);
    }
    if (getWidth() > 1280)
    {
        ui::caption(g, "4 / 4", { 704, 134, 80, 20 }, ui::text, 15.0f);
        ui::caption(g, "4 BAR LOOP", { 704, 158, 95, 15 }, ui::muted, 9.0f);
    }

    ui::surface(g, { 20, 208, 204, h - 266 });
    ui::caption(g, "CHANNEL RACK", { 38, 222, 170, 20 });
    ui::caption(g, "CHANNEL GAIN", { 38, 550, 170, 16 });
    if (getHeight() > 880)
    {
        ui::caption(g, audioSelected ? "TAKE ENGINE" : drumsSelected ? "ONE-SHOT ENGINE" : "SINE ENGINE",
                    { 38, 690, 172, 20 }, accent);
        ui::caption(g, audioSelected ? juce::String(project.takeCount) + " TAKES / SONG ONLY"
                    : drumsSelected ? "8 PADS / 64 STEPS" : "16 VOICES / 2 OCTAVES",
                    { 38, 714, 172, 18 }, ui::muted, 9.0f);
    }
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
    ui::caption(g, audioSelected ? "AUDIO TAKES" : drumsSelected ? "RHYTHM MATRIX" : "NOTE MATRIX",
                { 270, 223, 234, 26 }, ui::text, 15.0f);
    g.setColour(ui::border.withAlpha(0.7f));
    g.drawHorizontalLine(278, 254, w - 38);
    ui::surface(g, { 240, h - 172, w - 264, 114 });
    ui::caption(g, audioSelected ? "TAKE INSPECTOR" : drumsSelected ? "PERFORMANCE PADS" : "PERFORMANCE KEYS",
                { 256, getHeight() - 169, 280, 22 }, accent, 9.0f);
    g.setColour(ui::border.withAlpha(0.5f));
    g.drawHorizontalLine(getHeight() - 44, 24, w - 24);
    ui::caption(g, "SONORA  /  NATIVE AUDIO", { getWidth() - 237, getHeight() - 34, 211, 20 }, ui::muted, 9.0f);
}

void MainComponent::resized()
{
    title.setBounds(78, 22, 254, 40);
    subtitle.setBounds(350, 33, getWidth() - 920, 28);
    auto fileRow = juce::Rectangle<int>(getWidth() - 548, 27, 524, 34);
    for (auto* button : { &newProject, &open, &save, &saveAs, &exportButton })
    {
        button->setBounds(fileRow.removeFromLeft(98));
        fileRow.removeFromLeft(7);
    }
    play.setBounds(38, 138, 92, 34);
    stop.setBounds(138, 138, 56, 34);
    record.setBounds(202, 138, 58, 34);
    tempo.setBounds(274, 139, 144, 32);
    position.setBounds(450, 134, 250, 42);
    songMode.setBounds(716, 138, 84, 34);
    removeSection.setBounds(808, 138, 34, 34);
    addSection.setBounds(848, 138, 34, 34);
    undo.setBounds(getWidth() - 416, 138, 62, 34);
    redo.setBounds(getWidth() - 346, 138, 62, 34);
    panic.setBounds(getWidth() - 266, 138, 76, 34);
    audioSettings.setBounds(getWidth() - 180, 138, 142, 34);
    for (int track = 0; track < maxTracks; ++track)
        trackButtons[static_cast<std::size_t>(track)].setBounds(34, 246 + track * 29, 176, 26);
    audioTab.setBounds(34, 482, 124, 28);
    addTrack.setBounds(162, 482, 48, 28);
    mute.setBounds(38, 516, 78, 30);
    solo.setBounds(126, 516, 78, 30);
    trackVolume.setBounds(44, 568, 156, 80);
    clear.setBounds(getWidth() - 242, 222, 66, 30);
    demo.setBounds(getWidth() - 168, 222, 130, 30);
    kitButton.setBounds(getWidth() - 330, 222, 80, 30);
    description.setBounds(256, 256, getWidth() - 294, 18);
    repeatBar.setBounds(getWidth() - 470, 222, 132, 30);
    const int sectionWidth = 46;
    for (int s = 0; s < maxSections; ++s)
        sectionButtons[static_cast<std::size_t>(s)].setBounds(624 + s * (sectionWidth + 4), 222, sectionWidth, 24);
    for (int i = 0; i < numPatterns; ++i)
        patternTabs[static_cast<std::size_t>(i)].setBounds(420 + i * 32, 222, 28, 24);
    duplicatePattern.setBounds(556, 222, 60, 24);
    pianoRoll.setBounds(254, 288, getWidth() - 292, getHeight() - 568);
    drumSequencer.setBounds(pianoRoll.getBounds());
    if (audioView != nullptr)
        audioView->setBounds(pianoRoll.getBounds());
    if (fxBar != nullptr)
        fxBar->setBounds(240, getHeight() - 268, getWidth() - 264, 88);
    keyboard.setBounds(256, getHeight() - 140, getWidth() - 296, 68);
    keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / 21.0f);
    const auto padWidth = (getWidth() - 288) / drumPads;
    for (int pad = 0; pad < drumPads; ++pad)
        padButtons[static_cast<std::size_t>(pad)].setBounds(254 + pad * padWidth,
            getHeight() - 144, padWidth - 6, 74);
    status.setBounds(24, getHeight() - 36, getWidth() - 282, 24);
    outputMeter.setBounds(34, getHeight() - 98, 174, 24);
    if (exportPanel != nullptr)
        exportPanel->setBounds((getWidth() - 460) / 2, (getHeight() - 300) / 2, 460, 300);
    if (kitPanel != nullptr)
        kitPanel->setBounds((getWidth() - 560) / 2, (getHeight() - 460) / 2, 560, 460);
}
}
