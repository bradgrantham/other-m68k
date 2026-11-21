# Updating the BIOS

The BIOS lives in flash memory, so it can be replaced with a newer version without
opening the machine. The Griffin Jr. does the job itself, from a file on the SD card.

Drive C: is kept in the same chips but in a separate part of them. Updating the BIOS does
not touch it, and your files there are safe.

## Before you start

You need the new BIOS as a file, usually named `ROM.BIN`, which is exactly 256 KB long.
Copy it to the SD card on another computer.

Type `info` and note the version on the first line, so that you can tell afterwards that
the update took.

Use a power supply you trust. The one thing that can spoil an update is losing power
part of the way through.

## The update

1. Type `flashid`. The answer should end with `SST39SF040 x2`, which is the kind of chip
   the update knows how to write. If it does not, stop here.
2. Type `flash D:ROM.BIN`. The Griffin Jr. reads the file and checks it. A file that is
   the wrong length or has been damaged is refused at this point, and nothing is changed.
3. The Griffin Jr. asks whether to go ahead. Type `y`.
4. Wait. The screen does not change for several seconds, because the program that draws
   the screen is the thing being replaced. Do not press NMI or RESET, and do not unplug
   the power.
5. The machine starts again by itself with the new BIOS. Type `info` to see the new
   version.

If you have a serial cable on port A, you can watch the update there. The Griffin Jr.
sends a dot for each of the 32 blocks as it finishes them.

## If it goes wrong

If the power fails during those few seconds, the BIOS is left half written and the machine
will not start. No harm has come to the hardware, but the Griffin Jr. can no longer mend
itself. The two ROM chips sit in sockets so that they can be lifted out and rewritten in
a chip programmer. The files for that, one for the socket marked EVEN and one for ODD,
are built along with `ROM.BIN`.

## Starting drive C: afresh

`format C:` erases everything on drive C: and leaves it empty. It asks first. Afterwards,
copy back what you want from the SD card:

```
copy D:*.ELF C:
copy D:AUTOEXEC.CMD C:
```

Drive C: holds 768 KB, of which a few KB go on housekeeping. The programs that come with
the Griffin Jr. take up a little under half of the rest.
