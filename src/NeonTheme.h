#pragma once
#include <algorithm>
#include "OmarchyTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora::ui
{
// Mutable runtime palette: set once from the Omarchy theme (or the built-in
// neon defaults) and re-applied live whenever the theme changes. Paint code
// reads these at paint time, so a repaint is all a theme switch needs.
inline juce::Colour background { 0xff080b12 }, panel { 0xff101622 }, raised { 0xff192231 };
inline juce::Colour border { 0xff263346 }, text { 0xffe7f0fc }, muted { 0xff8b9db7 };
inline juce::Colour cyan { 0xff57efd5 }, violet { 0xffb19aff }, blue { 0xff62aaff }, danger { 0xffff7c93 };
inline juce::Colour warn { 0xffffb86b };

// True when the live palette is dark; adaptive rendering (glass edges,
// toggle text, highlights) keys off this instead of assuming darkness.
inline bool uiDark = true;

inline void applyPalette(const omarchy::Palette& palette)
{
    background = juce::Colour(palette.background);
    panel = juce::Colour(palette.panel);
    raised = juce::Colour(palette.raised);
    border = juce::Colour(palette.border);
    text = juce::Colour(palette.text);
    muted = juce::Colour(palette.muted);
    uiDark = palette.dark;
    cyan = juce::Colour(palette.accent);
    violet = juce::Colour(palette.drums);
    // Melody follows the accent; drums/audio/danger track their theme hues.
    blue = juce::Colour(palette.audio);
    danger = juce::Colour(palette.danger);
    warn = juce::Colour(palette.warn);
}

// Global UI text scale, following the display scale (Omarchy monitor scale).
// Re-applied on every theme pass so display changes take effect live; every
// caption, button, label and editor in the app sizes through font(), so one
// factor scales all text.
inline float uiScale = 1.0f;
inline void setScale(float scale) { uiScale = std::clamp(scale, 1.0f, 2.0f); }
inline juce::Font font(float size, bool bold = false, float tracking = 0.0f)
{
    return juce::Font(juce::FontOptions(std::max(10.5f, size * 1.08f * uiScale), bold ? juce::Font::bold : juce::Font::plain))
        .withExtraKerningFactor(tracking);
}

inline void caption(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> area,
                    juce::Colour colour = muted, float size = 10.0f)
{
    g.setFont(font(size, true, 0.13f));
    g.setColour(colour);
    g.drawFittedText(value, area, juce::Justification::centredLeft, 1, 0.75f);
}

// Soft outer glow used for hovered/pressed controls and selections. The
// colour always comes from the live palette so Omarchy themes keep working.
inline void glow(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour,
                 float radius = 8.0f, float alpha = 0.22f)
{
    g.setColour(colour.withAlpha(alpha));
    g.fillRoundedRectangle(bounds.expanded(3.0f), radius + 2.0f);
    g.setColour(colour.withAlpha(alpha * 0.45f));
    g.fillRoundedRectangle(bounds.expanded(6.0f), radius + 4.0f);
}

inline void surface(juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 12.0f)
{
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(bounds.translated(0, 3), radius);
    g.setGradientFill(juce::ColourGradient(panel.brighter(0.075f), bounds.getTopLeft(),
                                          panel.darker(0.16f), bounds.getBottomRight(), false));
    g.fillRoundedRectangle(bounds, radius);
    // Glass top edge: a faint contrasting line catching the surface.
    g.setColour((uiDark ? juce::Colours::white : juce::Colours::black).withAlpha(uiDark ? 0.06f : 0.10f));
    g.drawLine(bounds.getX() + radius, bounds.getY() + 1.0f, bounds.getRight() - radius, bounds.getY() + 1.0f, 1.0f);
    g.setColour(border.withAlpha(0.8f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

class NeonTheme final : public juce::LookAndFeel_V4
{
public:
    NeonTheme() { applyPalette(); }

    // Re-syncs every LookAndFeel colour from the live ui:: palette. Call after
    // ui::applyPalette() and repaint; no restart needed.
    void applyPalette()
    {
        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::TextButton::buttonColourId, raised);
        setColour(juce::TextButton::buttonOnColourId, cyan);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, uiDark ? cyan : cyan.darker(0.45f));
        setColour(juce::Label::textColourId, text);
        setColour(juce::Slider::thumbColourId, cyan);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, background);
        setColour(juce::Slider::textBoxOutlineColourId, border);
        setColour(juce::TextEditor::backgroundColourId, background);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::highlightColourId, cyan.withAlpha(0.25f));
        setColour(juce::TextEditor::outlineColourId, border);
        setColour(juce::TextEditor::focusedOutlineColourId, cyan);
        setColour(juce::ComboBox::backgroundColourId, raised);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, border);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha(uiDark ? 0.15f : 0.22f));
        setColour(juce::PopupMenu::highlightedTextColourId, uiDark ? cyan : cyan.darker(0.45f));
        setColour(juce::TooltipWindow::backgroundColourId, raised);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, border);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int height) override
    {
        return font(height < 28 ? 12.0f : 13.0f, true, 0.025f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool hover, bool down) override
    {
        const auto role = button.getProperties()["role"].toString();
        const auto accent = button.findColour(juce::TextButton::buttonOnColourId);
        const bool active = button.getToggleState() || down;
        const auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        const float alpha = button.isEnabled() ? 1.0f : 0.32f;
        auto fill = active ? accent.withMultipliedBrightness(0.22f) : raised;
        if (role == "primary") fill = accent;
        if (hover) fill = fill.brighter(0.12f);
        if (down) fill = fill.darker(0.1f);
        // Luminous edge: hovered, pressed, and selected controls bloom.
        if (down)
            glow(g, bounds, accent, 8.0f, 0.30f * alpha);
        else if (active)
            glow(g, bounds, accent, 8.0f, ((role == "track" || role == "trackCompact") ? 0.34f : 0.20f) * alpha);
        else if (hover)
            glow(g, bounds, accent, 8.0f, (role == "pad" ? 0.16f : 0.10f) * alpha);
        if (active || (hover && role == "pad"))
        {
            g.setColour(accent.withAlpha(0.09f * alpha));
            g.fillRoundedRectangle(bounds.expanded(1.5f), 10.0f);
        }
        g.setGradientFill(juce::ColourGradient(fill.withAlpha(alpha), bounds.getTopLeft(),
            fill.darker(0.22f).withAlpha(alpha), bounds.getBottomLeft(), false));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour((active || hover || role == "primary" ? accent.withAlpha(0.65f) : border).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
        if (button.hasKeyboardFocus(true))
        {
            g.setColour(accent);
            g.drawRoundedRectangle(bounds.reduced(2), 6.0f, 1.0f);
        }
        if (role == "track" || role == "trackCompact")
        {
            g.setColour(accent.withAlpha(active ? 1.0f : 0.32f));
            const float inset = role == "trackCompact" ? 5.0f : 13.0f;
            g.fillRoundedRectangle(bounds.getX() + 1, bounds.getY() + inset, 3.0f, bounds.getHeight() - inset * 2, 1.5f);
        }
        if (role == "pad")
        {
            g.setColour(accent.withAlpha(down ? 1.0f : hover ? 0.7f : 0.3f));
            g.fillRoundedRectangle(bounds.getX() + 13, bounds.getBottom() - 10, bounds.getWidth() - 26, 2, 1);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) override
    {
        const auto role = button.getProperties()["role"].toString();
        const auto accent = button.findColour(juce::TextButton::buttonOnColourId);
        const auto alpha = button.isEnabled() ? 1.0f : 0.35f;
        const auto bounds = button.getLocalBounds();
        const auto colour = role == "primary" ? background
            : button.getToggleState() || down ? accent : text;
        g.setColour(colour.withAlpha(alpha));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        if (role == "trackCompact")
            g.drawFittedText(button.getButtonText(), bounds.reduced(12, 2), juce::Justification::centredLeft, 1);
        else if (role == "track")
        {
            g.drawText(button.getButtonText(), bounds.reduced(18, 0).withTrimmedBottom(18), juce::Justification::centredLeft);
            caption(g, button.getProperties()["detail"].toString(),
                    { 18, 34, button.getWidth() - 32, 16 }, muted.withAlpha(alpha), 9.0f);
        }
        else if (role == "pad")
        {
            caption(g, button.getProperties()["shortcut"].toString(), { 14, 10, 30, 18 }, accent);
            g.setColour(colour.withAlpha(alpha));
            g.setFont(font(12.0f, true));
            g.drawFittedText(button.getButtonText(), bounds.reduced(12, 0).withTrimmedTop(18).withTrimmedBottom(13),
                             juce::Justification::centredLeft, 2);
        }
        else
            g.drawFittedText(button.getButtonText(), bounds.reduced(8, 2), juce::Justification::centred, 1);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float value,
                          float start, float end, juce::Slider& slider) override
    {
        const auto accent = slider.findColour(juce::Slider::thumbColourId);
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height)).reduced(9);
        const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        juce::Path arc, filled;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
        filled.addCentredArc(centre.x, centre.y, radius, radius, 0, start, start + value * (end - start), true);
        g.setColour(border);
        g.strokePath(arc, juce::PathStrokeType(3.0f));
        g.setColour(accent.withAlpha(0.12f));
        g.strokePath(filled, juce::PathStrokeType(9.0f));
        g.setColour(accent);
        g.strokePath(filled, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto knob = juce::Rectangle<float>(radius * 1.54f, radius * 1.54f).withCentre(centre);
        g.setGradientFill(juce::ColourGradient(raised.brighter(0.08f), knob.getTopLeft(), background, knob.getBottomRight(), false));
        g.fillEllipse(knob);
        g.setColour(border.brighter(0.1f));
        g.drawEllipse(knob, 1.0f);
        const auto angle = start + value * (end - start) - juce::MathConstants<float>::halfPi;
        const auto dx = std::cos(angle), dy = std::sin(angle);
        g.setColour(accent);
        g.drawLine(centre.x + dx * radius * 0.45f, centre.y + dy * radius * 0.45f,
                   centre.x + dx * radius * 0.65f, centre.y + dy * radius * 0.65f, 3.0f);
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = start + static_cast<float>(i) * (end - start) / 10.0f - juce::MathConstants<float>::halfPi;
            g.setColour(muted.withAlpha(0.4f));
            g.drawLine(centre.x + std::cos(a) * (radius + 6), centre.y + std::sin(a) * (radius + 6),
                       centre.x + std::cos(a) * (radius + 9), centre.y + std::sin(a) * (radius + 9), 1.0f);
        }
    }
};
}
