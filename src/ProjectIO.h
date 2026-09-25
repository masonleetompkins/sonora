#pragma once
#include "Pattern.h"
#include <juce_core/juce_core.h>

namespace sonora
{
class ProjectIO
{
public:
    static juce::String encode(const ProjectState& state);
    static juce::Result decode(const juce::String& json, ProjectState& destination);
    static juce::Result save(const juce::File& file, const ProjectState& state);
    static juce::Result load(const juce::File& file, ProjectState& destination);
};
}
