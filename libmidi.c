#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "libmidi.h"

#define FOURCC(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

#define CC_MThd FOURCC('M', 'T', 'h', 'd')
#define CC_MTrk FOURCC('M', 'T', 'r', 'k')

#define MEMCPY(dst, src, len)              \
    {                                      \
        memcpy(dst, src, len), src += len; \
    }

struct midi_stream_t {
    const uint8_t* ptr;
    const uint8_t* end;
};

static void swap(uint8_t* x, uint8_t* y)
{
    (*x = *x ^ *y), (*y = *y ^ *x), (*x = *x ^ *y);
}

static void endian16(uint16_t* x)
{
    uint8_t* p = (uint8_t*)x;
    swap(p + 0, p + 1);
}

static void endian32(uint32_t* x)
{
    uint8_t* p = (uint8_t*)x;
    swap(p + 0, p + 3);
    swap(p + 1, p + 2);
}

#define ENDIAN16(PTR)     \
    {                     \
        endian16(&(PTR)); \
    }
#define ENDIAN32(PTR)     \
    {                     \
        endian32(&(PTR)); \
    }

enum {
    e_EMASK = 0x00, // MSB mask for end of VLQ
    e_CMASK = 0x80, // MSB mask for continuation of VLQ
};

#if defined(_MSC_VER)
#define restrict __restrict
#endif

static size_t vlq_read(const uint8_t* restrict in, uint64_t* restrict out)
{
    size_t read = 1;
    uint64_t accum = 0, shl = 0;
    for (;;++read) {
        const uint64_t c = *(in++);
        accum |= ((uint64_t)(c & 0x7full)) << shl;
        if ((c & e_CMASK) == 0) {
            break;
        }
        shl += 7;
    }
    *out = accum;
    return read;
}

struct midi_t* midi_load(void* restrict data, size_t size)
{
#define TRY(EXPR)       \
    {                   \
        if (!(EXPR))    \
            goto error; \
    }
    const uint8_t* ptr = (const uint8_t*)data;
    const uint8_t* const end = ptr + size;
    TRY(size >= 14);
    // allocate header
    struct midi_t* hdr = malloc(sizeof(struct midi_t));
    assert(hdr);
    hdr->tracks = NULL;
    // extract headers
    MEMCPY(hdr, ptr, 14);
    // endian swap
    ENDIAN32(hdr->length);
    ENDIAN16(hdr->format);
    ENDIAN16(hdr->num_tracks);
    ENDIAN16(hdr->divisions);
    // sanity checks
    TRY(hdr->mthd == CC_MThd);
    TRY(hdr->format <= e_midi_fmt_multi_song);
    TRY(hdr->num_tracks);
    // collect tracks
    hdr->tracks = malloc(sizeof(struct midi_track_t) * hdr->num_tracks);
    assert(hdr->tracks);
    for (uint32_t i = 0; i < hdr->num_tracks; ++i) {
        struct midi_track_t* trk = hdr->tracks + i;
        MEMCPY(trk, ptr, 8);
        // endian swap
        ENDIAN32(trk->length);
        // sanity checks
        TRY(trk->mtrk == CC_MTrk);
        // copy out track data
        trk->data = ptr;
        ptr += trk->length;
        TRY(ptr <= end);
    }
    // return header
    return hdr;
error:
    // error handler
    assert(hdr);
    free(hdr->tracks), free(hdr);
    return NULL;
#undef TRY
}

void midi_free(struct midi_t* midi)
{
    assert(midi);
    free(midi->tracks);
    free(midi);
}

struct midi_stream_t* midi_stream(struct midi_t* restrict midi, uint32_t track)
{
    assert(midi);
    if (track >= midi->num_tracks) {
        return NULL;
    }
    struct midi_stream_t* stream = malloc(sizeof(struct midi_stream_t));
    assert(stream);
    const struct midi_track_t* trk = midi->tracks + track;
    assert(trk->data);
    stream->ptr = trk->data;
    stream->end = trk->data + trk->length;
    return stream;
}

void midi_stream_free(struct midi_stream_t* stream)
{
    free(stream);
}

bool midi_next_event(struct midi_stream_t* stream, struct midi_event_t * event)
{
    const uint8_t * ptr = stream->ptr;
    ptr += vlq_read(ptr, &(event->delta));
    event->data = ptr;

    

    return false;
}
