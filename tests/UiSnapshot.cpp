// Renders UI components offscreen to PNG for visual review without touching
// the desktop: `SonoraUiSnapshot <output-dir>`.
#include "AiSidebar.h"
#include "ArrangementView.h"
#include "NeonTheme.h"
#include "DrumSequencer.h"
#include "PianoRoll.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace
{
sonora::ArrangementState sampleState(int sections, double playTick)
{
    using namespace sonora;
    ProjectState project = defaultProject();
    project.tracks[0].melodies[0].count = 12;
    project.tracks[0].melodies[1].count = 20;
    project.tracks[1].drumPatterns[0].steps[0][0] = 100;
    project.tracks[1].drumPatterns[1].steps[0][0] = 100;
    project.tracks[2].id = 3;
    project.tracks[2].kind = TrackKind::Synth;
    project.tracks[2].setTrackName("Fingered Bass");
    project.tracks[2].melodies[0].count = 16;
    project.tracks[3].id = 4;
    project.tracks[3].kind = TrackKind::Synth;
    project.tracks[3].setTrackName("Warm Pad");
    project.tracks[3].melodies[0].count = 4;
    project.tracks[3].melodies[2].count = 6;
    ArrangementState state;
    state.song = buildSongFromTemplate(project, SongTemplate::Pop);
    while (state.song.sections < sections)
        state.song.duplicateSection(state.song.sections - 1);
    state.song.trackOn[4][3] = false;
    const juce::Colour colours[] { ui::cyan, ui::violet, ui::blue, ui::warn };
    for (int t = 0; t < maxTracks; ++t)
    {
        const auto i = static_cast<std::size_t>(t);
        state.used[i] = project.tracks[i].kind != TrackKind::None;
        state.drums[i] = project.tracks[i].kind == TrackKind::Drums;
        state.names[i] = project.tracks[i].trackName();
        state.colours[i] = colours[t % 4];
        for (int slot = 0; slot < numPatterns; ++slot)
            state.contentCount[i][static_cast<std::size_t>(slot)] = state.drums[i]
                ? project.tracks[i].drumPatterns[static_cast<std::size_t>(slot)].hitCount()
                : project.tracks[i].melodies[static_cast<std::size_t>(slot)].count;
    }
    state.selected = 1;
    state.playTick = playTick;
    return state;
}

bool render(juce::Component& component, int width, int height, const juce::File& file)
{
    component.setSize(width, height);
    juce::Image image(juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g(image);
        g.fillAll(sonora::ui::panel);
        component.paintEntireComponent(g, true);
    }
    juce::PNGImageFormat png;
    file.deleteFile();
    juce::FileOutputStream stream(file);
    return stream.openedOk() && png.writeImageToStream(image, stream);
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    sonora::ui::NeonTheme theme;
    juce::LookAndFeel::setDefaultLookAndFeel(&theme);
    const juce::File dir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    sonora::ArrangementView view;
    bool ok = true;
    view.setState(sampleState(10, 3.5 * sonora::patternTicks));
    ok = render(view, 1600, 330, dir.getChildFile("arrangement-wide.png")) && ok;
    view.setState(sampleState(sonora::maxSections, -1.0));
    ok = render(view, 830, 330, dir.getChildFile("arrangement-narrow.png")) && ok;
    {
        sonora::AiSidebar sidebar;
        sidebar.setSize(380, 820);
        sidebar.setContext("Starter Drums", "Chorus  /  part 4, bars 13-16  /  loop B", sonora::AiSidebar::Mode::Drums, true);
        using R = sonora::AiSidebar::Message::Role;
        sidebar.addMessage({ R::User, "Write a punchy beat for the chorus that locks with the bass." });
        sidebar.addMessage({ R::Assistant, "Here's a driving four-on-the-floor with claps on 2 and 4, and 16th hats "
                                           "that open up on the last beat of each bar to push into the next." });
        sidebar.addMessage({ R::Info, "Applied to loop B: 38 hits. Undo (Ctrl+Z) restores the old beat." });
        sidebar.addMessage({ R::User, "Nice. Can you add a snare fill in bar 4?" });
        sidebar.setBusy(true);
        sidebar.setStatus("Thinking...  8 s");
        ok = render(sidebar, 380, 820, dir.getChildFile("ai-sidebar.png")) && ok;
        sonora::AiSidebar empty;
        empty.setSize(380, 820);
        empty.setContext("Whole song", "10 parts  /  40 bars  /  4 tracks", sonora::AiSidebar::Mode::Song, true);
        ok = render(empty, 380, 820, dir.getChildFile("ai-sidebar-empty.png")) && ok;
    }
    {
        // Piano roll at 1.25x (Omarchy monitor scale): notes spread over four
        // octaves, C2 held live, scrollbar visible.
        sonora::ui::setScale(1.25f);
        sonora::PianoRoll roll;
        sonora::Pattern pattern;
        pattern.count = 5;
        pattern.notes[0] = { 1, 0, 960, 36, 100 };
        pattern.notes[1] = { 2, 1920, 480, 60, 110 };
        pattern.notes[2] = { 3, 3840, 240, 72, 90 };
        pattern.notes[3] = { 4, 5760, 1920, 84, 100 };
        pattern.notes[4] = { 5, 9600, 480, 24, 80 };
        roll.setPattern(pattern);
        roll.setViewBase(48);
        roll.setLiveNotes({ 36 });
        roll.setPlayhead(2000.0, true);
        ok = render(roll, 900, 420, dir.getChildFile("piano-roll.png")) && ok;
        sonora::ui::setScale(1.0f);
    }
    {
        // Daylight Paper mode: same boards under the light palette.
        sonora::ui::applyPalette(sonora::omarchy::sonoraLightPalette());
        theme.applyPalette();
        view.setState(sampleState(10, 3.5 * sonora::patternTicks));
        ok = render(view, 1600, 330, dir.getChildFile("arrangement-light.png")) && ok;
        sonora::PianoRoll lightRoll;
        sonora::Pattern lightPattern;
        lightPattern.count = 3;
        lightPattern.notes[0] = { 1, 0, 960, 60, 100 };
        lightPattern.notes[1] = { 2, 1920, 480, 64, 110 };
        lightPattern.notes[2] = { 3, 3840, 240, 67, 90 };
        lightRoll.setPattern(lightPattern);
        lightRoll.setViewBase(48);
        lightRoll.setLiveNotes({ 60 });
        lightRoll.setPlayhead(2000.0, true);
        ok = render(lightRoll, 900, 420, dir.getChildFile("piano-roll-light.png")) && ok;
        sonora::DrumSequencer lightDrums;
        sonora::DrumPattern lightGrid;
        lightGrid.steps[0][0] = 110;
        lightGrid.steps[0][16] = 110;
        lightGrid.steps[1][16] = 100;
        lightGrid.steps[2][4] = 90;
        lightDrums.setPattern(lightGrid);
        lightDrums.setPlayhead(16 * sonora::stepTicks, true);
        ok = render(lightDrums, 900, 300, dir.getChildFile("drums-light.png")) && ok;
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    std::cout << (ok ? "snapshots written to " : "snapshot failed: ") << dir.getFullPathName() << "\n";
    return ok ? 0 : 1;
}
