# Interface

Why it looks the way it does. Every number here comes from the physical panel
or from published guidance, not from preference.

---

## The panel is small, so measure it

4.3" diagonal at 800x480 means an active area of **93.7 x 56.2 mm**, so
**8.54 px/mm**, about 217 DPI.

```
  7 mm =  60 px      12 mm = 103 px
  9 mm =  77 px      14 mm = 120 px
 10 mm =  85 px      15 mm = 128 px
```

Designing in pixels without that conversion produces controls that look fine on
a monitor and cannot be hit in a moving vehicle.

## Touch targets

| Guidance | Source | What was built |
|---|---|---|
| at least 14 mm | driver workload research | navigation buttons 128 px = 15 mm |
| at least 15 mm for industrial controls | ISA-101, IEC 61131 | dialog buttons 120 px tall |
| 76 dp minimum | Android Auto | exceeded |
| 5-10 mm spacing | capacitive panel vendors | 4 px gap plus border |
| 20-25 mm with thin gloves | industrial practice | not met, worth considering |

The first version had a confirmation button about 8 mm across. It was reported
as "small, and you cannot tell whether you hit it". Both halves of that
complaint were real: too small, and no feedback.

The 14 mm rule held for the controls that were deliberately designed as
driving controls and for nothing else. Settings rows, brightness levels, list
rows, the profile switcher, the navigation handle and the keyboard were all
below it. That is now resolved by **two modes**, below, rather than by growing
everything: a control that has no business being pressed at 90 km/h does not
need to be 14 mm, it needs to be gone.

## Two modes, decided by road speed

The interface has two shapes and the vehicle picks between them. The question
it answers is not cosmetic: **can the person look at the screen, or are they
only going to reach for it?**

| | standing | driving |
|---|---|---|
| when | measured speed below the threshold | measured speed above it, **or no speed at all** |
| small controls | fine, aim with the eye | gone or grown |
| settings | all of them | brightness and bus mode only |
| bench tools | all three tiles | hidden, with the reason on the grid |
| marker | none | filled `DRIVE` tag in the status strip |

### Threshold and hysteresis

```
enter driving    >= 10 km/h    immediately
leave driving    <  5 km/h     sustained for 5 s
between 5 and 10               nothing is decided
```

Ten km/h is above walking pace, so pushing the van into a bay or rolling to a
parking slot does not take the settings away. The hysteresis is **doubled on
purpose**, because one gap is not enough:

- A 5 km/h gap between the two thresholds is three orders of magnitude wider
  than the resolution of `2003` (0.01 km/h), so nothing flutters at the edge.
- A gap alone still fails in a queue: creeping forward to 12 km/h and back to
  zero crosses both thresholds every few seconds. So dropping back also needs
  **five seconds continuously below 5 km/h**. A queue cycle never reaches it; a
  vehicle that has actually stopped reaches it once.

It is **deliberately asymmetric**: nothing at all in the dangerous direction,
five seconds in the safe one. Five seconds of small buttons on a moving vehicle
is a real cost; five extra seconds of large buttons on a stationary one is not.

The dwell is skipped in one case: when driving mode was an *assumption* and a
measurement arrives that contradicts it. The countdown exists against queues,
not against the truth, so plugging the cable into a parked van unlocks the
board at once.

### No speed is not zero speed

Only `DIAG_SHOW_LIVE` counts. A stale reading answers *how fast the vehicle
was going*, not *whether it is moving now*; a refused or never-read identifier
answers nothing. **With no live reading the interface assumes driving.**

Both choices cost something and this one costs less:

- Assuming *standing* is dangerous. It happens exactly when the cable is pulled
  or the ECU stops answering - which, on a moving vehicle, is the moment the
  person reaches for the board because it stopped showing anything. The
  interface would shrink under their hand.
- Assuming *driving* is merely annoying. That happens on a bench, where there
  is time to look.

The same state occurs **after every power-up**, before the first answer, and
permanently on a board with no vehicle attached. So that this does not brick a
bench board - no settings means no language, no network, not even a way to arm
the bus - the person may state that the vehicle is standing. It is behind the
same three-second hold as every other serious choice, it is offered **only**
when the speed is missing, it is dropped by the first reading above 10 km/h,
and it is never stored: after a restart the board knows nothing again and
starts on the safe side.

The tag says which it is. `DRIVE` is measured; `DRIVE?` is assumed. The
question mark is the same mark the interface already uses for an unverified
reading, and it carries the difference on its own - colour never does.

### What happens to each control that was too small

Measured from the pixels, not from the source. One rule for all of them would
be wrong: a fault list is read without touching it, while a settings row is an
action.

| control | was | driving |
|---|---|---|
| settings rows, 8 of them | 160 x 68 | **gone** - set once, on a bench |
| display off | 170 x 68 | **gone** - a dark screen says nothing; dimming covers glare and stays |
| service / resets | 170 x 68 | **gone** - irreversible |
| bus mode | 160 x 68 | **grows** to 228 x 120 |
| brightness | 160 x 68 | **grows** to 228 x 120 |
| brightness levels | 104 x 120 | **grow** to 140 x 120, live profile only |
| dialog back | 124 x 70 | **touch area grows** to 174 x 120, drawing unchanged |
| navigation handle | 800 x 44 | **touch area grows** to 800 x 120, drawing unchanged |
| fault rows | 350 x 48 | **stop being targets**, stay readable |
| profile switcher | 164 x 64 | **stops being a target**, name stays readable |
| DPF rail | 64 x 416 | **stops being a target**, stays an indicator |
| bench tiles | 129 x 120 | **gone** - they hold 260 x 100 and 128 x 100 controls inside |
| network rows, keyboard | 598 x 58, 60 x 60 | **unreachable** - the WiFi button is gone |

Two of these keep their drawing and grow only the touch area
(`lv_obj_set_ext_click_area`), which is the only place in the interface where
the two differ. Both are at a screen edge with nothing beside them, and both
would have cost real screen area if drawn at 120 px: a 120 px handle eats a
quarter of the page for a strip that shows nothing.

The bus mode is the one settings control that **must** stay, and not for
comfort. Without it nothing can be read, and because speed is what releases
driving mode, a board locked out of normal mode could never get back to
standing mode either. That is not a lock, it is a latch. Switching it is
already behind a hold confirmation.

Brightness stays because glare is a safety matter that happens at speed -
night, a tunnel, oncoming high beams - and a quarter of a second of a dazzled
driver is worse than any dialog.

### Locked says why

A greyed-out button says "not now" and not why, so people tap it again to find
out. The locked settings page carries a sentence instead, and the sentence
differs between the measured case and the assumed one, because the person can
do something about the second.


## Feedback must be obvious

Pressing any button swaps **the whole background** to the accent colour and
darkens the label. Subtle shading was tried and went unnoticed. On a screen
being glanced at, feedback has to be visible in peripheral vision.

## Layout

```
+-------------------------------------------------+------+
| TALENTO            TEMPERATURE 87          DEMO  | DPF  |   64 px
+-------------------------------------------------+      |
|                                                  |  ##  |
|                 active page                      |  ##  |  variable
|                                                  |  ..  |
|                                                  | 8.8  |
+-------------------------------------------------+------+
|                      =====                       |          44 px handle
+--------------------------------------------------------+
```

**Top strip** holds what must never be hidden. The automotive guidance is that
information should be readable in one or two glances, under two seconds - that
only works if it does not move or need a tap.

**Right rail** shows particulate filter loading on every page, colour coded:
sage below 12 g, bone above 12, vermilion above 18. A regeneration tints the
whole rail: dim red once it has been requested, blinking while it runs, with
the fill draining from the top. Progress reported by the engine control unit
moves the fill smoothly; without it the fill steps down in fifths, because a
smooth sweep would read as a measurement and the estimate is not one.

**Bottom** is a three-state navigation:

| state | height | what is shown |
|---|---|---|
| 0 | 44 px | handle only, two short bars, the universal "pull me" mark |
| 1 | 152 px | six page buttons, 129 x 140 px each |
| 2 | 416 px | the same bar, still at the top, with two rows of tiles below it |

The page buttons were 110 px tall at first, which is 12.9 mm - below the
14 mm an in-vehicle control needs. They are 140 px now, 16.4 mm, because
they are the control pressed most often while moving. Six in one row still
leaves 129 px each, enough for the full captions.

States 1 and 2 are **one panel** that changes height, not two. That is why
the page buttons stay visible at the top of the grid instead of sliding away.

It is an **overlay**. The first version resized every page to whatever the
navigation left over, so pulling it up shrank the gauges and the content jumped
about. Controls must not redraw content: pages are always 416 px tall and the
navigation slides over them. The pages lose their bottom 152 px while the bar
is out, which is why nothing important lives below 264 px - and the bar drops
back to the handle after five seconds of inactivity anyway.

The panel is **dragged like an Android bottom sheet**: up, hold, back down,
without lifting the finger. It only changes its y position, never its size, so
nothing is laid out again and only the strip it moved over is redrawn. On
release it settles into the nearest state measured in pixels - the three states
are not evenly spaced - unless the finger was moving faster than about
400 px/s, which steps one state in that direction the way a flick should.

Swiping left and right changes page. Swiping right **from the left edge**
returns to the main page, the way Android does it. Arrow buttons were dropped:
with six pages plus two arrows each control would have been 100 px, which is
11.7 mm, below the minimum above. Gestures do the same job without stealing
space.

## A drag is not a tap

LVGL sends `LV_EVENT_CLICKED` to the object the press **started** on, wherever
the finger ends up. Navigation buttons are 110 px tall, so dragging the bar up
from one of them also switched page. Every button therefore records the press
point and discards the click when the finger travelled more than **30 px**,
which is 3.5 mm: above the wobble of a stationary finger (LVGL itself treats up
to 10 px as standing still) and below the 50 px a gesture needs, so one stroke
never does two things.

## Confirmation has to be visible, not felt

Destructive actions are confirmed by holding a button for three seconds. The
countdown used to be printed on that button, under the finger. It now takes the
middle of the screen: a draining ring with the remaining seconds inside it, and
every elapsed second flashes the whole overlay yellow. The board has no buzzer
and no vibration motor, so feedback can only be seen, and it has to be visible
in peripheral vision. When the time is up the ring turns sage and shows a tick
for 700 ms before the action runs - otherwise the dialog would vanish before
anyone noticed they had held long enough.

A confirmation also has to say **what exactly it will destroy**, with the real
numbers rather than a category. "Reset settings" is a blind promise; "deletes
all 2 saved networks together with their passwords: domov, UPC1234567" is a
decision. The counts and names are read when the dialog opens, so they cannot
be stale, and they are already on the button that opens it.

## Profiles

Three stored profiles, **BENCH / DAY / NIGHT**. They hold the two settings that
really differ between a workbench, daylight and a dark cab: **backlight
brightness** and **blank after**. Units and language are **shared** - they
belong to the person reading the screen, not to the situation, and switching a
profile must never make the interface unreadable.

Names, not 1 / 2 / 3. A number has to be remembered; the name is read at a
glance, which is exactly what is hard at night. The name only labels *when* a
profile is meant to be used - what is in it is the owner's business.

The switch sits in the **top strip**, next to the profile name, so it is one
tap from every page; the settings page has the same control. Three marks beside
the name show how many profiles there are and which one is live, so a glance
confirms the switch without reading.

Profiles replaced an earlier day/night brightness pair. Keeping both would have
meant two mechanisms for one thing - six stored levels and two switches that
could disagree.

## Gauges have to flow

Sampling and drawing are deliberately separated:

```
sampling     20 Hz     (later: real values over CAN)
drawing      50 Hz     displayed value eases toward the sampled one
```

The shown value approaches the measured one by a fraction each frame, so the
needle moves continuously instead of stepping. Even at five samples per second
it looks smooth. This matters more than raw sample rate.

## Colour

```
ink      #0B1016   ground, dark enough for night driving
panel    #0E151D   surfaces
rule     #1B2A38   hairlines
demand   #6FA8D0   what the ECU asks for
actual   #F0DFB8   what the engine delivers
deficit  #C8432B   fault
sage     #68937A   in tolerance
label    #65798B   captions
bright   #DCE7F0   primary text
```

Cool blue is **always** the setpoint and warm bone is **always** the measured
value, on every screen and in the laptop-side tooling. The pairing carries
meaning, so it is never used decoratively.

## Shape

Panels are square with a one pixel border - an instrument panel, not floating
cards with shadows. Buttons get a 6 px radius. The inconsistency is deliberate:
if everything were square you could not tell a control from a caption.

## Fonts

Built-in Montserrat only, which has **no Czech diacritics**. One accented
character in a label produced `glyph dsc. not found for U+F3` thirty times a
second. All strings are plain ASCII until a font with the right code points is
generated.

Largest built-in size is 48 px, cap height about 4.1 mm. Guidance says avoid
text below 6.5 mm for in-vehicle use, which needs roughly a 76 px font. Open
item in the roadmap.

## The one ornament

The chart recorder on the pressure page: two traces, setpoint and measured,
with the gap between them as the subject. That is the single place where the
interface is allowed to be expressive, because the gap *is* the diagnosis.
Everything else stays quiet.
