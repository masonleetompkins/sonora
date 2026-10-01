#pragma once
#include "AgentActions.h"
#include "AiMelody.h"

namespace sonora::ai
{
// The in-app agent: one conversation that can do anything the app can do.
// Claude (the user's own CLI login, sandboxed exactly like the melody assistant)
// gets the whole project as text and answers with a reply plus an ordered list
// of actions; agent::applyActions turns those into one undoable edit.
inline constexpr int agentTimeoutMs = 300000;

struct AgentRequest
{
    juce::String message;
    ProjectState project;
    int selectedTrack = 0;
    std::array<int, maxTracks> loopSlot {};  // the loop each track is showing
    int editPart = -1;                       // song part being edited in the loop view, or -1
    bool songView = false, playing = false;
    std::vector<juce::String> library;       // sample library names
    std::vector<ChatTurn> history;           // earlier turns, oldest first
};

struct AgentResult
{
    juce::String reply, error;
    std::vector<agent::Action> actions;
    juce::StringArray unreadable; // actions that could not be parsed
    bool ok() const { return error.isEmpty(); }
};

juce::String agentSystemPrompt();
juce::String agentSchema();
juce::String buildAgentMessage(const AgentRequest& request);
juce::StringArray agentArguments();
AgentResult parseAgentResponse(const juce::String& cliStdout);
AgentResult runAgent(const AgentRequest& request, const std::atomic<bool>* cancel, int timeoutMs = agentTimeoutMs);
}
