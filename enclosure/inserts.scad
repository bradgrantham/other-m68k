// Separately printed inserts for the Griffin Jr. case: button caps (silver) and light pipes
// (clear or white). case.scad calls inserts() with its own dimensions; this file can also be
// rendered alone for a quick look.
//
//   openscad -D 'part="inserts"' --export-format binstl -o stl/inserts.stl case.scad

// cap: a disc the size of the pressor column with a shaft that glues into the pressor's hole
module button_cap(pressor_d, shaft_d, cap_t = 1.0, shaft_len = 6) {
    cylinder(d = pressor_d - 0.2, h = cap_t, $fn = 48);
    translate([0, 0, cap_t - 0.01]) cylinder(d = shaft_d - 0.3, h = shaft_len, $fn = 32);
}

// light pipe: a rod for the lid's bore, flush with the lid top; trim at the bottom to taste
module light_pipe(pipe_in, length) cylinder(d = pipe_in - 0.2, h = length, $fn = 48);

module inserts(pressor_d, shaft_d, pipe_in, pipe_len, n_buttons, n_leds) {
    for (i = [0 : n_buttons - 1]) translate([i * (pressor_d + 4), 0, 0]) button_cap(pressor_d, shaft_d);
    for (i = [0 : n_leds - 1])    translate([i * (pipe_in + 4), pressor_d + 6, 0]) light_pipe(pipe_in, pipe_len);
}

inserts(6, 3.0, 4.25, 15, 2, 3);
