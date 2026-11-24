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
sage below 12 g, bone above 12, vermilion above 18.

**Bottom** is a three-state navigation, driven by swiping up:

| state | height | what is shown |
|---|---|---|
| 0 | 44 px | handle only, two short bars, the universal "pull me" mark |
| 1 | 124 px | six page buttons |
| 2 | 300 px | 3 x 6 tile grid for future functions, currently empty |

States 1 and 2 collapse back to the handle after five seconds of inactivity, so
the gauges get the screen back without anyone doing anything.

Swiping left and right changes page. Swiping right **from the left edge**
returns to the main page, the way Android does it. Arrow buttons were dropped:
with six pages plus two arrows each control would have been 100 px, which is
11.7 mm, below the minimum above. Gestures do the same job without stealing
space.

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
