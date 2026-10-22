# The Paragon: design

*A fighter that watches the whole journey, learns the player, and is the final boss. In the demo it cannot be beaten. The story (who it is, why it watches) comes after this design; every place a story beat will plug in is marked **[story hook]**.*

**Working names.** The fighter is called **PARAGON** on screen until the story names it. "Paragon" here means *the best possible opponent*, not a character with a past yet.

## 1. Goals

1. **Feels perfect.** It never wastes a frame, never misses an input, knows every move's frame data exactly, and always has a counter to what you usually do.
2. **Feels personal.** It plays *you*. Two players meet two different Paragons, because it learned from what each of them actually did.
3. **Grows with the journey.** Every duel feeds it, and every duelist you beat teaches it something. Winning makes the end harder.
4. **Cannot be beaten (demo).** Extremely hard, but never actually winnable. The loss has to feel *inevitable*, not buggy or cheap.
5. **Fair to read.** It only reacts to what a human could see (animations and positions, after a short reaction delay). It never reads button presses.

## 2. What it learns (the Observer)

A **player model** (`GePlayerModel`, `engine/ge_model`) watches every duel from the player's side. It is plain counters, kept small, saved as a text file, so the Paragon remembers across play sessions (on a 3DS: `/3ds/gloamwood/profile.txt`).

| It records | Why it matters |
|---|---|
| Attacks you start, by **role** (poke, low, overhead, anti-air, projectile, rush, combo, super, jump-in, throw) and by **range** (close / near / mid / far) | Predicts what you'll try at the range you're standing at |
| Which attack tends to **follow** which (a bigram) | Predicts your next button after the last one |
| Neutral habits by range: walk in, walk back, stand, crouch, jump, dash, block high, block low | Predicts what you'll do when *not* attacking |
| Defence by **guard height**: how often you block a low, an overhead, a mid | Tells it which mix-up works on you |
| Your answer to **jump-ins** (anti-air, block, or nothing) | Tells it when jumping is safe |
| Your answer to **projectiles** (jump, block, dash, shoot back) | Tells it how to zone you |
| What you do **after blocking** and **after getting up** | Tells it when to throw or to wait |
| How you handle **throws** (tech or not) | Tells it whether grabbing is worth it |
| Overworld stats (slashes, pogo bounces, dashes, needles, heals, deaths) | Not used in fights yet; shown on the end screen. **[story hook]** |

Counters decay slowly between duels and quickly *inside* the final fight, so if you change your style mid-fight it re-learns within a few exchanges.

## 3. How it decides (the Brain)

Every tick, in priority order:

1. **Defend.** It reacts to what you are doing as it looked `react` ticks ago (see knobs). It blocks at the right height, tech-escapes throws, and jumps or dashes through projectiles depending on what the model says works.
2. **Punish.** If you are in recovery (a whiffed or blocked move, or hit-stun), it computes *exactly* how many frames you are stuck for, picks the strongest move whose startup plus approach fits inside that window, and fires it on the right tick. On a hit it cancels along the highest-damage route and spends meter on a super when it will connect.
3. **Read and counter.** In neutral it asks the model for a probability distribution over what you will do next (given range and your last attack), then picks the response with the best expected payoff from a hand-authored table. Examples: your habit is jumping, so it anti-airs; you block standing, so it goes low; you block crouching, so it uses an overhead; you hold block, so it throws; you walk in at range, so it pokes; you spam projectiles, so it rushes. A small exploration chance keeps it from being predictable; the chance shrinks as it learns.
4. **Space.** Otherwise it walks to the range where its best poke hits and yours doesn't.

It perfectly executes the plan it chooses, but the *plan* can be wrong. When it reads you wrong it takes the hit. That is why it can be hurt, which is what makes the fight feel real.

## 4. How it grows with the journey

* **Learning level (0 to 100)** rises with how much it has observed. It controls the *reaction delay* (12 ticks at the start, down to 5 at the end), how often it exploits the model instead of exploring, and how sharp its spacing is.
* **Techniques absorbed from beaten duelists.** Each duelist you win against gives it something: **[story hook: why does it take their power?]**

| Beat | It gains |
|---|---|
| Grobb | Rock-throw projectile; more health |
| Silke | Ground-dust projectile; faster walk and dash |
| Mosshide | Hits harder; much more health |
| The Champion | Starts the fight with a full super meter; faster reactions |
| Avery The Cat | Armoured rush |

* **Losing teaches it too.** Every duel, won or lost, goes into the model.

## 5. The guaranteed loss

Two rails, both authored, either of which ends the fight:

1. **It cannot be knocked out.** Its health clamps at 1. The moment it would drop below that, the finisher plays.
2. **The clock.** After 75 seconds of fighting, the finisher plays regardless.
3. **It never simply knocks you out.** If its ordinary attacks would take your last health, you are left at 1 and the finisher plays instead. (Added after the first hardware playtest: the player was beaten in 10 seconds and never saw the finisher.)

The **finisher** is a short cinematic: the screen darkens, the Paragon gathers itself, one slash crosses the whole screen, and the player is struck down (unblockable, no input possible). It is the same rule for everyone, every time. **[story hook: this is where the Paragon speaks, or does not.]**

So the best possible player outcome is to take it to 1 HP and watch the finisher play. That is the "extremely difficult but guaranteed death" the demo needs.

## 6. After the loss (demo flow)

1. The finisher plays and the player is struck down.
2. An **end-of-demo screen** lists what the Paragon learned about this player: the six most distinctive habits, in plain words (for example, "YOU BLOCK HIGH 94% OF THE TIME. YOU BLOCK LOW 11%").
3. The player wakes at the last bench. The Paragon is still waiting, and now it has learned from this loss too. A rematch is harder than the first fight, but still cannot be won.

## 6b. What it looks like (placeholder art, real design)

All art in the demo is drawn by code and will be replaced by artists. The *design* below is what the art should keep.

* **Silhouette first.** Same height and build as Mikoshi, the player's hero, on purpose: it is what you could be. Everything else differs.
* **Head:** a smooth porcelain mask with no mouth or nose. One vertical gold slit for an eye, at the front. Three thin spines sweep back like a crown. A gold ring (a halo) hangs behind the head. Nothing else in the game has a halo, so it reads at any size.
* **Body:** the colour of the space between stars (deep indigo), with a cloak that trails to the floor. It is outlined in dim gold where everyone else has a black outline, so it stays readable on any background.
* **Weapon:** the longest blade in the game, thin and gold, held in the same grip as Mikoshi's needle-sword.
* **Moves:** Mikoshi's move list, with its own frame data (more reach, one tick quicker). The demo's duelists each give it one more technique when you beat them (section 4).
* **Motif: the eye.** Its slit eye, the great window behind it, and its duel pattern (a field of eyes) all say one thing: *it has been watching.*

## 6c. The Still Hall (the final area)

Past Avery The Cat's far door: a long, low, symmetrical stone hall, the only indoor room in the demo.

| Element | Why |
|---|---|
| A bench just inside the door | The rematch is a few steps away after each loss, so losing never costs a long walk back |
| Five pillars with gold capitals, candles at their feet | Rhythm and stillness. After the forest, nothing moves except candle flames and slow gold dust |
| Narrow pointed windows | They look out onto its pattern, the field of eyes, before the fight starts |
| One great almond-shaped window behind the place where it waits, with a gold slit in the middle | An eye, watching the door you came in by. You walk toward it the whole length of the room |
| A gold threshold set in the floor in front of it | The line you cross to start the fight |
| Dressed-stone floor (also in its arena) | The fight happens *here*, not in a generic arena |

**The duel itself** uses the eye pattern as its background. Its irises breathe outward (one palette group) while the dark tide around them flows separately (a second group), so the eyes never wash out. **[story hook: what the hall was before the Paragon took it.]**

## 6c-bis. Its voice (placeholder)

Before the fight it says two lines, typed out a letter at a time in a gold-trimmed box: how long it has watched ("I HAVE WATCHED ALL 16 OF YOUR DUELS", or on a rematch "AGAIN. THAT IS 3 TIMES NOW") and one of the habits it learned about you, read back in its own words. At the finisher it says one word: "ENOUGH." During the fight the bottom screen shows what it currently believes about you, refreshed every two seconds, so you can watch it change its mind. **[story hook: the real script replaces these lines; the hooks are in `begin_battle` and `draw_paragon_voice`.]**

## 6d. Encounter patterns (all duels)

Every duel opens with an EarthBound-style pattern of the duelist's own: three white flashes, then their pattern floods the screen in a wave of diamonds starting from where they stand (the Pokemon wipe), then breaks up from the middle to show the duel, which keeps the forest backdrop. The Paragon is the exception: its duel takes place inside its pattern, the field of eyes. The engine side is `ge_pattern`: a 64x64 picture of palette indices, shown with palette cycling, a per-row sideways ripple (optionally alternating rows, the classic EarthBound shimmer), a vertical ripple and a scroll. Each row is a straight copy from a cached tile, so it costs about the same as one opaque background layer on the 3DS.

| Duelist | Pattern |
|---|---|
| Grobb | earthy checkerboard, alternating-row shimmer |
| Silke | cold blue plasma, slow vertical swell |
| Mosshide | green rings flowing outward fast |
| The Champion | red and gold diamonds, scrolling |
| The Paragon | a field of gold eyes on a dark tide |

## 7. Tuning knobs

| Knob | Where | Effect |
|---|---|---|
| `react` ticks (12 to 5) | `GeParagon.react` | How late it sees what you do |
| Exploration rate | `GeParagon.explore` | How often it plays a non-optimal option |
| Max health, damage scale | its `GeFighterDef` | Length and pressure of the fight |
| Finisher clock | `GeFight.finisher_tick` | How long you can survive |
| Learning rate / decay | `GePlayerModel` | How fast it forms and changes opinions |
| Payoff table | `ge_paragon.c` | Its *style* (more throws, more zoning, ...) |

## 8. What a story needs from this

* A **name and a reason** for the watching. The model already counts things worth quoting.
* A **voice** at the intro, at the finisher, and on the end screen.
* A decision on the **ending**: the demo ends in the loss; a full game could use the same finisher as a *cutscene* when the story wants it, and make the fight winnable by flipping one flag (`undying`) at the right point.

## 9. Measured results

All numbers come from `tests/test_paragon.c` (run by `ctest`) and the end-to-end demo run; they are repeatable because everything is seeded.

| Question | Result |
|---|---|
| Is it strong? Paragon with an empty memory vs the built-in level-3 CPU, best of 3, 12 seeds | **12 / 12 won** (30 / 30 over a bigger sweep, at learn levels 0, 40 and 100) |
| Does learning help? Ten fights against a player who always throws the same medium punch: damage the Paragon takes | **49 with a blank memory, 35 after six earlier duels with that player** (28% less) |
| Can it be beaten? 24 matches with the demo's boss rules against a mix of the level-3 CPU and a habitual player, different seeds and learn levels | **24 / 24 taken by the Paragon**; its health never went below 1 |
| Is it deterministic? The same seed twice | identical length, result, damage and learned counters |
| End to end in the demo? Walk into the hall, lose, see the report, rematch | passes (`paragon_demo` in `ctest`); the rematch starts at a higher level (8 → 30 on a fresh profile, 68 → 90 → 100 with every technique) |
| Clean under AddressSanitizer + UBSan? | yes, all four test programs |

**Not measured yet:** how hard it *feels* to a human (only a person can say), and its frame rate on a real 3DS. The brain costs a few thousand instructions per tick, small next to the renderer, but the first play log from hardware will say for certain.

**Things found while building it** (worth knowing if you tune it):

* A walk, a pause and another walk is a *dash* by the engine's own input rules, and a dashing fighter cannot block. The brain now refuses to double-tap by accident (`odir` in `ge_paragon.c`). Before that fix (and the guarding below) it lost about 1 match in 10 to the level-3 CPU; after, none in 30.
* Reacting only after a human-length delay is not enough against a 3-tick poke. It also guards by default whenever it is inside the opponent's reach, and crouches to do it (lows and mids are common, the overhead is slow enough to see).
