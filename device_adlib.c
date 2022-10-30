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

#include "libmidi.h"
#include "midiplay.h"

#define OPL_CHANNELS   9u
#define MIDI_CHANNELS 16u

// event age counter
uint32_t age;

// opl channel
struct opl_channel_t {
    uint8_t  midi_key;
    uint8_t  midi_channel;
    uint8_t  midi_velocity;
    uint32_t age;
};

struct midi_channel_t {
    uint8_t program;
};

struct opl_channel_t  opl_channel [OPL_CHANNELS];
struct midi_channel_t midi_channel[MIDI_CHANNELS];

// ----------------------------------------------------------------------------
//
// ----------------------------------------------------------------------------

// allocate the most suitable OPL channel
static struct opl_channel_t* opl_channel_alloc()
{
    // this considers:
    // - oldest channel
    // - oldest free channel

    struct opl_channel_t* oldest  = &opl_channel[0];

    for (uint32_t i = 0; i < OPL_CHANNELS; ++i) {
        // if this channel has a better age then our channel
        if (opl_channel[i].age < oldest->age) {
            oldest = &opl_channel[i];
        }
    }

    return oldest;
}

static void opl_note_off(int channel)
{
    // TODO
}

// ----------------------------------------------------------------------------
// 
// ----------------------------------------------------------------------------

static void note_off(const struct midi_event_t* event)
{
    const uint32_t channel  = event->channel;
    const uint32_t key      = event->data[0];

    for (int i = 0; i < OPL_CHANNELS; ++i) {
        struct opl_channel_t *oc = &opl_channel[i];
        if (oc->midi_key != key) {
            continue;
        }
        if (oc->midi_channel != channel) {
            continue;
        }

        // send a note off to the OPL chip
        opl_note_off(i);

        // free up this channel
        oc->age = 0;
    }
}

static void note_on(const struct midi_event_t* event)
{
    const uint32_t channel  = event->channel;
    const uint32_t key      = event->data[0];
    const uint32_t velocity = event->data[1];

    assert(channel < MIDI_CHANNELS);
    assert(key < 256);

    if (velocity == 0) {
        // this is sometimes used in place of a note off
        // TODO
        return;
    }

    struct opl_channel_t* oc = opl_channel_alloc();
    oc->age           = age;
    oc->midi_key      = key;
    oc->midi_channel  = channel;
    oc->midi_velocity = velocity;

    // lookup the program for this channel
    struct midi_channel_t* mc = &midi_channel[channel];

    // upload program to OPL channel
    const uint32_t midi_program = mc->program;

    // convert midi note to hertz

    // send key-on to OPL

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

static void ctrl_change(const struct midi_event_t* event)
{
}

// ----------------------------------------------------------------------------
// ADLIB (OPL2/OPL3) synthesizer device
// ----------------------------------------------------------------------------

static bool device_adlib_open(void)
{
    return true;
}

static void device_adlib_send(const struct midi_event_t* event)
{
    ++age;

    switch (event->type) {
    case e_midi_event_note_on:     note_on    (event); break;
    case e_midi_event_note_off:    note_off   (event); break;
    case e_midi_event_prog_change: prog_change(event); break;
    case e_midi_event_ctrl_change: ctrl_change(event); break;
    }
}

static void device_adlib_close(void)
{
}

void device_adlib_select(void)
{
    device_open  = device_adlib_open;
    device_send  = device_adlib_send;
    device_close = device_adlib_close;
}
