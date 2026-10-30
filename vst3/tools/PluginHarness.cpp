// PluginHarness.cpp -- headless validation of the built VST3.
// Loads the plugin through JUCE's AudioPluginFormatManager (the same
// path a DAW's scanner takes), then proves: instantiation, editor
// exists, silence -> silence, REV HALL impulse tail decays, pitch
// program shifts 440 -> ~880 Hz at 48 kHz, and state save/load
// round-trips into a fresh instance.
// Usage: SpxHarness /path/to/PyGamer SPX.vst3
#include <JuceHeader.h>
#include <cmath>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s\n", msg); } \
                              else printf("ok: %s\n", msg); } while (0)

// Hosted (VST3) parameters are matched by name; JUCE 7 hosts expose
// normalised values, so plain<->normalised uses this plugin's known
// ranges (Program = int 1..90, everything else used here is 0..1).
static juce::AudioProcessorParameter* findParam (juce::AudioProcessor& p, const juce::String& name)
{
    for (auto* par : p.getParameters())
        if (par->getName (512) == name)
            return par;
    return nullptr;
}
static void setPlain (juce::AudioProcessor& p, const juce::String& name, float plain)
{
    if (auto* par = findParam (p, name))
    {
        float norm = (name == "Program") ? (plain - 1.0f) / 89.0f : plain;
        par->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    }
}
static float getPlain (juce::AudioProcessor& p, const juce::String& name)
{
    auto* par = findParam (p, name);
    if (par == nullptr) return -999.0f;
    float n = par->getValue();
    return (name == "Program") ? n * 89.0f + 1.0f : n;
}

static const int SR = 48000, BLOCK = 512;

static void runBlocks (juce::AudioPluginInstance& inst, juce::AudioBuffer<float>& buf,
                       juce::MidiBuffer& midi, int blocks)
{
    for (int i = 0; i < blocks; i++)
        inst.processBlock (buf, midi);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2)
    {
        fprintf (stderr, "usage: SpxHarness <plugin.vst3>\n");
        return 2;
    }
    const juce::String path (argv[1]);

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> types;
    for (int i = 0; i < fm.getNumFormats(); i++)
        fm.getFormat (i)->findAllTypesForFile (types, path);
    CHECK (types.size() > 0, "VST3 file scanned (plugin description found)");
    if (types.isEmpty())
        return 1;
    printf ("found: %s by %s (%s)\n", types[0]->name.toRawUTF8(),
            types[0]->manufacturerName.toRawUTF8(), types[0]->pluginFormatName.toRawUTF8());

    juce::String err;
    auto inst = fm.createPluginInstance (*types[0], SR, BLOCK, err);
    CHECK (inst != nullptr, "plugin instantiates");
    if (inst == nullptr)
    {
        printf ("error: %s\n", err.toRawUTF8());
        return 1;
    }
    inst->setPlayConfigDetails (2, 2, SR, BLOCK);
    inst->prepareToPlay (SR, BLOCK);
    CHECK (inst->hasEditor(), "plugin reports an editor");

    {
        auto* ed = inst->createEditor();
        CHECK (ed != nullptr, "editor component creates");
        if (ed != nullptr)
        {
            printf ("editor size: %d x %d\n", ed->getWidth(), ed->getHeight());
            delete ed;
        }
    }

    juce::AudioBuffer<float> buf (2, BLOCK);
    juce::MidiBuffer midi;

    // ---- silence -> silence -------------------------------------------
    buf.clear();
    runBlocks (*inst, buf, midi, SR / BLOCK);
    double peak = 0.0;
    buf.clear();
    runBlocks (*inst, buf, midi, 4);
    for (int ch = 0; ch < 2; ch++)
        for (int i = 0; i < BLOCK; i++)
            peak = juce::jmax (peak, (double) std::fabs (buf.getSample (ch, i)));
    CHECK (peak == 0.0, "silence in -> silence out");

    // ---- REV HALL impulse tail decays ----------------------------------
    // (input is rewritten every block: the buffer is in-place, so last
    // block's output must never become this block's input)
    setPlain (*inst, "Program", 1.0f);
    std::vector<float> tailL, tailR;
    for (int b = 0; b < SR * 3 / BLOCK + 1; b++)
    {
        buf.clear();
        if (b == 0)
        {
            buf.setSample (0, 0, 0.9f);
            buf.setSample (1, 0, 0.9f);
        }
        inst->processBlock (buf, midi);
        tailL.insert (tailL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLOCK);
        tailR.insert (tailR.end(), buf.getReadPointer (1), buf.getReadPointer (1) + BLOCK);
    }
    auto winRms = [] (const std::vector<float>& v, double a, double b) {
        double s = 0; int n = 0;
        for (int i = (int) (a * SR); i < (int) (b * SR) && i < (int) v.size(); i++) { s += v[i] * v[i]; n++; }
        return n ? std::sqrt (s / n) : 0.0;
    };
    bool finite = true;
    double maxAbs = 0.0;
    for (float v : tailL) { if (! std::isfinite (v)) finite = false; maxAbs = juce::jmax (maxAbs, (double) std::fabs (v)); }
    CHECK (finite, "reverb tail: all samples finite");
    CHECK (maxAbs < 1.2, "reverb tail: no overload");
    double early = winRms (tailL, 0.10, 0.40), late = winRms (tailL, 2.40, 2.90);
    double earlyPeak = 0.0;
    for (int i = (int) (0.02 * SR); i < (int) (0.40 * SR); i++)
        earlyPeak = juce::jmax (earlyPeak, (double) std::fabs (tailL[i]));
    printf ("reverb tail: early RMS %.4f, early peak %.4f, late RMS %.4f\n", early, earlyPeak, late);
    CHECK (early > 0.0015 && earlyPeak > 0.01, "reverb: impulse produces a tail");
    CHECK (late < early * 0.35, "reverb: tail decays");

    // ---- pitch program shifts 440 -> ~880 ------------------------------
    setPlain (*inst, "Program", 21.0f);        // PITCH CHANGE A
    setPlain (*inst, "Param 2", 1.0f);          // PITCH A knob max -> +12 st
    setPlain (*inst, "Balance", 1.0f);         // 100% wet
    std::vector<float> tone;
    double phase = 0.0;
    for (int b = 0; b < SR * 2 / BLOCK; b++)
    {
        for (int i = 0; i < BLOCK; i++)
        {
            float s = (float) (0.5 * std::sin (phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / SR;
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        inst->processBlock (buf, midi);
        tone.insert (tone.end(), buf.getReadPointer (0), buf.getReadPointer (0) + BLOCK);
    }
    {
        int cross = 0, prev = 0;
        const int a = (int) (1.5 * SR), b = (int) tone.size();
        for (int i = a; i < b; i++)
        {
            int s = tone[i] > 0.006f ? 1 : (tone[i] < -0.006f ? -1 : 0);
            if (s != 0 && prev != 0 && s != prev) cross++;
            if (s != 0) prev = s;
        }
        double f = cross / 2.0 / ((b - a) / (double) SR);
        char msg[100];
        snprintf (msg, sizeof (msg), "pitch +12 via plugin @48k: %.1f Hz (want 880)", f);
        CHECK (std::fabs (f - 880.0) < 44.0, msg);
    }

    // ---- state save / load round-trip ----------------------------------
    // Sonic proof (stronger than reading host-side parameter caches):
    // instance A is given distinctive settings (DELAY L,R with DLY L at
    // 0.31 of its range = 434 ms, balance 0.77) and its state saved.
    // Instance B restores that state; instance C is set to the same
    // values live. After flushing both delay lines with silence, an
    // impulse must produce the SAME output from B and C.
    auto renderImpulseResponse = [&] (juce::AudioPluginInstance& p) {
        std::vector<float> outL, outR;
        juce::AudioBuffer<float> b2 (2, BLOCK);
        juce::MidiBuffer m2;
        int flushBlocks = (int) (1.6 * SR) / BLOCK;
        for (int i = 0; i < flushBlocks; i++) { b2.clear(); p.processBlock (b2, m2); }
        int total = (int) (1.2 * SR) / BLOCK;
        for (int i = 0; i < total; i++)
        {
            b2.clear();
            if (i == 0) { b2.setSample (0, 0, 0.9f); b2.setSample (1, 0, 0.9f); }
            p.processBlock (b2, m2);
            outL.insert (outL.end(), b2.getReadPointer (0), b2.getReadPointer (0) + BLOCK);
            outR.insert (outR.end(), b2.getReadPointer (1), b2.getReadPointer (1) + BLOCK);
        }
        return std::pair (outL, outR);
    };

    setPlain (*inst, "Program", 7.0f);         // DELAY L,R
    setPlain (*inst, "Param 1", 0.31f);        // DLY L -> 434 ms
    setPlain (*inst, "Balance", 0.77f);
    runBlocks (*inst, buf, midi, 2);
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    CHECK (state.getSize() > 0, "state serializes");

    juce::String err2;
    auto inst2 = fm.createPluginInstance (*types[0], SR, BLOCK, err2);
    auto inst3 = fm.createPluginInstance (*types[0], SR, BLOCK, err2);
    CHECK (inst2 != nullptr && inst3 != nullptr, "state-test instances create");
    if (inst2 != nullptr && inst3 != nullptr)
    {
        inst2->setPlayConfigDetails (2, 2, SR, BLOCK);
        inst2->prepareToPlay (SR, BLOCK);
        inst2->setStateInformation (state.getData(), (int) state.getSize());
        float prog = getPlain (*inst2, "Program");
        printf ("restored program (host view): %.1f\n", prog);
        CHECK (std::fabs (prog - 7.0f) < 0.01f, "state: program restored");
        CHECK (inst2->hasEditor(), "second instance has editor");

        inst3->setPlayConfigDetails (2, 2, SR, BLOCK);
        inst3->prepareToPlay (SR, BLOCK);
        setPlain (*inst3, "Program", 7.0f);
        setPlain (*inst3, "Param 1", 0.31f);
        setPlain (*inst3, "Balance", 0.77f);
        runBlocks (*inst3, buf, midi, 2);

        auto rb = renderImpulseResponse (*inst2);
        auto rc = renderImpulseResponse (*inst3);
        double maxDiff = 0.0, echoPeak = 0.0;
        const int n2 = (int) std::min (rb.first.size(), rc.first.size());
        for (int i = 0; i < n2; i++)
        {
            maxDiff = juce::jmax (maxDiff, (double) std::fabs (rb.first[i] - rc.first[i]));
            double t = i / (double) SR;
            if (t > 0.40 && t < 0.47)
                echoPeak = juce::jmax (echoPeak, (double) std::fabs (rb.first[i]));
        }
        printf ("state round-trip: max |B-C| = %.6f, B echo peak @434ms = %.3f\n", maxDiff, echoPeak);
        CHECK (echoPeak > 0.03, "state: restored instance echoes at the saved 434 ms");
        CHECK (maxDiff < 0.002, "state: restored instance sounds identical to live-set instance");
    }

    printf ("\nplugin harness: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
