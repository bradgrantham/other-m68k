# Sound

The Griffin Jr. has no sound chip. It has one wire that can be switched on and off very
quickly, a few small parts to smooth the result, and a processor with time to spare.
Speech, music and three-part harmony all come out of that one wire. This chapter explains
how.

## From numbers to sound

A loudspeaker makes sound by moving in and out. To play a recording, a computer sends the
speaker a long list of numbers, each one saying how far out the speaker should be at that
instant. Each number is called a **sample**, and if the samples arrive fast enough, the ear
hears a smooth sound.

The Griffin Jr. uses samples from 0 to 255, where 128 is the resting position, and it
sends out 15,707 of them every second. That rate is high enough for clear speech and
pleasant music. It is about the quality of an AM radio.

## One wire and eight bits

A sample can have 256 values, but the wire has only two states, on and off. The Griffin
Jr. gets from one to the other with a trick as old as the light dimmer: it switches the
wire so quickly that only the average matters.

For each sample, CPLD 2 sends out a pattern of eight on-or-off bits, one after another.
If none of the eight is on, the average is the lowest level. If all eight are on, it is
the highest. Four on and four off gives the middle.

![Five patterns, each shown twice over, and the level each one gives.](figures/pdm.svg){width=100%}

The patterns go by at more than 125,000 bits a second, far too fast to hear. A resistor
and a capacitor beside the audio socket smooth the rapid switching into a level, in the
way a heavy flywheel smooths the kicks of an engine, and what reaches your speaker is the
average.

Eight bits give only nine levels, from none on to all eight on, which would be a coarse
sort of sound. The software gets round that by spreading the error. If a sample calls for
a level between two patterns, it uses the lower pattern for some samples and the higher
one for others, in the right proportion, and the smoothing blends them. This is the same
idea as the dithering that `bmpview` uses to show a photograph in 256 colours.

## Why 15,707?

The number is the count of lines the television draws each second. At the start of every
line CPLD 2 sends the processor a signal, and the BIOS answers by handing over the pattern
for the next sample. So the picture is the metronome for the sound, and no separate timer
is needed.

It also means sound costs processor time. Answering that signal 15,707 times a second
takes about a third of everything the 68000 can do, for as long as sound is playing. When
nothing is playing, the BIOS switches the signal off and the cost is nothing.

## Making sound from a program

A program has three ways to make sound, in order of effort.

**The monitor's `beep`** is the simplest, and BASIC has a `BEEP` of its own.

**BASIC's `SOUND` and `PLAY`**, from chapter 6, give you three voices. BASIC works out
the samples for all three, adds them together and passes the total to the BIOS.

**The BIOS sound stream** is what programs written in C use, and it is how `tune`, `sdplay`
and `sstv` work. The program fills a small buffer with patterns and hands it to the BIOS,
which plays it and gives back an empty one. As long as the program keeps the buffers
coming, the sound never breaks. A buffer holds up to 512 samples, which is about a
thirtieth of a second.

The file that `sdplay` plays without `-pcm` holds patterns instead of samples. The work
of turning samples into patterns has then been done ahead of time on another computer,
which leaves the Griffin Jr. with more time for other things while it plays.

::: curious
**Reading the abench numbers**

Chapter 4 showed the program `abench`, which measures the cost of sound. Here are its
results again.

```{.screen session=abench}
```

Every line of the picture lasts 764 ticks of the processor's clock. The figure at the end
of each row is how many of those ticks went on sound, and the percentage is what was left
for the program.

The BIOS stream is the cheapest at about 240 ticks, which leaves the program 68 per cent
of the machine. The rows marked "callback" are for programs that ask to be called on every
line so that they can work out each sample at the last moment. That gives the most
control and costs the most.

The last two rows measure the conversion from samples to patterns. It needs a table that
takes four tenths of a second to build when a program starts, which is the short pause
before `tune` and `sdplay` begin.
:::

## Getting the best sound

The audio socket gives a signal at the level that amplified speakers, headphones and the
audio input of a television expect. The same signal goes to both ears.

Sound stutters briefly whenever a file is written to drive C:. Writing to the flash chips
stops everything else for about a tenth of a second at a time, and the sound holds its
last value until the machine comes back. Writing to the SD card does not do this.
