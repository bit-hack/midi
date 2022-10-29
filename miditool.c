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

#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "libmidi.h"


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

static char toPrintableAscii(const char ch)
{
    return (ch >= 32 && ch <= 126) ? ch : '.';
}

static const char* eventName(uint32_t type)
{
    switch (type) {
    case e_midi_event_note_off:        return "note off";
    case e_midi_event_note_on:         return "note on";
    case e_midi_event_poly_aftertouch: return "poly aftertouch";
    case e_midi_event_ctrl_change:     return "ctrl change";
    case e_midi_event_prog_change:     return "prog change";
    case e_midi_event_chan_aftertouch: return "chan aftertouch";
    case e_midi_event_pitch_wheel:     return "pitch wheel";
    case e_midi_event_sysex:           return "sysex";
    case e_midi_event_meta:            return "meta";
    case e_midi_event_sysex_end:       return "sysex end";
    case e_midi_event_channel_mode:    return "channel mode";
    default:                           return "unknown";
    }
}

static const char* metaEventName(uint32_t type)
{
    switch (type) {
    case e_midi_meta_sequence_number: return "sequence_number";
    case e_midi_meta_text:            return "text";
    case e_midi_meta_copyright:       return "copyright";
    case e_midi_meta_track_name:      return "track_name";
    case e_midi_meta_inst_name:       return "inst_name";
    case e_midi_meta_lyric:           return "lyric";
    case e_midi_meta_marker:          return "marker";
    case e_midi_meta_cue_point:       return "cue_point";
    case e_midi_meta_prog_name:       return "prog_name";
    case e_midi_meta_device_name:     return "device_name";
    case e_midi_meta_chan_prefix:     return "chan_prefix";
    case e_midi_meta_port:            return "port";
    case e_midi_meta_end_of_track:    return "end_of_track";
    case e_midi_meta_tempo:           return "tempo";
    case e_midi_meta_smpte_offset:    return "smpte_offset";
    case e_midi_meta_time_signature:  return "time_signature";
    case e_midi_meta_key_signature:   return "key_signature";
    case e_midi_meta_seq_event:       return "seq_event";
    default:                          return "unknown";
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
        (uint32_t)event->channel
    );

    for (size_t i = 0; i < event->length; ++i) {
        printf("%c%02x", ((i == 0) ? '{' : ' '), event->data[i]);
    }

    printf("}\n");
}

int dump_tracks(struct midi_t* mid)
{
    // itterate over tracks
    for (int i = 0; i < mid->num_tracks; ++i) {
        struct midi_stream_t* stream = midi_stream(mid, i);
        if (!stream) {
            return 1;
        }
        printf("Track %d\n", i);
        struct midi_event_t event;
        while (!midi_stream_end(stream)) {
            if (!midi_event_next(stream, &event)) {
                return 1;
            }
            print_event(&event);
        }
        midi_stream_free(stream);
    }
    return 0;
}

int dump_demux_events(struct midi_t* mid)
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

const char * path = "";

#if defined(_MSC_VER)
LONG NTAPI crash_handler(struct _EXCEPTION_POINTERS *ExceptionInfo) {
    const LPCSTR cmd = GetCommandLineA();
    fprintf(stderr, "exception: %s!\n", cmd);
    exit(1);
    return ExceptionContinueExecution;
}
#endif

int main(const int argc, const char* args[])
{
#if defined(_MSC_VER)
    if (IsDebuggerPresent() == FALSE) {
        AddVectoredExceptionHandler(1, crash_handler);
    }
#endif

//    freopen("null", "wb", stdout);

    if (argc < 2) {
        return 1;
    }
    // load this raw file
    struct file_t file;
    const char *path = args[1];
    if (!file_load(path, &file)) {
        return 1;
    }
    // parse as midi
    struct midi_t* mid = midi_load(file.file_, file.size_);
    if (!mid) {
        return 1;
    }
    int ret_val = 0;
    switch (1 /* mode */) {
    case 0:
        ret_val = dump_tracks(mid);
        break;
    case 1:
        ret_val = dump_demux_events(mid);
        break;
    }
    midi_free(mid);
    // success
    return ret_val;
}
