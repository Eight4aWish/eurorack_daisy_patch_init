// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// seed_check: a bench check for a Daisy Seed taken off a board (desoldered), on USB alone.
// Flash it through the ROM bootloader (BOOT held, RESET tapped), open the USB serial port,
// and it reports:
//
//   - the board version and system clock;
//   - SDRAM: all 64 MB written and read back twice, an address-unique pattern and its
//     inverse, so a dead chip, a stuck data line or a broken address line all show;
//   - QSPI flash: two 4 KB sectors near the end, each erased, written, read back and erased
//     again, five runs (nothing else on the chip is touched);
//   - the audio codec: the audio callback's rate over one second;
//   - the header pins D0-D30: with nothing attached, each should read high with its pull-up
//     and low with its pull-down. A pin stuck low is a short to ground, stuck high a short
//     to 3V3, and a pin pulled low by its neighbour being driven low is a solder bridge.
//
// The user LED blinks twice a second throughout: a running program, whatever the port shows.
// The report repeats every 3 s, so a terminal opened late still sees it. Any byte sent to
// the port reboots it into the ROM bootloader, ready for the next flash.

#include "daisy_seed.h"
#include <stdio.h>
#include <string.h>

using namespace daisy;

static DaisySeed hw;

static constexpr uint32_t kSdramWords = 16u * 1024u * 1024u;   // 64 MB
static uint32_t DSY_SDRAM_BSS sdram[kSdramWords];

static volatile uint32_t audioBlocks = 0;
static void Audio(AudioHandle::InputBuffer, AudioHandle::OutputBuffer out, size_t n)
{
    for(size_t i = 0; i < n; i++)
        out[0][i] = out[1][i] = 0.0f;
    audioBlocks = audioBlocks + 1;
}

static const Pin kPins[] = {
    seed::D0,  seed::D1,  seed::D2,  seed::D3,  seed::D4,  seed::D5,  seed::D6,  seed::D7,
    seed::D8,  seed::D9,  seed::D10, seed::D11, seed::D12, seed::D13, seed::D14, seed::D15,
    seed::D16, seed::D17, seed::D18, seed::D19, seed::D20, seed::D21, seed::D22, seed::D23,
    seed::D24, seed::D25, seed::D26, seed::D27, seed::D28, seed::D29, seed::D30};
static constexpr int kNumPins = sizeof(kPins) / sizeof(kPins[0]);
static GPIO          gpio[kNumPins];

static volatile bool toBootloader = false;
static void OnUsbReceive(uint8_t*, uint32_t*) { toBootloader = true; }

static char report[3072];
static int  reportLen = 0;
template <typename... A>
static void Add(const char* fmt, A... a)
{
    if(reportLen < (int)sizeof(report))
        reportLen += snprintf(report + reportLen, sizeof(report) - reportLen, fmt, a...);
}

static uint32_t Pattern(uint32_t i) { return i * 2654435761u ^ 0x5A5AA5A5u; }

static void CheckSdram()
{
    uint32_t errors = 0, firstBad = 0xFFFFFFFF;
    for(int pass = 0; pass < 2; pass++)
    {
        const uint32_t flip = pass ? 0xFFFFFFFFu : 0u;
        for(uint32_t i = 0; i < kSdramWords; i++)
            sdram[i] = Pattern(i) ^ flip;
        SCB_CleanInvalidateDCache();   // read from the chip, not the cache
        for(uint32_t i = 0; i < kSdramWords; i++)
            if(sdram[i] != (Pattern(i) ^ flip))
            {
                if(errors == 0)
                    firstBad = i * 4;
                errors++;
            }
    }
    if(errors == 0)
        Add("SDRAM   PASS  64 MB, two patterns, no errors\r\n");
    else
        Add("SDRAM   FAIL  %lu bad words, first at offset 0x%08lx\r\n",
            (unsigned long)errors, (unsigned long)firstBad);
}

static void CheckQspi()
{
    // Erased, written, read back and erased again, five times over two sectors: a fault that
    // repeats on the same byte is the chip or its wiring; one that wanders is the write path.
    static uint8_t buf[4096];
    // Not the chip's last sector: its very last byte reads back as the byte 32 before it (the
    // controller's FIFO depth) through the memory-mapped window, on a healthy Seed, while the
    // byte before it and an erase read fine. Found 2026-11-12; a read quirk at the end of the
    // controller's address range, not the chip, so the test stays clear of it.
    const uint32_t kSectors[2] = {0x7FE000, 0x7FD000};
    int  totalBad = 0, notBlank = 0, errs = 0;
    char where[256] = "";
    for(int run = 0; run < 5; run++)
    {
        const uint32_t sector = kSectors[run & 1];
        for(int i = 0; i < 4096; i++)
            buf[i] = (uint8_t)(i * 7 + 3 + run);
        if(hw.qspi.EraseSector(sector) != QSPIHandle::Result::OK)
            errs++;
        if(hw.qspi.Write(sector, sizeof(buf), buf) != QSPIHandle::Result::OK)
            errs++;
        const uint8_t* mem = (const uint8_t*)hw.qspi.GetData(sector);
        SCB_InvalidateDCache_by_Addr((uint32_t*)mem, sizeof(buf));
        for(int i = 0; i < 4096; i++)
            if(mem[i] != buf[i])
            {
                totalBad++;
                if(strlen(where) < sizeof(where) - 40)
                    snprintf(where + strlen(where), sizeof(where) - strlen(where),
                             " [run %d +0x%03x want %02x got %02x]", run, i, buf[i], mem[i]);
            }
        if(hw.qspi.EraseSector(sector) != QSPIHandle::Result::OK)
            errs++;
        SCB_InvalidateDCache_by_Addr((uint32_t*)mem, sizeof(buf));
        for(int i = 0; i < 4096; i++)
            if(mem[i] != 0xFF)
                notBlank++;
    }
    if(errs == 0 && totalBad == 0 && notBlank == 0)
        Add("QSPI    PASS  five erase/write/verify/erase runs over two sectors, no errors\r\n");
    else
    {
        Add("QSPI    FAIL  %d driver errors, %d bad bytes, %d not blank after erase\r\n", errs,
            totalBad, notBlank);
        if(where[0])
            Add("       %s\r\n", where);
    }
}

static void CheckAudio()
{
    hw.SetAudioBlockSize(48);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.StartAudio(Audio);
    System::Delay(100);
    const uint32_t b0 = audioBlocks;
    System::Delay(1000);
    const uint32_t blocks = audioBlocks - b0;
    hw.StopAudio();
    // 48 kHz / 48 samples = 1000 blocks a second
    if(blocks > 980 && blocks < 1020)
        Add("AUDIO   PASS  codec running, %lu blocks in 1 s (1000 expected)\r\n",
            (unsigned long)blocks);
    else
        Add("AUDIO   FAIL  %lu blocks in 1 s (1000 expected)\r\n", (unsigned long)blocks);
}

static void SetAll(GPIO::Pull pull)
{
    for(int i = 0; i < kNumPins; i++)
        gpio[i].Init(kPins[i], GPIO::Mode::INPUT, pull);
    System::DelayUs(50);
}

static void CheckPins()
{
    char stuckLow[160] = "", stuckHigh[160] = "", bridges[320] = "";
    bool lowAlready[kNumPins] = {};
    SetAll(GPIO::Pull::PULLUP);
    for(int i = 0; i < kNumPins; i++)
        if(!gpio[i].Read())
        {
            lowAlready[i] = true;
            snprintf(stuckLow + strlen(stuckLow), sizeof(stuckLow) - strlen(stuckLow), " D%d", i);
        }
    SetAll(GPIO::Pull::PULLDOWN);
    for(int i = 0; i < kNumPins; i++)
        if(gpio[i].Read())
            snprintf(stuckHigh + strlen(stuckHigh), sizeof(stuckHigh) - strlen(stuckHigh), " D%d", i);
    // Each pin driven low in turn, the rest pulled up: any that follow it are joined to it.
    for(int k = 0; k < kNumPins; k++)
    {
        SetAll(GPIO::Pull::PULLUP);
        gpio[k].Init(kPins[k], GPIO::Mode::OUTPUT);
        gpio[k].Write(false);
        System::DelayUs(50);
        for(int i = k + 1; i < kNumPins; i++)
            if(!gpio[i].Read() && !lowAlready[i])   // one stuck low says nothing about a bridge
                snprintf(bridges + strlen(bridges), sizeof(bridges) - strlen(bridges),
                         " D%d-D%d", k, i);
    }
    SetAll(GPIO::Pull::NOPULL);
    if(!stuckLow[0] && !stuckHigh[0] && !bridges[0])
        Add("PINS    PASS  D0-D30: none stuck, no bridges\r\n");
    else
    {
        Add("PINS    CHECK D0-D30:\r\n");
        if(stuckLow[0])
            Add("        low with pull-up (short to ground?):%s\r\n", stuckLow);
        if(stuckHigh[0])
            Add("        high with pull-down (short to 3V3?):%s\r\n", stuckHigh);
        if(bridges[0])
            Add("        joined (bridge?):%s\r\n", bridges);
    }
}

int main(void)
{
    hw.Init(true);   // 480 MHz
    hw.StartLog(false);
    // Any byte from the PC reboots into the ROM bootloader, for the next flash without BOOT.
    hw.usb_handle.SetReceiveCallback(OnUsbReceive, UsbHandle::FS_INTERNAL);

    const char* ver = "Seed (rev 4, AK4556)";
    switch(hw.CheckBoardVersion())
    {
        case DaisySeed::BoardVersion::DAISY_SEED_1_1: ver = "Seed 1.1 (WM8731)"; break;
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: ver = "Seed 2 DFM (PCM3060)"; break;
        case DaisySeed::BoardVersion::DAISY_SEED_1_2: ver = "Seed 1.2"; break;
        default: break;
    }
    Add("seed_check: %s, %lu MHz\r\n", ver,
        (unsigned long)(System::GetSysClkFreq() / 1000000u));
    CheckSdram();
    CheckQspi();
    CheckAudio();
    CheckPins();
    Add("done; the LED blinks twice a second\r\n");

    uint32_t last = 0;
    bool     led  = false;
    for(;;)
    {
        if(toBootloader)
            System::ResetToBootloader(System::BootloaderMode::STM);
        const uint32_t now = System::GetNow();
        if(now - last >= 250)
        {
            last = now;
            led  = !led;
            hw.SetLed(led);
            static int ticks = 0;
            if(++ticks % 12 == 0)
            {
                hw.PrintLine("----");
                // PrintLine takes one line at a time
                char* p = report;
                while(*p)
                {
                    char* e = strstr(p, "\r\n");
                    if(!e)
                        break;
                    *e = '\0';
                    hw.PrintLine("%s", p);
                    *e = '\r';
                    p = e + 2;
                }
            }
        }
    }
}
