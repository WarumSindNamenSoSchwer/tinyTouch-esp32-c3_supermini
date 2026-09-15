#!/bin/zsh
# Build tinyTouch Manager.app from the single Swift source file.
set -euo pipefail

here="${0:A:h}"
app="$here/build/tinyTouch Manager.app"
binary="$app/Contents/MacOS/tinyTouch Manager"

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

swiftc -O -parse-as-library \
  -target arm64-apple-macos13.0 \
  -o "$binary" \
  "$here/TinyTouchApp.swift"

# The backend stays in the repo rather than the bundle: it needs the repo's
# helper modules and the repo venv's pyserial, both resolved relative to its
# own source location.

cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>tinyTouch Manager</string>
  <key>CFBundleDisplayName</key><string>tinyTouch Manager</string>
  <key>CFBundleIdentifier</key><string>com.tinytouch.manager</string>
  <key>CFBundleExecutable</key><string>tinyTouch Manager</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

codesign --force --sign - "$app"
print "Built: $app"
