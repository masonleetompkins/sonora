#include "MainComponent.h"

class SonoraApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Sonora"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->requestClose();
    }
    bool moreThanOneInstanceAllowed() override { return false; }

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("Sonora", juce::Colour(0xff11141c), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new sonora::MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(1120, 780, 3840, 2160);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            getContentComponent()->grabKeyboardFocus();
        }
        void requestClose()
        {
            static_cast<sonora::MainComponent*>(getContentComponent())->requestClose(
                [] { juce::JUCEApplication::getInstance()->quit(); });
        }
        void closeButtonPressed() override { requestClose(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION(SonoraApplication)
