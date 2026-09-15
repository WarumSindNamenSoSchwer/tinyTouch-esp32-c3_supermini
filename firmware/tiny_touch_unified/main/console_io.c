#include "console_io.h"

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// The ESP32-C3 has no USB-OTG peripheral. Its USB port is the fixed-function
// USB-Serial/JTAG controller, which always presents one CDC interface. The
// console therefore speaks to that controller directly instead of TinyUSB.

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define USJ_RX_BUFFER 1024
#define USJ_TX_BUFFER 1024

static bool driver_ready;

void console_io_init(void) {
  if (driver_ready) return;
  usb_serial_jtag_driver_config_t config = {
    .rx_buffer_size = USJ_RX_BUFFER,
    .tx_buffer_size = USJ_TX_BUFFER,
  };
  driver_ready = usb_serial_jtag_driver_install(&config) == ESP_OK;
}

bool console_io_connected(void) {
  // usb_serial_jtag_is_connected() is backed by the console connection monitor,
  // which this build does not install: the IDF console is disabled so that log
  // output cannot interleave with the tinyTouch protocol on the same port. A
  // bounded write timeout already prevents an unplugged device from stalling
  // the console task, so treat the driver as writable whenever it is installed.
  return driver_ready;
}

size_t console_io_write(const char *data, size_t length) {
  if (!driver_ready) return 0;
  int written = usb_serial_jtag_write_bytes(data, length, pdMS_TO_TICKS(20));
  return written > 0 ? (size_t)written : 0;
}

void console_io_flush(void) {
  // usb_serial_jtag_write_bytes already hands the data to the driver's TX ring,
  // which the driver drains on its own. There is no separate flush to perform.
}

size_t console_io_read(char *data, size_t capacity) {
  if (!driver_ready) return 0;
  int count = usb_serial_jtag_read_bytes(data, capacity, 0);
  return count > 0 ? (size_t)count : 0;
}

#else

#include "tusb.h"

void console_io_init(void) {}

bool console_io_connected(void) { return tud_cdc_connected(); }

size_t console_io_write(const char *data, size_t length) {
  uint32_t request = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length;
  return tud_cdc_write(data, request);
}

void console_io_flush(void) { tud_cdc_write_flush(); }

size_t console_io_read(char *data, size_t capacity) {
  if (!tud_cdc_available()) return 0;
  uint32_t request = capacity > UINT32_MAX ? UINT32_MAX : (uint32_t)capacity;
  return tud_cdc_read(data, request);
}

#endif
