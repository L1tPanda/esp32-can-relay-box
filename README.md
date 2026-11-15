# ESP32 CAN relay box

An ESP32 board with relays (or MOSFETs) and a CAN transceiver that switches its outputs from CAN
frames, and reports its output states and sensed inputs back on the bus. Use it to give any CAN
device (a dash, a keypad, a PLC, a CAN tool) real switched outputs: a map switch, a fan override,
lights, a pump.

- **Works with any CAN system.** Bitrate, frame ids, which byte or bit drives each output, the status
  frame and a safety timeout are all set from the serial console and saved on the board. No
  recompiling.
- **Keeps its states** through power cycles if you want: the outputs are right as soon as power comes
  on, before the sending device has even booted.
- **Senses up to 4 inputs** and reports them in its status frame (e.g. whether an ECU is running the
  fan).
- **Presets:** `powertune` for a PowerTune dash running
  [powertune-me442-custom-firmware](https://github.com/L1tPanda/powertune-me442-custom-firmware)
  (patches 0003/0004), and `generic`.

Unofficial and not affiliated with any of the devices it's used with. If you connect it to engine
management or other safety-related wiring, that's at your own risk: test on the bench first.
Licensed GPL-3.

![Open box](enclosure/preview_open.png)

## How it works

**Command frame** (`cmd_id`), from your device: output N is on when
`(byte out_byteN & out_maskN) != 0` (or off, with `outN_invert 1`). Each output can use its own byte
or bit, so 4 bits of one byte work as well as 4 bytes.

**Sync bit** (optional, `sync_mask` ≠ 0): the box only follows a command frame when
`byte sync_byte & sync_mask` is set. A sender that is still starting up (all off) then can't
overwrite the saved states. The sender sets the bit once it has read the box's status frame and taken
on its states.

**Status frame** (`status_id`), every `status_ms` and straight away when an input changes:

| Byte | Meaning |
|---|---|
| 0-3 | output states 1-4 (0 / 1) |
| 4 | 0 |
| 5 | sensed inputs: bit 0 = input 1 ... bit 3 = input 4 |
| 6 | 0 |
| 7 | counter, +1 every frame |

**Power-up:** each output with `outN_restore 1` comes back in its saved state; the others start off.
States are saved in flash (NVS) when they change.

**Timeout** (`timeout_ms` > 0): all outputs go off if no command frame arrives for that long, e.g.
when the sender dies. With 0 the box keeps its states.

## Presets

| | `powertune` (factory default) | `generic` |
|---|---|---|
| Bitrate | 500 kbps | 500 kbps |
| Command frame | `0x6A1`, bytes 0-3 = outputs 1-4 | `0x700`, bytes 0-3 = outputs 1-4 |
| Sync bit | byte 4 bit 0 | off |
| Status frame | `0x6A2`, 10 Hz | `0x701`, 10 Hz |
| Power-up | saved states restored | all off |
| Timeout | off (keep states) | 1 s |

With the `powertune` preset the dash's **CAN button 1-4** drive outputs 1-4, the dash shows the box's
states after it boots, and **CAN input 1-4** sources show the sensed inputs (dash patch 0004).

`preset <name>` changes only the CAN settings. The board pins stay as they are.

## Hardware

Any ESP32 with free GPIOs plus a 3.3 V CAN transceiver module (e.g. SN65HVD230: CTX, CRX, 3V3, GND,
CANH, CANL). Factory pins are for the **KinCony KC868-A2 V2.6** (`board a2`):

| | GPIO |
|---|---|
| Output 1 / 2 | 15 / 2 (relays) |
| Input 1 / 2 | 36 / 39 (opto inputs IN1 / IN2, active when pulled to ground) |
| CAN TX / RX | 4 / 16: the I2C header, SDA → module CTX, SCL → module CRX |

Its 3.3 kΩ I2C pull-ups don't affect CAN. On other boards set `outN_pin`, `inN_pin`,
`in_active_low`, `can_tx` and `can_rx`.

**Bus termination:** with everything off, measure CAN-H to CAN-L. About 60 Ω means the bus is
already terminated at both ends, so remove the module's 120 Ω resistor (usually marked "121").

**Example (MX-5 with a Motorsport Electronics ME442):** output 1 grounds the ECU's map-select input
(active low) for map 2; output 2 grounds the fan relay coil in parallel with the ECU; input 1 watches
the map-select line and input 2 the fan relay coil, so the dash can show "fan running" even when the
ECU switched it on.

### Protecting the inputs (12-14.4 V vehicles)

The KC868-A2's IN1/IN2 are **dry-contact** opto inputs: the board feeds the optocoupler from its own
supply and the input is "active" when something pulls it to ground. They aren't meant to have a
voltage fed in, and KinCony publishes no voltage rating. Steady battery voltage (14.4 V running) on
an input is harmless when the A2 runs from the same supply. The danger is **switching spikes**: when
a relay coil is switched off, its low side can jump to tens of volts or more for a moment.

**Sensing a relay coil (e.g. the fan relay, switched to ground by the ECU):** fit both diodes.

```
   +12V (switched) ───────┬──────────────┐
                          │              │
                     relay coil     flyback diode 1N4007
                          │           (band towards +12V)
                          │              │
   ECU output (to GND) ───┴──────────────┘ ◄── coil low side
                          │
                          │  1N4007, band (cathode) towards the coil
                          ▼
                     A2 IN2 (anode side)
```

- **Series diode** (1N4007, or 1N4148 for short wires): anode on the A2 input, cathode (band) on
  the coil's low side. When the ECU grounds the coil, the input still pulls to ground through the
  diode (about 0.7 V drop, still reads active). When the coil is off or spiking, the diode blocks
  it (1N4007: 1000 V).
- **Flyback diode across the coil** (1N4007, band towards +12 V), or use a relay with a built-in
  diode. It stops the spike at its source and protects the ECU's output too. Skip it if the relay
  already has one (check the relay's diagram, or measure across pins 85/86).

**Sensing an ECU input with its own pull-up (e.g. a 5 V map-select input):** check before you rely
on it. The A2 input sits near its supply voltage, so it can push current into the ECU's input, which
may read wrong or make the box's input read active all the time. A series diode doesn't help here
(it points the wrong way). Measure the ECU input's voltage with the A2 input connected and
disconnected; if it changes, or the box shows the input active when it shouldn't be, leave that
input unconnected.

**Powering the box from the vehicle:** use a fused, ignition-switched supply. A TVS diode across the
supply input (e.g. SMBJ24A, band to +) clamps load-dump spikes. The A2's input capacitors are 35 V
parts.

**Boards without opto inputs:** feed the signal to a spare GPIO through a divider (47 kΩ from the
signal, 10 kΩ to GND: 14.4 V → 2.5 V) with a 3.3 V zener or a BAT54S clamp at the GPIO, and set
`in_active_low` to suit (a coil's low side reads low when the coil is on).

## Firmware (`firmware/can_relay`)

Arduino, ESP32 core 3.x. With [arduino-cli](https://arduino.github.io/arduino-cli/):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/can_relay
arduino-cli upload --fqbn esp32:esp32:esp32 -p COM3 firmware/can_relay
```

Back up the board first if it holds anything you want to keep:
`esptool --port COM3 read_flash 0 0x400000 backup.bin`.

### Serial console (115200 baud)

| Command | Does |
|---|---|
| `help` | list the commands and settings |
| `status` | outputs, inputs, CAN state and error counters, command frames seen |
| `config` | show all settings |
| `preset powertune` / `preset generic` | load a CAN setup |
| `board a2` | load the KC868-A2 pins |
| `set <key> <value>` | change a setting (saved straight away; numbers in decimal or `0x..`) |
| `on N` / `off N` | switch output N by hand (saved) |
| `selftest` | sends 5 frames out through the transceiver and reads them back: proves the wiring with no other node on the bus |
| `factory` | back to factory defaults (`powertune` + `board a2`) |
| `reboot` | restart |

Settings:

| Key | Values |
|---|---|
| `bitrate` | 125, 250, 500, 1000 (kbps) |
| `cmd_id`, `cmd_ext` | command frame id; 1 = 29-bit extended id |
| `status_id`, `status_ext`, `status_ms` | status frame id, extended, period (0 = no status frame) |
| `sync_byte`, `sync_mask` | sync bit location; mask 0 = off |
| `timeout_ms` | outputs off after this long without a command frame; 0 = keep states |
| `outN_byte`, `outN_mask`, `outN_invert` | which byte/bits of the command frame drive output N (1-4) |
| `outN_restore` | 1 = restore output N's saved state at power-up |
| `outN_pin`, `inN_pin` | GPIO, -1 = not used (pin changes reboot the board) |
| `in_active_low` | 1 = an input is active when pulled low |
| `can_tx`, `can_rx` | GPIOs to the transceiver's TX (CTX) and RX (CRX) |

Example: a keypad sending `0x18FF1021` (extended) with buttons in bits 0-1 of byte 2, at 250 kbps:

```
set bitrate 250
set cmd_id 0x18FF1021
set cmd_ext 1
set out1_byte 2
set out1_mask 0x01
set out2_byte 2
set out2_mask 0x02
```

## Enclosure (`enclosure/`)

OpenSCAD source (`can_relay_box.scad`, parametric) and ready STLs for the KC868-A2:
**125 × 111 × 34 mm**, 4 slotted mounting ears and 2 strap slots. All wires leave through one M16
gland next to the USB-C slot (reflash with the lid on); the relay wires run along a channel at the
top edge. Zip-tie bridges take the strain off soldered joints. Print in PETG or ASA, not PLA, if it
goes in a car. Build notes: [`enclosure/PRINT_NOTES.md`](enclosure/PRINT_NOTES.md).

## Testing without hardware

The [powertune-dash-emulator](https://github.com/L1tPanda/powertune-dash-emulator) has
`fake_relay_box.py`, which behaves like this box with the `powertune` preset (over UDP).
