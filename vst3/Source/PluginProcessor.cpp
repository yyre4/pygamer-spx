#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SpxCore.h"   // the SPX90 II DSP core (single TU: this one)

// =====================================================================
// Parameter layout. Param 0..7 are normalised 0..1 and mapped through
// the current algorithm's descriptor table (range/step/mode), because
// JUCE parameter ranges are fixed at creation while the SPX's eight
// knobs change meaning per program -- exactly like the hardware's
// single PARAMETER edit system.
juce::AudioProcessorValueTreeState::ParameterLayout SpxAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterInt> ("program", "Program", 1, 90, 1));
    // defaults = program 1 (REV HALL) knob positions, from the ROM table
    const auto& rom0 = SPX_ROM[0];
    const auto& dt = ALG_DESC[rom0.algo];
    for (int i = 0; i < 8; i++)
    {
        float def = 0.0f;
        if (i < dt.n)
        {
            const auto& d = dt.d[i];
            def = d.max > d.min ? (rom0.p[i] - d.min) / (d.max - d.min) : 0.0f;
        }
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "param" + juce::String (i), "Param " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), juce::jlimit (0.0f, 1.0f, def)));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "balance", "Balance", juce::NormalisableRange<float> (0.0f, 1.0f), rom0.bal));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "ingain", "Input Gain", juce::NormalisableRange<float> (0.1f, 4.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "outlevel", "Output Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.9f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "source", "Source",
        juce::StringArray { "Line", "Demo Pluck", "Demo Pad", "Demo Noise" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));
    return layout;
}

SpxAudioProcessor::SpxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    spx_init();
    programParam  = apvts.getRawParameterValue ("program");
    for (int i = 0; i < 8; i++)
    {
        paramNorm[i] = apvts.getRawParameterValue ("param" + juce::String (i));
        cachedNorm[i] = paramNorm[i]->load();
    }
    balanceParam  = apvts.getRawParameterValue ("balance");
    inGainParam   = apvts.getRawParameterValue ("ingain");
    outLevelParam = apvts.getRawParameterValue ("outlevel");
    sourceParam   = apvts.getRawParameterValue ("source");
    bypassParam   = apvts.getRawParameterValue ("bypass");
    cachedBalance = balanceParam->load();
    cachedProg = 1;
}

SpxAudioProcessor::~SpxAudioProcessor() = default;

// =====================================================================
void SpxAudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Preserve user memories across re-prepares (rate changes etc.):
    // spx_set_sample_rate() re-inits the whole core.
    std::vector<unsigned char> img (SPX_USER_SIZE);
    bool haveUsers = false;
    if (coreRate > 0 && spx_serialize_users (img.data(), SPX_USER_SIZE) == SPX_USER_SIZE)
        haveUsers = true;
    else if (havePendingUsers)
    {
        img = pendingUserImage;
        haveUsers = true;
    }

    spx_set_sample_rate ((int) (sampleRate + 0.5));
    coreRate = (int) (sampleRate + 0.5);

    if (haveUsers)
        spx_deserialize_users (img.data(), SPX_USER_SIZE);

    spx_load_program (juce::jlimit (1, 90, (int) programParam->load()));
    applyGlobalsFromApvts();
    if (pendingSnapshot.valid)
        applySnapshotToCore();
    syncApvtsFromCore();
}

void SpxAudioProcessor::releaseResources() {}

bool SpxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()
        && in != juce::AudioChannelSet::disabled())
        return false;
    return true;
}

// =====================================================================
void SpxAudioProcessor::applyGlobalsFromApvts()
{
    spx.in_gain = inGainParam->load();
    spx.out_level = outLevelParam->load();
    spx.source = (uint8_t) juce::jlimit (0, 3, (int) sourceParam->load());
    spx.bypass = bypassParam->load() > 0.5f;
}

void SpxAudioProcessor::applySnapshotToCore()
{
    spx.cur.algo = (uint8_t) juce::jlimit (0, (int) ALG_COUNT - 1, pendingSnapshot.algo);
    std::memcpy (spx.cur.name, pendingSnapshot.name, sizeof (spx.cur.name));
    std::memcpy (spx.cur.p, pendingSnapshot.p, sizeof (spx.cur.p));
    spx.cur.bal = pendingSnapshot.bal;
    spx_params_changed();
    spx_reset_dsp();
}

float SpxAudioProcessor::coreToNorm (int i) const
{
    const SpxParamDesc* d = spx_param_desc (i);
    if (d == nullptr) return 0.0f;
    float v = spx.cur.p[i];
    if (d->unit == U_MODE)
        return d->mode_count > 1 ? juce::jlimit (0.0f, 1.0f, (float) (int) (v + 0.5f) / (float) (d->mode_count - 1)) : 0.0f;
    return d->max > d->min ? juce::jlimit (0.0f, 1.0f, (v - d->min) / (d->max - d->min)) : 0.0f;
}

void SpxAudioProcessor::applyNormToCore (int i, float norm)
{
    const SpxParamDesc* d = spx_param_desc (i);
    if (d == nullptr) return;
    if (d->unit == U_MODE)
    {
        int idx = juce::jlimit (0, (int) d->mode_count - 1,
                                (int) std::lround (norm * (float) (d->mode_count - 1)));
        spx_set_param (i, (float) idx);
        return;
    }
    float v = d->min + norm * (d->max - d->min);
    if (d->step > 0.0f)
        v = d->min + std::round ((v - d->min) / d->step) * d->step;
    spx_set_param (i, v);
}

void SpxAudioProcessor::syncApvtsFromCore()
{
    for (int i = 0; i < 8; i++)
    {
        float n = coreToNorm (i);
        cachedNorm[i] = n;
        if (auto* p = apvts.getParameter ("param" + juce::String (i)))
            p->setValueNotifyingHost (n);
    }
    cachedBalance = spx.cur.bal;
    if (auto* p = apvts.getParameter ("balance"))
        p->setValueNotifyingHost (spx.cur.bal);
    cachedProg = spx.cur_prog;
}

// =====================================================================
void SpxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = 2; ch < numOut; ch++)
        buffer.clear (ch, 0, n);

    // ---- pull host/UI parameter changes into the core ---------------
    const int progWanted = juce::jlimit (1, 90, (int) programParam->load());
    if (progWanted != (int) spx.cur_prog)
    {
        // Program change: parameters the host has NOT touched since our
        // last sync adopt the new program's defaults; parameters the
        // host edited in the same batch (session restore, automation
        // landing on the change point) win over the defaults.
        float hostNorm[8];
        for (int i = 0; i < 8; i++)
            hostNorm[i] = paramNorm[i]->load();
        const float hostBal = balanceParam->load();
        spx_load_program (progWanted);
        for (int i = 0; i < 8; i++)
        {
            if (std::fabs (hostNorm[i] - cachedNorm[i]) <= 1.0e-6f)
            {
                float nrm = coreToNorm (i);
                cachedNorm[i] = nrm;
                if (auto* p = apvts.getParameter ("param" + juce::String (i)))
                    p->setValueNotifyingHost (nrm);
            }
            else
            {
                applyNormToCore (i, hostNorm[i]);
                cachedNorm[i] = hostNorm[i];
            }
        }
        if (std::fabs (hostBal - cachedBalance) <= 1.0e-6f)
        {
            cachedBalance = spx.cur.bal;
            if (auto* p = apvts.getParameter ("balance"))
                p->setValueNotifyingHost (spx.cur.bal);
        }
        else
        {
            spx.cur.bal = hostBal;
            cachedBalance = hostBal;
        }
        cachedProg = spx.cur_prog;
    }
    else
    {
        for (int i = 0; i < 8; i++)
        {
            float v = paramNorm[i]->load();
            if (v != cachedNorm[i])
            {
                cachedNorm[i] = v;
                applyNormToCore (i, v);
            }
        }
        float bal = balanceParam->load();
        if (bal != cachedBalance)
        {
            cachedBalance = bal;
            spx.cur.bal = bal;
        }
    }
    applyGlobalsFromApvts();

    // ---- MIDI: the core's own byte parser (PC / notes / CC7 / CC91) -
    for (auto md : midiMessages)
    {
        for (int i = 0; i < md.numBytes; i++)
            spx_midi_feed ((uint8_t) md.data[i]);
    }

    // ---- audio: stereo in summed to the hardware's single input -----
    auto* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    auto* inR = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    auto* outL = numOut > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; i++)
    {
        float mono = 0.0f;
        if (inL != nullptr && inR != nullptr) mono = 0.5f * (inL[i] + inR[i]);
        else if (inL != nullptr) mono = inL[i];
        float ol, orr;
        spx_process (mono, &ol, &orr);
        if (outL != nullptr) outL[i] = ol;
        if (outR != nullptr) outR[i] = orr;
        else if (outL != nullptr) outL[i] = 0.5f * (ol + orr);
    }

    // ---- reflect core-side changes (MIDI PC / CC91 / note pitch) ----
    if ((int) spx.cur_prog != progWanted)
    {
        if (auto* p = apvts.getParameter ("program"))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) spx.cur_prog));
        syncApvtsFromCore();
    }
    else if (spx.cur.bal != cachedBalance)
    {
        syncApvtsFromCore();
    }
    else
    {
        // MIDI note-on can retune the pitch program's params in-core
        for (int i = 0; i < 8; i++)
        {
            float cn = coreToNorm (i);
            if (std::fabs (cn - cachedNorm[i]) > 0.001f)
            {
                syncApvtsFromCore();
                break;
            }
        }
    }
}

// =====================================================================
juce::AudioProcessorEditor* SpxAudioProcessor::createEditor()
{
    return new SpxEditor (*this);
}

juce::AudioProcessorParameter* SpxAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

// =====================================================================
// State: APVTS XML + the core's 60 user memories (base64 of the SD
// image format) + a snapshot of the current (possibly edited) program.
void SpxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    std::vector<unsigned char> img (SPX_USER_SIZE);
    if (spx_serialize_users (img.data(), SPX_USER_SIZE) == SPX_USER_SIZE)
        xml->setAttribute ("spxUsers", juce::Base64::toBase64 (img.data(), img.size()));
    xml->setAttribute ("spxAlgo", (int) spx.cur.algo);
    xml->setAttribute ("spxName", juce::String (spx.cur.name));
    for (int i = 0; i < 8; i++)
        xml->setAttribute ("spxP" + juce::String (i), (double) spx.cur.p[i]);
    xml->setAttribute ("spxBal", (double) spx.cur.bal);
    copyXmlToBinary (*xml, destData);
}

void SpxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    auto b64 = xml->getStringAttribute ("spxUsers");
    if (b64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (b64);
        if (mb.getSize() == SPX_USER_SIZE)
        {
            pendingUserImage.assign ((unsigned char*) mb.getData(),
                                     (unsigned char*) mb.getData() + mb.getSize());
            havePendingUsers = true;
            if (coreRate > 0)
                spx_deserialize_users (pendingUserImage.data(), SPX_USER_SIZE);
        }
    }
    if (xml->hasAttribute ("spxAlgo"))
    {
        pendingSnapshot.valid = true;
        pendingSnapshot.algo = xml->getIntAttribute ("spxAlgo");
        auto nm = xml->getStringAttribute ("spxName");
        std::memset (pendingSnapshot.name, 0, sizeof (pendingSnapshot.name));
        std::memcpy (pendingSnapshot.name, nm.toRawUTF8(),
                     juce::jmin ((size_t) 16, std::strlen (nm.toRawUTF8())));
        for (int i = 0; i < 8; i++)
            pendingSnapshot.p[i] = (float) xml->getDoubleAttribute ("spxP" + juce::String (i));
        pendingSnapshot.bal = (float) xml->getDoubleAttribute ("spxBal", 0.5);
    }
    if (coreRate > 0)
    {
        spx_load_program (juce::jlimit (1, 90, (int) programParam->load()));
        applyGlobalsFromApvts();
        if (pendingSnapshot.valid)
            applySnapshotToCore();
        syncApvtsFromCore();
    }
}

// =====================================================================
// Editor-facing accessors
int          SpxAudioProcessor::getCoreProgramNumber() const { return spx.cur_prog; }
juce::String SpxAudioProcessor::getCoreProgramName() const { return juce::String (spx.cur.name); }
juce::String SpxAudioProcessor::getCoreAlgoName() const { return juce::String (ALG_NAME[spx.cur.algo]); }
int          SpxAudioProcessor::getCoreParamCount() const { return spx_param_count(); }
int          SpxAudioProcessor::getMeterLeds() const { return spx_meter_leds(); }
juce::String SpxAudioProcessor::getSourceName() const { return juce::String (SPX_SRC_NAME[spx.source]); }
bool         SpxAudioProcessor::isFreezePlaying() const
{
    return spx.cur.algo == ALG_FREEZE && spx.frz_state == 1;
}
juce::String SpxAudioProcessor::getCoreParamName (int i) const
{
    const SpxParamDesc* d = spx_param_desc (i);
    return d != nullptr ? juce::String (d->name) : juce::String ("—");
}
juce::String SpxAudioProcessor::getCoreParamValueText (int i) const
{
    const SpxParamDesc* d = spx_param_desc (i);
    if (d == nullptr) return "—";
    char buf[32] = {};
    spx_format_value (d, spx.cur.p[i], buf, sizeof (buf));
    return juce::String (buf);
}
void SpxAudioProcessor::triggerFromGui() { spx_trigger(); }
int SpxAudioProcessor::storeFromGui()
{
    int slot = (int) spx.cur_prog;
    if (slot <= SPX_ROM_PROGRAMS)
        slot = SPX_ROM_PROGRAMS + 1;   // ROM edits land in user slot 31
    if (spx_store_program (slot))
    {
        if (slot != (int) spx.cur_prog)
        {
            spx_load_program (slot);
            if (auto* p = apvts.getParameter ("program"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) slot));
        }
        syncApvtsFromCore();
    }
    return slot;
}
void SpxAudioProcessor::recallFromGui()
{
    spx_load_program (spx.cur_prog);
    syncApvtsFromCore();
}

// =====================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpxAudioProcessor();
}
