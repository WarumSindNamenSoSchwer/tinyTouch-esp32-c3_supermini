# tinyTouch Manager (macOS GUI)

A native SwiftUI app for everything the `tinytouch` CLI does day to day:

- device state (firmware, sensor, BLE keyboard, helper)
- set or change the typed password (stored only in the macOS Keychain)
- enroll all four fingerprint views with live progress, delete fingerprints
- register/remove computers, run a keyboard test, read the helper log

## build & run

```
./software/macos-gui/build-app.sh
open "software/macos-gui/build/tinyTouch Manager.app"
```

The app drives `tinytouch_gui_backend.py`, which reuses the helper's serial
protocol and Keychain modules from `software/macos-helper/`. The backend
pauses the LaunchAgent while it owns the serial port and restarts it after,
so the fingerprint-to-password flow keeps working the moment an action ends.

The app must stay in the repo checkout: the backend resolves the helper
modules and the repo `.venv` relative to its own path.
