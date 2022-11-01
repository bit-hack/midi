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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libmidi.h"
#include "midiplay.h"


// ----------------------------------------------------------------------------
// Midi device abstraction
// ----------------------------------------------------------------------------

device_open_t  device_open;
device_close_t device_close;
device_send_t  device_send;

// ----------------------------------------------------------------------------
// System timer
// ----------------------------------------------------------------------------

// processor counter at program start
static LARGE_INTEGER counter_start;

// processor counter frequency
static double counter_frequency;

static double timer_get_millis(void)
{
    LARGE_INTEGER counter = { 0 };
    QueryPerformanceCounter(&counter);

    const uint64_t diff = counter.QuadPart - counter_start.QuadPart;
    return (double)diff * 1000.0 / (double)counter_frequency;
}

static void timer_init(void)
{
    LARGE_INTEGER freq = { 0 };
    QueryPerformanceFrequency(&freq);
    counter_frequency = (double)freq.QuadPart;
    QueryPerformanceCounter(&counter_start);
}

// ----------------------------------------------------------------------------
// File loading helper
// ----------------------------------------------------------------------------

struct file_t {
    void* file_;
    size_t size_;
};

static bool file_load(const char* path, struct file_t* out)
{
#define TRY(EXPR)       \
    {                   \
        if (!(EXPR))    \
            goto error; \
    }
    assert(path && out);
    FILE* fd = fopen(path, "rb");
    TRY(fd);
    TRY(fseek(fd, 0, SEEK_END) == 0);
    TRY((out->size_ = ftell(fd)) > 0);
    TRY(out->file_ = malloc(out->size_));
    rewind(fd);
    TRY(fread(out->file_, 1, out->size_, fd) == out->size_);
    fclose(fd);
    return true;
error:
    if (fd) {
        fclose(fd);
    }
    return false;
#undef TRY
}

// ----------------------------------------------------------------------------
// Midi Playing routines
// ----------------------------------------------------------------------------

// the midi file we are parsing
static struct midi_t* midi;

// accumulate midi event times in milliseconds
static double milliseconds;

// unlike music, tempo in MIDI is not given as beats per minute, but rather in
// microseconds per beat.
//
// 60,000,000 microseconds per minute
//  1,000,000 microseconds per second
//      1,000 microseconds per millisecond
//
// set the tempo to 60000000 / 500000 = 120 quarter notes per minute (120 beats
// per minute)
static uint64_t tempo = 500000; // 120 bpm

// midi file divisions:
//
// when the top bit of the time division bytes is 0, the time division is in
// ticks per beat. the remaining 15 bits are the number of MIDI ticks per
// beat (per quarter note). if, for example, these 15 bits compute to the
// number 60, then the time division is 60 ticks per beat and the length of
// one tick is
//
// 1 tick = microseconds per beat / 60

// timing in MIDI files is centered around ticks and beats. A beat is the same
// as a quarter note. beats are divided into ticks, the smallest unit of time
// in MIDI.

static double ticks_to_milliseconds(uint64_t ticks)
{
    const double usPerTick = (double)tempo / (double)(midi->divisions);
    const double millisPerTick = usPerTick / 1000.0;
    const double millis = millisPerTick * ticks;
    return millis;
}

static void handle_delta(uint64_t delta)
{
    // accumulate for target time in milliseconds
    milliseconds += ticks_to_milliseconds(delta);

    // wait until we reach out target time
    for (;;) {
        const double l = timer_get_millis();
        const double diff = l - milliseconds;

        // resynchronoise if delayed for too long
        if (diff >= 1000.0) {
            milliseconds = l;
            break;
        }

        if (diff >= 0.0) {
            break;
        }

        // yield the processor in our waiting loop
        Sleep(1);
    }
}

static uint64_t read_number(const uint8_t* ptr, uint64_t len)
{
    uint64_t out = 0;
    for (; len--; ++ptr) {
        out = (out * 256) | (*ptr);
    }
    return out;
}

static void handle_meta(const struct midi_event_t* event)
{
    switch (event->meta) {
    case e_midi_meta_tempo:
        tempo = read_number(event->data, event->length);
        break;
    }
}

static void handle_event(const struct midi_event_t* event, uint64_t delta)
{
    // wait for a period of time
    handle_delta(delta);

    // apply the event
    switch (event->type) {
    case e_midi_event_note_off:
    case e_midi_event_note_on:
    case e_midi_event_poly_aftertouch:
    case e_midi_event_ctrl_change:
    case e_midi_event_prog_change:
    case e_midi_event_chan_aftertouch:
    case e_midi_event_pitch_wheel:
    case e_midi_event_channel_mode:
        device_send(event);
        break;
    case e_midi_event_meta:
        handle_meta(event);
        break;
    }
}

static int play_demux_events(struct midi_t* mid)
{
    // create streams for each track in the midi file
#define MAX_STREAMS 256
    struct midi_stream_t* streams[MAX_STREAMS] = { NULL };
    uint64_t times[MAX_STREAMS] = { 0 };
    for (int i = 0; i < mid->num_tracks; ++i) {
        streams[i] = midi_stream(mid, i);
        if (streams[i] == NULL) {
            return 1;
        }
    }
#undef MAX_STREAMS

    // play events in a loop
    struct midi_event_t event;
    uint64_t lastTime = 0;
    uint64_t time     = 0;
    size_t   index    = 0;
    while (midi_stream_mux(streams, times, mid->num_tracks, &event, &time, &index)) {

        // work out delta since last event
        const uint64_t delta = time - lastTime;
        lastTime = time;
        // dispatch the event
        handle_event(&event, delta);
    }

    // release all of the tracks
    for (int i = 0; i < mid->num_tracks; ++i) {
        assert(streams[i]);
        midi_stream_free(streams[i]);
    }

    return 0;
}

// ----------------------------------------------------------------------------
// Program entry point
// ----------------------------------------------------------------------------

int main(const int argc, const char* args[])
{
    if (argc < 2) {
        fprintf(stderr, "Midi file argument required\n");
        return 1;
    }

    if (0) device_windows_select();
    if (1) device_adlib_select();

    // load this raw file
    struct file_t file;
    const char* path = args[1];
    if (!file_load(path, &file)) {
        fprintf(stderr, "Unable open file\n");
        return 1;
    }
    printf("Playing: '%s'\n", path);

    // parse as midi
    midi = midi_load(file.file_, file.size_);
    if (!midi) {
        fprintf(stderr, "Unable to parse midi file\n");
        return 1;
    }

    // open the output midi device
    if (!device_open()) {
        fprintf(stderr, "Unable to open midi device\n");
        return 1;
    }

    // start the performance timer
    timer_init();

    // run the play loop
    const int ret_val = play_demux_events(midi);

    // release midi file
    midi_free(midi);
    // shutdown midi device
    device_close();

    // success
    return ret_val;
}
