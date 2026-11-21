# Griffin Jr. (other-m68k)

A homebrew 68000 single-board computer: hardware, glue logic, firmware, SDK, emulator,
enclosure and documentation. @README.md has the specification summary, the directory layout
and the build targets.

## How information flows

Each fact has one home. When you change something, follow it downstream and update what
depends on it in the same piece of work.

| when this changes | update |
|---|---|
| `spec/board.json` (memory map, registers, interrupts, syscalls, ABI) | `make gen` regenerates `generated/`; then the firmware, SDK, emulator and HDL that use it; then `docs/programmers-guide.md` and the user's guide chapters that quote addresses, registers or timings |
| firmware behaviour (`fw/`): boot and POST messages, the banner, LED codes, monitor commands and keys, scripts, drives, the fault screen, the clock | `fw/README.md`, `docs/programmers-guide.md` where it applies, and `docs/users-guide/` |
| the program list (`sdk/examples/`, what `make flash` puts on C:, `flashfs/`, BASIC from `../polybasic`) or what a program does or takes on its command line | `docs/users-guide/` (the chapter on the programs, and the command reference) |
| the SDK (`sdk/`), system calls, libraries | `docs/programmers-guide.md`, `sdk/README.md` |
| the board (`kicad/`): connectors, part positions, silkscreen | `kicad/ORDER.md`, the BOM flow (`parts.json`, `fab_export.py`), `enclosure/`, and the board tour, figures and pinouts in `docs/users-guide/` |
| a program or system from `docs/experience-plan.md` is added | its section or chapter in `docs/users-guide/`, and its entry in the plan |

The user's guide is the last stop for anything a person at the keyboard can see or do. A
change to the firmware or the program list is not finished until the guide matches.

## The user's guide (`docs/users-guide/`)

- **Audience:** a curious person who has not programmed a machine like this. Low-level detail
  goes in the marked "for the curious" boxes.
- **Style:** in the manner of the home-computer manuals of the early 1980s. Friendly, second
  person, "type this and see what happens".
- **Plain language.** Common words and active voice. Define a technical word the first time
  it appears, and say what a thing does before how it works. Plain does not mean choppy: a
  sentence is as long as its idea needs, related facts are joined with their reason ("so",
  "because", "which means"), and sentence length varies as it does when a person explains
  something across a table. A run of short flat statements reads like a school primer; the
  manuals this guide is modelled on were plain and still had warmth and rhythm.
- **No AI-assistant phrasing.** No em-dash asides, no "it's worth noting", "let's dive in",
  "simply", "seamlessly", "powerful", "robust", "delve", "whether you're X or Y", no
  three-item flourishes, no summary sentence that repeats the paragraph, no exclamation marks
  for enthusiasm. If a sentence could appear in any manual for any product, cut it.
- **Everything shown is real.** Each command, listing and screen in the guide is run in the
  emulator before it goes in, and again when the firmware or the program changes. Screens are
  captured, not typed from memory.
- **Source and build:** one Markdown file per chapter, built to PDF by `make -C docs/users-guide`
  (see its README for the tools). The PDF is a build product: do not check it in. It is
  meant to be built and published by GitHub as a release, as the enclosure files will be, so
  keep the build free of steps that only work on this machine.
