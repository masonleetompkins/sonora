#pragma once
#include "Pattern.h"
#include <atomic>
#include <juce_core/juce_core.h>

namespace sonora::ai
{
// AI melody generation through the locally installed Claude Code CLI, using
// the user's existing Claude subscription login. Sonora never reads, stores,
// or transmits credentials itself: it runs `claude` as a sandboxed child
// process (no shell, no tools, no MCP, no settings/hooks, empty working
// directory, API-key variables stripped) and treats its output as untrusted
// data that is parsed, clamped, and validated before becoming notes.

inline constexpr int maxPromptChars = 2000;
inline constexpr int maxContextChars = 60000;
inline constexpr int defaultTimeoutMs = 180000;

// Absolute path to an executable `claude`, or an empty File. Honors
// SONORA_CLAUDE_PATH (absolute, executable) first, then absolute PATH entries,
// then the usual per-user install locations (desktop launchers often start
// apps without the shell's PATH).
juce::File findClaudeExecutable();

// Environment for the child: the parent's, minus API-key variables so the
// CLI always uses the subscription login and never bills or leaks a key.
juce::StringArray sanitizedEnvironment(const juce::StringArray& parentEnvironment);

struct ProcessResult
{
    int exitCode = -1;
    juce::String output, errorOutput, failure;
    bool timedOut = false, cancelled = false;
    bool ok() const { return failure.isEmpty() && !timedOut && !cancelled && exitCode == 0; }
};

// posix_spawn with an argv array (never a shell), stdin fed from `input`,
// stdout/stderr captured (capped), run in `workDir`, killed as a process group
// on timeout or cancel.
ProcessResult runProcess(const juce::File& executable, const juce::StringArray& arguments,
                         const juce::String& input, const juce::File& workDir,
                         const juce::StringArray& environment, int timeoutMs,
                         const std::atomic<bool>* cancel);

// What the melody should fit: the whole project plus the pattern slot each
// track is currently previewing, and the track/slot being written.
struct MelodyRequest
{
    juce::String prompt;
    ProjectState project;
    int track = 0;
    std::array<int, maxTracks> melodySlots {}, drumSlots {};
};

juce::String systemPrompt();
juce::String responseSchema();
juce::String buildUserMessage(const MelodyRequest& request);
juce::StringArray claudeArguments();

struct MelodyResult
{
    Pattern pattern;
    juce::String title, explanation, error;
    bool ok() const { return error.isEmpty(); }
};

// Parses `claude -p --output-format json` stdout into a valid Pattern:
// pitches folded by octaves into range, times clamped to the loop, same-pitch
// overlaps trimmed, capacity capped, ids reassigned.
MelodyResult parseMelodyResponse(const juce::String& cliStdout);

// End to end: locate the CLI, run it sandboxed, parse the answer.
MelodyResult generateMelody(const MelodyRequest& request, const std::atomic<bool>* cancel,
                            int timeoutMs = defaultTimeoutMs);

// ---- Sidebar assistant --------------------------------------------------
// A conversation about one track: Claude replies in words and may return a
// complete replacement pattern (notes for instruments, hits for drums). The
// target's current pattern is always sent, so "edit what's there" works; the
// song part being worked on (if any) decides which loops the other tracks
// play and which are silent.
inline constexpr int maxHistoryTurns = 12;
inline constexpr int maxTurnChars = 800;

struct ChatTurn
{
    bool fromUser = true;
    juce::String text;
};

struct AssistantRequest
{
    juce::String message;
    ProjectState project;
    int track = 0;
    int part = -1; // song part being worked on, or -1 for a free loop
    std::array<int, maxTracks> melodySlots {}, drumSlots {};
    std::vector<ChatTurn> history; // earlier turns, oldest first
};

// Loop slot the target track uses: the part's slot when working on a part.
int assistantTargetSlot(const AssistantRequest& request);

struct AssistantResult
{
    juce::String reply, error;
    bool changed = false, drums = false;
    Pattern pattern;
    DrumPattern drumPattern;
    int count() const { return drums ? drumPattern.hitCount() : pattern.count; }
    bool ok() const { return error.isEmpty(); }
};

juce::String assistantSystemPrompt(bool drums);
juce::String assistantSchema(bool drums);
juce::String buildAssistantMessage(const AssistantRequest& request);
juce::StringArray assistantArguments(bool drums);
AssistantResult parseAssistantResponse(const juce::String& cliStdout, bool drums);
AssistantResult runAssistant(const AssistantRequest& request, const std::atomic<bool>* cancel,
                             int timeoutMs = defaultTimeoutMs);

// ---- Song composer ------------------------------------------------------
// Arranges the whole song from every track's loops and may write new
// variation loops, but only into slots that are empty (existing loops are
// never rewritten). The result replaces the arrangement as one edit.
inline constexpr int songTimeoutMs = 300000;

struct SongRequest
{
    juce::String message;
    ProjectState project;
    std::vector<ChatTurn> history;
};

struct SongLoopWrite
{
    int track = 0, slot = 0;
    bool drums = false;
    Pattern pattern;
    DrumPattern drumPattern;
};

struct SongResult
{
    juce::String reply, error;
    bool changed = false;
    Arrangement song;
    std::vector<SongLoopWrite> writes;
    juce::StringArray skipped; // requested writes refused (slot not empty)
    bool ok() const { return error.isEmpty(); }
};

juce::String songSystemPrompt();
juce::String songSchema();
juce::String buildSongMessage(const SongRequest& request);
juce::StringArray songArguments();
SongResult parseSongResponse(const juce::String& cliStdout, const ProjectState& project);
// Writes new loops (still-empty slots only) and replaces the arrangement.
bool applySongResult(ProjectState& project, const SongResult& result);
SongResult runSongComposer(const SongRequest& request, const std::atomic<bool>* cancel,
                           int timeoutMs = songTimeoutMs);
}
