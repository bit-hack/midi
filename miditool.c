#define _CRT_SECURE_NO_WARNINGS

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

static
bool file_load(const char* path, struct file_t* out)
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

static
char toPrintableAscii(const char ch)
{
    return (ch >= 32 && ch <= 126) ? ch : '.';
}

int main(const int argc, const char* args[])
{
    // load this raw file
    struct file_t file;
    if (!file_load(args[1], &file)) {
        return 1;
    }
    // parse as midi
    struct midi_t* mid = midi_load(file.file_, file.size_);
    if (!mid) {
        return 1;
    }
    // parse all tracks
    for (int i = 0; i < mid->num_tracks; ++i) {
        struct midi_stream_t* stream = midi_stream(mid, i);
        if (!stream) {
            return 1;
        }
        printf("Track %d\n", i);
        struct midi_event_t event;
        while (!midi_stream_end(stream)) {
            if (!midi_next_event(stream, &event)) {
                return 1;
            }
            printf("%6llu: [%02x] %02x ",
                (uint64_t)event.delta,
                (uint32_t)event.type,
                (uint32_t)event.channel);
#if AS_HEX
            for (size_t i = 0; i < event.length; ++i) {
                printf("%c%02x", ((i == 0) ? '{' : ' '), event.data[i]);
            }
#else
            printf("{");
            for (size_t i = 0; i < event.length; ++i) {
                const char data = toPrintableAscii(event.data[i]);
                printf("%c", data);
            }
#endif
            printf("}\n");
        }
    }
    // success
    return 0;
}
