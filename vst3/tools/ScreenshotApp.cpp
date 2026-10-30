// ScreenshotApp.cpp -- renders the actual plugin editor to PNGs for
// socials. Builds the processor from source (same code the VST3
// wraps), feeds it the built-in DEMO PLUCK source so the meter is
// alive, opens the real editor in a window (run under Xvfb), and
// snapshots it at 2x. Then composes social.png: the plugin centred
// on a clean dark backdrop with a soft shadow.
// Usage: SpxScreenshot <output-dir>
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

class SpxScreenshotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SpxScreenshot"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    void initialise (const juce::String& args) override
    {
        juce::File outDir = args.isEmpty()
            ? juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile ("workspace/spx-vst3")
            : juce::File (args);
        outDir.createDirectory();

        proc = std::make_unique<SpxAudioProcessor>();
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);

        // DEMO PLUCK source: the box makes sound (and meter) on its own
        if (auto* par = proc->apvts.getParameter ("source"))
            par->setValueNotifyingHost (par->convertTo0to1 (1.0f));

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        for (int b = 0; b < 200; b++)   // ~2.1 s of plucks through REV HALL
            proc->processBlock (buf, midi);
        printf ("meter LEDs lit for screenshot: %d / 7\n", proc->getMeterLeds());

        editor.reset (proc->createEditor());
        window = std::make_unique<juce::DocumentWindow> (
            "PyGamer SPX", juce::Colour (0xff101012), juce::DocumentWindow::allButtons);
        window->setUsingNativeTitleBar (false);
        window->setContentNonOwned (editor.get(), false);
        window->centreWithSize (editor->getWidth(), editor->getHeight());
        window->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        printf ("editor size at snapshot: %d x %d (window %d x %d)\n",
                editor->getWidth(), editor->getHeight(),
                window->getWidth(), window->getHeight());
        // pin the size explicitly: a window content area (title bar,
        // borders) must not shrink the render
        editor->setSize (980, 340);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        // ---- clean 2x snapshot of just the editor ---------------------
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        writePng (img, outDir.getChildFile ("screenshot.png"));
        printf ("screenshot.png: %d x %d\n", img.getWidth(), img.getHeight());
        reportStats (img, "screenshot");

        // ---- social composition: plugin on a dark backdrop ------------
        const int W = 1600, H = 1600;
        juce::Image social (juce::Image::RGB, W, H, true);
        {
            juce::Graphics g (social);
            juce::ColourGradient bg (juce::Colour (0xff1b1c22), 0.0f, 0.0f,
                                     juce::Colour (0xff0a0a0d), 0.0f, (float) H, false);
            g.setGradientFill (bg);
            g.fillAll();
            // soft radial glow behind the unit
            juce::ColourGradient glow (juce::Colour (0x2e4d96ff), W * 0.5f, H * 0.44f,
                                       juce::Colour (0x00000000), W * 0.5f, H * 0.05f, true);
            g.setGradientFill (glow);
            g.fillEllipse (W * 0.5f - 700.0f, H * 0.44f - 700.0f, 1400.0f, 1400.0f);

            const float scale = 1440.0f / (float) img.getWidth();
            const int pw = (int) (img.getWidth() * scale), ph = (int) (img.getHeight() * scale);
            const int px = (W - pw) / 2, py = (int) (H * 0.44f) - ph / 2;
            // soft shadow: stacked offset rounded rects, decreasing alpha
            for (int i = 6; i >= 1; i--)
            {
                g.setColour (juce::Colours::black.withAlpha (0.10f));
                g.fillRoundedRectangle ((float) px - (float) i * 1.5f, (float) py + 14.0f + (float) i * 2.0f,
                                        (float) pw + (float) i * 3.0f, (float) ph + (float) i * 1.5f, 14.0f);
            }
            g.setOpacity (1.0f);
            g.drawImage (img, px, py, pw, ph, 0, 0, img.getWidth(), img.getHeight());
        }
        writePng (social, outDir.getChildFile ("social.png"));
        printf ("social.png: %d x %d\n", social.getWidth(), social.getHeight());
        reportStats (social, "social");

        window->setVisible (false);
        quit();
    }

    void shutdown() override
    {
        window.reset();
        editor.reset();
        proc.reset();
    }

private:
    static void writePng (const juce::Image& img, juce::File f)
    {
        // NOTE: JUCE's FileOutputStream does not reliably truncate an
        // existing file on this platform (a previous PNG's bytes were
        // found appended-to, leaving a stale image at the head of the
        // file). Delete first so the file is exactly this image.
        f.deleteFile();
        juce::PNGImageFormat png;
        std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
        if (out != nullptr)
        {
            png.writeImageToStream (img, *out);
            out->flush();
            printf ("wrote %s (%lld bytes)\n", f.getFullPathName().toRawUTF8(),
                    (long long) f.getSize());
        }
    }
    static void reportStats (const juce::Image& img, const char* tag)
    {
        // sampled colour diversity + luminance spread: proof of a real render
        std::set<juce::uint32> colours;
        double mean = 0, m2 = 0; long n = 0;
        for (int y = 0; y < img.getHeight(); y += 5)
            for (int x = 0; x < img.getWidth(); x += 5)
            {
                auto c = img.getPixelAt (x, y);
                colours.insert (c.getARGB() & 0xffffff);
                double lum = c.getPerceivedBrightness();
                mean += lum; n++;
            }
        mean /= (double) n;
        for (int y = 0; y < img.getHeight(); y += 5)
            for (int x = 0; x < img.getWidth(); x += 5)
            {
                double d = img.getPixelAt (x, y).getPerceivedBrightness() - mean;
                m2 += d * d;
            }
        printf ("%s stats: %zu distinct colours (sampled), luminance stddev %.3f\n",
                tag, colours.size(), std::sqrt (m2 / (double) n));
    }

    std::unique_ptr<SpxAudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<juce::DocumentWindow> window;
};

START_JUCE_APPLICATION (SpxScreenshotApp)
