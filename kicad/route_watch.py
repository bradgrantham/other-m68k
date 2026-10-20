#!/usr/bin/env python3
"""route_watch.py - run a long command (freerouting) under a progress watchdog.

    route_watch.py --log FILE [--interval SEC] [--pattern REGEX] -- command args...

The command's stdout/stderr go to FILE.  Every --interval seconds (default 900 = 15 min)
the watchdog compares two things with the previous check: the number of log lines that
match --pattern (default "pass #", freerouting's per-pass summary) and the child's CPU
time.  If neither moved, the child is stalled - freerouting 2.5.0's native build, for one,
dies with an exception in its routing thread and then sleeps forever - so it is killed,
the tail of the log is printed and the exit status is 3.  Otherwise a one-line status
(elapsed, passes, last summary line) is printed at every check, and the command's own
exit status is returned when it finishes.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time


def cpu_seconds(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            fields = f.read().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
    except (OSError, IndexError, ValueError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", required=True)
    ap.add_argument("--interval", type=float, default=900.0, help="seconds between checks (default 900)")
    ap.add_argument("--pattern", default=r"pass #", help="regex of a log line that proves progress")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == "--" else args.cmd
    if not cmd:
        ap.error("no command given")
    pat = re.compile(args.pattern)
    t0 = time.time()
    with open(args.log, "w") as log:
        child = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    print(f"route_watch: pid {child.pid}, checking every {args.interval:g} s, log {args.log}", flush=True)
    last = (0, 0.0)
    next_check = t0 + args.interval
    while True:
        rc = child.poll()
        if rc is not None:
            print(f"route_watch: finished after {(time.time() - t0) / 60:.1f} min, exit {rc}", flush=True)
            sys.exit(rc)
        if time.time() < next_check:
            time.sleep(min(5.0, max(0.1, next_check - time.time())))
            continue
        next_check += args.interval
        with open(args.log, errors="replace") as f:
            lines = [ln.rstrip() for ln in f if pat.search(ln)]
        cpu = cpu_seconds(child.pid) or 0.0
        now = (len(lines), cpu)
        elapsed = (time.time() - t0) / 60
        tail = lines[-1][-150:] if lines else "(no progress line yet)"
        if now[0] == last[0] and now[1] - last[1] < 1.0:
            print(f"route_watch: STALLED after {elapsed:.1f} min: {now[0]} progress lines, {cpu:.0f} CPU s, "
                  f"no change in {args.interval:g} s; killing pid {child.pid}", flush=True)
            try:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(10)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
            except ProcessLookupError:
                pass
            with open(args.log, errors="replace") as f:
                text = f.read().splitlines()
            print("--- last 15 lines not from a stack trace:")
            for ln in [l for l in text if not l.startswith("\tat ")][-15:]:
                print("  " + ln[:200])
            sys.exit(3)
        print(f"route_watch: {elapsed:.1f} min, {now[0]} progress lines, {cpu:.0f} CPU s: {tail}", flush=True)
        last = now


if __name__ == "__main__":
    main()
