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

static void print_event(const struct midi_event_t* event)
{
    printf("%6llu: [%02x] %02x ",
        (uint64_t)event->delta,
        (uint32_t)event->type,
        (uint32_t)event->channel);
#if AS_HEX || 1
    for (size_t i = 0; i < event->length; ++i) {
        printf("%c%02x", ((i == 0) ? '{' : ' '), event->data[i]);
    }
#else
    printf("{");
    for (size_t i = 0; i < event->length; ++i) {
        const char data = toPrintableAscii(event->data[i]);
        printf("%c", data);
    }
#endif
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

#if defined(_MSC_VER)
LONG NTAPI crash_handler(struct _EXCEPTION_POINTERS *ExceptionInfo) {
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

    freopen("null", "wb", stdout);

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
