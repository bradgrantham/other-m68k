#!/usr/bin/env python3
"""Capture the screens the user's guide shows by running them in the emulator.

sessions.json maps a name to what is typed on the keyboard.  For each session this
writes build/screens/NAME.txt (the 53x24 console text, trimmed) and, when the session
asks for it, build/shots/NAME.png (the 320x240 picture, doubled).  The chapters refer
to the sessions by name, so a firmware or program change shows up in the guide at the
next build instead of leaving a stale listing behind.

Session fields:
  keys     text typed on the PS/2 keyboard (\\n = Enter); default none
  frames   how long to run, in video frames (60 per second); default 900
  delay    frames to wait before typing; default 350 (the power-on test takes ~250)
  sd       true: fit the guide's SD card (the files in card/) as drive D:
  start    drop the screen lines before the first line that starts with this text
  shot     true: also save the picture
  page     which framebuffer page the picture is read from (0 or 1); default 0
  joy, paddle   passed to the emulator's --joy / --paddle
"""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, "..", ".."))
EMU = os.path.join(TOP, "emu", "build", "m68k-emu")
ROM = os.path.join(TOP, "fw", "build", "flash.img")
BUILD = os.path.join(HERE, "build")
PAGES = {0: 0x0ED400, 1: 0x0DA800}          # spec/board.json video.pages
W, H = 320, 240


def make_card(path):
    """A small FAT32 card holding the files in card/ (upper-cased names)."""
    env = dict(os.environ, PATH=os.environ["PATH"] + ":/sbin:/usr/sbin", MTOOLS_SKIP_CHECK="1")
    if os.path.exists(path):
        os.remove(path)
    with open(path, "wb") as f:
        f.truncate(64 << 20)
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "GRIFFIN", path], check=True, env=env,
                   stdout=subprocess.DEVNULL)
    card = os.path.join(HERE, "card")
    for name in sorted(os.listdir(card)):
        subprocess.run(["mcopy", "-i", path, os.path.join(card, name), "::/" + name.upper()],
                       check=True, env=env)


def save_png(data, path):
    from PIL import Image
    im = Image.new("RGB", (W, H))
    im.putdata([((b >> 5) * 255 // 7, ((b >> 2) & 7) * 255 // 7, (b & 3) * 255 // 3)
                for b in data])
    im.resize((W * 2, H * 2), Image.NEAREST).save(path)


def run(name, s, card):
    text = os.path.join(BUILD, "screens", name + ".txt")
    cmd = [EMU, "--rom", ROM, "--headless", "--fast", "--no-audio", "--no-stdin",
           "--frames", str(s.get("frames", 900)), "--type-delay", str(s.get("delay", 350)),
           "--text-dump", text]
    if s.get("keys"):
        cmd += ["--type", s["keys"].replace("\n", "\\n")]
    if s.get("sd"):
        mine = os.path.join(BUILD, "card-run.img")      # each session starts from a fresh card
        shutil.copyfile(card, mine)
        cmd += ["--sd", mine]
    if s.get("joy"):
        cmd += ["--joy", s["joy"]]
    if s.get("paddle"):
        cmd += ["--paddle", s["paddle"]]
    if s.get("shot"):
        cmd += ["--dump-mem", "0x%X" % PAGES[s.get("page", 0)], str(W * H)]
    r = subprocess.run(cmd, check=True, capture_output=True)
    out = (r.stdout + r.stderr).decode("latin-1")       # the memory dump goes to stderr

    lines = [l.rstrip() for l in open(text, encoding="latin-1").read().split("\n")]
    if "start" in s:
        hits = [i for i, l in enumerate(lines) if l.startswith(s["start"])]
        if not hits:
            sys.exit("screens.py: session %s: no screen line starts with %r" % (name, s["start"]))
        lines = lines[hits[0]:]
    while lines and not lines[-1]:
        lines.pop()
    while lines and not lines[0]:
        lines.pop(0)
    open(text, "w").write("\n".join(lines) + "\n")

    if s.get("shot"):
        data = bytearray()
        for l in out.split("\n"):
            if re.match(r"^[0-9A-F]{6}: ", l):
                data += bytes.fromhex(l[8:56].replace(" ", ""))
        if len(data) != W * H:
            sys.exit("screens.py: session %s: picture dump is %d bytes" % (name, len(data)))
        save_png(data, os.path.join(BUILD, "shots", name + ".png"))


def main():
    sessions = json.load(open(os.path.join(HERE, "sessions.json")))
    for d in ("screens", "shots"):
        os.makedirs(os.path.join(BUILD, d), exist_ok=True)
    for f in (EMU, ROM):
        if not os.path.exists(f):
            sys.exit("screens.py: %s is missing: run `make emu flash` at the top level" % f)
    card = os.path.join(BUILD, "card.img")
    make_card(card)
    only = sys.argv[1:]
    for name, s in sessions.items():
        if name.startswith("_") or (only and name not in only):
            continue
        run(name, s, card)
        print("screens.py:", name)


if __name__ == "__main__":
    main()
