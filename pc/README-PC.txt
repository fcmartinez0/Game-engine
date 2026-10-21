FRAN'S DEMO - playable on a PC
==============================

WINDOWS (this zip)
  1. Unzip it anywhere (a folder you can write to: your Desktop or Documents,
     not Program Files).
  2. Double-click gloamwood.exe. A black console window opens beside the game:
     leave it, it just prints what is happening.
  Windows may say "Windows protected your PC" because the program isn't signed:
  click "More info", then "Run anyway".
  The game saves next to gloamwood.exe: gloamwood_settings.txt, gloamwood_profile.txt
  (what the final boss has learned about you) and the mods folder (your world.txt).
  Quit with ESC or by closing the window.

LINUX / MAC (build it yourself, it takes a minute)
  Linux:  sudo apt install cmake gcc libsdl2-dev        (or your distro's names)
  Mac:    brew install cmake sdl2
  Then, in the project folder:
          cmake -S . -B build && cmake --build build -j
          ./build/demo

THE WINDOW
  Just the game: a PC has no bottom screen (that is the 3DS's second screen).
  Backspace (SELECT) opens the dev menu over the paused game; click its rows with
  the mouse. The WORLD EDITOR (Enter) makes the window taller to show its map and
  panels under the game, and the window goes back to its size when you stop
  editing. The MOUSE is the stylus there: click and drag. You can resize the window.

KEYS (a gamepad works too: buttons by position, like a 3DS)
  Move            arrow keys or WASD
  B  jump         Z or Space  (again in the air = double jump; off walls = wall jump)
  Y  slash        X or J      (hold UP = up-slash, DOWN in the air = pogo)
  R  dash         E or O
  X  needle       V or I
  A  silk spin    C or L
  L  heal / guard Q or U      (hold)
  START           Enter       (opens the world editor while exploring)
  SELECT          Backspace   (pauses, opens the dev menu)
  ESC             quit
  F12             saves screenshot.png
  Duels use the same keys: Y/X/A = light/medium/heavy (X/V/C), B = sword thrust (Z),
  L = guard (Q), R = dash (E). Full list in the main README.md.

THE CAVES
  Walk LEFT from the start to the stone door at the forest's edge and press UP.

THE WORLD EDITOR (press Enter while exploring)
  Click a brush under the map (the editor's lower panel), then click or drag on the map. Click a piece to
  change its settings, drag it to move it, drop it on ERASER to remove it.
  Q undoes. Hold E (R) and drag a tile brush to fill a box. Arrow keys scroll the map.
  Quickest first try: PUZZLE tab, LEVER, click the map, GATE, click the map beside it,
  Enter to play it, slash the lever (X). Enter again goes back to editing.
  SAVE writes mods\world.txt (plain text: you can also edit it by hand; the
  previous one is kept as world.bak).
  The main README.md has the full guide ("Building the world").
