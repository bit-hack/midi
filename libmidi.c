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

static
void swap(uint8_t* x, uint8_t* y)
{
    const uint8_t t = *x;
                 *x = *y;
                 *y =  t;
}

static
void endian16(uint16_t* x)
{
    uint8_t* p = (uint8_t*)x;
    swap(p + 0, p + 1);
}

static
void endian32(uint32_t* x)
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

static
size_t vlq_read(const uint8_t* restrict in, uint64_t* restrict out)
{
    size_t read = 1;
    uint64_t accum = 0, shl = 0;
    for (;; ++read) {
        const uint64_t c = *(in++);
        accum |= ((uint64_t)(c & 0x7full)) << shl;
        if ((c & e_CMASK) == 0) {
            break;
        }
        shl += 7;
    }
    out ? (*out = accum) : (void)0;
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

struct midi_stream_t* midi_stream(struct midi_t* midi, uint32_t track)
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

static
bool on_meta_event(struct midi_stream_t* stream, struct midi_event_t* event)
{
    const uint8_t type = *(stream->ptr++);

    uint64_t vlq_value;
    const size_t vlq_size = vlq_read(stream->ptr, &vlq_value);
    stream->ptr += vlq_size;

    event->length = vlq_size;
    event->data = stream->ptr;

    if (type == 0x0 /* Sequence Number */) {
        assert(vlq_value == 2);
        stream->ptr += vlq_value;
        return true;
    }
    if (type >= 0x1 && type <= 0x7 /* Text */) {
        stream->ptr += vlq_value;
        return true;
    }
    if (type == 0x20 /* Midi Channel Prefix */) {
        assert(vlq_value == 1);
        stream->ptr += vlq_value;
        return true;
    }
    if (type == 0x2f /* End of Track */) {
        assert(vlq_value == 0);
        // HACK (reset the stream pointer)
        stream->ptr -= 4;
        event->length = 0;
        return true;
    }
    if (type == 0x51 /* Set Tempo */) {
        assert(vlq_value == 3);
        stream->ptr += vlq_value;
        return true;
    }
    if (type == 0x58 /* Time Signature */) {
        assert(vlq_value == 4);
        const uint8_t nn = stream->ptr[0];
        const uint8_t dd = stream->ptr[1];
        const uint8_t cc = stream->ptr[2];
        const uint8_t bb = stream->ptr[3];
        stream->ptr += vlq_value;
        return true;
    }
    if (type == 0x59 /* Key Signature */) {
        assert(vlq_value == 2);
        const uint8_t sf = stream->ptr[0];
        const uint8_t mi = stream->ptr[1];
        stream->ptr += vlq_value;
        return true;
    }
    assert(!"Unknown meta event");
    return false;
}

static
bool on_midi_sysex(struct midi_stream_t* stream, struct midi_event_t* event)
{
    switch (event->channel) {
//  case 0x03: /* song select */
    case 0x07: /* escape sequence */ {
        uint64_t vlq_value = 0;
        const uint64_t vlq_size = vlq_read(stream->ptr, &vlq_value);
        event->length = (vlq_size + vlq_value);
        stream->ptr += event->length;
        return true;
    }
    case 0x0F: /* meta event */ {
        return on_meta_event(stream, event);
    }
    default:
        assert(!"not implemented");
    }
    return false;
}

static
bool on_midi_cc(struct midi_stream_t* stream, struct midi_event_t* event)
{
    const uint8_t index = stream->ptr[0];
    const uint8_t value = stream->ptr[1];
    // MSB should not be set
    assert(!((index & 0x80) || (value & 0x80)));
    if (index >= 120) {
        assert(!"TODO: channel mode");
    } else {
        stream->ptr += (event->length = 2);
    }
    return true;
}

bool midi_next_event(struct midi_stream_t* stream, struct midi_event_t* event)
{
    // parse midi stream event
    if (stream->ptr >= stream->end) {
        return false;
    }
    // parse delta time
    const size_t vlq_size = vlq_read(stream->ptr, &(event->delta));
    stream->ptr += vlq_size;
    // midi parse event byte
    const uint8_t cmd = *(stream->ptr++);
    if ((cmd & 0x80) == 0) {
        return false;
    }

    event->type = cmd & 0xf0;
    event->channel = cmd & 0x0f;
    event->data = stream->ptr;
    event->length = 0;

    // parse for length
    switch (event->type) {
    case e_midi_event_note_off:
    case e_midi_event_note_on:
    case e_midi_event_poly_aftertouch:
    case e_midi_event_pitch_wheel:
        stream->ptr += (event->length = 2);
        return true;
    case e_midi_event_prog_change:
    case e_midi_event_chan_aftertouch:
        stream->ptr += (event->length = 1);
        return true;
    case e_midi_event_sysex:
        return on_midi_sysex(stream, event);
    case e_midi_event_ctrl_change:
        return on_midi_cc(stream, event);
    default:
        assert(!"unknown event type");
    }
    return false;
}
