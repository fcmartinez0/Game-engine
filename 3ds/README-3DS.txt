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
  R                    dash (works in the air too, you can't be hurt mid-dash)
  X                    throw a needle (3 charges, they refill)
  A                    silk spin (uses 1 silk segment)
  hold L               heal 2 masks (uses 1 silk segment)
  UP at a bench        rest: full health, respawn point

CONTROLS - duels (when a duelist spots you)
  D-pad                walk, UP jump, DOWN crouch (holding back also blocks)
  hold L               block (hold DOWN too to block low; you stand still)
  R                    dash (hold back + R = backdash)
  Y / X / A            light / medium / heavy
  B                    sword thrust: slow but the longest reach (DOWN + B low, in the air too)
  A + B together       super (when the meter is full)
  Y + X together       throw (up close)
  down, down-forward, forward + attack      projectile
  forward, down, down-forward + attack      rising anti-air
  down, down-back, back + X or A            rush

DEV MENU - press SELECT any time (the game pauses)
  SPEED READOUT    frames per second and milliseconds per frame, at the bottom
  DETAIL           AUTO / LOW / FULL background effects
  PLAY LOG         on by default
  GOD MODE         you can't be hurt (in duels too; the Paragon's finisher still lands)
  DUEL LEVEL       EASY / NORMAL / HARD for the ordinary duelists
  WARP             jump to a section (shaft, duelists, benches, boss, the Paragon's hall)
  MARK THIS MOMENT puts a marker in the log - use it when something looks
                   wrong, stutters, or feels unfair, so it can be found later
  WIPE PARAGON MEMORY forgets everything the final boss has learned about you
  UP/DOWN to choose, A or LEFT/RIGHT to change, SELECT or B to close.

THE PLAY LOG - please send this back
  File:  SD:/3ds/gloamwood/log.txt   (every session is appended)
  It records play time, a performance line every 10 seconds (fps, slow
  frames, CPU time), duels won/lost, deaths, benches, the boss, your
  markers, and a summary when you quit with START + SELECT.
  Copy it to a computer and upload it.

THE PARAGON (final boss, past Thornwarden's far door)
  It learns how YOU fight, from every duel, and remembers between sessions:
  File:  SD:/3ds/gloamwood/profile.txt
  It cannot be beaten in this demo - the point is to see what it learned.
  After it ends the fight a screen lists your habits. Please send back
  profile.txt along with log.txt; the log also records what it learned.
