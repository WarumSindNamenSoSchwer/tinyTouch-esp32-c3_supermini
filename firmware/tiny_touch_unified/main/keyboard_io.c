#include "keyboard_io.h"

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// The ESP32-C3 cannot be a USB keyboard: it has no USB-OTG peripheral, only the
// fixed-function USB-Serial/JTAG controller. The same boot-keyboard reports are
// therefore delivered over BLE HID, which the C3 does support in hardware.

#include <string.h>

#include "ble_hid_gap.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb_descriptors.h"

static const char *TAG = "ble_kbd";
static const uint8_t KEYBOARD_REPORT_ID = 1;
static const size_t KEYBOARD_REPORT_LEN = 8;

static esp_hidd_dev_t *hid_device;
static volatile bool host_connected;

// Boot-protocol keyboard: 8 modifier bits, one reserved byte, six key slots.
static const uint8_t keyboard_report_map[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,        //   Usage Minimum (Left Control)
  0x29, 0xE7,        //   Usage Maximum (Right GUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x03,        //   Input (Const,Var,Abs)
  0x95, 0x05,        //   Report Count (5)
  0x75, 0x01,        //   Report Size (1)
  0x05, 0x08,        //   Usage Page (LEDs)
  0x19, 0x01,        //   Usage Minimum (Num Lock)
  0x29, 0x05,        //   Usage Maximum (Kana)
  0x91, 0x02,        //   Output (Data,Var,Abs)
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x03,        //   Report Size (3)
  0x91, 0x03,        //   Output (Const,Var,Abs)
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data,Array,Abs)
  0xC0,              // End Collection
};

static esp_hid_raw_report_map_t report_maps[] = {
  {.data = keyboard_report_map, .len = sizeof(keyboard_report_map)},
};

static esp_hid_device_config_t hid_config = {
  .vendor_id = 0x303a,
  .product_id = 0x4001,
  .version = 0x0100,
  .device_name = "tinyTouch",
  .manufacturer_name = "tinyTouch",
  .serial_number = "tinyTouch",
  .report_maps = report_maps,
  .report_maps_len = 1,
};

static void hidd_event_callback(void *handler_args, esp_event_base_t base,
                                int32_t id, void *event_data) {
  (void)handler_args;
  (void)base;
  switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
      esp_hid_ble_gap_adv_start();
      break;
    case ESP_HIDD_CONNECT_EVENT:
      host_connected = true;
      ESP_LOGI(TAG, "BLE host connected");
      break;
    case ESP_HIDD_DISCONNECT_EVENT:
      host_connected = false;
      ESP_LOGI(TAG, "BLE host disconnected; advertising again");
      // Keep the device discoverable so a host can reconnect after sleep or
      // range loss without a firmware restart.
      esp_hid_ble_gap_adv_start();
      break;
    default:
      break;
  }
}

void keyboard_io_init(void) {
  if (hid_device) return;
  hid_config.device_name = tiny_touch_device_name();
  hid_config.serial_number = tiny_touch_serial_number();
  esp_err_t status = esp_hid_gap_init(ESP_BT_MODE_BLE);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "BLE GAP init failed: %s", esp_err_to_name(status));
    return;
  }
  status = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD,
                                    hid_config.device_name);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "BLE advertising init failed: %s", esp_err_to_name(status));
    return;
  }
  status = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "GATTS callback registration failed: %s", esp_err_to_name(status));
    return;
  }
  status = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE,
                             hidd_event_callback, &hid_device);
  if (status != ESP_OK) {
    hid_device = NULL;
    ESP_LOGE(TAG, "HID device init failed: %s", esp_err_to_name(status));
  }
}

// The GAP layer calls this once a link is authenticated. The
// upstream example uses it to drive a demo task; here it is the authority on
// whether a bonded host can actually receive reports.
void ble_hid_task_start_up(void) { host_connected = true; }

bool keyboard_io_ready(void) { return hid_device != NULL && host_connected; }

bool keyboard_io_send(uint8_t modifier, uint8_t keycode) {
  if (!keyboard_io_ready()) return false;
  uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
  esp_err_t status = esp_hidd_dev_input_set(hid_device, 0, KEYBOARD_REPORT_ID,
                                            report, KEYBOARD_REPORT_LEN);
  memset(report, 0, sizeof(report));
  return status == ESP_OK;
}

#else

#include "class/hid/hid_device.h"
#include "tusb.h"

void keyboard_io_init(void) {}

bool keyboard_io_ready(void) { return tud_hid_ready(); }

bool keyboard_io_send(uint8_t modifier, uint8_t keycode) {
  uint8_t report[6] = {keycode, 0, 0, 0, 0, 0};
  if (keycode == 0) return tud_hid_keyboard_report(0, modifier, NULL);
  return tud_hid_keyboard_report(0, modifier, report);
}

#endif
