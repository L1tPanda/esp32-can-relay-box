// CAN relay box – enclosure for a KinCony KC868-A2 V2.6 + SN65HVD230 CAN module
// Follows the dash's MAP / FAN buttons (CAN 0x6A1) with the A2's two relays.
// Green terminal blocks removed; wires are soldered to the pads.
//
// Board coordinates: origin = bottom-left corner of the board, laid as in the photo
// (power/CAN pads + USB-C on the LEFT, relay pads on the RIGHT, ESP32 aerial at the BOTTOM).
// Measured by the user 2026-11-08.
//
// Print box and lid in PETG or ASA (PLA softens in a hot car). No supports needed.
// Hardware: 4x M3 heat-set inserts + M3x8 (lid), 4x M3 self-tappers (board), 1x M16 gland.

/* [View] */
part = "assembly"; // [assembly, box, lid]
show_lid = false;  // assembly: tick to put the lid on

/* [KC868-A2 V2.6 – measured] */
pcb_x = 87;            // A: width, left edge to right edge
pcb_y = 93;            // B: height, bottom edge to top edge
pcb_t = 1.6;
pcb_holes = [[3, 3], [84, 3], [3, 90], [84, 90]];   // H1..H4 (3 mm in from each edge)
pcb_hole_screw = 2.9;  // board holes are 3.8 mm: M3 self-tapper into a 2.9 mm pilot
comp_h = 15;           // C: tallest part (relays)
under_h = 4;           // D: pins under the board
pad_left = [46, 77];   // P1..P2: the 9 power/CAN pads, along the left edge
pad_right = [39, 65];  // R1..R2: the 6 relay pads, along the right edge
usb_y = 38;            // U: USB-C centre from the bottom edge
usb_z = 3;             // E: USB-C centre above the board top
standoff_d = 6.5;

/* [CAN module – SN65HVD230] */
mod_l = 16;            // M1
mod_w = 14;            // M2
mod_wall_h = 4;

/* [Box] */
wall = 2.5;
floor_t = 2.5;
corner_r = 4;
gap_left = 22;         // left bay: cable entry, CAN module, USB
gap_right = 11;        // right bay: relay wires turn up into the channel
gap_bottom = 3;
gap_top = 10;          // wire channel along the top: relay wires run across to the left bay
top_clear = 6;
boss_d = 8;
boss_hole = 4.0;       // M3 heat-set insert

/* [Cable entries] */
// ONE entry on the left wall, next to the USB slot, for every wire
gland_d = 16.2;        // M16 gland (4-8 mm cable / bundle); 20.2 for M20 won't fit the height
gland_z = 14;          // hole centre above the inside floor (clears the gland nut)
usb_slot = true;
usb_w = 14;           // wide enough for the cable's moulded plug to pass the 22 mm bay
usb_h = 9;

/* [Wire tie-downs (strain relief for the soldered joints)] */
ties = true;
tie_w = 3.2;           // zip tie up to 2.5 mm wide
tie_h = 2;

/* [Lid] */
lid_t = 2.5;
lip_h = 2.5;
lip_t = 1.6;
lip_clear = 0.3;
label = "CAN RELAY";
label_size = 9;
led_holes = [];        // optional LED windows, board coords [[x, y], ...]
led_d = 3.2;

/* [Mounting] */
ears = true;           // 4 ears with slots on the top and bottom walls
ear_len = 13;
ear_w = 16;
ear_t = 4;
ear_slot_w = 5.5;
ear_slot_l = 8;
ear_inset = 16;
strap_slots = true;    // slots under the floor for a zip tie / velcro strap

$fn = 48;

// ---------- derived ----------
ix = gap_left + pcb_x + gap_right;
iy = gap_bottom + pcb_y + gap_top;
standoff_h = under_h + 2;
ih = standoff_h + pcb_t + comp_h + top_clear;
ox = ix + 2 * wall;
oy = iy + 2 * wall;
oh = floor_t + ih;
org = [wall + gap_left, wall + gap_bottom, floor_t + standoff_h];   // board corner in box coords
bo = wall + boss_d/2 - 1;
boss_xy = [[bo, bo], [ox - bo, bo], [bo, oy - bo], [ox - bo, oy - bo]];
gl_y = org[1] + (pad_left[0] + pad_left[1]) / 2;    // gland in line with the 9 pads, just above the USB
chan_y = org[1] + pcb_y + gap_top / 2;              // middle of the top wire channel
cradle = [wall + 2, org[1] + 6];                    // CAN module, bottom of the left bay

module rbox(x, y, h, r) {
    r2 = max(r, 0.01);
    hull() for (i = [r2, x - r2], j = [r2, y - r2]) translate([i, j, 0]) cylinder(r = r2, h = h);
}

module ear() {
  translate([0, 1, 0]) {
    difference() {
        hull() {
            translate([-ear_w/2, -0.01, 0]) cube([ear_w, 0.01, ear_t]);
            translate([-ear_w/2 + 4, -ear_len + 4, 0]) cylinder(r = 4, h = ear_t);
            translate([ ear_w/2 - 4, -ear_len + 4, 0]) cylinder(r = 4, h = ear_t);
        }
        hull() for (d = [-ear_len + 4.5, -ear_len + 4.5 + ear_slot_l - ear_slot_w])
            translate([0, d + ear_slot_w/2 - 1, -1]) cylinder(d = ear_slot_w, h = ear_t + 2);
    }
    translate([-1.5, 0, 0]) rotate([90, 0, 90]) linear_extrude(3)
        polygon([[0, ear_t], [0, ear_t + 8], [-6, ear_t]]);
  }
}

// zip-tie anchor: a bridge on the floor; the tie goes under it and round the wire bundle
module tie_anchor() {
    difference() {
        translate([-tie_w/2 - 2, -5, 0]) cube([tie_w + 4, 10, tie_h + 2]);
        translate([-tie_w/2, -6, -0.01]) cube([tie_w, 12, tie_h]);
    }
}

module box() {
    difference() {
        union() {
            rbox(ox, oy, oh, corner_r);
            if (ears) for (x = [ear_inset, ox - ear_inset]) {
                translate([x, 0, 0]) ear();
                translate([x, oy, 0]) mirror([0, 1, 0]) ear();
            }
        }
        translate([wall, wall, floor_t]) rbox(ix, iy, oh, max(corner_r - wall, 0.5));
        // cable entries
        translate([-1, gl_y, floor_t + gland_z]) rotate([0, 90, 0]) cylinder(d = gland_d, h = wall + 2);
        // USB-C slot, left wall
        if (usb_slot)
            translate([-1, org[1] + usb_y - usb_w/2, org[2] + pcb_t + usb_z - usb_h/2]) cube([wall + 2, usb_w, usb_h]);
        // strap slots under the floor, across the box
        if (strap_slots) for (x = [org[0] + 15, org[0] + pcb_x - 15])
            translate([x - 2.5, wall + 8, -1]) cube([5, oy - 2 * wall - 16, 1.2 + 1]);
    }
    for (p = boss_xy) translate([p[0], p[1], floor_t - 0.01]) difference() {
        cylinder(d = boss_d, h = ih - lip_h - 0.5);
        translate([0, 0, 2]) cylinder(d = boss_hole, h = ih);
    }
    for (h = pcb_holes) translate([org[0] + h[0], org[1] + h[1], floor_t - 0.01]) difference() {
        cylinder(d = standoff_d, h = standoff_h + 0.01);
        translate([0, 0, 1]) cylinder(d = pcb_hole_screw, h = standoff_h + 1);
    }
    // CAN module cradle: 4 corner stops (module on foam tape), open sides so the wires
    // can leave the pin edge whichever way round it sits
    translate([cradle[0], cradle[1], floor_t - 0.01]) difference() {
        cube([mod_w + 3.2, mod_l + 3.2, mod_wall_h]);
        translate([1.6, 1.6, -1]) cube([mod_w, mod_l, mod_wall_h + 2]);
        translate([4, -1, -1]) cube([mod_w + 3.2 - 8, mod_l + 6, mod_wall_h + 2]);
        translate([-1, 4, -1]) cube([mod_w + 6, mod_l + 3.2 - 8, mod_wall_h + 2]);
    }
    // wire tie-downs: by the gland, in the top channel, and in the right bay
    if (ties) {
        translate([wall + gap_left * 0.55, gl_y, floor_t - 0.01]) tie_anchor();
        translate([org[0] + pcb_x / 2, chan_y, floor_t - 0.01]) tie_anchor();
        translate([ox - wall - gap_right / 2, org[1] + pad_right[1] + 10, floor_t - 0.01])
            rotate([0, 0, 90]) tie_anchor();
    }
}

module lid() {
    difference() {
        rbox(ox, oy, lid_t, corner_r);
        for (p = boss_xy) {
            translate([p[0], p[1], -1]) cylinder(d = 3.4, h = lid_t + 2);
            translate([p[0], p[1], lid_t - 1.2]) cylinder(d = 6.2, h = 2);
        }
        for (l = led_holes) translate([org[0] + l[0], org[1] + l[1], -1]) cylinder(d = led_d, h = lid_t + 2);
        translate([ox/2, oy/2, lid_t - 0.6]) linear_extrude(1)
            text(label, size = label_size, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
    difference() {
        translate([wall + lip_clear, wall + lip_clear, -lip_h])
            rbox(ix - 2 * lip_clear, iy - 2 * lip_clear, lip_h, max(corner_r - wall, 0.5));
        translate([wall + lip_clear + lip_t, wall + lip_clear + lip_t, -lip_h - 1])
            rbox(ix - 2 * (lip_clear + lip_t), iy - 2 * (lip_clear + lip_t), lip_h + 2, 0.5);
        for (p = boss_xy) translate([p[0], p[1], -lip_h - 1]) cylinder(d = boss_d + 1.5, h = lip_h + 2);
    }
}

module board_dummy() {   // stand-in for the A2 (positions scaled from the photo) and the module
    translate(org) {
        color("seagreen") difference() {
            cube([pcb_x, pcb_y, pcb_t]);
            translate([39, -1, -1]) cube([9, 6, pcb_t + 2]);   // notch by the aerial
            for (h = pcb_holes) translate([h[0], h[1], -1]) cylinder(d = 3.8, h = pcb_t + 2);
        }
        color("goldenrod") translate([1, pad_left[0], pcb_t]) cube([3, pad_left[1] - pad_left[0], 0.6]);
        color("goldenrod") translate([pcb_x - 4, pad_right[0], pcb_t]) cube([3, pad_right[1] - pad_right[0], 0.6]);
        color("dimgray") translate([57, 59, pcb_t]) cube([20, 15, comp_h]);   // relays
        color("dimgray") translate([57, 39, pcb_t]) cube([20, 15, comp_h]);
        color("silver") translate([65, 15, pcb_t]) cube([22, 17, 13.5]);    // RJ45
        color("whitesmoke") translate([62, 4, pcb_t]) cube([14, 6, 6]);      // JST
        color("lightgray") translate([16, 1, pcb_t]) cube([18, 26, 3]);      // ESP32
        color("lightgray") translate([-1, usb_y - 4.5, pcb_t + usb_z - 1.6]) cube([7.5, 9, 3.2]);
        for (y = [20, 27]) color("black") translate([1, y, pcb_t]) cube([6, 6, 4]);   // buttons
    }
    color("mediumblue") translate([cradle[0] + 1.6, cradle[1] + 1.6, floor_t + 1]) cube([mod_w, mod_l, 1.6]);
    // cables
    color("orange") translate([-12, gl_y, floor_t + gland_z]) rotate([0, 90, 0]) cylinder(d = 7, h = 12 + wall + gap_left * 0.6);
    // relay wires: right pads -> right bay -> top channel -> left bay -> gland
    rx = ox - wall - gap_right / 2;
    lx = wall + gap_left * 0.6;
    path = [[org[0] + pcb_x - 2, org[1] + (pad_right[0] + pad_right[1]) / 2, org[2] + 3],
            [rx, org[1] + (pad_right[0] + pad_right[1]) / 2, floor_t + 3],
            [rx, chan_y, floor_t + 3], [lx, chan_y, floor_t + 3],
            [lx, gl_y, floor_t + gland_z]];
    for (i = [0 : len(path) - 2]) color("red") hull() {
        translate(path[i]) sphere(d = 4, $fn = 16);
        translate(path[i + 1]) sphere(d = 4, $fn = 16);
    }
}

if (part == "box") box();
else if (part == "lid") translate([0, oy, lid_t]) rotate([180, 0, 0]) lid();   // label side on the bed, reads correctly
else {
    color("dimgray", 0.9) box();
    board_dummy();
    if (show_lid) color("silver", 0.6) translate([0, 0, oh]) lid();
    else color("silver") translate([0, oy + ear_len + 10, 0]) translate([0, oy, lid_t]) rotate([180, 0, 0]) lid();
}
echo(str("Outside: ", ox, " x ", oy, " x ", oh + lid_t, " mm (without ears); inside ", ix, " x ", iy, " x ", ih));
