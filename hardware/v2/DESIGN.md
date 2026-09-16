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
| proximity electrode | **copper pour + S3 touch peripheral** | The MX-Keys-style hover detection, at zero BOM cost: the S3's touch controller has a proximity mode that wakes from deep sleep. One large top-layer pour is the sensor. |
| IMU | **LIS2DW12** | 0.5 µA wake-on-motion backup: tap/pick-up wake plus transport detection. Complements the capacitive hover sense. |
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

## wake-on-approach: how the MX Keys does it, and how v2 gets the same

The Logitech MX Keys senses a hovering hand **capacitively**: a large
electrode under the top surface (effectively the keyboard's ground/top plane)
is driven by a proximity-capable touch controller. A hand at several
centimeters changes the electrode's self-capacitance by a few femtofarads and
that shift is detectable long before contact. No camera, no IR, no radar.
That is why it feels magical: the whole surface is the sensor.

**v2 gets this for free.** The ESP32-S3's touch peripheral has a dedicated
*proximity sensing* mode (up to three channels, works during light sleep,
wakes from deep sleep). Design rules that make it work like the MX Keys:

- Dedicate one touch pin (e.g. GPIO4/TOUCH4) to a **large copper pour on the
  top layer** under the case surface, hatched, no ground pour directly under
  it. The bigger the electrode, the longer the reach: a 20×35 mm pour on a
  device this size gives roughly 3–8 cm of hover detection after tuning.
- Keep the electrode trace short and away from the antenna keepout.
- Tune threshold + debounce in firmware; the IDF exposes proximity channels
  with measurement-interval control for µA-level average draw.
- If the case is metal, this does not work; the case must be plastic (as
  planned) with the pour under it.

Wake ladder, cheapest sense first:

1. **Deep sleep, everything dark.** Sensor behind its load switch, LED off.
   Touch-proximity scan keeps running in the RTC domain: total board draw
   ~25 µA.
2. **Hand approaches (3–8 cm):** proximity wake fires, LED fades in, BLE
   reconnects (~1 s to a bonded host). This is the MX-Keys moment.
3. **Backup wake sources:** LIS2DW12 tap/pick-up interrupt (motion) and the
   ZW101 touch line (contact). Either also wakes the board, covering the
   case where the hand comes in too fast or from a dead angle.
4. **Idle timeout:** no proximity, no motion, no touch for N minutes → dark.

The IMU stays on the board: it distinguishes "picked up and carried" from
"hand hovered nearby", which the firmware can use to arm/disarm faster, and
it doubles as a transport-detection input (lock immediately when the device
leaves the desk).

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
| proximity electrode | 4 (TOUCH4) | large top-layer pour, RTC wake |
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

## BOM for PCBWay assembly, cost-focused

Target: full turnkey assembly at PCBWay, no hand soldering. Prices are
LCSC/PCBWay catalog magnitudes at qty 5–10, rounded.

| # | function | part | ~€/pc | note |
| -- | -- | -- | -- | -- |
| 1 | MCU+BLE+USB | ESP32-S3-MINI-1-N8 | 3.20 | certified module: no RF layout risk, no antenna tuning, PCBWay places it |
| 2 | secure element | ATECC608B-SSHDA-B (SOIC-8) | 0.90 | SOIC instead of UDFN: cheaper assembly, easier probing during bring-up |
| 3 | IMU | LIS2DW12TR | 1.10 | backup wake + transport detection |
| 4 | charger/power path | BQ24074RGTR | 1.60 | the pass-through requirement makes this worth it |
| 5 | regulator | TPS63001DRCR | 1.40 | buck-boost; TPS63901 is lower-Iq but pricier/newer, either footprint works |
| 6 | fuel gauge | MAX17048G+T10 | 1.20 | optional but the apps want a battery % |
| 7 | load switch | TPS22910AYZVR | 0.30 | sensor power gating |
| 8 | USB-C 16p | HRO TYPE-C-31-M-12 | 0.25 | the LCSC standard part |
| 9 | passives, LEDs, buttons, TVS | — | ~1.50 | USBLC6 on D+/D−, 5.1k CC, RC, tactiles |
| | **board electronics** | | **~11.50** | |
| 10 | fingerprint module | ZW101 (or R503-mini class) | 8–12 | bought separately (AliExpress/Taobao), connects via FPC/JST, **not** PCBWay-assembled |
| 11 | battery 503035 500 mAh protected | | 3–4 | separately, JST-PH pigtail |

Realistic per-unit landed cost at qty 5: electronics ~12 €, PCBA setup+boards
~60–100 € total order, sensor ~10 €, battery ~4 €. First batch of 5 lands
around 35–45 €/piece; the marginal unit is ~26 €.

Cost cuts that do not hurt:
- Drop MAX17048, read battery voltage on an ADC pin through a divider
  (−1.20 €, battery % becomes an estimate).
- Drop the IMU if the capacitive hover wake proves sufficient in prototype
  (−1.10 €); keep the footprint unpopulated.
- TP4056+DW01 instead of BQ24074 (−1.20 €) **only if** USB pass-through may
  degrade to "charges while used via BLE"; true power-path is what BQ24074
  buys.

Do not cut: the SE (0.90 € for the entire at-rest story) and the certified
RF module (a bare chip + own antenna costs more in respins than it saves).

## fingerprint sensor: options considered

| sensor | ~€ | protocol | verdict |
| -- | -- | -- | -- |
| **ZW101** (v1 sensor) | 8–10 | 0xEF01 UART | **default choice.** Driver exists and is hardware-proven, aura LED, touch line, in-module matching. |
| R503 / R503-mini | 9–12 | 0xEF01 UART | same protocol family, round with RGB ring, drop-in with minor tweaks; pick whichever mounts better in the case |
| FPC1020/FPM10A optical | 5–7 | 0xEF01-ish | bigger, optical window, worse FRR, no aura LED: not worth the 3 € |
| capacitive raw sensors (FPC1035 etc.) | 3–5 | SPI raw image | matching must run on the MCU: big firmware project, templates end up on the S3 (worse security than in-module), reject |
| Goodix/Egis phone modules | 2–4 | undocumented | NDA/no docs, reject |

Conclusion: stay with the ZW101/R503 family. The 0xEF01 driver, the handshake
password, enrollment UX and LED behavior all carry over unchanged.

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
