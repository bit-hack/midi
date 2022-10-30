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

#include <mmeapi.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libmidi.h"
#include "midiplay.h"


// ----------------------------------------------------------------------------
// Microsoft Midi out device
// ----------------------------------------------------------------------------

// the output midi device handle
static HMIDIOUT hmidiout;

static bool device_windows_open(void)
{
    MMRESULT res = { 0 };

    UINT numDevices = midiOutGetNumDevs();
    if (numDevices == 0) {
        fprintf(stderr, "midiOutGetNumDevs() reports no midi devices\n");
        return false;
    }

    MIDIOUTCAPS caps = { 0 };
    res = midiOutGetDevCaps(0, &caps, sizeof(caps));
    if (res != MMSYSERR_NOERROR) {
        fprintf(stderr, "midiOutGetDevCaps() failed\n");
        return false;
    }
    printf("Using MIDI device '%s'\n", caps.szPname);

    // default to midi device 0 here which is the SW Synth on my PC
    UINT deviceid = 0;
    res = midiOutOpen(&hmidiout, deviceid, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        fprintf(stderr, "midiOutOpen() failed\n");
        return false;
    }

    return true;
}

static void device_windows_send(const struct midi_event_t* event)
{
    union {
        DWORD dwData;
        BYTE bData[4];
    } u;

    u.bData[0] = (event->type & 0xf0) | (event->channel & 0x0f);
    u.bData[1] = event->data[0];
    u.bData[2] = event->data[1];
    u.bData[3] = 0;

    midiOutShortMsg(hmidiout, u.dwData);
}

static void device_windows_close(void)
{
    midiOutClose(hmidiout);
}

void device_windows_select(void)
{
    device_open  = device_windows_open;
    device_send  = device_windows_send;
    device_close = device_windows_close;
}
