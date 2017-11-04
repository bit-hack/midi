//  ____     _____________      _____   ___________   ___
// |    |\  |   \______   \    /     \ |   \______ \ |   |\
// |    ||  |   ||    |  _/\  /  \ /  \|   ||    |  \|   ||
// |    ||__|   ||    |   \/ /    Y    \   ||    `   \   ||
// |________\___||________/\ \____|____/___/_________/___||
//  \________\___\________\/  \____\____\__\_________\____\

#pragma once
#include <stdbool.h>
#include <stdint.h>


enum midi_format_t {

    // one song one track
    e_midi_fmt_one_track = 0,

    // one song, multiple tracks
    // note: midi_stream_mux() will help to demux events from tracks
    e_midi_fmt_multi_track = 1,

    // one song per track
    e_midi_fmt_multi_song = 2,
};

enum midi_event_type_t {

    // note off
    // 1000nnnn [0- 15] channel
    // 0kkkkkkk [0-127]
    // 0vvvvvvv [0-127]
    e_midi_event_note_off = 0x80,

    // note on
    // 1001nnnn [0- 15] channel
    // 0kkkkkkk [0-127] key
    // 0vvvvvvv [0-127] velocity
    e_midi_event_note_on = 0x90,

    // polyphonic aftertouch
    // 1010nnnn [0- 15] channel
    // 0kkkkkkk [0-127] key
    // 0vvvvvvv [0-127] velocity
    e_midi_event_poly_aftertouch = 0xa0,

    // channel control change
    // 1011nnnn [0- 15] channel
    // 0ccccccc [0-119] control index
    // 0vvvvvvv [0-127] control value
    // note: if control index >= 120 then this is midi channel mode event
    e_midi_event_ctrl_change = 0xb0,

    // program change
    // 1100nnnn [0- 15] channel
    // 0ppppppp [0-127] program
    e_midi_event_prog_change = 0xc0,

    // channel aftertouch
    // 1101nnnn
    // 0vvvvvvv
    e_midi_event_chan_aftertouch = 0xd0,

    // 1110nnnn
    // 0lllllll
    // 0mmmmmmm
    e_midi_event_pitch_wheel = 0xe0,

    // 1111nnnn ...
    e_midi_event_sysex = 0xF0,

    // sysex meta event
    // 11111111 ...
    e_midi_event_meta = 0xFF,

    e_midi_event_sysex_end = 0xF7,

    // channel mode event
    // note: derived from e_midi_event_ctrl_change
    e_midi_event_channel_mode = 0x100
};

enum midi_event_meta_type_t {
    // 0
    e_midi_meta_sequence_number,
    // 1
    e_midi_meta_text,
    // 2
    e_midi_meta_copyright,
    // 3
    e_midi_meta_track_name,
    // 4
    e_midi_meta_inst_name,
    // 5
    e_midi_meta_lyric,
    // 6
    e_midi_meta_marker,
    // 7
    e_midi_meta_cue_point,
    // 8
    e_midi_meta_prog_name,
    // 9
    e_midi_meta_device_name,
    // 20
    e_midi_meta_chan_prefix,
    // 21
    e_midi_meta_port,
    // 2f
    e_midi_meta_end_of_track,
    // 51
    e_midi_meta_tempo,
    // 54
    e_midi_meta_smpte_offset,
    // 58
    e_midi_meta_time_signature,
    // 59
    e_midi_meta_key_signature,
    // 7f
    e_midi_meta_seq_event,
};

enum midi_channel_mode_type_t {
    e_midi_cmode_all_sound_off          = 120,
    e_midi_cmode_reset_all_controllers  = 121,
    e_midi_cmode_local_control          = 122,
    e_midi_cmode_all_notes_off          = 123,
    e_midi_cmode_omni_mode_off          = 124,
    e_midi_cmode_omni_mode_on           = 125,
    e_midi_cmode_mono_mode_on           = 126,
    e_midi_cmode_poly_mode_on           = 127,
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
    uint32_t type;
    uint32_t meta;
    uint32_t channel;
    uint64_t length; // length of data field
    const uint8_t* data;
};

struct midi_stream_t;

// load a midi file from memory
struct midi_t* midi_load(
    const void* data,
    size_t size);

// release a midi file
void midi_free(
    struct midi_t* midi);

// create a new midi track stream
struct midi_stream_t* midi_stream(
    struct midi_t* midi,
    uint32_t track);

// releases a midi stream
void midi_stream_free(
    struct midi_stream_t* stream);

// return true if stream has ended
bool midi_stream_end(
    struct midi_stream_t* stream);

// multiplex multiple input data streams
bool midi_stream_mux(
    struct midi_stream_t** stream,
    uint64_t* delta,
    const size_t count,
    struct midi_event_t* event,
    uint64_t* time_out,
    size_t* index);

// peek an event without advancing the stream
bool midi_event_peek(
    struct midi_stream_t* stream,
    struct midi_event_t* event);

// return event and advance to next event
bool midi_event_next(
    struct midi_stream_t* stream,
    struct midi_event_t* event);

// return next event delta time
bool midi_event_delta(
    struct midi_stream_t* stream,
    uint64_t* delta);
