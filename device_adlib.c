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
#define DRUM_CHANNEL   9u

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

/**
 * phase_inc = fmus * 2^19 / fsam
 * fsam      = fM / 72
 * fmus      = desired frequency
 * fsam      = sampling frequency       (50kHz)
 * fM        = frequency of input clock (3.6Mhz)
 *
 * phase_inc = 2^B * F' * MUL
 * B         = block
 * F'        = increment limited to single octabe
 * MUL       = multiple
 *
 * FNum      = (fmus * 2^19 / fsam) / 2^(block-1)
 *
 * FNum      = (fmus * 2^19 / 50000) / 2^(block-1)
 *
 * We can also map frequency to fnumber as follows:
 *
 *     // this table has values for (freq / fnum) per block
 *     static const float intervals[8] = {
 *         .048f, .095f, .190f, .379f, .759f, 1.517f, 3.034f, 6.069f
 *     };
 *     const float interval = intervals[block];
 *     uint32_t fnumber = (uint32_t)(freq / interval);
**/

// key to block mapping
static const uint8_t opl_block_table[128] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7,
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

// key to fnum mapping (some values are over 10bits)
static const uint16_t opl_fnum_table[128] = {
  182,  192,  204,  216,  229,  242,  257,  272,  288,  306, 324,  343,  363,
  385,  408,  432,  458,  485,  514,  544,  577,  611,  647, 686,  727,  770,
  816,  864,  915,  970,  514,  544,  577,  611,  647,  686, 727,  770,  816,
  864,  915,  970,  514,  544,  577,  611,  647,  686,  727, 770,  816,  864,
  915,  970,  514,  544,  577,  611,  647,  686,  727,  770, 816,  864,  915,
  970,  514,  544,  577,  611,  647,  686,  727,  770,  816, 864,  915,  970,
  514,  544,  577,  611,  647,  686,  727,  770,  816,  864, 915,  970,  514,
  544,  577,  611,  647,  686,  727,  770,  816,  864,  915, 970,  514,  544,
  577,  611,  647,  686,  727,  770,  816,  864,  915,  970, 1028, 1089, 1153,
  1222, 1295, 1372, 1453, 1540, 1631, 1728, 1831, 1940,
};

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

static uint8_t opl_reg_read(uint32_t index)
{
    return opl_regs[index];
}

static uint32_t opl_channel_index(const struct opl_channel_t* oc)
{
    size_t diff = oc - opl_channel;
    return (uint32_t)diff;
}

// allocate the most suitable OPL channel
static struct opl_channel_t* opl_channel_alloc(uint32_t channel, uint32_t key)
{
    struct opl_channel_t* best = &opl_channel[0];

    for (uint32_t i = 1; i < OPL_CHANNELS; ++i) {

        struct opl_channel_t* oc = &opl_channel[i];

        // reuse an existing channel and key if we can
        if (oc->midi_channel == channel && oc->midi_key == key) {
            // this is optimal so just return it immediately
            return oc;
        }

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

        // only look for active channels
        if (!oc->active) {
            continue;
        }

        // key and channel need to match
        if (oc->midi_key     != key ||
            oc->midi_channel != midi_channel) {
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

static void opl_note_on(uint32_t channel, uint32_t key)
{
    assert(channel <  OPL_CHANNELS);
    assert(key     <= 127);

    // the key_on 0xb0 register bit
    const uint8_t key_on = (1 << 5);

          uint32_t fnumber = opl_fnum_table[key];
    const uint32_t block   = opl_block_table[key];

    // some midi keys cant fit in the 3bit block and 10bit fnumber
    // so just clamp them
    fnumber = (fnumber >= 1023) ? 1023 : fnumber;

    const uint8_t ra0 = fnumber & 0xff;
    const uint8_t rb0 = ((block & 0x7) << 2) | ((fnumber & 0x300) >> 8) | key_on;

    opl_reg_write(0xa0 + channel, ra0, 0xff);
    opl_reg_write(0xb0 + channel, rb0, 0x3f);
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

static void opl_total_level(uint32_t slot, uint8_t velocity)
{
    assert(velocity <= 127);

    const uint32_t level = opl_reg_read(0x40 + slot) & 0x3f;  // 6bit
    const uint32_t vel   = opl_volume_table[velocity]; // 7bit
    const uint32_t out   = (level * vel) >> 7;

    opl_reg_write(0x40 + slot, out, 0x3f);
}

static void opl_channel_volume(uint32_t channel, uint32_t velocity)
{
  assert(channel < OPL_CHANNELS);

  // check the operator configurations to see which levels we need to change

  const uint8_t rc0 = opl_reg_read(0xC0 + channel);

  uint8_t conn = rc0 & 0x01;

  //               .--------.
  //               V        |
  // conn=0    P1 -+-- OP1 -o-+-- OP2 ----> OUT
  //                          ^
  //                          |
  //                          P2

  //               .--------.
  //               V        |
  // conn=1    P1 -+-- OP1 -o-.
  //                          |
  //                          + ----> OUT
  //                          |
  //           P2 ---- OP2 ---'

  const uint32_t op1 = opl_slot0[channel];
  const uint32_t op2 = opl_slot1[channel];

  // always level shift OP2
  opl_total_level(op2, velocity);

  // level shift OP1 too when in connection 1
  if (conn) {
    opl_total_level(op1, velocity);
  }
}

// ----------------------------------------------------------------------------
// Midi event handling
// ----------------------------------------------------------------------------

static void midi_drum_on(const uint32_t key, const uint32_t velocity)
{
    struct opl_channel_t* oc = opl_channel_alloc(DRUM_CHANNEL, key);
    const uint32_t ci = opl_channel_index(oc);
    oc->age           = ageCounter;
    oc->midi_key      = key;
    oc->midi_channel  = DRUM_CHANNEL;
    oc->midi_velocity = velocity;
    oc->active        = true;

    // drum notes are within this range
    if (key < 35 || key > 75) {
      // not a valid drum note
      return;
    }

    // upload program to OPL channel
    const uint8_t* program = opl_drums_table_miles[key - 35];
    opl_program_set(ci, program);

    // set channel volume
    opl_channel_volume(ci, velocity);

    // send key-on to OPL
    opl_note_on(ci, 35);
}

static void midi_note_on(const struct midi_event_t* event)
{
    const uint32_t channel  = event->channel;
    const uint32_t key      = event->data[0];
    const uint32_t velocity = event->data[1];

    assert(channel < MIDI_CHANNELS);
    assert(key < 256);

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

    if (channel == DRUM_CHANNEL) {
        midi_drum_on(key, velocity);
        return;
    }

    struct opl_channel_t* oc = opl_channel_alloc(channel, key);
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

    // set channel volume
    opl_channel_volume(ci, velocity);

    // send key-on to OPL
    opl_note_on(ci, key);
}

static void midi_note_off(const struct midi_event_t* event)
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

static void midi_prog_change(const struct midi_event_t* event)
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

    return true;
}

static void device_adlib_send(const struct midi_event_t* event)
{
    ++ageCounter;

    switch (event->type) {
    case e_midi_event_note_on:     midi_note_on    (event); break;
    case e_midi_event_note_off:    midi_note_off   (event); break;
    case e_midi_event_prog_change: midi_prog_change(event); break;
    }
}

static void device_adlib_close(void)
{
    // clear registers
    oplpi_reset();
}

void device_adlib_select(void)
{
    device_open  = device_adlib_open;
    device_send  = device_adlib_send;
    device_close = device_adlib_close;
}
