/*
 * Naz Mouse USB Passthrough — Arduino Leonardo R3 + USB Host Shield 2.0
 *
 * Reads HID reports from the Naz mouse (VID 0x046D, PID 0xC547) via the
 * Host Shield and re-sends them to the PC through the Leonardo's native USB
 * as a 16-bit-axis HID mouse.
 *
 * Dependencies (install via Arduino Library Manager):
 *   - USB Host Shield Library 2.0
 *
 * Wiring: stack the USB Host Shield on the Leonardo — no extra wires needed.
 *
 * Debugging: set DEBUG_RAW to 1, flash, open Serial Monitor at 115200.
 * Plug in the mouse and move it — you'll see hex dumps of each report.
 * Use those to confirm the byte layout, then set DEBUG_RAW back to 0.
 */

#include <SPI.h>
#include <usbhid.h>
#include <hiduniversal.h>
#include <usbhub.h>
#include <HID.h>

// ── Configuration ───────────────────────────────────────────────────────────

#define DEBUG_RAW       0     // 1 = dump raw reports to Serial (adds latency)
#define SERIAL_BAUD     115200
#define MOUSE_REPORT_ID 1

// ── Output HID descriptor: 5 buttons, 16-bit X/Y, 8-bit wheel ─────────────

static const uint8_t kMouseDesc[] PROGMEM = {
  0x05, 0x01,              // Usage Page (Generic Desktop)
  0x09, 0x02,              // Usage (Mouse)
  0xA1, 0x01,              // Collection (Application)
  0x85, MOUSE_REPORT_ID,   //   Report ID (1)
  0x09, 0x01,              //   Usage (Pointer)
  0xA1, 0x00,              //   Collection (Physical)

  // 5 buttons
  0x05, 0x09,              //     Usage Page (Buttons)
  0x19, 0x01,              //     Usage Minimum (1)
  0x29, 0x05,              //     Usage Maximum (5)
  0x15, 0x00,              //     Logical Minimum (0)
  0x25, 0x01,              //     Logical Maximum (1)
  0x95, 0x05,              //     Report Count (5)
  0x75, 0x01,              //     Report Size (1)
  0x81, 0x02,              //     Input (Data, Variable, Absolute)
  // 3-bit padding
  0x95, 0x01,              //     Report Count (1)
  0x75, 0x03,              //     Report Size (3)
  0x81, 0x01,              //     Input (Constant)

  // X and Y — 16-bit signed relative
  0x05, 0x01,              //     Usage Page (Generic Desktop)
  0x09, 0x30,              //     Usage (X)
  0x09, 0x31,              //     Usage (Y)
  0x16, 0x00, 0x80,        //     Logical Minimum (-32768)
  0x26, 0xFF, 0x7F,        //     Logical Maximum (32767)
  0x75, 0x10,              //     Report Size (16)
  0x95, 0x02,              //     Report Count (2)
  0x81, 0x06,              //     Input (Data, Variable, Relative)

  // Vertical wheel — 8-bit signed relative
  0x09, 0x38,              //     Usage (Wheel)
  0x15, 0x81,              //     Logical Minimum (-127)
  0x25, 0x7F,              //     Logical Maximum (127)
  0x75, 0x08,              //     Report Size (8)
  0x95, 0x01,              //     Report Count (1)
  0x81, 0x06,              //     Input (Data, Variable, Relative)

  0xC0,                    //   End Collection
  0xC0                     // End Collection
};

// ── Output report struct (must match descriptor above, packed) ──────────────

#pragma pack(push, 1)
struct MouseReport {
  uint8_t buttons;
  int16_t x;
  int16_t y;
  int8_t  wheel;
};
#pragma pack(pop)

// ── Register the HID descriptor via a global constructor ────────────────────

struct HIDRegistrar {
  HIDRegistrar() {
    static HIDSubDescriptor node(kMouseDesc, sizeof(kMouseDesc));
    HID().AppendDescriptor(&node);
  }
};
static HIDRegistrar _hidReg;

// ── Send a report to the PC ─────────────────────────────────────────────────

static void sendMouseReport(const MouseReport *r) {
  HID().SendReport(MOUSE_REPORT_ID, r, sizeof(MouseReport));
}

// ── USB Host Shield objects ─────────────────────────────────────────────────

USB     Usb;
USBHub  Hub(&Usb);
HIDUniversal Hid(&Usb);

// ── HID report parser: receives reports from the Naz mouse ─────────────────

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

#if DEBUG_RAW
  Serial.print(F("RPT["));
  Serial.print(dLen);
  Serial.print(F("]: "));
  for (uint8_t i = 0; i < dLen; i++) {
    if (d[i] < 0x10) Serial.print('0');
    Serial.print(d[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
#endif

  MouseReport rpt = {0, 0, 0, 0};

  if (dLen >= 6) {
    // 16-bit X/Y layout
    // [buttons:8] [x_lo:8] [x_hi:8] [y_lo:8] [y_hi:8] [wheel:8]
    rpt.buttons = d[0];
    rpt.x       = (int16_t)((uint16_t)d[1] | ((uint16_t)d[2] << 8));
    rpt.y       = (int16_t)((uint16_t)d[3] | ((uint16_t)d[4] << 8));
    rpt.wheel   = (int8_t)d[5];
  } else if (dLen >= 4) {
    // Boot-protocol fallback: 8-bit X/Y
    // [buttons:8] [x:8] [y:8] [wheel:8]
    rpt.buttons = d[0];
    rpt.x       = (int8_t)d[1];
    rpt.y       = (int8_t)d[2];
    rpt.wheel   = (int8_t)d[3];
  } else {
    return;
  }

  sendMouseReport(&rpt);
}

static MouseParser parser;

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
#if DEBUG_RAW
  Serial.begin(SERIAL_BAUD);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000)
    ;
  Serial.println(F("Naz Mouse Passthrough"));
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
  Serial.println(F("Ready — plug in the Naz mouse."));
#endif
}

void loop() {
  Usb.Task();

}
