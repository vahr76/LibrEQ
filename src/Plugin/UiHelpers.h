#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>
#include <cmath>

namespace libreq {

// Shared by every UI object of the plugin; released with the last editor.
class UiFonts {
public:
    UiFonts()
        : regularFace(juce::Typeface::createSystemTypefaceFor(BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize)),
          boldFace(juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize)) {}

    juce::Font regular(float height) const { return juce::Font(juce::FontOptions().withTypeface(regularFace).withHeight(height)); }
    juce::Font bold(float height) const { return juce::Font(juce::FontOptions().withTypeface(boldFace).withHeight(height)); }
    bool loaded() const { return regularFace != nullptr && boldFace != nullptr; }

private:
    juce::Typeface::Ptr regularFace, boldFace;
};

// Nearest note and its deviation in cents, for example "A4" or "C#3 +12c".
inline juce::String noteLabel(double frequency) {
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
    const int nearest = juce::roundToInt(midi);
    const int cents = juce::roundToInt((midi - nearest) * 100.0);
    const int octave = static_cast<int>(std::floor(nearest / 12.0)) - 1;
    juce::String label = juce::String(names[((nearest % 12) + 12) % 12]) + juce::String(octave);
    if (cents != 0)
        label << " " << (cents > 0 ? "+" : "") << cents << "c";
    return label;
}

} // namespace libreq
