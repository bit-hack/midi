#pragma once

#include <stdbool.h>
#include <stdint.h>

enum midi_format_t {
    e_midi_fmt_one_track = 0,
    e_midi_fmt_multi_track = 1,
    e_midi_fmt_multi_song = 2,
};

enum midi_event_type_t {
    // 1000nnnn
    // 0kkkkkkk [0-127]
    // 0vvvvvvv [0-127]
    e_midi_event_note_off = 0x80,

    // 1001nnnn 0kkkkkkk 0vvvvvvv
    e_midi_event_note_on = 0x90,

    // 1010nnnn 0kkkkkkk 0vvvvvvv
    e_midi_event_poly_aftertouch = 0xa0,

    // channel control change
    // 1011nnnn [0- 15] channel
    // 0ccccccc [0-119] control index
    // 0vvvvvvv [0-127] control value
    e_midi_event_ctrl_change = 0xb0,

    // program change
    // 1100nnnn 0ppppppp
    e_midi_event_prog_change = 0xc0,

    // 1101nnnn 0vvvvvvv
    e_midi_event_chan_aftertouch = 0xd0,

    // 1110nnnn 0lllllll 0mmmmmmm
    e_midi_event_pitch_wheel = 0xe0,

    // 1111nnnn ...
    e_midi_event_sysex = 0xF0,

    // 11111111 ...
    e_midi_event_sysex_meta = 0xFF,

    // channel mode messages
    // 1011nnnn [120-127]
    // note: same as e_midi_event_ctrl_change
//    e_midi_event_chan_mode

    e_midi_event_sysex_end = 0xF7
};

struct midi_t {
    uint32_t mthd;
    uint32_t length;
    uint16_t format;
    uint16_t num_tracks;
    uint16_t divisions;
    struct midi_track_t* tracks;
};

struct midi_track_t {
    uint32_t mtrk;
    uint32_t length;
    const uint8_t* data;
};

struct midi_event_t {
    uint64_t delta;
    uint8_t type;
    uint8_t channel;
    uint64_t length; // length of data field
    const uint8_t* data;
};

struct midi_stream_t;

struct midi_t* midi_load(const void* data, size_t size);
void midi_free(struct midi_t* midi);

struct midi_stream_t* midi_stream(struct midi_t* midi, uint32_t track);
void midi_stream_free(struct midi_stream_t* stream);
bool midi_next_event(struct midi_stream_t* stream, struct midi_event_t * event);
bool midi_stream_end(struct midi_stream_t* stream);
