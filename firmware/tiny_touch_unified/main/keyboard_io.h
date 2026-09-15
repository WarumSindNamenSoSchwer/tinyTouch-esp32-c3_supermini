#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Keyboard transport used to type a password into the focused field.
//
// On the ESP32-S3 this is the HID interface of the composite USB device. The
// ESP32-C3 has no USB-OTG peripheral and therefore cannot present a USB
// keyboard at all, so there the same reports are delivered over BLE HID.

void keyboard_io_init(void);

// True when a host is connected and will accept a report right now.
bool keyboard_io_ready(void);

// Send one boot-keyboard report. keycode 0 releases all keys.
bool keyboard_io_send(uint8_t modifier, uint8_t keycode);
