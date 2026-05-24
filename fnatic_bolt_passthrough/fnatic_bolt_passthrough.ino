/*
 * Fnatic Bolt USB Passthrough — Arduino Leonardo R3 + USB Host Shield 2.0
 *
 * Reads HID reports from the Fnatic Bolt (VID 0x2F0E, PID 0x0203) via the
 * Host Shield and re-sends them to the PC through the Leonardo's native USB
 * as a 16-bit-axis HID mouse.
 *
 * VID/PID spoofing: handled via boards.local.txt in the project root.
 * Select the "Arduino Leonardo (Fnatic Bolt)" board in the IDE.
 * The build system injects USB_VID, USB_PID, and USB_PRODUCT from there.
 *
 * Dependencies (install via Arduino Library Manager):
 *   - USB Host Shield Library 2.0
 *
 * Wiring: stack the USB Host Shield on the Leonardo — no extra wires needed.
 *
 * First run: open Serial Monitor at 115200. Set DEBUG_RAW to 1 below.
 * Plug in the Bolt. You'll see hex dumps of every report — use those to
 * confirm the byte layout matches the 6-byte layout (8 buttons/x16/y16/wheel)
 * or 4-byte boot protocol (buttons/x8/y8/wheel). If it doesn't match, adjust
 * the parse logic in MouseParser::Parse().
 *
 * Once confirmed, set DEBUG_RAW to 0 and re-flash for minimum latency.
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

// ── Output HID descriptor: 8 buttons, 16-bit X/Y, 8-bit wheel ──────────────

static const uint8_t kMouseDesc[] PROGMEM = {
  0x05, 0x01,              // Usage Page (Generic Desktop)
  0x09, 0x02,              // Usage (Mouse)
  0xA1, 0x01,              // Collection (Application)
  0x85, MOUSE_REPORT_ID,   //   Report ID (1)
  0x09, 0x01,              //   Usage (Pointer)
  0xA1, 0x00,              //   Collection (Physical)

  // 8 buttons
  0x05, 0x09,              //     Usage Page (Buttons)
  0x19, 0x01,              //     Usage Minimum (1)
  0x29, 0x08,              //     Usage Maximum (8)
  0x15, 0x00,              //     Logical Minimum (0)
  0x25, 0x01,              //     Logical Maximum (1)
  0x95, 0x08,              //     Report Count (8)
  0x75, 0x01,              //     Report Size (1)
  0x81, 0x02,              //     Input (Data, Variable, Absolute)

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
  uint8_t buttons;    // 8 buttons
  int16_t x;
  int16_t y;
  int8_t  wheel;
};
#pragma pack(pop)

// ── Register the HID descriptor via a global constructor ────────────────────
// Mirrors how Arduino's Mouse library works: the descriptor is appended before
// USBDevice.attach() runs in main(), so the PC sees the correct device on
// first enumeration.

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

// ── HID report parser: receives reports from the Bolt ───────────────────────

class MouseParser : public HIDReportParser {
public:
  void Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) override;
};

void MouseParser::Parse(USBHID * /* hid */, bool is_rpt_id,
                         uint8_t len, uint8_t *buf) {
  uint8_t *d   = buf;
  uint8_t  dLen = len;

  if (!is_rpt_id && dLen > 0 && (buf[0] == 0x10 || buf[0] == 0x11)) return;

  if (is_rpt_id && dLen > 0) {
    d++;
    dLen--;
  }

#if DEBUG_RAW
  Serial.print(F("id="));
  Serial.print(is_rpt_id);
  Serial.print(F(" b0=0x"));
  if (buf[0] < 0x10) Serial.print('0');
  Serial.print(buf[0], HEX);
  Serial.print(F(" ["));
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
  static uint8_t prev_buttons = 0;

  if (dLen >= 6) {
    // 16-bit X/Y layout (expected for the Bolt at high polling rate)
    // [buttons:8 (8 buttons)] [x_lo:8] [x_hi:8] [y_lo:8] [y_hi:8] [wheel:8]
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

  if (rpt.x == 0 && rpt.y == 0 && rpt.wheel == 0
      && rpt.buttons == prev_buttons) return;
  prev_buttons = rpt.buttons;

  sendMouseReport(&rpt);
}

static MouseParser parser;

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
#if DEBUG_RAW
  Serial.begin(SERIAL_BAUD);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000)
    ; // wait up to 3 s for Serial Monitor, then continue anyway
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

// No delay() in loop — Usb.Task() polls the host shield as fast as possible
// to minimise polling latency.
void loop() {
  Usb.Task();
}
