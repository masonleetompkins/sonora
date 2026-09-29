#include "AiSidebar.h"

namespace sonora
{
namespace
{
constexpr float bubblePad = 9.0f, bubbleGap = 8.0f;

juce::Colour bubbleColour(AiSidebar::Message::Role role)
{
    using R = AiSidebar::Message::Role;
    switch (role)
    {
        case R::User: return ui::cyan.withAlpha(0.18f);
        case R::Assistant: return ui::raised;
        case R::Error: return ui::danger.withAlpha(0.15f);
        case R::Info: break;
    }
    return juce::Colours::transparentBlack;
}

juce::Colour textColour(AiSidebar::Message::Role role)
{
    using R = AiSidebar::Message::Role;
    return role == R::Info ? ui::muted : role == R::Error ? ui::danger : ui::text;
}
}

AiSidebar::AiSidebar()
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&transcript, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(6);
    addAndMakeVisible(input);
    input.setMultiLine(true, true);
    input.setReturnKeyStartsNewLine(false);
    input.setFont(ui::font(15.0f));
    input.setColour(juce::TextEditor::backgroundColourId, ui::background);
    input.setColour(juce::TextEditor::textColourId, ui::text);
    input.setColour(juce::TextEditor::outlineColourId, ui::border);
    input.setColour(juce::TextEditor::focusedOutlineColourId, ui::cyan);
    input.onReturnKey = [this] { submit(input.getText()); };
    for (auto* button : { &send, &stop, &mic, &paste, &close, &newChat })
    {
        addAndMakeVisible(button);
        button->setWantsKeyboardFocus(false);
    }
    send.getProperties().set("role", "primary");
    send.onClick = [this] { submit(input.getText()); };
    stop.onClick = [this] { if (onCancel) onCancel(); };
    mic.onClick = [this] { if (onVoice) onVoice(); };
    mic.setTooltip("Record your request locally with Voxtype. Click again to transcribe; review the text before Send.");
    paste.onClick = [this] {
        const auto text = juce::SystemClipboard::getTextFromClipboard().trim();
        if (text.isEmpty())
            setStatus("Clipboard is empty. Dictate with Caps Lock first (Voxtype falls back to clipboard).");
        else
            appendDictation(text);
    };
    paste.setTooltip("Insert clipboard text into the message box. Use this after Caps Lock dictation: "
                     "simulated typing arrives garbled in this app, but Voxtype's clipboard fallback keeps the text intact.");
    close.onClick = [this] { if (onClose) onClose(); };
    newChat.onClick = [this] { if (onNewChat) onNewChat(); };
    close.setTooltip("Close the AI sidebar (Ctrl+I)");
    newChat.setTooltip("Start a fresh conversation (the current pattern is kept).");
    stop.setVisible(false);
    for (auto& chip : chips)
    {
        addAndMakeVisible(chip);
        chip.setWantsKeyboardFocus(false);
        chip.onClick = [this, &chip] { submit(chip.getButtonText()); };
    }
    addAndMakeVisible(status);
    status.setFont(ui::font(12.0f));
    status.setColour(juce::Label::textColourId, ui::muted);
    updateChips();
}

void AiSidebar::setContext(const juce::String& newTitle, const juce::String& newDetail, Mode newMode, bool available,
                           const juce::String& unavailableReason)
{
    const bool chipsChanged = newMode != mode;
    title = newTitle;
    detail = newDetail;
    mode = newMode;
    canChat = available;
    unavailable = unavailableReason;
    input.setEnabled(canChat && !busy);
    send.setEnabled(canChat && !busy);
    mic.setEnabled(canChat && !busy);
    paste.setEnabled(canChat && !busy);
    for (auto& chip : chips)
        chip.setEnabled(canChat && !busy);
    input.setTextToShowWhenEmpty(!canChat ? unavailable
                                 : mode == Mode::Song ? "Describe the song you want, or ask for changes..."
                                 : mode == Mode::Drums ? "Describe a beat, or ask to change this one..."
                                                       : "Describe a melody, or ask to change this one...",
                                 ui::muted);
    transcript.placeholder = !canChat ? unavailable
        : mode == Mode::Song
            ? "I can see every track's loops and the whole arrangement. Ask me to compose the full song: I'll pick "
              "the sections, decide which tracks play where, and write variation loops into empty slots."
        : mode == Mode::Drums ? "Ask me to write a beat or change this one. I can hear the other tracks in this section."
                              : "Ask me to write a melody or change this one. I can hear the other tracks in this section.";
    if (chipsChanged)
        updateChips();
    relayoutTranscript(false);
    repaint();
}

void AiSidebar::updateChips()
{
    static constexpr const char* melodyIdeas[] { "Write a melody that fits", "Make it simpler",
                                                 "More syncopated", "Answer the other parts" };
    static constexpr const char* drumIdeas[] { "Write a beat that fits", "Add a hi-hat groove",
                                               "Add a fill in bar 4", "Make it half-time" };
    static constexpr const char* songIdeas[] { "Compose the full song", "Add more variation",
                                               "Bigger final chorus", "Make it shorter" };
    for (std::size_t i = 0; i < chips.size(); ++i)
        chips[i].setButtonText(mode == Mode::Song ? songIdeas[i] : mode == Mode::Drums ? drumIdeas[i] : melodyIdeas[i]);
}

void AiSidebar::setBusy(bool value)
{
    busy = value;
    stop.setVisible(busy);
    send.setVisible(!busy);
    input.setEnabled(canChat && !busy);
    mic.setEnabled(canChat && !busy);
    paste.setEnabled(canChat && !busy);
    send.setEnabled(canChat && !busy);
    for (auto& chip : chips)
        chip.setEnabled(canChat && !busy);
    if (!busy)
        status.setText({}, juce::dontSendNotification);
}

void AiSidebar::setVoiceRecording(bool recording)
{
    mic.setButtonText(recording ? "Stop mic" : "Mic");
    mic.setColour(juce::TextButton::buttonOnColourId, recording ? ui::danger : ui::cyan);
    if (recording)
        status.setText("Listening... click Stop mic when you're done", juce::dontSendNotification);
    else if (!busy)
        status.setText({}, juce::dontSendNotification);
    resized();
}

void AiSidebar::appendDictation(const juce::String& text)
{
    if (text.trim().isEmpty())
        return;
    if (input.getText().isNotEmpty() && !input.getText().endsWithChar(' '))
        input.insertTextAtCaret(" ");
    input.insertTextAtCaret(text.trim() + " ");
    input.grabKeyboardFocus();
}

void AiSidebar::setStatus(const juce::String& text) { status.setText(text, juce::dontSendNotification); }

void AiSidebar::addMessage(Message message)
{
    transcript.messages.push_back(std::move(message));
    relayoutTranscript(true);
}

void AiSidebar::clearMessages()
{
    transcript.messages.clear();
    relayoutTranscript(false);
}

void AiSidebar::submit(const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || busy || !canChat)
        return;
    input.clear();
    if (onSend)
        onSend(trimmed);
}

int AiSidebar::Transcript::layoutFor(int width)
{
    bubbles.clear();
    layouts.clear();
    float y = 6.0f;
    const auto emptyText = placeholder;
    std::vector<Message> shown = messages;
    if (shown.empty() && emptyText.isNotEmpty())
        shown.push_back({ Message::Role::Info, emptyText });
    for (const auto& message : shown)
    {
        const bool user = message.role == Message::Role::User;
        const float maxWidth = static_cast<float>(width) * (message.role == Message::Role::Info ? 1.0f : 0.86f) - 2.0f * bubblePad;
        juce::AttributedString text;
        text.append(message.text, ui::font(message.role == Message::Role::Info ? 12.5f : 14.0f), textColour(message.role));
        text.setWordWrap(juce::AttributedString::byWord);
        juce::TextLayout layout;
        layout.createLayout(text, std::max(40.0f, maxWidth));
        float textWidth = 0.0f;
        for (int line = 0; line < layout.getNumLines(); ++line)
            textWidth = std::max(textWidth, layout.getLine(line).getLineBoundsX().getLength());
        const float w = std::min(maxWidth, std::ceil(textWidth) + 1.0f) + 2.0f * bubblePad;
        const float h = layout.getHeight() + 2.0f * bubblePad;
        const float x = user ? static_cast<float>(width) - w : 0.0f;
        bubbles.emplace_back(x, y, w, h);
        layouts.push_back(std::move(layout));
        y += h + bubbleGap;
    }
    return static_cast<int>(std::ceil(y));
}

void AiSidebar::Transcript::paint(juce::Graphics& g)
{
    std::vector<Message> shown = messages;
    if (shown.empty() && placeholder.isNotEmpty())
        shown.push_back({ Message::Role::Info, placeholder });
    for (std::size_t i = 0; i < bubbles.size() && i < shown.size(); ++i)
    {
        const auto& bubble = bubbles[i];
        const auto role = shown[i].role;
        if (role != Message::Role::Info)
        {
            g.setColour(bubbleColour(role));
            g.fillRoundedRectangle(bubble, 9.0f);
            if (role == Message::Role::User)
            {
                g.setColour(ui::cyan.withAlpha(0.35f));
                g.drawRoundedRectangle(bubble.reduced(0.5f), 9.0f, 1.0f);
            }
        }
        layouts[i].draw(g, bubble.reduced(bubblePad));
    }
}

void AiSidebar::relayoutTranscript(bool scrollToEnd)
{
    const int width = std::max(80, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    const int height = transcript.layoutFor(width);
    transcript.setSize(width, std::max(height, viewport.getHeight()));
    if (scrollToEnd)
        viewport.setViewPosition(0, std::max(0, transcript.getHeight() - viewport.getHeight()));
    transcript.repaint();
}

void AiSidebar::resized()
{
    auto area = getLocalBounds().reduced(14, 12);
    auto header = area.removeFromTop(54);
    close.setBounds(header.removeFromRight(30).removeFromTop(28));
    header.removeFromRight(6);
    newChat.setBounds(header.removeFromRight(82).removeFromTop(28));
    area.removeFromTop(4);
    auto bottom = area.removeFromBottom(192);
    status.setBounds(bottom.removeFromTop(20));
    bottom.removeFromTop(2);
    auto chipArea = bottom.removeFromTop(62);
    const int chipWidth = (chipArea.getWidth() - 6) / 2;
    for (std::size_t i = 0; i < chips.size(); ++i)
        chips[i].setBounds(chipArea.getX() + static_cast<int>(i % 2) * (chipWidth + 6),
                           chipArea.getY() + static_cast<int>(i / 2) * 32, chipWidth, 28);
    bottom.removeFromTop(6);
    auto buttons = bottom.removeFromBottom(32);
    send.setBounds(buttons.removeFromRight(88));
    stop.setBounds(send.getBounds());
    buttons.removeFromRight(6);
    mic.setBounds(buttons.removeFromRight(84));
    buttons.removeFromRight(6);
    paste.setBounds(buttons.removeFromRight(84));
    bottom.removeFromBottom(6);
    input.setBounds(bottom);
    viewport.setBounds(area);
    relayoutTranscript(true);
}

void AiSidebar::paint(juce::Graphics& g)
{
    ui::surface(g, getLocalBounds().toFloat(), 12.0f);
    const auto area = getLocalBounds().reduced(14, 12);
    ui::caption(g, "AI ASSISTANT", { area.getX(), area.getY(), 200, 18 }, ui::cyan, 10.0f);
    g.setColour(ui::text);
    g.setFont(ui::font(16.0f, true));
    g.drawFittedText(title, juce::Rectangle<int>(area.getX(), area.getY() + 18, area.getWidth() - 124, 18),
                     juce::Justification::centredLeft, 1, 0.8f);
    g.setColour(ui::muted);
    g.setFont(ui::font(12.0f));
    g.drawFittedText(detail, juce::Rectangle<int>(area.getX(), area.getY() + 36, area.getWidth(), 16),
                     juce::Justification::centredLeft, 1, 0.8f);
    g.setColour(ui::border.withAlpha(0.7f));
    g.drawHorizontalLine(area.getY() + 56, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
}
}
