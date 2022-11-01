//  ____     _____________      _____   ___________   ___
// |    |\  |   \______   \    /     \ |   \______ \ |   |\
// |    ||  |   ||    |  _/\  /  \ /  \|   ||    |  \|   ||
// |    ||__|   ||    |   \/ /    Y    \   ||    `   \   ||
// |________\___||________/\ \____|____/___/_________/___||
//  \________\___\________\/  \____\____\__\_________\____\

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdio.h>
#endif

#include <mmeapi.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "libmidi.h"
#include "midiplay.h"
#include "bank.h"

#define OPL_CHANNELS   9u
#define MIDI_CHANNELS 16u

// event age counter
static uint32_t ageCounter;

struct opl_channel_t {
    uint8_t  midi_key;
    uint8_t  midi_channel;
    uint8_t  midi_velocity;
    uint32_t age;
    bool     active;
};

struct midi_channel_t {
    uint8_t program;
};

struct opl_channel_t  opl_channel [OPL_CHANNELS];
struct midi_channel_t midi_channel[MIDI_CHANNELS];

// mirror of the OPL registers
static uint8_t opl_regs[256];

// ----------------------------------------------------------------------------
// Lookup tables
// ----------------------------------------------------------------------------

// logarithmic relationship between midi and FM volumes
static uint8_t opl_volume_table[128] = {
     0,  11,  16,  19,  22,  25,  27,  29,  32,  33,  35,  37,  39, 40, 42, 43,
    45,  46,  48,  49,  50,  51,  53,  54,  55,  56,  57,  58,  59, 60, 61, 62,
    64,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75, 75, 76, 77,
    78,  79,  80,  80,  81,  82,  83,  83,  84,  85,  86,  86,  87, 88, 89, 89,
    90,  91,  91,  92,  93,  93,  94,  95,  96,  96,  97,  97,  98, 99, 99, 100,
   101, 101, 102, 103, 103, 104, 104, 105, 106, 106, 107, 107, 108,
   109, 109, 110, 110, 111, 112, 112, 113, 113, 114, 114, 115, 115,
   116, 117, 117, 118, 118, 119, 119, 120, 120, 121, 121, 122, 122,
   123, 123, 124, 124, 125, 125, 126, 126, 127
};

//  1  2  3    4  5  6    7  8  9   10 11 12   13 14 15   16 17 18    slot
//  1  2  3    1  2  3    4  5  6    4  5  6    7  8  9    7  8  9    channel
// 20 21 22 | 23 24 25 | 28 29 2a | 2b 2c 2d | 30 31 32 | 33 34 35    slot/reg
// c0 c1 c2 | c0 c1 c2 | c3 c4 c5 | c3 c4 c5 | c6 c7 c8 | c6 c7 c8    channel/reg

static uint32_t opl_slot0[OPL_CHANNELS] = { 0, 1, 2, /**/  8,  9, 10, /**/ 16, 17, 18 };
static uint32_t opl_slot1[OPL_CHANNELS] = { 3, 4, 5, /**/ 11, 12, 13, /**/ 19, 20, 21 };

// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------

static void opl_reg_write(uint32_t index, uint8_t value, uint8_t mask)
{
    const uint8_t old = opl_regs[index];

    opl_regs[index] &=        ~mask;
    opl_regs[index] |= value & mask;

    if (opl_regs[index] != old) {
        oplpi_write(index, opl_regs[index]);
    }
}

static uint32_t opl_channel_index(const struct opl_channel_t* oc)
{
    size_t diff = oc - opl_channel;
    return (uint32_t)diff;
}

// allocate the most suitable OPL channel
static struct opl_channel_t* opl_channel_alloc(void)
{
    struct opl_channel_t* best = &opl_channel[0];

    for (uint32_t i = 1; i < OPL_CHANNELS; ++i) {

        struct opl_channel_t* oc = &opl_channel[i];

        // steal if non active
        if (best->active && (!oc->active)) {
            best = oc;
            continue;
        }

        // dont steal from active notes
        if ((!best->active) && oc->active) {
            continue;
        }

        // if this channel has a better age then our channel
        if (best->age > oc->age) {
            best = oc;
            continue;
        }
    }

    return best;
}

static struct opl_channel_t* opl_channel_find(uint32_t midi_channel, uint32_t key)
{
    for (uint32_t i = 0; i < OPL_CHANNELS; ++i) {
        struct opl_channel_t* oc = &opl_channel[i];
        if (!oc->active) {
            continue;
        }
        if (oc->midi_key != key) {
            continue;
        }
        if (oc->midi_channel != midi_channel) {
            continue;
        }
        return oc;
    }

    return NULL;
}

static void opl_note_off(uint32_t channel)
{
    assert(channel < OPL_CHANNELS);

    // clear key-on bit for this channel
    opl_reg_write(0xb0 + channel, 0x00, 1u << 5);
}

static void opl_note_on(uint32_t channel)
{
    assert(channel < OPL_CHANNELS);

    // set key-on bit for this channel
    opl_reg_write(0xb0 + channel, 0xff, 1u << 5);
}

static void opl_program_set(uint32_t channel, const uint8_t* data)
{
    assert(channel < OPL_CHANNELS);

    const uint32_t s0 = opl_slot0[channel];
    const uint32_t s1 = opl_slot1[channel];

    const uint8_t index[11] = {
        0x20 + s0, 0x20 + s1,
        0x40 + s0, 0x40 + s1,
        0x60 + s0, 0x60 + s1,
        0x80 + s0, 0x80 + s1,
        0xe0 + s0, 0xe0 + s1,
        0xc0 + channel
    };

    for (uint32_t i = 0; i < 11; ++i) {
        opl_reg_write(index[i], data[i], 0xff);
    }
}

// see which block value a frequency falls on
static const uint8_t get_frequency_block(float freq)
{
    static const float block_frequencies[8] = {
        48.503f, 97.006f, 194.013f, 388.026f, 776.053f, 1552.107f, 3104.215f, 6208.431f
    };

    for (uint8_t i = 0; i < 8; i++) {
        if (freq < block_frequencies[i]) {
            return i;
        }
    }

    return 7;
}

static void opl_set_fnumber(uint32_t channel, float freq)
{
    assert(channel < MIDI_CHANNELS);

    // https://github.com/DhrBaksteen/ArduinoOPL2/blob/master/src/OPL2.h
    // https://nerdlypleasures.blogspot.com/2018/01/opl23-frequency-1hz-ish-difference.html

    static const float intervals[8] = {
        .048f, .095f, .190f, .379f, .759f, 1.517f, 3.034f, 6.069f
    };

    // find the best block for our frequency
    const uint8_t block = get_frequency_block(freq);

    // find the frequency interval for this block
    const float interval = intervals[block];

    uint32_t fnumber = (uint32_t)(freq / interval);
    fnumber = (fnumber >= 1024) ? 1023 : fnumber;

    //    |                 |
    // A0 | f f f f f f f f | (f) fnumber-low
    // A8 | f f f f f f f f | (F) fnumber-hi
    // B0 | . . K B B B F F | (B) block
    // B8 | . . K B B B F F | (K) key on
    //    |                 |

    const uint8_t a = fnumber & 0xff;
    const uint8_t b = ((block & 0x7) << 2) | ((fnumber & 0x300) >> 8);

    opl_reg_write(0xa0 + channel, a, 0xff);
    opl_reg_write(0xb0 + channel, b, 0x1f);
}

// ----------------------------------------------------------------------------
// Midi event handling
// ----------------------------------------------------------------------------

static float note_freq(uint8_t key)
{
    // key  hz
    // 69   440.0
    // freq = 440.f * 2 ^ ((n-69) / 12)

    // static const float freqTable[] = {
    //     6644.875161279122f, // 116
    //     7040.0f           , // 117
    //     7458.620184289437f, // 118
    //     7902.132820097988f, // 119
    //     8372.018089619156f, // 120
    //     8869.844191259906f, // 121
    //     9397.272573357044f, // 122
    //     9956.06347910659f , // 123
    //    10548.081821211836f, // 124
    //    11175.303405856126f, // 125
    //    11839.8215267723f  , // 126
    //    12543.853951415975f, // 127
    // };

    return 440.f * powf(2.f, ((float)key - 69.f) / 12.f);
}

#if 0
static void opl_set_note(uint32_t channel, uint32_t note, uint32_t volume)
{
    // note: these pitches are too high for some reason

    static uint16_t fnums[] = {
      0x16b, 0x181, 0x198, 0x1b0, 0x1ca, 0x1e5, 0x202, 0x220, 0x241, 0x263, 0x287, 0x2ae
    };

    if (note < 0)
        return;

    uint16_t freq  = fnums[note % 12];
    uint16_t block = note / 12;
    uint8_t  keyon = (1 << 5);
    uint8_t  pack  = ((freq >> 8) & 0x3) | ((block & 7) << 2) | keyon;

    opl_reg_write(0xa0 + channel, freq & 0xff, 0xff);
    opl_reg_write(0xb0 + channel, pack,        0xff);
}
#endif

static void note_on(const struct midi_event_t* event)
{
    const uint32_t channel  = event->channel;
    const uint32_t key      = event->data[0];
    const uint32_t velocity = event->data[1];

    assert(channel < MIDI_CHANNELS);
    assert(key < 256);

    if (channel == 9 /* skip drums for now */) {
        return;
    }

    if (velocity == 0) {

        // effectively note off
        struct opl_channel_t *oc = opl_channel_find(channel, key);
        if (!oc) {
            return;
        }

        oc->active = false;
        opl_note_off(opl_channel_index(oc));
        return;
    }

    struct opl_channel_t* oc = opl_channel_alloc();
    const uint32_t        ci = opl_channel_index(oc);
    oc->age           = ageCounter;
    oc->midi_key      = key;
    oc->midi_channel  = channel;
    oc->midi_velocity = velocity;
    oc->active        = true;

    // lookup the program for this channel
    struct midi_channel_t* mc = &midi_channel[channel];

    // upload program to OPL channel
    const uint32_t midi_program = mc->program;
    const uint8_t* program = opl_gm_table[midi_program];
    opl_program_set(ci, program);

#if 1
    // set channel volume
    // TODO

    // convert midi to frequency
    const float freq = note_freq(key);
    opl_set_fnumber(ci, freq);
    // send key-on to OPL
    opl_note_on(ci);
#else
    opl_set_note(ci, key, velocity);
#endif
}

static void note_off(const struct midi_event_t* event)
{
    const uint32_t channel = event->channel;
    const uint32_t key     = event->data[0];

    for (int i = 0; i < OPL_CHANNELS; ++i) {
        struct opl_channel_t* oc = &opl_channel[i];
        if (oc->midi_key != key) {
            continue;
        }
        if (oc->midi_channel != channel) {
            continue;
        }

        // send a note off to the OPL chip
        opl_note_off(i);

        // free up this channel
        oc->active = false;
    }
}

static void prog_change(const struct midi_event_t* event)
{
    const uint32_t channel = event->channel;
    const uint32_t program = event->data[0];

    assert(channel < MIDI_CHANNELS);
    assert(program < 128);

    // lookup the program for this channel
    struct midi_channel_t* mc = &midi_channel[channel];

    // save the program
    mc->program = program;
}

// ----------------------------------------------------------------------------
// ADLIB (OPL2/OPL3) synthesizer device
// ----------------------------------------------------------------------------

static bool device_adlib_open(void)
{
    oplpi_init();

    // clear registers
    for (uint32_t i = 0; i <= 0xff; ++i) {
        opl_reg_write(i, 0, 0xff);
    }

    return true;
}

static void device_adlib_send(const struct midi_event_t* event)
{
    ++ageCounter;

    switch (event->type) {
    case e_midi_event_note_on:     note_on    (event); break;
    case e_midi_event_note_off:    note_off   (event); break;
    case e_midi_event_prog_change: prog_change(event); break;
    }
}

static void device_adlib_close(void)
{
    // clear registers
    for (uint32_t i = 0; i <= 0xff; ++i) {
        opl_reg_write(i, 0, 0xff);
    }
}

void device_adlib_select(void)
{
    device_open  = device_adlib_open;
    device_send  = device_adlib_send;
    device_close = device_adlib_close;
}
