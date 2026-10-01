#pragma once
#include "Pattern.h"
#include <atomic>
#include <juce_core/juce_core.h>

namespace sonora::ai::detail
{
// Helpers shared by the melody assistant, the song composer, and the agent:
// untrusted-text cleaning, the compact loop describers that feed the model,
// and the sandboxed `claude` runner. Defined in AiMelody.cpp.
juce::String cleanText(const juce::String& text, int maxChars);
juce::String instrumentDescription(const Track& track);
juce::String describeNotes(const Pattern& pattern);
juce::String describeDrums(const DrumPattern& drums);
juce::String drumRulesText();
juce::String melodyRulesText();
int toClampedInt(const juce::var& v);
juce::StringArray sandboxArguments(const juce::String& system, const juce::String& schema);
juce::String runSandboxedClaude(const juce::StringArray& arguments, const juce::String& input,
                                const std::atomic<bool>* cancel, int timeoutMs, juce::String& error);
// The structured answer object from `claude -p --output-format json`, or a
// void var with `error` set.
juce::var extractAnswer(const juce::String& cliStdout, juce::String& error);
}
