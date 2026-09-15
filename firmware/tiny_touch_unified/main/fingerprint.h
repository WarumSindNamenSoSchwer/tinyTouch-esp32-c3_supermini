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
fingerprint_match_t fingerprint_authorize_poll_match(void);
bool fingerprint_authorize_prompted(void (*prompt)(void));
bool fingerprint_prompted_authorization_active(void);
int fingerprint_count(void);
bool fingerprint_enroll(uint16_t slot, void (*prompt)(const char *message));
bool fingerprint_delete(uint16_t slot);
bool fingerprint_delete_all(void);
