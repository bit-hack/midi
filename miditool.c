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

int main(const int argc, const char** args)
{
    // load this raw file
    struct file_t file;
    if (!file_load(args[1], &file)) {
        return 1;
    }
    // parse as midi
    struct midi_hdr_t* mid = midi_load(file.file_, file.size_);
    if (!mid) {
        return 1;
    }
    // success
    return 0;
}
