/**
 * nam_import_check — the module's .nam import (src/nam_import.cpp) run on the desktop,
 * with the real A2 engine for the levelling, so its .a2nb files can be compared with
 * what tools/nam_to_a2nb.py and tools/level_captures.py write for the same downloads.
 *
 *   nam_import_check <out dir> <file.nam>...
 *
 * Build, from tools/ (gitignored):
 *   c++ -std=c++17 -O2 -I.. -o nam_import_check nam_import_check.cpp ../src/nam_import.cpp
 */

#define NAM_A2_HOT_DATA
#define NAM_A2_STATE_DATA
#define NAM_A2_HOT_STATE_DATA
#include "nam/nam_a2_runtime.h"
#include "src/nam_import.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if(argc < 3)
    {
        fprintf(stderr, "usage: %s <out dir> <file.nam>...\n", argv[0]);
        return 2;
    }
    static float  phrase[nam_import::kPhraseLen];
    static double table[nam_import::kTableLen];
    nam_import::MakePhrase(phrase, table);
    static nam_a2_daisy::A2Player player;
    int                           bad = 0;
    for(int a = 2; a < argc; a++)
    {
        const std::string path = argv[a];
        FILE*             f    = fopen(path.c_str(), "rb");
        if(!f)
        {
            printf("%s: cannot open\n", path.c_str());
            bad++;
            continue;
        }
        std::vector<char> text;
        fseek(f, 0, SEEK_END);
        text.resize((size_t)ftell(f) + 1);
        fseek(f, 0, SEEK_SET);
        const size_t len = fread(text.data(), 1, text.size() - 1, f);
        fclose(f);
        text[len] = '\0';

        float       w[nam_import::kWeights];
        size_t      count = 0;
        const char* why   = "";
        const std::string base = path.substr(path.find_last_of('/') + 1);
        if(!nam_import::ExtractLite(text.data(), len, w, nam_import::kWeights, &count, &why))
        {
            printf("%s: refused, %s (%zu weights)\n", base.c_str(), why, count);
            bad++;
            continue;
        }
        char name[nam_import::kNameLen], file[256];
        nam_import::DefaultName(base.c_str(), name);
        nam_import::FileName(name, file, sizeof(file));

        player.load_weights(w, nam_import::kWeights);
        nam_import::ALevel lv;
        float              out[nam_a2_daisy::kBlockSize];
        for(size_t i = 0; i < nam_import::kPhraseLen; i += nam_a2_daisy::kBlockSize)
        {
            player.process_block_48(phrase + i, out);
            lv.Add(out, nam_a2_daisy::kBlockSize);
        }
        const float gain = nam_import::GainFor(lv.Db());

        static uint8_t blob[nam_import::kFileBytes];
        nam_import::Pack(name, gain, w, blob);
        const std::string dst = std::string(argv[1]) + "/" + file;
        FILE*             o   = fopen(dst.c_str(), "wb");
        fwrite(blob, 1, sizeof(blob), o);
        fclose(o);
        printf("%s -> %s  name \"%s\"  level %.2f dB  gain %.4f\n", base.c_str(), file, name, lv.Db(), gain);
    }
    return bad ? 1 : 0;
}
