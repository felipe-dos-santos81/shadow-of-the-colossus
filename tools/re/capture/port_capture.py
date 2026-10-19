#!/usr/bin/env python3
"""macOS capture driver for the user's SOTC port rig (dev-only; nothing here enters the product).

    python3 tools/re/capture/port_capture.py <schedule-file> --out out/re/capture

The port repo (default ~/code/mine/ShadowOfTheColossus-PC) is a read-only input: this driver
never writes into it, and never into its user dir.  All state lives under --out:

    <out>/user/                 SOTC_USER_DIR for the runs (screenshots/ land here)
    <out>/save-template/        the known starting save state, restored before every run
    <out>/run/                  the pad file + the per-shot dump trigger
    <out>/logs/                 the port's stdout per run + the driver's own event log
    <out>/manifest.json         one entry per collected shot: {time, action, file, sha256}

Schedule file: one event per line, `<time-seconds> <action> [hold_ms]` (`#` starts a comment):

    5 cross                 press cross for 250 ms (the default hold), 5 s into the run
    10 cross,down           hold both tokens for 250 ms
    60 lx=128 3000          hold the left stick right for 3 s (stick input walks/runs)
    50 shot                 dump the next presented frame as a PNG and collect it
    40 log:title menu       a marker line in the driver log

`action` is either `shot`, `log:<text>`, or BB_PAD_FILE tokens joined by `,` (cross, circle,
square, triangle, l1..r3, options, touchpad, touchpad_left/right, up/down/left/right, and
lx=/ly=/rx=/ry= with 0..255).  Times count from the port's "Starting main loop" log line
(C1 §1.3: the boot length varies wildly; `--from-start` switches to process start), or from a
`--from-marker` line — the measured route anchors its shots at gameplay start with
`--from-marker "st_gameflow() starting game"`, since the world load between the main loop and
the game flow varies by tens of seconds between runs.

The port hooks (C1 §1.3, measured):
  * BB_PAD_FILE: whitespace tokens, re-read on mtime change; an empty file releases everything.
  * BB_FRAME_DUMP_TRIGGER: the port *removes* the file and saves the next presented frame as a
    PNG in <user>/screenshots/ — so the driver re-creates the trigger for every shot and waits
    for the file to disappear before collecting the PNG.
  * SOTC_SHOT_OVERLAY: unset here — the game-only screenshot is the reference frame.

Saves: before every run the driver restores <out>/save-template/CUSA08809 into
<out>/user/savedata/1/CUSA08809 (the port's own sotc_play.py discipline), so a run never
depends on the saves its predecessor wrote.  The template is seeded once, read-only, from the
port's out/save-template (its own convention) or the port's user/savedata; the port's user dir
is never written.  --keep-save skips the restore, --fresh starts with no savedata at all.

Speed note (C1 §1.3): a cold pipeline cache makes the first boot grind (~80 min observed);
the driver seeds <out>/user/cache from the port's user dir once (--no-seed-cache opts out).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
DEFAULT_PORT = Path.home() / 'code' / 'mine' / 'ShadowOfTheColossus-PC'
MAIN_LOOP_MARKER = b'Starting main loop'
PNG_MAGIC = b'\x89PNG\r\n\x1a\n'
PNG_IEND = b'IEND\xaeB`\x82'
PAD_TOKENS = {
    'cross', 'circle', 'square', 'triangle', 'l1', 'r1', 'l2', 'r2', 'l3', 'r3',
    'options', 'touchpad', 'touchpad_left', 'touchpad_right',
    'up', 'down', 'left', 'right',
}
PAD_TOKEN_RE = re.compile(r'^(lx|ly|rx|ry)=(\d{1,3})$')


class CaptureError(RuntimeError):
    pass


def log(message, logfile=None):
    line = f'[{time.strftime("%H:%M:%S")}] {message}'
    print(line, flush=True)
    if logfile is not None:
        logfile.parent.mkdir(parents=True, exist_ok=True)
        with open(logfile, 'a') as f:
            f.write(line + '\n')


def parse_schedule(path):
    """[(time_seconds, action, hold_seconds)] — validated, sorted; the file is the route record."""
    events = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) not in (2, 3):
            raise CaptureError(f'{path}:{number}: expected `<time> <action> [hold_ms]`, got `{line}`')
        try:
            at = float(parts[0])
            hold = float(parts[2]) / 1000 if len(parts) == 3 else 0.25
        except ValueError as error:
            raise CaptureError(f'{path}:{number}: bad number in `{line}` ({error})')
        if at < 0 or hold < 0:
            raise CaptureError(f'{path}:{number}: negative time/hold in `{line}`')
        events.append((at, parts[1], hold))
    if not events:
        raise CaptureError(f'{path}: no events')
    events.sort(key=lambda event: event[0])  # the file may group a phase's lines; times rule
    return events


def validate_action(action, where):
    if action == 'shot' or action.startswith('log:'):
        return
    for token in action.split(','):
        if token in PAD_TOKENS:
            continue
        match = PAD_TOKEN_RE.match(token)
        if match and 0 <= int(match.group(2)) <= 255:
            continue
        raise CaptureError(f'{where}: unknown pad token `{token}` (see BB_PAD_FILE names)')


def restore_save_template(out, port, user_dir, keep_save, fresh, logfile):
    """The port's sotc_play.py discipline: a known save state before every run (never its own)."""
    target = user_dir / 'savedata' / '1' / 'CUSA08809'
    if fresh:
        shutil.rmtree(user_dir / 'savedata', ignore_errors=True)
        log(f'saves: --fresh, no savedata for this run ({user_dir / "savedata"} removed)', logfile)
        return
    template = out / 'save-template' / 'CUSA08809'
    if not template.is_dir():
        source = port / 'out' / 'save-template' / 'CUSA08809'
        if not source.is_dir():
            source = port / 'user' / 'savedata' / '1' / 'CUSA08809'
        if source.is_dir():
            template.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, template)
            log(f'saves: seeded the save-template from {source} (read-only copy)', logfile)
        else:
            log(f'saves: no save-template source under {port}; the run starts without saves', logfile)
            return
    if keep_save:
        log(f'saves: --keep-save, left {target} as it is', logfile)
        return
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(template, target)
    log(f'saves: restored {template} -> {target}', logfile)


def seed_cache(out, port, user_dir, enabled, logfile):
    """A warm pipeline cache is the difference between a ~1 s and a ~11 min boot (C1 §1.3)."""
    if not enabled:
        return
    ours = user_dir / 'cache' / 'CUSA08809'
    theirs = port / 'user' / 'cache' / 'CUSA08809'
    if ours.is_dir() or not theirs.is_dir():
        return
    ours.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(theirs, ours)
    log(f'cache: seeded {ours} from {theirs} ({sum(1 for _ in ours.iterdir())} entries)', logfile)


def macos_env(port, env):
    """The env the probe needs on macOS (C1 §1.3).  sotc.py sets these for the probe it launches
    as well (it prepends its own out/ to DYLD_LIBRARY_PATH, so that entry ends up duplicated —
    harmless, and keeping it here makes the driver self-contained)."""
    out = port / 'out'
    env.setdefault('DYLD_LIBRARY_PATH', str(out))
    env.setdefault('VK_DRIVER_FILES', str(out / 'kosmickrisp_mesa_icd.json'))
    env.setdefault('MVK_CONFIG_LOG_LEVEL', '2')
    return env


class Driver:
    def __init__(self, args, events):
        self.args = args
        self.events = events
        self.out = args.out
        self.port = args.port
        self.user_dir = args.user_dir
        self.tag = args.tag
        self.run_dir = self.out / 'run'
        self.pad = self.run_dir / f'{self.tag}-pad.txt'
        self.trigger = self.run_dir / f'{self.tag}-dump.trigger'
        self.screenshots = self.user_dir / 'screenshots'
        self.manifest_path = self.out / 'manifest.json'
        stamp = time.strftime('%Y%m%d-%H%M%S')
        self.port_log = self.out / 'logs' / f'{self.tag}-{stamp}.log'
        self.event_log = self.out / 'logs' / f'{self.tag}-{stamp}.driver.log'
        self.process = None
        self.failures = []
        self.shots = []
        self.collected = set()  # port-side PNG names already copied (never collect twice)

    def log(self, message):
        log(message, self.event_log)

    def manifest_append(self, entry):
        entries = []
        if self.manifest_path.is_file():
            entries = json.loads(self.manifest_path.read_text())
        entries.append(entry)
        self.manifest_path.write_text(json.dumps(entries, indent=1) + '\n')

    def start(self):
        env = dict(os.environ)
        env['SOTC_USER_DIR'] = str(self.user_dir)
        env['BB_PAD_FILE'] = str(self.pad)
        env['BB_FRAME_DUMP_TRIGGER'] = str(self.trigger)
        env['SOTC_FPS'] = '1'                  # frame-presented markers in the port log
        env.setdefault('BB_HDR', '0')          # the SDR baseline C1 booted (its §1.4.5)
        env.pop('SOTC_SHOT_OVERLAY', None)     # game-only stills
        macos_env(self.port, env)
        for pair in self.args.env:
            name, value = pair.split('=', 1)
            env[name] = value
        self.out.joinpath('logs').mkdir(parents=True, exist_ok=True)
        self.run_dir.mkdir(parents=True, exist_ok=True)
        self.pad.write_text('')
        self.trigger.unlink(missing_ok=True)
        self.log(f'launch: python3 sotc.py (cwd={self.port}) SOTC_USER_DIR={self.user_dir}')
        self.log(f'log: {self.port_log}')
        self.port_log_file = open(self.port_log, 'wb')
        self.process = subprocess.Popen(
            [sys.executable, 'sotc.py'], cwd=self.port, env=env,
            stdout=self.port_log_file, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
            start_new_session=True)  # SIGTERM must reach bb-probe, sotc.py's child
        self.started = time.monotonic()

    def wait_log_marker(self, marker, what):
        """The schedule's time base: the moment `marker` (bytes) appears in the port log."""
        deadline = self.started + self.args.boot_timeout
        while True:
            if marker in self.port_log.read_bytes():
                return time.monotonic()
            if self.process.poll() is not None:
                raise CaptureError(f'port exited rc={self.process.returncode} before {what} '
                                   f'(log: {self.port_log})')
            if time.monotonic() > deadline:
                raise CaptureError(f'{what} not seen within {self.args.boot_timeout:.0f}s '
                                   f'(log: {self.port_log})')
            time.sleep(0.2)

    def write_pad(self, tokens):
        self.pad.write_text(' '.join(tokens) + '\n')

    def take_shot(self, at):
        # Snapshot the screenshots before the trigger: the port saves the PNG right after it
        # removes the trigger file, so a snapshot taken later could swallow the new PNG.
        before = {p.name for p in self.screenshots.glob('*.png')} if self.screenshots.is_dir() else set()
        self.trigger.touch()
        deadline = time.monotonic() + self.args.shot_timeout
        while self.trigger.exists():
            if time.monotonic() > deadline:
                self.failures.append(f'shot at {at}s: trigger not consumed in '
                                     f'{self.args.shot_timeout:.0f}s')
                self.log(f'{at:6.1f}s shot: TRIGGER NOT CONSUMED (game not presenting?)')
                return
            time.sleep(0.02)
        # The port removes the trigger, then saves the next presented frame: wait for the PNG.
        # The file appears before it is complete (a killed run caught 0-byte copies), so a shot
        # is only collected once the bytes carry the whole PNG (magic + IEND).
        png = None
        deadline = time.monotonic() + self.args.shot_timeout
        while png is None:
            if self.screenshots.is_dir():
                new = [p for p in self.screenshots.glob('*.png')
                       if p.name not in before and p.name not in self.collected]
                for candidate in sorted(new, key=lambda p: p.stat().st_mtime, reverse=True):
                    data = candidate.read_bytes()
                    if data.startswith(PNG_MAGIC) and data.endswith(PNG_IEND):
                        png, png_data = candidate, data
                        break
            if png is not None:
                break
            if time.monotonic() > deadline:
                self.failures.append(f'shot at {at}s: no complete PNG appeared in {self.screenshots}')
                self.log(f'{at:6.1f}s shot: NO COMPLETE PNG in {self.screenshots}')
                return
            time.sleep(0.05)
        target = self.out / f'{self.tag}-{png.name}'
        target.write_bytes(png_data)
        digest = hashlib.sha256(png_data).hexdigest()
        self.collected.add(png.name)
        self.manifest_append({'time': at, 'action': 'shot', 'file': target.name, 'sha256': digest})
        self.shots.append(target)
        self.log(f'{at:6.1f}s shot: {target.name} sha256 {digest[:16]}…')

    def schedule(self, t0):
        for at, action, hold in self.events:
            while time.monotonic() < t0 + at:
                if self.process.poll() is not None:
                    self.failures.append(f'port exited rc={self.process.returncode} before {at}s')
                    return
                time.sleep(0.02)
            if action == 'shot':
                self.take_shot(at)
            elif action.startswith('log:'):
                self.log(f'{at:6.1f}s log: {action[4:]}')
            else:
                self.write_pad(action.split(','))
                time.sleep(hold)
                self.write_pad([])
                self.log(f'{at:6.1f}s pad: {action} ({hold * 1000:.0f} ms)')

    def stop(self):
        if self.process is None or self.process.poll() is not None:
            return self.process.returncode if self.process else None
        try:
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
        except ProcessLookupError:
            return self.process.wait()  # the group ended between poll() and the signal
        try:
            return self.process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.log('port ignored SIGTERM; SIGKILL')
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
            except ProcessLookupError:
                pass
            return self.process.wait()

    def run(self):
        restore_save_template(
            self.out, self.port, self.user_dir, self.args.keep_save, self.args.fresh, self.event_log)
        seed_cache(self.out, self.port, self.user_dir, self.args.seed_cache, self.event_log)
        self.start()
        if self.args.from_start:
            t0, anchor = self.started, 'process start'
        elif self.args.from_marker:
            t0 = self.wait_log_marker(self.args.from_marker.encode(), f'marker {self.args.from_marker!r}')
            anchor = f'marker {self.args.from_marker!r}'
        else:
            t0 = self.wait_log_marker(MAIN_LOOP_MARKER, 'the main loop')
            anchor = 'main loop'
        self.log(f'schedule start: {anchor} (boot {t0 - self.started:.1f}s)')
        thread = threading.Thread(target=self.schedule, args=(t0,), daemon=True)
        thread.start()
        thread.join(timeout=max(at for at, _, _ in self.events) + self.args.shot_timeout * 2 + 60)
        time.sleep(self.args.grace)
        rc = self.stop()
        self.port_log_file.close()
        self.log(f'port rc={rc}; shots {len(self.shots)}; failures {len(self.failures)}')
        for failure in self.failures:
            self.log(f'FAILURE: {failure}')
        return 0 if not self.failures else 1


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('schedule', type=Path, help='the route file: `<time> <action> [hold_ms]`')
    parser.add_argument('--out', type=Path, default=REPO / 'out' / 're' / 'capture',
                        help='artifacts dir (default: out/re/capture)')
    parser.add_argument('--port', type=Path, default=DEFAULT_PORT, help='the port repo (read-only)')
    parser.add_argument('--user-dir', type=Path, default=None,
                        help='SOTC_USER_DIR for the runs (default: <out>/user)')
    parser.add_argument('--tag', default=None, help='names the log/copies (default: schedule stem)')
    parser.add_argument('--from-start', action='store_true',
                        help='schedule times count from process start, not the main loop')
    parser.add_argument('--from-marker', default=None, metavar='TEXT',
                        help='schedule times count from the log line TEXT (e.g. '
                             '"st_gameflow() starting game": gameplay start); wins over the '
                             'main-loop anchor')
    parser.add_argument('--boot-timeout', type=float, default=1800,
                        help='seconds to wait for "Starting main loop" (default: 1800)')
    parser.add_argument('--shot-timeout', type=float, default=120,
                        help='seconds to wait for a shot to be consumed/written (default: 120)')
    parser.add_argument('--grace', type=float, default=20, help='seconds to keep running after the '
                        'last event before the port is stopped (default: 20)')
    parser.add_argument('--keep-save', action='store_true', help='skip the save-template restore')
    parser.add_argument('--fresh', action='store_true', help='start with no savedata at all')
    parser.add_argument('--no-seed-cache', action='store_false', dest='seed_cache',
                        help="don't seed <out>/user/cache from the port's user dir")
    parser.add_argument('--env', action='append', default=[], metavar='NAME=VALUE',
                        help='extra environment for the port (repeatable)')
    parser.add_argument('--dry-run', action='store_true', help='parse and print the plan, no run')
    args = parser.parse_args()

    args.schedule = args.schedule.resolve()
    args.out = args.out.resolve()
    args.port = args.port.resolve()
    args.user_dir = (args.user_dir or (args.out / 'user')).resolve()
    args.tag = args.tag or args.schedule.stem

    try:
        events = parse_schedule(args.schedule)
        for at, action, _ in events:
            validate_action(action, f'{args.schedule}:{at}s')
    except CaptureError as error:
        print(f'port_capture: {error}', file=sys.stderr)
        return 2
    if not args.port.is_dir():
        print(f'port_capture: no port repo at {args.port}', file=sys.stderr)
        return 2

    if args.dry_run:
        print(f'schedule {args.schedule} -> {len(events)} events, tag {args.tag}')
        for at, action, hold in events:
            print(f'  {at:7.1f}s  {action}' + (f'  ({hold * 1000:.0f} ms)' if action not in
                                               ('shot',) and not action.startswith('log:') else ''))
        print(f'out {args.out}\nuser dir {args.user_dir}\nport {args.port}')
        return 0

    driver = Driver(args, events)
    try:
        return driver.run()
    except CaptureError as error:
        driver.log(f'ABORT: {error}')
        driver.stop()
        return 1
    except KeyboardInterrupt:
        driver.log('interrupted')
        driver.stop()
        return 130


if __name__ == '__main__':
    sys.exit(main())
