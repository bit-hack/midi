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

struct file_t {
    void* file_;
    size_t size_;
};

HMIDIOUT hmidiout;

struct midi_t* midi;

double milliseconds;

ULONGLONG startTicks = 0;

// tempo:
//
// Unlike music, tempo in MIDI is not given as beats per minute, but rather in microseconds per beat.
//
// 60,000,000 microseconds per minute
//  1,000,000 microseconds per second
//      1,000 microseconds per millisecond
// set the tempo to 60000000 / 500000 = 120 quarter notes per minute (120 beats per minute)
uint64_t tempo = 500000;  // 120 bpm

// midi file divisions:
//
// When the top bit of the time division bytes is 0, the time division is in ticks per beat.
// The remaining 15 bits are the number of MIDI ticks per beat (per quarter note). If, for
// example, these 15 bits compute to the number 60, then the time division is 60 ticks per
// beat and the length of one tick is
//
// 1 tick = microseconds per beat / 60

// Timing in MIDI files is centered around ticks and beats.
// A beat is the same as a quarter note.
// Beats are divided into ticks, the smallest unit of time in MIDI.

double frequency;
LARGE_INTEGER counterStart;

static double sysMilliseconds() {

  LARGE_INTEGER counter = { 0 };
  QueryPerformanceCounter(&counter);

  return (double)(counter.QuadPart - counterStart.QuadPart) * 1000.0 / (double)frequency;
}

static double ticksToMilliseconds(uint64_t ticks) {
    double usPerTick     = (double)tempo / (double)(midi->divisions);
    double millisPerTick = usPerTick / 1000.0;
    double millis        = millisPerTick * ticks;
    return millis;
}

static bool midiOpen()
{
    MMRESULT res = { 0 };

    UINT numDevices = midiOutGetNumDevs();
    if (numDevices == 0) {
        return false;
    }

    MIDIOUTCAPS caps = { 0 };
    res = midiOutGetDevCaps(0, &caps, sizeof(caps));
    if (res != MMSYSERR_NOERROR) {
        return false;
    }

    UINT deviceid = 0;
    res = midiOutOpen(&hmidiout, deviceid, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
      return false;
    }

    return true;
}

static void midi_send(const struct midi_event_t* event)
{
    union {
        DWORD dwData;
        BYTE bData[4];
    } u;

    u.bData[0] = (event->type & 0xf0) | (event->channel & 0x0f);
    u.bData[1] = event->data[0]; 
    u.bData[2] = event->data[1]; 
    u.bData[3] = 0;

    midiOutShortMsg(hmidiout, u.dwData);
}

static void midiClose()
{
    midiOutClose(hmidiout);
}

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

static const char* eventName(uint32_t type)
{
    switch (type) {
    case e_midi_event_note_off:
        return "note off";
    case e_midi_event_note_on:
        return "note on";
    case e_midi_event_poly_aftertouch:
        return "poly aftertouch";
    case e_midi_event_ctrl_change:
        return "ctrl change";
    case e_midi_event_prog_change:
        return "prog change";
    case e_midi_event_chan_aftertouch:
        return "chan aftertouch";
    case e_midi_event_pitch_wheel:
        return "pitch wheel";
    case e_midi_event_sysex:
        return "sysex";
    case e_midi_event_meta:
        return "meta";
    case e_midi_event_sysex_end:
        return "sysex end";
    case e_midi_event_channel_mode:
        return "channel mode";
    default:
        return "unknown";
    }
}

static const char* metaEventName(uint32_t type)
{
    switch (type) {
    case e_midi_meta_sequence_number:
        return "sequence_number";
    case e_midi_meta_text:
        return "text";
    case e_midi_meta_copyright:
        return "copyright";
    case e_midi_meta_track_name:
        return "track_name";
    case e_midi_meta_inst_name:
        return "inst_name";
    case e_midi_meta_lyric:
        return "lyric";
    case e_midi_meta_marker:
        return "marker";
    case e_midi_meta_cue_point:
        return "cue_point";
    case e_midi_meta_prog_name:
        return "prog_name";
    case e_midi_meta_device_name:
        return "device_name";
    case e_midi_meta_chan_prefix:
        return "chan_prefix";
    case e_midi_meta_port:
        return "port";
    case e_midi_meta_end_of_track:
        return "end_of_track";
    case e_midi_meta_tempo:
        return "tempo";
    case e_midi_meta_smpte_offset:
        return "smpte_offset";
    case e_midi_meta_time_signature:
        return "time_signature";
    case e_midi_meta_key_signature:
        return "key_signature";
    case e_midi_meta_seq_event:
        return "seq_event";
    default:
        return "unknown";
    }
}

static void print_event(const struct midi_event_t* event)
{
    const char* name = eventName(event->type);

    printf("%6llu: [%02x:%s:%s] %02x ",
        (uint64_t)event->delta,
        (uint32_t)event->type,
        name,
        (event->type == e_midi_event_meta) ? metaEventName(event->meta) : "",
        (uint32_t)event->channel);

    for (size_t i = 0; i < event->length; ++i) {
        printf("%c%02x", ((i == 0) ? '{' : ' '), event->data[i]);
    }

    printf("}\n");
}

static void handle_delta(uint64_t delta)
{
    milliseconds += ticksToMilliseconds(delta);

    for (;;) {
      double l = sysMilliseconds();

      if (l > milliseconds) {
          break;
      }

      Sleep(0);
    }
}

static uint64_t read_number(const uint8_t *ptr, uint64_t len) {
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

static void handle_event(const struct midi_event_t* event)
{
    // wait for a period of time
    handle_delta(event->delta);

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
        midi_send(event);
        break;
    case e_midi_event_meta:
        handle_meta(event);
        break;
    }
}

static int dump_tracks(struct midi_t* mid)
{
    // itterate over tracks
    for (int i = 0; i < mid->num_tracks; ++i) {

        // get the midi stream
        struct midi_stream_t* stream = midi_stream(mid, i);
        if (!stream) {
            return 1;
        }

        printf("Track %d\n", i);

        // itterate over all of the events
        struct midi_event_t event;
        while (!midi_stream_end(stream)) {
            if (!midi_event_next(stream, &event)) {
                return 1;
            }

            print_event(&event);
            handle_event(&event);
        }
        midi_stream_free(stream);
    }
    return 0;
}

static int dump_demux_events(struct midi_t* mid)
{
#define MAX_STREAMS 256
    struct midi_stream_t* streams[MAX_STREAMS] = { NULL };
    uint64_t times[MAX_STREAMS] = { 0 };
    for (int i = 0; i < mid->num_tracks; ++i) {
        streams[i] = midi_stream(mid, i);
        if (streams[i] == NULL) {
            return 1;
        }
    }

    struct midi_event_t event;
    uint64_t time = 0;
    size_t index = 0;
    while (midi_stream_mux(streams, times, mid->num_tracks, &event, &time, &index)) {
        printf("%8llu %02x", time, (int)index);
        print_event(&event);
    }

    for (int i = 0; i < mid->num_tracks; ++i) {
        assert(streams[i]);
        midi_stream_free(streams[i]);
    }

#undef MAX_STREAMS
    return 0;
}

int main(const int argc, const char* args[])
{
    if (argc < 2) {
        return 1;
    }

    // load this raw file
    struct file_t file;
    const char* path = args[1];
    if (!file_load(path, &file)) {
        return 1;
    }

    // parse as midi
    midi = midi_load(file.file_, file.size_);
    if (!midi) {
        return 1;
    }

    midiOpen();

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    frequency = (double)freq.QuadPart;

    QueryPerformanceCounter(&counterStart);

    int ret_val = 0;
    switch (0 /* mode */) {
    case 0:
        ret_val = dump_tracks(midi);
        break;
    case 1:
        ret_val = dump_demux_events(midi);
        break;
    }
    midi_free(midi);

    midiClose();

    // success
    return ret_val;
}
