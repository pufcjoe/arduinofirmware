# Naz Mouse USB Passthrough

USB HID passthrough firmware for the **Naz mouse** using an **Arduino Leonardo R3** and **USB Host Shield 2.0**.

The Leonardo reads raw HID reports from the Naz mouse via the Host Shield and re-transmits them to the PC through its native USB as a HID mouse with full 16-bit X/Y axis resolution.

## Mouse Info

| Field | Value |
|-------|-------|
| VID   | `0x046D` |
| PID   | `0xC547` |

## Requirements

### Hardware
- Arduino Leonardo R3
- USB Host Shield 2.0 (stacks directly onto the Leonardo)
- USB cable (Leonardo to PC)
- Naz mouse (plugs into Host Shield USB-A port)

### Software
- [Arduino IDE](https://www.arduino.cc/en/software)
- **USB Host Shield Library 2.0** (install via Library Manager: Sketch > Include Library > Manage Libraries)

## Setup

1. Open `naz_mouse_passthrough.ino` in the Arduino IDE.
2. Install the **USB Host Shield Library 2.0** from Library Manager.
3. Select **Board: Arduino Leonardo** and the correct **COM port**.
4. Flash the sketch.
5. Plug the Naz mouse into the USB Host Shield's USB-A port.

## USB Device Spoofing (optional)

To make the Leonardo report itself to the PC with the G PRO X Superlight's VID, PID, manufacturer, and product strings:

1. Copy `boards.local.txt` from the repo root to your Arduino AVR core directory:
   ```
   %LOCALAPPDATA%\Arduino15\packages\arduino\hardware\avr\<version>\
   ```
2. Restart the Arduino IDE.
3. Select **Board: Arduino Leonardo (G PRO X Superlight)** instead of the regular Leonardo.
4. Flash the sketch.

The PC will now see the Leonardo as:

| Field        | Value |
|--------------|-------|
| VID          | `0x046D` |
| PID          | `0xC547` |
| Manufacturer | `Logitech` |
| Product      | `PRO X Superlight Wireless Gaming Mouse` |

The bootloader VID/PID remains unchanged, so uploads still work normally.

## Debugging

To inspect raw HID reports:

1. Set `DEBUG_RAW` to `1` at the top of the sketch.
2. Flash and open Serial Monitor at **115200** baud.
3. Move the mouse and click buttons. You'll see hex dumps:
   ```
   RPT[6]: 00 05 00 FD FF 00
   ```
4. Confirm the byte layout matches: `[buttons] [x_lo] [x_hi] [y_lo] [y_hi] [wheel]`
5. Once confirmed, set `DEBUG_RAW` back to `0` and re-flash for minimum latency.

## Report Format

The parser auto-detects two layouts:

| Layout | Size | Fields |
|--------|------|--------|
| 16-bit (default) | 6 bytes | `buttons:8`, `x:16le`, `y:16le`, `wheel:8` |
| Boot protocol | 4 bytes | `buttons:8`, `x:8`, `y:8`, `wheel:8` |

If the mouse uses a different layout, adjust the parse logic in `MouseParser::Parse()`.

## Features

- 16-bit X/Y axes (no clipping at high DPI)
- 5 button support (left, right, middle, back, forward)
- Scroll wheel
- Minimal latency when `DEBUG_RAW` is disabled
