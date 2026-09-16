# tinyTouch v2 — custom PCB design specification

Status: design specification, ready for schematic capture.
Owner: Muri. Derived from the working ESP32-C3 SuperMini prototype.

## goals

1. One clean board instead of a dev-board sandwich.
2. Wireless first: battery powered, BLE, works without a USB cable.
3. USB-C as a full function port: charging, flashing, USB HID keyboard,
   USB CCID smart card (PIV), serial console. Pass-through, not charge-only.
4. Wake on approach/handling: the sensor LED stays dark until the device is
   picked up or touched.
5. Real security: secrets survive neither board theft nor disassembly.
   Encrypted at rest, encrypted in transit, keys in silicon.
6. PIV "ID card" function on macOS and Windows.
7. Host apps for macOS and Windows.

## block diagram

```
                                 ┌────────────────────┐
        USB-C ──────────────────►│ power path + charger│──► LiPo 503035 (500 mAh)
          │                      │ BQ24074             │
          │  D+/D-               └─────────┬──────────┘
          │                                │ VSYS
          │                      ┌─────────▼──────────┐
          │                      │ 3V3 buck-boost      │──► all rails
          │                      │ TPS63901            │
          │                      └────────────────────┘
          │
┌─────────▼──────────┐  UART1   ┌────────────────────┐
│ ESP32-S3-MINI-1-N8 │◄────────►│ ZW101 fingerprint   │
│  USB-OTG + BLE     │  touch   │ sensor module       │
│  flash encryption  │◄─────────│ (LED, templates)    │
│  secure boot v2    │          └────────────────────┘
│                    │  I2C     ┌────────────────────┐
│                    │◄────────►│ ATECC608B secure    │
│                    │          │ element             │
│                    │  I2C+INT ┌────────────────────┐
│                    │◄────────►│ LIS2DW12 IMU        │
│                    │          │ (tap / wake)        │
│                    │  I2C     ┌────────────────────┐
│                    │◄────────►│ MAX17048 fuel gauge │
└────────────────────┘          └────────────────────┘
```

## part choices and why

| function | part | why this one |
| -- | -- | -- |
| MCU | **ESP32-S3-MINI-1-N8** | The one chip that has both USB-OTG and BLE. USB-OTG restores everything the C3 port had to drop: USB HID typing, USB CCID/PIV, composite descriptors. BLE keeps the wireless keyboard. Certified module, 8 MB flash, PSRAM not needed. |
| fingerprint | **ZW101 module** (as in v1) | Known protocol (0xEF01), working driver, aura LED. Its templates live inside the module; see the security section for the honest consequences. |
| secure element | **ATECC608B-TNGTLS / -MAHDA** | I2C, ~1 €. Stores P-256 private keys non-extractably: the PIV key, the BLE transport key, and the password-wrapping key live here. Hardware ECDH+ECDSA, monotonic counters, tamper-hard. |
| IMU | **LIS2DW12** | 0.5 µA in low-power wake mode, tap/activity interrupts in hardware. This is the "notices being touched or picked up" sensor; it wakes the MCU from deep sleep via INT1. |
| charger + power path | **BQ24074** | True power-path: the board runs from USB while the battery charges, and switches to battery glitch-free on unplug. This is what makes "USB pass-through" work correctly. |
| regulator | **TPS63901** | Buck-boost, 75 nA quiescent. Runs 3V3 from a LiPo across its whole 3.0–4.2 V range and from USB VSYS, instead of browning out below 3.4 V like an LDO. |
| fuel gauge | **MAX17048** | 1 µA, I2C, battery percentage for the apps and a low-battery LED warning. |
| battery | **LiPo 503035, 500 mAh, with protection** | ~2 g, fits behind the sensor. Sized in the power budget below. |
| USB connector | **USB-C 16-pin (mid-mount)** | 5.1 kΩ CC pull-downs for 5 V sink. D+/D− straight to the S3's native USB. |
| buttons | reset + boot (side tactile) | Flash recovery without opening the case. |
| optional | **DRV2605L + LRA** | Haptic click on match/reject. Nice, not required; keep the footprint, populate later. |

Deliberately absent:
- No separate BLE chip: the S3 does both radios' jobs.
- No NFC: a passive NFC tag cannot do challenge-response PIV, and an active
  NFC-SE stack (SmartMX etc.) is NDA territory. The "ID card" function is PIV
  over USB CCID, which both macOS and Windows treat as a real smart card.
- No display: the sensor LED plus haptics carries all states.

## wake-on-approach: what the IMU can and cannot do

An accelerometer senses motion, not proximity. Nothing on this BOM can see a
hand hovering. What works, and what the prototype should ship:

1. **Deep sleep, everything dark.** ESP32-S3 in deep sleep, sensor powered
   down through a load switch, LED off. Board draws ~20 µA.
2. **Touch or pick-up wakes it.** Two wake sources, either is enough:
   - LIS2DW12 tap/activity interrupt: touching the case, tapping it, or
     picking it up wakes the MCU in <100 ms.
   - The ZW101's touch line also functions as a wake source when the sensor
     is left in its own low-power detect mode instead of fully off.
3. **Wake sequence:** power the sensor, LED fades in (aura command), BLE
   reconnects (~1 s to a bonded host), ready.
4. **Idle timeout:** no touch for N minutes (configurable) → back to dark.

The subjective effect is exactly the requested one: the device lights up when
you reach for it and handle it, because reaching for it always moves it.

## security architecture

Threat model: attacker steals the device, disassembles it, and probes every
chip. Nothing recoverable may unlock the Mac.

**At rest**

| secret | where | protection |
| -- | -- | -- |
| password | never on the device (HID mode) | The host keeps it (Keychain/DPAPI); the device only types what the host sends per event. Unchanged from v1, and the strongest possible answer for HID mode. |
| PIV private key | ATECC608B slot | Generated on-chip, never leaves the die. The S3 sends the hash, the SE signs. Decapping-resistant silicon. |
| BLE transport key | ATECC608B slot | Per-pairing ECDH (see transit). |
| pairing/HMAC keys, config | S3 NVS | NVS encryption + eFuse flash encryption + secure boot v2. eFuse keys are read-protected; this closes the "dump the SPI flash" attack. |
| fingerprint templates | inside the ZW101 module | Honest limit: this sensor family stores templates in its own flash, they cannot be moved into the SE. Mitigations: (a) sensor handshake password, random per device, stored in encrypted NVS, so a stolen sensor module answers no commands; (b) a template is not a secret equivalent to a password — it cannot be replayed against the device without the physical finger on this sensor, because matching happens inside the module; (c) the v1 UART-spoofing attack dies here: match events only count when the sensor authenticates with the handshake password. |

**In transit (the wireless part)**

BLE HID types the password, so the radio link is the sensitive hop. Two
layers, so a BLE-stack bug alone leaks nothing:

1. **BLE link security:** LE Secure Connections (numeric comparison at
   pairing), bonded, encrypted link. This is the same layer every BLE
   keyboard relies on.
2. **Application layer, end to end:** the v1 serial protocol (EV event, HMAC,
   AES-CTR password reply) moves onto a custom GATT service. Session keys
   come from ECDH between the ATECC608B and the host app, with the SE's key
   pinned at pairing time. Consequence: the password is ciphertext even
   inside a hypothetically compromised BLE link, and only *this* physical
   device (this SE) can decrypt it. The host releases the password only
   after the device proves a fingerprint match via HMAC'd event, same
   challenge-response as v1.

USB path: identical application protocol over the CDC console, as in v1.

## power budget

| state | draw | notes |
| -- | -- | -- |
| deep sleep (dark) | ~25 µA | S3 deep sleep + LIS2DW12 wake mode + gauge + buck-boost Iq |
| ready, BLE connected, LED on | ~45 mA | dominated by radio + sensor idle + LED |
| match + type burst | ~90 mA, <2 s | brief |

500 mAh LiPo:
- always-ready (never sleeps): ~10 h. Not the plan.
- wake-on-handling, 50 wakes/day à 2 min: **weeks per charge.**

The wake-on-approach feature is therefore also the battery-life feature.

## pinout sketch (S3-MINI)

| signal | GPIO | note |
| -- | -- | -- |
| USB D−/D+ | 19/20 | native USB |
| sensor UART TX/RX | 17/18 | UART1 |
| sensor touch/INT | 21 | RTC-capable, deep-sleep wake |
| sensor power switch | 14 | load switch EN (TPS22910A) |
| I2C SDA/SCL | 8/9 | SE + IMU + gauge shared bus |
| IMU INT1 | 10 | RTC-capable, deep-sleep wake |
| charger PGOOD/CHG | 12/13 | status inputs |
| boot/reset | 0/EN | side buttons |

## host software plan

| platform | typing path | PIV | app |
| -- | -- | -- | -- |
| macOS | BLE HID or USB HID; helper delivers password over GATT/CDC | native (SmartCard framework sees CCID) | existing helper + tinyTouch Manager GUI, extended with BLE GATT transport and battery display |
| Windows | same HID paths; new helper service (Rust or C#) doing the identical GATT/CDC protocol, password in DPAPI/Credential Manager | native (Windows smart card minidriver, PIV is built in) | tray app + settings GUI, feature-parity with the Mac GUI |

Shared core: one Rust crate implementing the event/password protocol, the
GATT client, and the CCID plumbing, with thin platform shells. That kills the
"two implementations drift apart" failure mode before it starts.

## roadmap

1. **Rev A schematic + layout** (KiCad): 4-layer, ~22×40 mm plus sensor
   flex/connector. Sensor mounts over the battery.
2. **Firmware port:** v1 firmware already splits console/keyboard transports
   (console_io/keyboard_io from the C3 port); add the S3 USB composite back,
   the GATT transport, SE integration (esp-cryptoauthlib), deep sleep + IMU
   wake, sensor load switch.
3. **Security bring-up:** eFuse flash encryption + secure boot v2 on a
   sacrificial board first (eFuses are irreversible), SE provisioning
   script, sensor handshake enrollment.
4. **Windows helper + app.**
5. **Rev B:** fixes plus optional haptics population.

## open decisions

- Case: reuse the printed v1 case style or design around the battery stack.
- Haptics in Rev A as unpopulated footprint (recommended) or drop entirely.
- Battery size: 500 mAh 503035 vs 300 mAh 402030 if the case should shrink.
