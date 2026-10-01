#pragma once
#include "NeonTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{
// Right-hand AI chat panel: transcript, suggestion chips, and a message box.
// Pure view: the owner runs requests and feeds replies back in.
class AiSidebar final : public juce::Component
{
public:
    struct Message
    {
        enum class Role { User, Assistant, Info, Error } role = Role::Info;
        juce::String text;
    };

    // Agent: one conversation that can operate the whole app. The older
    // per-track and whole-song modes remain for reference and tests.
    enum class Mode { Melody, Drums, Song, Agent };
    std::function<void(const juce::String&)> onSend;
    std::function<void()> onCancel, onClose, onNewChat, onVoice;

    AiSidebar();
    // title: target track; detail: part + loop; canChat false greys the input.
    void setContext(const juce::String& title, const juce::String& detail, Mode mode, bool canChat,
                    const juce::String& unavailableReason = {});
    void setBusy(bool busy);
    void setVoiceRecording(bool recording);
    void appendDictation(const juce::String& text);
    void setStatus(const juce::String& text);
    void addMessage(Message message);
    void clearMessages();
    bool hasMessages() const { return !transcript.messages.empty(); }
    void focusInput() { input.grabKeyboardFocus(); }
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Transcript final : public juce::Component
    {
        std::vector<Message> messages;
        juce::String placeholder;
        int layoutFor(int width); // returns content height and caches bubble rectangles
        void paint(juce::Graphics& g) override;
        std::vector<juce::Rectangle<float>> bubbles;
        std::vector<juce::TextLayout> layouts;
    };
    void submit(const juce::String& text);
    void relayoutTranscript(bool scrollToEnd);
    void updateChips();

    Transcript transcript;
    juce::Viewport viewport;
    juce::TextEditor input;
    juce::TextButton send { "Send" }, stop { "Stop" }, mic { "Mic" }, paste { "Paste" }, close { "X" }, newChat { "New chat" };
    std::array<juce::TextButton, 4> chips;
    juce::Label status;
    juce::String title, detail, unavailable;
    bool busy = false, canChat = true;
    Mode mode = Mode::Melody;
};
}
