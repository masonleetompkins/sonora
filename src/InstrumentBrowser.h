#pragma once
#include "InstrumentSearch.h"
#include "NeonTheme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
// Searchable instrument picker with favorites, shown in a call-out under the
// instrument button. Pure view over InstrumentSearch.h: the owner supplies the
// entries and the favorites, and hears about picks and favorite changes.
class InstrumentBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    std::function<void(const PickerEntry&)> onPick;
    std::function<void(const Favorites&)> onFavoritesChanged;

    InstrumentBrowser(std::vector<PickerEntry> allEntries, Favorites favoriteSet, juce::String currentKey)
        : entries(std::move(allEntries)), favorites(std::move(favoriteSet)), current(std::move(currentKey))
    {
        addAndMakeVisible(search);
        search.setTextToShowWhenEmpty("Search sounds...", ui::muted);
        search.setFont(ui::font(14.0f));
        search.setColour(juce::TextEditor::backgroundColourId, ui::raised);
        search.setColour(juce::TextEditor::textColourId, ui::text);
        search.setColour(juce::TextEditor::outlineColourId, ui::border);
        search.setSelectAllWhenFocused(true);
        search.onTextChange = [this] { rebuild(true); };
        search.onReturnKey = [this] { pickSelected(); };
        search.onKey = [this](const juce::KeyPress& key) { return handleKey(key); };
        addAndMakeVisible(onlyFavorites);
        onlyFavorites.setButtonText("Favorites only");
        onlyFavorites.setWantsKeyboardFocus(false);
        onlyFavorites.setTooltip("Show just the sounds you have starred.");
        onlyFavorites.onClick = [this] { rebuild(true); };
        addAndMakeVisible(list);
        list.setModel(this);
        list.setRowHeight(rowHeight);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setWantsKeyboardFocus(false);
        setSize(380, 520);
        rebuild(false);
        // Open scrolled to the current choice so "what am I using" is visible.
        for (std::size_t row = 0; row < rows.size(); ++row)
            if (!rows[row].header && entries[static_cast<std::size_t>(rows[row].entry)].key == current)
            {
                list.scrollToEnsureRowIsOnscreen(static_cast<int>(row));
                break;
            }
    }

    void focusSearch() { search.grabKeyboardFocus(); }
    int visibleRowCount() const { return static_cast<int>(rows.size()); }
    const Favorites& currentFavorites() const { return favorites; }

    void paint(juce::Graphics& g) override
    {
        ui::surface(g, getLocalBounds().toFloat(), 12.0f);
        if (rows.empty())
        {
            g.setColour(ui::muted);
            g.setFont(ui::font(12.0f));
            const bool noFavorites = onlyFavorites.getToggleState() && favorites.size() == 0;
            g.drawFittedText(noFavorites ? "No favorites yet. Click the star next to a sound to keep it here."
                                         : "No matches. Try fewer words.",
                             list.getBounds().reduced(20), juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 12);
        auto top = area.removeFromTop(32);
        onlyFavorites.setBounds(top.removeFromRight(126));
        search.setBounds(top.reduced(0, 1));
        area.removeFromTop(8);
        list.setBounds(area);
    }

private:
    static constexpr int rowHeight = 28;

    struct SearchBox final : public juce::TextEditor
    {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (onKey && onKey(key))
                return true;
            return juce::TextEditor::keyPressed(key);
        }
    };

    void rebuild(bool resetSelection)
    {
        rows = pickerRows(entries, search.getText(), favorites, onlyFavorites.getToggleState());
        list.updateContent();
        if (resetSelection)
        {
            // Land on the best match so Enter does the obvious thing.
            int first = -1;
            for (std::size_t row = 0; row < rows.size() && first < 0; ++row)
                if (!rows[row].header)
                    first = static_cast<int>(row);
            list.selectRow(first, true, true);
            list.scrollToEnsureRowIsOnscreen(std::max(0, first));
        }
        repaint();
    }

    bool handleKey(const juce::KeyPress& key)
    {
        const int step = key == juce::KeyPress::downKey ? 1 : key == juce::KeyPress::upKey ? -1 : 0;
        if (step == 0)
            return false;
        int row = list.getSelectedRow();
        const int count = static_cast<int>(rows.size());
        for (int guard = 0; guard < count; ++guard)
        {
            row += step;
            if (row < 0 || row >= count)
                return true; // end of the list: stay put
            if (!rows[static_cast<std::size_t>(row)].header)
            {
                list.selectRow(row, true, true);
                list.scrollToEnsureRowIsOnscreen(row);
                return true;
            }
        }
        return true;
    }

    void pickRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(rows.size()) || rows[static_cast<std::size_t>(row)].header)
            return;
        const auto& entry = entries[static_cast<std::size_t>(rows[static_cast<std::size_t>(row)].entry)];
        if (onPick)
            onPick(entry);
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }
    void pickSelected() { pickRow(list.getSelectedRow()); }

    // ---- ListBoxModel ----
    int getNumRows() override { return static_cast<int>(rows.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= static_cast<int>(rows.size()))
            return;
        const auto& item = rows[static_cast<std::size_t>(row)];
        if (item.header)
        {
            ui::caption(g, item.title, { 8, height - 18, width - 16, 16 }, ui::cyan, 9.0f);
            return;
        }
        const auto& entry = entries[static_cast<std::size_t>(item.entry)];
        const bool isCurrent = entry.key == current;
        if (selected)
        {
            g.setColour(ui::cyan.withAlpha(0.18f));
            g.fillRoundedRectangle(2.0f, 1.0f, static_cast<float>(width) - 4.0f, static_cast<float>(height) - 2.0f, 6.0f);
        }
        if (isCurrent)
        {
            g.setColour(ui::cyan);
            g.fillEllipse(8.0f, static_cast<float>(height) * 0.5f - 3.0f, 6.0f, 6.0f);
        }
        g.setColour(ui::text);
        g.setFont(ui::font(13.0f, isCurrent));
        g.drawText(entry.name, 22, 0, width - 22 - 130, height, juce::Justification::centredLeft, true);
        // Searching flattens the groups, so say where each result lives.
        if (search.getText().trim().isNotEmpty() || entry.kind != PickerEntry::Kind::Instrument)
        {
            g.setColour(ui::muted);
            g.setFont(ui::font(10.5f));
            g.drawText(entry.family, width - 36 - 110, 0, 110, height, juce::Justification::centredRight, true);
        }
        paintStar(g, starBounds(width, height), favorites.has(entry.key));
    }

    static juce::Rectangle<float> starBounds(int width, int height)
    {
        return { static_cast<float>(width) - 28.0f, static_cast<float>(height) * 0.5f - 8.0f, 16.0f, 16.0f };
    }

    static void paintStar(juce::Graphics& g, juce::Rectangle<float> area, bool filled)
    {
        juce::Path star;
        star.addStar(area.getCentre(), 5, area.getWidth() * 0.22f, area.getWidth() * 0.5f, -juce::MathConstants<float>::halfPi);
        if (filled)
        {
            g.setColour(ui::warn);
            g.fillPath(star);
        }
        else
        {
            g.setColour(ui::muted.withAlpha(0.7f));
            g.strokePath(star, juce::PathStrokeType(1.2f));
        }
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (row < 0 || row >= static_cast<int>(rows.size()) || rows[static_cast<std::size_t>(row)].header)
            return;
        const auto& entry = entries[static_cast<std::size_t>(rows[static_cast<std::size_t>(row)].entry)];
        const auto width = list.getRowPosition(row, true).getWidth();
        if (starBounds(width, rowHeight).expanded(6.0f).contains(event.position))
        {
            favorites.toggle(entry.key);
            if (onFavoritesChanged)
                onFavoritesChanged(favorites);
            // Keep the viewport where it is: only the starred rows can move.
            const auto keep = list.getVerticalPosition();
            rebuild(false);
            list.setVerticalPosition(keep);
            return;
        }
        pickRow(row);
    }

    void returnKeyPressed(int row) override { pickRow(row); }

    std::vector<PickerEntry> entries;
    Favorites favorites;
    juce::String current;
    std::vector<PickerRow> rows;
    SearchBox search;
    juce::ToggleButton onlyFavorites;
    juce::ListBox list;
};
}
