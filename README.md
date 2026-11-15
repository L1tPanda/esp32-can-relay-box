# ESP32 CAN relay box

An ESP32 board with MOSFETs (or relays) and a CAN transceiver that switches its outputs from CAN
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
CANH, CANL). The factory pins (`board mos4`) are for the **ESP32 Quad MOS Switch Module**:
ESP32-32E, four N-channel MOSFET outputs, 5-60 V supply, USB-C, every GPIO on headers.

| | GPIO |
|---|---|
| Output 1-4 | **not published: find them with `scan`** (see below), then `set out1_pin ..` to `out4_pin` |
| Input 1 / 2 | 34 / 35 (input-only pins), through a divider (see below), active low |
| CAN TX / RX | 22 / 21 → module CTX / CRX, plus 3V3 and GND from the header |

**Finding the output pins:** disconnect any loads, open the serial console and type `scan`. Each
GPIO goes high for 1.5 s in turn and its number is printed; note which number lights each output's
LED (or a test lamp across OUT+ / OUT-). Then `set out1_pin <GPIO>` and so on. If `scan` shows a
MOSFET on GPIO21 or 22, move CAN to two other free pins with `set can_tx` / `set can_rx`.

**Outputs are low-side:** a MOSFET connects its **OUT−** terminal to ground when on; **OUT+** is the
supply. Wire whatever needs grounding to OUT−:

- an ECU input that is active low (e.g. a map-select input): ECU pin → OUT−. The ECU's own pull-up
  does the rest; leave OUT+ unconnected.
- a relay coil: coil from switched +12 V to OUT−. Power the board from the same switched +12 V.

Check with a meter that OUT− really is the switched side before wiring a car to it.

**Bus termination:** with everything off, measure CAN-H to CAN-L. About 60 Ω means the bus is
already terminated at both ends, so remove the module's 120 Ω resistor (usually marked "121").

**Example (MX-5 with a Motorsport Electronics ME442):** output 1 (OUT−) grounds the ECU's map-select
input (active low) for map 2; output 2 (OUT−) grounds the fan relay coil in parallel with the ECU;
input 2 watches the fan relay coil, so the dash can show "fan running" even when the ECU switched it
on.

### Sensed inputs and protection (12-14.4 V vehicles)

ESP32 pins take 3.3 V at most, so each input goes through a divider with a clamp. GPIO34/35 are
input-only pins with no internal pull-ups, which suits this.

```
  signal ──[ 47 kΩ ]──┬──────────────► GPIO34 (input 1) / GPIO35 (input 2)
                      │
                   [ 10 kΩ ]   3.3 V zener (or BAT54S to 3V3 and GND)
                      │              │
  GND ────────────────┴──────────────┘
```

14.4 V becomes about 2.5 V at the pin (reads high); a grounded signal reads low. The 47 kΩ limits
the current from spikes and the clamp holds the pin at 3.3 V, so a relay coil's switch-off spike
can't reach the ESP32. A 100 nF capacitor across the 10 kΩ filters noise if the wires are long.

- **Sensing a relay coil switched to ground** (e.g. the fan relay, by the ECU): connect the coil's
  low side as the signal. It reads low when the coil is on, so keep `in_active_low 1`.
- **Flyback diode across the coil** (1N4007, band towards +12 V), unless the relay already has one
  (check its diagram). It stops the switch-off spike at the source and protects the ECU's output
  and the MOSFET.
- **Sensing a line with its own pull-up** (e.g. a 5 V ECU input): 5 V gives about 0.9 V through
  this divider, which won't read high. Use 10 kΩ / 10 kΩ for 5 V signals, or skip it: the box
  already knows what it drives.

**Powering the box from the vehicle:** a fused, ignition-switched supply. A TVS diode across the
supply input (e.g. SMBJ24A, band to +) clamps load-dump spikes.

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
| `board mos4` | load the Quad MOS Switch Module pins (inputs, CAN) |
| `scan` | pulse each GPIO in turn to find which one drives each output |
| `set <key> <value>` | change a setting (saved straight away; numbers in decimal or `0x..`) |
| `on N` / `off N` | switch output N by hand (saved) |
| `selftest` | sends 5 frames out through the transceiver and reads them back: proves the wiring with no other node on the bus |
| `factory` | back to factory defaults (`powertune` + `board mos4`) |
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

The OpenSCAD source (`can_relay_box.scad`) is parametric: board size, mounting holes, cable entry,
USB slot, CAN module cradle, mounting ears and strap slots. The current files are laid out for an
earlier board (KinCony KC868-A2); a version for the Quad MOS Switch Module will follow once that
board has been measured. Print in PETG or ASA, not PLA, if it goes in a car. Build notes:
[`enclosure/PRINT_NOTES.md`](enclosure/PRINT_NOTES.md).

## Testing without hardware

The [powertune-dash-emulator](https://github.com/L1tPanda/powertune-dash-emulator) has
`fake_relay_box.py`, which behaves like this box with the `powertune` preset (over UDP).
