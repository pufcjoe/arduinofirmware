# DELL Laser Mouse USB Passthrough

USB HID passthrough firmware for the **DELL Laser Mouse** using an **Arduino Leonardo R3** and **USB Host Shield 2.0**.

The Leonardo reads raw HID reports from the Dell mouse via the Host Shield and re-transmits them to the PC through its native USB — matching the real mouse's USB identity 1:1.

## Mouse Info (dumped from real hardware)

| Field | Value |
|-------|-------|
| VID | `0x0461` |
| PID | `0x4D51` |
| bcdDevice | `0x0717` |
| Product | `DELL Laser Mouse` |
| Manufacturer | (none — iManufacturer=0) |
| bcdUSB | `0x0200` |
| bcdHID | `0x0111` |

### Interface / Endpoint

| Field | Value |
|-------|-------|
| bInterfaceClass | 3 (HID) |
| bInterfaceSubClass | 1 (Boot Interface) |
| bInterfaceProtocol | 2 (Mouse) |
| bEndpointAddress | `0x81` (IN 1) |
| bmAttributes | `0x03` (Interrupt) |
| wMaxPacketSize | 5 |
| bInterval | 10 ms |
| bmConfigAttributes | `0xA0` (bus-powered, remote wakeup) |
| MaxPower | 100 mA |

### HID Report (5 bytes, no report ID)

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Buttons (5) | 0/1 absolute |
| 5–7 | Padding | — |
| 8–19 | X (12-bit) | -2047..2047 relative |
| 20–31 | Y (12-bit) | -2047..2047 relative |
| 32–39 | Wheel (8-bit) | -127..127 relative |

## Requirements

### Hardware
- Arduino Leonardo R3
- USB Host Shield 2.0 (stacks directly onto the Leonardo)
- USB cable (Leonardo to PC)
- DELL Laser Mouse (plugs into Host Shield USB-A port)

### Software
- [Arduino IDE](https://www.arduino.cc/en/software)
- **USB Host Shield Library 2.0** (install via Library Manager)

## Setup

1. **Apply core patches** (one-time): the Arduino AVR core needs small patches to allow overriding USB descriptor fields. See [Core Patches](#core-patches) below.
2. Copy `boards.local.txt` from the repo root to your Arduino AVR core directory:
   ```
   %LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\
   ```
3. Restart the Arduino IDE.
4. Select **Board: Arduino Leonardo (DELL Laser Mouse)**.
5. Flash the sketch.
6. Plug the DELL Laser Mouse into the USB Host Shield's USB-A port.

> **Note:** CDC serial is disabled in this board definition so the PC sees a pure HID mouse (no COM port). To re-flash, **double-tap the Leonardo's reset button** to enter the bootloader.

## Core Patches

These are minimal, backward-compatible patches (using `#ifndef` guards) to four files in the Arduino AVR core. They don't affect other board definitions.

**Location:** `%LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\`

### `cores/arduino/USBDesc.h`
Wrap `IMANUFACTURER`, `IPRODUCT`, `ISERIAL` in `#ifndef` guards so they can be overridden via `-D` flags.

### `cores/arduino/USBCore.cpp`
Add `USB_DEVICE_VERSION` define (defaults to `0x100`) and use it in the device descriptor instead of the hardcoded `0x100`.

### `libraries/HID/src/HID.cpp`
- Add overridable defines for `HID_INTERFACE_SUBCLASS`, `HID_INTERFACE_PROTOCOL`, `HID_ENDPOINT_INTERVAL`, `HID_ENDPOINT_SIZE`.
- Fix `SendReport()` to skip the report ID byte when `id == 0` (required for devices with no report ID in the descriptor).

### `libraries/HID/src/HID.h`
Add overridable `HID_BCD_VERSION_L` / `HID_BCD_VERSION_H` defines used in `D_HIDREPORT`.

## Debugging

Set `DEBUG_RAW` to `1` at the top of the sketch, then use a board definition **with CDC enabled** (or create a debug variant). Open Serial Monitor at 115200 baud to see raw hex dumps of incoming reports.

## What matches 1:1

- VID, PID, bcdDevice
- Product string, no manufacturer string, no serial number
- HID report descriptor (64 bytes, 12-bit X/Y, 8-bit wheel, 5 buttons)
- Interface class/subclass/protocol (HID Boot Mouse)
- Endpoint packet size and polling interval
- Configuration attributes and power
- bcdHID version
- No CDC interface (pure HID device)
- Raw 5-byte reports forwarded without modification

## What can't match (hardware limits)

- `bMaxPacketSize0`: Leonardo reports 64 (full-speed), real mouse reports 8
- USB speed: Leonardo is full-speed, real mouse may enumerate differently on some controllers
