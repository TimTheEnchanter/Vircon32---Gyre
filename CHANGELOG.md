# Changelog

## 1.0 (2026-11-03)

First release.

### Game
- Tempest-style tube shooter: your claw rides the rim of a wireframe tube, moving toward whichever neighbouring lane lies in the pressed direction, with auto-fire and a superzapper (a full clear, then a weaker second use, each level).
- Five enemy types: flippers, tankers, spikers, fuseballs (only hittable while crossing a lane) and pulsars (electrify their lane after a flickering warning).
- Sixteen webs, one per level, several of them open or with uneven lanes. Colors change every four levels. After level 16 the webs repeat.
- A gentle climb for six levels, then a steeper one: faster enemies, more of them at once, and more fuseballs and pulsars.
- Spikes left by spikers kill you during the dive to the next web unless you shoot them away first.
- A perfect level bonus (500 × the level) for clearing a level without losing a life. Extra life every 20,000 points.
- Four secret bonus stages behind hidden warps: Spin Cycle, Gem Rush, Dark Web and Swarm. No lives at risk, a 5,000 bonus for a perfect stage.

### Feel
- The claw flashes red and trembles, with a warning ping, when a flipper or fuseball on the rim is about to strike.
- Spikes chip when hit and shatter when shot away.
- The dive fades out at the far end of the tube, and each new web fades in as the camera eases toward it.
- Level banners, new-enemy notes on levels 4 and 5, and a game over screen showing your score and level.

### Menus and saving
- Pause menu with Resume, Options and Quit to Title.
- Options: music and effects volume, Easy/Normal/Hard, screen shake and screen flashes.
- A top-five high score table with initials, tagged E or H for Easy and Hard scores.
- Attract mode: logo, high scores and a demo replaying a recorded game exactly. The built-in demo starts on level 5, so it shows the later enemies.
- The memory card saves the table, the best game as the attract demo, the options and every secret stage ever found. It never writes to a card belonging to another game, and checksums reject damaged data.

### Sound and music
- 13 synthesized sound effects.
- Five chiptune tracks played by the note sequencer from Rockfield: a title theme, the in-game trance theme, a secret stage loop, a level clear fanfare and a game over sting. The in-game theme resumes where it left off after a fanfare or a secret stage.

### vector.h 1.1
- A 3D camera: `vec_camera`, `vec_project`, `vec_line_3d` (with clipping at the near plane and the screen edges) and `vec_depth_fade`.
- A `?` character in the vector font.

### Tools
- `tools/headless.cpp` runs a ROM without a window for testing (scripted input, frame and audio capture, memory cards, dropped-frame log).
- `tools/gen_demo.py` plays scripted games and turns the best into the built-in demo.
