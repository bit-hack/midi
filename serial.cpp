#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "serial.h"

struct serial_t {
  HANDLE handle;
};

static BOOL set_timeouts(HANDLE handle) {
  COMMTIMEOUTS com_timeout;
  ZeroMemory(&com_timeout, sizeof(com_timeout));
  com_timeout.ReadIntervalTimeout = 200;
  com_timeout.ReadTotalTimeoutMultiplier = 10;
  com_timeout.ReadTotalTimeoutConstant = 100;
  com_timeout.WriteTotalTimeoutMultiplier = 200;
  com_timeout.WriteTotalTimeoutConstant = 10;
  return SetCommTimeouts(handle, &com_timeout);
}

static void get_com_error(HANDLE handle) {
  DWORD errors = 0;
  COMSTAT stat = { 0 };
  if (ClearCommError(handle, &errors, &stat)) {
    // read error
  }
}

static bool get_port_name(const char* port, char* out, size_t size) {

  // make out an empty string by default
  *out = '\0';

  // use a user supplied com port name
  if (port) {
    snprintf(out, size, "\\\\.\\%s", port);
    return true;
  }

  // find the first available COM port
  for (int i = 0; i < 256; ++i) {
    char temp[16];
    snprintf(temp, sizeof(temp), "COM%d", i);
    HANDLE h = CreateFile(temp, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      snprintf(out, size, "\\\\.\\COM%d", i);
      return true;
    }
  }

  // no success
  return false;
}

serial_t* serial_open(const char *port, uint32_t baud_rate) {
  // construct com port device name
  char dev_name[32];
  if (!get_port_name(port, dev_name, sizeof(dev_name))) {
    return NULL;
  }
  // open handle to serial device
  HANDLE handle = CreateFileA(
    dev_name,
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    0,
    NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    goto on_error;
  }
  // query serial device control block
  DCB dbc;
  ZeroMemory(&dbc, sizeof(dbc));
  dbc.DCBlength = sizeof(dbc);
  if (GetCommState(handle, &dbc) == FALSE) {
    goto on_error;
  }
  // change baud rate
  if (dbc.BaudRate != baud_rate) {
    dbc.BaudRate = baud_rate;
  }
  dbc.fBinary = TRUE;
  dbc.fParity = FALSE;
  dbc.fOutxCtsFlow = FALSE;
  dbc.fDtrControl = FALSE;
  dbc.ByteSize = 8;
  dbc.fOutX = FALSE;
  dbc.fInX = FALSE;
  dbc.fNull = FALSE;
  dbc.fRtsControl = RTS_CONTROL_DISABLE;
  dbc.Parity = NOPARITY;
  dbc.StopBits = ONESTOPBIT;
  dbc.EofChar = 0;
  dbc.ErrorChar = 0;
  dbc.EvtChar = 0;
  dbc.XonChar = 0;
  dbc.XoffChar = 0;

  if (SetCommState(handle, &dbc) == FALSE) {
    goto on_error;
  }
  // set com timeouts
  if (set_timeouts(handle) == FALSE) {
    goto on_error;
  }
  // wrap in serial object
  serial_t* serial = (serial_t*)malloc(sizeof(serial_t));
  if (serial == NULL) {
    goto on_error;
  }
  serial->handle = handle;
  // success
  return serial;
  // error handler
on_error:
  if (handle != INVALID_HANDLE_VALUE)
    CloseHandle(handle);
  return NULL;
}

void serial_close(serial_t* serial) {
  assert(serial);
  if (serial->handle != INVALID_HANDLE_VALUE) {
    CloseHandle(serial->handle);
  }
  free(serial);
}

uint32_t serial_send(serial_t* serial, const void* src, size_t nbytes) {
  assert(serial && src && nbytes);
  DWORD nb_written = 0;
  if (WriteFile(
    serial->handle,
    src,
    (DWORD)nbytes,
    &nb_written,
    NULL) == FALSE) {
    return 0;
  }

  FlushFileBuffers(serial->handle);

  if (nb_written != nbytes) {
    // DebugBreak();
    get_com_error(serial->handle);
  }
  return nb_written;
}

uint32_t serial_read(serial_t* serial, void* dst, size_t nbytes) {
  assert(serial && dst && nbytes);
  DWORD nb_read = 0;
  if (ReadFile(
    serial->handle,
    dst,
    (DWORD)nbytes,
    &nb_read,
    NULL) == FALSE) {
    return 0;
  }

  if (nb_read != nbytes) {
    // DebugBreak();
    get_com_error(serial->handle);
  }
  return nb_read;
}

void serial_flush(serial_t* serial) {
  FlushFileBuffers(serial->handle);
}
