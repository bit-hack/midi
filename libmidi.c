//  ____     _____________      _____   ___________   ___
// |    |\  |   \______   \    /     \ |   \______ \ |   |\
// |    ||  |   ||    |  _/\  /  \ /  \|   ||    |  \|   ||
// |    ||__|   ||    |   \/ /    Y    \   ||    `   \   ||
// |________\___||________/\ \____|____/___/_________/___||
//  \________\___\________\/  \____\____\__\_________\____\

#if defined(_MSC_VER) && defined(_DEBUG)
#include <stdio.h>
#define assert(EXPR) {                                      \
  if (!(EXPR)) {                                            \
    fprintf(stderr, "%s:%u: assert\n", __FILE__, __LINE__); \
    __debugbreak();                                         \
  }                                                         \
}
#else
#include <assert.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "libmidi.h"


#if defined(STRICT_MIDI)
#define strict(EXPR) assert(EXPR)
#else
#define strict(EXPR) (void)(EXPR)
#endif

#define FOURCC(a, b, c, d) ( \
     ((a) <<  0) |           \
     ((b) <<  8) |           \
     ((c) << 16) |           \
     ((d) << 24))

enum {
    CC_MThd = FOURCC('M', 'T', 'h', 'd'),
    CC_MTrk = FOURCC('M', 'T', 'r', 'k'),
    CC_RIFF = FOURCC('R', 'I', 'F', 'F'),
    CC_RMID = FOURCC('R', 'M', 'I', 'D'),
    CC_data = FOURCC('d', 'a', 't', 'a'),
};
#undef FOURCC

#define MEMCPY(dst, src, len)              \
    {                                      \
        memcpy(dst, src, len), src += len; \
    }

struct midi_stream_t {
    uint8_t prevEvent;
    const uint8_t* ptr;
    const uint8_t* end;
};

static void swap(uint8_t* x, uint8_t* y)
{
    const uint8_t t = *x;
    *x = *y;
    *y = t;
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

static const uint32_t read32(const uint8_t **ptr, const uint8_t *end)
{
    if (*ptr >= end)
        return 0;
    const uint32_t out = *(uint32_t*)(*ptr);
    *ptr += 4;
    return out;
}

static bool skip_riff_header(const uint8_t **ptr, size_t size)
{
    const uint8_t *end = (*ptr) + size, *temp = *ptr;
    const uint32_t fourcc = read32(&temp, end);
    if (fourcc == CC_RIFF) {
        const uint32_t riff_size = read32(&temp, end);
        if ((temp + riff_size) > end)
            return false;
        if (read32(&temp, end) != CC_RMID)
            return false;
        if (read32(&temp, end) != CC_data)
            return false;
        const uint32_t data_size = read32(&temp, end);
        if ((temp + data_size) > end)
            return false;
        // update read pointer
        *ptr = temp;
    }
    return true;
}

struct midi_t* midi_load(const void* data, size_t size)
{
#define TRY(EXPR)       \
    {                   \
        if (!(EXPR))    \
            goto error; \
    }
    const uint8_t* ptr = (const uint8_t*)data;
    const uint8_t* const end = ptr + size;
    if (size < 14)
        return NULL;
    // skip RIFF header if present
    if (!skip_riff_header(&ptr, size))
        return false;
    // allocate header
    struct midi_t* hdr = malloc(sizeof(struct midi_t));
    assert(hdr);
    memset(hdr, 0, sizeof(struct midi_t));
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
    stream->prevEvent = 0xff;
    return stream;
}

void midi_stream_free(struct midi_stream_t* stream)
{
    assert(stream);
    free(stream);
}

static bool stream_overflow(struct midi_stream_t* stream)
{
    if (stream->ptr > stream->end) {
        stream->ptr = stream->end;
        return true;
    }
    return false;
}

static bool on_meta_event(struct midi_stream_t* stream, struct midi_event_t* event)
{
    assert(stream && event);
    // read meta event type
    const uint8_t type = *(stream->ptr++);
    // read meta data size
    uint64_t vlq_value;
    const size_t vlq_size = vlq_read(stream->ptr, &vlq_value);
    stream->ptr += vlq_size;
    // set event data
    event->type = e_midi_event_meta;
    event->length = vlq_value;
    event->data = stream->ptr;
    event->meta = type;
    // step over meta event data
    stream->ptr += vlq_value;
    if (stream_overflow(stream))
        return false;

    if (type == 0x0 /* Sequence Number */) {
        strict(vlq_value == 2);
        return true;
    }
    if (type >= 0x1 && type <= 0x7 /* Text */) {
        return true;
    }
    if (type == 0x20 /* Midi Channel Prefix */) {
        strict(vlq_value == 1);
        return true;
    }
    if (type == 0x21 /* Midi port */) {
        strict(vlq_value == 1);
        return true;
    }
    if (type == 0x2f /* End of Track */) {
        strict(vlq_value == 0);
        // force stream end
        stream->ptr = stream->end;
        return true;
    }
    if (type == 0x51 /* Set Tempo */) {
        strict(vlq_value == 3);
        return true;
    }
    if (type == 0x54 /* smpte offset */) {
        strict(vlq_value == 5);
        return true;
    }
    if (type == 0x58 /* Time Signature */) {
        strict(vlq_value == 4);
        return true;
    }
    if (type == 0x59 /* Key Signature */) {
        strict(vlq_value == 2);
        return true;
    }
    strict(!"Unknown meta event");
    return true;
}

static bool on_midi_sysex(struct midi_stream_t* stream, struct midi_event_t* event)
{
    assert(stream && event);
    uint64_t vlq_value = 0, vlq_size = 0;
    switch (event->channel) {
    case 0x0: /* single complete SysEx message  */
        vlq_size = vlq_read(stream->ptr, &vlq_value);
        event->length = (vlq_size + vlq_value);
        stream->ptr += event->length;
        return !stream_overflow(stream);
    case 0x07: /* escape sequence */
        vlq_size = vlq_read(stream->ptr, &vlq_value);
        event->length = (vlq_size + vlq_value);
        stream->ptr += event->length;
        return !stream_overflow(stream);
    case 0x0F: /* meta event */
        return on_meta_event(stream, event);
    default: /* unknown sysex event */
        strict(!"Unknown SysEx message");
        vlq_size = vlq_read(stream->ptr, &vlq_value);
        event->length = (vlq_size + vlq_value);
        stream->ptr += event->length;
        return !stream_overflow(stream);
    }
}

static bool on_midi_cc(struct midi_stream_t* stream, struct midi_event_t* event)
{
    assert(stream && event);
    const uint8_t index = stream->ptr[0];
    const uint8_t value = stream->ptr[1];
    // MSB should not be set
    const int msb_set = (index & 0x80) | (value & 0x80);
    strict(msb_set == 0);
    if (msb_set)
        return false;
    if (index >= 120) {
        event->type = e_midi_event_channel_mode;
    }
    stream->ptr += (event->length = 2);
    return !stream_overflow(stream);
}

bool midi_event_peek(struct midi_stream_t* stream, struct midi_event_t* event)
{
    assert(stream && event);
    struct midi_stream_t temp = *stream;
    return midi_event_next(&temp, event);
}

bool midi_event_delta(struct midi_stream_t* stream, uint64_t* delta)
{
    assert(stream && delta);
    if (stream->ptr >= stream->end) {
        // stream has ended
        return false;
    }
    // parse delta time
    vlq_read(stream->ptr, delta);
    return !stream_overflow(stream);
}

bool midi_event_next(struct midi_stream_t* stream, struct midi_event_t* event)
{
    assert(stream && event);
    if (stream->ptr >= stream->end) {
        // stream has ended
        return false;
    }
    // parse delta time
    const size_t vlq_size = vlq_read(stream->ptr, &(event->delta));
    stream->ptr += vlq_size;
    // midi parse event byte
    uint8_t cmd = *(stream->ptr);
    if (cmd & 0x80) {
        stream->prevEvent = cmd;
        stream->ptr++;
    } else {
        // continuation of previous event
        cmd = stream->prevEvent;
    }
    // output known event data
    event->type    = cmd & 0xf0;
    event->channel = cmd & 0x0f;
    event->data    = stream->ptr;
    event->length  = 0;
    event->meta    = 0;
    // parse for length
    switch (event->type) {
    case e_midi_event_note_off:
    case e_midi_event_note_on:
    case e_midi_event_poly_aftertouch:
    case e_midi_event_pitch_wheel:
        stream->ptr += (event->length = 2);
        break;
    case e_midi_event_prog_change:
    case e_midi_event_chan_aftertouch:
        stream->ptr += (event->length = 1);
        break;
    case e_midi_event_sysex:
        return on_midi_sysex(stream, event);
    case e_midi_event_ctrl_change:
        return on_midi_cc(stream, event);
    default:
        assert(!"unknown event type");
    }
    return !stream_overflow(stream);
}

bool midi_stream_end(struct midi_stream_t* stream)
{
    assert(stream);
    return stream->ptr == stream->end;
}

bool midi_stream_mux(
    struct midi_stream_t** stream,
    uint64_t* delta,
    const size_t count,
    struct midi_event_t* event,
    uint64_t* delta_out,
    size_t* index)
{
    assert(stream && delta && count && event);
    static const uint64_t invalid = ~0llu;
    struct midi_stream_t* next = NULL;
    uint64_t* next_delta = NULL;
    uint64_t min_delta = invalid;
    // select track with nearest pending event
    for (size_t i = 0; i < count; ++i) {
        uint64_t cur_delta;
        if (!midi_event_delta(stream[i], &cur_delta)) {
            // unable to parse, stream likely ended
            continue;
        }
        cur_delta += delta[i];
        if (cur_delta >= min_delta) {
            // event is not closest pending
            continue;
        }
        min_delta = cur_delta;
        // save new stream pointers
        next = stream[i];
        next_delta = delta + i;
        *index = i;
    }
    // will be null if all tracks have ended
    if (next == NULL || next_delta == NULL) {
        return false;
    }
    // parse midi event from stream
    if (!midi_event_next(next, event)) {
        return false;
    }
    // save event timing
    *next_delta = min_delta;
    *delta_out = min_delta;
    return true;
}
