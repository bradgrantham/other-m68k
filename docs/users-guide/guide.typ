// The Griffin Jr. user's guide: page design.  The chapters are Markdown (NN-name.md),
// turned into build/ch/NN-name.typ by pandoc with filter.lua; the Makefile appends them to
// this file to make build/guide.typ.
//
// The look is a home-computer manual of the early 1980s: a half-letter page that prints
// two-up as a booklet, black plus one second colour, a schoolbook serif for the text and
// a plain sans for headings.

#let spot = rgb("#c2410c")              // the second colour (figures/board.py uses it too)
#let paper = rgb("#fbf7ef")
#let screen-blue = rgb("#000055")       // the console's background
#let serif = ("C059", "Libertinus Serif")
#let sans = ("Nimbus Sans", "DejaVu Sans")
#let mono = ("DejaVu Sans Mono",)

#set document(title: "Griffin Jr. User's Guide")
#set text(font: serif, size: 9.5pt, lang: "en")
#set par(justify: false, leading: 0.62em, spacing: 0.95em)

// ---- boxes and keys (filter.lua turns ::: curious, ::: try, ::: careful and [X]{.key}
// into these) ----------------------------------------------------------------------------

#let labelled-box(label, tint, body) = block(
  width: 100%, breakable: false, above: 1.1em, below: 1.1em,
  inset: (left: 10pt, right: 8pt, top: 7pt, bottom: 8pt),
  stroke: (left: 3pt + spot), fill: tint,
  {
    text(font: sans, size: 7pt, weight: "bold", fill: spot, tracking: 0.08em, upper(label))
    v(-0.45em)
    set text(size: 8.8pt)
    body
  },
)
#let curious(body) = labelled-box("For the curious", paper, body)
#let try(body) = labelled-box("Try this", spot.lighten(88%), body)
#let careful(body) = labelled-box("Careful", luma(235), body)

#let key(body) = box(
  inset: (x: 3pt, y: 0pt), outset: (y: 2.5pt), radius: 2pt, stroke: 0.5pt + luma(90),
  text(font: sans, size: 7.5pt, weight: "bold", body),
)

// ---- listings -----------------------------------------------------------------------------

#show raw: set text(font: mono, size: 8pt)
#show raw.where(block: false): it => text(fill: rgb("#7c2d12"), it)

// What the screen shows: white on the console's blue, 53 columns wide.
#show raw.where(block: true, lang: "screen"): it => block(
  fill: screen-blue, inset: (x: 7pt, y: 7pt), radius: 3pt, breakable: false,
  above: 1em, below: 1em,
  text(fill: white, size: 7.6pt, it),
)
// Anything else in a block is something to type, or a file's contents.
#show raw.where(block: true): it => {
  if it.lang == "screen" { it } else {
    block(width: 100%, inset: (left: 9pt, y: 5pt), stroke: (left: 1.5pt + luma(150)), it)
  }
}

// ---- tables and figures ---------------------------------------------------------------------

#set table(stroke: none, inset: (x: 5pt, y: 3.5pt), align: left + top)
#show table: set text(size: 8.5pt)
#show table: set par(justify: false, leading: 0.5em)
#let th(body) = text(font: sans, size: 7.5pt, weight: "bold", fill: spot, body)   // a column heading
#show figure.where(kind: table): set block(breakable: true)
#show figure.where(kind: table): set align(left)
#set figure(gap: 0.7em)
#show figure.caption: set text(font: sans, size: 7.5pt)
#set figure.caption(separator: [. ])
#show figure.where(kind: image): set figure(supplement: [Figure])
#let horizontalrule = line(length: 100%, stroke: 0.5pt + luma(150))
#show link: set text(fill: spot)
#set list(marker: box(width: 4pt, height: 4pt, fill: spot, baseline: -1.5pt), indent: 0.4em)
#set enum(indent: 0.4em)
#set terms(hanging-indent: 1.2em)

// ---- headings ---------------------------------------------------------------------------------

#set heading(numbering: (..n) => if n.pos().len() == 1 { numbering("1", ..n) })
#show heading: set text(font: sans)
#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  v(0.3in)
  if it.numbering != none {
    grid(columns: (auto, 1fr), column-gutter: 8pt, align: bottom,
      text(size: 44pt, weight: "bold", fill: spot, counter(heading).display("1")),
      pad(bottom: 5pt, text(size: 8pt, weight: "bold", tracking: 0.15em, fill: spot, "CHAPTER")))
    v(0.1em)
  }
  text(size: 21pt, weight: "bold", it.body)
  v(0.2em)
  line(length: 100%, stroke: 1.5pt + spot)
  v(0.6em)
}
#show heading.where(level: 2): it => block(above: 1.6em, below: 0.8em,
  text(size: 12.5pt, weight: "bold", it.body))
#show heading.where(level: 3): it => block(above: 1.4em, below: 0.85em,
  text(size: 9.5pt, weight: "bold", fill: spot, it.body))

// ---- title page ---------------------------------------------------------------------------------

#set page(width: 5.5in, height: 8.5in, margin: (x: 0.6in, top: 0.7in, bottom: 0.75in))

#page(fill: spot, margin: 0.6in)[
  #set text(font: sans, fill: white)
  #v(1.3in)
  #text(size: 46pt, weight: "bold")[Griffin Jr.]
  #v(-1.6em)
  #line(length: 100%, stroke: 2pt + white)
  #v(-0.3em)
  #text(size: 17pt)[User's Guide]
  #v(1fr)
  #block(fill: screen-blue, inset: 12pt, radius: 4pt, width: 100%,
    text(font: mono, size: 9pt)[C:/> hello #box(fill: white, width: 0.6em, height: 0.95em, baseline: 0.15em)])
  #v(0.5in)
  #text(size: 9pt)[A 68000 computer with colour pictures, sound, a joystick port and nothing hidden.]
]

#page[
  #v(1fr)
  #set text(size: 8pt)
  This guide describes the Griffin Jr. board revision C3 and the firmware and programs
  built from the same source tree. Every command, listing and screen picture in it was
  produced by running the machine's own software.

  The Griffin Jr. was designed in October 2026.
]

// ---- contents, then the chapters ----------------------------------------------------------------

#set page(
  numbering: "1",
  header: context {
    let here-page = here().page()
    let starts = query(heading.where(level: 1)).filter(h => h.location().page() == here-page)
    if starts.len() == 0 {
      let before = query(heading.where(level: 1).before(here()))
      if before.len() > 0 {
        set text(font: sans, size: 7.5pt, fill: luma(90))
        grid(columns: (1fr, auto), [Griffin Jr. User's Guide], before.last().body)
        v(-0.6em)
        line(length: 100%, stroke: 0.4pt + luma(150))
      }
    }
  },
  footer: context align(center, text(font: sans, size: 8pt, counter(page).display("1"))),
)
#counter(page).update(1)

#{
  show heading: none
  heading(numbering: none, outlined: false)[Contents]
}
#v(0.3in)
#text(font: sans, size: 21pt, weight: "bold")[Contents]
#v(0.2em)
#line(length: 100%, stroke: 1.5pt + spot)
#v(0.6em)
#{
  show outline.entry.where(level: 1): it => {
    v(0.5em)
    text(font: sans, weight: "bold", it)
  }
  outline(title: none, depth: 2, indent: 1.4em)
}

// The chapters follow (the Makefile appends them).
