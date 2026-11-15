# CAN relay box enclosure – print and build notes

Case for a **KinCony KC868-A2 (V2.6)** plus an **SN65HVD230** CAN module. It follows the dash's
MAP / FAN buttons (CAN `0x6A1`) with the A2's two relays. The green terminal blocks are removed
and the wires are soldered to the pads.

**Outside: 125 × 111 × 34 mm**, plus 13 mm mounting ears top and bottom.

## Board measurements used (measured 2026-11-08)
| | Value | Setting |
|---|---|---|
| Board | 87 wide × 93 tall | `pcb_x`, `pcb_y` |
| Mounting holes | Ø3.8, 3 mm in from each edge: (3,3) (84,3) (3,90) (84,90) | `pcb_holes` |
| Power/CAN pads (left edge) | 46–77 mm up from the bottom edge | `pad_left` |
| Relay pads (right edge) | 39–65 mm up | `pad_right` |
| USB-C (left edge) | centre 38 mm up, 3 mm above the board | `usb_y`, `usb_z` |
| Tallest part / under the board | 15 / 4 mm | `comp_h`, `under_h` |
| CAN module | 16 × 14 | `mod_l`, `mod_w` |

**Assumption:** "3,3" on the sheet was taken as each hole being 3 mm from both nearest edges.
If a hole is somewhere else, change `pcb_holes` and re-export.

## Layout
- **One cable entry for everything:** an M16 gland (16.2 mm hole) on the left wall, just above
  the USB slot, in line with the power/CAN pads. Run power, ground, CAN-H, CAN-L and the 4 relay
  wires through it, ideally as one 8-core cable or a sleeved bundle 4–8 mm across.
- **USB-C slot** (14 × 9 mm) below the gland, for reflashing with the lid on. The left bay is
  22 mm wide; a plug with a long moulded body may need a short USB-C extension.
- **Relay wires:** from the right-edge pads, down the 11 mm right bay, across the 10 mm channel
  along the top edge, then down the left bay to the gland (red line in the preview).
- **Left bay, bottom:** CAN module on foam tape between 4 corner stops; all sides are open so
  the wires can leave the pin edge either way round.
- **3 zip-tie bridges** on the floor (by the gland, in the top channel, in the right bay):
  tie the wires down so pulling the cable never loads the soldered joints.
- The ESP32 aerial (bottom edge) has open space and no metal near it.

## Files
| File | Material | Orientation |
|---|---|---|
| can_relay_box.stl | PETG or ASA | Opening up, no supports |
| can_relay_lid.stl | PETG or ASA | Already flipped: label side on the bed |
| can_relay_box.scad | – | Parametric source (`part = assembly / box / lid`) |

**Suggested settings:** 0.2 mm layers, 4 walls, 5 top and bottom layers, 30 % infill. **Don't use PLA.**

## Hardware
- 4× M3 heat-set inserts (4.0 mm hole) in the corner bosses, 4× M3 × 8 screws for the lid.
- Board: 4× M3 self-tapping screws (or M2.5) into the 6 mm standoffs (2.9 mm pilot).
  For inserts, set `pcb_hole_screw = 4.0`.
- 1× M16 cable gland (4–8 mm cable), or a 16 mm rubber grommet since it lives inside the car.
- 3 small zip ties (≤ 2.5 mm wide) for the wire bridges.

## Mounting
- **4 ears with 5.5 mm slots:** M5 bolts, or self-tappers into a bracket or panel.
- **2 strap slots under the floor:** a zip tie or velcro strap round a bracket or loom.
- Keep it in the cabin (e.g. by the ECU in the passenger footwell), away from heater ducts.
