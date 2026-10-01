#pragma once
#include "InstrumentSearch.h"
#include "Pattern.h"
#include <functional>
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>

namespace sonora::agent
{
// The in-app agent's hands. The model answers with a list of actions; this
// file turns them into project edits. Everything here is pure (a function of
// the project and the actions), so the whole thing is testable without Claude,
// and every route into the project goes through the same validation the UI
// uses. Model output is untrusted: every field is checked and clamped, and a
// bad action is skipped and reported, never half-applied.

// ---- Parameter registry ------------------------------------------------------
// Every knob in the app that is a plain number, by dotted name. The agent sets
// them through one generic action, and the prompt documents them from this
// same table, so a new parameter becomes controllable by adding one line.
struct ParamSpec
{
    juce::String name;
    bool perTrack = true;               // needs a track index (else project-wide)
    float min = 0.0f, max = 1.0f;
    bool isBool = false, isInt = false;
    juce::String note;                  // short hint for the prompt
    std::vector<juce::String> choices;  // names for enumerated values
    std::function<float(const ProjectState&, int)> get;
    std::function<void(ProjectState&, int, float)> set;
};

const std::vector<ParamSpec>& parameters();
const ParamSpec* findParam(const juce::String& name);
// Compact reference for the prompt: name, range, default.
juce::String describeParameters();
// The parameters of `track` that differ from a fresh track, as "name=value".
juce::String describeChangedParameters(const ProjectState& project, int track);

// ---- Actions -----------------------------------------------------------------
struct Hit
{
    int pad = 0, step = 0, velocity = 100;
};

struct SectionSpec
{
    SongPart part = SongPart::Section;
    std::vector<std::pair<int, int>> tracks; // track, loop 0-3
    SectionChord chord;
};

struct Action
{
    juce::String op;
    int track = -1, loop = -1, toLoop = -1, section = -1, to = -1, take = -1;
    std::optional<double> value, value2;
    std::optional<bool> on, latch;
    juce::String text, param, name, kind, instrument, key, scale, part, target, view, arp, rate, chordType;
    std::vector<Note> notes;
    std::vector<Hit> hits;
    std::vector<AutomationPoint> points;
    std::vector<SectionSpec> sections;
    bool hasNotes = false, hasHits = false, hasPoints = false, hasSections = false;
};

// Parses the model's JSON. Unknown fields are ignored; wrong types become an
// error for that action only.
bool parseAction(const juce::var& json, Action& out, juce::String& error);
// At most `maxActions` are kept; the rest are reported as skipped.
inline constexpr int maxActions = 120;
std::vector<Action> parseActions(const juce::var& list, juce::StringArray& errors);

// What an action asks of the app itself rather than the project. The caller
// runs these after the project edit lands; they use the same buttons and
// dialogs a person would, so confirmations still apply.
struct UiEffect
{
    enum class Kind
    {
        Play, Stop, Panic, SetView, SelectTrack, EditSection, Save, Export, Undo, Redo, NewProject,
        OpenProject, ToggleRecord, TakeToSampler, DeleteTake
    };
    Kind kind = Kind::Stop;
    int number = -1;
    juce::String text;
};

struct Context
{
    int selectedTrack = 0;
    std::array<int, maxTracks> loopSlot {}; // the loop each track is showing (default for omitted `loop`)
    std::vector<juce::String> library;      // sample library names
};

struct Report
{
    juce::StringArray done, failed;
    std::vector<UiEffect> effects;
    bool changed = false;
};

// Applies the actions in order to `project`. The project is left untouched if
// the result would not validate. Never throws on bad input.
Report applyActions(ProjectState& project, const std::vector<Action>& actions, const Context& context);

// Closest match for a spoken sound name (instrument, synth patch, or library
// sample). `suggestions` gets the best few names when nothing matches.
std::optional<PickerEntry> resolveSound(const juce::String& text, const std::vector<juce::String>& library,
                                        juce::StringArray* suggestions = nullptr);

// Name parsers shared with the prompt and tests (all case-insensitive).
std::optional<int> parsePitchClass(const juce::String& text);                 // "C", "f#", "Bb" -> 0-11
std::optional<MusicScale> parseScale(const juce::String& text);
std::optional<ChordType> parseChordType(const juce::String& text);
std::optional<SongPart> parseSongPart(const juce::String& text);
std::optional<SongTemplate> parseTemplate(const juce::String& text);
std::optional<AutomationTarget> parseAutomationTarget(const juce::String& text);
int parseLoop(const juce::String& text); // "A".."D" or "0".."3"; -1 if not a loop
}
