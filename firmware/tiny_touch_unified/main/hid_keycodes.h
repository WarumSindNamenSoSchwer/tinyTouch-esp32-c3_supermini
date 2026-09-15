#pragma once

#include <stdint.h>

// Minimal HID keyboard constants for targets that do not build TinyUSB.
//
// The ESP32-S3 build takes these from TinyUSB's class/hid/hid.h. The ESP32-C3
// has no USB-OTG peripheral, so TinyUSB is not part of that build at all, yet
// the same USB HID usage IDs are still what a BLE keyboard reports. Keeping the
// names identical lets touch_pin_hid.c stay target independent.

#define KEYBOARD_MODIFIER_LEFTCTRL 0x01
#define KEYBOARD_MODIFIER_LEFTSHIFT 0x02
#define KEYBOARD_MODIFIER_LEFTALT 0x04
#define KEYBOARD_MODIFIER_LEFTGUI 0x08

#define HID_KEY_ENTER 0x28

// {shift, keycode} for every ASCII code point, US layout. Entries with keycode
// zero are not typeable and are rejected before any key is emitted.
#define HID_ASCII_TO_KEYCODE                    \
  {0, 0}, /* 0x00 Null      */                  \
  {0, 0}, /* 0x01           */                  \
  {0, 0}, /* 0x02           */                  \
  {0, 0}, /* 0x03           */                  \
  {0, 0}, /* 0x04           */                  \
  {0, 0}, /* 0x05           */                  \
  {0, 0}, /* 0x06           */                  \
  {0, 0}, /* 0x07           */                  \
  {0, 0x2A}, /* 0x08 Backspace */               \
  {0, 0x2B}, /* 0x09 Tab       */               \
  {0, 0x28}, /* 0x0A Line Feed */               \
  {0, 0}, /* 0x0B           */                  \
  {0, 0}, /* 0x0C           */                  \
  {0, 0x28}, /* 0x0D CR        */               \
  {0, 0}, /* 0x0E           */                  \
  {0, 0}, /* 0x0F           */                  \
  {0, 0}, /* 0x10           */                  \
  {0, 0}, /* 0x11           */                  \
  {0, 0}, /* 0x12           */                  \
  {0, 0}, /* 0x13           */                  \
  {0, 0}, /* 0x14           */                  \
  {0, 0}, /* 0x15           */                  \
  {0, 0}, /* 0x16           */                  \
  {0, 0}, /* 0x17           */                  \
  {0, 0}, /* 0x18           */                  \
  {0, 0}, /* 0x19           */                  \
  {0, 0}, /* 0x1A           */                  \
  {0, 0x29}, /* 0x1B Escape    */               \
  {0, 0}, /* 0x1C           */                  \
  {0, 0}, /* 0x1D           */                  \
  {0, 0}, /* 0x1E           */                  \
  {0, 0}, /* 0x1F           */                  \
                                                \
  {0, 0x2C}, /* 0x20 Space     */               \
  {1, 0x1E}, /* 0x21 !         */               \
  {1, 0x34}, /* 0x22 "         */               \
  {1, 0x20}, /* 0x23 #         */               \
  {1, 0x21}, /* 0x24 $         */               \
  {1, 0x22}, /* 0x25 %         */               \
  {1, 0x24}, /* 0x26 &         */               \
  {0, 0x34}, /* 0x27 '         */               \
  {1, 0x26}, /* 0x28 (         */               \
  {1, 0x27}, /* 0x29 )         */               \
  {1, 0x25}, /* 0x2A *         */               \
  {1, 0x2E}, /* 0x2B +         */               \
  {0, 0x36}, /* 0x2C ,         */               \
  {0, 0x2D}, /* 0x2D -         */               \
  {0, 0x37}, /* 0x2E .         */               \
  {0, 0x38}, /* 0x2F /         */               \
  {0, 0x27}, /* 0x30 0         */               \
  {0, 0x1E}, /* 0x31 1         */               \
  {0, 0x1F}, /* 0x32 2         */               \
  {0, 0x20}, /* 0x33 3         */               \
  {0, 0x21}, /* 0x34 4         */               \
  {0, 0x22}, /* 0x35 5         */               \
  {0, 0x23}, /* 0x36 6         */               \
  {0, 0x24}, /* 0x37 7         */               \
  {0, 0x25}, /* 0x38 8         */               \
  {0, 0x26}, /* 0x39 9         */               \
  {1, 0x33}, /* 0x3A :         */               \
  {0, 0x33}, /* 0x3B ;         */               \
  {1, 0x36}, /* 0x3C <         */               \
  {0, 0x2E}, /* 0x3D =         */               \
  {1, 0x37}, /* 0x3E >         */               \
  {1, 0x38}, /* 0x3F ?         */               \
                                                \
  {1, 0x1F}, /* 0x40 @         */               \
  {1, 0x04}, /* 0x41 A         */               \
  {1, 0x05}, /* 0x42 B         */               \
  {1, 0x06}, /* 0x43 C         */               \
  {1, 0x07}, /* 0x44 D         */               \
  {1, 0x08}, /* 0x45 E         */               \
  {1, 0x09}, /* 0x46 F         */               \
  {1, 0x0A}, /* 0x47 G         */               \
  {1, 0x0B}, /* 0x48 H         */               \
  {1, 0x0C}, /* 0x49 I         */               \
  {1, 0x0D}, /* 0x4A J         */               \
  {1, 0x0E}, /* 0x4B K         */               \
  {1, 0x0F}, /* 0x4C L         */               \
  {1, 0x10}, /* 0x4D M         */               \
  {1, 0x11}, /* 0x4E N         */               \
  {1, 0x12}, /* 0x4F O         */               \
  {1, 0x13}, /* 0x50 P         */               \
  {1, 0x14}, /* 0x51 Q         */               \
  {1, 0x15}, /* 0x52 R         */               \
  {1, 0x16}, /* 0x53 S         */               \
  {1, 0x17}, /* 0x54 T         */               \
  {1, 0x18}, /* 0x55 U         */               \
  {1, 0x19}, /* 0x56 V         */               \
  {1, 0x1A}, /* 0x57 W         */               \
  {1, 0x1B}, /* 0x58 X         */               \
  {1, 0x1C}, /* 0x59 Y         */               \
  {1, 0x1D}, /* 0x5A Z         */               \
  {0, 0x2F}, /* 0x5B [         */               \
  {0, 0x31}, /* 0x5C \         */               \
  {0, 0x30}, /* 0x5D ]         */               \
  {1, 0x23}, /* 0x5E ^         */               \
  {1, 0x2D}, /* 0x5F _         */               \
                                                \
  {0, 0x35}, /* 0x60 `         */               \
  {0, 0x04}, /* 0x61 a         */               \
  {0, 0x05}, /* 0x62 b         */               \
  {0, 0x06}, /* 0x63 c         */               \
  {0, 0x07}, /* 0x64 d         */               \
  {0, 0x08}, /* 0x65 e         */               \
  {0, 0x09}, /* 0x66 f         */               \
  {0, 0x0A}, /* 0x67 g         */               \
  {0, 0x0B}, /* 0x68 h         */               \
  {0, 0x0C}, /* 0x69 i         */               \
  {0, 0x0D}, /* 0x6A j         */               \
  {0, 0x0E}, /* 0x6B k         */               \
  {0, 0x0F}, /* 0x6C l         */               \
  {0, 0x10}, /* 0x6D m         */               \
  {0, 0x11}, /* 0x6E n         */               \
  {0, 0x12}, /* 0x6F o         */               \
  {0, 0x13}, /* 0x70 p         */               \
  {0, 0x14}, /* 0x71 q         */               \
  {0, 0x15}, /* 0x72 r         */               \
  {0, 0x16}, /* 0x73 s         */               \
  {0, 0x17}, /* 0x74 t         */               \
  {0, 0x18}, /* 0x75 u         */               \
  {0, 0x19}, /* 0x76 v         */               \
  {0, 0x1A}, /* 0x77 w         */               \
  {0, 0x1B}, /* 0x78 x         */               \
  {0, 0x1C}, /* 0x79 y         */               \
  {0, 0x1D}, /* 0x7A z         */               \
  {1, 0x2F}, /* 0x7B {         */               \
  {1, 0x31}, /* 0x7C |         */               \
  {1, 0x30}, /* 0x7D }         */               \
  {1, 0x35}, /* 0x7E ~         */               \
  {0, 0x4C}, /* 0x7F Delete    */
