#pragma once
#include "Pattern.h"
#include <algorithm>
#include <functional>
#include <set>
#include <vector>
#include <juce_core/juce_core.h>

namespace sonora
{
// The instrument picker's data layer, free of any GUI so it can be tested:
// what can be picked, how a search ranks it, which rows the list shows, and
// which entries are favorites.

// One pickable sound: a sound-bank instrument, the editable synth or the
// sampler (by table index), a synth patch, or a sound in the sample library.
struct PickerEntry
{
    enum class Kind { Instrument, Patch, Sample };
    Kind kind = Kind::Instrument;
    int index = 0;        // instruments[] or synthPatches() index
    juce::String name;    // shown in the list
    juce::String family;  // group heading, also searchable
    juce::String key;     // stable identity for favorites; survives table growth
    juce::String sample;  // library-relative file name (Kind::Sample)
};

inline juce::String instrumentDisplayName(int index)
{
    if (index == 0)
        return "Sonora Synth (editable)";
    if (isSamplerInstrument(index))
        return "Sampler";
    return validInstrument(index) ? juce::String(instruments[static_cast<std::size_t>(index)].name) : juce::String();
}

// "Drums/Kick 01.wav" -> "Kick 01". Plain string work: a juce::File must not be
// built from a relative path.
inline juce::String sampleDisplayName(const juce::String& name)
{
    return name.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false);
}

inline juce::String instrumentKey(int index) { return "instrument:" + instrumentDisplayName(index); }
inline juce::String patchKey(const juce::String& name) { return "patch:" + name; }
inline juce::String sampleKey(const juce::String& name) { return "sample:" + name; }

// Names Sonora generated itself (instrument or synth-patch names). Tracks with
// these names follow instrument/patch changes; custom names are never touched.
inline bool isGeneratedTrackName(const juce::String& name)
{
    for (const auto& preset : instruments)
        if (name == preset.name)
            return true;
    for (const auto& patch : synthPatches())
        if (name == patch.name)
            return true;
    return false;
}

// Everything pickable, in a stable order: instruments by table order, then
// synth patches, then the library. Families keep first-appearance order.
inline std::vector<PickerEntry> buildPickerEntries(const std::vector<juce::String>& library)
{
    std::vector<PickerEntry> entries;
    for (std::size_t i = 0; i < instruments.size(); ++i)
    {
        PickerEntry entry;
        entry.kind = PickerEntry::Kind::Instrument;
        entry.index = static_cast<int>(i);
        entry.name = instrumentDisplayName(entry.index);
        entry.family = instruments[i].family;
        entry.key = instrumentKey(entry.index);
        entries.push_back(entry);
    }
    const auto& patches = synthPatches();
    for (std::size_t i = 0; i < patches.size(); ++i)
    {
        PickerEntry entry;
        entry.kind = PickerEntry::Kind::Patch;
        entry.index = static_cast<int>(i);
        entry.name = patches[i].name;
        entry.family = "Synth patches";
        entry.key = patchKey(entry.name);
        entries.push_back(entry);
    }
    for (const auto& name : library)
    {
        PickerEntry entry;
        entry.kind = PickerEntry::Kind::Sample;
        entry.name = name;
        entry.family = "My samples";
        entry.key = sampleKey(name);
        entry.sample = name;
        entries.push_back(entry);
    }
    return entries;
}

// What a track plays, as the picker's identity: lets the list mark the
// current choice and the button show something meaningful.
inline juce::String currentPickerKey(const Track& track)
{
    if (isSamplerInstrument(track.instrumentPreset))
        return track.samplerFile[0] != '\0' ? sampleKey(track.samplerFileName()) : instrumentKey(samplerInstrument);
    if (track.instrumentPreset == 0)
    {
        for (const auto& patch : synthPatches())
            if (patch.params == track.synth)
                return patchKey(patch.name);
        return instrumentKey(0);
    }
    return instrumentKey(track.instrumentPreset);
}

inline juce::String describeTrackInstrument(const Track& track)
{
    if (isSamplerInstrument(track.instrumentPreset))
        return track.samplerFile[0] != '\0'
            ? "Sampler: " + sampleDisplayName(track.samplerFileName())
            : juce::String("Sampler (no sound yet)");
    if (track.instrumentPreset == 0)
    {
        for (const auto& patch : synthPatches())
            if (patch.params == track.synth)
                return juce::String("Synth: ") + patch.name;
        return "Sonora Synth (custom)";
    }
    return instrumentDisplayName(track.instrumentPreset);
}

// ---- Favorites -------------------------------------------------------------
class Favorites
{
public:
    static constexpr int maxEntries = 500;
    static constexpr int maxKeyLength = 300;

    bool has(const juce::String& key) const { return keys_.count(key) != 0; }
    int size() const { return static_cast<int>(keys_.size()); }
    // Returns whether the key is a favorite afterwards.
    bool toggle(const juce::String& key)
    {
        if (key.isEmpty() || key.length() > maxKeyLength)
            return false;
        if (keys_.erase(key) != 0)
            return false;
        if (static_cast<int>(keys_.size()) >= maxEntries)
            return false;
        keys_.insert(key);
        return true;
    }
    void set(const juce::String& key, bool favorite)
    {
        if (has(key) != favorite)
            toggle(key);
    }
    std::vector<juce::String> keys() const { return { keys_.begin(), keys_.end() }; }
    bool operator==(const Favorites& other) const { return keys_ == other.keys_; }

    // Stored under the user's config folder. A missing file is just "no
    // favorites"; a damaged one is reported and ignored, never half-applied.
    juce::Result load(const juce::File& file)
    {
        if (!file.existsAsFile())
            return juce::Result::ok();
        const auto parsed = juce::JSON::parse(file);
        const auto* root = parsed.getDynamicObject();
        const auto* list = root != nullptr ? root->getProperty("favorites").getArray() : nullptr;
        if (list == nullptr)
            return juce::Result::fail("Favorites file is not valid.");
        std::set<juce::String> loaded;
        for (const auto& item : *list)
        {
            if (!item.isString())
                continue;
            const auto key = item.toString();
            if (key.isNotEmpty() && key.length() <= maxKeyLength && static_cast<int>(loaded.size()) < maxEntries)
                loaded.insert(key);
        }
        keys_ = std::move(loaded);
        return juce::Result::ok();
    }

    juce::Result save(const juce::File& file) const
    {
        juce::Array<juce::var> list;
        for (const auto& key : keys_)
            list.add(key);
        auto* root = new juce::DynamicObject();
        root->setProperty("version", 1);
        root->setProperty("favorites", list);
        if (!file.getParentDirectory().createDirectory().wasOk())
            return juce::Result::fail("Could not create the settings folder.");
        // replaceWithText writes through a temporary file, so a crash never
        // leaves a half-written favorites list.
        return file.replaceWithText(juce::JSON::toString(juce::var(root))) ? juce::Result::ok()
                                                                           : juce::Result::fail("Could not save favorites.");
    }

private:
    std::set<juce::String> keys_;
};

// ---- Search and layout ------------------------------------------------------
// Every whitespace-separated word must match. Lower is better: a name that
// starts with the word, then a word inside the name, then anywhere in the
// name, then the family heading. -1 means no match.
inline int pickerMatchScore(const PickerEntry& entry, const juce::StringArray& words)
{
    const auto name = entry.name.toLowerCase();
    const auto family = entry.family.toLowerCase();
    int score = 0;
    for (const auto& word : words)
    {
        if (name.startsWith(word))
            score += 0;
        else if ((" " + name).contains(" " + word))
            score += 1;
        else if (name.contains(word))
            score += 2;
        else if (family.contains(word))
            score += 3;
        else
            return -1;
    }
    return score;
}

struct PickerRow
{
    bool header = false;
    juce::String title;  // header text
    int entry = -1;      // index into the entries vector (-1 for headers)
};

inline std::vector<PickerRow> pickerRows(const std::vector<PickerEntry>& entries, const juce::String& query,
                                         const Favorites& favorites, bool favoritesOnly)
{
    const auto words = juce::StringArray::fromTokens(query.trim().toLowerCase(), " \t", "");
    const bool searching = !words.isEmpty();
    auto ranked = [&](const std::function<bool(const PickerEntry&)>& include) {
        std::vector<std::pair<int, int>> scored; // score, entry index
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (!include(entries[i]))
                continue;
            const int score = searching ? pickerMatchScore(entries[i], words) : 0;
            if (score >= 0)
                scored.emplace_back(score, static_cast<int>(i));
        }
        if (searching)
            std::stable_sort(scored.begin(), scored.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
        return scored;
    };
    std::vector<PickerRow> rows;
    const auto favoriteHits = ranked([&](const PickerEntry& e) { return favorites.has(e.key); });
    if (!favoriteHits.empty())
    {
        rows.push_back({ true, "Favorites", -1 });
        for (const auto& hit : favoriteHits)
            rows.push_back({ false, {}, hit.second });
    }
    if (favoritesOnly)
        return rows;
    if (searching)
    {
        const auto hits = ranked([&](const PickerEntry& e) { return !favorites.has(e.key); });
        if (!hits.empty())
        {
            rows.push_back({ true, "Results", -1 });
            for (const auto& hit : hits)
                rows.push_back({ false, {}, hit.second });
        }
        return rows;
    }
    std::vector<juce::String> families;
    for (const auto& entry : entries)
        if (std::find(families.begin(), families.end(), entry.family) == families.end())
            families.push_back(entry.family);
    for (const auto& family : families)
    {
        rows.push_back({ true, family, -1 });
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].family == family)
                rows.push_back({ false, {}, static_cast<int>(i) });
    }
    return rows;
}
}
