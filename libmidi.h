#pragma once

#include <stdbool.h>
#include <stdint.h>

enum midi_format_t {
    e_midi_fmt_one_track = 0,
    e_midi_fmt_multi_track = 1,
    e_midi_fmt_multi_song = 2,
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
    uint8_t* data;
};

struct midi_stream_t;

struct midi_t* midi_load(const void* data, size_t size);
void midi_free(struct midi_t* midi);

struct midi_stream_t* midi_stream(struct midi_t* midi, uint32_t track);
void midi_stream_free(struct midi_stream_t* stream);
bool midi_next_event(struct midi_stream_t* stream, struct midi_event_t * event);
