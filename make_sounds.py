#!/usr/bin/env python3
"""Synthesizes Gyre's sound effects.

Writes each sound as a .wav (to listen to) and a .vsnd (Vircon32's
native format: "V32-VSND", uint32 sample count, then 16-bit stereo
samples at 44100 Hz), into the sounds/ folder. The order of SOUNDS
is the order they must appear in the ROM definition XML.
"""
import os
import struct
import wave
import numpy as np

RATE = 44100
rng = np.random.default_rng(7)


def t_axis(seconds):
    return np.arange(int(RATE * seconds)) / RATE


def envelope(n, attack=0.002, release=None, curve=3.0):
    """Short attack, then exponential-ish decay to zero."""
    t = np.linspace(0, 1, n)
    env = (1 - t) ** curve
    a = int(attack * RATE)
    if a > 0:
        env[:a] *= np.linspace(0, 1, a)
    return env


def lowpass(x, cutoff):
    """One-pole low-pass; cutoff may be a scalar or per-sample array."""
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
    alpha = 1 - np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += alpha[i] * (x[i] - acc)
        y[i] = acc
    return y


def sweep(f0, f1, seconds, shape="square", exp=True):
    t = t_axis(seconds)
    if exp:
        freq = f0 * (f1 / f0) ** (t / seconds)
    else:
        freq = f0 + (f1 - f0) * t / seconds
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    if shape == "square":
        return np.sign(np.sin(phase))
    if shape == "saw":
        return 2 * ((phase / (2 * np.pi)) % 1.0) - 1
    if shape == "tri":
        return 2 * np.abs(2 * ((phase / (2 * np.pi)) % 1.0) - 1) - 1
    return np.sin(phase)


def normalize(x, peak):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def bang(seconds, cutoff_start, cutoff_end, peak):
    n = int(RATE * seconds)
    noise = rng.uniform(-1, 1, n)
    cut = np.geomspace(cutoff_start, cutoff_end, n)
    s = lowpass(lowpass(noise, cut), cut)
    return normalize(s * envelope(n, curve=2.2), peak)


def notes(freqs, note_len, shape="square", peak=0.4, tail=1.0):
    out = []
    for f in freqs:
        s = sweep(f, f, note_len, shape)
        out.append(s * envelope(len(s), curve=tail))
    s = np.concatenate(out)
    return normalize(lowpass(s, 5000), peak)


# ---------------------------------------------------------------- sounds

def fire():
    s = sweep(2600, 900, 0.08, "square") * 0.6 + sweep(2600, 900, 0.08, "tri") * 0.4
    return normalize(lowpass(s, 7000) * envelope(len(s), curve=1.3), 0.4)


def enemy_die():
    n = int(RATE * 0.35)
    tone = sweep(900, 120, 0.35, "square") * 0.5
    return normalize(lowpass(tone, 3000) * envelope(n, curve=2.0) + bang(0.35, 4000, 300, 0.6), 0.6)


def tanker_split():
    a = sweep(500, 1200, 0.08, "tri")
    b = sweep(700, 1600, 0.08, "tri")
    s = np.concatenate([a, b]) * envelope(len(a) * 2, curve=1.0)
    return normalize(s + np.concatenate([bang(0.16, 3000, 400, 0.3)]), 0.5)


def move():
    t = t_axis(0.025)
    return normalize(np.sin(2 * np.pi * 1800 * t) * np.exp(-t * 200), 0.3)


def zapper():
    n = int(RATE * 0.9)
    t = np.arange(n) / RATE
    noise = rng.uniform(-1, 1, n) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 30 * t)))
    tone = sweep(200, 3000, 0.9, "saw") * 0.4
    s = lowpass(noise * 0.7 + tone, 5000) * envelope(n, curve=1.2)
    return normalize(s, 0.8)


def warp():
    # a long rising whoosh for the dive down the tube
    seconds = 2.6
    n = int(RATE * seconds)
    t = np.arange(n) / RATE
    noise = rng.uniform(-1, 1, n)
    cut = np.geomspace(200, 6000, n)
    whoosh = lowpass(lowpass(noise, cut), cut)
    tone = sweep(80, 900, seconds, "tri") * 0.35
    env = np.minimum(1, t / 0.4) * np.minimum(1, (seconds - t) / 0.3)
    return normalize((whoosh + tone) * env, 0.7)


def death():
    n = int(RATE * 1.4)
    rumble = sweep(120, 30, 1.4, "sine") * envelope(n, curve=1.5)
    return normalize(bang(1.4, 3000, 90, 1.0) + rumble * 0.5, 0.9)


def spike_hit():
    t = t_axis(0.08)
    s = sum(np.sin(2 * np.pi * f * t) * w for f, w in [(1500, 1.0), (2400, 0.6), (3700, 0.4)])
    return normalize(s * np.exp(-t * 45), 0.35)


def enemy_fire():
    s = sweep(1400, 500, 0.1, "square")
    return normalize(lowpass(s, 5000) * envelope(len(s), curve=1.5), 0.35)


def level_start():
    return notes([330, 494, 659, 988], 0.07, "square", 0.4, 0.5)


def extra_life():
    return notes([659, 784, 988, 1319, 988, 1319], 0.07, "square", 0.35, 0.6)


def grab():
    t = t_axis(0.4)
    s = np.sign(np.sin(2 * np.pi * (90 + 40 * np.sin(2 * np.pi * 25 * t)) * t))
    return normalize(lowpass(s, 2500) * np.exp(-t * 4), 0.6)


SOUNDS = [
    ("fire", fire), ("enemy_die", enemy_die), ("tanker_split", tanker_split),
    ("move", move), ("zapper", zapper), ("warp", warp), ("death", death),
    ("spike_hit", spike_hit), ("enemy_fire", enemy_fire), ("level_start", level_start),
    ("extra_life", extra_life), ("grab", grab),
]

# music instruments come last (see make_music.py and chipmusic.h)
from make_music import INSTRUMENTS, write_music_data
SOUNDS += INSTRUMENTS


def write(name, samples, outdir):
    """samples: a mono array, or a (left, right) pair for stereo music"""
    if isinstance(samples, tuple):
        left, right = samples
        pcm = np.clip(np.stack([left, right], axis=1) * 32767, -32768, 32767).astype("<i2")
        stereo = pcm.reshape(-1)
        pcm = pcm[:, 0]
    else:
        pcm = np.clip(samples * 32767, -32768, 32767).astype("<i2")
        stereo = np.repeat(pcm, 2)

    with wave.open(os.path.join(outdir, name + ".wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(stereo.tobytes())

    with open(os.path.join(outdir, name + ".vsnd"), "wb") as f:
        f.write(b"V32-VSND")
        f.write(struct.pack("<I", len(pcm)))
        f.write(stereo.tobytes())


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    write_music_data(os.path.join(here, "music_data.h"))
    outdir = os.path.join(here, "sounds")
    os.makedirs(outdir, exist_ok=True)
    for i, (name, fn) in enumerate(SOUNDS):
        s = fn()
        write(name, s, outdir)
        n = len(s[0]) if isinstance(s, tuple) else len(s)
        print(f"{i:2d} {name:14s} {n / RATE:.2f}s")
