// Griffin Jr. enclosure: two-part printed shell for the rev C3 board.
//
// Everything that depends on the board comes from build/board.scad, written by
// board_geometry.py from the released KiCad board and parts.json (heights and mating-plug
// envelopes). Case coordinates: x to the right as on the board, y from the FRONT edge
// (PS/2, audio, SD, joystick, buttons), z = 0 at the board bottom; the board rests on the
// bottom half's corner bosses and pillars, the halves split at the board bottom.
//
//   openscad -D 'part="top"'      -o build/top.stl      case.scad   (print upside down)
//   openscad -D 'part="bottom"'   -o build/bottom.stl   case.scad
//   openscad -D 'part="inserts"'  -o build/inserts.stl  case.scad   (button caps, light pipes)
//   openscad -D 'part="assembly"' case.scad                         (preview with the board)
//
// Descended from the Rocinante rev1 case (~/trees/rocinante/case): same shell, lip, heat-set
// insert bosses, membrane-spring button pressors and LED light pipes; the bosses are fused into
// the corners of the walls, mid-board pillar pairs support the lid and the board, the serial
// headers get wall notches for their adapters, and every position is generated, not measured.

include <build/board.scad>
use <inserts.scad>

part = "assembly";
board_stl = true;    // assembly preview: import the merged stl/board.stl (false: slab and boxes only)

// ---------------------------------------------------------------- parameters (mm)
wall       = 2;      // side walls
wall_top   = 2;      // lid and tray floor
gap        = 0.5;    // board edge to inner wall
tray       = 4;      // inside depth below the board: lead ends and the DE-9's board locks (3.8 in the KiCad model)
top_space  = 1;      // above the tallest part
lip_h      = 2.5;    // the bottom half's lip rising into the lid
lip_fudge  = 0.15;   // clearance per side between lip and lid recess

boss_r       = 4.2;  // corner boss radius; it fuses into the two walls at the corner
insert_d     = 4.0;  // bore for an M3 heat-set insert (4.0 for the short OD 4.6 type; 5.3 for the long one)
insert_depth = 6;
screw_d      = 3.4;  // M3 clearance through the bottom boss
head_d       = 6.2;  // screw head recess
head_depth   = 3;    // leaves 2 mm of boss under the board; M3 x 8 screws

pillar_d     = 5;    // mid-board support posts, one pair per spot in parts.json
pillar_clear = 0.3;  // the lid post stops this short of the board top

slop       = 1;      // added on every side of a plug envelope
notch_fudge = 0.2;   // bottom tab vs notch, per side

pressor_d  = 6;      // button pressor column
spring_d   = 12;     // membrane spring ring around it
spring_t   = 0.4;    // membrane thickness (two 0.2 mm layers, printed first)
shaft_d    = 3.0;    // button cap shaft hole
pipe_in    = 4.25;   // light pipe bore (TAP rod stock)
pipe_out   = 6.25;

vent_w = 2; vent_pitch = 4; vent_margin = 2;
label_size = 2.6; label_depth = 0.6; label_inset = 4;   // labels sunk into the lid top; 0.6 = three 0.2 mm layers, see "labels"

$fn = 48;
eps = 0.01;

// ---------------------------------------------------------------- derived
in_x0 = -gap;            in_y0 = -gap;
in_x1 = board_w + gap;   in_y1 = board_d + gap;
out_x0 = in_x0 - wall;   out_y0 = in_y0 - wall;
out_x1 = in_x1 + wall;   out_y1 = in_y1 + wall;
r_in  = 3 + gap;         // board corner radius 3 mm plus the gap
r_out = r_in + wall;

z_floor_in  = -tray;
z_floor_out = z_floor_in - wall_top;
z_lid_in    = board_t + max_part_h + top_space;
z_lid_out   = z_lid_in + wall_top;
z_mid_wall  = (z_lid_in + board_t) / 2;

module rrect(x0, y0, x1, y1, r)
    translate([x0 + r, y0 + r]) offset(r = r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]);

// ---------------------------------------------------------------- shell and lip
module outer_solid() translate([0, 0, z_floor_out])
    linear_extrude(z_lid_out - z_floor_out) rrect(out_x0, out_y0, out_x1, out_y1, r_out);
module inner_void() translate([0, 0, z_floor_in])
    linear_extrude(z_lid_in - z_floor_in) rrect(in_x0, in_y0, in_x1, in_y1, r_in);
module shell() difference() { outer_solid(); inner_void(); }

// ring between the inner wall face and the wall's middle, grown by f, from the split up lip_h
module lip_ring(f) translate([0, 0, -eps]) linear_extrude(lip_h + eps) difference() {
    rrect(in_x0 - wall / 2 - f, in_y0 - wall / 2 - f, in_x1 + wall / 2 + f, in_y1 + wall / 2 + f, r_in + wall / 2 + f);
    rrect(in_x0 - f, in_y0 - f, in_x1 + f, in_y1 + f, r_in - f);
}

module above_split() translate([-500, -500, 0]) cube([1000, 1000, 1000]);
module below_split() translate([-500, -500, -1000]) cube([1000, 1000, 1000]);

// ---------------------------------------------------------------- corner bosses
// hull of the cylinder at the hole and the wedge to the two nearest walls: the insert keeps a
// full wall on every side although the hole is only 5 mm from both edges
module boss_shape(h, z0, hgt) {
    wx = h[0] < board_w / 2 ? in_x0 : in_x1;
    wy = h[1] < board_d / 2 ? in_y0 : in_y1;
    hull() {
        translate([h[0], h[1], z0]) cylinder(r = boss_r, h = hgt);
        for (p = [[wx, wy], [h[0], wy], [wx, h[1]]])
            translate([p[0] - eps, p[1] - eps, z0]) cube([2 * eps, 2 * eps, hgt]);
    }
}
module top_bosses() difference() {
    for (h = holes) boss_shape(h, board_t, z_lid_in - board_t + eps);
    for (h = holes) translate([h[0], h[1], board_t - eps]) cylinder(d = insert_d, h = insert_depth + eps);
}
module bottom_bosses() for (h = holes) boss_shape(h, z_floor_out, -z_floor_out);
module bottom_boss_holes() for (h = holes) translate([h[0], h[1], z_floor_out - eps]) {
    cylinder(d = screw_d, h = -z_floor_out + 2 * eps);
    cylinder(d = head_d, h = head_depth + eps);
}

// ---------------------------------------------------------------- pillars
module bottom_pillars() for (p = pillars) translate([p[0], p[1], z_floor_in - eps]) cylinder(d = pillar_d, h = tray + eps);
module top_pillars() for (p = pillars) translate([p[0], p[1], board_t + pillar_clear])
    cylinder(d = pillar_d, h = z_lid_in - board_t - pillar_clear + eps);

// ---------------------------------------------------------------- wall cutouts
// children: a 2D shape centred on the connector axis, extruded through the named wall
module through_wall(edge, along, z, len = wall + gap + 2) {
    if (edge == "back")  translate([along, in_y1 - 1, z]) rotate([-90, 0, 0]) linear_extrude(len) children();
    if (edge == "front") translate([along, in_y0 + 1, z]) rotate([90, 0, 0])  linear_extrude(len) children();
    if (edge == "left")  translate([in_x0 + 1, along, z]) rotate([0, -90, 0]) linear_extrude(len) children();
    if (edge == "right") translate([in_x1 - 1, along, z]) rotate([0, 90, 0])  linear_extrude(len) children();
}
// a rectangular cutout whose top would leave less than 1.5 mm of wall under the lid runs up
// into the lid instead (the DE-9 hood on this board), so no unprintable sliver is left
module rect_cutout(c) {
    z0 = board_t + c[5] - c[4] / 2 - slop;
    z1 = board_t + c[5] + c[4] / 2 + slop;
    zt = z1 > z_lid_in - 1.5 ? z_lid_in + eps : z1;
    through_wall(c[1], c[2], (z0 + zt) / 2) square([c[3] + 2 * slop, zt - z0], center = true);
}
module cutouts() {
    for (c = rect_cutouts)  rect_cutout(c);
    for (c = round_cutouts) through_wall(c[1], c[2], board_t + c[4]) circle(d = c[3] + 2 * slop);
}

// serial header notches: open from the split up to z_top so the lid lifts off over the pins
// and an adapter housing can pass through the wall; the bottom half's wall rises into the notch
// as a tab up to tab_z, below the housing
module notch_box(n, w, z0, z1) {
    if (n[1] == "back")  translate([n[2] - w / 2, in_y1 - 1, z0]) cube([w, wall + 2, z1 - z0]);
    if (n[1] == "front") translate([n[2] - w / 2, out_y0 - 1, z0]) cube([w, wall + 2, z1 - z0]);
}
module notches() for (n = notches) notch_box(n, n[3] + 2 * slop, -1, board_t + n[4]);
// the tabs are the notch footprints merged (adjacent notches become one tab) and shrunk by the fudge
function min_tab_z() = min([for (n = notches) n[5]]);
module notch_tabs() if (len(notches) > 0) intersection() {
    translate([0, 0, -eps]) linear_extrude(board_t + min_tab_z() + eps)
        offset(delta = -notch_fudge) projection() for (n = notches) notch_box(n, n[3] + 2 * slop, 0, 1);
    difference() { outer_solid(); translate([in_x0, in_y0, -500]) cube([in_x1 - in_x0, in_y1 - in_y0, 1000]); }  // the wall only
}

// ---------------------------------------------------------------- buttons and LEDs
module pressors_add() for (b = buttons) translate([b[0], b[1], board_t + b[2] + 0.2])
    cylinder(d = pressor_d, h = z_lid_out - (board_t + b[2] + 0.2));
module pressors_sub() for (b = buttons) translate([b[0], b[1], board_t + b[2]]) {
    difference() {                                     // ring cavity, leaving the membrane spring on top
        cylinder(d = spring_d, h = z_lid_out - spring_t - (board_t + b[2]));
        cylinder(d = pressor_d, h = z_lid_out);
    }
    translate([0, 0, 2.2]) cylinder(d = shaft_d, h = z_lid_out);   // cap shaft hole; 2 mm of solid pressor foot stays
}
module pipes_add() for (l = leds) translate([l[0], l[1], board_t + l[2] + 0.4])
    cylinder(d = pipe_out, h = z_lid_out - (board_t + l[2] + 0.4));
module pipes_sub() for (l = leds) translate([l[0], l[1], board_t + l[2]]) cylinder(d = pipe_in, h = z_lid_out);

// ---------------------------------------------------------------- vents and labels
module vents_sub() difference() {
    for (v = vents) {
        x0 = v[1] + vent_margin; x1 = v[3] - vent_margin;
        y0 = v[2] + vent_margin; y1 = v[4] - vent_margin;
        for (y = [y0 : vent_pitch : y1 - vent_w])
            translate([x0, y, z_lid_in - 1]) cube([x1 - x0, vent_w, wall_top + 2]);
    }
    for (p = pillars) translate([p[0], p[1], 0]) cylinder(d = pillar_d + 6, h = 100);
}
module deboss_top(x, y, s) translate([x, y, z_lid_out - label_depth])
    linear_extrude(label_depth + eps) text(s, size = label_size, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
module deboss_front_wall(x, z, s) translate([x, out_y0 + label_depth, z]) rotate([90, 0, 0])
    linear_extrude(label_depth + eps) text(s, size = label_size, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
function label_y(edge) = edge == "back" ? in_y1 - label_inset : in_y0 + label_inset;
module labels_sub() {
    for (c = rect_cutouts)  deboss_top(c[2], label_y(c[1]), c[6]);
    for (c = round_cutouts) deboss_top(c[2], label_y(c[1]), c[5]);
    for (n = notches)       deboss_top(n[2], label_y(n[1]), n[6]);
    for (l = leds)          deboss_top(l[0], l[1] + pipe_out / 2 + 3, l[3]);
    for (b = buttons)       deboss_front_wall(b[0], z_mid_wall, b[3]);   // the spring ring fills the lid above them
}
// the same letters as solids filling the pockets exactly: printed in a second colour on a
// multi-material printer (load top.stl and labels.stl as one multi-part object), or used
// as a guide for a filament change at 0.6 mm since the lid prints face down
module labels_add() intersection() { labels_sub(); outer_solid(); }

// ---------------------------------------------------------------- the halves
module top_half() difference() {
    union() {
        difference() { intersection() { shell(); above_split(); } lip_ring(lip_fudge); }
        top_bosses();
        top_pillars();
        pressors_add();
        pipes_add();
    }
    cutouts(); notches(); pressors_sub(); pipes_sub(); vents_sub(); labels_sub();
}
module bottom_half() difference() {
    union() {
        intersection() { shell(); below_split(); }
        lip_ring(0);
        bottom_bosses();
        bottom_pillars();
        notch_tabs();
    }
    cutouts(); bottom_boss_holes();
}
module board_model() color("green", 0.5) difference() {
    linear_extrude(board_t) rrect(0, 0, board_w, board_d, 3);
    for (h = holes) translate([h[0], h[1], -1]) cylinder(d = 3.2, h = 5);
}

// ---------------------------------------------------------------- fit-check models
// Parts KiCad has no usable model for come out as boxes to their parts.json height (sockets
// are boxes too: the library model is the empty socket). The merged stl/board.stl adds the
// KiCad models of the connectors, switches, crystals and capacitors to this.
module part_boxes() color("dimgray") for (b = boxes)
    translate([b[1], b[2], board_t]) cube([b[3] - b[1], b[4] - b[2], b[5]]);

// a right-angle header on a vertical footprint: body on the pin row, legs up to the bend,
// horizontal pins toward the edge. hd = [ref, edge, along, row from edge, width, pin top, bend to tip]
module ra_header(hd) {
    e = hd[1]; row = hd[3]; w = hd[4]; top = hd[5]; len = hd[6]; pin = 0.64;
    // local frame: x across the row, y toward the edge (the edge at y = row), z up
    module placed() {
        if (e == "back")  translate([hd[2], board_d - row, board_t]) children();
        if (e == "front") translate([hd[2], row, board_t]) rotate([0, 0, 180]) children();
        if (e == "left")  translate([row, hd[2], board_t]) rotate([0, 0, 90]) children();
        if (e == "right") translate([board_w - row, hd[2], board_t]) rotate([0, 0, -90]) children();
    }
    color("black") placed() translate([-w / 2, -1.27, 0]) cube([w, 2.54, 2.54]);
    color("silver") placed() {
        translate([-w / 2, -pin / 2, 0]) cube([w, pin, top]);                 // legs (as a bar)
        translate([-w / 2, -pin / 2, top - pin]) cube([w, len + pin / 2, pin]); // horizontal pins
    }
}
module part_models() { part_boxes(); for (hd = headers) ra_header(hd); }

// the mating plugs, slop-free, from just inside the board edge to 20 mm outside the wall;
// the serial adapters' socket housings over the right-angle header pins
module plugs() color("orange", 0.7) {
    for (c = rect_cutouts)  through_wall(c[1], c[2], board_t + c[5], 20) square([c[3], c[4]], center = true);
    for (c = round_cutouts) through_wall(c[1], c[2], board_t + c[4], 20) circle(d = c[3]);
    for (n = notches) {
        hd = [for (h = headers) if (h[0] == n[0]) h][0];
        through_wall(n[1], n[2], board_t + (n[5] + n[4]) / 2, 20 + hd[3]) square([n[3], n[4] - n[5]], center = true);
    }
}

if (part == "top")       translate([0, 0, z_lid_out]) rotate([180, 0, 0]) top_half();   // lid face down for printing
if (part == "labels")    translate([0, 0, z_lid_out]) rotate([180, 0, 0]) labels_add();  // same placement as top
if (part == "bottom")    translate([0, 0, -z_floor_out]) bottom_half();
if (part == "inserts")   inserts(pressor_d, shaft_d, pipe_in, z_lid_out - board_t - 0.4 - 1.1, len(buttons), len(leds));
if (part == "board")     { board_model(); part_models(); }              // merged with the KiCad models into stl/board.stl
if (part == "plugs")     plugs();
if (part == "fit")       { top_half(); bottom_half(); }                  // both halves in place; stl/fit.stl adds board and plugs
if (part == "assembly") { bottom_half(); color("gray", 0.6) top_half(); plugs();
                          if (!board_stl) { board_model(); part_models(); } else import("stl/board.stl"); }
