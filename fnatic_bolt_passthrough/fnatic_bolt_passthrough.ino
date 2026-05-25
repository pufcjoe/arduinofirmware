/*
 * Fnatic Bolt USB Passthrough — Arduino Leonardo R3 + USB Host Shield 2.0
 *
 * Reads HID reports from the Fnatic Bolt (VID 0x2F0E, PID 0x0203) via the
 * Host Shield and re-sends them to the PC through the Leonardo's native USB
 * as a HID mouse.
 *
 * The output HID descriptor is a 1:1 match of the real Bolt's mouse interface:
 *   - 5 buttons (absolute)
 *   - 16-bit X/Y axes (-32767..32767, relative)
 *   - 8-bit vertical wheel (-127..127, relative)
 *   - 8-bit AC pan / horizontal scroll (-127..127, relative)
 *   - No report ID (raw 7-byte reports)
 *
 * VID/PID/string spoofing: handled via boards.local.txt in the project root.
 * Select the "Arduino Leonardo (Fnatic Bolt)" board in the IDE.
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

// ── Output HID descriptor — match of real Bolt mouse interface (73 bytes) ───
//
// Real device caps (dumped from hardware):
//   Buttons: UsagePage=0x0009, Range 1–5, Absolute
//   X:       UsagePage=0x0001, Usage=0x0030, 16-bit, LogMin=-32767, LogMax=32767
//   Y:       UsagePage=0x0001, Usage=0x0031, 16-bit, LogMin=-32767, LogMax=32767
//   Wheel:   UsagePage=0x0001, Usage=0x0038, 8-bit,  LogMin=-127,  LogMax=127
//   AC Pan:  UsagePage=0x000C, Usage=0x0238, 8-bit,  LogMin=-127,  LogMax=127
//
// Report layout (7 bytes, no report ID):
//   Byte 0:       buttons[4:0] + 3 bits padding
//   Bytes 1–2:    X (16-bit LE signed)
//   Bytes 3–4:    Y (16-bit LE signed)
//   Byte 5:       wheel (8-bit signed)
//   Byte 6:       AC pan / horizontal scroll (8-bit signed)

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

  // X, Y — 16-bit signed relative
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x16, 0x01, 0x80,  //     Logical Minimum (-32767)
  0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x02,        //     Report Count (2)
  0x81, 0x06,        //     Input (Data, Variable, Relative)

  // Wheel — 8-bit signed relative
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x06,        //     Input (Data, Variable, Relative)

  // AC Pan (horizontal scroll) — 8-bit signed relative
  0x05, 0x0C,        //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,  //     Usage (AC Pan)
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

// ── Send a raw 7-byte report to the PC (no report ID) ──────────────────────

static void sendRawReport(const uint8_t *data, uint8_t len) {
  HID().SendReport(0, data, len);
}

// ── USB Host Shield objects ─────────────────────────────────────────────────

USB     Usb;
USBHub  Hub(&Usb);
HIDUniversal Hid(&Usb);

// ── HID report parser: receives reports from the Bolt ───────────────────────

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

  if (dLen < 7) return;

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

  sendRawReport(d, 7);
}

static MouseParser parser;

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
#if DEBUG_RAW
  Serial.begin(SERIAL_BAUD);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000)
    ;
  Serial.println(F("Fnatic Bolt Passthrough"));
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
  Serial.println(F("Ready — plug in the Fnatic Bolt."));
#endif
}

void loop() {
  Usb.Task();
}
