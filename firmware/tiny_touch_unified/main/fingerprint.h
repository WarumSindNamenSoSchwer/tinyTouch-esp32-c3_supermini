#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t slot;
  uint16_t score;
} fingerprint_match_t;

void fingerprint_init(void);
bool fingerprint_is_ready(void);
// Bring-up diagnostic: sweep pin orientation and baud rate, reporting each
// attempt into report. Returns true when the sensor answered.
bool fingerprint_probe(char *report, size_t report_cap);
bool fingerprint_recover(void);
bool fingerprint_present_hint(void);
void fingerprint_led_idle(void);
// Direct LED control: function 1..6 (breathe, flash, on, off, fade in/out),
// color bitmask (1 blue, 2 green, 4 red), cycles (0 = forever).
bool fingerprint_led_set(uint8_t function, uint8_t color, uint8_t cycles);
fingerprint_match_t fingerprint_authorize_poll_match(void);
bool fingerprint_authorize_prompted(void (*prompt)(void));
bool fingerprint_prompted_authorization_active(void);
int fingerprint_count(void);
// Bitmap of occupied template slots 1..32 (bit 0 = slot 1). 0 when unavailable.
uint32_t fingerprint_slot_bitmap(void);
bool fingerprint_enroll(uint16_t slot, void (*prompt)(const char *message));
bool fingerprint_delete(uint16_t slot);
bool fingerprint_delete_all(void);
