# ge: a platformer + fighting-game engine (desktop and Nintendo 3DS)

A C99 engine for **Silksong-style action platformers** in which some enemies challenge you to a **Street Fighter-style duel**. When a duelist spots you, the screen flashes and shutters like a Pokémon trainer battle, and the game becomes a 1v1 fighting game with its own controls. Win, and you're back in the forest where you left off.

![Gloamwood](docs/gloamwood.png)

*The included demo, "Gloamwood". Clockwise from top left: exploring (spike pit, silk-ring double jump), a duelist spotting you on the plateau, the boss, a duel in progress. Every pixel is drawn in code; there are no art files.*

## Quick start

```sh
cmake -S . -B build && cmake --build build -j
./build/demo                       # windowed (needs SDL2)
./build/demo --autoplay            # a scripted bot plays it, duels included
./build/demo --warp 150            # start at a column (150 = just before the boss)
ctest --test-dir build             # 161 checks, no window needed
./build/demo_headless --autoplay --frames 3000 --dump-every 300 --dump-dir shots   # PNGs, no display
```

## Controls

Buttons are named by position on a 3DS (A right, B bottom, X top, Y left). On a desktop gamepad the same positions apply.

| Overworld | 3DS / pad | Keyboard |
|---|---|---|
| Move · aim up / down | D-pad / stick | arrows or WASD |
| Jump (again in the air = double jump; jump off walls) | B | Z or Space |
| Needle slash · **up** + slash · **down** + slash in the air = pogo bounce | Y | X or J |
| Dash (also in the air, invulnerable) | R | E or O |
| Throw needle (3 charges, they refill) | X | V or I |
| Silk spin (costs 1 silk segment) | A | C or L |
| Heal 2 masks (hold, costs 1 silk segment) | L | Q or U |
| Rest at a bench: full health + respawn point | Up | Up |

| Duel (fighting game) | 3DS / pad | Keyboard |
|---|---|---|
| Walk · jump (up) · crouch (down) · holding back also blocks, at the right height (back = high, down-back = low) | D-pad | arrows |
| Break a throw (about 0.25 s after the grab) | Y + X | X + V |
| **Guard**: blocks every height (only throws beat it); you stand still | L (hold) | Q or U |
| **Dash** (hold back for a backdash) | R | E or O |
| Light · Medium · Heavy | Y · X · A | X · V · C |
| Sword thrust: slow, longest reach (down = low, also in the air) | B | Z |
| Super (full meter) | A + B together | C + Z |
| Throw: Light + Medium together, up close | Y + X | X + V |
| Specials: ↓↘→ + attack (projectile), →↓↘ + attack (rising anti-air), ↓↙← + M/H (rush), →→ dash, ←← backdash | | |

## What's in the game

**Overworld.** Masks for health, a silk spool filled by landing hits, and needle tools. Slash in three directions, pogo off enemies, spikes and enemy shots, wall-slide and wall-jump up a shaft, double-jump, air-dash. Crawlers, swooping bats, spitting plants and club-wielding grunts all hurt on contact and can be cut down. Spikes cost a mask and drop you back on safe ground. Benches heal you and set your respawn point; resting at one (or dying) brings the ordinary enemies back, as in Hollow Knight, while beaten duelists and the boss stay beaten. **Difficulty** (dev menu, saved, easy by default) changes masks (8 / 6 / 4), whether spikes cost a mask, how sharp and how hard-hitting duelists are, and the boss's health; the Paragon is the same on every setting.

**Duelists** (purple wisps around them) stand guard and turn to look around. If one sees you, a "!" pops up, the screen flashes three times, and the duelist's own animated pattern floods out from where they stand in a wave of diamonds (EarthBound-style: palette cycling plus rippling rows), then breaks up again to show the duel in the forest. Only the Paragon fights inside its pattern. Slashing one first gets you a first-strike bonus: it starts the duel at 85% health. Win, and your masks and silk refill. Lose, and you wake up at the last bench. There are four duelists, and the last one is best-of-three.

**Duels** are a full fighting game. Each fighter has standing, crouching and jumping light/medium/heavy attacks, an overhead, a low sweep that knocks down, a projectile, a rising anti-air with invulnerability, a rush, a throw (which can be teched) and a super. Holding back blocks by the classic rules (lows crouching, overheads and jump-ins standing); the L guard blocks every height but roots you and loses to throws. Attack buttons pressed a few frames early are buffered. For the player, the duel names what got through ("LOW! HOLD DOWN-BACK"), marks low and high attacks with an arrow while they wind up, and draws a blue shield while you block. There's chip damage, light → medium → heavy → special cancels, damage scaling, juggles with a limit, knockdowns, rounds and a 99-second clock. The goblin hits hard and slow; the moth fencer is quick with long reach. Both come from one move list with different tuning.

**The final boss, the Paragon.** Past Thornwarden's far door is a long hall with a bench and one more fighter, built to be *your* opponent: it watches every duel you fight and learns what you do (which attacks, from what range, how you block, how you answer jump-ins and projectiles, whether you tech throws), remembers it between sessions in a text file, and plays to beat those habits. Every duelist you defeat also teaches it a technique. It cannot be beaten in this demo: it can be taken down to 1 health, and then, or after 75 seconds, it ends the fight. Afterwards an end-of-demo screen lists the habits it learned about you in plain words. See [docs/PARAGON.md](docs/PARAGON.md).

**The boss**, Thornwarden, shuts the gate behind you. It has armour (no flinching), a sweeping swing, a charge across the arena, a leap that sends shockwaves along the floor, and spear volleys you can slash out of the air. It gets faster below half health.

## How the engine is put together

```
 your game (demo/)  ─ fills intents, runs AI, draws the HUD, decides when to start a duel
        │
 engine/  ge_world   tilemap (solid / one-way / spikes) · parallax · camera · particles · shots
          ge_actor   platformer controller + action combat (the overworld rules)
          ge_fight   fighting-game rules, built on a GeWorld used as the arena
          ge_anim    sprite sheets · clips · frame data (hitboxes, guard, pogo, ...)
          ge_gfx     software renderer → one 400×240 framebuffer · ge_font text
          ge_engine  fixed 60 Hz loop
        │
 platform/  sdl2.c │ n3ds.c │ null.c    ← the six functions a port implements
```

* **One controller for everyone.** The hero and every enemy fill the same `GeIntent` (move, jump, attack, dash, tool, skill, channel), so the hero's double jump or an enemy's charge comes from tuning numbers in a `GeActorDef`, not separate code. *Analogy: everyone drives the same car; only the driver changes.*
* **Two rulebooks, one world.** A duel is a second `GeWorld` that borrows the overworld's backdrop images. The fighting-game module takes over its combat (blocking, juggles, rounds) and draws it at 2× zoom. *Analogy: the same stage with a different referee and a closer camera.*
* **Fixed 60 Hz steps and a seeded RNG** make everything deterministic: the same inputs produce the same game, frame for frame. That keeps the tests honest and makes replays possible.

## Authoring

**Animations and frame data** are text (`.anim`): which cell, for how many ticks, with hitboxes per frame. Startup, active and recovery frames are just frames with or without a hitbox. *Analogy: a film strip with sticky notes on the frames where the blade is dangerous.*

```
cell 32 32
origin 16 31
clip slash1 once dmg=5 kb=2.5,-1 stop=3 stun=14 cancel=1 next=slash2 recoil=1.2
  frame 8 2
  frame 9 3 hit=0,-30,30,24
  frame 10 6
```

| Key | Meaning |
|---|---|
| `dmg kb=x,y stop stun` | damage, knockback, hit-stop freeze, hit-stun |
| `cancel=F next=clip` | combo: from frame F, attack again to chain into `next` |
| `lunge rise` | push forward / launch upward when the clip starts |
| `pogo recoil` | bounce up on a hit (down-slash) / get pushed back on a hit |
| `shoot=F inv=N` | fire the actor's projectile on frame F / invulnerable for N ticks |
| `guard=mid\|low\|high\|none bstun chip kd=1` | fighting game: how it must be blocked, blockstun, chip damage, knockdown |

**Move lists** for duels use fighting-game numpad notation, relative to the way the fighter faces. *Analogy: read the arrows as a phone keypad, with 5 in the middle: 6 is forward, 2 is down, 236 is down → down-forward → forward.*

```c
MV("623H", "f_rise", 3, 0, NULL, GE_AI_ANTIAIR)   /* input, clip, cancel level, meter, projectile, CPU role */
MV("236L", "f_shoot", 3, 0, &needle, GE_AI_ZONE)
MV("LM",   "f_throw", 0, 0, NULL, GE_AI_THROW)    /* two buttons = throw */
MV("2M",   "f_2m", 1, 0, NULL, GE_AI_LOW)
```

The CPU opponent (`ge_fight_cpu`, levels 0–3) uses the role tags to pick its moves: it blocks (and reads lows and overheads at higher levels), anti-airs jump-ins, zones with projectiles, and cancels into specials when a hit lands.

**Backdrops** are `.layers` files: one parallax layer per line, back to front.

```
layer sky.pam factor=0
layer hills.pam factor=0.3 drift=0.05 y=120
layer grass.pam factor=1.4 y=220 alpha=200 front
```

### The mods folder: change characters without rebuilding

The game checks `mods/` (on a 3DS: `SD:/3ds/gloamwood/mods/`) at start-up and on **RELOAD MODS** in the dev menu. Per character (`hero` = Wren, `brawler` = Grobb and Mosshide, `fencer` = Silke and the Champion, `paragon`, `warden` = the boss, `fx` = projectiles):

| File | What it changes |
|---|---|
| `NAME.anim` | animation clips and attack frame data: cells, timing, hitboxes, damage, stun, guard height, cancels |
| `NAME.pam` | the sprite sheet (optional: without it your `.anim` uses the built-in sheet) |
| `NAME.moves` | fighters only: which input does which clip, cancel level, what the CPU uses it for, and tuning (walk, jump, dash, health, throw) |

**EXPORT CHARACTERS** in the dev menu writes the built-in versions of all of them to `mods/_export/` with a README; copy a file up into `mods/`, change it, reload. Open a `.pam` and `.anim` together in the asset tool to edit them visually. Keep every clip name the built-in file has (the game finds them by name); add as many new ones as you like. Problems (a missing clip, a frame pointing outside the sheet, an unknown move input) are listed on the bottom screen and in the play log, and the built-in version is kept, so a typo never breaks the game. `--no-mods` ignores the folder.

### Asset prep tool (drag and drop)

`tools/asset-prep/index.html` is a standalone page, not part of the engine; open it in any browser.

* **Sprite sheets:** drop a sheet, click cells to build clips, drag hitboxes on the live preview, and fill in the attack fields (including pogo, guard, blockstun and the rest). Download `.pam` + `.anim`.
* **Backdrops:** drop layer images, tune their scroll and see a preview that pans like the game camera, then download a `.zip`.

Drop exported files back on the page to keep editing.

```c
ge_sprite_load(&hero, "hero.pam", "hero.anim");
ge_world_load_layers(&world, "forest.layers", &line);   // or: ./build/demo --layers forest.layers
```

## Nintendo 3DS

You need a 3DS with custom firmware (Luma3DS) and the Homebrew Launcher.

**Ready-made package:** `3ds/package.sh` builds the game and writes `dist/gloamwood-3ds.zip`. Unzip it onto the SD card root to get `/3ds/gloamwood/gloamwood.3dsx` plus a readme with the controls.

**Duel coach:** during a duel the bottom screen switches to a coach: the full move list for the 3DS buttons, and a running explanation of what just happened ("HIT MID-SWING. BLOCK FIRST, THEN HIT BACK", "GRABBED! PRESS Y+X AT ONCE TO BREAK IT"). Against the Paragon it shows what the Paragon currently believes about you. **PRACTICE DUEL** in the dev menu starts a no-stakes duel against a beginner Grobb anywhere in the overworld.

**Dev menu and play log:** the dev menu lives on the **bottom screen**: on a 3DS that is the touch screen, and on a PC the window shows a 320×240 panel under the game, like an emulator, and you click it with the mouse. Tap a row to use it (speed readout, detail level, play log, god mode, duel level, warps, "mark this moment", wipe the Paragon's memory); it works while you play, and **Select** also pauses the game so the D-pad can drive it. The engine side is one optional callback, `GeGame.draw_bottom`, that returns whether the panel changed; it is only sent to the screen then, so a still panel costs nothing. On the 3DS the play log is on by default and goes to `/3ds/gloamwood/log.txt`. Each session is appended, with play time, a performance line every 10 seconds, duels, deaths, the boss, your markers, and a summary when you quit with Start + Select. On desktop, pass `--log` to write `gloamwood_log.txt`.

**Build.** Either install devkitPro (`dkp-pacman -S 3ds-dev`) and run:

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
cd 3ds && make                    # → 3ds/gloamwood.3dsx
```

or use devkitPro's official Docker image, with nothing to install:

```sh
docker run --rm -v "$PWD":/src -w /src/3ds devkitpro/devkitarm make
```

**Install.** Copy `gloamwood.3dsx` to the `/3ds/` folder on the SD card, either by moving the card to your PC or wirelessly with FTPD. Launch the Homebrew Launcher (through the Homebrew Launcher app, or Rosalina's menu: L + ↓ + Select → Miscellaneous → Switch the hb. title to the current app) and pick **Gloamwood**. Start + Select quits.

To skip the SD card shuffle while iterating, start the launcher, press Y for netloader, then run `3dslink 3ds/gloamwood.3dsx` from the PC (it ships with devkitPro).

**Performance work for the original 3DS.** Its ARM11 is slow, has no hardware divide and no NEON, so the renderer is written around that: parallax layers are compiled once into runs of opaque pixels (plain copies) and translucent pixels (blended), skipping empty space entirely; the first full-screen layer replaces the clear; opaque tiles are copied; text writes pixels directly; blends use packed multiplies instead of per-channel divides; and the screen copy rotates the picture in cache-friendly 8×8 blocks with word-sized stores. Instructions per frame in the overworld dropped roughly 7× on the desktop profile (8.2M → 1.1M); see the dev menu's speed readout for the real number on your console.

**Which 3DS:** the original 3DS / 3DS XL / 2DS (268 MHz) automatically gets `low_detail`, which drops the mist, light shafts, clouds and vignette. A New 3DS / New 3DS XL / New 2DS XL (804 MHz, recognisable by the C-stick nub and ZL/ZR) runs full detail. Press **Select** in-game for a readout of frames per second and milliseconds per frame.

**Status:** the build uses the real devkitARM toolchain and libctru, and compiles with no warnings. It **has not been run on hardware or an emulator yet**, and performance on the console is unmeasured. The renderer is software. The demo turns off its decorative layers on the original 3DS (`low_detail`). If it isn't fast enough, the next step is a GPU path (citro2d).

## Testing

* `paragon_tests` (55 checks): the player model learns a repeated habit and predicts it, saves and loads, describes the player in words; an undying fighter bottoms out at 1 health and the finisher always ends the round (by hit and by clock); frame-data helpers match real recovery; the Paragon beats the level-3 CPU, takes less damage once it has been taught a player's habit, wins 24 of 24 matches under the boss rules, and is deterministic. `paragon_demo` walks into the hall headlessly and loses to it as designed.
* `engine_tests` (146 checks): the pattern backgrounds (every picture, palette cycling, tiling, clipping, separate cycling groups), the clip parser, tile physics, one-way platforms and spikes, double jump, wall slide and wall jump, air dash, pogo off spikes and enemies, up-slash, flyers, recoil, contact damage, tools and ammo, breakable shots, the silk meter, skills and heals, armour, floor-hugging shots, zoomed drawing, the font, the 3DS screen rotation, the fast blit paths against the plain ones (including off-screen and clipped), and the `.layers` and asset-tool round trips.
* `fight_tests` (61 checks): the forgiving guard (L also blocks in your own move's recovery and in the air), the K button, the guard button (stops lows and overheads, loses to throws), the guard pose when holding back against an attack, the input buffer (and that it expires), the throw-break window, the dash button, motion inputs (including too-slow ones, and left-facing fighters), command normals, dashes, high/low/mid blocking, chip damage, throws in and out of range, cancels only on contact, the L→M→H combo, the juggle limit, supers, invulnerable anti-airs, K.O., time over, best-of-three, and a deterministic CPU-vs-CPU match.
* **Full run:** `--autoplay` plays the whole demo. In a 45,000-tick run (12.5 minutes of game time) the bot climbed the shaft, won all four duels (one after a retry; the champion 2-0) and reached the boss. Its best attempt got the boss down to 78 of 180 health; the boss won every time. With `--god` (a dev flag) it beats the boss and the victory screen plays. Set `GLOAM_LOG=1` to print duels, deaths and boss events.

## Not there yet

The Paragon's story · audio · the 3DS bottom screen and stereoscopic 3D · a level editor (levels are code, or ASCII via `ge_tilemap_from_ascii`) · slopes · save files · remappable controls · multi-hit supers (each move connects once) · the GPU renderer above.
