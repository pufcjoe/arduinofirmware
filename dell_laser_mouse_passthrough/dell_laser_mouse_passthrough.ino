/*
 * DELL Laser Mouse USB Passthrough — Arduino Leonardo R3 + USB Host Shield 2.0
 *
 * Reads HID reports from the DELL Laser Mouse (VID 0x0461, PID 0x4D51) via the
 * Host Shield and re-sends them to the PC through the Leonardo's native USB
 * as a HID mouse.
 *
 * The output HID descriptor is a 1:1 match of the real Dell mouse:
 *   - 5 buttons (absolute)
 *   - 12-bit X/Y axes (-2047..2047, relative)
 *   - 8-bit wheel (-127..127, relative)
 *   - No report ID (raw 5-byte reports)
 *
 * VID/PID/string spoofing: handled via boards.local.txt in the project root.
 * Select the "Arduino Leonardo (DELL Laser Mouse)" board in the IDE.
 *
 * Dependencies (install via Arduino Library Manager):
 *   - USB Host Shield Library 2.0
 *
 * Wiring: stack the USB Host Shield on the Leonardo — no extra wires needed.
 */

#include <SPI.h>
#include <usbhid.h>
#include <hiduniversal.h>
#include <usbhub.h>
#include <HID.h>

// ── Configuration ───────────────────────────────────────────────────────────

#define DEBUG_RAW       1     // 1 = dump raw reports to Serial (adds latency)
#define SERIAL_BAUD     115200

// ── Output HID descriptor — exact match of real DELL Laser Mouse (64 bytes) ─
//
// Real device caps (dumped from hardware):
//   Buttons: UsagePage=0x0009, Range 1–5, Absolute
//   X:       UsagePage=0x0001, Usage=0x0030, 12-bit, LogMin=-2047, LogMax=2047
//   Y:       UsagePage=0x0001, Usage=0x0031, 12-bit, LogMin=-2047, LogMax=2047
//   Wheel:   UsagePage=0x0001, Usage=0x0038, 8-bit,  LogMin=-127,  LogMax=127
//
// Report layout (5 bytes, no report ID):
//   Byte 0:       buttons[4:0] + 3 bits padding
//   Bytes 1–3:    X[11:0] (12 bits) | Y[11:0] (12 bits), packed little-endian
//   Byte 4:       wheel[7:0]

static const uint8_t kMouseDesc[] PROGMEM = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)

  // 5 buttons
  0x05, 0x09,        //     Usage Page (Buttons)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x05,        //     Usage Maximum (5)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x05,        //     Report Count (5)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data, Variable, Absolute)

  // 3-bit padding
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x03,        //     Report Size (3)
  0x81, 0x01,        //     Input (Constant)

  // X, Y — 12-bit signed relative
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x16, 0x01, 0xF8,  //     Logical Minimum (-2047)
  0x26, 0xFF, 0x07,  //     Logical Maximum (2047)
  0x75, 0x0C,        //     Report Size (12)
  0x95, 0x02,        //     Report Count (2)
  0x81, 0x06,        //     Input (Data, Variable, Relative)

  // Wheel — 8-bit signed relative
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x06,        //     Input (Data, Variable, Relative)

  0xC0,              //   End Collection
  0xC0               // End Collection
};

// ── Register the HID descriptor via a global constructor ────────────────────

struct HIDRegistrar {
  HIDRegistrar() {
    static HIDSubDescriptor node(kMouseDesc, sizeof(kMouseDesc));
    HID().AppendDescriptor(&node);
  }
};
static HIDRegistrar _hidReg;

// ── Send a raw 5-byte report to the PC (no report ID) ──────────────────────

static void sendRawReport(const uint8_t *data, uint8_t len) {
  HID().SendReport(0, data, len);
}

// ── USB Host Shield objects ─────────────────────────────────────────────────

USB     Usb;
USBHub  Hub(&Usb);
HIDUniversal Hid(&Usb);

// ── HID report parser: receives reports from the DELL Laser Mouse ──────────

class MouseParser : public HIDReportParser {
public:
  void Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) override;
};

void MouseParser::Parse(USBHID * /* hid */, bool is_rpt_id,
                         uint8_t len, uint8_t *buf) {
  uint8_t *d   = buf;
  uint8_t  dLen = len;

  if (is_rpt_id && dLen > 0) {
    d++;
    dLen--;
  }

  if (dLen < 5) return;

#if DEBUG_RAW
  Serial.print(F("["));
  Serial.print(dLen);
  Serial.print(F("]: "));
  for (uint8_t i = 0; i < dLen; i++) {
    if (d[i] < 0x10) Serial.print('0');
    Serial.print(d[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
#endif

  sendRawReport(d, 5);
}

static MouseParser parser;

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
#if DEBUG_RAW
  Serial.begin(SERIAL_BAUD);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000)
    ;
  Serial.println(F("DELL Laser Mouse Passthrough"));
  Serial.println(F("Initializing USB Host Shield..."));
#endif

  if (Usb.Init() == -1) {
#if DEBUG_RAW
    Serial.println(F("ERROR: USB Host Shield init failed. Check wiring/shield."));
#endif
    for (;;)
      ;
  }

  if (!Hid.SetReportParser(0, &parser)) {
#if DEBUG_RAW
    Serial.println(F("WARN: SetReportParser(0) failed"));
#endif
  }

#if DEBUG_RAW
  Serial.println(F("Ready — plug in the DELL Laser Mouse."));
#endif
}

void loop() {
  Usb.Task();
}
