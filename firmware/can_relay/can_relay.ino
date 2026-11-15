// CAN relay box – ESP32 + SN65HVD230 CAN module. Set up for the KinCony KC868-A2.
//
// Drives outputs from the PowerTune dash's CAN buttons and KEEPS THEIR STATES itself, so the
// outputs are right as soon as the ignition comes on, without waiting for the dash to boot.
//
//   0x6A1 dash -> box (500 kbps, 11-bit): bytes 0-3 = CAN buttons 1-4, byte 4 bit 0 = dash synced.
//         The box only follows a frame with the synced bit set: a dash that is still starting up
//         (all buttons off) can't undo the saved states.
//   0x6A2 box -> dash, 10 Hz: bytes 0-3 = the saved states the box is driving,
//         byte 5 = sensed inputs (bit 0 = CAN input 1 ...), byte 7 counter.
//         The dash copies bytes 0-3 onto its buttons, then sets the synced bit.
//
// Sensed inputs: the A2's opto inputs IN1/IN2 are active when pulled to ground, so wire them to
// the low side of what the ECU switches:
//   IN1 -> ECU Dig In 5 (map select line): active = map 2 selected (by the box or any switch)
//   IN2 -> fan relay coil, ECU side:      active = fan relay energised (by the ECU or the box)
// The dash shows them with State gauges on "CAN input 1" / "CAN input 2".
//
// States are saved in flash (NVS) on every change and restored at power-up.
// Set RESTORE[] false for an output that should always start OFF (e.g. the map switch, if you
// want every drive to start on map 1).
//
// Wiring (A2 I2C header -> CAN module):  SDA -> CTX, SCL -> CRX, 3V3 -> 3V3, GND -> GND
//
// Serial console, 115200 baud, for bench testing:
//   b1 on | b1 off ... b4 on | b4 off   change a button state (as if tapped on the dash; saved)
//   selftest                            send frames out through the CAN module and read them back
//   status                              show states, outputs and CAN
//   pins                                check the I2C header pull-ups (confirms SDA/SCL pin numbers)

#include <driver/twai.h>
#include <Preferences.h>

const int COUNT = 4;
const int OUT_PIN[COUNT] = {15, 2, -1, -1};          // A2: relay 1, relay 2; buttons 3-4 unused
const bool RESTORE[COUNT] = {true, true, true, true}; // restore the saved state at power-up
const int IN_PIN[COUNT] = {36, 39, -1, -1};           // A2: IN1, IN2 (opto, LOW when the input is grounded)
const bool IN_ACTIVE_LOW = true;
const int CAN_TX = 4;    // header SDA -> module CTX
const int CAN_RX = 16;   // header SCL -> module CRX
const uint32_t DASH_ID = 0x6A1;
const uint32_t BOX_ID = 0x6A2;

Preferences prefs;
bool state[COUNT] = {};
bool canUp = false;
uint32_t dashFrames = 0, ignoredFrames = 0, lastDash = 0, lastSend = 0;
uint8_t counter = 0;
uint8_t inputs = 0, inputsRaw = 0;
uint32_t inputsChanged = 0;
String line;

void apply() {
  for (int i = 0; i < COUNT; i++)
    if (OUT_PIN[i] >= 0) digitalWrite(OUT_PIN[i], state[i] ? HIGH : LOW);
}

void save() {
  uint8_t bits = 0;
  for (int i = 0; i < COUNT; i++) if (state[i]) bits |= 1 << i;
  if (prefs.getUChar("bits", 0xFF) != bits) prefs.putUChar("bits", bits);
}

void setState(int i, bool v, const char *why) {
  if (state[i] == v) return;
  state[i] = v;
  apply();
  save();
  Serial.printf("button %d %s (%s)\n", i + 1, v ? "ON" : "off", why);
}

bool startCan(twai_mode_t mode) {
  if (canUp) { twai_stop(); twai_driver_uninstall(); canUp = false; }
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, mode);
  g.rx_queue_len = 32;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { Serial.println("CAN driver install FAILED"); return false; }
  if (twai_start() != ESP_OK) { Serial.println("CAN start FAILED"); twai_driver_uninstall(); return false; }
  canUp = true;
  return true;
}

void sendState() {
  if (!canUp) return;
  twai_message_t m = {};
  m.identifier = BOX_ID;
  m.data_length_code = 8;
  for (int i = 0; i < COUNT; i++) m.data[i] = state[i];
  m.data[5] = inputs;
  m.data[7] = counter++;
  twai_transmit(&m, 0);
}

void readInputs() {
  uint8_t raw = 0;
  for (int i = 0; i < COUNT; i++)
    if (IN_PIN[i] >= 0 && (digitalRead(IN_PIN[i]) == (IN_ACTIVE_LOW ? LOW : HIGH))) raw |= 1 << i;
  if (raw != inputsRaw) { inputsRaw = raw; inputsChanged = millis(); }
  if (raw != inputs && millis() - inputsChanged >= 50) {          // 50 ms debounce
    for (int i = 0; i < COUNT; i++)
      if ((raw ^ inputs) & (1 << i)) Serial.printf("input %d %s\n", i + 1, raw & (1 << i) ? "ACTIVE" : "off");
    inputs = raw;
    sendState();                                                  // tell the dash straight away
  }
}

void checkPins() {
  // R33/R34 (3.3k) pull SDA/SCL up. With the weak internal pull-down on, a pin that still
  // reads HIGH has the 3.3k pull-up on it, so it really is a header pin.
  bool up = canUp;
  if (up) { twai_stop(); twai_driver_uninstall(); canUp = false; }
  pinMode(CAN_TX, INPUT_PULLDOWN);
  pinMode(CAN_RX, INPUT_PULLDOWN);
  delay(5);
  int a = digitalRead(CAN_TX), b = digitalRead(CAN_RX);
  Serial.printf("GPIO%d (SDA): %s   GPIO%d (SCL): %s\n", CAN_TX, a ? "pulled up - OK" : "LOW - not the header pin?",
                CAN_RX, b ? "HIGH - OK" : "LOW - not the header pin?");
  if (up) startCan(TWAI_MODE_NORMAL);
}

void selfTest() {
  // NO_ACK + self-reception: each frame goes out through the module onto CANH/CANL and comes
  // back in on CRX, so this proves ESP32 <-> module wiring without any other node on the bus.
  Serial.println("self-test: sending 5 frames through the module...");
  if (!startCan(TWAI_MODE_NO_ACK)) return;
  int ok = 0;
  for (int i = 0; i < 5; i++) {
    twai_message_t m = {};
    m.identifier = 0x7F0;
    m.self = 1;
    m.data_length_code = 2;
    m.data[0] = 0xA5; m.data[1] = i;
    twai_transmit(&m, pdMS_TO_TICKS(100));
    twai_message_t r;
    uint32_t t0 = millis();
    while (millis() - t0 < 100)
      if (twai_receive(&r, pdMS_TO_TICKS(20)) == ESP_OK && r.identifier == 0x7F0 && r.data[1] == i) { ok++; break; }
  }
  twai_status_info_t s; twai_get_status_info(&s);
  Serial.printf("self-test: %d/5 frames came back. tx errors %u, rx errors %u\n", ok, s.tx_error_counter, s.rx_error_counter);
  Serial.println(ok == 5 ? "PASS - module wired correctly" :
                 "FAIL - check 3V3/GND, and swap SDA/SCL wires (CTX/CRX) if 0/5");
  startCan(TWAI_MODE_NORMAL);
}

void status() {
  twai_status_info_t s = {};
  if (canUp) twai_get_status_info(&s);
  for (int i = 0; i < COUNT; i++)
    Serial.printf("button %d %s%s   ", i + 1, state[i] ? "ON " : "off", OUT_PIN[i] < 0 ? " (no output)" : "");
  for (int i = 0; i < COUNT; i++)
    if (IN_PIN[i] >= 0) Serial.printf("\ninput %d %s", i + 1, inputs & (1 << i) ? "ACTIVE" : "off");
  Serial.printf("\nCAN %s, tx err %u, rx err %u | dash frames %lu (ignored, not synced: %lu), %s\n",
                canUp ? "up" : "DOWN", s.tx_error_counter, s.rx_error_counter, dashFrames, ignoredFrames,
                millis() - lastDash < 500 && dashFrames ? "dash LIVE" : "no dash");
}

void command(String c) {
  c.trim(); c.toLowerCase();
  if (c.length() >= 5 && c[0] == 'b' && c[1] >= '1' && c[1] <= '4' && (c.endsWith(" on") || c.endsWith(" off")))
    setState(c[1] - '1', c.endsWith(" on"), "console");
  else if (c == "selftest") selfTest();
  else if (c == "status") status();
  else if (c == "pins") checkPins();
  else if (c.length()) Serial.println("commands: b1..b4 on|off, selftest, status, pins");
}

void setup() {
  // outputs first, from the saved states, before anything else
  prefs.begin("canrelay", false);
  uint8_t bits = prefs.getUChar("bits", 0);
  for (int i = 0; i < COUNT; i++) {
    state[i] = RESTORE[i] && (bits & (1 << i));
    if (OUT_PIN[i] >= 0) pinMode(OUT_PIN[i], OUTPUT);
    if (IN_PIN[i] >= 0) pinMode(IN_PIN[i], INPUT);
  }
  apply();
  save();

  Serial.begin(115200);
  delay(200);
  Serial.println("\nCAN relay box - dash buttons 0x6A1 in, saved states 0x6A2 out, 500 kbps");
  checkPins();
  startCan(TWAI_MODE_NORMAL);
  status();
}

void loop() {
  if (canUp) {
    twai_message_t m;
    while (twai_receive(&m, 0) == ESP_OK) {
      if (m.extd || m.rtr || m.identifier != DASH_ID || m.data_length_code < 5) continue;
      dashFrames++;
      lastDash = millis();
      if (!(m.data[4] & 1)) { ignoredFrames++; continue; }    // dash still starting: keep our states
      for (int i = 0; i < COUNT; i++) setState(i, m.data[i] != 0, "dash");
    }
    twai_status_info_t s; twai_get_status_info(&s);
    if (s.state == TWAI_STATE_BUS_OFF) { Serial.println("CAN bus-off, recovering"); twai_initiate_recovery(); }
    else if (s.state == TWAI_STATE_STOPPED) twai_start();
  }
  readInputs();
  if (millis() - lastSend >= 100) { lastSend = millis(); sendState(); }
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { command(line); line = ""; }
    else if (line.length() < 40) line += ch;
  }
}
