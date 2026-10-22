/**
 * nam_import — a downloaded .nam turned into an .a2nb on the module, so a user can copy
 * TONE3000 downloads straight onto the card with no script to run.
 *
 * The same steps as the desktop tools, so the file the module writes is the file they
 * would have written: tools/nam_to_a2nb.py (the A2-Lite submodel's weights, the display
 * name from the filename, the file name from the display name) and
 * tools/level_captures.py (the gain that puts the capture at -27 dB A-weighted on the
 * not-amps' test phrase). Everything here is plain C++ with no card or hardware, so
 * tools/nam_import_check.cpp runs it on the desktop against the Python tools; the card
 * side is captures::ImportNam in capture_store.cpp.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace nam_import
{

constexpr size_t kWeights   = 1871;   // A2-Lite at 3 channels; tools/a2nb.py EXPECTED_WEIGHTS
constexpr size_t kNameLen   = 12;     // the header's name field, NUL-padded; 11 characters
constexpr size_t kHeader    = 32;
constexpr size_t kFileBytes = kHeader + kWeights * sizeof(float);
constexpr float  kTargetDb  = -27.0f; // level_captures.py TARGET_DB
constexpr size_t kPhraseLen = 192000; // test_signal.make_synth: eight notes of 0.5 s at 48 kHz
constexpr size_t kTableLen  = 2048;   // its wavetable

/** The A2-Lite weights in a .nam file's text (NUL-terminated at json[len]). An A2
 *  download (SlimmableContainer) holds both sizes; the submodel whose max_value is
 *  nearest 0.5 is A2-Lite, as nam_to_a2nb.py takes it. *count is how many weights the
 *  file had, so the caller can refuse anything but kWeights. On failure *why is a short
 *  reason for the panel ("not A2", "bad file"). */
bool ExtractLite(const char* json, size_t len, float* weights, size_t cap, size_t* count, const char** why);

/** nam_to_a2nb.default_name: the filename's stem less a leading [TAG], anything but
 *  letters, digits, space, '_' and '-' turned to spaces, at most 11 characters. */
void DefaultName(const char* filename, char out[kNameLen]);

/** nam_to_a2nb's output file name for a display name: "<name, other chars '_'>.a2nb". */
void FileName(const char* name, char* out, size_t cap);

/** The .a2nb bytes (tools/a2nb.py pack) into out[kFileBytes]. */
void Pack(const char* name, float gain, const float* weights, uint8_t* out);

/** The levelling phrase, test_signal.make_synth() scaled to a peak of 0.5, into
 *  out[kPhraseLen]; table[kTableLen] is scratch for its wavetable (16 KB, so the caller
 *  chooses the memory: on the module, not DTCMRAM). */
void MakePhrase(float* out, double* table);

/** level_captures.py's measure: A-weighted (IEC 61672, as its second-order sections at
 *  48 kHz) mean square, in dB, of everything Add()ed. */
class ALevel
{
  public:
    void   Add(const float* y, size_t n);
    double Db() const;

  private:
    double z_[3][2] = {};
    double sum_     = 0.0;
    size_t n_       = 0;
};

/** The gain that takes a capture measured at level_db (gain 1) to kTargetDb. */
float GainFor(double level_db);

} // namespace nam_import
