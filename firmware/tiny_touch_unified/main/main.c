#include "esp_err.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

#include "config_console.h"
#include "device_config.h"
#include "fingerprint.h"
#include "piv.h"
#include "touch_pin_hid.h"
#include "keyboard_io.h"
#include "usb_ccid.h"
#include "usb_descriptors.h"

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  tiny_touch_init_serial();
  device_config_init();
  fingerprint_init();
  // Prime the sensor's live-detection state before the HID task begins. This
  // is the same probe STATUS performs; doing it at boot avoids requiring a
  // host status command after USB reconnect before the first fingerprint.
  (void)fingerprint_count();
  piv_init();
  usb_ccid_start(piv_handle_apdu);
  // Bring up the keyboard transport before the HID task starts, so the first
  // fingerprint after boot already has a host to type into.
  keyboard_io_init();
  config_console_start();
  touch_pin_hid_start();
  // All persistent state and runtime services initialized successfully. Keep
  // this OTA slot across later power cycles instead of rolling back once.
  (void)esp_ota_mark_app_valid_cancel_rollback();
}
