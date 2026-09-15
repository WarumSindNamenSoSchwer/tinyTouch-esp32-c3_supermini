#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void touch_pin_hid_start(void);
bool touch_pin_hid_submit_response(const char *response);
void touch_pin_hid_usb_attached(void);
void touch_pin_hid_send_logs(void);
void touch_pin_hid_log_event(const char *event, int value);
// Bring-up diagnostic: type a literal string over the keyboard transport,
// bypassing the fingerprint and host-password path.
bool touch_pin_hid_type_test(const uint8_t *data, size_t length);
