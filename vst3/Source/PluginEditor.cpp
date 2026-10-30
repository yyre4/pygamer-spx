#include "PluginEditor.h"

// =====================================================================
// Look & feel
void SpxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto cap = juce::Colour ((juce::uint32) (juce::int64) slider.getProperties()
                                 .getWithDefault ("capColour", (juce::int64) 0xff8a919c));

    // drop shadow
    g.setColour (juce::Colour (0x59000000));
    g.fillEllipse (cx - r, cy - r + 3.0f, r * 2.0f, r * 2.0f);
    // value arc
    juce::Path bgArc;
    bgArc.addCentredArc (cx, cy, r + 1.0f, r + 1.0f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff17181b));
    g.strokePath (bgArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path valArc;
    valArc.addCentredArc (cx, cy, r + 1.0f, r + 1.0f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (cap.withAlpha (0.85f));
    g.strokePath (valArc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // body
    juce::ColourGradient body (juce::Colour (0xff585b61), cx, cy - r, juce::Colour (0xff2a2b2f), cx, cy + r, false);
    g.setGradientFill (body);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour (juce::Colour (0xff141518));
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.2f);
    // coloured cap
    const float cr = r * 0.70f;
    juce::ColourGradient capGrad (cap.brighter (0.28f), cx, cy - cr, cap.darker (0.30f), cx, cy + cr, false);
    g.setGradientFill (capGrad);
    g.fillEllipse (cx - cr, cy - cr, cr * 2.0f, cr * 2.0f);
    // pointer
    const float ix = cx + cr * 0.80f * std::sin (angle);
    const float iy = cy - cr * 0.80f * std::cos (angle);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.drawLine (cx + cr * 0.30f * std::sin (angle), cy - cr * 0.30f * std::cos (angle), ix, iy, 3.0f);
}

// =====================================================================
// Membrane button
void MembraneButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad (pressed ? juce::Colour (0xff2c2e33) : juce::Colour (0xff46484e),
                               b.getX(), b.getY(),
                               pressed ? juce::Colour (0xff232428) : juce::Colour (0xff33353a),
                               b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (juce::Colour (0xff5a5d63));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (juce::Colour (0xffd8dade));
    g.setFont (juce::Font (10.5f, juce::Font::bold));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
    // status LED, top-right
    g.setColour (ledOn ? juce::Colour (0xffffb02e) : juce::Colour (0xff4a4436));
    g.fillEllipse ((float) getWidth() - 13.0f, 5.0f, 6.0f, 6.0f);
    if (ledOn)
    {
        g.setColour (juce::Colour (0x55ffb02e));
        g.fillEllipse ((float) getWidth() - 15.0f, 3.0f, 10.0f, 10.0f);
    }
}
void MembraneButton::mouseDown (const juce::MouseEvent&) { pressed = true; repaint(); if (onClick) onClick(); }
void MembraneButton::mouseUp (const juce::MouseEvent&) { pressed = false; repaint(); }
void MembraneButton::mouseExit (const juce::MouseEvent&) { pressed = false; repaint(); }

// =====================================================================
// Seven-segment display
void SegDisplay::drawDigit (juce::Graphics& g, int digit, float dx, float dy, float dw, float dh)
{
    static const bool SEGS[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
        {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
        {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1},
    };
    const float t = dw * 0.20f;
    const juce::Rectangle<float> rects[7] = {
        { dx + 2, dy, dw - 4, t },                                  // a
        { dx + dw - t, dy + 2, t, dh * 0.5f - 3 },                  // b
        { dx + dw - t, dy + dh * 0.5f + 1, t, dh * 0.5f - 3 },      // c
        { dx + 2, dy + dh - t, dw - 4, t },                         // d
        { dx, dy + dh * 0.5f + 1, t, dh * 0.5f - 3 },               // e
        { dx, dy + 2, t, dh * 0.5f - 3 },                           // f
        { dx + 2, dy + dh * 0.5f - t * 0.5f, dw - 4, t },           // g
    };
    digit = juce::jlimit (0, 9, digit);
    for (int s = 0; s < 7; s++)
    {
        if (SEGS[digit][s])
        {
            g.setColour (juce::Colour (0x40ff3b26));
            g.fillRoundedRectangle (rects[s].expanded (1.5f), 2.0f);
            g.setColour (juce::Colour (0xffff3b26));
            g.fillRoundedRectangle (rects[s], 2.0f);
        }
        else
        {
            g.setColour (juce::Colour (0x14ff3b26));
            g.fillRoundedRectangle (rects[s], 2.0f);
        }
    }
}
void SegDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0a0506));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (juce::Colour (0xff2c2126));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    const float dw = (b.getWidth() - 30.0f) * 0.5f, dh = b.getHeight() - 18.0f;
    drawDigit (g, value / 10, 9.0f, 9.0f, dw, dh);
    drawDigit (g, value % 10, 21.0f + dw, 9.0f, dw, dh);
}

// =====================================================================
void LcdDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient grad (juce::Colour (0xffdcebb2), b.getX(), b.getY(),
                               juce::Colour (0xffb7cb84), b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (juce::Colour (0x30ffffff));
    g.fillRoundedRectangle (b.withHeight (b.getHeight() * 0.45f), 5.0f);
    g.setColour (juce::Colour (0xff7d9250));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (juce::Colour (0xff26320f));
    auto mono = juce::Font (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::bold);
    g.setFont (mono);
    g.drawText (line1, juce::Rectangle<int> (12, 6, getWidth() - 24, getHeight() / 2),
                juce::Justification::centredLeft, false);
    g.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 14.5f, juce::Font::bold));
    g.drawText (line2, juce::Rectangle<int> (12, getHeight() / 2 + 2, getWidth() - 24, getHeight() / 2 - 6),
                juce::Justification::centredLeft, false);
}

// =====================================================================
void LedMeter::paint (juce::Graphics& g)
{
    static const juce::Colour COL[7] = {
        juce::Colour (0xff39d353), juce::Colour (0xff39d353), juce::Colour (0xff39d353),
        juce::Colour (0xff39d353), juce::Colour (0xff39d353), juce::Colour (0xffffd23e),
        juce::Colour (0xffff4030),
    };
    const float gap = 3.0f;
    const float w = ((float) getWidth() - gap * 6.0f) / 7.0f;
    for (int i = 0; i < 7; i++)
    {
        juce::Rectangle<float> r ((float) i * (w + gap), 1.0f, w, (float) getHeight() - 2.0f);
        g.setColour (COL[i].withAlpha (i < leds ? 1.0f : 0.13f));
        g.fillRoundedRectangle (r, 2.0f);
    }
}

// =====================================================================
SpxEditor::KnobSet& SpxEditor::makeKnob (KnobSet& ks, const juce::String& paramId,
                                         juce::Colour cap, bool small)
{
    ks.slider = std::make_unique<juce::Slider> (
        juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    ks.slider->getProperties().set ("capColour", (juce::int64) cap.getARGB());
    ks.slider->setLookAndFeel (&lnf);
    if (small)
        ks.slider->setMouseDragSensitivity (140);
    addAndMakeVisible (*ks.slider);
    ks.nameLabel = std::make_unique<juce::Label> ();
    ks.nameLabel->setFont (juce::Font (10.0f, juce::Font::bold));
    ks.nameLabel->setColour (juce::Label::textColourId, juce::Colour (0xffc9ccd1));
    ks.nameLabel->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (*ks.nameLabel);
    ks.valueLabel = std::make_unique<juce::Label> ();
    ks.valueLabel->setFont (juce::Font (10.0f));
    ks.valueLabel->setColour (juce::Label::textColourId, juce::Colour (0xff8f959d));
    ks.valueLabel->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (*ks.valueLabel);
    ks.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, paramId, *ks.slider);
    return ks;
}

SpxEditor::SpxEditor (SpxAudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    static const juce::Colour CAPS[8] = {
        juce::Colour (0xff2ec4b6), juce::Colour (0xffff8c42), juce::Colour (0xffffd23e),
        juce::Colour (0xff6bcb77), juce::Colour (0xff4d96ff), juce::Colour (0xff9b5de5),
        juce::Colour (0xfff15bb5), juce::Colour (0xffef476f),
    };
    for (int i = 0; i < 8; i++)
    {
        makeKnob (paramKnobs[i], "param" + juce::String (i), CAPS[i], false);
        paramKnobs[i].slider->onDragStart = [this, i] { selectParam (i); };
    }
    makeKnob (inputKnob, "ingain", juce::Colour (0xffb8bec6), false);
    makeKnob (inputKnobSmall, "ingain", juce::Colour (0xffb8bec6), true);
    makeKnob (balanceKnob, "balance", juce::Colour (0xffe8e9eb), true);
    balanceKnob.slider->onDragStart = [this] { selectParam (8); };
    makeKnob (outputKnob, "outlevel", juce::Colour (0xffb8bec6), true);
    inputKnob.nameLabel->setText ("INPUT GAIN", juce::dontSendNotification);
    inputKnobSmall.nameLabel->setText ("INPUT", juce::dontSendNotification);
    balanceKnob.nameLabel->setText ("BALANCE", juce::dontSendNotification);
    outputKnob.nameLabel->setText ("OUTPUT", juce::dontSendNotification);

    addAndMakeVisible (segDisplay);
    addAndMakeVisible (lcd);
    addAndMakeVisible (meter);

    auto wire = [this] (MembraneButton& b, const juce::String& t, std::function<void()> f) {
        b.setText (t);
        b.onClick = std::move (f);
        addAndMakeVisible (b);
    };
    wire (btnParam, "PARAMETER", [this] {
        int n = juce::jmax (1, proc.getCoreParamCount());
        selectParam ((selectedParam + 1) % n);
    });
    wire (btnBalance, "BALANCE", [this] { selectParam (8); });
    wire (btnUtility, "UTILITY", [this] {
        if (auto* par = proc.apvts.getParameter ("source"))
            par->setValueNotifyingHost (par->convertTo0to1 (
                (float) ((int) (par->convertFrom0to1 (par->getValue()) + 1.0f) % 4)));
    });
    wire (btnStore, "STORE", [this] { proc.storeFromGui(); });
    wire (btnRecall, "RECALL", [this] { proc.recallFromGui(); });
    wire (btnMemMinus, "MEM -", [this] {
        if (auto* par = proc.apvts.getParameter ("program"))
            par->setValueNotifyingHost (par->convertTo0to1 (
                (float) juce::jlimit (1, 90, proc.getCoreProgramNumber() - 1)));
    });
    wire (btnMemPlus, "MEM +", [this] {
        if (auto* par = proc.apvts.getParameter ("program"))
            par->setValueNotifyingHost (par->convertTo0to1 (
                (float) juce::jlimit (1, 90, proc.getCoreProgramNumber() + 1)));
    });
    wire (btnBypass, "BYPASS", [this] {
        if (auto* par = proc.apvts.getParameter ("bypass"))
            par->setValueNotifyingHost (par->getValue() > 0.5f ? 0.0f : 1.0f);
    });
    wire (btnSource, "SOURCE", [this] {
        if (auto* par = proc.apvts.getParameter ("source"))
            par->setValueNotifyingHost (par->convertTo0to1 (
                (float) ((int) (par->convertFrom0to1 (par->getValue()) + 1.0f) % 4)));
    });
    wire (btnTrigger, "TRIGGER", [this] { proc.triggerFromGui(); });

    refreshParamLabels();
    refreshLcd();
    setSize (980, 340);
    startTimerHz (24);
}

SpxEditor::~SpxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// =====================================================================
void SpxEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient panel (juce::Colour (0xff35373c), 0.0f, 0.0f,
                                juce::Colour (0xff26272b), 0.0f, b.getHeight(), false);
    g.setGradientFill (panel);
    g.fillAll();

    auto section = [&g] (juce::Rectangle<float> r, const juce::String& label) {
        g.setColour (juce::Colour (0xff2d2e33));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff41434a));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (juce::Colour (0xff9aa0a8));
        g.setFont (juce::Font (10.0f, juce::Font::bold));
        g.drawText (label, r.withHeight (18.0f).translated (10.0f, 3.0f),
                    juce::Justification::centredLeft, false);
    };
    section ({ 16.0f, 56.0f, 136.0f, 268.0f }, "INPUT");
    section ({ 168.0f, 56.0f, 432.0f, 268.0f }, "PROGRAM / MEMORY");
    section ({ 616.0f, 56.0f, 348.0f, 268.0f },
             algoTitle.isEmpty() ? juce::String ("PARAMETERS") : "PARAMETERS - " + algoTitle);

    // corner screws
    for (auto c : { juce::Point<float> (16, 16), juce::Point<float> (964, 16),
                    juce::Point<float> (16, 324), juce::Point<float> (964, 324) })
    {
        g.setColour (juce::Colour (0xff4a4c51));
        g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (0xff1d1e21));
        g.drawLine (c.x - 3.5f, c.y - 3.5f, c.x + 3.5f, c.y + 3.5f, 1.6f);
    }

    // title
    g.setColour (juce::Colour (0xffecedef));
    g.setFont (juce::Font (23.0f, juce::Font::bold));
    g.drawText ("PYGAMER SPX", juce::Rectangle<int> (28, 12, 260, 28),
                juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff9aa0a8));
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("SPX90 II EMULATION", juce::Rectangle<int> (292, 19, 220, 16),
                juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff777d85));
    g.setFont (juce::Font (10.0f));
    g.drawText ("VST3  -  30 PROGRAMS + 60 USER  -  MIDI PC SELECT",
                juce::Rectangle<int> (560, 20, 396, 16), juce::Justification::centredRight, false);

    // hints in the program section
    g.setColour (juce::Colour (0xff767c84));
    g.setFont (juce::Font (9.5f));
    g.drawText ("MEM -/+ choose a program. PARAMETER picks the knob the LCD is showing; BALANCE jumps to the mix.",
                juce::Rectangle<int> (184, 262, 400, 14), juce::Justification::centredLeft, false);
    g.drawText ("STORE saves edits into a user slot (31-90). RECALL reloads the stored program. TRIGGER fires Freeze / Pan.",
                juce::Rectangle<int> (184, 278, 400, 14), juce::Justification::centredLeft, false);
    g.drawText ("UTILITY / SOURCE: Line in, or the built-in demo sources (pluck / pad / noise) for trying programs.",
                juce::Rectangle<int> (184, 294, 400, 14), juce::Justification::centredLeft, false);
}

void SpxEditor::resized()
{
    // INPUT section
    inputKnob.slider->setBounds (34, 86, 100, 92);
    inputKnob.nameLabel->setBounds (24, 178, 120, 13);
    inputKnob.valueLabel->setBounds (24, 191, 120, 13);
    meter.setBounds (28, 212, 112, 14);
    btnSource.setBounds (28, 238, 112, 32);
    btnTrigger.setBounds (28, 278, 112, 32);

    // PROGRAM section
    segDisplay.setBounds (184, 84, 108, 62);
    lcd.setBounds (304, 84, 280, 62);
    btnParam.setBounds (184, 164, 96, 36);
    btnBalance.setBounds (288, 164, 88, 36);
    btnUtility.setBounds (384, 164, 88, 36);
    btnStore.setBounds (480, 164, 104, 36);
    btnRecall.setBounds (184, 208, 96, 36);
    btnMemMinus.setBounds (288, 208, 88, 36);
    btnMemPlus.setBounds (384, 208, 88, 36);
    btnBypass.setBounds (480, 208, 104, 36);

    // PARAMETERS section: 8 knobs, 4 x 2
    for (int i = 0; i < 8; i++)
    {
        const int col = i % 4, row = i / 4;
        const int x = 624 + col * 86, y = 84 + row * 94;
        paramKnobs[i].slider->setBounds (x + 13, y, 58, 58);
        paramKnobs[i].nameLabel->setBounds (x, y + 59, 84, 13);
        paramKnobs[i].valueLabel->setBounds (x, y + 72, 84, 13);
    }
    // bottom strip: BALANCE / OUTPUT / INPUT small knobs
    auto strip = [] (KnobSet& ks, int x) {
        ks.slider->setBounds (x, 272, 46, 46);
        ks.nameLabel->setJustificationType (juce::Justification::centredLeft);
        ks.nameLabel->setBounds (x + 50, 278, 58, 13);
        ks.valueLabel->setJustificationType (juce::Justification::centredLeft);
        ks.valueLabel->setBounds (x + 50, 292, 58, 13);
    };
    strip (balanceKnob, 624);
    strip (outputKnob, 738);
    strip (inputKnobSmall, 852);
}

// =====================================================================
void SpxEditor::refreshParamLabels()
{
    const int n = proc.getCoreParamCount();
    for (int i = 0; i < 8; i++)
    {
        paramKnobs[i].slider->setVisible (i < n);
        paramKnobs[i].nameLabel->setVisible (i < n);
        paramKnobs[i].valueLabel->setVisible (i < n);
        if (i < n)
            paramKnobs[i].nameLabel->setText (proc.getCoreParamName (i), juce::dontSendNotification);
    }
    algoTitle = proc.getCoreAlgoName();
    if (selectedParam >= n && selectedParam != 8)
        selectedParam = 0;
}

void SpxEditor::refreshLcd()
{
    juce::String l1 = twoDigits (proc.getCoreProgramNumber()) + " " + proc.getCoreProgramName();
    juce::String l2;
    if (selectedParam == 8)
    {
        auto bal = proc.apvts.getRawParameterValue ("balance")->load();
        l2 = "BALANCE " + juce::String ((int) std::lround (bal * 100.0f)) + "%";
    }
    else if (selectedParam < proc.getCoreParamCount())
    {
        l2 = proc.getCoreParamName (selectedParam) + " " + proc.getCoreParamValueText (selectedParam);
    }
    lcd.setText (l1, l2);
}

void SpxEditor::timerCallback()
{
    meter.setLeds (proc.getMeterLeds());
    segDisplay.setValue (proc.getCoreProgramNumber());

    const int prog = proc.getCoreProgramNumber();
    if (prog != lastProg)
    {
        lastProg = prog;
        refreshParamLabels();
        repaint();
    }
    for (int i = 0; i < 8 && i < proc.getCoreParamCount(); i++)
        paramKnobs[i].valueLabel->setText (proc.getCoreParamValueText (i), juce::dontSendNotification);

    auto fmtGain = [] (float v) { return "x" + juce::String (v, 2); };
    inputKnob.valueLabel->setText (fmtGain (proc.apvts.getRawParameterValue ("ingain")->load()),
                                   juce::dontSendNotification);
    inputKnobSmall.valueLabel->setText (fmtGain (proc.apvts.getRawParameterValue ("ingain")->load()),
                                        juce::dontSendNotification);
    balanceKnob.valueLabel->setText (
        juce::String ((int) std::lround (proc.apvts.getRawParameterValue ("balance")->load() * 100.0f)) + "%",
        juce::dontSendNotification);
    outputKnob.valueLabel->setText (
        juce::String ((int) std::lround (proc.apvts.getRawParameterValue ("outlevel")->load() * 100.0f)) + "%",
        juce::dontSendNotification);

    if (auto* par = proc.apvts.getParameter ("bypass"))
        btnBypass.setLed (par->getValue() > 0.5f);
    btnTrigger.setLed (proc.isFreezePlaying());
    auto srcName = proc.getSourceName();
    if (srcName.startsWith ("LINE"))
        srcName = "LINE IN";
    btnSource.setText (srcName);

    refreshLcd();
}
