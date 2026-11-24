# Splittable pages

A proposal for replacing the six fixed page layouts with a tree of fields that
the owner splits and fills on the board, the way a tiling window manager splits
a screen. This is a design document. No code exists yet.

Every dimension below is computed from the panel (8.54 px/mm, see
[`ui.md`](ui.md)) and from the glyph tables in `src/font_ui_*.c`. Where a number
could not be computed it is marked **estimate** or **needs measurement**.

---

## 1. The surface

| | px | mm |
|---|---|---|
| screen | 800 x 480 | 93.7 x 56.2 |
| top strip (`TOP_H`) | 800 x 64 | 93.7 x 7.5 |
| DPF rail (`RAIL_W`) | 64 x 416 | 7.5 x 48.7 |
| **page content** | **736 x 416** | **86.2 x 48.7** |

The navigation overlay is the other constraint. `src/ui.c` has
`NAV_BAR_H = NAV_PAD + NAV_BTN_H + NAV_PAD = 6 + 140 + 6 = 152` and
`NAV_FULL_H = PAGE_H = 416`. State 1 therefore covers the bottom **152 px** of
the page and state 2 covers all of it.

**Always-clear band: the top 416 - 152 = 264 px** of the content area, 30.9 mm.
(`ui.md` still says 124 px for state 1; that is stale, the code says 152.)

Chrome used in every calculation below, matching what the existing pages already
do:

```
field border      1 px each side        (ui.md: "square with a one pixel border")
field inner pad   6 px left/right, 4 px top/bottom
gutter between siblings   4 px          (ui.md: "4 px gap plus border")

horizontal chrome 2*1 + 2*6 = 14 px
vertical chrome   2*1 + 2*4 = 10 px
```

---

## 2. Rules of division

A page is a tree. A node is either a **split** (2 or 3 children, by column or by
row) or a **leaf** that holds one widget. Splits are always equal; there is no
drag-to-resize handle, because such a handle would need a 120 px touch target of
its own and would sit exactly on the line the finger is most likely to brush.

Equal splits also mean every reachable field size can be enumerated in advance,
which is what makes the on-board editor possible: a widget declares the sizes it
supports and the editor greys out the rest.

### 2.1 What sets the floor

The floor is legibility, not touch. Measured digit metrics from the generated
fonts (`box_h` of the digit glyphs, `adv_w` of the widest digit):

| font | digit cap height | | advance | line height |
|---|---|---|---|---|
| | px | mm | px | px |
| 14 | 10 | 1.17 | 9.38 | 18 |
| 16 | 12 | 1.41 | 10.69 | 20 |
| 20 | 14 | 1.64 | 13.38 | 24 |
| 24 | 17 | 1.99 | 16.06 | 29 |
| 28 | 20 | 2.34 | 18.75 | 34 |
| 32 | 22 | 2.58 | 21.44 | 38 |
| 36 | 25 | 2.93 | 24.06 | 42 |
| 40 | 29 | 3.40 | 26.75 | 49 |
| 48 | 34 | 3.98 | 32.13 | 58 |

The in-vehicle criterion is angular, not metric: character height of at least
**20 minutes of arc**. What each font subtends, and how close the panel would
have to be to reach 20':

| font | arcmin at 700 mm | at 800 mm | at 900 mm | distance for 20' |
|---|---|---|---|---|
| 24 | 9.8 | 8.6 | 7.6 | 342 mm |
| 32 | 12.7 | 11.1 | 9.8 | 443 mm |
| 40 | 16.7 | 14.6 | 13.0 | 584 mm |
| 48 | 19.6 | 17.1 | 15.2 | **684 mm** |

**Only font 48 comes close, and only if the panel sits within about 684 mm of
the driver's eyes.** The eye-to-panel distance on this van has not been
measured - it is the single unmeasured input to everything in this document.
(`ui.md` quotes a 6.5 mm guideline, which is 20' at 1117 mm; a 4.3" panel does
not normally sit that far away.)

Two consequences:

1. **The primary number in any field read while driving is font 48.** Nothing
   smaller qualifies at any plausible mounting distance.
2. **Captions cannot be read while driving at all.** A font 20 caption is 8.0'
   at 700 mm. Captions exist to confirm at standstill; while moving, a field is
   identified by its position and its colour, not by its text. That is a
   constraint on the whole design, not a defect of the small fields.

So the smallest field worth having is the one that holds a font 48 number of at
least 3 digits plus one caption line:

```
MIN_W = 14 (chrome) + 3 * 32.13 (widest digits, font 48) = 110.4 -> 111 px
MIN_H = 10 (chrome) + 58 (font 48 line) + 4 (gap) + 24 (font 20 caption) = 96 px
```

### 2.2 Reachable sizes

Applying equal splits with a 4 px gutter and refusing anything below
111 x 96 px:

**Widths** (start 736):

| from | halve | third |
|---|---|---|
| 736 | 366 | 242 |
| 366 | 181 | 119 |
| 242 | 119 | 78 - refused |
| 181 | 88 - refused | refused |
| 119 | 57 - refused | refused |

**Heights** (start 416):

| from | halve | third |
|---|---|---|
| 416 | 206 | 136 |
| 206 | 101 | 66 - refused |
| 136 | 66 - refused | refused |
| 101 | refused | refused |

```
reachable widths   736  366  242  181  119     (86.2  42.9  28.3  21.2  13.9 mm)
reachable heights  416  206  136  101          (48.7  24.1  15.9  11.8 mm)
```

**At most two splits per axis, so at most four splits on any path from the page
to a field.** Twenty distinct field sizes exist.

**Maximum 6 columns x 4 rows = 24 fields.** Six columns is reached two ways
(736 /3 /2, or 736 /2 /3); eight columns would need 88 px fields, which cannot
hold three digits. Four rows is reached only as 416 /2 /2; six rows would need
66 px, which cannot hold a single font 48 line.

**Floor field: 119 x 101 px = 13.9 x 11.8 mm.** It holds a 3-digit font 48
number and a font 20 caption, and nothing else.

### 2.3 Halves and thirds, not halves only

Allow both. Thirds are not decoration, they reach sizes halving cannot:

- Halving alone gives widths 736 / 366 / 181 and heights 416 / 206 / 101. The
  next halving step in either axis (88 px, 66 px) is below the floor, so pure
  halving caps out at **4 columns x 4 rows**, and 4 columns of 181 px is the
  widest useful arrangement it can build.
- Thirds add width 242 and height 136, and unlock the 6-column lattice. The
  119 x 101 floor field only exists because thirds exist.
- Height thirds divide exactly: 416 - 8 = 408 = 3 x 136, no remainder. Width
  thirds do not: 736 - 8 = 728 = 2 x 243 + 242, so one child is 1 px narrower.
  Give the extra pixel to the leftmost children and never to a child that is
  itself split again, so the error cannot accumulate.
- A 3-row split is also the natural shape for a demanded / actual / deviation
  group, which is the thing this van most needs on screen.

Nothing beyond thirds. Quarters are two halvings, sixths are a third and a
halving; both are already reachable. Fifths and sevenths add sizes no widget
asks for and multiply the editor's button count for nothing.

### 2.4 Where the navigation bar lands

With the always-clear band at 264 px:

| rows | field height | fully clear | partly clear | covered |
|---|---|---|---|---|
| 1 | 416 | - | the one field (264 of 416) | - |
| 2 | 206 | row 1 | row 2 (54 of 206, 26 %) | - |
| 3 | 136 | row 1 | row 2 (124 of 136, 91 %) | row 3 |
| 4 | 101 | rows 1, 2 | row 3 (54 of 101, 53 %) | row 4 |

**Rule: the bottom row of any layout is a parked row.** Counters, odometer,
standstill readings. The editor draws a hairline at y = 264 so the owner sees
what the navigation will eat. The bar drops back to the handle after 5 s
(`NAV_IDLE_MS`), so this is a recurring nuisance rather than a permanent loss -
but a value that is hidden exactly when the owner has just touched the screen is
the wrong value to hide.

A tiled page must also stay transparent to the existing gestures: fields are not
clickable and not scrollable in normal use, so left/right swipe still changes
page and the left-edge swipe still goes home.

---

## 3. Widget catalogue

### 3.1 Digit capacity by field width

Worst-case glyph advance (the widest digit), 14 px of chrome removed:

| field width | inner | font 48 | font 40 | font 32 | font 24 | font 20 |
|---|---|---|---|---|---|---|
| 736 | 722 | 22 | 26 | 33 | 44 | 53 |
| 366 | 352 | 10 | 13 | 16 | 21 | 26 |
| 242 | 228 | 7 | 8 | 10 | 14 | 17 |
| 181 | 167 | 5 | 6 | 7 | 10 | 12 |
| 119 | 105 | 3 | 3 | 4 | 6 | 7 |

Useful exact widths at font 48: `888` = 96.4 px, `2759` = 128.5 px,
`14.2` = 107.2 px (the point is 10.88 px), `-416` = 146.9 px (the minus is
18.38 px), `888888` = 192.8 px.

Two consequences worth stating now, because they decide where things can go:

- **A one-decimal value such as `14.2` V does not fit a 119 px field at font 48**
  (107.2 > 105). It needs 181 px, or it drops to font 40 (89.3 px) and stops
  being a driving readout.
- **A signed 4-digit deviation such as `-416` needs 181 px** at font 48.

### 3.2 Height budget

Inner height after 10 px of chrome, and what is left after one font 48 line:

| field height | inner | after a font 48 line (58) | fits |
|---|---|---|---|
| 416 | 406 | 348 | dial, chart, 4 more lines |
| 206 | 196 | 138 | value + bar + caption + sub-line |
| 136 | 126 | 68 | value + bar + caption |
| 101 | 91 | 33 | value + caption only |

### 3.3 The widgets

#### W1 - Dial

Needle plus tick scale plus a centre number. Diameter `D = min(w, h) - 14`.

| field | D | D in mm | centre box (0.55 D) | centre digits |
|---|---|---|---|---|
| 366 x 416 | 352 | 41.2 | 194 px | 6 at font 48 |
| 736 x 416 | 402 (capped by height) | 47.1 | 221 px | 6 at font 48 |
| 366 x 206 | 192 | 22.5 | 106 px | 3 at font 48, 4 at font 36 |
| 242 x 206 | 192 | 22.5 | 106 px | 3 at font 48, 4 at font 36 |

**Minimum: 242 x 206.** Below that the scale stops working, not the number. At
D = 192 a 270 degree arc is 448 px long; ten major ticks sit 44.8 px (5.2 mm)
apart and one minor between them 22.4 px (2.6 mm). At D = 122 (a 242 x 136
field) the same scale puts minors 1.7 mm apart, which is a grey smear. If the
scale is unreadable the dial is a slow way of drawing a number - use W3 instead.

At the minimum size the duties split: **the needle is the glance, the number is
the detail.** A 4-digit centre at font 36 is 2.93 mm and is read at standstill;
that is acceptable precisely because the needle already answered the glance.

Overflow: a dial never overflows, it clamps at the end of its arc and the needle
turns vermilion. The scale multiplier (x1000 for engine speed) is painted on the
dial face once and never changes.

#### W2 - Trend

Two traces against time, setpoint in `demand` blue and measured in `actual`
bone, with the area between them filled. This is the chart recorder `ui.md`
already calls the one ornament, generalised.

One sample per pixel column: 352 samples at 366 px, 722 at 736 px. At the 3.5 Hz
used in the existing logging that is 100 s and 206 s of history.

**Minimum: 366 x 136.** At 126 px of inner height a 0-3000 mbar scale is
23.8 mbar/px, so the 416 mbar shortfall recorded in the P2263 case is a 17 px
gap - visible. At 101 px height the axis labels (font 14, 18 px line) take a
third of the field, so the limit is labelling, not resolution.

Digits: a chart carries no glance digits. One current-value readout in a corner,
font 32, 4 digits (85.8 px). Reading an exact number off a trace is not the job.

#### W3 - Value

Number, unit, caption. The workhorse.

| field | digits at font 48 | layout |
|---|---|---|
| 119 x 101 | 3 | value, caption. Unit goes into the caption (`COOLANT C`) |
| 181 x 101 | 5 | value + inline unit, caption |
| 242 x 136 | 7 | value + inline unit, caption, one sub-line |
| 366 x 206 | 10 | value, unit, caption, two sub-lines |

**Minimum: 119 x 101** (the floor field). Below the floor there is no field.

**Counter variant.** Six-digit totals such as the odometer need 192.8 px at
font 48, so a 242 px field, or font 32 at 181 px (128.6 px). Counters are never
glance items, so they take font 32 and belong in the parked bottom row.

#### W4 - Scaled bar

Horizontal fill with tick marks, end labels and an optional shaded normal band.
For quantities where the position inside a known range is the reading and the
exact number is secondary - coolant temperature is the example.

Bar 24 px tall, scale labels font 14 (3 digits = 28.1 px each).

| field | scale labels | value font |
|---|---|---|
| 119 x 101 | none, end markers only | 48, 3 digits |
| 181 x 136 | 3 (min, mid, max) | 48, 5 digits |
| 242 x 136 | 5 | 48, 7 digits |
| 366 x 206 | 5 plus a band legend | 48 |

**Minimum with a font 48 number: 181 x 136** (58 + 4 + 24 bar + 4 + 24 caption =
114 <= 126). In a 101 px field the bar forces the number down to font 32, which
is a standstill readout; if the field is that small, drop the scale and use W5
or W3.

#### W5 - Percent

0 to 100 with the fill behind the number instead of beside it. Separate from W4
because the full scale is known without being drawn, so the fill reads as a
level in peripheral vision with no tick marks at all.

**Minimum: 119 x 101.** `100` is 96.4 px at font 48 and fits the 105 px inner
width; the `%` glyph is another 40.4 px and does not, so at the floor size the
sign lives in the caption. At 181 px the `%` goes inline (136.8 px total).

Digits: always 3, never decimals. A percentage with a decimal is a false
precision - `202E` accelerator position has a 0.125 % step and is still a pedal.

Overflow: the pedal exceeds 100 on kickdown. Let the number pass 100 and let the
fill clamp. Three digits cover it to 999.

#### W6 - Deviation pair

**The new type.** For every quantity where the ECU both asks and measures. These
pairs found a real regulation deviation before any lamp did, and that is the
reason this widget exists rather than two W3 fields side by side.

Why not two numbers: two 4-digit font 48 numbers are 257 px of digits before
chrome, so a 271 px field minimum - and the driver still has to subtract them.
The gap is the diagnosis, so the widget computes it.

Layout at 242 x 136 (inner 228 x 126):

```
+--------------------------------+
| 2343 mbar                      |   font 48 actual, bone      58 px
|        |                       |   deviation axis            16 px
|   -----+-------                |   demand = the blue tick at centre
| -416                           |   font 32 signed gap        38 px
+--------------------------------+   total 120 of 126
```

- The axis spans three times the tolerance band, centred on demand. The bone
  marker sits at `actual - demand` scaled onto it. Outside the band the marker
  and the gap number turn `deficit` vermilion.
- Colours are the ones `ui.md` already fixes: `demand` blue is always the
  setpoint, `actual` bone is always the measurement. This widget is where that
  pairing earns its keep.
- Demand is printed as a font 20 number only when width allows (242 px and up);
  otherwise it is only the tick position, which is all a glance needs.

**Minimum: 242 x 136.** Actual (128.5 px) plus unit (about 53 px at font 20)
is 188 px inside 228.

**Gap-only mode** for smaller fields: the pair reduced to its conclusion, the
signed deviation alone at font 48 with the quantity name as caption. Needs
181 x 101 (`-416` is 146.9 px, inner 167). At 119 px it would have to drop to
font 40 (95.6 px) - allow it, but it is the last useful size.

**Gating is part of the widget, not of the page.** A deviation means nothing
when the ECU is not regulating for that quantity. The vehicle document proves
this: `P2263-22` was set 9 km into a running regeneration, when the air path was
being driven for exhaust temperature and not for boost, and the engine control
unit does not inhibit the boost diagnostic in that state. The widget must do
what the ECU does not:

- below a configured engine speed and pedal, or while a regeneration is
  running, draw the gap in `label` grey and suppress the vermilion;
- spool-up is not a fault either - the logged steady-state deviation fell from
  708 mbar median at 1500-1800 rpm to 22 mbar at 2500-3000, so the tolerance
  band must be a function of engine speed, not a constant. The band table is
  **estimate** until it is fitted from logged data.

#### W7 - Difference of two measurements

Like W6 in shape but without a setpoint: two measured values and their
difference, where no "should" exists. The temperature rise across the
particulate filter (`2442` in, `2441` out) is the case.

Difference at font 48, 3 digits; the two inputs at font 20 on one sub-line
(two 3-digit groups = 80.3 px plus separators).

**Minimum: 181 x 136.** In 101 px there is no room for the sub-line, and a
difference without its inputs is not interpretable.

Kept separate from W6 on purpose: colouring a difference as a deviation would
claim a target that does not exist.

#### W8 - State lamp

One word and a background. `ON` / `OFF` / `---`. Font 48 (`OFF` = 96.4 px fits
the 105 px floor inner width).

**Minimum: 119 x 101**, same as everything else.

This widget is the argument for the floor. If 88 x 66 fields were allowed, the
only thing that could live in one is a coloured square with no word at all - an
instrument whose meaning has to be memorised. The floor exists so that every
field can still say what it is.

#### W9 - Faults

Two sizes, one widget.

- **Count**, at the floor field: one digit plus caption, vermilion when above
  zero. Counts only what survives the `0x2F` status filter - `19 02 FF` returns
  well over a hundred entries and almost all are `0x50`, which is "never ran
  since clear", not a fault.
- **List**, at 736 x 206 and up: code plus name per row, font 24, 29 px line, so
  6 rows in 196 px of inner height (5 with a header). `P2263-22` is 8 characters
  = at most 128.5 px at font 24, leaving 594 px for the name. The list needs the
  full width; it may not be placed in a split column.

### 3.4 When to divide and print a multiplier

The owner's own suggestion, taken from the tachometer. The rule:

**Allowed** only where the multiplier is (a) already the instrument convention
for that quantity and (b) static - painted once, part of the label, never
recomputed. On this van that is exactly one case: **engine speed, x1000 on the
dial face**, which is what every tachometer in Europe does.

**Forbidden** for pressure, temperature, voltage, air mass, percentage,
distance. Nobody reads `2.8 x1000 mbar`. If the field cannot hold the digits,
the field is the wrong size for the signal and the editor must say so.

**Forbidden entirely: automatic range switching.** A number whose multiplier
changes with its own value silently changes meaning between two glances, which
is worse than a number that is simply too long to show.

**Prefer a unit change** where a conventional one exists - but only if the whole
system changes with it. Boost is 2759 mbar in the ECU description file, in the
vehicle document and in every log; showing 2.76 bar on screen would break the
link between what the owner sees and what the log says. Keep mbar, and accept
that boost needs 4 digits and therefore a 181 px field.

**When a value does not fit, show dashes, not a wrong number.** Same width, same
caption. Out of range, sensor at a rail, or a raw `0xFFFF` all produce `---`.

**Stale is a separate state from invalid.** When the poller has had no answer
for the field's signal within its timeout, keep the last value, dim it to
`label` grey and put a dot before it. A frozen number that still looks live is
the one failure mode that makes the whole screen untrustworthy.

---

## 4. What belongs on a tiled page

From the verified list only. Nothing from `ideas-monitoring.md` can be placed
until it has been probed on the vehicle - every one of those identifiers is
**estimate** by that document's own rule, and five identifiers from the same
description file already answer `7F 22 31`.

| signal | DID | range | digits | widget | why that one |
|---|---|---|---|---|---|
| `rpm` | `2002` | 0-5000 | 4 | W1 dial | a rate; the needle angle is the reading, the number is confirmation |
| `speed` | `2003` | 0-160 | 3 | W3 value | the exact figure is the point - 0.01 km/h resolution is what makes it worth reading at all |
| `coolant` | `2001` | -40..130 | 3 | W4 bar with band | only the position against ~90 C and against the limit matters; a number alone makes you remember both thresholds |
| `boost` / `boost_sp` | `2401` / `2402` | 0-3000 mbar | 4 | **W6 pair** | this pair found the real regulation deviation; needs 181 px minimum for the signed gap |
| `rail` / `rail_sp` | `2801` / `2802` | 0-1800 bar | 4 | **W6 pair** | same structure, and the ECU has a rail test routine (`31 01 02 09`) to confirm what the pair suggests |
| `maf` / `maf_sp` | `2412` / `2413` | 0-2000 mg | 4 | **W6 pair** | the air-path third of the same story; the difference is already stored and unused |
| `pedal` | `202E` | 0-110 % | 3 | W5 percent | the driver's own input, the reference for every other reading on the page |
| `throttle` / `egr` / `vnt` | `2404` / `2407` / `2527` | 0-100 % | 3 | W5 percent | positions, not rates. Promote to W6 when `2405` and `2408` setpoints are probed - **estimate** |
| `torque` | `2004` | -400..400 | 3 + sign | W3 value | an ECU model output, not a measurement; useful next to the pairs, not on its own |
| `vbat` | `2005` | 0-15.9 V | `14.2` | W3 value | needs 181 px at font 48 (107.2 px) - cannot go in a floor field |
| `csf_up` / `csf_dn` | `2442` / `2441` | 0-700 C | 3 | **W7 difference** | the rise across the filter is the number; the two inputs alone are two numbers you then have to subtract |
| `rgn_dur` | `2487` | 0-1800 s | mm:ss | W8 lamp + W3 | a regeneration is a state first and a duration second |
| faults | `19 02 01` | 0-99 | 2 | W9 count | the short query, already filtered to what is failing now |
| `odometer` | `2006` | 6 digits | 6 | W3 counter | font 32, parked bottom row, never a glance item |

**Deliberately not placed:**

- `soot` `242C` must not become a plain number. The vehicle document shows it
  swinging between 7.7 and 15.7 g in one hour with no regeneration, correlating
  with pre-filter temperature at r = +0.84. A field showing it live would report
  a fault every time the exhaust warms up. If it is shown at all it must be a
  dedicated widget that holds the last value read below 150 C as the reference
  and shows the live value as secondary - **proposed, not verified**, and it
  needs the binned reference table from that document before it can be built.
  The equivalent load `24BA` is already on the right rail and that is enough.
- Adaptation tables (`2508`, `2509`, `290C`-`290F`), oil service (`2110`,
  `2299`), start counters, airbag impedances: one-off reads during a check. A
  field that updates once per ignition cycle is a page, not a tile.
- Everything in `ideas-monitoring.md`. Oil pressure `25B9` is the strongest
  candidate for a W4 bar once probed - **estimate**.

---

## 5. Editing on the board

### 5.1 A separate mode, and locked above walking pace

**Edit mode is separate from normal use.** Three reasons, all of them already
demonstrated in this codebase:

- `ui.md` records that LVGL delivers `LV_EVENT_CLICKED` to the object the press
  started on, which already made a navigation drag change page. A long-press to
  split in normal mode would fire during a gesture over a moving vehicle.
- The page surface is where left/right swipes change page. Making fields
  interactive in normal use puts the two in competition.
- Destroying a layout is not reversible by the same gesture that made it.

**The editor refuses to open above 5 km/h** (`2003`, which is already polled),
and closes itself if the vehicle starts moving, keeping the last committed
layout. The reason is stated on screen rather than left as a dead button.

Entry point: the navigation tile grid at state 2, which currently holds twelve
empty tiles of 129 x 120 px (`NAV_TILE_H` = (416 - 152 - 20 - 4) / 2 = 120, and
a static assert in `ui.c` already keeps that at or above the 120 px touch
minimum).

### 5.2 Selecting a field that is smaller than a touch target

The floor field is 119 x 101 px = 13.9 x 11.8 mm, under the 14 mm minimum in the
height axis. The resolution is to separate selecting from acting:

**Selecting is not acting.** A tap anywhere inside a field selects the smallest
leaf under the finger. Selection destroys nothing, so a miss costs one more tap.
The selected field gets a 2 px `demand` blue outline and its identity is printed
in the top strip: `FIELD 7  119 x 101  BOOST`. That is feedback in the same
style `ui.md` demands - visible, not subtle.

**Acting always obeys 120 px.** On entering edit mode the navigation panel is
replaced by an action bar of full-height buttons, 736 px wide, 3 or 4 across:

```
+--------+--------+--------+--------+
| < PREV | SPLIT  | WIDGET | MERGE  |   181 x 120 px each
+--------+--------+--------+--------+
```

`PREV` / `NEXT` step through the leaves in pre-order, so a field that is hard to
hit can always be reached without hitting it. That is the answer to a field
below the touch minimum: it is selectable by finger as a convenience and by two
oversized arrows as a guarantee.

### 5.3 The four operations

**SPLIT** - opens four buttons, 366 x 120 px, two by two:

```
2 COLUMNS   3 COLUMNS
2 ROWS      3 ROWS
```

Any option whose result would fall below 111 x 96 px is drawn in `label` grey
and does not respond; the greyed button still shows the size it would have
produced, so the refusal explains itself. The widget already in the field moves
to the first child; the others come up empty.

**WIDGET** - a paged list of only the widgets that fit the selected field's
size, 2 columns x 3 rows of 366 x 120 px buttons, 6 per page. Each entry shows
the name and what it can hold here, for example `VALUE - 5 digits` or
`DIAL - too small` would simply not be listed. Picking a widget leads to a
signal picker in the same grid, grouped by module and filtered to the signals
whose digit need fits. This is where the editor enforces section 3.4: boost
cannot be dropped into a 119 px field, and the list says so by omission.

**MERGE** - replaces the selected field and its siblings with their parent. The
surviving widget is the one in the selected field. If more than one sibling
holds a widget, use the existing three-second hold confirmation with the real
names in it, exactly as `ui.md` requires: *"merging discards 2 widgets: BOOST,
RAIL"*. One widget or none, no confirmation - splitting again undoes it.

**EMPTY** fields are legal and are drawn as the page background with a hairline
outline. A layout under construction must not be forced to be complete.

### 5.4 Committing

Changes are applied live on screen but written to NVS only on leaving edit mode
through `DONE`. `CANCEL` restores the tree as it was on entry. Two reasons:
flash wear from a per-tap write, and a half-finished layout surviving a reset.

---

## 6. Cost

### 6.1 Stored layout

A node packs into 4 bytes:

```
u8  kind     0 = leaf, 1 = split into columns, 2 = split into rows
u8  arg      leaf: widget id;  split: 2 or 3
u16 signal   leaf: signal id;  split: 0
```

Pre-order array, no pointers. With at most 24 leaves there are at most 23
internal nodes, so **47 nodes = 188 bytes per page**, 6 pages = **1128 bytes**
plus a version header. One NVS blob per page, six keys, written on commit only.

Layouts are shared across profiles, for the same reason `ui.md` gives for units
and language: a layout belongs to the person reading the screen, not to the
lighting.

### 6.2 Heap

Measured on the host against LVGL 8.4.0 with this project's `lv_conf.h`
(`LV_MEM_CUSTOM = 1`, so this is real `malloc` accounting via `mallinfo2`),
averaged over 50 objects:

| object | bytes |
|---|---|
| plain `lv_obj` panel | 220 |
| panel + 3 style properties | 228 |
| label, 4 characters | 198 |
| label + font + colour override | 254 |
| `lv_bar` | 348 |
| `lv_meter` + scale + 21 ticks + needle | 214 |
| `lv_chart`, 2 series x 352 points | 1965 |
| **one field: panel + value + unit + caption** | **959** |

> These are **host** figures on a 64-bit target with glibc `malloc`. On the
> ESP32-S3 pointers are 4 bytes and the heap has different per-block overhead,
> so treat them as an upper bound. **Needs measurement** on the board with
> `heap_caps_get_free_size` before and after building a page.

A worst-case page of 24 fields is **23.0 kB**. A chart adds 1.96 kB (1408 bytes
of it is the two 352-point series buffers).

**This is the finding that matters.** `src/ui.c` builds all six pages at init and
switches them with `LV_OBJ_FLAG_HIDDEN` - `page[N_PAGES]` all live at once. Six
worst-case tiled pages would be **about 138 kB against the roughly 180 kB of
free heap**, before anything the diagnostic side allocates. Two ways out:

- build the fields of a page lazily on first visit and tear them down on leave,
  which costs a visible rebuild on every page change; or
- cap the total field count across all pages (24 on one page is a pathological
  layout anyway), and refuse the split that would cross the cap.

The second is cheaper and honest: the editor already refuses splits for size
reasons, so refusing one more for memory needs no new concept. Series buffers
should go to PSRAM, where 5.8 MB makes the chart cost irrelevant.

### 6.3 Redraw

`LV_DISP_DEF_REFR_PERIOD` is 30 ms, so 33 frames per second, which matches what
the board does.

LVGL redraws invalidated rectangles. A font 48 four-digit number occupies
128.5 x 58 = 7452 px:

| what changes in one frame | area | share of 800 x 480 |
|---|---|---|
| 6 four-digit numbers | 44 700 px | 11.6 % |
| 12 four-digit numbers | 89 400 px | 23.3 % |
| 24 four-digit numbers | 178 900 px | 46.6 % |
| one 192 px dial needle (bounding box) | 36 900 px | 9.6 % |
| two dial needles | | 19.2 % |
| one 352 x 126 chart | 44 352 px | 11.6 % |

So a full 24-field page where everything changes every frame repaints about half
the screen at 33 Hz. That is the ceiling, and it is reachable.

**Two rules keep it off the ceiling:**

1. **Ease needles and bar fills; never ease digits.** `ui.md` describes the
   displayed value easing toward the sampled one every frame. Applied to a
   number that means the text changes on all 33 frames per second, so the worst
   case above becomes the normal case - and a digit that eases is also unreadable
   while it does it. Needles and fills are cheap to ease and are the things that
   benefit. Digits step at the sample rate.
2. **Repaint a label only when its rendered text differs.** Compare the formatted
   string, not the float. A boost reading quantised to whole mbar changes far
   less often than 20 times a second once the engine is steady.

With both rules a realistic page of 12 fields at a 20 Hz sample rate repaints a
fraction of 23.3 %, not 23.3 % every frame.

**Needs measurement, cannot be estimated from here:** actual frame time with
many small invalid rectangles on this RGB panel (LVGL merges overlapping invalid
areas, and the RGB driver's buffer strategy decides whether many small rectangles
are cheaper or dearer than one large one), the cost of `lv_meter` redraw at
D = 192 on this CPU, and whether a PSRAM-backed chart series costs anything
noticeable to read during a redraw.

---

## 7. What to build first

**1. W3 Value, with the field tree behind it.** It is the only widget that fits
every reachable size, so it is the one that proves the tree, the split
arithmetic, the gutters and the NVS blob without any drawing work. Until a page
can be split into 24 fields of plain numbers and survive a reboot, nothing else
is worth starting.

**2. W6 Deviation pair.** It is the reason this van is instrumented at all. The
boost pair caught a regulation deviation that the lamp reported only as
`P2263-22`, nine kilometres into a regeneration, with no indication of which way
or by how much. It is also the widget with the most design content - the
engine-speed-dependent tolerance band and the gating - so finding out early
whether the band can be fitted from logged data decides whether the widget is
honest.

**3. W5 Percent.** Cheapest possible second widget, fits the floor field, and it
is what turns a page of numbers into something readable in peripheral vision.
Pedal, throttle, EGR and VNT are four signals already read, and together they
are the context that makes the deviation pair interpretable.

W1 dial fourth, W2 trend last - the trend is the most expensive in both memory
and redraw, and the existing pressure page already does that job.

## The biggest risk

**The whole grid rests on one number that has never been measured: the distance
from the driver's eyes to the panel.**

Everything in section 2 descends from it. At 684 mm the 48 px font just reaches
the 20 arcmin criterion and the 119 x 101 floor field is defensible. At 900 mm
it subtends 15.2' - the largest font this project has is then **below** the
in-vehicle minimum, and the honest floor is not 119 x 101 but somewhere around
242 x 206, which caps a page at six fields instead of twenty-four. The editor,
the catalogue and the whole idea of a 24-field page would survive, but half the
catalogue would never be placeable.

Two things follow:

- **Measure the mounting distance before building the editor**, and keep the
  floor as two constants (`MIN_W`, `MIN_H`) derived from it rather than as magic
  numbers. The derivation in section 2.1 is four lines of arithmetic; it should
  live in the source as a comment with the measured distance in it.
- **A font larger than 48 px would move the floor the other way.** `ui.md`
  already lists this as an open item: 20' at 800 mm needs a 4.65 mm cap height,
  which is a 40 px cap, which is roughly a 56 px Montserrat. `tools/gen_fonts.sh`
  can generate it. A 56 px digit is 37.5 px wide, so three digits plus chrome is
  127 px - the floor field grows from 119 to 127 px wide and from 101 to about
  112 px tall, and the maximum page drops from 6 x 4 to 5 x 3 fields. That is a
  cheaper trade than it looks, and it should be decided before the editor
  hardcodes a lattice.

The second risk is smaller but more certain: **six tiled pages do not fit in
180 kB of heap** if they are all built at once, which is what the current code
does. Section 6.2 gives two ways out, and neither is free.
