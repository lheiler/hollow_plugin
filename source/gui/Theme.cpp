#include "Theme.h"

#include "HollowFonts.h"

namespace hl::gui
{
using namespace juce;

namespace colours
{
    Colour forModule (int moduleId)
    {
        // filter 1, trash, filter 2, convolve, motion, degrade, dynamics, echo
        static const Colour c[] = { Colour (0xff2f6db0), Colour (0xffc8442a), Colour (0xff4f95cf), Colour (0xff3a8797),
                                    Colour (0xff6570c4), Colour (0xff8a6db3), Colour (0xff5c7896), Colour (0xff1b2a40) };
        return c[jlimit (0, 7, moduleId)];
    }

    Colour forBand (int band)
    {
        static const Colour c[] = { Colour (0xff1b2a40), Colour (0xff2f6db0), Colour (0xff7fa8d2) };
        return c[jlimit (0, 2, band)];
    }
} // namespace colours

namespace
{
    // Oxanium sits a little larger than the old system font per point; the mono is wide, so it runs smaller
    constexpr float sansScale = 1.08f, monoScale = 1.0f;

    Typeface::Ptr load (const char* data, int size)
    {
        return Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    Font make (const Typeface::Ptr& typeface, float height)
    {
        return Font (FontOptions (typeface).withHeight (height));
    }
} // namespace

EmbeddedTypefaces::EmbeddedTypefaces()
    : light (load (HollowFonts::OxaniumLight_ttf, HollowFonts::OxaniumLight_ttfSize)),
      regular (load (HollowFonts::OxaniumRegular_ttf, HollowFonts::OxaniumRegular_ttfSize)),
      semiBold (load (HollowFonts::OxaniumSemiBold_ttf, HollowFonts::OxaniumSemiBold_ttfSize)),
      mono (load (HollowFonts::MartianMonoRegular_ttf, HollowFonts::MartianMonoRegular_ttfSize))
{
}

Font font (float height, bool bold)
{
    const SharedResourcePointer<EmbeddedTypefaces> t;
    return make (bold ? t->semiBold : t->regular, height * sansScale);
}

Font lightFont (float height)
{
    const SharedResourcePointer<EmbeddedTypefaces> t;
    return make (t->light, height * sansScale);
}

Font monoFont (float height)
{
    const SharedResourcePointer<EmbeddedTypefaces> t;
    return make (t->mono, height * monoScale);
}

void drawLabel (Graphics& g, const String& text, Rectangle<float> area, Justification justification, Colour colour, float height)
{
    const auto upper = text.toUpperCase();
    auto f = font (height, true).withExtraKerningFactor (0.14f);
    const float width = GlyphArrangement::getStringWidth (f, upper);

    // squeeze long captions in narrow spots rather than clipping them
    if (width > area.getWidth() && width > 0.0f)
    {
        const float squeeze = jmax (0.72f, area.getWidth() / width);
        f = font (height * jmax (0.85f, squeeze), true).withExtraKerningFactor (0.14f * squeeze * squeeze);
    }

    g.setColour (colour);
    g.setFont (f);
    g.drawText (upper, area, justification, false);
}

float frequencyToX (float hz, Rectangle<float> area) noexcept
{
    const float norm = std::log (jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
    return area.getX() + norm * area.getWidth();
}

float xToFrequency (float x, Rectangle<float> area) noexcept
{
    const float norm = jlimit (0.0f, 1.0f, (x - area.getX()) / jmax (1.0f, area.getWidth()));
    return 20.0f * std::pow (1000.0f, norm);
}

void drawFrequencyGrid (Graphics& g, Rectangle<float> area, bool withLabels)
{
    static const float lines[] = { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                   1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };

    for (float f : lines)
    {
        const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
        g.setColour (major ? colours::gridStrong : colours::grid);
        g.fillRect (Rectangle<float> (std::round (frequencyToX (f, area)), area.getY(), 1.0f, area.getHeight()));
    }

    if (! withLabels)
        return;

    static const std::pair<float, const char*> labels[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                            { 1000, "1K" }, { 2000, "2K" }, { 5000, "5K" }, { 10000, "10K" } };
    g.setFont (monoFont (9.5f));
    g.setColour (colours::textFaint);

    for (auto [f, text] : labels)
        g.drawText (text, Rectangle<float> (frequencyToX (f, area) + 3.0f, area.getBottom() - 13.0f, 30.0f, 11.0f),
                    Justification::centredLeft, false);
}

//==============================================================================
namespace
{
    /** In-place separable box blur (3 passes approximate a gaussian). */
    void blur (Image& image, int radius)
    {
        if (radius < 1)
            return;

        Image::BitmapData data (image, Image::BitmapData::readWrite);
        const int w = data.width, h = data.height;
        std::vector<float> line ((size_t) jmax (w, h) * 4), out ((size_t) jmax (w, h) * 4);

        const auto pass = [&] (bool horizontal)
        {
            const int length = horizontal ? w : h, count = horizontal ? h : w;

            for (int j = 0; j < count; ++j)
            {
                for (int i = 0; i < length; ++i)
                {
                    const auto c = horizontal ? data.getPixelColour (i, j) : data.getPixelColour (j, i);
                    const float a = c.getFloatAlpha();
                    line[(size_t) i * 4 + 0] = c.getFloatRed() * a;
                    line[(size_t) i * 4 + 1] = c.getFloatGreen() * a;
                    line[(size_t) i * 4 + 2] = c.getFloatBlue() * a;
                    line[(size_t) i * 4 + 3] = a;
                }

                for (int i = 0; i < length; ++i)
                {
                    float acc[4] = {};
                    int n = 0;

                    for (int k = jmax (0, i - radius); k <= jmin (length - 1, i + radius); ++k, ++n)
                        for (int ch = 0; ch < 4; ++ch)
                            acc[ch] += line[(size_t) k * 4 + (size_t) ch];

                    for (int ch = 0; ch < 4; ++ch)
                        out[(size_t) i * 4 + (size_t) ch] = acc[ch] / (float) n;
                }

                for (int i = 0; i < length; ++i)
                {
                    const float a = out[(size_t) i * 4 + 3];
                    const auto c = a > 1.0e-4f ? Colour::fromFloatRGBA (out[(size_t) i * 4] / a, out[(size_t) i * 4 + 1] / a, out[(size_t) i * 4 + 2] / a, a)
                                               : Colours::transparentBlack;

                    if (horizontal)
                        data.setPixelColour (i, j, c);
                    else
                        data.setPixelColour (j, i, c);
                }
            }
        };

        for (int p = 0; p < 2; ++p)
        {
            pass (true);
            pass (false);
        }
    }

    struct Shard
    {
        Point<float> start, end;
        float width, alpha;
        bool dark;
    };

    std::vector<Shard> makeShards (float w, float h)
    {
        Random rng (20260924);
        std::vector<Shard> shards;
        const Point<float> focus (w * 0.80f, h * 0.24f);
        const float reach = jmax (w, h);

        for (int i = 0; i < 22; ++i)
        {
            // Mostly sweeping down-left and up-right, like debris flying out of the focus
            const float base = (i % 3 == 0) ? 0.35f : (i % 3 == 1 ? 3.6f : 2.2f);
            const float angle = base + (rng.nextFloat() - 0.5f) * 1.1f;
            const Point<float> dir (std::cos (angle), std::sin (angle));
            const float length = reach * (0.18f + 0.55f * rng.nextFloat());
            const float offset = 10.0f + 50.0f * rng.nextFloat();
            shards.push_back ({ focus + dir * offset, focus + dir * (offset + length),
                                4.0f + 36.0f * rng.nextFloat() * rng.nextFloat(),
                                0.18f + 0.45f * rng.nextFloat(), rng.nextFloat() < 0.55f });
        }

        return shards;
    }

    Path shardPath (const Shard& s, float widthScale)
    {
        const auto dir = (s.end - s.start) / jmax (1.0f, s.start.getDistanceFrom (s.end));
        const Point<float> perp (-dir.y, dir.x);
        const auto belly = s.start + (s.end - s.start) * 0.3f;
        const float half = s.width * widthScale * 0.5f;

        Path p;
        p.startNewSubPath (s.start);
        p.quadraticTo (belly + perp * half * 1.3f, s.end);
        p.quadraticTo (belly - perp * half * 1.3f, s.start);
        p.closeSubPath();
        return p;
    }
} // namespace

Image renderBackdrop (int width, int height, float scale)
{
    const int w = jmax (1, roundToInt ((float) width * scale));
    const int h = jmax (1, roundToInt ((float) height * scale));
    const float fw = (float) w, fh = (float) h;
    const auto shards = makeShards (fw, fh);

    Image image (Image::ARGB, w, h, true, SoftwareImageType());
    Graphics g (image);

    g.setGradientFill (ColourGradient (Colour (0xfffafcfd), 0.0f, 0.0f, Colour (0xffe4eaf1), 0.0f, fh, false));
    g.fillAll();

    // Soft layer at quarter resolution, blurred: sweeping band + shard glow
    {
        const int sw = w / 4 + 1, sh = h / 4 + 1;
        Image soft (Image::ARGB, sw, sh, true, SoftwareImageType());
        {
            Graphics sg (soft);
            sg.addTransform (AffineTransform::scale (0.25f));

            Path swoosh;
            swoosh.startNewSubPath (-0.1f * fw, 0.98f * fh);
            swoosh.cubicTo (0.10f * fw, 0.62f * fh, 0.30f * fw, 0.50f * fh, 0.62f * fw, 0.56f * fh);
            sg.setGradientFill (ColourGradient (Colour (0xff6f8fb5).withAlpha (0.55f), 0.0f, fh, Colour (0xff9fbad6).withAlpha (0.0f), 0.62f * fw, 0.5f * fh, false));
            sg.strokePath (swoosh, PathStrokeType (0.11f * fh, PathStrokeType::curved, PathStrokeType::rounded));

            Path swoosh2;
            swoosh2.startNewSubPath (-0.1f * fw, 0.72f * fh);
            swoosh2.cubicTo (0.12f * fw, 0.48f * fh, 0.25f * fw, 0.44f * fh, 0.42f * fw, 0.47f * fh);
            sg.setGradientFill (ColourGradient (Colour (0xff1b2a40).withAlpha (0.35f), 0.0f, 0.72f * fh, Colour (0xff6f8fb5).withAlpha (0.0f), 0.42f * fw, 0.47f * fh, false));
            sg.strokePath (swoosh2, PathStrokeType (0.035f * fh, PathStrokeType::curved, PathStrokeType::rounded));

            for (const auto& s : shards)
            {
                const auto c = s.dark ? Colour (0xff1b2a40) : Colour (0xff6f8fb5);
                sg.setGradientFill (ColourGradient (c.withAlpha (s.alpha), s.start, c.withAlpha (0.0f), s.end, false));
                sg.fillPath (shardPath (s, 2.2f));
            }
        }

        blur (soft, 3);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (soft, Rectangle<float> (0.0f, 0.0f, (float) sw * 4.0f, (float) sh * 4.0f));
    }

    // Crisp layer: the blades themselves, with fine highlight edges
    for (const auto& s : shards)
    {
        const auto c = s.dark ? Colour (0xff1b2a40) : Colour (0xff47698f);
        g.setGradientFill (ColourGradient (c.withAlpha (s.alpha * 0.9f), s.start, c.withAlpha (0.0f), s.end, false));
        g.fillPath (shardPath (s, 0.55f));

        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.75f), s.start, Colours::white.withAlpha (0.0f), s.end, false));
        g.drawLine ({ s.start, s.end }, 0.8f * scale);
    }

    // Sparse hairline streaks
    Random rng (7);

    for (int i = 0; i < 9; ++i)
    {
        const Point<float> a (fw * rng.nextFloat(), fh * (0.1f + 0.3f * rng.nextFloat()));
        const Point<float> b = a + Point<float> (fw * (0.2f + 0.3f * rng.nextFloat()), -fh * 0.06f * rng.nextFloat());
        g.setGradientFill (ColourGradient (Colour (0xff47698f).withAlpha (0.0f), a, Colour (0xff47698f).withAlpha (0.35f), b, false));
        g.drawLine ({ a, b }, 0.7f * scale);
    }

    // Tape damage: a few horizontal slices knocked sideways, with thin chroma-split edges
    {
        Random tear (1312);
        const Image copy = image.createCopy();

        for (int i = 0; i < 9; ++i)
        {
            const int y = (int) (fh * (0.05f + 0.9f * tear.nextFloat()));
            const int bandH = jmax (1, (int) ((1.0f + 9.0f * tear.nextFloat() * tear.nextFloat()) * scale));
            const int shift = (int) ((tear.nextFloat() - 0.5f) * 90.0f * scale);
            const auto band = Rectangle<int> (0, y, w, bandH).getIntersection (image.getBounds());

            if (band.isEmpty())
                continue;

            g.drawImage (copy, band.getX() + shift, band.getY(), band.getWidth(), band.getHeight(),
                         band.getX(), band.getY(), band.getWidth(), band.getHeight());

            const bool warm = i % 3 == 0;
            g.setColour ((warm ? Colour (0xfff07f3c) : Colour (0xff3b76b3)).withAlpha (warm ? 0.22f : 0.16f));
            g.fillRect (Rectangle<float> ((float) jmax (0, shift), (float) y, fw * (0.3f + 0.6f * tear.nextFloat()), 1.0f * scale));
        }

        // faint scanlines
        g.setColour (Colour (0xff1b2a40).withAlpha (0.018f));

        for (float y = 0.0f; y < fh; y += 3.0f * scale)
            g.fillRect (Rectangle<float> (0.0f, y, fw, 1.0f));
    }

    // Film grain
    {
        Image::BitmapData data (image, Image::BitmapData::readWrite);
        Random grain (99);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto c = data.getPixelColour (x, y);
                const float n = (grain.nextFloat() - 0.5f) * 0.035f;
                data.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, c.getFloatRed() + n),
                                                                  jlimit (0.0f, 1.0f, c.getFloatGreen() + n),
                                                                  jlimit (0.0f, 1.0f, c.getFloatBlue() + n), 1.0f));
            }
        }
    }

    return image;
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (Label::textColourId, colours::text);

    setColour (ComboBox::backgroundColourId, colours::panel);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::outline);
    setColour (ComboBox::arrowColourId, colours::textDim);

    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::ink);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (PopupMenu::headerTextColourId, colours::textDim);

    setColour (TextButton::buttonColourId, colours::panel);
    setColour (TextButton::buttonOnColourId, colours::ink);
    setColour (TextButton::textColourOffId, colours::textDim);
    setColour (TextButton::textColourOnId, Colours::white);

    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (TextEditor::backgroundColourId, Colours::white);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::outlineColourId, colours::outline);
    setColour (TextEditor::focusedOutlineColourId, colours::accent);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.25f));
    setColour (TextEditor::highlightedTextColourId, colours::text);
    setColour (CaretComponent::caretColourId, colours::ink);

    setColour (TooltipWindow::backgroundColourId, colours::panel);
    setColour (TooltipWindow::textColourId, colours::text);
    setColour (TooltipWindow::outlineColourId, colours::outline);
}

void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto accentColour = slider.findColour (Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();
    const bool hot = slider.isMouseOverOrDragging();

    // Scale ticks outside the ring: a fine instrument dial
    for (int i = 0; i <= 20; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 20.0f;
        const bool major = i % 5 == 0;
        const auto p0 = centre.getPointOnCircumference (radius - (major ? 4.0f : 2.5f), a);
        const auto p1 = centre.getPointOnCircumference (radius, a);
        g.setColour (colours::textFaint.withAlpha (major ? 0.9f : 0.5f));
        g.drawLine ({ p0, p1 }, 0.8f);
    }

    const float ring = radius - 7.0f;
    Path track;
    track.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, PathStrokeType (1.0f));

    const bool bipolar = slider.getProperties()["bipolar"];
    const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (valueAngle - fromAngle) > 0.001f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, jmin (fromAngle, valueAngle), jmax (fromAngle, valueAngle), true);
        g.setColour (enabled ? accentColour : colours::textFaint);
        g.strokePath (value, PathStrokeType (hot ? 2.6f : 2.0f));
    }

    // Modulation: an orange arc from the knob value to where modulation is pushing it right now
    const auto& props = slider.getProperties();

    if (props.contains ("modPos"))
    {
        const float modPos = jlimit (0.0f, 1.0f, (float) props["modPos"]);
        const float modAngle = startAngle + modPos * (endAngle - startAngle);
        const float outer = ring + 4.0f;
        Path modArc;
        modArc.addCentredArc (centre.x, centre.y, outer, outer, 0.0f, jmin (valueAngle, modAngle), jmax (valueAngle, modAngle), true);
        g.setColour (colours::accent);
        g.strokePath (modArc, PathStrokeType (2.0f));
        g.fillEllipse (Rectangle<float> (4.5f, 4.5f).withCentre (centre.getPointOnCircumference (outer, modAngle)));
    }
    else if ((bool) props["modRouted"])
    {
        const float outer = ring + 4.0f;
        g.setColour (colours::accent.withAlpha (0.6f));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (centre.getPointOnCircumference (outer, valueAngle)));
    }

    // Glass hub with a hairline pointer
    const float hub = ring - 7.0f;
    const auto hubArea = Rectangle<float> (hub * 2.0f, hub * 2.0f).withCentre (centre);
    g.setGradientFill (ColourGradient (Colours::white, hubArea.getTopLeft(), colours::panelRaised.darker (hot ? 0.04f : 0.0f), hubArea.getBottomRight(), false));
    g.fillEllipse (hubArea);
    g.setColour (colours::outline);
    g.drawEllipse (hubArea, 0.8f);

    const auto tip = centre.getPointOnCircumference (hub - 2.0f, valueAngle);
    const auto inner = centre.getPointOnCircumference (hub * 0.25f, valueAngle);
    g.setColour (enabled ? colours::ink : colours::textFaint);
    g.drawLine ({ inner, tip }, 1.2f);
    g.setColour (enabled ? accentColour : colours::textFaint);
    g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (centre.getPointOnCircumference (ring, valueAngle)));
}

void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                    Slider::SliderStyle style, Slider& slider)
{
    if (style != Slider::LinearBar && style != Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    // Bipolar amount bar: hairline well, centre tick, orange fill from the centre
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (1.0f, jmax (1.0f, (float) height * 0.5f - 5.0f));
    const float centreX = bounds.getCentreX();
    const bool hot = slider.isMouseOverOrDragging();

    g.setColour (colours::well.withAlpha (0.9f));
    g.fillRect (bounds);
    g.setColour (hot ? colours::textFaint : colours::outline);
    g.drawRect (bounds, 1.0f);

    const float pos = jlimit (bounds.getX(), bounds.getRight(), sliderPos);
    g.setColour (slider.isEnabled() ? colours::accent : colours::textFaint);
    g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (centreX, pos), bounds.getY() + 2.0f, jmax (centreX, pos), bounds.getBottom() - 2.0f));

    g.setColour (colours::ink.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (centreX - 0.5f, bounds.getY() - 2.0f, 1.0f, bounds.getHeight() + 4.0f));
}

void LookAndFeel::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& sectionName)
{
    auto r = area.toFloat().reduced (10.0f, 0.0f);
    ::hl::gui::drawLabel (g, sectionName, r.withTrimmedTop (4.0f), Justification::bottomLeft, colours::accent, 9.5f);
    g.setColour (colours::outline);
    g.fillRect (r.removeFromBottom (1.0f));
}

void LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& button, bool highlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();
    const auto accentColour = button.findColour (ToggleButton::tickColourId);
    const String style = button.getProperties()["style"].toString();

    if (style == "power")
    {
        const float d = jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
        const auto r = Rectangle<float> (d, d).withCentre (bounds.getCentre());
        g.setColour (on ? accentColour : colours::textFaint.withAlpha (highlighted ? 1.0f : 0.7f));

        if (on)
            g.fillEllipse (r.withSizeKeepingCentre (d * 0.28f, d * 0.28f));

        g.drawEllipse (r.reduced (1.0f), 1.0f);
        return;
    }

    // Default: squared hairline chip; filled ink with white text when on
    g.setColour (on ? accentColour : colours::panel.withAlpha (highlighted ? 1.0f : 0.8f));
    g.fillRect (bounds);
    g.setColour (on ? accentColour : colours::outline);
    g.drawRect (bounds, 1.0f);

    g.setColour (on ? Colours::white : colours::textDim);
    g.setFont (font (jmin (11.0f, bounds.getHeight() * 0.5f), true).withExtraKerningFactor (0.12f));
    g.drawText (button.getButtonText().toUpperCase(), bounds, Justification::centred, false);
}

void LookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    g.setColour (on ? colours::ink : colours::panel.withAlpha (down ? 1.0f : (highlighted ? 0.95f : 0.75f)));
    g.fillRect (bounds);
    g.setColour (on ? colours::ink : (highlighted ? colours::textFaint : colours::outline));
    g.drawRect (bounds, 1.0f);
}

void LookAndFeel::drawButtonText (Graphics& g, TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? Colours::white : (button.isEnabled() ? colours::text : colours::textFaint));
    g.drawText (button.getButtonText().toUpperCase(), button.getLocalBounds(), Justification::centred, false);
}

Font LookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return font (jmin (11.5f, (float) buttonHeight * 0.42f), true).withExtraKerningFactor (0.14f);
}

void LookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    const auto bounds = Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (colours::panel.withAlpha (box.isMouseOver (true) ? 1.0f : 0.8f));
    g.fillRect (bounds);
    g.setColour (box.isMouseOver (true) ? colours::textFaint : colours::outline);
    g.drawRect (bounds, 1.0f);

    const float s = 4.0f;
    const auto c = Point<float> ((float) width - 12.0f, (float) height * 0.5f);
    Path chevron;
    chevron.startNewSubPath (c.x - s, c.y - s * 0.5f);
    chevron.lineTo (c.x, c.y + s * 0.5f);
    chevron.lineTo (c.x + s, c.y - s * 0.5f);
    g.setColour (colours::textDim);
    g.strokePath (chevron, PathStrokeType (1.0f));
}

Font LookAndFeel::getComboBoxFont (ComboBox& box)
{
    return font (jmin (13.0f, (float) box.getHeight() * 0.52f));
}

void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (7, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawRect (0, 0, width, height, 1);
}

Font LookAndFeel::getPopupMenuFont()
{
    return font (13.5f);
}

Font LookAndFeel::getLabelFont (Label& label)
{
    return font (jmin (13.0f, (float) label.getHeight() * 0.7f));
}

Rectangle<int> LookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    const int w = jmin (320, (int) GlyphArrangement::getStringWidth (font (12.5f), tipText) + 24);
    const int h = w >= 320 ? 44 : 26;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (colours::accent);
    g.fillRect (0, 0, 2, height);
    g.setColour (colours::text);
    g.setFont (font (12.5f));
    g.drawFittedText (text, Rectangle<int> (width, height).reduced (10, 4), Justification::centredLeft, 2);
}

Typeface::Ptr LookAndFeel::getTypefaceForFont (const Font& f)
{
    // Anything that asks for the default font (menus, labels, host dialogs) gets Oxanium too
    if (f.getTypefaceName() == Font::getDefaultSansSerifFontName())
        return f.isBold() ? typefaces->semiBold : typefaces->regular;

    return LookAndFeel_V4::getTypefaceForFont (f);
}

void LookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool isMouseOver, bool)
{
    g.setColour (isMouseOver ? colours::textDim : colours::textFaint);

    for (int i = 1; i <= 3; ++i)
        g.drawLine ((float) w - (float) i * 4.0f, (float) h - 1.0f, (float) w - 1.0f, (float) h - (float) i * 4.0f, 0.8f);
}

} // namespace hl::gui
