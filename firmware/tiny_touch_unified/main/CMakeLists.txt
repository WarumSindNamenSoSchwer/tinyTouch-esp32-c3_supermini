# The ESP32-C3 has no USB-OTG peripheral, so it cannot build TinyUSB. It uses
# the USB-Serial/JTAG console and a BLE HID keyboard instead.
# IDF_TARGET is set before Kconfig is read, so it is the value available during
# early requirement expansion; CONFIG_IDF_TARGET_* is not yet defined here.
if(IDF_TARGET STREQUAL "esp32c3")
  set(TINYTOUCH_TRANSPORT_SRCS "ble_hid_gap.c" "usb_ccid.c" "usb_descriptors.c")
  set(TINYTOUCH_TRANSPORT_REQUIRES esp_driver_usb_serial_jtag esp_hid bt)
else()
  set(TINYTOUCH_TRANSPORT_SRCS "usb_ccid.c" "usb_descriptors.c")
  set(TINYTOUCH_TRANSPORT_REQUIRES esp_tinyusb)
endif()

idf_component_register(
  SRCS
    "main.c"
    "config_console.c"
    "console_io.c"
    "keyboard_io.c"
    "device_config.c"
    "fingerprint.c"
    "firmware_update.c"
    "piv.c"
    "touch_pin_hid.c"
    ${TINYTOUCH_TRANSPORT_SRCS}
  INCLUDE_DIRS "."
  REQUIRES app_update driver esp_timer mbedtls nvs_flash ${TINYTOUCH_TRANSPORT_REQUIRES}
  PRIV_REQUIRES esp_driver_uart
)

target_compile_definitions(
  ${COMPONENT_LIB} PRIVATE
  TINYTOUCH_FIRMWARE_VERSION="${TINYTOUCH_VERSION}"
  TINYTOUCH_BUILD_ID="${TINYTOUCH_BUILD_ID}"
  TINYTOUCH_PROTOCOL_VERSION=6
)

if(TINYTOUCH_DEVELOPMENT_SKIP_FINGERPRINT_AUTH)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYTOUCH_DEVELOPMENT_SKIP_FINGERPRINT_AUTH=1)
endif()
