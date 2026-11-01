#!/usr/bin/env python3
"""Gyre's chiptune music, played by a note sequencer on the console.

The songs are written below as note strings. Instead of rendering them to
audio (which made the ROM about 14 MB), this script exports:

  * a handful of tiny instrument samples: three pulse (square) waves with
    different duty cycles, a stepped triangle, and four drums; and
  * music_data.h, the note events for each song, which chipmusic.h plays
    by pitching those samples with the sound chip's channel speed and
    shaping each note's volume frame by frame.

Tempos are chosen so a sixteenth note is a whole number of frames
(6, 8 and 9 frames at 60 fps), so every note starts exactly on a frame.

make_sounds.py imports INSTRUMENTS from here (they go into the ROM after
the sound effects) and calls write_music_data() to regenerate the header.
"""
import numpy as np

RATE = 44100
FPS = 60
MAX_VOICES = 7          # sound channels the engine may use
GAIN_MAX = 0.6          # loudest gain used in the songs -> velocity 127
rng = np.random.default_rng(1985)

NOTE_INDEX = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5,
              "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def midi(name):
    """'A4' -> 69"""
    return 12 * (int(name[-1]) + 1) + NOTE_INDEX[name[:-1]]


def freq(name):
    return 440.0 * 2 ** ((midi(name) - 69) / 12)


A_MINOR = ["A", "B", "C", "D", "E", "F", "G"]


def diatonic_down(name, steps):
    """Moves a note down by scale steps in A minor (G# counts as G)."""
    pitch, octave = name[:-1], int(name[-1])
    pitch = pitch.replace("#", "")
    i = A_MINOR.index(pitch)
    for _ in range(steps):
        i -= 1
        if A_MINOR[i % 7] == "B":       # crossing from C down to B: octave changes
            octave -= 1
    return A_MINOR[i % 7] + str(octave)


# ------------------------------------------------------------- instruments
#
# Pitched samples loop: their length is a whole number of waveform periods,
# plus one extra sample equal to the first (the sound chip loops at
# length - 1). Each instrument also carries a fixed stereo position, since
# the chip's channels have no panning of their own.

PULSE_BASE = 441.0      # period of exactly 100 samples
TRI_BASE = 110.25       # period of exactly 400 samples

INS_P12, INS_P25, INS_P50, INS_TRI, INS_KICK, INS_SNARE, INS_HAT, INS_OHAT = range(8)
DUTY_TO_INS = {0.125: INS_P12, 0.25: INS_P25, 0.5: INS_P50}


def looped(wave):
    return np.concatenate([wave, wave[:1]])


def pulse_sample(duty, pan):
    phase = (np.arange(4400) * PULSE_BASE / RATE) % 1.0
    w = looped(np.where(phase < duty, 1.0, -1.0) * 0.5)
    return (w * min(1.0, 1.0 - pan), w * min(1.0, 1.0 + pan))


def triangle_sample():
    phase = (np.arange(4000) * TRI_BASE / RATE) % 1.0
    tri = 2 * np.abs(2 * phase - 1) - 1
    w = looped(np.round(tri * 7.5) / 7.5 * 0.9)
    return (w, w)


def kick_sample():
    t = np.arange(int(RATE * 0.25)) / RATE
    f = 140 * np.exp(-t * 28) + 45
    w = np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 14) * 0.9
    return (w, w)


def snare_sample():
    t = np.arange(int(RATE * 0.2)) / RATE
    w = (rng.uniform(-1, 1, len(t)) * np.exp(-t * 22) * 0.8
         + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 30) * 0.5) * 0.7
    return (w, w)


def hat_sample(open_):
    t = np.arange(int(RATE * (0.25 if open_ else 0.06))) / RATE
    n = rng.uniform(-1, 1, len(t))
    n = n - np.concatenate([[0], n[:-1]])
    w = n * np.exp(-t * (18 if open_ else 70)) * 0.5
    return (w * 0.8, w)


# name, function returning (left, right); same order as the INS_ numbers
INSTRUMENTS = [
    ("ins_pulse12", lambda: pulse_sample(0.125, 0.3)),
    ("ins_pulse25", lambda: pulse_sample(0.25, -0.25)),
    ("ins_pulse50", lambda: pulse_sample(0.5, -0.3)),
    ("ins_triangle", triangle_sample),
    ("drum_kick", kick_sample),
    ("drum_snare", snare_sample),
    ("drum_hat", lambda: hat_sample(False)),
    ("drum_ohat", lambda: hat_sample(True)),
]

DRUM_INS = {"kick": INS_KICK, "snare": INS_SNARE, "hat": INS_HAT, "ohat": INS_OHAT}


# ------------------------------------------------------------- recording songs

class Song:
    """Same interface the songs were written against, but it records
    note events (in frames) instead of rendering audio."""

    def __init__(self, bpm, steps_per_beat, bars, beats_per_bar=4):
        frames = 60.0 * FPS / bpm / steps_per_beat
        assert abs(frames - round(frames)) < 1e-6, "tempo must give whole frames per step"
        self.fps = int(round(frames))                    # frames per step
        self.total_steps = bars * steps_per_beat * beats_per_bar
        self.length = self.total_steps * self.fps        # in frames
        self.events = []

    def tone(self, start_step, steps, note, kind, gain, pan=0.0,
             duty=0.5, decay=0.0, sustain=1.0, gate=0.9, vibrato=0.0):
        ins = INS_TRI if kind == "triangle" else DUTY_TO_INS[duty]
        dur = max(1, int(round(steps * gate * self.fps)))
        self.events.append(dict(start=start_step * self.fps, dur=dur, note=midi(note), gain=gain,
                                ins=ins, decay=decay, sustain=sustain, vibrato=vibrato))

    def drum(self, start_step, kind, gain, pan=0.0):
        # drums are one-shots; a later hit on the same voice may cut the tail
        self.events.append(dict(start=start_step * self.fps, dur=1, note=0, gain=gain,
                                ins=DRUM_INS[kind], decay=0.0, sustain=1.0, vibrato=0.0))

    def melody(self, start_step, text, kind, gain, pan=0.0, **kw):
        """'A4 - C5 E5 . D5' : one token per step; '-' holds, '.' rests."""
        tokens = text.split()
        i = 0
        while i < len(tokens):
            tok = tokens[i]
            if tok in "-.":
                i += 1
                continue
            length = 1
            while i + length < len(tokens) and tokens[i + length] == "-":
                length += 1
            self.tone(start_step + i, length, tok, kind, gain, pan, **kw)
            i += length


def layer(song_factory, fill):
    s = song_factory()
    fill(s)
    return s.events, None


def mix_layers(song, layers):
    """Collects each part; an echo becomes one extra delayed, quieter part."""
    song.parts = []
    for events, _, fx in layers:
        song.parts.append(events)
        if fx and getattr(song, "echo_ok", True):
            delay_steps, _feedback, mix = fx
            if mix >= 0.3:                      # only the prominent echoes get a voice
                echo = []
                for e in events:
                    e2 = dict(e)
                    e2["start"] = (e["start"] + delay_steps * song.fps) % song.length
                    e2["gain"] = e["gain"] * mix
                    e2["vibrato"] = 0.0
                    echo.append(e2)
                song.parts.append(echo)


class _Rendered:
    def __init__(self, song, loop):
        self.song, self.loop = song, loop


def _render(self, loop):
    return (_Rendered(self, loop), None)


Song.render = _render


def finish(rendered, _none, peak):
    return rendered


# ------------------------------------------------------------- chords

CHORDS = {
    "Am": ["A3", "C4", "E4"], "F": ["F3", "A3", "C4"], "C": ["C4", "E4", "G4"],
    "G": ["G3", "B3", "D4"], "E": ["E3", "G#3", "B3"],
}
ROOTS = {"Am": "A1", "F": "F1", "C": "C2", "G": "G1", "E": "E1"}
PAD = {"Am": "E4", "F": "C5", "C": "G4", "G": "D5", "E": "G#4"}


def up(n, octaves=1):
    return n[:-1] + str(int(n[-1]) + octaves)


# ------------------------------------------------------------- Gyre theme

def gyre_theme():
    """Trance, A minor, 150 BPM (6 frames per sixteenth), 32 bars (51 s).

      bars  1-8   groove: four-on-the-floor kick, rolling bass, offbeat
                  open hats, gated sixteenth arpeggio, pad
      bars  9-16  the anthem lead enters
      bars 17-20  breakdown: kick and bass drop out, pad and lead carry it
      bars 21-24  build: snare roll from eighths to sixteenths, rising run
      bars 25-32  drop: everything back, chords change every bar, lead an
                  octave higher

    Seven voices: kick, hats, snare, bass, arpeggio, lead, pad."""
    BPM, SPB, BARS = 150, 4, 32
    new = lambda: Song(BPM, SPB, BARS)
    progression = (["Am", "Am", "F", "F", "C", "C", "G", "G"] +
                   ["Am", "Am", "F", "F", "C", "C", "G", "E"] +
                   ["F", "F", "G", "G"] +
                   ["Am", "Am", "E", "E"] +
                   ["Am", "F", "C", "G", "Am", "F", "G", "E"])
    assert len(progression) == BARS

    def breakdown(bar):
        return 16 <= bar < 20

    def build(bar):
        return 20 <= bar < 24

    # lead, on an eighth-note grid (8 tokens per bar)
    lead8 = """
    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .
    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .    .  .  .  .  .  .  .  .
    A5 -  -  -  C6 -  B5 A5   E5 -  -  -  -  -  G5 A5   F5 -  -  -  A5 -  G5 F5   C5 -  -  -  -  -  E5 F5
    G5 -  -  -  E5 -  G5 C6   B5 -  -  -  A5 -  G5 E5   D5 -  -  -  G5 -  B5 D6   B5 -  -  -  G#5 - -  -
    A5 -  -  -  -  -  -  -    C6 -  -  -  -  -  -  -    B5 -  -  -  -  -  -  -    D6 -  -  -  -  -  -  -
    E6 -  -  -  -  -  -  -    -  -  -  -  -  -  -  -    .  .  .  .  .  .  .  .    E5 F#5 G#5 A5 B5 C6 D6 E6
    A6 -  -  -  C7 -  B6 A6   F6 -  -  -  A6 -  G6 F6   G6 -  -  -  E6 -  G6 C7   B6 -  -  -  D7 -  B6 G6
    A6 -  -  -  G6 -  E6 C6   F6 -  -  -  E6 -  C6 A5   B5 -  D6 -  G6 -  F6 D6   E6 -  -  -  G#6 - B6 -
    """
    lead16 = " ".join(t + " -" if t != "." else ". ." for t in lead8.split())
    assert len(lead16.split()) == BARS * 16

    def lead_part(s):
        s.melody(0, lead16, "pulse", 0.27, duty=0.25, decay=0.6, sustain=0.6,
                 gate=0.95, vibrato=0.008)

    def pad_part(s):
        # one long note per bar; louder in the breakdown, where it carries the music
        for bar, ch in enumerate(progression):
            gain = 0.15 if breakdown(bar) or build(bar) else 0.09
            s.tone(bar * 16, 16, PAD[ch], "pulse", gain, duty=0.5, decay=0.9, sustain=0.6, gate=0.98)

    def arp_part(s):
        # the trance gate: chord tones every sixteenth, short and accented
        for bar, ch in enumerate(progression):
            a, b, c = CHORDS[ch]
            seq = [up(a), up(c), up(a, 2), up(c), up(b), up(c), up(a, 2), up(b, 2)]
            ramp = 1.0
            if build(bar):
                ramp = 0.5 + 0.5 * ((bar - 20) * 16) / 64.0
            for k in range(16):
                accent = 1.25 if k % 4 == 2 else 1.0          # pushes the offbeat, like a pumping gate
                s.tone(bar * 16 + k, 1, seq[k % 8], "pulse", 0.075 * accent * ramp, duty=0.125,
                       decay=0.05, sustain=0.0, gate=0.9)

    def bass_part(s):
        # rolling bass on the three sixteenths between kicks
        for bar, ch in enumerate(progression):
            if breakdown(bar) or (build(bar) and bar < 22):
                continue
            root = ROOTS[ch]
            for k in range(16):
                if k % 4 != 0:
                    note = up(root) if k % 4 == 3 and k == 15 else up(root)
                    s.tone(bar * 16 + k, 1, note, "triangle", 0.42, gate=0.55)

    def kick_part(s):
        for bar in range(BARS):
            if breakdown(bar) or (build(bar) and bar < 22):
                continue
            for k in (0, 4, 8, 12):
                s.drum(bar * 16 + k, "kick", 0.62)

    def hat_part(s):
        for bar in range(BARS):
            if breakdown(bar):
                continue
            for k in (2, 6, 10, 14):
                s.drum(bar * 16 + k, "ohat", 0.17)               # offbeat open hat
            if not build(bar):
                for k in range(1, 16, 2):
                    s.drum(bar * 16 + k, "hat", 0.07)            # light sixteenths around it

    def snare_part(s):
        for bar in range(BARS):
            b = bar * 16
            if breakdown(bar):
                continue
            if build(bar):
                # roll: eighths, then sixteenths, getting louder into the drop
                step = 2 if bar < 22 else 1
                for k in range(0, 16, step):
                    progress = ((bar - 20) * 16 + k) / 64.0
                    s.drum(b + k, "snare", 0.12 + 0.28 * progress)
                continue
            for k in (4, 12):
                s.drum(b + k, "snare", 0.3)

    song = new()
    mix_layers(song, [
        (*layer(new, lead_part), None),
        (*layer(new, pad_part), None),
        (*layer(new, arp_part), None),
        (*layer(new, bass_part), None),
        (*layer(new, kick_part), None),
        (*layer(new, hat_part), None),
        (*layer(new, snare_part), None),
    ])
    return finish(*song.render(loop=True), 0.6)


TRACKS = [
    ("game", gyre_theme),
]


# ------------------------------------------------------------- export

def allocate_voices(parts):
    """Gives each part its own channel(s). Notes in one part that overlap in
    time are spread over extra channels (chords, drums hit together)."""
    voices = []
    for part in parts:
        mine = []
        for e in sorted(part, key=lambda e: e["start"]):
            for v in mine:
                if v[-1]["start"] + v[-1]["dur"] <= e["start"]:
                    v.append(e)
                    break
            else:
                mine.append([e])
        voices += mine
    return voices


def write_music_data(path):
    decays, vibratos = [0.0], [0.0]
    tracks = []
    for name, fn in TRACKS:
        r = fn()
        song = r.song
        voices = allocate_voices(song.parts)
        assert len(voices) <= MAX_VOICES, f"{name} needs {len(voices)} voices"
        if r.loop:
            length = song.length
        else:
            length = max(e["start"] + e["dur"] for v in voices for e in v) + 40
        for v in voices:
            for e in v:
                for table, key in ((decays, "decay"), (vibratos, "vibrato")):
                    if e[key] not in table:
                        table.append(e[key])
        tracks.append((name, r.loop, length, voices))
    assert len(decays) <= 16 and len(vibratos) <= 4

    events, firsts, counts = [], [], []
    for name, loop, length, voices in tracks:
        for v in range(MAX_VOICES):
            evs = sorted(voices[v], key=lambda e: e["start"]) if v < len(voices) else []
            firsts.append(len(events) // 2)
            counts.append(len(evs))
            for e in evs:
                vel = max(1, min(127, int(round(e["gain"] / GAIN_MAX * 127))))
                sus = int(round(e["sustain"] * 15))
                w0 = e["start"] | (min(e["dur"], 0x7FFF) << 16)
                w1 = (e["note"] | (vel << 7) | (e["ins"] << 14) | (decays.index(e["decay"]) << 17)
                      | (sus << 21) | (vibratos.index(e["vibrato"]) << 25))
                events += [w0, w1]

    def c_array(ctype, name, values, per_line=8, fmt="{}"):
        rows = [", ".join(fmt.format(v) for v in values[i:i + per_line])
                for i in range(0, len(values), per_line)]
        return f"{ctype}[ {len(values)} ] {name} =\n{{\n    " + ",\n    ".join(rows) + "\n};\n"

    k = [0.0 if d == 0 else float(np.exp(-1.0 / (FPS * d))) for d in decays]
    with open(path, "w") as f:
        f.write("// generated by make_music.py: note events for chipmusic.h\n\n")
        f.write(f"#define MUSIC_TRACKS {len(tracks)}\n")
        for i, (name, *_ ) in enumerate(tracks):
            f.write(f"#define MUSIC_{name.upper()} {i}\n")
        f.write("\n")
        f.write(c_array("int", "mus_track_length", [t[2] for t in tracks]))
        f.write(c_array("int", "mus_track_loops", [1 if t[1] else 0 for t in tracks]))
        f.write(c_array("int", "mus_voice_first", firsts))
        f.write(c_array("int", "mus_voice_count", counts))
        f.write("// per-frame volume multiplier for each decay setting (0 = no decay)\n")
        f.write(c_array("float", "mus_decay_k", k, fmt="{:.6f}"))
        f.write(c_array("float", "mus_vibrato_depth", vibratos + [0.0] * (4 - len(vibratos)), fmt="{:.4f}"))
        f.write("// two words per note: start | duration << 16, then\n"
                "// note | velocity << 7 | instrument << 14 | decay << 17 | sustain << 21 | vibrato << 25\n")
        f.write(c_array("int", "mus_events", events, per_line=6, fmt="0x{:08X}"))
    for name, loop, length, voices in tracks:
        n = sum(len(v) for v in voices)
        print(f"  {name:9s} {length / FPS:5.1f}s  {'loop' if loop else 'once'}  {len(voices)} voices  {n} notes")
    print(f"  music_data.h: {len(events) // 2} notes, {len(events) * 4} bytes")


if __name__ == "__main__":
    write_music_data("music_data.h")
