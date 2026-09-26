#include "ArrangementView.h"

namespace sonora
{
namespace
{
juce::String slotLetter(int slot) { return juce::String::charToString(static_cast<juce::juce_wchar>('A' + slot)); }
constexpr float footerHeight = 26.0f;
}

juce::Colour ArrangementView::partColour(SongPart part)
{
    switch (part)
    {
        case SongPart::Intro: case SongPart::Outro: return juce::Colour(0xff8b9db7);
        case SongPart::Verse: return ui::blue;
        case SongPart::PreChorus: return ui::cyan;
        case SongPart::Chorus: return ui::violet;
        case SongPart::Bridge: return ui::warn;
        case SongPart::Break: return juce::Colour(0xff6f86a8);
        case SongPart::Build: return juce::Colour(0xffffa24d);
        case SongPart::Drop: return ui::danger;
        case SongPart::Section: case SongPart::numParts: break;
    }
    return ui::muted;
}

void ArrangementView::setState(const ArrangementState& next)
{
    state = next;
    repaint();
}

std::vector<int> ArrangementView::rows() const
{
    std::vector<int> out;
    for (int t = 0; t < maxTracks; ++t)
        if (state.used[static_cast<std::size_t>(t)])
            out.push_back(t);
    return out;
}

ArrangementView::Layout ArrangementView::layout() const
{
    Layout l;
    l.headerTop = 6.0f;
    l.gridTop = l.headerTop + l.headerHeight + 6.0f;
    const auto count = std::max<std::size_t>(1, rows().size());
    l.rowHeight = juce::jlimit(30.0f, 48.0f, (static_cast<float>(getHeight()) - l.gridTop - footerHeight - 6.0f)
                                                 / static_cast<float>(count));
    l.gridLeft = l.labelWidth + 10.0f;
    const float available = static_cast<float>(getWidth()) - l.gridLeft - l.addWidth - 12.0f;
    l.columnWidth = juce::jlimit(34.0f, 150.0f, available / static_cast<float>(std::max(1, state.song.sections)));
    return l;
}

juce::Rectangle<float> ArrangementView::headerBounds(int section) const
{
    const auto l = layout();
    return { l.gridLeft + l.columnWidth * static_cast<float>(section), l.headerTop, l.columnWidth - 4.0f, l.headerHeight };
}

juce::Rectangle<float> ArrangementView::cellBounds(int section, int row) const
{
    const auto l = layout();
    return { l.gridLeft + l.columnWidth * static_cast<float>(section), l.gridTop + l.rowHeight * static_cast<float>(row),
             l.columnWidth - 4.0f, l.rowHeight - 5.0f };
}

juce::Rectangle<float> ArrangementView::chipBounds(int row, int slot) const
{
    const auto l = layout();
    const float size = std::min(26.0f, l.rowHeight - 12.0f);
    return { l.labelWidth - (4.0f - static_cast<float>(slot)) * (size + 4.0f),
             l.gridTop + l.rowHeight * static_cast<float>(row) + (l.rowHeight - 5.0f - size) * 0.5f, size, size };
}

ArrangementView::Hit ArrangementView::locate(juce::Point<float> p) const
{
    const auto l = layout();
    const auto rowList = rows();
    Hit hit;
    if (p.y >= l.headerTop && p.y < l.headerTop + l.headerHeight)
    {
        const int section = static_cast<int>(std::floor((p.x - l.gridLeft) / l.columnWidth));
        if (p.x >= l.gridLeft && section >= 0 && section < state.song.sections)
            return { Hit::Area::Header, section, -1, -1 };
        const float addLeft = l.gridLeft + l.columnWidth * static_cast<float>(state.song.sections);
        if (p.x >= addLeft && p.x < addLeft + l.addWidth)
            return { Hit::Area::Add, -1, -1, -1 };
        return hit;
    }
    const int row = static_cast<int>(std::floor((p.y - l.gridTop) / l.rowHeight));
    if (p.y < l.gridTop || row < 0 || row >= static_cast<int>(rowList.size()))
        return hit;
    for (int slot = 0; slot < numPatterns; ++slot)
        if (chipBounds(row, slot).expanded(2.0f).contains(p))
            return { Hit::Area::Chip, -1, row, slot };
    const int section = static_cast<int>(std::floor((p.x - l.gridLeft) / l.columnWidth));
    if (p.x >= l.gridLeft && section >= 0 && section < state.song.sections)
        return { Hit::Area::Cell, section, row, -1 };
    return hit;
}

void ArrangementView::emit(ArrangementAction action)
{
    if (onAction)
        onAction(action);
}

void ArrangementView::paint(juce::Graphics& g)
{
    const auto l = layout();
    const auto rowList = rows();
    const auto& song = state.song;
    const int playing = state.playTick >= 0.0 ? static_cast<int>(state.playTick) / patternTicks : -1;

    // Part headers.
    for (int s = 0; s < song.sections; ++s)
    {
        const auto area = headerBounds(s);
        const auto part = song.parts[static_cast<std::size_t>(s)];
        const auto colour = partColour(part);
        const bool isPlaying = s == playing;
        g.setColour(colour.withAlpha(isPlaying ? 0.42f : 0.2f));
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRoundedRectangle(area.getX(), area.getY(), area.getWidth(), 3.0f, 1.5f);
        if (s == state.selected)
        {
            g.setColour(ui::text);
            g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.5f);
            juce::Path marker;
            marker.addTriangle(area.getX() + 6, area.getBottom() - 13, area.getX() + 6, area.getBottom() - 5,
                               area.getX() + 12, area.getBottom() - 9);
            g.fillPath(marker);
        }
        static constexpr const char* shortNames[] { "P", "INT", "VRS", "PRE", "CHR", "BRG", "BRK", "BLD", "DRP", "OUT" };
        const bool narrow = area.getWidth() < 64.0f;
        const juce::String name = part == SongPart::Section
            ? (narrow ? juce::String("P") : juce::String("PART ")) + juce::String(s + 1)
            : narrow ? juce::String(shortNames[static_cast<int>(part)]) : juce::String(songPartName(part)).toUpperCase();
        g.setColour(ui::text);
        g.setFont(ui::font(narrow ? 9.5f : 11.0f, true, 0.06f));
        g.drawFittedText(name, area.reduced(5.0f, 4.0f).withTrimmedBottom(16.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.6f);
        g.setColour(ui::muted);
        g.setFont(ui::font(9.0f));
        g.drawText(juce::String(s * 4 + 1) + (area.getWidth() > 50.0f ? "-" + juce::String(s * 4 + 4) : juce::String()),
                   area.reduced(5.0f, 4.0f).withTrimmedTop(26.0f).withTrimmedLeft(s == state.selected ? 10.0f : 0.0f)
                       .toNearestInt(),
                   juce::Justification::centredLeft);
    }
    // Add-part button.
    if (song.sections < maxSections)
    {
        const juce::Rectangle<float> add(l.gridLeft + l.columnWidth * static_cast<float>(song.sections), l.headerTop,
                                         l.addWidth - 4.0f, l.headerHeight);
        const bool hot = hover.area == Hit::Area::Add;
        g.setColour(hot ? ui::raised.brighter(0.15f) : ui::raised);
        g.fillRoundedRectangle(add, 6.0f);
        g.setColour(hot ? ui::cyan : ui::muted);
        g.drawRoundedRectangle(add.reduced(0.5f), 6.0f, 1.0f);
        g.setFont(ui::font(20.0f, true));
        g.drawText("+", add.toNearestInt(), juce::Justification::centred);
    }
    ui::caption(g, "TRACK  /  LOOPS (drag onto parts)", { 8, static_cast<int>(l.headerTop + 14), static_cast<int>(l.labelWidth), 18 },
                ui::muted, 9.0f);

    // Track rows.
    for (std::size_t row = 0; row < rowList.size(); ++row)
    {
        const int track = rowList[row];
        const auto t = static_cast<std::size_t>(track);
        const auto colour = state.colours[t];
        const float y = l.gridTop + l.rowHeight * static_cast<float>(row);
        g.setColour(colour);
        g.fillRoundedRectangle(8.0f, y + 6.0f, 3.0f, l.rowHeight - 17.0f, 1.5f);
        g.setColour(ui::text);
        g.setFont(ui::font(12.0f, true));
        const float chipsLeft = chipBounds(static_cast<int>(row), 0).getX();
        g.drawFittedText(state.names[t], juce::Rectangle<float>(18.0f, y, chipsLeft - 24.0f, l.rowHeight - 5.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.7f);
        for (int slot = 0; slot < numPatterns; ++slot)
        {
            const auto chip = chipBounds(static_cast<int>(row), slot);
            const bool content = state.contentCount[t][static_cast<std::size_t>(slot)] > 0;
            const bool hot = hover.area == Hit::Area::Chip && hover.row == static_cast<int>(row) && hover.slot == slot;
            g.setColour(content ? colour.withAlpha(hot ? 0.75f : 0.5f) : ui::raised);
            g.fillRoundedRectangle(chip, 5.0f);
            g.setColour(content ? colour : ui::border);
            g.drawRoundedRectangle(chip.reduced(0.5f), 5.0f, 1.0f);
            g.setColour(content ? ui::text : ui::muted);
            g.setFont(ui::font(11.0f, true));
            g.drawText(slotLetter(slot), chip.toNearestInt(), juce::Justification::centred);
        }
        for (int s = 0; s < song.sections; ++s)
        {
            const auto cell = cellBounds(s, static_cast<int>(row));
            const auto si = static_cast<std::size_t>(s);
            const bool on = song.trackOn[si][t];
            const int slot = song.slots[si][t];
            const bool content = state.contentCount[t][static_cast<std::size_t>(slot)] > 0;
            const bool hot = hover.area == Hit::Area::Cell && hover.section == s && hover.row == static_cast<int>(row);
            if (on)
            {
                g.setColour(colour.withAlpha(content ? (hot ? 0.7f : 0.52f) : 0.16f));
                g.fillRoundedRectangle(cell, 5.0f);
                g.setColour(content ? colour : colour.withAlpha(0.6f));
                g.drawRoundedRectangle(cell.reduced(0.5f), 5.0f, 1.0f);
                g.setColour(content ? ui::text : ui::muted);
                g.setFont(ui::font(12.0f, true));
                g.drawText(content ? slotLetter(slot) : slotLetter(slot) + (cell.getWidth() > 60.0f ? " (empty)" : ""),
                           cell.toNearestInt(), juce::Justification::centred);
            }
            else
            {
                g.setColour(hot ? ui::raised.brighter(0.1f) : ui::raised.withAlpha(0.35f));
                g.fillRoundedRectangle(cell, 5.0f);
                g.setColour(ui::border.withAlpha(0.6f));
                g.drawRoundedRectangle(cell.reduced(0.5f), 5.0f, 1.0f);
            }
        }
    }

    // Playhead and drag feedback.
    if (state.playTick >= 0.0 && !rowList.empty())
    {
        const float x = l.gridLeft + static_cast<float>(state.playTick / patternTicks) * l.columnWidth;
        g.setColour(ui::text.withAlpha(0.9f));
        g.fillRect(x - 1.0f, l.headerTop, 2.0f, l.gridTop + l.rowHeight * static_cast<float>(rowList.size()) - l.headerTop);
    }
    if (drag == Drag::Header && dragMoved && dropSection >= 0)
    {
        const auto target = headerBounds(dropSection);
        const float x = dropSection > dragSection ? target.getRight() + 2.0f : target.getX() - 2.0f;
        g.setColour(ui::cyan);
        g.fillRect(x - 1.5f, l.headerTop, 3.0f, l.headerHeight);
    }
    if (drag == Drag::Chip && dragMoved)
    {
        const auto t = static_cast<std::size_t>(dragTrack);
        const juce::Rectangle<float> chip(dragPoint.x - 14.0f, dragPoint.y - 14.0f, 28.0f, 28.0f);
        g.setColour(state.colours[t].withAlpha(0.85f));
        g.fillRoundedRectangle(chip, 6.0f);
        g.setColour(ui::text);
        g.setFont(ui::font(12.0f, true));
        g.drawText(slotLetter(dragSlot), chip.toNearestInt(), juce::Justification::centred);
    }

    g.setColour(ui::muted);
    g.setFont(ui::font(11.0f));
    g.drawText(rowList.empty() ? juce::String("Add an instrument track to start arranging.")
                               : juce::String("Drag a loop chip onto parts  |  Click a cell: on/off, drag across to paint  |  "
                                              "Scroll or right-click a cell: pick loop  |  Click a part: play from it, "
                                              "drag to reorder, double-click to edit it"),
               juce::Rectangle<float>(8.0f, static_cast<float>(getHeight()) - footerHeight, static_cast<float>(getWidth()) - 16.0f,
                                      footerHeight).toNearestInt(),
               juce::Justification::centredLeft);
}

void ArrangementView::mouseDown(const juce::MouseEvent& event)
{
    const auto hit = locate(event.position);
    const auto rowList = rows();
    dragMoved = false;
    drag = Drag::None;
    if (hit.area == Hit::Area::Add)
    {
        emit({ ArrangementAction::Kind::Add });
        return;
    }
    if (hit.area == Hit::Area::Header)
    {
        if (event.mods.isPopupMenu())
        {
            showHeaderMenu(hit.section);
            return;
        }
        emit({ ArrangementAction::Kind::SelectSection, hit.section });
        drag = Drag::Header;
        dragSection = dropSection = hit.section;
        return;
    }
    if (hit.area == Hit::Area::Chip)
    {
        drag = Drag::Chip;
        dragTrack = rowList[static_cast<std::size_t>(hit.row)];
        dragSlot = hit.slot;
        dragPoint = event.position;
        lastPainted = -1;
        return;
    }
    if (hit.area == Hit::Area::Cell)
    {
        const int track = rowList[static_cast<std::size_t>(hit.row)];
        if (event.mods.isPopupMenu())
        {
            showCellMenu(hit.section, track);
            return;
        }
        const auto si = static_cast<std::size_t>(hit.section);
        const auto t = static_cast<std::size_t>(track);
        paintOn = !state.song.trackOn[si][t];
        dragSlot = state.song.slots[si][t];
        dragTrack = track;
        drag = Drag::Paint;
        lastPainted = hit.section;
        if (onGestureBegin)
            onGestureBegin();
        emit({ ArrangementAction::Kind::SetCell, hit.section, track, dragSlot, paintOn });
    }
}

void ArrangementView::paintCell(const juce::MouseEvent& event)
{
    const auto hit = locate(event.position);
    const auto rowList = rows();
    if (hit.area != Hit::Area::Cell || hit.section == lastPainted
        || rowList[static_cast<std::size_t>(hit.row)] != dragTrack)
        return;
    lastPainted = hit.section;
    const bool on = drag == Drag::Chip ? true : paintOn;
    emit({ ArrangementAction::Kind::SetCell, hit.section, dragTrack, dragSlot, on });
}

void ArrangementView::mouseDrag(const juce::MouseEvent& event)
{
    if (event.getDistanceFromDragStart() > 4)
        dragMoved = true;
    if (drag == Drag::Paint)
        paintCell(event);
    else if (drag == Drag::Chip && dragMoved)
    {
        if (lastPainted < 0 && onGestureBegin)
        {
            onGestureBegin();
            lastPainted = -2; // gesture open, nothing painted yet
        }
        dragPoint = event.position;
        paintCell(event);
        repaint();
    }
    else if (drag == Drag::Header && dragMoved)
    {
        const auto l = layout();
        dropSection = juce::jlimit(0, state.song.sections - 1,
                                   static_cast<int>(std::floor((event.position.x - l.gridLeft) / l.columnWidth)));
        repaint();
    }
}

void ArrangementView::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (drag == Drag::Paint && onGestureEnd)
        onGestureEnd();
    else if (drag == Drag::Chip)
    {
        if (dragMoved && onGestureEnd)
            onGestureEnd();
        else if (!dragMoved) // a click on a loop chip opens it for editing
            emit({ ArrangementAction::Kind::EditLoop, -1, dragTrack, dragSlot });
    }
    else if (drag == Drag::Header && dragMoved && dropSection >= 0 && dropSection != dragSection)
        emit({ ArrangementAction::Kind::Move, dragSection, -1, dropSection });
    drag = Drag::None;
    dropSection = -1;
    repaint();
}

void ArrangementView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto hit = locate(event.position);
    if (hit.area == Hit::Area::Header)
        emit({ ArrangementAction::Kind::EditPart, hit.section });
    else if (hit.area == Hit::Area::Cell)
    {
        // The two clicks toggled the cell off and back on; now open its loop.
        const int track = rows()[static_cast<std::size_t>(hit.row)];
        emit({ ArrangementAction::Kind::EditLoop, hit.section, track,
               state.song.slots[static_cast<std::size_t>(hit.section)][static_cast<std::size_t>(track)] });
    }
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto hit = locate(event.position);
    if (hit.area != Hit::Area::Cell || std::abs(wheel.deltaY) < 1.0e-4f)
        return;
    const int track = rows()[static_cast<std::size_t>(hit.row)];
    const auto si = static_cast<std::size_t>(hit.section);
    const auto t = static_cast<std::size_t>(track);
    const int step = wheel.deltaY > 0 ? -1 : 1;
    const int slot = (state.song.slots[si][t] + step + numPatterns) % numPatterns;
    emit({ ArrangementAction::Kind::SetCell, hit.section, track, slot, true });
}

void ArrangementView::mouseMove(const juce::MouseEvent& event)
{
    const auto hit = locate(event.position);
    if (hit.area != hover.area || hit.section != hover.section || hit.row != hover.row || hit.slot != hover.slot)
    {
        hover = hit;
        setMouseCursor(hit.area == Hit::Area::Chip ? juce::MouseCursor::DraggingHandCursor
                       : hit.area == Hit::Area::None ? juce::MouseCursor::NormalCursor
                                                     : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ArrangementView::mouseExit(const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void ArrangementView::showCellMenu(int section, int track)
{
    const auto si = static_cast<std::size_t>(section);
    const auto t = static_cast<std::size_t>(track);
    const int current = state.song.slots[si][t];
    const bool on = state.song.trackOn[si][t];
    juce::PopupMenu menu;
    menu.addSectionHeader(state.names[t] + "  /  " + juce::String(songPartName(state.song.parts[si])) + " "
                          + juce::String(section + 1));
    for (int slot = 0; slot < numPatterns; ++slot)
    {
        const int count = state.contentCount[t][static_cast<std::size_t>(slot)];
        menu.addItem(1 + slot, "Loop " + slotLetter(slot) + "   "
                                   + (count > 0 ? juce::String(count) + (state.drums[t] ? " hits" : " notes") : juce::String("(empty)")),
                     true, on && slot == current);
    }
    menu.addItem(10, "Silent in this part", true, !on);
    menu.addSeparator();
    menu.addItem(11, "Edit this loop");
    menu.addItem(12, "Make a separate copy of this loop for this part");
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<ArrangementView>(this), section, track, current](int result) {
            if (safe == nullptr || result == 0)
                return;
            if (result >= 1 && result <= numPatterns)
                safe->emit({ ArrangementAction::Kind::SetCell, section, track, result - 1, true });
            else if (result == 10)
                safe->emit({ ArrangementAction::Kind::SetCell, section, track, current, false });
            else if (result == 11)
                safe->emit({ ArrangementAction::Kind::EditLoop, section, track, current });
            else if (result == 12)
                safe->emit({ ArrangementAction::Kind::MakeUnique, section, track });
        });
}

void ArrangementView::showHeaderMenu(int section)
{
    const auto current = state.song.parts[static_cast<std::size_t>(section)];
    juce::PopupMenu types;
    for (int part = 1; part <= static_cast<int>(SongPart::numParts); ++part)
    {
        const auto type = static_cast<SongPart>(part % static_cast<int>(SongPart::numParts)); // generic last
        types.addItem(100 + static_cast<int>(type), songPartName(type), true, type == current);
    }
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String(songPartName(current)) + "  /  bars " + juce::String(section * 4 + 1) + "-"
                          + juce::String(section * 4 + 4));
    menu.addSubMenu("Name this part", types);
    menu.addItem(1, "Play from here");
    menu.addItem(2, "Edit this part in Loop view");
    menu.addSeparator();
    menu.addItem(3, "Duplicate", state.song.sections < maxSections);
    menu.addItem(4, "Insert empty part after", state.song.sections < maxSections);
    menu.addItem(5, "Move left", section > 0);
    menu.addItem(6, "Move right", section < state.song.sections - 1);
    menu.addSeparator();
    menu.addItem(7, "Delete part", state.song.sections > 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<ArrangementView>(this), section](int result) {
            if (safe == nullptr || result == 0)
                return;
            using K = ArrangementAction::Kind;
            if (result >= 100)
                safe->emit({ K::SetPartType, section, -1, result - 100 });
            else if (result == 1)
                safe->emit({ K::SelectSection, section });
            else if (result == 2)
                safe->emit({ K::EditPart, section });
            else if (result == 3)
                safe->emit({ K::Duplicate, section });
            else if (result == 4)
                safe->emit({ K::Insert, section });
            else if (result == 5)
                safe->emit({ K::Move, section, -1, section - 1 });
            else if (result == 6)
                safe->emit({ K::Move, section, -1, section + 1 });
            else if (result == 7)
                safe->emit({ K::Remove, section });
        });
}
}
