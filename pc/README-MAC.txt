FRAN'S DEMO - for Mac
=====================

You have "Fran's demo.app": one app for Apple Silicon Macs (M1 and newer) and Intel Macs, for macOS 11 (Big
Sur) or newer (it has been tried on macOS 15 and 26). Nothing else needs installing.

OPENING IT THE FIRST TIME
  macOS is like a doorman who checks every visitor's passport. The game's maker hasn't bought one from
  Apple (it costs money every year), so this app arrives with a handwritten name tag: it's safe, the
  doorman just doesn't know it. You tell him once and he remembers.
  1. Double-click the zip to unpack it, then drag "Fran's demo" into Applications (or onto the desktop).
  2. Double-click the app. macOS says it can't check it for malware. Click "Done" (not "Move to Trash").
  3. Open the Apple menu > System Settings > Privacy & Security. Scroll down to the Security part: it
     says "Fran's demo" was blocked. Click "Open Anyway", type your Mac password, and confirm.
     (The button shows for about an hour after step 2; if it is gone, double-click the app again.)
     On macOS 11 or 12 the same place is called System Preferences > Security & Privacy > General.
     On macOS 14 or older you can also hold Control while clicking the app, choose Open, and confirm.
  From then on it opens with a double-click.

  If macOS says the app is "damaged" instead: open Terminal (Spotlight: type Terminal), type
      xattr -cr
  followed by a space, drag "Fran's demo" onto the Terminal window, and press Return. Then open it again.

PLAYING
  Quit with ESC, Cmd+Q, the red button, or QUIT on the title screen. A picture plays for three seconds
  when the game starts; after the first second any of A, B, X, Y or START (the keys below) skips it.
  Then the title: CONTINUE (when there is a save), NEW GAME, DIFFICULTY, QUIT.
  The game saves itself at every bench, after a duel won, the boss, a relic or a key, and when you
  leave; CONTINUE wakes you at the last bench you rested at.
  The window is just the game (a Mac has no bottom screen: that is the 3DS's second screen).
  Backspace (SELECT) opens the dev menu over the paused game; click its rows with the mouse. The WORLD
  EDITOR (Enter, then WORLD EDITOR) makes the window taller to show its map and panels; the mouse is the stylus there.
  On a Mac keyboard Backspace is the "delete" key, and F12 (screenshot) needs the fn key held.
  The window can be resized, and the green button makes it full screen.

KEYS (a game controller works too: buttons by position, like a 3DS)
  Move            arrow keys or WASD
  B  jump         Z or Space  (again in the air = double jump)
                              off a wall the arrows pick the jump: away = a kick out,
                              nothing = a hop straight up, toward the wall = climb it
  Y  slash        X or J      (hold UP = up-slash, DOWN in the air = pogo)
  R  dash         E or O
  X  needle       V or I
  A  silk spin    C or L
  L  heal / guard Q or U      (hold)
  START           Enter       (pause menu: resume, world editor, difficulty, save and quit)
  SELECT          Backspace   (pauses, opens the dev menu)
  F12             saves screenshot.png in the save folder
  Duels use the same keys: Y/X/A = light/medium/heavy (X/V/C), B = sword thrust (Z),
  L = guard (Q), R = dash (E). Full list in the project's README.md.
  The super (a duel move): press right, down, left, then the slash key (X) together with right.

THE OLD TOWER
  The door beside the forest's second bench. Kick from wall to wall up its shaft (a crack half way up
  hides a mask), cross the planks over the spikes, climb the chimney, and get onto the top ledge
  (double jump, or hold toward it and jump). Its key opens the road east, the locked gate before
  Avery The Cat. DOWN + jump drops through a plank: the quick way back down.

THE CAVES
  Walk LEFT from the start to the stone door at the forest's edge and press UP.

WHERE THE GAME SAVES
  In your own folder, so the app can sit in Applications:
      ~/Library/Application Support/Fran's demo/
  To open it: in Finder choose Go > Go to Folder... and paste that line. It holds your progress
  (gloamwood_save.txt), the settings, the final boss's memory of you (gloamwood_profile.txt), screenshots
  and the "mods" folder (your world.txt from the world editor). Deleting the folder starts the game fresh.

THE WORLD EDITOR (Enter while exploring, then WORLD EDITOR)
  Click a brush under the map, then click or drag on the map. Click a piece to change its settings, drag
  it to move it, drop it on ERASER to remove it. Q undoes. Arrow keys scroll the map.
  Quickest first try: PUZZLE tab, LEVER, click the map, GATE, click the map beside it, Enter to play it,
  slash the lever (X). Enter again goes back to editing.
  The project's README.md has the full guide ("Building the world").

BUILDING IT YOURSELF (optional)
  Needs Xcode's command line tools (Terminal: xcode-select --install) and CMake (brew install cmake).
  In the project folder:   sh tools/package-mac.sh
  That makes dist/frans-demo-mac.zip, the same zip you have. For a quick run without packaging:
  brew install sdl2, then   cmake -S . -B build && cmake --build build -j && ./build/demo
