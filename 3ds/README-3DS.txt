GLOAMWOOD - playtest build for Nintendo 3DS (custom firmware)
=============================================================

INSTALL
  1. Copy the "3ds" folder from this zip onto the root of your SD card.
     You should end up with:  SD:/3ds/gloamwood/gloamwood.3dsx
  2. Put the card back, open the Homebrew Launcher, pick GLOAMWOOD.
  Quit with START + SELECT (that also writes the end-of-session summary).

CONTROLS - exploring
  D-pad / Circle Pad   move, aim up/down
  B                    jump (again in the air = double jump, off walls = wall jump)
  Y                    slash   (hold UP = up-slash, DOWN in the air = pogo down-slash)
  A                    dash (works in the air too)
  X                    throw a needle (3 charges, they refill)
  R                    silk spin (uses 1 silk segment)
  hold L               heal 2 masks (uses 1 silk segment)
  UP at a bench        rest: full health, respawn point

CONTROLS - duels (when a duelist spots you)
  D-pad                walk, UP jump, DOWN crouch, HOLD BACK TO BLOCK
  Y / X / A            light / medium / heavy
  B                    super (when the meter is full)
  Y + X together       throw (up close)
  down, down-forward, forward + attack      projectile
  forward, down, down-forward + attack      rising anti-air
  down, down-back, back + X or A            rush

DEV MENU - press SELECT any time (the game pauses)
  SPEED READOUT    frames per second and milliseconds per frame, at the bottom
  DETAIL           AUTO / LOW / FULL background effects
  PLAY LOG         on by default
  GOD MODE         you can't be hurt
  WARP             jump to a section (shaft, duelists, benches, boss)
  MARK THIS MOMENT puts a marker in the log - use it when something looks
                   wrong, stutters, or feels unfair, so it can be found later
  UP/DOWN to choose, A or LEFT/RIGHT to change, SELECT or B to close.

THE PLAY LOG - please send this back
  File:  SD:/3ds/gloamwood/log.txt   (every session is appended)
  It records play time, a performance line every 10 seconds (fps, slow
  frames, CPU time), duels won/lost, deaths, benches, the boss, your
  markers, and a summary when you quit with START + SELECT.
  Copy it to a computer and upload it.
