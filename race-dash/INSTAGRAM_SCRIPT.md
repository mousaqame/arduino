# Instagram Reel - "Your racing game is already sending this"

Target length: **40 seconds**. A 20-second cut is at the bottom.

The teaching point is deliberately ONE idea: the game broadcasts its own
telemetry, so the hard part isn't sensors or wizardry - it's listening. Everything
else in the video serves that one idea.

---

## The 40-second script

| Time | Shot | On-screen text | Voiceover |
|---|---|---|---|
| 0:00-0:03 | **Hook.** Tight macro on the rev counter needle snapping up through the range. Game engine audio loud. | `This needle is being driven by Forza.` | "This isn't a prop. The game is driving it." |
| 0:03-0:09 | Pull back to the whole rig - both dials, the OLED, the LCD, the wheel in frame. | `Real gauges. Real telemetry.` | "Real rev counter, real speedo, two little screens - all running off a couple of Arduinos." |
| 0:09-0:15 | Screen-record the Forza settings menu. Cursor lands on **Data Out**, flips it ON, types the IP and port. | `1. The game already broadcasts it` | "Here's the part nobody tells you. Forza has a setting called Data Out. Turn it on, give it your PC's IP address - and the game starts firing a packet at your own network sixty times a second." |
| 0:15-0:21 | Terminal, raw bytes scrolling fast. Then freeze and highlight two slices with boxes. | `2. It's just bytes in known spots` | "In Horizon 5 that packet is 324 bytes. Speed sits at byte 256. Engine revs at byte 16. You're not decoding anything clever - you just reach in and take them." |
| 0:21-0:26 | USB cable plugging into the Uno. Cut to the servo horn twitching in time with revs. | `3. Send it to the Arduino` | "Push those two numbers down the USB cable, and the Arduino turns them into an angle. That's the whole trick." |
| 0:26-0:34 | **Payoff.** Split screen: game's on-screen speedo left, your real needle right. Hard acceleration, then a gear change - cut to the OLED flashing SHIFT. | `Game vs. real. Same instant.` | "Sixty updates a second means it moves when the car moves. No lag. Nothing faked." |
| 0:34-0:40 | Hands sliding the printed paper dial into place behind the needle. End on the full rig running. | `Wiring + code + printable dials -> link in bio` | "Whole build's open - wiring, code, and the dial faces to print. Go make one." |

---

## The 20-second cut

Same idea, less breathing room. Use when the 40s version tests slow.

| Time | Shot | On-screen text | Voiceover |
|---|---|---|---|
| 0:00-0:03 | Macro on needle snapping up. | `Forza is driving this needle.` | "This isn't a prop. The game is driving it." |
| 0:03-0:10 | Forza's Data Out setting flipping ON, then bytes scrolling. | `The game broadcasts its own telemetry` | "Forza has a setting called Data Out. Switch it on and it fires a packet at your network sixty times a second - speed, revs, gear, all in there." |
| 0:10-0:16 | USB into the Arduino, servo moving, split screen with the game. | `Read it -> send it -> needle moves` | "Read the bytes, send them to an Arduino, and it turns them into an angle. That's it." |
| 0:16-0:20 | Full rig running. | `Full build -> link in bio` | "Wiring and code are in the bio." |

---

## Caption

> Your racing game already knows how fast you're going - and it'll happily tell
> anyone who asks.
>
> Forza has a setting buried in the menus called **Data Out**. Turn it on, point
> it at your own PC, and it starts broadcasting a 324-byte packet sixty times a
> second: speed, engine revs, gear, throttle, brake.
>
> So I stopped trying to be clever and just listened. Read two numbers out of
> that packet, send them down a USB cable, and an Arduino swings a servo to the
> right angle. Paper dial behind the needle. Done.
>
> No sensors. No magic. The hard part was already solved by the game.
>
> Wiring, code and printable dial faces in the bio.

**Hashtags:** #arduino #simracing #forzahorizon5 #diyelectronics #maker
#simracingsetup #electronics #arduinoproject #racingsim #diyproject
#telemetry #makersgonnamake

---

## Notes for filming

- **Lead with motion.** The first frame must already be moving. A still dashboard
  gets scrolled past; a needle snapping through its range does not.
- **The Data Out menu is the money shot.** It is the moment the viewer learns
  something they can act on. Hold it long enough to actually read the setting.
- **Shoot the split screen last** and match the two clips on a single hard
  acceleration - that sync is the proof, and it only works if both halves are
  the same run.
- **Don't say "324 bytes" more than once.** One concrete number makes it feel
  real; three makes it feel like homework.
- **Accuracy caveat worth honouring:** byte 256 and byte 16 are the Horizon 5
  layout. Motorsport uses a different one. The script says "in Horizon 5" for
  exactly that reason - keep that phrase in.
- Rev the engine in-game rather than driving, so the needle sweeps its full range
  on cue instead of sitting mid-scale.
