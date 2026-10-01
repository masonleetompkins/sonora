#pragma once
#include "NeonTheme.h"
#include "Pattern.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
// Song view: parts across the top, one row per track. Each track row starts
// with its four loops as chips (A-D) that can be dragged onto parts; cells
// show which loop plays in each part. All edits leave as Actions so the owner
// can apply them with undo.
struct ArrangementState
{
    Arrangement song;
    int musicKey = 0; // song key, so chord cells can show their transpose
    std::array<bool, maxTracks> used {}, drums {};
    std::array<juce::String, maxTracks> names;
    std::array<juce::Colour, maxTracks> colours;
    std::array<std::array<int, numPatterns>, maxTracks> contentCount {}; // notes or hits per slot
    int selected = 0;       // part playback starts from
    double playTick = -1.0; // song playhead in ticks; negative when stopped
};

struct ArrangementAction
{
    enum class Kind { SetCell, SelectSection, EditPart, EditLoop, SetPartType, Duplicate, Insert, Remove, Move, Add, MakeUnique, SetChord };
    Kind kind = Kind::SelectSection;
    int section = -1, track = -1, value = 0;
    bool on = false;
};

class ArrangementView final : public juce::Component
{
public:
    std::function<void(const ArrangementAction&)> onAction;
    std::function<void()> onGestureBegin, onGestureEnd;

    void setState(const ArrangementState& next);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    static juce::Colour partColour(SongPart part);

private:
    struct Hit
    {
        enum class Area { None, Header, Add, Cell, Chip } area = Area::None;
        int section = -1, row = -1, slot = -1;
        bool chord = false; // lower header strip: the section's chord
    };
    struct Layout
    {
        float headerTop = 0, headerHeight = 64, gridTop = 0, rowHeight = 40;
        float labelWidth = 236, gridLeft = 0, columnWidth = 80, addWidth = 44;
    };
    Layout layout() const;
    std::vector<int> rows() const;
    Hit locate(juce::Point<float> p) const;
    juce::Rectangle<float> cellBounds(int section, int row) const;
    juce::Rectangle<float> chipBounds(int row, int slot) const;
    juce::Rectangle<float> headerBounds(int section) const;
    void emit(ArrangementAction action);
    void paintCell(const juce::MouseEvent& event);
    void showCellMenu(int section, int track);
    void showHeaderMenu(int section);
    void showChordMenu(int section);
    juce::Rectangle<float> chordBounds(int section) const;

    ArrangementState state;
    enum class Drag { None, Paint, Chip, Header } drag = Drag::None;
    int dragTrack = -1, dragSlot = 0, dragSection = -1, dropSection = -1, lastPainted = -1;
    bool paintOn = true, dragMoved = false;
    juce::Point<float> dragPoint;
    Hit hover;
};
}
