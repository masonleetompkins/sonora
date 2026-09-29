#pragma once
#include "AudioEngine.h"
#include "PianoRoll.h"
#include "DrumSequencer.h"
#include "ProjectIO.h"
#include "Export.h"
#include "KitSamples.h"
#include "KnobMaps.h"
#include "ArrangementView.h"
#include "AiSidebar.h"
#include "IdeaCapture.h"
#include "AiDictation.h"
#include "MiniLabDisplay.h"
#include "AiMelody.h"
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
    // Every prompt is hosted inside Sonora's own window. Separate desktop
    // windows get tiled away from the app by tiling WMs (Hyprland) while
    // still blocking input, which leaves the app looking frozen.
    void showDialog(const juce::MessageBoxOptions& options, std::function<void(int)> callback = {});
    void hostModal(juce::Component* dialog);
    void offerRecovery();
    void exportAudio();
    void selectTrack(bool drums);
    void selectChannel(int channel);
    void selectPattern(int slot);
    void refreshAudioView();
    void updateTrackControls();
    void auditionPad(int pad);
    void toggleRecord();
    void finalizeTake();
    // Opens stereo inputs on the current device if none are active. Called
    // when recording becomes possible (Audio tab, monitoring, REC) so plain
    // playback never touches input hardware like Bluetooth headset mics.
    void ensureAudioInputs();
    void deleteTake(std::uint32_t id);
    void refreshTakes();
    void rebuildWaveCache();
    void collectTakes(const juce::File& projectFile);
    void resolveTakes(const juce::File& projectFile);
    juce::File resolveTakeFile(const AudioTakeMeta& take) const;
    double currentRate() const;
    int currentInputLatency() const;
    TrackMix& selectedMix()
    {
        if (audioSelected)
            return project.tracks[0].mix;
        return project.tracks[static_cast<std::size_t>(std::clamp(selectedTrack, 0, maxTracks - 1))].mix;
    }
    bool dirty() const { return recoveredUnsaved || !(project == savedProject); }

    AudioEngine engine;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    PianoRoll pianoRoll;
    DrumSequencer drumSequencer;
    juce::Label title, subtitle, description, status, position, outputMeter;
    juce::TextButton audioSettings { "Audio / MIDI" }, panic { "Panic" };
    juce::TextButton play { "Play" }, stop { "Stop" }, record { "REC" }, ideaButton { "Idea REC" }, undo { "Undo" }, redo { "Redo" };
    juce::TextButton newProject { "New" }, open { "Open" }, save { "Save" }, saveAs { "Save as" };
    juce::TextButton exportButton { "Export" };
    juce::TextButton clear { "Clear" }, demo { "Demo melody" };
    juce::TextButton duplicatePattern { "Dup" };
    std::array<juce::TextButton, numPatterns> patternTabs;
    std::array<juce::TextButton, maxTracks> trackButtons;
    juce::TextButton addTrack { "+" };
    juce::TextButton audioTab { "Audio" };
    juce::TextButton mute { "Mute" }, solo { "Solo" }, repeatBar { "Repeat bar 1" };
    juce::TextButton kitButton { "Kit" };
    juce::ComboBox instrumentChoice;
    juce::TextButton editSynth { "Edit sound" };
    // LOOP edits 4-bar patterns; SONG shows the arrangement board. The view
    // also sets playback (loop vs whole song).
    juce::TextButton loopView { "LOOP" }, songView { "SONG" };
    juce::ComboBox songTemplate, partChoice;
    juce::TextButton partTrackOn { "Plays in this part" };
    juce::Label partHint;
    std::unique_ptr<ArrangementView> arrangement;
    // editPart >= 0: LOOP view previews and edits that song part (all tracks
    // use its loops; tracks silent in it are muted). songStartPart: where
    // song playback starts.
    int editPart = -1, songStartPart = 0;
    void setSongView(bool song);
    void refreshArrangement();
    void handleArrangementAction(const ArrangementAction& action);
    void updateSongControls();
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
    struct SynthPanel;
    struct AssistantWorker;
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
    std::unique_ptr<SynthPanel> synthPanel;
    // AI assistant sidebar (right edge). Docked beside the editor when the
    // window is wide enough, otherwise it floats over the editor's right side.
    static constexpr int sidebarWidth = 380;
    std::unique_ptr<AiSidebar> aiSidebar;
    std::unique_ptr<AssistantWorker> assistantWorker;
    bool transcribing = false;
    std::vector<ai::ChatTurn> chatHistory, songHistory; // track chat / song composer
    static constexpr juce::uint32 songChatId = 0xFFFFFFFFu;
    juce::uint32 assistantStartedAt = 0, chatTrackId = 0;
    bool claudeAvailable = false;
    bool sidebarOpen() const;
    bool sidebarDocked() const { return getWidth() - sidebarWidth >= 1120; }
    int contentWidth() const { return sidebarOpen() && sidebarDocked() ? getWidth() - sidebarWidth : getWidth(); }
    void toggleAiSidebar();
    void refreshAiSidebar();
    void sendToAssistant(const juce::String& text);
    void assistantFinished(const ai::AssistantResult& result, juce::uint32 trackId, int slot);
    void songComposerFinished(const ai::SongResult& result, const std::array<juce::uint32, maxTracks>& ids);
    void startAiJob(std::function<void(const std::atomic<bool>*)> job);
    void finishAiJob();
    void toggleIdeaRecord();
    void finishIdeaRecord();
    void ideaFinished(IdeaResult raw, ai::AssistantResult refined, std::uint32_t trackId, int slot);
    void toggleDictation();
    std::unique_ptr<PitchWorker> pitchWorker;
    void refreshKitPanel();
    void refreshSynthPanel();
    void applySynthPatch(int patch);
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
    int fxTarget = 0, fxEffect = 1; // EQ tab (ids follow the signal chain)
    void refreshFxBar();
    float getFxParam(int slot) const;
    void setFxParam(int slot, float value);
    bool getFxEnabled() const;
    void setFxEnabled(bool enabled);
    void exportFinished();
    std::unique_ptr<juce::FileChooser> chooser;
    ProjectState project = defaultProject(), savedProject = project, editStart;
    ExportJob pendingJob;
    std::vector<ProjectState> undoStack, redoStack;
    juce::File projectFile, recoveryFile;
    bool pendingPublish = true, editing = false, dialogPending = false, recoveredUnsaved = false;
    std::uint64_t revision = 0, recoveredRevision = 0;
    int timerTicks = 0;
    bool drumsSelected = false, audioSelected = false, lastRecordingShown = false;
    // Editor-selected instrument track, plus per-track loop-preview library
    // slots (UI-only; song mode follows the arrangement instead).
    int selectedTrack = 0;
    std::array<int, maxTracks> trackMelodySlot {}, trackDrumSlot {};
    void selectTrackIndex(int track);
    void setTrackIcon(int track, int icon);
    void moveTrack(int from, int to);
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    int dragTrack = -1, dragHover = -1;
    juce::Point<int> dragStartPos;
    // Drum-kit editors follow the selected drum track, else the first one.
    int drumEditTrack() const
    {
        if (project.tracks[static_cast<std::size_t>(selectedTrack)].kind == TrackKind::Drums)
            return selectedTrack;
        for (int track = 0; track < maxTracks; ++track)
            if (project.tracks[static_cast<std::size_t>(track)].kind == TrackKind::Drums)
                return track;
        return 1;
    }
    // MEL/DRM effect targets follow the first synth/drum track.
    int fxTrackFor(bool drums) const
    {
        if (!audioSelected)
            return selectedTrack;
        for (int track = 0; track < maxTracks; ++track)
        {
            const auto kind = project.tracks[static_cast<std::size_t>(track)].kind;
            if (drums ? kind == TrackKind::Drums : kind == TrackKind::Synth)
                return track;
        }
        return drums ? 1 : 0;
    }
    void refreshTrackList();
    void showTrackMenu(int track);
    void showAddTrackMenu();
    void renameTrack(int track);
    void addTrackOfKind(TrackKind kind);
    void deleteTrack(int track);
    // Recording state (message thread owns transitions; audio thread only
    // pushes into the recorder FIFO and reads atomics).
    TakeRecorder recorder;
    TakeRecorder ideaRecorder;
    TakeRecorder dictationRecorder;
    std::atomic<bool> dictationRecording { false };
    juce::File dictationFile;
    double dictationStarted = 0.0;
    std::atomic<bool> ideaRecording { false };
    juce::CriticalSection ideaMutex;
    std::vector<IdeaEvent> ideaEvents;
    double ideaStarted = 0.0, ideaRate = 48000.0;
    juce::File ideaFile;
    std::uint32_t ideaTrackId = 0;
    int ideaSlot = 0;
    TrackKind ideaKind = TrackKind::None;
    bool ideaHasAudio = false;
    bool recording = false, monitorInputs = false;
    std::atomic<std::int64_t> lastMidiMillis { 0 };
    int inputMode = 0, recordChannels = 1, recordStartTick = 0;
    juce::File recordFile;
    std::atomic<float> inputPeak { 0.0f };
    std::unique_ptr<const TakeSet> takeStorage;
    std::array<std::unique_ptr<const SampleBank>, maxTracks> bankStorage;
    std::array<juce::String, maxTracks> lastBankSignature;
    std::uint64_t takesRevision = 0;
    double takesRate = 0.0;
    std::uint32_t selectedTake = 0, nextTakeId = 1;
    WaveCache waveCache;
    juce::String lastTakesSignature;
    juce::File audioSettingsFile();
    void saveAudioSettings();
    juce::String audioErrorMessage;
    std::uint64_t themeFingerprint = 0;
    // Appearance mode for the header theme toggle: Light/Dark force the
    // built-in palettes, System follows the Omarchy theme. Persisted in ui.json.
    omarchy::ThemeMode themeMode = omarchy::ThemeMode::Light;
    juce::TextButton themeButton { "☾" };
    juce::File uiSettingsFile();
    void loadUiSettings();
    void saveUiSettings();
    void refreshThemeButton();
    void applyOmarchyTheme(bool force = false);
    void refreshKeyboardColours();
    // MIDI hardware plug-and-play: auto-enabled input identifiers, MCU port
    // identifiers for transport routing, and a status-bar summary.
    juce::StringArray mcuDeviceIds;
    juce::String midiStatusText;
    bool lastMiniLab = false;
    void autoConnectMidi();
    // MiniLab knobs: the MIDI thread only stores the latest absolute value per
    // knob; the UI timer applies them to the selected track's knob map, with
    // one undo step per gesture (closed after a short idle).
    std::array<std::atomic<int>, 8> knobValues;
    std::array<int, 8> appliedKnobValues;
    std::atomic<unsigned> knobSerial { 0 };
    unsigned lastKnobSerial = 0;
    bool knobGesture = false;
    juce::uint32 knobIdleUntil = 0, knobHighlightUntil = 0;
    int lastKnob = -1;
    void applyKnobChanges();
    void paintKnobStrip(juce::Graphics& g);
    juce::Rectangle<int> knobStripArea() const;
    // On-screen dragging of the knob strip (same geometry as the paint code).
    // One mouse gesture is one undo step; absolute MIDI knobs stay in sync so
    // the next hardware turn starts from the dragged value instead of jumping.
    juce::Rectangle<float> knobChipRect(int knob) const;
    int screenKnob = -1, screenKnobStartY = 0;
    float screenKnobStart = 0.0f;
    bool screenKnobEditing = false;
    // MiniLab 3 screen + pad feedback (DAW program only). Output is opened
    // alongside the auto-connected inputs; replies arrive as SysEx.
    enum class MiniLabMode { Unknown, Arturia, Daw };
    std::unique_ptr<juce::MidiOutput> miniLabOut;
    juce::String miniLabOutId;
    MiniLabMode miniLabMode = MiniLabMode::Unknown;
    std::uint8_t miniLabPadBank = minilab::padBankA;
    juce::String miniLabTop, miniLabBottom;
    std::uint32_t miniLabPadRgb = 0;
    juce::uint32 miniLabRefreshedAt = 0, knobScreenUntil = 0;
    bool miniLabHintShown = false;
    void openMiniLabOutput();
    void miniLabSend(const minilab::Bytes& bytes);
    void handleMiniLabSysex(const minilab::Bytes& bytes);
    void refreshMiniLabDisplay(bool force);
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
