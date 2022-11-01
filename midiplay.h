//  ____     _____________      _____   ___________   ___
// |    |\  |   \______   \    /     \ |   \______ \ |   |\
// |    ||  |   ||    |  _/\  /  \ /  \|   ||    |  \|   ||
// |    ||__|   ||    |   \/ /    Y    \   ||    `   \   ||
// |________\___||________/\ \____|____/___/_________/___||
//  \________\___\________\/  \____\____\__\_________\____\

#pragma once

#include "libmidi.h"

typedef bool (*device_open_t )(void);
typedef void (*device_close_t)(void);
typedef void (*device_send_t )(const struct midi_event_t* event);

extern device_open_t  device_open;
extern device_close_t device_close;
extern device_send_t  device_send;

void device_windows_select(void);
void device_adlib_select  (void);

void oplpi_write(uint8_t reg, uint8_t byte);
void oplpi_init();
