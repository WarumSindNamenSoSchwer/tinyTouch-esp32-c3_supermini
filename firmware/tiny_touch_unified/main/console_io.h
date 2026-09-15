#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Byte transport for the configuration console.
//
// The ESP32-S3 exposes the console as one interface of the composite TinyUSB
// device. The ESP32-C3 has no USB-OTG peripheral at all, only the fixed-function
// USB-Serial/JTAG controller, so there the console is that controller's CDC
// port. Both cases present the same line-oriented stream to config_console.c.

void console_io_init(void);

// True once a host has opened the port. Writes before this are dropped so a
// disconnected device never blocks on a full buffer.
bool console_io_connected(void);

// Write up to length bytes, returning how many were accepted right now.
size_t console_io_write(const char *data, size_t length);
void console_io_flush(void);

// Read up to capacity bytes without blocking beyond a short poll interval.
size_t console_io_read(char *data, size_t capacity);
