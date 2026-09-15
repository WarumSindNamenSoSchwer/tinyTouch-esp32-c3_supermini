#pragma once

#include <stdint.h>

#include "sdkconfig.h"

// Derive the per-device identity from the MAC address. Both transports need it:
// USB puts it in the string descriptors, BLE puts it in the HID device config.
void tiny_touch_init_serial(void);
const char *tiny_touch_serial_number(void);
const char *tiny_touch_device_name(void);

#if !CONFIG_IDF_TARGET_ESP32C3

// USB descriptors exist only on targets with a USB-OTG peripheral. The ESP32-C3
// has none, so it never builds a composite TinyUSB device.

#include "tusb.h"

extern tusb_desc_device_t const tiny_touch_device_descriptor;
extern uint8_t const tiny_touch_configuration_descriptor[];
extern uint8_t const tiny_touch_hid_report_descriptor[];
extern char const *tiny_touch_string_descriptors[];
extern int const tiny_touch_string_descriptor_count;

#endif
