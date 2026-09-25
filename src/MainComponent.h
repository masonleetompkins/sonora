#pragma once
#include "AudioEngine.h"
#include "PianoRoll.h"
#include "DrumSequencer.h"
#include "ProjectIO.h"
#include "Export.h"
#include "KitSamples.h"
#include "NeonTheme.h"
#include "PitchCorrect.h"
#include <map>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

namespace sonora
{
class MainComponent final : public juce::AudioAppComponent,
                              private juce::Timer,
                              private juce::AudioIODeviceCallback,
                              private juce::MidiInputCallback,
                              private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;
    void prepareToPlay(int, double) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;
    // Second device callback (registered after the player's): taps inputs for
    // metering/recording and adds software monitoring to the finished mix.
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    // A dead device must never take the transport or a take down with it:
    // park everything and say so in the status bar.
    void audioDeviceError(const juce::String& message) override;
    // MIDI router: everything musical flows to the engine collector, except
    // Mackie transport notes from MCU ports, which drive the transport.
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void releaseResources() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void requestClose(std::function<void()> callback);

private:
    void timerCallback() override;
    void openAudioSettings();
    void beginEdit();
    void endEdit();
    void projectChanged();
    void undoEdit();
    void redoEdit();
    void saveProject(bool saveAs, std::function<void()> after = {});
    void openProject();
    void resetProject();
    void confirmDiscard(std::function<void()> action);
    void showError(const juce::String& message);
    void offerRecovery();
    void exportAudio();
    void loadDemo();
    void loadDrumDemo();
    void selectTrack(bool drums);
    void selectChannel(int channel);
    void selectPattern(int slot);
    void refreshAudioView();
    void updateTrackControls();
    void auditionPad(int pad);
    void toggleRecord();
    void finalizeTake();
    void deleteTake(std::uint32_t id);
    void refreshTakes();
    void rebuildWaveCache();
    void collectTakes(const juce::File& projectFile);
    void resolveTakes(const juce::File& projectFile);
    juce::File resolveTakeFile(const AudioTakeMeta& take) const;
    double currentRate() const;
    int currentInputLatency() const;
    TrackMix& selectedMix() { return drumsSelected ? project.drumMix : project.melodyMix; }
    bool dirty() const { return recoveredUnsaved || !(project == savedProject); }

    AudioEngine engine;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    PianoRoll pianoRoll;
    DrumSequencer drumSequencer;
    juce::Label title, subtitle, description, status, position, outputMeter;
    juce::TextButton audioSettings { "Audio / MIDI" }, panic { "Panic" };
    juce::TextButton play { "Play" }, stop { "Stop" }, record { "REC" }, undo { "Undo" }, redo { "Redo" };
    juce::TextButton newProject { "New" }, open { "Open" }, save { "Save" }, saveAs { "Save as" };
    juce::TextButton exportButton { "Export" };
    juce::TextButton clear { "Clear" }, demo { "Demo melody" };
    juce::TextButton duplicatePattern { "Dup" };
    std::array<juce::TextButton, numPatterns> patternTabs;
    juce::TextButton melodyTab { "01  Sine keys" }, drumsTab { "02  Starter drums" };
    juce::TextButton audioTab { "03  Audio" };
    juce::TextButton mute { "Mute" }, solo { "Solo" }, repeatBar { "Repeat bar 1" };
    juce::TextButton kitButton { "Kit" };
    juce::TextButton songMode { "Song" }, addSection { "+" }, removeSection { "-" };
    std::array<juce::TextButton, maxSections> melodySections, drumSections;
    std::array<juce::TextButton, drumPads> padButtons;
    juce::Slider tempo, trackVolume;
    ui::NeonTheme theme;
    juce::TooltipWindow tooltips { this, 700 };
    juce::Component::SafePointer<juce::DialogWindow> audioDialog;
    struct ExportWorker;
    struct ExportPanel;
    struct FxBar;
    struct AudioView;
    struct PitchWorker;
    struct KitPanel;
    struct WaveCache
    {
        std::uint32_t takeId = 0;
        std::vector<float> peaks;
        int frames = 0;
    };
    std::unique_ptr<ExportWorker> exportWorker;
    std::unique_ptr<ExportPanel> exportPanel;
    std::unique_ptr<FxBar> fxBar;
    std::unique_ptr<AudioView> audioView;
    std::unique_ptr<KitPanel> kitPanel;
    std::unique_ptr<PitchWorker> pitchWorker;
    void refreshKitPanel();
    void loadPadSample(int pad);
    void clearPadSample(int pad);
    void collectSamples(const juce::File& destination);
    void refreshPadBank();
    void loadPresetSelection(int id);
    void saveKitAsPreset();
    void deleteSelectedPreset();
    juce::String lastPresetName;
    CorrectionSettings pitchSettings;
    std::map<std::uint32_t, PitchContour> pitchContours;
    void analyzeTake(std::uint32_t id);
    void refreshPitchDisplay();
    void applyPitch();
    void finishTunedTake(const AudioTakeMeta& source, juce::AudioBuffer<float> tuned,
                         double rate, const PitchContour& contour);
    void pitchFinished();
    const PreloadedTake* findLoadedTake(std::uint32_t id) const;
    int fxTarget = 0, fxEffect = 0;
    void refreshFxBar();
    float getFxParam(int slot) const;
    void setFxParam(int slot, float value);
    bool getFxEnabled() const;
    void setFxEnabled(bool enabled);
    void exportFinished();
    std::unique_ptr<juce::FileChooser> chooser;
    ProjectState project, savedProject, editStart;
    ExportJob pendingJob;
    std::vector<ProjectState> undoStack, redoStack;
    juce::File projectFile, recoveryFile;
    bool pendingPublish = true, editing = false, dialogPending = false, recoveredUnsaved = false;
    std::uint64_t revision = 0, recoveredRevision = 0;
    int timerTicks = 0;
    bool drumsSelected = false, audioSelected = false, lastRecordingShown = false;
    // Editor-selected library patterns (UI-only, default slot A). Loop-mode
    // playback previews these; song mode follows the arrangement instead.
    int melodyPatternSel = 0, drumPatternSel = 0;
    // Recording state (message thread owns transitions; audio thread only
    // pushes into the recorder FIFO and reads atomics).
    TakeRecorder recorder;
    bool recording = false, monitorInputs = false;
    int inputMode = 0, recordChannels = 1, recordStartTick = 0;
    juce::File recordFile;
    std::atomic<float> inputPeak { 0.0f };
    std::unique_ptr<const TakeSet> takeStorage;
    std::unique_ptr<const SampleBank> bankStorage;
    juce::String lastBankSignature;
    std::uint64_t takesRevision = 0;
    double takesRate = 0.0;
    std::uint32_t selectedTake = 0, nextTakeId = 1;
    WaveCache waveCache;
    juce::String lastTakesSignature;
    juce::File audioSettingsFile();
    void saveAudioSettings();
    juce::String audioErrorMessage;
    std::uint64_t themeFingerprint = 0;
    void applyOmarchyTheme(bool force = false);
    void refreshKeyboardColours();
    // MIDI hardware plug-and-play: auto-enabled input identifiers, MCU port
    // identifiers for transport routing, and a status-bar summary.
    juce::StringArray mcuDeviceIds;
    juce::String midiStatusText;
    bool lastMiniLab = false;
    void autoConnectMidi();
    bool lastRunning = false, lastSectionRunning = false, lastCanUndo = false, lastCanRedo = false;
    int lastSection = -1;
    float lastReductionDb = 0.0f;
    juce::String lastPositionText;
    float meterPeak = 0.0f;
    std::array<int, drumPads> padFlashes {};
    int previousDrumStep = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
}
