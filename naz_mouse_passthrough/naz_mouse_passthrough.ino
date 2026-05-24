/*
 * Naz Mouse USB Passthrough — Arduino Leonardo R3 + USB Host Shield 2.0
 *
 * Reads HID reports from the Naz mouse (VID 0x046D, PID 0xC547) via the
 * Host Shield and re-sends them to the PC through the Leonardo's native USB
 * using a HID descriptor modeled on the Linux kernel's mse_high_res_descriptor
 * for the Lightspeed receiver: 16 buttons, 12-bit X/Y, 8-bit wheel + hwheel.
 *
 * VID/PID spoofing: handled via boards.local.txt in the project root.
 * Select the "Arduino Leonardo (G PRO X Superlight)" board in the IDE.
 * The build system injects USB_VID, USB_PID, and USB_PRODUCT from there.
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

// ── Output HID descriptor: 16 buttons, 12-bit X/Y, 8-bit wheel + hwheel ──

static const uint8_t kMouseDesc[] PROGMEM = {
  0x05, 0x01,              // Usage Page (Generic Desktop)
  0x09, 0x02,              // Usage (Mouse)
  0xA1, 0x01,              // Collection (Application)
  0x85, MOUSE_REPORT_ID,   //   Report ID (1)
  0x09, 0x01,              //   Usage (Pointer)
  0xA1, 0x00,              //   Collection (Physical)

  // 16 buttons
  0x95, 0x10,              //     Report Count (16)
  0x75, 0x01,              //     Report Size (1)
  0x15, 0x00,              //     Logical Minimum (0)
  0x25, 0x01,              //     Logical Maximum (1)
  0x05, 0x09,              //     Usage Page (Buttons)
  0x19, 0x01,              //     Usage Minimum (1)
  0x29, 0x10,              //     Usage Maximum (16)
  0x81, 0x02,              //     Input (Data, Variable, Absolute)

  // X and Y — 12-bit signed relative
  0x95, 0x02,              //     Report Count (2)
  0x75, 0x0C,              //     Report Size (12)
  0x16, 0x01, 0xF8,        //     Logical Minimum (-2047)
  0x26, 0xFF, 0x07,        //     Logical Maximum (2047)
  0x05, 0x01,              //     Usage Page (Generic Desktop)
  0x09, 0x30,              //     Usage (X)
  0x09, 0x31,              //     Usage (Y)
  0x81, 0x06,              //     Input (Data, Variable, Relative)

  // Vertical wheel — 8-bit signed relative
  0x95, 0x01,              //     Report Count (1)
  0x75, 0x08,              //     Report Size (8)
  0x15, 0x81,              //     Logical Minimum (-127)
  0x25, 0x7F,              //     Logical Maximum (127)
  0x09, 0x38,              //     Usage (Wheel)
  0x81, 0x06,              //     Input (Data, Variable, Relative)

  // Horizontal wheel (AC Pan) — 8-bit signed relative
  0x95, 0x01,              //     Report Count (1)
  0x05, 0x0C,              //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,        //     Usage (AC Pan)
  0x81, 0x06,              //     Input (Data, Variable, Relative)

  0xC0,                    //   End Collection
  0xC0                     // End Collection
};

// ── Output report struct (intermediate; manually packed before sending) ────

#pragma pack(push, 1)
struct MouseReport {
  uint16_t buttons;   // 16 buttons
  int16_t  x;
  int16_t  y;
  int8_t   wheel;
  int8_t   hwheel;
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

// ── Send a report to the PC (packs 12-bit X/Y to match descriptor) ─────────

static void sendMouseReport(const MouseReport *r) {
  uint8_t report[7];
  report[0] = r->buttons & 0xFF;
  report[1] = (r->buttons >> 8) & 0xFF;
  uint16_t ux = (uint16_t)(r->x & 0x0FFF);
  uint16_t uy = (uint16_t)(r->y & 0x0FFF);
  report[2] = ux & 0xFF;
  report[3] = ((ux >> 8) & 0x0F) | ((uy & 0x0F) << 4);
  report[4] = (uy >> 4) & 0xFF;
  report[5] = (uint8_t)r->wheel;
  report[6] = (uint8_t)r->hwheel;
  HID().SendReport(MOUSE_REPORT_ID, report, sizeof(report));
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

  MouseReport rpt = {0, 0, 0, 0, 0};
  static uint16_t prev_buttons = 0;

  if (dLen >= 13) {
    // Lightspeed 13-byte layout
    // [btn_lo:8] [btn_hi:8] [x_lo:8] [x_hi_nib:4|0:4] [y_lo:8] [y_hi_nib:4|0:4] [wheel:8] [hwheel:8] [vendor...]
    rpt.buttons = (uint16_t)d[0] | ((uint16_t)d[1] << 8);
    int16_t x = (int16_t)((uint16_t)d[2] | (((uint16_t)d[3] & 0x0F) << 8));
    if (x & 0x0800) x |= 0xF000;
    int16_t y = (int16_t)((uint16_t)d[4] | (((uint16_t)d[5] & 0x0F) << 8));
    if (y & 0x0800) y |= 0xF000;
    if (x < -2047) x = -2047;
    if (x >  2047) x =  2047;
    if (y < -2047) y = -2047;
    if (y >  2047) y =  2047;
    rpt.x      = x;
    rpt.y      = y;
    rpt.wheel  = (int8_t)d[6];
    rpt.hwheel = (int8_t)d[7];
  } else if (dLen >= 6) {
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

  if (rpt.x == 0 && rpt.y == 0 && rpt.wheel == 0
      && rpt.hwheel == 0 && rpt.buttons == prev_buttons) return;
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

// No delay() in loop — Usb.Task() polls the host shield as fast as possible
// to minimise polling latency.
void loop() {
  Usb.Task();
}
