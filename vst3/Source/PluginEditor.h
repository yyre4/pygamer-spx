#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// =====================================================================
// Custom look: chunky modular-style rotary knobs with coloured caps.
class SpxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

// =====================================================================
// SPX-style membrane button with an optional status LED.
class MembraneButton : public juce::Component
{
public:
    std::function<void()> onClick;
    void setText (const juce::String& t) { if (text != t) { text = t; repaint(); } }
    void setLed (bool on) { if (ledOn != on) { ledOn = on; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    juce::String text;
    bool ledOn = false, pressed = false;
};

// =====================================================================
// Big red two-digit seven-segment program number.
class SegDisplay : public juce::Component
{
public:
    void setValue (int v) { if (value != v) { value = v; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    int value = 1;
    void drawDigit (juce::Graphics&, int digit, float dx, float dy, float dw, float dh);
};

// =====================================================================
// Backlit LCD: program name / selected parameter readout.
class LcdDisplay : public juce::Component
{
public:
    void setText (const juce::String& l1, const juce::String& l2)
    {
        if (line1 != l1 || line2 != l2) { line1 = l1; line2 = l2; repaint(); }
    }
    void paint (juce::Graphics&) override;
private:
    juce::String line1, line2;
};

// =====================================================================
// Seven-segment-style input level meter (horizontal bar of LED blocks).
class LedMeter : public juce::Component
{
public:
    void setLeds (int n) { if (leds != n) { leds = n; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    int leds = 0;
};

// =====================================================================
class SpxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpxEditor (SpxAudioProcessor&);
    ~SpxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshParamLabels();
    void refreshLcd();
    void selectParam (int i) { selectedParam = i; refreshLcd(); }

    SpxAudioProcessor& proc;
    SpxLookAndFeel lnf;

    struct KnobSet
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> nameLabel, valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    KnobSet paramKnobs[8];
    KnobSet inputKnob, inputKnobSmall, balanceKnob, outputKnob;

    SegDisplay segDisplay;
    LcdDisplay lcd;
    LedMeter meter;
    MembraneButton btnParam, btnBalance, btnUtility, btnStore, btnRecall,
                   btnMemMinus, btnMemPlus, btnBypass, btnSource, btnTrigger;

    int selectedParam = 0;      // 0..7, or 8 = balance
    int lastProg = -1;
    juce::String algoTitle;

    KnobSet& makeKnob (KnobSet&, const juce::String& paramId, juce::Colour cap, bool small);
    static juce::String twoDigits (int v) { return juce::String (v).paddedLeft ('0', 2); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpxEditor)
};
