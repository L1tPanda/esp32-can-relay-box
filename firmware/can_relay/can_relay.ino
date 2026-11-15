// CAN relay box – ESP32 + CAN transceiver (e.g. SN65HVD230) switching relays / MOSFETs from CAN frames.
// Works with any CAN system: ids, bitrate, which byte/bit drives each output, status frame, timeout,
// and the board's pins are all set from the serial console and saved on the board (no recompiling).
//
// Presets (serial: "preset <name>"):
//   powertune  PowerTune dash with powertune-me442-custom-firmware patches 0003/0004 (factory default)
//              500 kbps, commands 0x6A1 bytes 0-3, sync bit byte 4 bit 0, status 0x6A2 at 10 Hz,
//              states kept through power cycles.
//   generic    500 kbps, commands 0x700 bytes 0-3 (non-zero = on), no sync bit, status 0x701 at 10 Hz,
//              outputs off at power-up and released if no command for 1 s.
//   Boards: "board a2" (KinCony KC868-A2: relays 15/2, inputs 36/39, CAN on the I2C header 4/16).
//
// How it works
//   Command frame (cmd_id): output N is ON when (byte out_byteN & out_maskN) != 0 (or == 0 if inverted).
//     If sync_mask != 0, a frame is only followed when (byte sync_byte & sync_mask) != 0: a sender that is
//     still starting up can't overwrite the saved states (the PowerTune dash sets it once it has read
//     the box's status frame).
//   Status frame (status_id), every status_ms and when an input changes:
//     bytes 0-3 = output states (0/1), byte 5 = sensed inputs (bit 0 = input 1 ...), byte 7 = counter.
//   States are saved in flash on change; restore_N picks whether output N comes back at power-up.
//   timeout_ms > 0: all outputs go off if no command frame arrives for that long (0 = keep states).
//
// Serial console (115200 baud): type "help".

#include <driver/twai.h>
#include <Preferences.h>

const int N = 4;                 // outputs and inputs
const uint32_t CFG_VERSION = 1;

struct Config {
  uint32_t version;
  uint32_t bitrate;              // kbit/s: 125, 250, 500, 1000
  uint32_t cmd_id;  uint8_t cmd_ext;
  uint32_t status_id; uint8_t status_ext;
  uint32_t status_ms;            // 0 = no status frame
  uint8_t sync_byte, sync_mask;  // sync_mask 0 = no sync needed
  uint32_t timeout_ms;           // 0 = keep states when commands stop
  uint8_t out_byte[N], out_mask[N], out_invert[N], restore[N];
  int8_t out_pin[N], in_pin[N];  // -1 = not used
  uint8_t in_active_low;
  int8_t can_tx, can_rx;
};

Config cfg;
Preferences prefs;
bool state[N] = {};
bool canUp = false;
uint32_t cmdFrames = 0, ignoredFrames = 0, lastCmd = 0, lastSend = 0;
bool cmdLive = false;
uint8_t counter = 0, inputs = 0, inputsRaw = 0;
uint32_t inputsChanged = 0;
String line;

// ---------------------------------------------------------------- presets
void boardA2(Config &c) {
  const int8_t outs[N] = {15, 2, -1, -1}, ins[N] = {36, 39, -1, -1};
  for (int i = 0; i < N; i++) { c.out_pin[i] = outs[i]; c.in_pin[i] = ins[i]; }
  c.in_active_low = 1;
  c.can_tx = 4; c.can_rx = 16;     // I2C header: SDA -> module CTX, SCL -> module CRX
}

void presetPowertune(Config &c) {
  c.bitrate = 500;
  c.cmd_id = 0x6A1; c.cmd_ext = 0;
  c.status_id = 0x6A2; c.status_ext = 0; c.status_ms = 100;
  c.sync_byte = 4; c.sync_mask = 0x01;
  c.timeout_ms = 0;
  for (int i = 0; i < N; i++) { c.out_byte[i] = i; c.out_mask[i] = 0xFF; c.out_invert[i] = 0; c.restore[i] = 1; }
}

void presetGeneric(Config &c) {
  c.bitrate = 500;
  c.cmd_id = 0x700; c.cmd_ext = 0;
  c.status_id = 0x701; c.status_ext = 0; c.status_ms = 100;
  c.sync_byte = 0; c.sync_mask = 0;
  c.timeout_ms = 1000;
  for (int i = 0; i < N; i++) { c.out_byte[i] = i; c.out_mask[i] = 0xFF; c.out_invert[i] = 0; c.restore[i] = 0; }
}

void factoryDefaults(Config &c) {
  memset(&c, 0, sizeof(c));
  c.version = CFG_VERSION;
  presetPowertune(c);
  boardA2(c);
}

// ---------------------------------------------------------------- outputs, inputs, storage
void apply() {
  for (int i = 0; i < N; i++)
    if (cfg.out_pin[i] >= 0) digitalWrite(cfg.out_pin[i], state[i] ? HIGH : LOW);
}

void setupPins() {
  for (int i = 0; i < N; i++) {
    if (cfg.out_pin[i] >= 0) pinMode(cfg.out_pin[i], OUTPUT);
    if (cfg.in_pin[i] >= 0) pinMode(cfg.in_pin[i], INPUT);
  }
  apply();
}

void saveStates() {
  uint8_t bits = 0;
  for (int i = 0; i < N; i++) if (state[i]) bits |= 1 << i;
  if (prefs.getUChar("bits", 0xFF) != bits) prefs.putUChar("bits", bits);
}

void saveConfig() { prefs.putBytes("cfg", &cfg, sizeof(cfg)); }

void loadConfig() {
  if (prefs.getBytesLength("cfg") != sizeof(cfg) || prefs.getBytes("cfg", &cfg, sizeof(cfg)) != sizeof(cfg)
      || cfg.version != CFG_VERSION) {
    factoryDefaults(cfg);
    saveConfig();
  }
}

void setState(int i, bool v, const char *why) {
  if (state[i] == v) return;
  state[i] = v;
  apply();
  saveStates();
  Serial.printf("output %d %s (%s)\n", i + 1, v ? "ON" : "off", why);
}

// ---------------------------------------------------------------- CAN
bool startCan(twai_mode_t mode) {
  if (canUp) { twai_stop(); twai_driver_uninstall(); canUp = false; }
  if (cfg.can_tx < 0 || cfg.can_rx < 0) { Serial.println("CAN pins not set"); return false; }
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)cfg.can_tx, (gpio_num_t)cfg.can_rx, mode);
  g.rx_queue_len = 32;
  twai_timing_config_t t;
  switch (cfg.bitrate) {
    case 125:  t = TWAI_TIMING_CONFIG_125KBITS(); break;
    case 250:  t = TWAI_TIMING_CONFIG_250KBITS(); break;
    case 1000: t = TWAI_TIMING_CONFIG_1MBITS(); break;
    default:   t = TWAI_TIMING_CONFIG_500KBITS(); break;
  }
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) { Serial.println("CAN driver install FAILED"); return false; }
  if (twai_start() != ESP_OK) { Serial.println("CAN start FAILED"); twai_driver_uninstall(); return false; }
  canUp = true;
  return true;
}

void sendStatus() {
  if (!canUp || !cfg.status_ms) return;
  twai_message_t m = {};
  m.identifier = cfg.status_id;
  m.extd = cfg.status_ext;
  m.data_length_code = 8;
  for (int i = 0; i < N; i++) m.data[i] = state[i];
  m.data[5] = inputs;
  m.data[7] = counter++;
  twai_transmit(&m, 0);
}

void handleCommand(const twai_message_t &m) {
  if (m.rtr || m.extd != cfg.cmd_ext || m.identifier != cfg.cmd_id) return;
  cmdFrames++;
  lastCmd = millis();
  cmdLive = true;
  if (cfg.sync_mask) {
    if (cfg.sync_byte >= m.data_length_code || !(m.data[cfg.sync_byte] & cfg.sync_mask)) { ignoredFrames++; return; }
  }
  for (int i = 0; i < N; i++) {
    if (cfg.out_byte[i] >= m.data_length_code) continue;
    bool on = (m.data[cfg.out_byte[i]] & cfg.out_mask[i]) != 0;
    setState(i, cfg.out_invert[i] ? !on : on, "CAN");
  }
}

void readInputs() {
  uint8_t raw = 0;
  for (int i = 0; i < N; i++)
    if (cfg.in_pin[i] >= 0 && digitalRead(cfg.in_pin[i]) == (cfg.in_active_low ? LOW : HIGH)) raw |= 1 << i;
  if (raw != inputsRaw) { inputsRaw = raw; inputsChanged = millis(); }
  if (raw != inputs && millis() - inputsChanged >= 50) {          // 50 ms debounce
    for (int i = 0; i < N; i++)
      if ((raw ^ inputs) & (1 << i)) Serial.printf("input %d %s\n", i + 1, raw & (1 << i) ? "ACTIVE" : "off");
    inputs = raw;
    sendStatus();                                                 // report straight away
  }
}

// ---------------------------------------------------------------- console
void showConfig() {
  Serial.printf("bitrate %lu | cmd_id 0x%lX%s | status_id 0x%lX%s every %lu ms | sync byte %u mask 0x%02X%s | timeout_ms %lu\n",
    cfg.bitrate, cfg.cmd_id, cfg.cmd_ext ? " (ext)" : "", cfg.status_id, cfg.status_ext ? " (ext)" : "", cfg.status_ms,
    cfg.sync_byte, cfg.sync_mask, cfg.sync_mask ? "" : " (off)", cfg.timeout_ms);
  for (int i = 0; i < N; i++)
    Serial.printf("output %d: pin %d, byte %u mask 0x%02X%s, restore %u | input %d: pin %d\n", i + 1,
      cfg.out_pin[i], cfg.out_byte[i], cfg.out_mask[i], cfg.out_invert[i] ? " inverted" : "", cfg.restore[i],
      i + 1, cfg.in_pin[i]);
  Serial.printf("inputs active %s | CAN pins tx %d rx %d\n", cfg.in_active_low ? "low" : "high", cfg.can_tx, cfg.can_rx);
}

void status() {
  twai_status_info_t s = {};
  if (canUp) twai_get_status_info(&s);
  for (int i = 0; i < N; i++)
    Serial.printf("out %d %s  ", i + 1, state[i] ? "ON " : "off");
  for (int i = 0; i < N; i++)
    if (cfg.in_pin[i] >= 0) Serial.printf("in %d %s  ", i + 1, inputs & (1 << i) ? "ACTIVE" : "off");
  Serial.printf("\nCAN %s %lu kbps, tx err %u, rx err %u | command frames %lu (ignored, no sync bit: %lu), %s\n",
    canUp ? "up" : "DOWN", cfg.bitrate, s.tx_error_counter, s.rx_error_counter, cmdFrames, ignoredFrames,
    cmdLive ? "sender LIVE" : "no sender");
}

void selfTest() {
  // NO_ACK + self-reception: frames go out through the transceiver and come back in on RX, which
  // proves ESP32 <-> transceiver wiring without any other node on the bus.
  Serial.println("self-test: sending 5 frames through the transceiver...");
  if (!startCan(TWAI_MODE_NO_ACK)) return;
  int ok = 0;
  for (int i = 0; i < 5; i++) {
    twai_message_t m = {};
    m.identifier = 0x7F0; m.self = 1; m.data_length_code = 2; m.data[0] = 0xA5; m.data[1] = i;
    twai_transmit(&m, pdMS_TO_TICKS(100));
    twai_message_t r;
    uint32_t t0 = millis();
    while (millis() - t0 < 100)
      if (twai_receive(&r, pdMS_TO_TICKS(20)) == ESP_OK && r.identifier == 0x7F0 && r.data[1] == i) { ok++; break; }
  }
  Serial.printf("self-test: %d/5 came back - %s\n", ok, ok == 5 ? "PASS" : "FAIL: check power/GND, swap TX/RX if 0/5");
  startCan(TWAI_MODE_NORMAL);
}

void help() {
  Serial.println(
    "on N | off N            switch output N (1-4) by hand; saved\n"
    "status                  outputs, inputs, CAN\n"
    "config                  show settings\n"
    "preset powertune|generic  load a CAN setup (keeps the board pins)\n"
    "board a2                load the KC868-A2 pins\n"
    "set <key> <value>       change a setting, saved straight away. Keys:\n"
    "    bitrate 125|250|500|1000   cmd_id 0x6A1   cmd_ext 0|1   status_id 0x6A2   status_ext 0|1\n"
    "    status_ms 100 (0 = off)    sync_byte 4    sync_mask 0x01 (0 = off)    timeout_ms 0 (0 = keep)\n"
    "    outN_byte 0-7   outN_mask 0x01   outN_invert 0|1   outN_restore 0|1   outN_pin 15 (-1 = none)\n"
    "    inN_pin 36 (-1 = none)   in_active_low 0|1   can_tx 4   can_rx 16\n"
    "factory                 back to factory defaults (powertune + board a2)\n"
    "selftest                CAN transceiver loop-back test\n"
    "reboot");
}

long num(const String &v) { return strtol(v.c_str(), nullptr, 0); }   // decimal or 0x..

bool setKey(String k, const String &v) {
  long x = num(v);
  if (k == "bitrate") { if (x != 125 && x != 250 && x != 500 && x != 1000) return false; cfg.bitrate = x; }
  else if (k == "cmd_id") cfg.cmd_id = x;
  else if (k == "cmd_ext") cfg.cmd_ext = x ? 1 : 0;
  else if (k == "status_id") cfg.status_id = x;
  else if (k == "status_ext") cfg.status_ext = x ? 1 : 0;
  else if (k == "status_ms") cfg.status_ms = x;
  else if (k == "sync_byte") { if (x < 0 || x > 7) return false; cfg.sync_byte = x; }
  else if (k == "sync_mask") cfg.sync_mask = x;
  else if (k == "timeout_ms") cfg.timeout_ms = x;
  else if (k == "in_active_low") cfg.in_active_low = x ? 1 : 0;
  else if (k == "can_tx") cfg.can_tx = x;
  else if (k == "can_rx") cfg.can_rx = x;
  else if (k.length() >= 6 && (k.startsWith("out") || k.startsWith("in")) ) {
    int p = k.startsWith("out") ? 3 : 2;
    int i = k[p] - '1';
    String f = k.substring(p + 1);
    if (i < 0 || i >= N || !f.startsWith("_")) return false;
    f = f.substring(1);
    if (p == 3 && f == "byte") { if (x < 0 || x > 7) return false; cfg.out_byte[i] = x; }
    else if (p == 3 && f == "mask") cfg.out_mask[i] = x;
    else if (p == 3 && f == "invert") cfg.out_invert[i] = x ? 1 : 0;
    else if (p == 3 && f == "restore") cfg.restore[i] = x ? 1 : 0;
    else if (p == 3 && f == "pin") cfg.out_pin[i] = x;
    else if (p == 2 && f == "pin") cfg.in_pin[i] = x;
    else return false;
  } else return false;
  return true;
}

void command(String c) {
  c.trim(); c.toLowerCase();
  int sp = c.indexOf(' ');
  String cmd = sp < 0 ? c : c.substring(0, sp);
  String arg = sp < 0 ? "" : c.substring(sp + 1);
  arg.trim();
  if (cmd == "on" || cmd == "off") {
    int i = arg.toInt() - 1;
    if (i >= 0 && i < N) setState(i, cmd == "on", "console"); else Serial.println("output 1-4");
  }
  else if (cmd == "status") status();
  else if (cmd == "config") showConfig();
  else if (cmd == "selftest") selfTest();
  else if (cmd == "reboot") ESP.restart();
  else if (cmd == "factory") { factoryDefaults(cfg); saveConfig(); Serial.println("factory defaults - rebooting"); delay(100); ESP.restart(); }
  else if (cmd == "preset") {
    if (arg == "powertune") presetPowertune(cfg);
    else if (arg == "generic") presetGeneric(cfg);
    else { Serial.println("presets: powertune, generic"); return; }
    saveConfig(); startCan(TWAI_MODE_NORMAL); showConfig();
  }
  else if (cmd == "board") {
    if (arg == "a2") boardA2(cfg); else { Serial.println("boards: a2 (or set the pins with set)"); return; }
    saveConfig(); Serial.println("pins saved - rebooting"); delay(100); ESP.restart();
  }
  else if (cmd == "set") {
    int s2 = arg.indexOf(' ');
    if (s2 < 0 || !setKey(arg.substring(0, s2), arg.substring(s2 + 1))) { Serial.println("bad setting - type help"); return; }
    saveConfig();
    String k = arg.substring(0, s2);
    if (k.endsWith("_pin") || k.startsWith("can_")) { Serial.println("pin saved - rebooting"); delay(100); ESP.restart(); }
    if (k == "bitrate") startCan(TWAI_MODE_NORMAL);
    showConfig();
  }
  else if (c.length()) help();
}

// ---------------------------------------------------------------- main
void setup() {
  prefs.begin("canrelay", false);
  loadConfig();
  // outputs first, from the saved states, before anything else
  uint8_t bits = prefs.getUChar("bits", 0);
  for (int i = 0; i < N; i++) state[i] = cfg.restore[i] && (bits & (1 << i));
  setupPins();
  saveStates();

  Serial.begin(115200);
  delay(200);
  Serial.println("\nCAN relay box - type help");
  showConfig();
  startCan(TWAI_MODE_NORMAL);
  status();
}

void loop() {
  if (canUp) {
    twai_message_t m;
    while (twai_receive(&m, 0) == ESP_OK) handleCommand(m);
    twai_status_info_t s; twai_get_status_info(&s);
    if (s.state == TWAI_STATE_BUS_OFF) { Serial.println("CAN bus-off, recovering"); twai_initiate_recovery(); }
    else if (s.state == TWAI_STATE_STOPPED) twai_start();
  }
  if (cmdLive && millis() - lastCmd > (cfg.timeout_ms ? cfg.timeout_ms : 1000)) {
    cmdLive = false;
    if (cfg.timeout_ms) {
      Serial.println("command frames stopped - outputs off");
      for (int i = 0; i < N; i++) setState(i, false, "timeout");
    }
  }
  readInputs();
  if (cfg.status_ms && millis() - lastSend >= cfg.status_ms) { lastSend = millis(); sendStatus(); }
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { command(line); line = ""; }
    else if (line.length() < 60) line += ch;
  }
}
