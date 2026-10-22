#include "nam_import.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nam_import
{
namespace
{

// ---------------------------------------------------------------- reading the .nam
// Just enough JSON to walk a .nam: find keys in objects, skip whatever is not wanted,
// read one array of numbers. The text must be NUL-terminated (strtod reads to a
// delimiter). Strings keep their escapes: only keys and the architecture are compared.
struct Cur
{
    const char* p;
    const char* e;
};

void Ws(Cur& c)
{
    while(c.p < c.e && (*c.p == ' ' || *c.p == '\n' || *c.p == '\r' || *c.p == '\t'))
        c.p++;
}

bool Lit(Cur& c, char ch)
{
    Ws(c);
    if(c.p < c.e && *c.p == ch)
    {
        c.p++;
        return true;
    }
    return false;
}

bool Str(Cur& c, const char** s, size_t* n)
{
    Ws(c);
    if(c.p >= c.e || *c.p != '"')
        return false;
    const char* b = ++c.p;
    while(c.p < c.e && *c.p != '"')
        c.p += (*c.p == '\\') ? 2 : 1;
    if(c.p >= c.e)
        return false;
    *s = b;
    *n = (size_t)(c.p - b);
    c.p++;
    return true;
}

bool Skip(Cur& c, int depth)
{
    Ws(c);
    if(c.p >= c.e || depth > 64)
        return false;
    const char ch = *c.p;
    if(ch == '"')
    {
        const char* s;
        size_t      n;
        return Str(c, &s, &n);
    }
    if(ch == '{' || ch == '[')
    {
        const char close = ch == '{' ? '}' : ']';
        c.p++;
        if(Lit(c, close))
            return true;
        while(true)
        {
            const char* s;
            size_t      n;
            if(ch == '{' && !(Str(c, &s, &n) && Lit(c, ':')))
                return false;
            if(!Skip(c, depth + 1))
                return false;
            if(Lit(c, ','))
                continue;
            return Lit(c, close);
        }
    }
    const char* b = c.p;  // a number, true, false or null: up to the next delimiter
    while(c.p < c.e && !strchr(",}] \t\r\n", *c.p))
        c.p++;
    return c.p > b;
}

// In the object at c, the value of key; v is left at it.
bool Find(Cur c, const char* key, Cur* v)
{
    if(!Lit(c, '{') || Lit(c, '}'))
        return false;
    const size_t kn = strlen(key);
    while(true)
    {
        const char* s;
        size_t      n;
        if(!Str(c, &s, &n) || !Lit(c, ':'))
            return false;
        Ws(c);
        if(n == kn && memcmp(s, key, n) == 0)
        {
            *v = c;
            return true;
        }
        if(!Skip(c, 0) || !Lit(c, ','))
            return false;
    }
}

bool Number(Cur c, double* out)
{
    Ws(c);
    char* end = nullptr;
    *out      = strtod(c.p, &end);
    return end > c.p && end <= c.e;
}

bool StrIs(Cur c, const char* want)
{
    const char* s;
    size_t      n;
    return Str(c, &s, &n) && n == strlen(want) && memcmp(s, want, n) == 0;
}

// The weights array: as Python reads it (json, float) and packs it (float32).
bool Weights(Cur model, float* out, size_t cap, size_t* count)
{
    Cur c;
    if(!Find(model, "weights", &c) || !Lit(c, '['))
        return false;
    size_t k = 0;
    if(!Lit(c, ']'))
        while(true)
        {
            Ws(c);
            char*        end = nullptr;
            const double v   = strtod(c.p, &end);
            if(end == c.p || end > c.e)
                return false;
            c.p = end;
            if(k < cap)
                out[k] = (float)v;
            k++;
            if(Lit(c, ','))
                continue;
            if(Lit(c, ']'))
                break;
            return false;
        }
    *count = k;
    return true;
}

// CRC32, IEEE 802.3: zlib's, as tools/a2nb.py packs and capture_store checks.
uint32_t Crc32(const uint8_t* d, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i = 0; i < n; i++)
    {
        crc ^= d[i];
        for(int b = 0; b < 8; b++)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

void Put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
void Put32(uint8_t* p, uint32_t v)
{
    for(int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

bool Allowed(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == ' '
           || ch == '_' || ch == '-';
}

bool Space(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'; }

constexpr double kPi = 3.141592653589793;  // math.pi; M_PI is not in strict C++

// level_captures.a_weighting() at 48 kHz, its second-order sections as scipy gives
// them: b0 b1 b2 a0 a1 a2.
constexpr double kSos[3][6] = {
    {0.23430179229951345, 0.4686035845990269, 0.23430179229951345, 1.0, -0.22455845805977914, 0.012606625271546396},
    {1.0, -2.0, 1.0, 1.0, -1.8938704947230707, 0.8951597690946617},
    {1.0, -2.0, 1.0, 1.0, -1.9946144559930215, 0.9946217070140843},
};

} // namespace

bool ExtractLite(const char* json, size_t len, float* weights, size_t cap, size_t* count, const char** why)
{
    const Cur doc = {json, json + len};
    Cur       arch, model = doc;
    *count = 0;
    *why   = "bad file";
    if(!Find(doc, "architecture", &arch))
        return false;
    if(StrIs(arch, "SlimmableContainer"))
    {
        Cur cfg, subs;
        if(!Find(doc, "config", &cfg) || !Find(cfg, "submodels", &subs) || !Lit(subs, '['))
            return false;
        // The submodel whose max_value is nearest 0.5; the first of equals, as min() picks.
        double best = 1e30;
        bool   any  = false;
        if(!Lit(subs, ']'))
            while(true)
            {
                Ws(subs);
                const Cur sub = subs;
                Cur       mv, m;
                double    v = 1.0;
                if(Find(sub, "max_value", &mv) && !Number(mv, &v))
                    return false;
                if(fabs(v - 0.5) < best && Find(sub, "model", &m))
                {
                    best  = fabs(v - 0.5);
                    model = m;
                    any   = true;
                }
                if(!Skip(subs, 0))
                    return false;
                if(Lit(subs, ','))
                    continue;
                if(Lit(subs, ']'))
                    break;
                return false;
            }
        if(!any)
            return false;
    }
    else if(!StrIs(arch, "WaveNet"))
    {
        *why = "not A2";
        return false;
    }
    if(!Weights(model, weights, cap, count))
        return false;
    if(*count != kWeights)
    {
        *why = "not A2";
        return false;
    }
    *why = "ok";
    return true;
}

void DefaultName(const char* filename, char out[kNameLen])
{
    // The stem: up to the last '.', unless that is the first character (pathlib's rule).
    const char* dot = strrchr(filename, '.');
    size_t      n   = (dot && dot > filename) ? (size_t)(dot - filename) : strlen(filename);
    const char* s   = filename;
    // A leading [TAG] and the whitespace after it.
    if(n > 0 && s[0] == '[')
    {
        const char* close = (const char*)memchr(s, ']', n);
        if(close)
        {
            n -= (size_t)(close + 1 - s);
            s = close + 1;
            while(n > 0 && Space(*s))
                s++, n--;
        }
    }
    // Runs of anything else as one space, then runs of spaces as one, trimmed.
    char   buf[256];
    size_t k = 0;
    for(size_t i = 0; i < n && k < sizeof(buf) - 1; i++)
    {
        const char ch = Allowed(s[i]) ? s[i] : ' ';
        if(ch == ' ' && (k == 0 || buf[k - 1] == ' '))
            continue;
        buf[k++] = ch;
    }
    while(k > 0 && buf[k - 1] == ' ')
        k--;
    // At most 11 characters, trimmed again.
    if(k > kNameLen - 1)
        k = kNameLen - 1;
    while(k > 0 && buf[k - 1] == ' ')
        k--;
    if(k == 0)
    {
        strcpy(out, "CAPTURE");
        return;
    }
    memcpy(out, buf, k);
    out[k] = '\0';
}

void FileName(const char* name, char* out, size_t cap)
{
    size_t k = 0;
    for(; name[k] && k + 6 < cap; k++)
        out[k] = (Allowed(name[k]) && name[k] != ' ') ? name[k] : '_';
    strcpy(out + k, ".a2nb");
}

void Pack(const char* name, float gain, const float* weights, uint8_t* out)
{
    memset(out, 0, kHeader);
    memcpy(out, "A2NB", 4);
    Put16(out + 4, 1);
    Put16(out + 6, 0);
    Put32(out + 8, (uint32_t)kWeights);
    uint32_t g;
    memcpy(&g, &gain, 4);
    Put32(out + 12, g);
    strncpy((char*)out + 16, name, kNameLen - 1);
    for(size_t i = 0; i < kWeights; i++)
    {
        uint32_t w;
        memcpy(&w, &weights[i], 4);
        Put32(out + kHeader + 4 * i, w);
    }
    Put32(out + 28, Crc32(out + kHeader, kWeights * sizeof(float)));
}

void MakePhrase(float* out, double* table)
{
    // test_signal.make_synth(), step for step, so the levels match level_captures.py.
    static const int    kSemis[8] = {0, 0, 3, 5, 0, -2, 0, 7};
    static const double kVel[8]   = {1.00, 0.62, 0.85, 1.00, 0.70, 0.92, 0.55, 1.00};
    const double        sr = 48000.0, root = 110.0, note_len = 0.5, gate = 0.42;
    const double        top = root * pow(2.0, 7 / 12.0);
    int                 harmonics = (int)((sr / 2) / top) - 1;
    harmonics = harmonics < 160 ? harmonics : 160;
    harmonics = harmonics > 1 ? harmonics : 1;

    constexpr int kTable = (int)kTableLen;
    for(int i = 0; i < kTable; i++)
        table[i] = 0.0;
    for(int n = 1; n <= harmonics; n++)
    {
        const double amp = 1.0 / n;
        for(int i = 0; i < kTable; i++)
            table[i] += amp * sin(2.0 * kPi * n * i / kTable);
    }
    double peak = 0.0;
    for(int i = 0; i < kTable; i++)
        peak = fabs(table[i]) > peak ? fabs(table[i]) : peak;
    peak = peak > 0.0 ? peak : 1.0;
    for(int i = 0; i < kTable; i++)
        table[i] = table[i] / peak;
    auto read = [&](double phase) {
        const double x  = phase * kTable;
        const int    i0 = (int)x % kTable, i1 = (i0 + 1) % kTable;
        const double f  = x - (int)x;
        return table[i0] * (1.0 - f) + table[i1] * f;
    };

    for(size_t i = 0; i < kPhraseLen; i++)
        out[i] = 0.0f;
    const double detune = 7.0 / 1200.0;
    size_t       pos    = 0;
    for(int k = 0; k < 8; k++)
    {
        const double f = root * pow(2.0, kSemis[k] / 12.0), f1 = f * pow(2.0, -detune), f2 = f * pow(2.0, detune);
        double       p1 = 0.0, p2 = 0.0;
        const int    n = (int)(gate * sr), atk = (int)(0.004 * sr), dec = (int)(0.12 * sr), rel = (int)(0.08 * sr);
        const double sus = 0.7;
        for(int i = 0; i < n && pos + i < kPhraseLen; i++)
        {
            double env;
            if(i < atk)
                env = (double)i / atk;
            else if(i < atk + dec)
                env = 1.0 - (1.0 - sus) * ((double)(i - atk) / dec);
            else if(i > n - rel)
                env = sus * fmax(0.0, (double)(n - i) / rel);
            else
                env = sus;
            out[pos + i] = (float)(0.5 * (read(p1) + read(p2)) * env * kVel[k]);
            p1 = fmod(p1 + f1 / sr, 1.0);
            p2 = fmod(p2 + f2 / sr, 1.0);
        }
        pos += (size_t)(note_len * sr);
    }
    // scale_peak(sig, 0.5)
    double m = 0.0;
    for(size_t i = 0; i < kPhraseLen; i++)
        m = fabs((double)out[i]) > m ? fabs((double)out[i]) : m;
    if(m > 0.0)
    {
        const double g = 0.5 / m;
        for(size_t i = 0; i < kPhraseLen; i++)
            out[i] = (float)((double)out[i] * g);
    }
}

void ALevel::Add(const float* y, size_t n)
{
    for(size_t i = 0; i < n; i++)
    {
        double x = y[i];
        for(int s = 0; s < 3; s++)  // direct form II transposed, as scipy's sosfilt
        {
            const double* c = kSos[s];
            const double  v = c[0] * x + z_[s][0];
            z_[s][0]        = c[1] * x - c[4] * v + z_[s][1];
            z_[s][1]        = c[2] * x - c[5] * v;
            x               = v;
        }
        sum_ += x * x;
    }
    n_ += n;
}

double ALevel::Db() const { return 10.0 * log10((n_ ? sum_ / (double)n_ : 0.0) + 1e-20); }

float GainFor(double level_db) { return (float)pow(10.0, ((double)kTargetDb - level_db) / 20.0); }

} // namespace nam_import
