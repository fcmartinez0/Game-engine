FRAN'S DEMO - playtest build for Nintendo 3DS (custom firmware)
==============================================================
(the folder and file names on the card still say gloamwood, so your saves, log and mods stay where they were)

INSTALL
  1. Copy the "3ds" folder from this zip onto the root of your SD card.
     You should end up with:  SD:/3ds/gloamwood/gloamwood.3dsx
  2. Put the card back, open the Homebrew Launcher, pick Fran's demo.
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
  hold L               GUARD: blocks everything (low, high, jump-ins) except
                       throws. You stand still while you hold it. On EASY and
                       NORMAL it also works in the air, mid-dash, and while
                       your own swing is finishing.
  D-pad                walk, UP jump, DOWN crouch. Holding BACK also blocks,
                       but only at the right height: back = high and middle,
                       down-back = low and middle. Words over your fighter
                       say what got through (LOW! / HIGH!), arrows on an
                       attack show if it hits low or high, and a blue shield
                       shows when you are blocking.
  Y + X when grabbed   break the throw (you have about a quarter second).
                       On EASY and NORMAL a tap of L breaks it too, and you
                       get a little longer.
  R                    dash (hold back + R = backdash)
  Y / X / A            light / medium / heavy
  B                    sword thrust: slow but the longest reach (DOWN + B low, in the air too)
  A + B together       super (when the meter is full)
  Y + X together       throw (up close)
  down, down-forward, forward + attack      projectile
  forward, down, down-forward + attack      rising anti-air
  down, down-back, back + X or A            rush

DEV MENU - always on the BOTTOM (touch) screen
  Tap a row to use it; tap the < > ends of DETAIL, DIFFICULTY and WARP to
  change them (tap the middle of WARP to go). It works while you play.
  SELECT also pauses the game and lets the D-pad + A drive the menu.
  SPEED READOUT    frames per second and milliseconds per frame, at the bottom
  DETAIL           AUTO / LOW / FULL background effects
  PLAY LOG         on by default
  GOD MODE         you can't be hurt (in duels too; the Paragon's finisher still lands)
  DIFFICULTY       EASY (the default) / NORMAL / HARD, saved between sessions.
                   Easy: 8 masks, spikes cost nothing, duelists are
                   beginners who hit softly, the boss has less health.
                   (The Paragon is the same on every setting.)
  WARP             jump to a section (shaft, duelists, benches, boss, the Paragon's hall)
  MARK THIS MOMENT puts a marker in the log - use it when something looks
                   wrong, stutters, or feels unfair, so it can be found later
  PRACTICE DUEL    a no-stakes duel against a beginner (or press DOWN at any bench)
  RELOAD MODS / EXPORT CHARACTERS   see MODS below
  WIPE PARAGON MEMORY forgets everything the final boss has learned about you
  EDIT WORLD       the world editor (or just press START): see below
  UP/DOWN to choose, A or LEFT/RIGHT to change, SELECT or B to close.

THE CREATURES now have eyes: they only know where you are while nothing solid
  is between you, they go to where they last saw you and give up after a while,
  and a creature you hit knows where you are. A grunt hops a one-tile step and
  follows you down a short drop, but waits at a wall or gate it can't climb and
  won't step into a pit or onto spikes. A crawler charges what is in front of
  it. A bat draws back for a moment before it dives (that is your warning), and
  never dives through rock. A spitter only spits at what it can see.

THE CAVES - walk LEFT from the start to the stone door at the forest's edge
  and press UP: the Hollow, then (its door "down") the Deep. Puzzle pieces:
  levers (slash them, or throw a needle), gates and bridges that follow
  them, plates, crates (push, knock, drop them), cracked stone (two hits),
  keys and locked gates, relics. A gate that opens out of sight shows a
  blinking arrow at the screen's edge.

THE WORLD EDITOR - press START while exploring (or dev menu > EDIT WORLD)
  Think of it as a model railway's table: the BOTTOM screen is the table
  seen from above (the map, 8 pixels a tile), the TOP screen is a camera
  at the trains' height, START runs a train over what you just laid.
  Below the map: four tabs of brushes. GROUND paints tiles; PUZZLE,
  EXPLORE and FOES place pieces.
    - Tap a brush, then tap or drag on the map. Tap the brush again (or B)
      to put it down. Hold R while dragging a tile brush: fill a box.
    - Tap a piece to select it: its settings replace the brushes
      (< > change them). Drag a piece to move it. Drop it on ERASER, or
      tap it with ERASER on, to remove it. B or a tap on empty map lets
      go of it.
    - Wires are like coloured extension cords: a gate opens while a
      switch on the same wire is on. A new gate joins the switch you
      placed or picked last, so LEVER, tap the map, GATE, tap the map,
      PLAY already works. A gate's OPENS WHEN: ANY (a doorbell: any
      button rings it) or ALL (a vault: every key turned).
    - D-pad scrolls the map. L undoes (a whole stroke or drag at once).
    - The green TEST FROM HERE figure is where PLAY puts you: drag it.
    - ROOM: other rooms, WIDER / NARROWER / TALLER / SHORTER, the
      backdrop, a NEW ROOM (its door leads back to the marker), BACK TO
      BUILT-IN, RESTORE AUTOSAVE, and an overview to tap and jump to.
    - Doors: select one, < > on LEADS TO links it (both ends at once), but
      only to a free door (one that leads nowhere): browsing can't break a
      working pair. To rewire a pair, step to NOWHERE first.
      GO THROUGH shows the other side.
    - The line at the bottom of the top screen warns about a gate with
      no switch, a door that goes nowhere, and the like.
  START (or PLAY) tests it. Testing costs nothing: each test starts from
  the game as it was when you began editing. START again: back to the
  editor. The bottom screen while testing: BACK TO EDITING, RESTART ROOM,
  STOP EDITING (carry on playing from here, for real), the wires' lights.
  SAVE writes your rooms to SD:/3ds/gloamwood/mods/world.txt (the one
  before is kept as world.bak). Each test and quitting also write
  world.autosave.txt. If the game ended with unsaved work, the next time you
  open the editor that autosave is kept as world.recover.txt, and
  ROOM > RESTORE AUTOSAVE puts that world back.
  world.txt is plain text: you can also edit it on a computer.
  Every stone wall can be climbed (wall-jump). The hero's reach, for
  building: a jump goes 3 tiles up and 4 across; a double jump 5 up;
  with the air dash about 10 across; a dash 3 tiles; the needle flies
  20 tiles straight.

DURING A DUEL the bottom screen is a coach: your moves, and what just
  happened and what to do about it. SELECT still opens the dev menu.

PRACTICE DUEL (dev menu): a no-stakes duel against a beginner, any time.

MODS - change characters without a new build
  Dev menu > EXPORT CHARACTERS writes every character's files to
  SD:/3ds/gloamwood/mods/_export/ (with a README). Copy one up into
  SD:/3ds/gloamwood/mods/, edit it (on a computer: the asset tool in
  tools/asset-prep edits .pam + .anim visually, .moves is plain text),
  put it back and tap RELOAD MODS. Mistakes are listed on the bottom
  screen and in the log; the built-in version is used instead.

THE PLAY LOG - please send this back
  File:  SD:/3ds/gloamwood/log.txt   (always the latest session; the one
         before it is log_previous.txt). The bottom screen says whether the
         log is being written. Switching it off needs two taps.
         It is written to the card at calm moments - the black middle of a
         fade, a bench, opening the menu, a marker - and at least once a
         minute (each write stalls the 3DS for a moment, so not mid-fight).
         Quitting with START + SELECT or from the HOME menu writes the rest.
         If the game ever crashes, the last minute may be missing: use
         MARK THIS MOMENT when something odd happens, which also saves.
  It records play time, a performance line every 10 seconds (fps, slow
  frames, and where each frame's time went: update, draw, present = sending
  the picture to the screen, bottom screen, and vsync wait = waiting for the
  screen's next refresh, which is time to spare when the frame was on time),
  duels won/lost, deaths, benches, the boss, your markers, and a summary
  when you quit.
  Copy it to a computer and upload it.

THE PARAGON (final boss, past Avery The Cat's far door)
  It learns how YOU fight, from every duel, and remembers between sessions:
  File:  SD:/3ds/gloamwood/profile.txt
  It cannot be beaten in this demo - the point is to see what it learned.
  After it ends the fight a screen lists your habits. Please send back
  profile.txt along with log.txt; the log also records what it learned.
