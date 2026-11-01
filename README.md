# Gyre for Vircon32

Version 0.1, the playable core (with the faster 180 BPM theme). A tube shooter in the style of Tempest, built with `vector.h`.

Load **Gyre.v32** in the Vircon32 emulator.

## How to play

Your claw rides the rim of a wireframe tube. Enemies climb up the lanes from the far end. Shoot them before they reach the rim.

| Input | Action |
|---|---|
| D-pad | Move along the rim. The claw moves to the neighbouring lane that lies most in the pressed direction, so on round webs Down always moves visually down. |
| B or A | Fire (hold for auto-fire) |
| X | Superzapper: destroys everything in the tube. Once per level, plus a weaker second use that destroys one enemy. |
| Start | Start / pause |

**Enemies**

- **Flipper** (red bow tie): climbs, flipping between lanes now and then, and sometimes fires. On the rim it flips toward you and grabs the claw.
- **Tanker** (purple diamond, from level 2): splits into two flippers when shot or when it reaches the rim.
- **Spiker** (green spiral, from level 3): climbs partway, leaving a spike in its lane, then retreats. Shoot spikes to shorten them.

**Levels.** When a level's last enemy is gone, the claw dives down the tube to the next web. A spike in your lane during the dive destroys you, so move to a clear lane or shoot the spike away first.

There are six webs: circle, square, plus, V, star and a flat strip. After those they repeat in a new color, faster.

Extra life every 20,000 points.

## What's here

- `Gyre.c`: the game.
- `vector.h`: the vector library, version 1.1, which adds a 3D camera:
  - `vec_camera` and `vec_project`;
  - `vec_line_3d`, with clipping at the near plane and the screen edges;
  - `vec_depth_fade`.
- `chipmusic.h`, `music_data.h`: the music engine from Rockfield, and Gyre's song as note events.
- `make_sounds.py`, `make_music.py`: generate the sound effects, the music instruments and `music_data.h`.
- `Gyre.xml`, `vector_lines.png`: ROM definition and line texture.

## Building

With the Vircon32 dev tools on your PATH, and Python 3 with NumPy:

```
python3 make_sounds.py
png2vircon vector_lines.png -o vector_lines.vtex
mkdir -p obj
compile Gyre.c -o obj/Gyre.asm
assemble obj/Gyre.asm -o obj/Gyre.vbin
packrom Gyre.xml -o Gyre.v32
```

## Not yet in 0.1

- High score saving.
- Options.
- An attract demo.
- More enemy types: fuseballs and pulsars.
- A pause menu.
