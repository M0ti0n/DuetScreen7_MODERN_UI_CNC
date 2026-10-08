#!/usr/bin/env python3
"""
fake_duet.py - a pretend CNC Duet for the DuetScreen simulator (no hardware needed).

It answers on the network the way a Duet in standalone mode does (rr_connect, rr_model, rr_gcode,
rr_reply and the file calls), so the simulator can be tested with every page live:

  * a 3-axis (or 4-axis with --axes 4) mill: X 0..600, Y 0..400, Z -120..0 machine coordinates
  * moves really take time (jog, G0/G1/G2/G3, G28 homing, G38.2 probing onto a fake stock top)
  * workplaces G54..G59.3 (G10 L2 / L20), tool offsets (G10 P), tools T0..T3, spindle M3/M4/M5
  * jobs: M32 / M23 + M24, pause M25, resume M24, cancel M0, progress and time left
  * macros: M98 with meta commands (if/elif/else/while, var, global, set, echo, abort)
  * machine messages: M291 S0..S7 and the panel's M292 answers
  * a virtual SD card (a normal folder) with demo jobs and macros, created on the first run

Usage (two terminals, both inside WSL, from the repo root):
  python3 scripts/fake_duet.py                      # listens on 127.0.0.1:8080
  ./out/build/CNC-Simulation-Release/DuetScreen     # then SETTINGS > CONNECTION > NETWORK: 127.0.0.1:8080

Options: --port, --bind, --axes 3|4, --homed, --speed (time factor, 2 = twice as fast),
         --sd DIR (the virtual SD card, default ~/fake_duet_sd), --reset-sd, --card1, --password, --quiet,
         --slow KB (file downloads at KB kilobytes per second, e.g. 300 for a Duet 2 WiFi)

Only the Python 3 standard library is used. It is a test tool: it is not RepRapFirmware.
"""

import argparse
import datetime
import json
import math
import queue
import re
import shutil
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

REPO = Path(__file__).resolve().parent.parent

AXES = {
    #     machine min, max, home, max speed (mm/min or deg/min)
    "X": dict(min=0.0, max=600.0, home=0.0, speed=6000.0),
    "Y": dict(min=0.0, max=400.0, home=0.0, speed=6000.0),
    "Z": dict(min=-120.0, max=0.0, home=0.0, speed=3000.0),
    "A": dict(min=-360.0, max=360.0, home=0.0, speed=7200.0),
}
RAPID = 5000.0  # G0 feed, mm/min
HOMING_FEED = 3000.0
WCS_CODES = ["G54", "G55", "G56", "G57", "G58", "G59", "G59.1", "G59.2", "G59.3"]
# Where the fake probe touches (machine coordinates): the stock top, and one edge in X and in Y
PROBE_SURFACES = {"Z": -70.0, "X": 150.0, "Y": 100.0}
TOOLS = [  # name, offsets X Y Z
    ("6mm Endmill", [0.0, 0.0, 0.0]),
    ("3mm Ballnose", [0.0, 0.0, -1.25]),
    ("Probe", [0.0, 0.0, 0.5]),
    ("V-Bit 60", [0.0, 0.0, 2.1]),
]
SPINDLE_MAX = 24000
INPUT_NAMES = ["HTTP", "Telnet", "File", "USB", "Aux", "Trigger", "Queue", "LCD", "SBC", "Daemon", "Aux2", "Autopause"]

quiet = False
slow_kbps = 0.0  # --slow: download speed (KB/s), 0 = full speed


def log(*a):
    if not quiet:
        print(*a, flush=True)


def r3(v):
    return round(float(v), 3)


def iso(t=None):
    return datetime.datetime.fromtimestamp(t if t is not None else time.time()).strftime("%Y-%m-%dT%H:%M:%S")


class Cancelled(Exception):
    """E-stop or job cancel: stop what is running."""


class Abort(Exception):
    """abort command, or M292 P1 on a blocking message: the macro stops."""


class BreakLoop(Exception):
    pass


class ContinueLoop(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------------
# Expressions: {move.axes[2].userPosition + 1}, exists(global.x), "a" ^ "b", ...
# ---------------------------------------------------------------------------------------------------------------------

_TOK = re.compile(
    r"\s*(?:(?P<num>\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+|\d+(?:[eE][-+]?\d+)?)|(?P<str>\"(?:[^\"]|\"\")*\")"
    r"|(?P<chr>'.')|(?P<id>[A-Za-z_][A-Za-z0-9_]*)|(?P<op>==|!=|<=|>=|&&|\|\||[-+*/()\[\].,<>!^#?:=]))"
)


def tokenize(text):
    out, pos = [], 0
    text = text.rstrip()
    while pos < len(text):
        m = _TOK.match(text, pos)
        if not m or m.end() == pos:
            raise ValueError(f"bad expression near '{text[pos:pos + 10]}'")
        pos = m.end()
        kind = m.lastgroup
        val = m.group(kind)
        if kind == "num":
            out.append(("num", float(val) if any(c in val for c in ".eE") else int(val)))
        elif kind == "str":
            out.append(("str", val[1:-1].replace('""', '"')))
        elif kind == "chr":
            out.append(("str", val[1]))
        else:
            out.append((kind, val))
    return out


class Missing(Exception):
    pass


class Expr:
    def __init__(self, text, resolve):
        self.t = tokenize(text)
        self.i = 0
        self.resolve = resolve  # name -> value, raises Missing

    def peek(self, v=None):
        if self.i >= len(self.t):
            return None
        tok = self.t[self.i]
        if v is None:
            return tok
        return tok if tok[1] == v and tok[0] in ("op", "id") else None

    def take(self, v=None):
        tok = self.peek(v)
        if tok is None:
            raise ValueError(f"expected '{v}'" if v else "unexpected end of expression")
        self.i += 1
        return tok

    def parse(self):
        v = self.ternary()
        if self.i != len(self.t):
            raise ValueError(f"unexpected '{self.t[self.i][1]}'")
        return v

    def ternary(self):
        c = self.orr()
        if self.peek("?"):
            self.take("?")
            a = self.ternary()
            self.take(":")
            b = self.ternary()
            return a if truthy(c) else b
        return c

    def orr(self):
        v = self.andd()
        while self.peek("||"):
            self.take()
            w = self.andd()
            v = truthy(v) or truthy(w)
        return v

    def andd(self):
        v = self.cmp()
        while self.peek("&&"):
            self.take()
            w = self.cmp()
            v = truthy(v) and truthy(w)
        return v

    def cmp(self):
        v = self.add()
        for op in ("==", "!=", "<=", ">=", "<", ">", "="):
            if self.peek(op):
                self.take()
                w = self.add()
                if op in ("==", "="):
                    return v == w
                if op == "!=":
                    return v != w
                return {"<=": v <= w, ">=": v >= w, "<": v < w, ">": v > w}[op]
        return v

    def add(self):
        v = self.mul()
        while True:
            if self.peek("+"):
                self.take()
                v = v + self.mul()
            elif self.peek("-"):
                self.take()
                v = v - self.mul()
            elif self.peek("^"):
                self.take()
                v = fmt_value(v) + fmt_value(self.mul())
            else:
                return v

    def mul(self):
        v = self.unary()
        while True:
            if self.peek("*"):
                self.take()
                v = v * self.unary()
            elif self.peek("/"):
                self.take()
                d = self.unary()
                v = v / d if d else float("nan")
            else:
                return v

    def unary(self):
        if self.peek("-"):
            self.take()
            return -self.unary()
        if self.peek("+"):
            self.take()
            return self.unary()
        if self.peek("!"):
            self.take()
            return not truthy(self.unary())
        if self.peek("#"):
            self.take()
            v = self.postfix()
            return len(v) if v is not None else 0
        return self.postfix()

    def postfix(self):
        tok = self.take()
        kind, val = tok
        if kind == "num" or kind == "str":
            return val
        if kind == "op" and val == "(":
            v = self.ternary()
            self.take(")")
            return v
        if kind == "op" and val == "{":
            raise ValueError("unexpected {")
        if kind != "id":
            raise ValueError(f"unexpected '{val}'")
        low = val.lower()
        if low in ("true", "false"):
            return low == "true"
        if low == "null":
            return None
        if low == "pi":
            return math.pi
        if self.peek("("):
            return self.call(low)
        cur = self.resolve(val)
        while True:
            if self.peek("."):
                self.take()
                name = self.take()[1]
                if not isinstance(cur, dict) or name not in cur:
                    raise Missing(name)
                cur = cur[name]
            elif self.peek("["):
                self.take()
                idx = self.ternary()
                self.take("]")
                if not isinstance(cur, list) or not isinstance(idx, (int, float)) or not (0 <= int(idx) < len(cur)):
                    raise Missing(f"[{idx}]")
                cur = cur[int(idx)]
            else:
                return cur

    def call(self, name):
        self.take("(")
        if name == "exists":
            start = self.i
            depth = 0
            while self.i < len(self.t):  # find the matching ')'
                v = self.t[self.i][1]
                if v == "(":
                    depth += 1
                elif v == ")":
                    if depth == 0:
                        break
                    depth -= 1
                self.i += 1
            sub = Expr("", self.resolve)
            sub.t = self.t[start:self.i]
            self.take(")")
            try:
                sub.parse()
                return True
            except Missing:
                return False
        args = []
        if not self.peek(")"):
            args.append(self.ternary())
            while self.peek(","):
                self.take()
                args.append(self.ternary())
        self.take(")")
        fns = {
            "abs": abs, "min": min, "max": max, "sqrt": math.sqrt, "sin": math.sin, "cos": math.cos,
            "tan": math.tan, "atan": math.atan, "atan2": math.atan2, "degrees": math.degrees,
            "radians": math.radians, "floor": lambda x: int(math.floor(x)), "ceil": lambda x: int(math.ceil(x)),
            "mod": lambda a, b: a % b, "isnan": lambda x: isinstance(x, float) and math.isnan(x),
            "round": lambda x, n=0: round(x, int(n)), "exp": math.exp, "log": math.log,
        }
        if name == "fileexists":
            return self.resolve("__fileexists__")(args[0])
        if name in fns:
            return fns[name](*args)
        raise ValueError(f"unknown function {name}()")


def truthy(v):
    if isinstance(v, str):
        return v != ""
    return bool(v)


def fmt_value(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        if math.isnan(v):
            return "nan"
        s = f"{v:.3f}".rstrip("0")
        return s + "0" if s.endswith(".") else s
    if isinstance(v, (list, tuple)):
        return "{" + ",".join(fmt_value(x) for x in v) + "}"
    return str(v)


def to_om_value(v):
    """A value as the object model shows it (global variables)."""
    if isinstance(v, float):
        return r3(v)
    return v


# ---------------------------------------------------------------------------------------------------------------------
# G-code words
# ---------------------------------------------------------------------------------------------------------------------


def strip_comment(line):
    """Remove ; comments (not inside quotes or braces)."""
    out, q, depth = [], False, 0
    for c in line:
        if c == '"':
            q = not q
        elif not q:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            elif c == ";" and depth == 0:
                break
        out.append(c)
    return "".join(out).rstrip()


def strip_paren_comments(line):
    """Remove (...) comments (not inside quotes or {expressions})."""
    out, q, depth, braces = [], False, 0, 0
    for c in line:
        if c == '"':
            q = not q
        if not q and depth == 0:
            if c == "{":
                braces += 1
            elif c == "}":
                braces -= 1
        if braces > 0:
            out.append(c)
            continue
        if not q and c == "(":
            depth += 1
            continue
        if not q and c == ")" and depth > 0:
            depth -= 1
            continue
        if depth == 0:
            out.append(c)
    return "".join(out)


def split_words(line):
    """G-code words: G10 L2 P1 X{1+2} -> [('G','10','num'), ('L','2','num'), ..., ('X','1+2','expr')]"""
    words, i, n = [], 0, len(line)
    while i < n:
        c = line[i]
        if c.isspace():
            i += 1
            continue
        if c == '"':  # a bare quoted parameter: M32 "file"
            j, s = i + 1, []
            while j < n:
                if line[j] == '"':
                    if j + 1 < n and line[j + 1] == '"':
                        s.append('"')
                        j += 2
                        continue
                    break
                s.append(line[j])
                j += 1
            words.append(("", "".join(s), "str"))
            i = j + 1
            continue
        if not c.isalpha():
            i += 1  # stray character
            continue
        letter = c.upper()
        i += 1
        while i < n and line[i] == " " and letter in "PSRKT" and i + 1 < n and line[i + 1] in '"{':
            i += 1  # M291 P "text"
        if i < n and line[i] == '"':
            j, s = i + 1, []
            while j < n:
                if line[j] == '"':
                    if j + 1 < n and line[j + 1] == '"':
                        s.append('"')
                        j += 2
                        continue
                    break
                s.append(line[j])
                j += 1
            words.append((letter, "".join(s), "str"))
            i = j + 1
        elif i < n and line[i] == "{":
            depth, j = 0, i
            while j < n:
                if line[j] == "{":
                    depth += 1
                elif line[j] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            words.append((letter, line[i + 1:j], "expr"))
            i = j + 1
        else:
            j = i
            while j < n and not line[j].isspace() and not (line[j].isalpha() and line[j] not in "eE" and j > i):
                j += 1
            words.append((letter, line[i:j], "num"))
            i = j
    return words


class Params:
    def __init__(self, words, ev):
        self.w = {}
        for letter, raw, kind in words:
            self.w[letter] = (raw, kind)
        self.ev = ev

    def __contains__(self, k):
        return k in self.w

    def value(self, k, default=None):
        if k not in self.w:
            return default
        raw, kind = self.w[k]
        if kind == "str":
            return raw
        if kind == "expr":
            return self.ev(raw)
        if raw == "":
            return default
        try:
            return float(raw)
        except ValueError:
            return raw

    def num(self, k, default=None):
        v = self.value(k, default)
        if v is None:
            return default
        try:
            return float(v)
        except (TypeError, ValueError):
            return default

    def text(self, k, default=""):
        v = self.value(k, None)
        return default if v is None else fmt_value(v) if not isinstance(v, str) else v


def split_commands(words):
    """Split a line's words into commands: a new one starts at each G or M (and at a leading T)."""
    cmds, cur = [], None
    for w in words:
        if w[0] in "GM" and w[2] == "num":
            if cur is not None:
                cmds.append(cur)
            cur = [w]
        elif w[0] == "T" and cur is None:
            cur = [w]
        else:
            if cur is None:
                cur = [("", "", "num")]  # coordinates only: the previous G0/G1 (modal)
            cur.append(w)
    if cur is not None:
        cmds.append(cur)
    return cmds


# ---------------------------------------------------------------------------------------------------------------------
# The machine
# ---------------------------------------------------------------------------------------------------------------------


class Ctx:
    """One running program: the HTTP channel, a macro, or the job file."""

    def __init__(self, m, channel, job=False):
        self.m = m
        self.channel = channel
        self.job = job
        self.gen = m.estop_gen
        self.relative = False
        self.inch = False
        self.feed = 1000.0
        self.motion = "G0"
        self.vars = {}
        self.params = {}
        self.iterations = [0]
        self.depth = 0
        self.on_line = None

    def child(self, params):
        c = Ctx(self.m, self.channel, self.job)
        c.gen = self.gen
        c.relative = self.relative
        c.inch = self.inch
        c.feed = self.feed
        c.params = params
        c.depth = self.depth + 1
        return c  # no on_line: a macro's lines are not the job file's position


class Machine:
    def __init__(self, args):
        self.args = args
        self.lock = threading.RLock()
        self.cond = threading.Condition(self.lock)
        self.motion_lock = threading.Lock()
        self.letters = ["X", "Y", "Z"] + (["A"] if args.axes == 4 else [])
        self.scale = max(0.05, args.speed)
        self.sd = Path(args.sd).expanduser()
        self.start = time.time()
        self.seqs = {k: 1 for k in ("boards", "directories", "fans", "global", "heat", "inputs", "job", "move",
                                    "network", "reply", "sensors", "spindles", "state", "tools", "volumes")}
        self.replies = []
        self.estop_gen = 0
        n = len(self.letters)
        self.wcs = [[0.0] * 9 for _ in range(n)]
        # G54: the stock corner at X100 Y50, its top at Z-70 (where the fake probe touches)
        self.wcs[0][0], self.wcs[1][0], self.wcs[2][0] = 100.0, 50.0, -70.0
        self.tool_offsets = [list(t[1]) for t in TOOLS]
        self.last_file = None
        self.last_duration = 0
        self.selected_file = None
        self.http_q = queue.Queue()
        self.reset()
        threading.Thread(target=self._http_worker, daemon=True).start()
        threading.Thread(target=self._physics, daemon=True).start()
        self.run_config()

    # ---- state ----

    def reset(self):
        with self.lock:
            n = len(self.letters)
            self.homed = [bool(self.args.homed)] * n
            self.mpos = [AXES[L]["home"] if self.args.homed else {"X": 300.0, "Y": 200.0, "Z": -10.0}.get(L, 0.0)
                         for L in self.letters]
            self.wcs_n = 0
            self.tool = 0
            self.spindle_active = 0
            self.spindle_state = "stopped"
            self.spindle_current = 0.0
            self.speed_factor = 1.0
            self.babystep = 0.0
            self.moving = False
            self.cur_feed = 0.0
            self.in_macro = 0
            self.halted = False
            self.job = None
            self.msgbox = None
            self.msg_seq = getattr(self, "msg_seq", 0)
            self.msg_answer = None
            self.input = None
            self.display_message = ""
            self.globals = {}
            self.allow_unhomed = False
            self.probe_triggered = False
            for k in self.seqs:
                self.seqs[k] += 1

    def bump(self, *keys):
        with self.lock:
            for k in keys:
                self.seqs[k] += 1

    def reply(self, text):
        with self.lock:
            for line in str(text).split("\n"):
                self.replies.append(line)
            self.seqs["reply"] += 1
        log("  <", text)

    def tool_off(self, i):
        if 0 <= self.tool < len(self.tool_offsets) and i < 3:
            return self.tool_offsets[self.tool][i]
        return 0.0

    def user(self, i):
        return self.mpos[i] - self.wcs[i][self.wcs_n] - self.tool_off(i)

    def status(self):
        if self.halted:
            return "halted"
        if self.job:
            return self.job["state"]
        if self.moving or self.in_macro > 0:
            return "busy"
        return "idle"

    # ---- object model ----

    def om_move(self, full=True):
        axes = []
        for i, L in enumerate(self.letters):
            a = {"machinePosition": r3(self.mpos[i]), "userPosition": r3(self.user(i)), "stepPos": 0}
            if full:
                d = AXES[L]
                a.update({
                    "acceleration": 1000.0, "babystep": r3(self.babystep) if L == "Z" else 0, "backlash": 0,
                    "current": 2000, "drivers": [f"0.{i}"], "homed": self.homed[i], "jerk": 600.0, "letter": L,
                    "max": d["max"], "maxProbed": False, "microstepping": {"interpolated": True, "value": 16},
                    "min": d["min"], "minProbed": False, "percentCurrent": 100, "percentStstCurrent": 70,
                    "printingJerk": 600.0, "reducedAcceleration": 500.0, "speed": d["speed"],
                    "stepsPerMm": 400.0, "visible": True, "workplaceOffsets": [r3(v) for v in self.wcs[i]]})
            axes.append(a)
        req = r3(self.cur_feed / 60.0) if self.moving else 0
        m = {"axes": axes, "currentMove": {"acceleration": 0, "deceleration": 0, "extrusionRate": 0,
                                           "laserPwm": None, "requestedSpeed": req, "topSpeed": req},
             "virtualEPos": 0}
        if full:
            m.update({"extruders": [], "idle": {"factor": 0.3, "timeout": 30.0},
                      "kinematics": {"name": "cartesian"}, "limitAxes": True,
                      "noMovesBeforeHoming": not self.allow_unhomed, "printingAcceleration": 10000.0,
                      "speedFactor": r3(self.speed_factor), "travelAcceleration": 10000.0,
                      "workplaceNumber": self.wcs_n, "compensation": {"file": None, "type": "none"},
                      "backlashFactor": 1, "keepout": [], "queue": [{"gracePeriod": 0.01, "length": 40}]})
        return m

    def om_job(self):
        j = self.job
        now = time.time()
        if j:
            dur = now - j["start"] - j["paused_total"] - ((now - j["pause_start"]) if j["pause_start"] else 0)
            pos = j["pos"]
            left = int(dur * (j["size"] - pos) / pos) if pos > 0 and dur > 1 else None
            f = {"customInfo": {}, "filament": [], "fileName": j["name"], "generatedBy": j["generator"],
                 "height": 0, "lastModified": j["modified"], "layerHeight": 0, "numLayers": 0,
                 "printTime": j["estimate"], "simulatedTime": None, "size": j["size"], "thumbnails": []}
            return {"build": None, "duration": int(now - j["start"]), "file": f, "filePosition": pos,
                    "lastDuration": self.last_duration, "lastFileName": self.last_file, "lastWarmUpDuration": 0,
                    "layer": None, "layerTime": None, "pauseDuration": int(j["paused_total"]),
                    "rawExtrusion": None, "warmUpDuration": None,
                    "timesLeft": {"filament": None, "file": left,
                                  "slicer": max(0, int(j["estimate"] - dur)) if j["estimate"] else None}}
        f = {"customInfo": {}, "filament": [], "fileName": None, "generatedBy": None, "height": 0,
             "lastModified": None, "layerHeight": 0, "numLayers": 0, "printTime": None, "simulatedTime": None,
             "size": 0, "thumbnails": []}
        return {"build": None, "duration": None, "file": f, "filePosition": 0, "lastDuration": self.last_duration,
                "lastFileName": self.last_file, "lastWarmUpDuration": 0, "layer": None, "layerTime": None,
                "pauseDuration": None, "rawExtrusion": None, "warmUpDuration": None,
                "timesLeft": {"filament": None, "file": None, "slicer": None}}

    def om_state(self, full=True):
        up = time.time() - self.start
        s = {"currentTool": self.tool, "gpOut": [], "msUpTime": int((up % 1) * 1000), "status": self.status(),
             "time": iso(), "upTime": int(up)}
        if full:
            s.update({"atxPower": None, "atxPowerPort": None, "beep": None, "deferredPowerDown": None,
                      "displayMessage": self.display_message, "laserPwm": None, "logFile": None, "logLevel": "off",
                      "machineMode": "CNC", "macroRestarted": False, "messageBox": self.msgbox,
                      "nextTool": self.tool, "powerFailScript": "", "previousTool": -1, "restorePoints": [],
                      "startupError": None, "thisActive": True})
        return s

    def om_tools(self, full=True):
        out = []
        for n, (name, _) in enumerate(TOOLS):
            t = {"state": "active" if n == self.tool else "off", "active": [], "standby": [], "isRetracted": False}
            if full:
                t.update({"axes": [[0], [1], [2]], "extruders": [], "fans": [], "feedForwardAdvance": 0,
                          "filamentExtruder": -1, "heaters": [], "mix": [], "name": name, "number": n,
                          "offsets": [r3(v) for v in self.tool_offsets[n]], "offsetsProbed": 0,
                          "retraction": {"extraRestart": 0, "length": 0, "speed": 0, "unretractSpeed": 0,
                                         "zHop": 0},
                          "spindle": 0, "spindleRpm": self.spindle_active if n == self.tool else 0})
            out.append(t)
        return out

    def om_spindles(self, full=True):
        s = {"active": self.spindle_active, "current": int(round(self.spindle_current)), "state": self.spindle_state}
        if full:
            s.update({"canReverse": True, "frequency": 0, "idlePwm": 0, "max": SPINDLE_MAX, "maxPwm": 1, "min": 0,
                      "minPwm": 0})
        return [s]

    def om_sensors(self):
        return {"analog": [], "filamentMonitors": [], "gpIn": [],
                "endstops": [{"highEnd": L == "Z", "triggered": False, "type": "inputPin"} for L in self.letters],
                "probes": [{"calibA": 0, "deadTime": 0.5, "threshold": 500, "triggerHeight": 0, "type": 8,
                            "value": [1000 if self.probe_triggered else 0]}]}

    def om_inputs(self, full=True):
        busy = self.in_macro > 0 or self.moving
        out = []
        for i, name in enumerate(INPUT_NAMES):
            inp = {"active": True, "feedRate": 50.0, "inMacro": i == 0 and self.in_macro > 0, "lineNumber": 0,
                   "motionSystem": 0, "state": "executing" if (i == 0 and busy) else "idle"}
            if full:
                inp.update({"axesRelative": False, "compatibility": "RepRapFirmware", "distanceUnit": "mm",
                            "drivesRelative": True, "inverseTimeMode": False, "macroRestartable": False,
                            "name": name, "selectedPlane": 0, "stackDepth": 0, "volumetric": False})
            out.append(inp)
        return out

    def om_boards(self, full=True):
        b = {"freeRam": 81348, "mcuTemp": {"current": 36.5, "max": 38.2, "min": 30.1},
             "v12": {"current": 12.1, "max": 12.2, "min": 12.0}, "vIn": {"current": 24.2, "max": 24.4, "min": 23.9}}
        if full:
            b.update({"canAddress": 0, "firmwareDate": "2026-06-01", "firmwareFileName": "Duet3Firmware_MB6HC.bin",
                      "firmwareName": "RepRapFirmware", "firmwareVersion": "3.6.1 (fake)", "maxHeaters": 32,
                      "maxMotors": 6, "name": "Duet 3 MB6HC (fake)", "shortName": "MB6HC",
                      "supports12864": False, "uniqueId": "FAKE0-DUET0-SIMUL-ATION-00000"})
        return [b]

    def om_volumes(self):
        vols = []
        for d in ("0", "1"):
            mounted = (self.sd / d).is_dir() and (d == "0" or self.args.card1)
            vols.append({"capacity": 31914983424 if mounted else 0, "freeSpace": 30000000000 if mounted else 0,
                         "mounted": mounted, "openFiles": 0, "path": f"{d}:/", "speed": 20000000 if mounted else 0})
        return vols

    def om_section(self, key, full=True):
        with self.lock:
            if key == "move":
                return self.om_move(full)
            if key == "job":
                return self.om_job()
            if key == "state":
                return self.om_state(full)
            if key == "tools":
                return self.om_tools(full)
            if key == "spindles":
                return self.om_spindles(full)
            if key == "sensors":
                return self.om_sensors()
            if key == "inputs":
                return self.om_inputs(full)
            if key == "boards":
                return self.om_boards(full)
            if key == "heat":
                return {"bedHeaters": [], "chamberHeaters": [], "coldExtrudeTemperature": 160.0,
                        "coldRetractTemperature": 90.0, "heaters": []}
            if key == "fans":
                return []
            if key == "volumes":
                return self.om_volumes()
            if key == "directories":
                return {"filaments": "0:/filaments/", "firmware": "0:/firmware/", "gCodes": "0:/gcodes/",
                        "macros": "0:/macros/", "menu": "0:/menu/", "system": "0:/sys/", "web": "0:/www/"}
            if key == "network":
                return {"corsSite": None, "hostname": "fakeduet", "name": "Fake Duet",
                        "interfaces": [{"activeProtocols": ["http"], "actualIP": "127.0.0.1",
                                        "configuredIP": "0.0.0.0", "dnsServer": "0.0.0.0", "firmwareVersion": None,
                                        "gateway": "0.0.0.0", "mac": "BE:EF:00:00:00:01", "numReconnects": None,
                                        "signal": None, "speed": 100, "state": "active", "subnet": "255.0.0.0",
                                        "type": "lan"}]}
            if key == "global":
                return {k: to_om_value(v) for k, v in self.globals.items()}
            if key == "seqs":
                return dict(self.seqs, volChanges=[0, 0])
            if key == "limits":
                return {"axes": 15, "tools": 50}
            return None

    def om_frequent(self):
        with self.lock:
            return {"boards": self.om_boards(False), "fans": [], "heat": {"heaters": []},
                    "inputs": self.om_inputs(False), "job": self.om_job(), "move": self.om_move(False),
                    "sensors": self.om_sensors(), "seqs": self.om_section("seqs"),
                    "spindles": self.om_spindles(False), "state": self.om_state(False),
                    "tools": self.om_tools(False)}

    # ---- expressions ----

    def resolver(self, ctx):
        def resolve(name):
            if name == "__fileexists__":
                return lambda p: self.path(str(p), "sys").exists()
            if name == "global":
                return dict(self.globals)
            if name == "var":
                return ctx.vars if ctx else {}
            if name == "param":
                return ctx.params if ctx else {}
            if name == "input":
                return self.input
            if name == "iterations":
                return ctx.iterations[-1] if ctx else 0
            if name == "line":
                return 0
            if name == "result":
                return 0
            v = self.om_section(name)
            if v is None:
                raise Missing(name)
            return v
        return resolve

    def eval(self, text, ctx):
        return Expr(text, self.resolver(ctx)).parse()

    # ---- files ----

    def path(self, name, default_dir="gcodes"):
        name = name.strip()
        m = re.match(r"^(\d):/?(.*)$", name)
        if m:
            return self.sd / m.group(1) / m.group(2)
        if name.startswith("/"):
            return self.sd / "0" / name.lstrip("/")
        return self.sd / "0" / default_dir / name

    def om_name(self, p):
        rel = p.relative_to(self.sd)
        parts = rel.parts
        return f"{parts[0]}:/" + "/".join(parts[1:])

    # ---- the program runner ----

    def run_text(self, text, ctx, offsets=False):
        entries = []
        pos = 0
        for raw in text.split("\n"):
            off = pos
            pos += len(raw.encode("utf-8", "replace")) + 1
            line = raw.rstrip("\r")
            body = strip_comment(line)
            stripped = body.strip()
            if stripped and re.match(r"^[(NGMTXYZABCIJFS]", stripped, re.I) and not re.match(
                    r"^(set|if|elif|else|while|var|global|echo|abort|break|continue|M98)\b", stripped, re.I):
                stripped = strip_paren_comments(stripped).strip()
            if not stripped:
                continue
            indent = len(line) - len(line.lstrip(" \t"))
            entries.append((indent, stripped, off))
        self.exec_range(entries, 0, len(entries), ctx)

    def check(self, ctx):
        if ctx.gen != self.estop_gen:
            raise Cancelled()
        if ctx.job and self.job is not None and self.job.get("cancel"):
            raise Cancelled()

    def exec_range(self, entries, start, end, ctx):
        def block_end(j, ind):
            k = j + 1
            while k < end and entries[k][0] > ind:
                k += 1
            return k

        i = start
        while i < end:
            ind, text, off = entries[i]
            self.check(ctx)
            if ctx.on_line:
                ctx.on_line(off)
            low = text.lower()
            word = low.split(None, 1)[0] if low else ""
            rest = text[len(word):].strip()
            try:
                if word == "if":
                    be = block_end(i, ind)
                    taken = truthy(self.eval(rest, ctx))
                    if taken:
                        self.exec_range(entries, i + 1, be, ctx)
                    i = be
                    while i < end and entries[i][0] == ind and entries[i][1].lower().split(None, 1)[0] in (
                            "elif", "else"):
                        w = entries[i][1].lower().split(None, 1)[0]
                        be2 = block_end(i, ind)
                        if not taken:
                            if w == "else" or truthy(self.eval(entries[i][1][4:].strip(), ctx)):
                                taken = True
                                self.exec_range(entries, i + 1, be2, ctx)
                        i = be2
                    continue
                if word in ("elif", "else"):
                    i = block_end(i, ind)
                    continue
                if word == "while":
                    be = block_end(i, ind)
                    ctx.iterations.append(0)
                    try:
                        while truthy(self.eval(rest, ctx)) and ctx.iterations[-1] < 100000:
                            try:
                                self.exec_range(entries, i + 1, be, ctx)
                            except ContinueLoop:
                                pass
                            ctx.iterations[-1] += 1
                    except BreakLoop:
                        pass
                    finally:
                        ctx.iterations.pop()
                    i = be
                    continue
                if word == "break":
                    raise BreakLoop()
                if word == "continue":
                    raise ContinueLoop()
                if word == "abort":
                    if rest:
                        self.reply(fmt_value(self.eval(rest, ctx)))
                    raise Abort()
                if word == "var":
                    name, _, expr = rest.partition("=")
                    ctx.vars[name.strip()] = self.eval(expr, ctx)
                elif word == "global":
                    name, _, expr = rest.partition("=")
                    name = name.strip()
                    if name in self.globals:
                        self.reply(f"Error: global variable '{name}' already exists")
                    else:
                        self.globals[name] = self.eval(expr, ctx)
                        self.bump("global")
                elif word == "set":
                    target, _, expr = rest.partition("=")
                    target = target.strip()
                    value = self.eval(expr, ctx)
                    if target.startswith("global."):
                        name = target[7:]
                        if name not in self.globals:
                            self.reply(f"Error: unknown variable '{target}'")
                        else:
                            self.globals[name] = value
                            self.bump("global")
                    elif target.startswith("var."):
                        ctx.vars[target[4:]] = value
                    else:
                        self.reply(f"Error: cannot set '{target}' on the fake Duet")
                elif word == "echo":
                    self.echo(rest, ctx)
                else:
                    self.exec_gcode(text, ctx)
            except (Missing, ValueError, ZeroDivisionError, TypeError) as e:
                self.reply(f"Error: {text[:60]}: {e}")
            i += 1

    def echo(self, rest, ctx):
        target, mode = None, None
        m = re.match(r'^(>>?)\s*"((?:[^"]|"")*)"\s*(.*)$', rest)
        if m:
            mode, target, rest = m.group(1), m.group(2).replace('""', '"'), m.group(3)
        values = []
        if rest.strip():
            e = Expr(rest, self.resolver(ctx))
            values.append(e.ternary())
            while e.peek(","):
                e.take()
                values.append(e.ternary())
        line = " ".join(fmt_value(v) for v in values)
        if target is None:
            self.reply(line)
            return
        p = self.path(target, "sys")
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w" if mode == ">" else "a", encoding="utf-8") as f:
            f.write(line + "\n")

    # ---- G-code ----

    def exec_gcode(self, text, ctx):
        text = re.sub(r"^N\d+\s*", "", text)
        text = re.sub(r"\*\d+\s*$", "", text)
        words = split_words(text)
        machine_coords = any(w[0] == "G" and w[1] == "53" for w in words)
        for cmd in split_commands(words):
            letter, num = cmd[0][0], cmd[0][1]
            p = Params(cmd[1:], lambda e: self.eval(e, ctx))
            code = f"{letter}{num}" if letter else ctx.motion
            code = re.sub(r"^([GM])0+(\d)", r"\1\2", code) if len(code) > 2 else code
            if code not in ("M112", "M999"):  # "M112 M999": the restart still runs after the stop
                self.check(ctx)
            if letter == "T":
                self.select_tool(int(float(num)) if num not in ("", None) else -1)
                continue
            try:
                self.do(code, p, ctx, machine_coords)
            except (Cancelled, Abort):
                raise
            except (Missing, ValueError, TypeError) as e:
                self.reply(f"Error: {code}: {e}")

    def do(self, code, p, ctx, machine_coords):
        m = self
        if code in ("G0", "G1"):
            ctx.motion = code
            self.move(p, ctx, rapid=(code == "G0"), machine_coords=machine_coords)
        elif code in ("G2", "G3"):
            ctx.motion = code
            self.arc(p, ctx, cw=(code == "G2"))
        elif code == "G4":
            secs = p.num("S", 0) + p.num("P", 0) / 1000.0
            self.sleep(secs, ctx)
        elif code == "G10":
            self.g10(p, ctx)
        elif code in ("G20", "G21"):
            ctx.inch = code == "G20"
        elif code == "G28":
            self.home(p, ctx)
        elif code in ("G38.2", "G38.3", "G38.4", "G38.5"):
            self.probe(p, ctx, error=code in ("G38.2", "G38.4"))
        elif code == "G53":
            pass
        elif code in WCS_CODES:
            with m.lock:
                m.wcs_n = WCS_CODES.index(code)
            self.bump("move")
        elif code == "G90":
            ctx.relative = False
        elif code == "G91":
            ctx.relative = True
        elif code == "G92":
            with m.lock:
                for i, L in enumerate(m.letters):
                    if L in p:
                        m.mpos[i] = p.num(L) + m.wcs[i][m.wcs_n] + m.tool_off(i)
                        m.homed[i] = True
            self.bump("move")
        elif code in ("G17", "G18", "G19", "G40", "G49", "G61", "G64", "G80", "G94", "G98", "G99"):
            pass
        elif code in ("M0", "M1"):
            if ctx.job:
                self.pause_job(by_file=True)
            elif self.job:
                self.cancel_job()
        elif code in ("M2", "M30"):
            name = p.text("P") or next((raw for (raw, kind) in p.w.values() if kind == "str"), "")
            if code == "M30" and name:  # M30 "file": delete it
                path = self.path(name, "gcodes")
                if path.is_file():
                    path.unlink()
                    log(f"  deleted {self.om_name(path) if path.is_absolute() else name}")
                else:
                    self.reply(f"Error: M30: file {name} not found")
            elif ctx.job:
                raise StopIteration()
        elif code in ("M3", "M4"):
            with m.lock:
                if "S" in p:
                    m.spindle_active = int(max(0, min(SPINDLE_MAX, p.num("S"))))
                m.spindle_state = "forward" if code == "M3" else "reverse"
            self.bump("spindles", "tools")
        elif code == "M5":
            with m.lock:
                m.spindle_state = "stopped"
            self.bump("spindles", "tools")
        elif code == "M6":
            if "T" in p:
                self.select_tool(int(p.num("T")))
        elif code in ("M7", "M8", "M9", "M80", "M81", "M106", "M107", "M110", "M111", "M120", "M121", "M400",
                      "M500", "M501", "M502", "M929", "M575", "M453", "M584", "M350", "M92", "M201", "M203", "M566",
                      "M906", "M84", "M18", "M17"):
            if code in ("M18", "M84"):
                with m.lock:
                    m.homed = [False] * len(m.letters)
                self.bump("move")
        elif code == "M20":
            self.reply(json.dumps(self.file_list(p.text("P", "0:/gcodes/"), int(p.num("R", 0)))))
        elif code == "M23":
            self.selected_file = p.text("P") or p.text("")
            self.reply(f"File {self.selected_file} selected for printing")
        elif code == "M24":
            if self.job:
                self.resume_job()
            elif self.selected_file:
                self.start_job(self.selected_file)
        elif code == "M25":
            self.pause_job()
        elif code == "M32":
            name = p.text("P") or next((raw for (raw, kind) in p.w.values() if kind == "str"), "")
            self.start_job(name)
        elif code == "M36":
            self.reply(json.dumps(self.file_info(p.text("P") or p.text(""))))
        elif code == "M98":
            self.macro(p.text("P"), ctx, p)
        elif code == "M99":
            raise StopIteration()
        elif code == "M114":
            with m.lock:
                u = " ".join(f"{L}:{m.user(i):.3f}" for i, L in enumerate(m.letters))
                mc = " ".join(f"{L}:{m.mpos[i]:.3f}" for i, L in enumerate(m.letters))
            self.reply(f"{u} E:0.000 Count 0 0 0 Machine {mc} Bed comp 0.000")
        elif code == "M115":
            self.reply("FIRMWARE_NAME: RepRapFirmware for Duet 3 MB6HC FIRMWARE_VERSION: 3.6.1 (fake) "
                       "ELECTRONICS: Duet 3 MB6HC v1.02 (fake_duet.py) FIRMWARE_DATE: 2026-06-01")
        elif code == "M117":
            with m.lock:
                m.display_message = p.text("P") or next((raw for (raw, kind) in p.w.values() if kind == "str"), "")
            self.bump("state")
        elif code == "M118":
            self.reply(p.text("P"))
        elif code == "M119":
            self.reply("Endstops - " + ", ".join(f"{L}: not stopped" for L in m.letters) + ", Z probe: not stopped")
        elif code == "M122":
            self.reply("=== Diagnostics ===\nRepRapFirmware for Duet 3 MB6HC version 3.6.1 (fake_duet.py)\n"
                       f"Status: {m.status()}, up {int(time.time() - m.start)} s\nNothing wrong: it is a fake.")
        elif code == "M220":
            if "S" in p:
                with m.lock:
                    m.speed_factor = max(0.01, p.num("S") / 100.0)
                self.bump("move")
            else:
                self.reply(f"Speed factor: {m.speed_factor * 100:.0f}%")
        elif code == "M221":
            pass
        elif code == "M290":
            self.babystep_cmd(p)
        elif code == "M291":
            self.message(p, ctx)
        elif code == "M292":
            self.answer(p)
        elif code == "M471":
            self.rename(p.text("S"), p.text("T"), p.num("D", 0) == 1)
        elif code == "M564":
            if "H" in p:
                with m.lock:
                    m.allow_unhomed = p.num("H") == 0
                self.bump("move")
        elif code == "M112":
            self.estop()
        elif code == "M999":
            self.restart()
        else:
            log(f"  (ignored {code})")

    # ---- motion ----

    def sleep(self, secs, ctx):
        end = time.time() + secs / self.scale
        while time.time() < end:
            self.check(ctx)
            time.sleep(0.02)

    def target_from(self, p, ctx, machine_coords):
        with self.lock:
            target = list(self.mpos)
            moved = []
            for i, L in enumerate(self.letters):
                if L not in p:
                    continue
                v = p.num(L)
                if ctx.inch and L != "A":
                    v *= 25.4
                if machine_coords:
                    target[i] = v
                elif ctx.relative:
                    target[i] += v
                else:
                    target[i] = v + self.wcs[i][self.wcs_n] + self.tool_off(i)
                moved.append(i)
        return target, moved

    def move(self, p, ctx, rapid, machine_coords=False):
        target, moved = self.target_from(p, ctx, machine_coords)
        if "F" in p:
            ctx.feed = max(1.0, p.num("F") * (25.4 if ctx.inch else 1))
        if not moved:
            return
        if not self.allow_unhomed and not all(self.homed[i] for i in moved):
            raise_reply = "Error: G0/G1: insufficient axes homed"
            self.reply(raise_reply)
            if ctx.job:
                self.cancel_job()
            return
        for i in moved:
            L = self.letters[i]
            if self.homed[i]:
                target[i] = max(AXES[L]["min"], min(AXES[L]["max"], target[i]))
        feed = RAPID if rapid else ctx.feed
        if ctx.job:
            feed *= self.speed_factor
        self.animate(target, feed, ctx)

    def arc(self, p, ctx, cw):
        with self.lock:
            sx, sy = self.user(0), self.user(1)
        ex = p.num("X", None)
        ey = p.num("Y", None)
        ex = sx if ex is None else (sx + ex if ctx.relative else ex)
        ey = sy if ey is None else (sy + ey if ctx.relative else ey)
        cx, cy = sx + p.num("I", 0), sy + p.num("J", 0)
        if "F" in p:
            ctx.feed = max(1.0, p.num("F"))
        r = math.hypot(sx - cx, sy - cy)
        a0 = math.atan2(sy - cy, sx - cx)
        a1 = math.atan2(ey - cy, ex - cx)
        if cw:
            while a1 >= a0:
                a1 -= 2 * math.pi
        else:
            while a1 <= a0:
                a1 += 2 * math.pi
        steps = max(4, int(abs(a1 - a0) * r / 2.0))
        for k in range(1, steps + 1):
            a = a0 + (a1 - a0) * k / steps
            sub = {"X": (str(cx + r * math.cos(a)), "num"), "Y": (str(cy + r * math.sin(a)), "num")}
            if "Z" in p and k == steps:
                sub["Z"] = p.w["Z"]
            q = Params([], p.ev)
            q.w = sub
            rel = ctx.relative
            ctx.relative = False
            try:
                self.move(q, ctx, rapid=False)
            finally:
                ctx.relative = rel

    def animate(self, target, feed, ctx, probe=None):
        with self.motion_lock:
            with self.lock:
                start = list(self.mpos)
            dist = math.sqrt(sum((t - s) ** 2 for t, s in zip(target, start)))
            if dist < 1e-6:
                return False
            touched = False
            if probe:
                frac = 1.0
                for i, L in enumerate(self.letters):
                    s, t = start[i], target[i]
                    surf = PROBE_SURFACES.get(L)
                    if surf is None or abs(t - s) < 1e-9:
                        continue
                    if min(s, t) <= surf <= max(s, t) and abs(s - surf) > 1e-6:
                        frac = min(frac, (surf - s) / (t - s))
                        touched = True
                if touched:
                    target = [s + (t - s) * frac for s, t in zip(start, target)]
                    dist = math.sqrt(sum((t - s) ** 2 for t, s in zip(target, start)))
            dur = dist / (feed / 60.0) / self.scale
            t0 = time.time()
            with self.lock:
                self.moving, self.cur_feed = True, feed
            try:
                while True:
                    self.check(ctx)
                    frac = 1.0 if dur <= 0 else min(1.0, (time.time() - t0) / dur)
                    with self.lock:
                        self.mpos = [s + (t - s) * frac for s, t in zip(start, target)]
                    if frac >= 1.0:
                        break
                    time.sleep(0.02)
            finally:
                with self.lock:
                    self.moving = False
            if touched:
                self.probe_triggered = True
                self.bump("sensors")
                threading.Timer(0.5, self._probe_release).start()
            return touched

    def _probe_release(self):
        self.probe_triggered = False
        self.bump("sensors")

    def home(self, p, ctx):
        wanted = [i for i, L in enumerate(self.letters) if L in p] or list(range(len(self.letters)))
        order = sorted(wanted, key=lambda i: 0 if self.letters[i] == "Z" else 1)  # Z up first
        with self.lock:
            self.in_macro += 1
        try:
            for i in order:
                L = self.letters[i]
                target = list(self.mpos)
                target[i] = AXES[L]["home"]
                self.animate(target, HOMING_FEED if L != "A" else 7200.0, ctx)
                with self.lock:
                    self.homed[i] = True
                self.bump("move")
        finally:
            with self.lock:
                self.in_macro -= 1

    def probe(self, p, ctx, error):
        target, moved = self.target_from(p, ctx, False)
        if "F" in p:
            ctx.feed = max(1.0, p.num("F"))
        if not moved:
            return
        if not all(self.homed[i] for i in moved) and not self.allow_unhomed:
            self.reply("Error: G38: insufficient axes homed")
            return
        if self.animate(target, min(ctx.feed, 1000.0), ctx, probe=True):
            return
        if error:
            self.reply("Error: G38.2: probe was not triggered during probing move")
            raise Abort()

    def g10(self, p, ctx):
        L = int(p.num("L", 0)) if "L" in p else None
        P = int(p.num("P", 1))
        with self.lock:
            if L in (2, 20):
                n = max(1, min(9, P)) - 1
                for i, letter in enumerate(self.letters):
                    if letter in p:
                        v = p.num(letter)
                        if L == 2:
                            self.wcs[i][n] = v
                        else:  # L20: the current position becomes v in workplace n
                            self.wcs[i][n] = self.mpos[i] - self.tool_off(i) - v
                self.bump("move")
            elif L is None or L == 1:
                if 0 <= P < len(self.tool_offsets):
                    for i, letter in enumerate("XYZ"):
                        if letter in p:
                            self.tool_offsets[P][i] = p.num(letter)
                    self.bump("tools", "move")
                else:
                    self.reply(f"Error: G10: tool {P} not found")

    def babystep_cmd(self, p):
        if "Z" not in p:
            self.reply(f"Baby stepping offsets (mm): Z:{self.babystep:.3f}")
            return
        z = p.num("Z")
        with self.lock:
            absolute = "R" in p and p.num("R") == 0
            new = z if absolute else self.babystep + z
            delta = new - self.babystep
            self.babystep = new
            self.mpos[2] += delta
        self.bump("move")

    def select_tool(self, n):
        with self.lock:
            if n >= len(TOOLS) or n < -1:
                self.reply(f"Error: T{n}: invalid tool number")
                return
            self.tool = n
        self.bump("tools", "state", "move")

    # ---- macros, messages ----

    def macro(self, name, ctx, p):
        if not name:
            self.reply("Error: M98: missing file name")
            return
        path = self.path(name, "sys")
        if not path.exists() and not re.match(r"^\d:/|^/", name):
            alt = self.path(name, "macros")
            path = alt if alt.exists() else path
        if not path.is_file():
            self.reply(f"Error: M98: Macro file {name} not found")
            return
        if ctx.depth > 10:
            self.reply("Error: M98: macros nested too deep")
            return
        params = {}
        for letter, (raw, kind) in p.w.items():
            if letter != "P":
                params[letter] = p.value(letter)
        child = ctx.child(params)
        with self.lock:
            self.in_macro += 1
        try:
            self.run_text(path.read_text(encoding="utf-8", errors="replace"), child)
        except StopIteration:
            pass
        except Abort:
            pass
        finally:
            with self.lock:
                self.in_macro -= 1

    def message(self, p, ctx):
        mode = int(p.num("S", 1))
        msg = p.text("P", "")
        title = p.text("R", "")
        timeout = p.num("T", 10 if mode == 0 else 0)
        choices = None
        if "K" in p:
            raw, kind = p.w["K"]
            v = p.value("K") if kind != "expr" else self._eval_list(raw, ctx)
            choices = [fmt_value(c) for c in (v if isinstance(v, list) else [v])]
        default = p.value("F", None)
        box = {"axisControls": int(p.num("Z", 0)), "cancelButton": bool(p.num("J", 0)) or mode == 3,
               "choices": choices, "default": default, "max": p.value("H", None), "message": msg,
               "min": p.value("L", None), "mode": mode, "timeout": int(timeout * 1000), "title": title}
        with self.cond:
            self.msg_seq += 1
            box["seq"] = self.msg_seq
            self.msgbox = box
            self.msg_answer = None
            seq = self.msg_seq
        self.bump("state")
        log(f"  [M291 S{mode}] {title}: {msg}" + (f" {choices}" if choices else ""))
        if mode <= 1:
            if timeout > 0:
                threading.Timer(timeout / self.scale, self._close_box, args=(seq,)).start()
            return
        # blocking: wait for the panel's M292
        with self.cond:
            while self.msg_answer is None:
                if ctx.gen != self.estop_gen or (ctx.job and self.job is not None and self.job.get("cancel")):
                    raise Cancelled()
                self.cond.wait(0.2)
            cancelled, value = self.msg_answer
            self.msg_answer = None
        if cancelled:
            self.reply("Cancelled")
            raise Abort()
        self.input = value

    def _eval_list(self, raw, ctx):
        """K{"a","b"} or R{3}: a comma list of expressions."""
        e = Expr(raw, self.resolver(ctx))
        vals = [e.ternary()]
        while e.peek(","):
            e.take()
            vals.append(e.ternary())
        return vals if len(vals) > 1 else vals[0]

    def _close_box(self, seq):
        with self.cond:
            if self.msgbox and self.msgbox["seq"] == seq:
                self.msgbox = None
        self.bump("state")

    def answer(self, p):
        with self.cond:
            box = self.msgbox
            if box is None:
                return
            if "S" in p and int(p.num("S")) != box["seq"]:
                return
            cancelled = p.num("P", 0) == 1
            value = None
            if "R" in p:
                raw, kind = p.w["R"]
                value = self._eval_list(raw, None) if kind == "expr" else p.value("R")
                if isinstance(value, list) and len(value) == 1:
                    value = value[0]
            self.msgbox = None
            self.msg_answer = (cancelled, value)
            self.cond.notify_all()
        self.bump("state")

    # ---- jobs ----

    def estimate(self, text):
        x = y = z = 0.0
        t, feed, rel, motion = 0.0, 1000.0, False, "G0"
        for line in text.split("\n"):
            line = strip_paren_comments(strip_comment(line)).upper()
            if not line.strip():
                continue
            g = re.findall(r"G(\d+)", line)
            for code in g:
                if code in ("0", "00", "1", "01", "2", "02", "3", "03"):
                    motion = code.lstrip("0") or "0"
                elif code == "90":
                    rel = False
                elif code == "91":
                    rel = True
                elif code == "4":
                    m = re.search(r"[SP](\d*\.?\d+)", line)
                    if m:
                        t += float(m.group(1)) / (1000.0 if "P" in line else 1.0)
            f = re.search(r"F(\d*\.?\d+)", line)
            if f:
                feed = max(1.0, float(f.group(1)))
            vals = {k: float(v) for k, v in re.findall(r"([XYZ])(-?\d*\.?\d+)", line)}
            if vals:
                nx = (x + vals.get("X", 0)) if rel else vals.get("X", x)
                ny = (y + vals.get("Y", 0)) if rel else vals.get("Y", y)
                nz = (z + vals.get("Z", 0)) if rel else vals.get("Z", z)
                d = math.sqrt((nx - x) ** 2 + (ny - y) ** 2 + (nz - z) ** 2)
                t += d / ((RAPID if motion == "0" else feed) / 60.0)
                x, y, z = nx, ny, nz
        return int(t / self.scale)

    def start_job(self, name):
        if self.job:
            self.reply("Error: M32: cannot start a job while one is running")
            return
        path = self.path(name, "gcodes")
        if not path.is_file():
            self.reply(f"Error: M32: GCode file \"{name}\" not found")
            return
        text = path.read_text(encoding="utf-8", errors="replace")
        gen = None
        for line in text.split("\n")[:30]:
            m = re.search(r"(Exported by|generated by|Generated by|Post Processor:?)\s*([^)\n;]+)", line)
            if m:
                gen = m.group(2).strip()
                break
        with self.lock:
            self.job = {"name": self.om_name(path), "size": path.stat().st_size, "start": time.time(), "pos": 0,
                        "state": "processing", "paused_total": 0.0, "pause_start": None, "pause_req": False,
                        "resume_req": False, "cancel": False, "estimate": self.estimate(text), "generator": gen,
                        "modified": iso(path.stat().st_mtime), "spindle": None}
        self.bump("job", "state")
        log(f"  job started: {self.job['name']} (about {self.job['estimate']} s)")
        threading.Thread(target=self._run_job, args=(text,), daemon=True).start()

    def _job_line(self, off):
        j = self.job
        if j is None:
            raise Cancelled()
        j["pos"] = off
        if j["pause_req"]:
            self._enter_pause()
        while j["state"] in ("paused", "pausing"):
            if j["cancel"]:
                raise Cancelled()
            if j["resume_req"]:
                self._leave_pause()
                break
            time.sleep(0.05)
        if j["cancel"]:
            raise Cancelled()

    def _enter_pause(self):
        j = self.job
        with self.lock:
            j["pause_req"] = False
            j["state"] = "paused"
            j["pause_start"] = time.time()
            j["spindle"] = (self.spindle_state, self.spindle_active)
            self.spindle_state = "stopped"
        self.bump("job", "state", "spindles")
        self.reply(f"Printing paused at {' '.join(f'{L}{self.user(i):.1f}' for i, L in enumerate(self.letters))}")

    def _leave_pause(self):
        j = self.job
        with self.lock:
            j["resume_req"] = False
            j["state"] = "resuming"
            if j["spindle"]:
                self.spindle_state, self.spindle_active = j["spindle"]
        self.bump("job", "state", "spindles")
        time.sleep(1.0 / self.scale)  # spindle back up to speed
        with self.lock:
            j["paused_total"] += time.time() - (j["pause_start"] or time.time())
            j["pause_start"] = None
            j["state"] = "processing"
        self.bump("job", "state")

    def _run_job(self, text):
        ctx = Ctx(self, "File", job=True)
        ctx.on_line = self._job_line
        result = "done"
        try:
            self.run_text(text, ctx)
        except StopIteration:
            pass
        except Cancelled:
            result = "cancelled"
        except Abort:
            result = "cancelled"
        except Exception as e:  # keep the fake alive
            self.reply(f"Error: job stopped: {e}")
            result = "cancelled"
        j = self.job
        if j is None:
            return
        if result == "cancelled" and ctx.gen == self.estop_gen:
            with self.lock:
                j["state"] = "cancelling"
            self.bump("state")
            time.sleep(0.5)
        with self.lock:
            self.spindle_state = "stopped"
            self.last_file = j["name"]
            self.last_duration = int(time.time() - j["start"])
            self.job = None
        self.bump("job", "state", "spindles", "tools")
        mins, secs = divmod(self.last_duration, 60)
        if result == "done":
            self.reply(f"Finished printing file {j['name']}, print time was {mins // 60}h {mins % 60}m {secs}s")
        else:
            self.reply(f"Cancelled printing file {j['name']}, print time was {mins // 60}h {mins % 60}m {secs}s")

    def pause_job(self, by_file=False):
        j = self.job
        if not j or j["state"] not in ("processing", "resuming"):
            return
        with self.lock:
            j["pause_req"] = True
            j["state"] = "pausing"
        self.bump("state")
        if by_file:
            self._job_line(j["pos"])

    def resume_job(self):
        j = self.job
        if j and j["state"] in ("paused", "pausing"):
            j["resume_req"] = True

    def cancel_job(self):
        j = self.job
        if j:
            j["cancel"] = True
            with self.cond:
                self.cond.notify_all()

    # ---- e-stop, restart, config ----

    def estop(self):
        with self.cond:
            self.estop_gen += 1
            self.halted = True
            self.spindle_state = "stopped"
            self.homed = [False] * len(self.letters)
            if self.job:
                self.job["cancel"] = True
            self.msgbox = None
            self.cond.notify_all()
        self.bump("move", "state", "spindles", "job")
        self.reply("Emergency stop")
        log("  *** EMERGENCY STOP ***")

    def restart(self):
        with self.cond:
            self.estop_gen += 1
            self.cond.notify_all()
        time.sleep(0.1)
        self.reset()
        log("  restarted (M999): unhomed, config.g runs again")
        threading.Thread(target=self.run_config, daemon=True).start()

    def run_config(self):
        cfg = self.sd / "0" / "sys" / "config.g"
        if cfg.is_file():
            ctx = Ctx(self, "Trigger")
            try:
                self.run_text(cfg.read_text(encoding="utf-8", errors="replace"), ctx)
            except (StopIteration, Abort, Cancelled):
                pass

    # ---- file calls ----

    def file_list(self, d, first=0):
        path = self.path(d if d.endswith("/") else d + "/", "gcodes")
        if not path.is_dir():
            return {"err": 2}
        files = []
        for e in sorted(path.iterdir(), key=lambda e: e.name.lower()):
            if e.name.startswith("."):
                continue
            st = e.stat()
            files.append({"type": "d" if e.is_dir() else "f", "name": e.name, "size": 0 if e.is_dir() else st.st_size,
                          "date": iso(st.st_mtime)})
        return {"dir": d, "first": first, "files": files[first:], "next": 0}

    def file_info(self, name):
        if not name and self.job:
            name = self.job["name"]
        path = self.path(name, "gcodes")
        if not path.is_file():
            return {"err": 1}
        text = path.read_text(encoding="utf-8", errors="replace")
        gen = None
        for line in text.split("\n")[:30]:
            m = re.search(r"(Exported by|generated by|Generated by|Post Processor:?)\s*([^)\n;]+)", line)
            if m:
                gen = m.group(2).strip()
                break
        st = path.stat()
        return {"err": 0, "fileName": self.om_name(path), "size": st.st_size, "lastModified": iso(st.st_mtime),
                "height": 0.0, "layerHeight": 0.0, "numLayers": 0, "printTime": self.estimate(text),
                "simulatedTime": None, "filament": [], "generatedBy": gen, "thumbnails": []}

    def rename(self, old, new, replace):
        src, dst = self.path(old, "gcodes"), self.path(new, "gcodes")
        if not src.exists():
            self.reply(f"Error: M471: file {old} not found")
            return False
        if dst.exists():
            if not replace:
                self.reply(f"Error: M471: file {new} already exists")
                return False
            dst.unlink()
        dst.parent.mkdir(parents=True, exist_ok=True)
        src.rename(dst)
        return True

    # ---- threads ----

    def submit(self, gcode):
        text = gcode.replace("\r", "")
        first = split_words(text.strip().split("\n")[0]) if text.strip() else []
        lead = f"{first[0][0]}{first[0][1]}" if first and first[0][0] in "GM" else ""
        lead = re.sub(r"^([GM])0+(\d)", r"\1\2", lead)
        for line in text.strip().split("\n"):
            log(">", line)
        if lead in ("M112", "M999", "M25", "M24", "M0", "M1", "M292", "M220", "M221", "M290", "M108", "M410"):
            ctx = Ctx(self, "HTTP")
            try:
                self.run_text(text, ctx)
            except (StopIteration, Abort, Cancelled):
                pass
        else:
            self.http_q.put(text)

    def _http_worker(self):
        while True:
            text = self.http_q.get()
            ctx = Ctx(self, "HTTP")
            try:
                self.run_text(text, ctx)
            except (StopIteration, Abort, Cancelled, BreakLoop, ContinueLoop):
                pass
            except Exception as e:
                self.reply(f"Error: {e}")

    def _physics(self):
        last = time.time()
        while True:
            time.sleep(0.05)
            now = time.time()
            dt = now - last
            last = now
            with self.lock:
                target = self.spindle_active if self.spindle_state in ("forward", "reverse") else 0
                step = 9000.0 * dt * self.scale
                if abs(self.spindle_current - target) <= step:
                    self.spindle_current = float(target)
                else:
                    self.spindle_current += step if target > self.spindle_current else -step


# ---------------------------------------------------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------------------------------------------------


def make_handler(m, password):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):
            pass

        def query(self):
            q = {}
            for part in urlsplit(self.path).query.split("&"):
                if not part:
                    continue
                k, _, v = part.partition("=")
                q[unquote(k)] = unquote(v)
            return q

        def send(self, body, ctype="application/json", code=200):
            data = body if isinstance(body, bytes) else body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(data)

        def send_slowly(self, data, kbps):
            # a slow network (Duet 2 WiFi): the file in 4 KB pieces at kbps kilobytes per second
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            piece = 4096
            for i in range(0, len(data), piece):
                self.wfile.write(data[i:i + piece])
                time.sleep(piece / (kbps * 1024.0))

        def js(self, obj, code=200):
            self.send(json.dumps(obj, separators=(",", ":")), code=code)

        def do_GET(self):
            route = urlsplit(self.path).path
            q = self.query()
            try:
                if route == "/rr_connect":
                    if password and q.get("password", "") != password:
                        return self.js({"err": 1})
                    log("panel connected")
                    return self.js({"err": 0, "sessionTimeout": 8000, "boardType": "duet3mb6hc", "apiLevel": 2,
                                    "sessionKey": 4321})
                if route == "/rr_disconnect":
                    return self.js({"err": 0})
                if route == "/rr_model":
                    key = q.get("key", "")
                    flags = q.get("flags", "")
                    if key == "":
                        result = m.om_frequent() if "f" in flags else {
                            k: m.om_section(k) for k in ("boards", "directories", "fans", "global", "heat", "inputs",
                                                         "job", "move", "network", "sensors", "seqs", "spindles",
                                                         "state", "tools", "volumes")}
                    else:
                        result = m.om_section(key)
                    # RRF sends object members in alphabetical order (DuetScreen relies on it, e.g. the
                    # message box: its seq must come before its title)
                    body = '{"key":%s,"flags":%s,"result":%s}' % (
                        json.dumps(key), json.dumps(flags), json.dumps(result, separators=(",", ":"), sort_keys=True))
                    return self.send(body)
                if route == "/rr_gcode":
                    m.submit(q.get("gcode", ""))
                    return self.js({"buff": 255})
                if route == "/rr_reply":
                    with m.lock:
                        text = "".join(line + "\n" for line in m.replies)
                        m.replies.clear()
                    return self.send(text, "text/plain")
                if route == "/rr_filelist" or route == "/rr_files":
                    return self.js(m.file_list(q.get("dir", "0:/gcodes/"), int(q.get("first", "0") or 0)))
                if route == "/rr_fileinfo":
                    return self.js(m.file_info(q.get("name", "")))
                if route == "/rr_download":
                    p = m.path(q.get("name", ""), "gcodes")
                    if not p.is_file():
                        return self.send(b"", "text/plain", 404)
                    if slow_kbps > 0:
                        return self.send_slowly(p.read_bytes(), slow_kbps)
                    return self.send(p.read_bytes(), "application/octet-stream")
                if route == "/rr_delete":
                    p = m.path(q.get("name", ""), "gcodes")
                    if p.is_file():
                        p.unlink()
                    elif p.is_dir():
                        try:
                            p.rmdir()
                        except OSError:
                            return self.js({"err": 1})
                    else:
                        return self.js({"err": 1})
                    m.bump("volumes")
                    return self.js({"err": 0})
                if route == "/rr_mkdir":
                    m.path(q.get("dir", ""), "gcodes").mkdir(parents=True, exist_ok=True)
                    return self.js({"err": 0})
                if route == "/rr_move":
                    ok = m.rename(q.get("old", ""), q.get("new", ""), q.get("deleteexisting", "no") == "yes")
                    return self.js({"err": 0 if ok else 1})
                if route == "/rr_thumbnail":
                    return self.js({"err": 1})
                if route == "/":
                    with m.lock:
                        pos = ", ".join(f"{L} {m.user(i):.3f}" for i, L in enumerate(m.letters))
                        st = m.status()
                    return self.send(f"<html><body style='font-family:sans-serif'><h3>fake_duet.py is running</h3>"
                                     f"<p>Status: {st}<br>Position: {pos}</p></body></html>", "text/html")
                return self.send(b"", "text/plain", 404)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_POST(self):
            route = urlsplit(self.path).path
            q = self.query()
            length = int(self.headers.get("Content-Length", "0") or 0)
            body = self.rfile.read(length) if length else b""
            if route == "/rr_upload":
                p = m.path(q.get("name", ""), "gcodes")
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(body)
                log(f"  uploaded {q.get('name', '')} ({len(body)} bytes)")
                m.bump("volumes")
                return self.js({"err": 0})
            return self.send(b"", "text/plain", 404)

    return Handler


# ---------------------------------------------------------------------------------------------------------------------
# The virtual SD card
# ---------------------------------------------------------------------------------------------------------------------

CONFIG_G = """; config.g of the fake Duet (fake_duet.py). Only the lines below matter here.
M453                                              ; CNC mode
if fileexists("0:/sys/cnc-settings.g")
  M98 P"0:/sys/cnc-settings.g"                    ; the CNC panel's settings (it writes this file)
if fileexists("0:/sys/cnc-lasttool.g")
  M98 P"0:/sys/cnc-lasttool.g"                    ; the tool loaded at power-off (panel setting)
"""

MACROS = {
    "01_Park.g": """; Park: Z up, then to the back of the table
G53 G0 Z0
G53 G0 X300 Y380
""",
    "02_Spindle_warmup.g": """; Spindle warm-up: 3 steps of 5 s
echo "Warm-up 6000 rpm"
M3 S6000
G4 S5
echo "Warm-up 12000 rpm"
M3 S12000
G4 S5
echo "Warm-up 18000 rpm"
M3 S18000
G4 S5
M5
echo "Warm-up done"
""",
    "03_Coolant_on.g": "M8\necho \"Coolant on\"\n",
    "04_Coolant_off.g": "M9\necho \"Coolant off\"\n",
    "Probing/Probe_Z_top.g": """; Probe Z on top of the stock (the fake stock top is at machine Z-70)
M291 P"Probe Z on the stock top. Probe over the stock?" R"Probe Z" S3
G91
G38.2 Z-100 F300
G0 Z2
G38.2 Z-5 F60
G90
G10 L20 P{move.workplaceNumber + 1} Z0
G91
G0 Z5
G90
M291 P{"Z zero set for " ^ (move.workplaceNumber < 6 ? "G" ^ (54 + move.workplaceNumber) : "G59." ^ (move.workplaceNumber - 5))} R"Probe Z" S1 T5
""",
    "Probing/Probe_X_edge.g": """; Probe the X edge (the fake edge is at machine X150), moving +X
G91
G38.2 X60 F300
G0 X-2
G38.2 X5 F60
G90
G10 L20 P{move.workplaceNumber + 1} X0
G91
G0 X-5
G90
echo "X zero set at the edge"
""",
    "Tests/Message_OK.g": 'M291 P"This is a blocking message (M291 S2). Press OK." R"Test" S2\necho "OK pressed"\n',
    "Tests/Yes_No.g": 'M291 P"Continue? (M291 S3: OK / Cancel)" R"Test" S3\necho "You chose OK"\n',
    "Tests/Choose_tool.g": """M291 P"Which tool? (M291 S4)" R"Test" S4 K{"6mm Endmill","3mm Ballnose","Probe","V-Bit 60"}
echo "You chose choice", input
""",
    "Tests/Enter_depth.g": """M291 P"Pocket depth in mm (M291 S6)" R"Test" S6 L0.1 H20 F3.5
echo "Depth:", input
""",
    "Tests/Enter_name.g": 'M291 P"Job name (M291 S7)" R"Test" S7 F"bracket"\necho "Name:", input\n',
    "Tests/Toast_5s.g": 'M291 P"A message that closes itself after 5 s (M291 S0)" R"Test" S0 T5\n',
    "Tests/Square_100mm.g": """; Moves a 100 mm square at Z-5 in the active workplace (needs homing)
G90
G0 Z5
G0 X0 Y0
G1 Z-5 F300
G1 X100 F1500
G1 Y100
G1 X0
G1 Y0
G0 Z5
""",
}


def demo_job_spoilboard():
    lines = ["; Spoilboard surfacing 200 x 150, 20 mm stepover (fake_duet.py demo)", "G21 G90", "G54",
             "(Begin operation: Surface spoilboard)", "T0", "M3 S12000", "G4 S2", "G0 Z5", "G0 X0 Y0",
             "G1 Z-0.5 F300"]
    y, row = 0.0, 0
    while y <= 150.0:
        lines.append(f"G1 X{200.0 if row % 2 == 0 else 0.0:.1f} F1800")
        y += 20.0
        if y <= 150.0:
            lines.append(f"G1 Y{y:.1f}")
        row += 1
    lines += ["G0 Z5", "(Finish operation: Surface spoilboard)", "M5", "G0 X0 Y0", "M30"]
    return "\n".join(lines) + "\n"


def demo_job_spiral():
    lines = ["; Spiral engraving (fake_duet.py demo)", "G21 G90", "G54", "(Begin operation: Spiral engrave)", "T3",
             "M3 S16000", "G0 Z3", "G0 X60 Y60", "G1 Z-0.3 F200"]
    for k in range(1, 1200):
        a = k * 0.05
        rr = 1.0 + a * 0.9
        lines.append(f"G1 X{60 + rr * math.cos(a):.3f} Y{60 + rr * math.sin(a):.3f} F900")
    lines += ["G0 Z3", "(Finish operation: Spiral engrave)", "M5", "M30"]
    return "\n".join(lines) + "\n"


def demo_job_pause():
    return """; Job with a tool change prompt and an M0 pause (fake_duet.py demo)
G21 G90
G54
(Begin operation: Profile 6mm)
T0
M3 S10000
G0 Z5
G0 X10 Y10
G1 Z-1 F300
G1 X60 F1200
G1 Y40
G1 X10
G1 Y10
G0 Z20
(Finish operation: Profile 6mm)
M5
(Begin operation: Finish 3mm ballnose)
M291 P"Change to T1 (3mm Ballnose), then OK" R"Tool change" S3
T1
M3 S14000
G0 X20 Y20
G1 Z-0.5 F300
G1 X50 F900
G1 Y30
G1 X20
G1 Y20
G0 Z10
M0 ; pause here: press RESUME
G0 X0 Y0
(Finish operation: Finish 3mm ballnose)
M5
M30
"""


def make_sd(sd, reset):
    sd = Path(sd).expanduser()
    if reset and sd.exists():
        shutil.rmtree(sd)
    if (sd / "0").exists():
        return sd
    log(f"creating the virtual SD card in {sd}")
    for d in ("gcodes/Fixtures", "macros", "sys", "filaments", "www"):
        (sd / "0" / d).mkdir(parents=True, exist_ok=True)
    (sd / "0" / "sys" / "config.g").write_text(CONFIG_G)
    for name, text in MACROS.items():
        p = sd / "0" / "macros" / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    g = sd / "0" / "gcodes"
    for f in ("bracket_fusion.nc", "plate_freecad.nc"):
        src = REPO / "tests" / "object_model" / "cnc" / "gcode" / f
        if src.is_file():
            shutil.copy(src, g / f)
    (g / "spoilboard_surface.gcode").write_text(demo_job_spoilboard())
    (g / "spiral_engrave.gcode").write_text(demo_job_spiral())
    (g / "tool_change_and_pause.gcode").write_text(demo_job_pause())
    (g / "Fixtures" / "vise_jaws.nc").write_text(
        "; Vise jaws (fake_duet.py demo)\nG21 G90\nG54\n(Begin operation: Vise jaws)\nM3 S9000\nG0 Z5\n"
        "G0 X0 Y0\nG1 Z-2 F250\nG1 X120 F1000\nG1 Y20\nG1 X0\nG1 Y0\nG0 Z5\n(Finish operation: Vise jaws)\nM5\nM30\n")
    (sd / "1").mkdir(exist_ok=True)
    (sd / "1" / "spare_part.nc").write_text("; On the second card (1:/)\nG21 G90\nG0 Z5\nG0 X0 Y0\nM30\n")
    return sd


def main():
    global quiet, slow_kbps
    ap = argparse.ArgumentParser(description="A pretend CNC Duet for the DuetScreen simulator.")
    ap.add_argument("--port", type=int, default=8080, help="TCP port (default 8080)")
    ap.add_argument("--bind", default="127.0.0.1", help="address to listen on (0.0.0.0 = whole network)")
    ap.add_argument("--axes", type=int, choices=(3, 4), default=3, help="3 = XYZ, 4 = XYZA")
    ap.add_argument("--homed", action="store_true", help="start homed (default: unhomed, like a real power-on)")
    ap.add_argument("--speed", type=float, default=1.0, help="time factor: 2 runs moves and jobs twice as fast")
    ap.add_argument("--sd", default="~/fake_duet_sd", help="folder used as the SD card (default ~/fake_duet_sd)")
    ap.add_argument("--reset-sd", action="store_true", help="delete the SD folder and create the demo files again")
    ap.add_argument("--card1", action="store_true", help="also mount the second card 1:/")
    ap.add_argument("--password", default="", help="require this password (rr_connect)")
    ap.add_argument("--quiet", action="store_true", help="do not print the G-code it receives")
    ap.add_argument("--slow", type=float, default=0, metavar="KB",
                    help="file downloads at KB kilobytes per second (e.g. 300 for a Duet 2 WiFi; default: full speed)")
    args = ap.parse_args()
    quiet = args.quiet
    slow_kbps = args.slow
    args.sd = str(make_sd(args.sd, args.reset_sd))
    m = Machine(args)
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(m, args.password))
    server.daemon_threads = True
    addr = "127.0.0.1" if args.bind in ("127.0.0.1", "0.0.0.0") else args.bind
    print(f"fake Duet ({args.axes} axes) listening on {args.bind}:{args.port}  SD card: {args.sd}")
    print(f"In the simulator: SETTINGS > CONNECTION > NETWORK, address {addr}:{args.port}   (Ctrl+C stops)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
