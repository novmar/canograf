# What else is worth reading from this van

What we already read is in
[`vehicles/x82-trafic3-talento.md`](vehicles/x82-trafic3-talento.md) and in
`talento_monitor.py`: engine speed, road speed, pedal, boost demanded and
actual, VNT, air mass demanded and actual, rail demanded and actual, EGR and
throttle position, the temperatures, battery voltage, the whole particulate
filter block, torque, and the air mass adaptation table. None of that is
repeated here.

This document is a shopping list for what to read **next**, ordered by how
likely it is to catch a real failure before the driver notices one.

---

## How to read an entry

Each entry says where the value comes from and how sure we are:

| Tag | Meaning |
|---|---|
| **in file** | the request exists in the pinned ECU description file, name quoted verbatim |
| **not found** | searched for, not present in the file - said so instead of inventing a DID |
| **derived** | no new request, computed from values we already read |
| **fitment unknown** | request exists, but it is not established that this van has the hardware |

And where it belongs: **display**, **log**, or **one-off** (read once during a
check, not continuously).

> **"In file" is not "supported".** The 5P00 software already refuses `247B`,
> `24C3`, `2437`, `247C` and `247D` with `7F 22 31`, all of which are in the
> same file. Every identifier below has to be probed once on the vehicle before
> it is trusted. The `FCxx` / `FDxx` / `FExx` block is Bosch supplier range and
> is the most likely to be refused, or to need a session we do not open.

Files searched, the ones pinned in `ddt4all/setup_talento_marcel.py`:

| Module | Description file |
|---|---|
| ECM `7E0` | `MD1CS006_7A00_5P0_V1.0_20200619T135243.json` |
| BCM `745` | `T4_VS_BCM_BIS_DDT2000_SW15_x_VISTEON_SW18_x_CONTI_20200221T113530.json` |
| EMM `74D` | `EMM_EDISON_DDT2000_SW15_3_V1.0_20181120T161416.json` |
| Airbag `752` | `RSAT4_ACU_eng_v15_20150511T131328.json` |
| ABS `740` | `ESC_X82___UDS___V1_9_20200730T164824.json` |
| EPS `742` | `X82_HEPS_V2.2_20210223T173911.json` |

---

## 1. Exhaust back pressure and filter differential pressure

**in file**, ECM, `22 2448` + `22 2443`. Log, and one number on the DPF page.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `2448` | `Pressure before turbine` | 2 | raw | mbar |
| `2443` | `CSF differential pressure` | 2 | raw - 32768 | mbar |
| `2444` | `CSF differential pressure sensor voltage` | 2 | 0.01, offset -327.68 | V |
| `252D` | `Flag indicating a frozen signal from the exhaust manifold pressure sensor` | 1 | bit, offset 7 | - |

This is the first thing to add. The vehicle document already proves that `242C`
soot mass is a temperature-driven model output and swings 8 g with no physical
change. `2443` is the actual differential pressure the model is built from, and
`2448` is the absolute pressure the turbine sees. A filter that is really
loading up, a collapsed catalyst substrate, or a crushed exhaust pipe all show
in those two and not in the soot estimate.

`252D` is free insurance: the ECU itself tells you the pressure sensor output
has stopped moving, which is the usual way these sensors die (blocked take-off
pipe), and it dies silently.

`2444` in volts next to `2443` in mbar makes a sensor out of calibration
obvious - at idle with a clear exhaust the differential should be near zero and
the voltage near its low rail.

## 2. Energy module live block

**in file**, EMM `74D`, legacy transport, `21 34` `Read_EnergyManagement`.
Display and log.

One frame carries the whole electrical health picture. Offsets are into the
response data, in the file's own numbering:

| Field in the file | Offset | Scaling | Unit |
|---|---|---|---|
| `Vxx_Umin_Cranking` | 33 | 2 bytes, 0.001 | V |
| `V_U_LastBattSleep_mes` | 31 | 2 bytes, 0.001, offset 11.5 | V |
| `V_I_IbattSensor_flt` | 26 | 2 bytes, 0.01, offset -200 | A |
| `V_Pct_SOCInit_est_CCP` | 28 | 3 bytes, / 1000 | % |
| `AlternatorLoad_DIAG` | 7 | 2 bytes, 0.39063 | % |
| `ExternalTemp_DIAG` | 35 | 1 byte, offset -40 | C |
| `BatteryWarning` | 45 | bit 0 | - |

`Vxx_Umin_Cranking` is the one that matters. The lowest voltage reached while
the starter turns is the cheapest proxy for battery internal resistance there
is, and it degrades over months before the van refuses to start on a cold
morning. Trend it per start and the battery announces its own end. Paired with
`V_U_LastBattSleep_mes` (resting voltage at the last sleep) it also separates a
tired battery from a parasitic drain.

We already read the battery *configuration* out of `21 16` and `21 46`. This is
the measured side, and we do not read it yet.

## 3. Per-cylinder fuel balance correction

**in file**, ECM, `22 FD38` .. `22 FD3B` and `22 FD37`. Idle only, log.

| DID | Name in the file | Bytes | Scaling |
|---|---|---|---|
| `FD38` | `Current cylinder-individual correction quantity: integrator outputs in geometrical order (for the cylinder 1)` | 2 | signed / 100 |
| `FD39` | same, cylinder 2 | 2 | signed / 100 |
| `FD3A` | same, cylinder 3 | 2 | signed / 100 |
| `FD3B` | same, cylinder 4 | 2 | signed / 100 |
| `FD37` | `FBC quantity` | 2 | signed / 100 |

The internal names are `FBC_qDvtCylPhys[0..3]` - fuel balance control, the
amount the ECU has to add or subtract on each cylinder to make the crankshaft
turn evenly. This is the only direct measure of injector wear available without
removing anything. A worn or partly blocked injector drifts to one side while
the other three move the opposite way, and it does that long before a misfire
code sets.

Read only at warm idle with no load - the correction is meaningless under load,
where the regulator is not running. One number is enough on screen: the spread
between the largest and the smallest of the four.

**Caveat:** supplier DID range. Probe it before building anything on it.

## 4. Oil pressure, temperature and level

**in file**, ECM. Pressure on the display, the rest in the log.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `25B9` | `Oil pressure mesured` | 2 | 0.001 | bar |
| `2007` | `Oil temperature - engine` | 2 | 0.1, offset -273 | C |
| `20D2` | `Oil level` | 1 | 0.315 | mm |
| `2113` | `OWE - Oil temperature for oil wear estimation on previous engine off` | 2 | 0.1, offset -273 | C |

The dashboard has a lamp that comes on when it is already too late. A real
pressure figure, compared at a fixed speed and a fixed oil temperature, falls
slowly as bearings and the pump wear and as the oil thins out from fuel
dilution. It is the single best long-term wear indicator on a diesel.

`20D2` in millimetres is the sender the service display is built from; useful
because it lets the log show oil consumption per 1000 km without anyone opening
the bonnet.

(The file spells `25B9` as "mesured". Quoted as written.)

## 5. Wheel speeds from ABS

**in file**, ABS `740`, UDS. Log.

| Request | Name in the file | Scaling |
|---|---|---|
| `22 4B00` | `WheelSpeed FL` | 2 bytes, 0.01 km/h |
| `22 4B01` | `WheelSpeed FR` | 2 bytes, 0.01 km/h |
| `22 4B02` | `WheelSpeed RL` | 2 bytes, 0.01 km/h |
| `22 4B03` | `WheelSpeed RR` | 2 bytes, 0.01 km/h |
| `22 4B24` | `Real Circumference` | 2 bytes, named list incl. `2115: 195/75R16` |

Four wheel speeds at 0.01 km/h resolution are worth far more than the one
vehicle speed we read now. See the derived section below - this is where tyre
pressure and brake drag come from, on a van that has no direct pressure sensors
worth trusting.

`4B24` tells you which tyre size the brake module thinks is fitted, which is
how an indirect pressure warning ends up permanently wrong after a wheel
change.

## 6. Fuel temperature and rail regulation error

**in file**, ECM. Log.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `2008` | `Fuel temperature` | 2 | 0.1, offset -273 | C |
| `2828` | `Rail pressure regulation error` | 2 | 0.1, offset -3276.8 | bar |
| `FDF0` | `Governor deviation of the rail pressure governor` | 2 | signed, 100 | hPa |
| `FDEA` | `Setpoint current for the metering unit` | 2 | signed | mA |

We read rail and rail setpoint, so the error is already computable, but `2828`
is the ECU's own filtered version and is what its diagnostics act on.

Fuel temperature is the missing piece. Return fuel heats up when injectors leak
back more than they should; a fuel temperature that climbs faster than usual
for the same ambient and the same load is an early injector return problem.
Also stops you mistaking normal summer rail behaviour for a fault.

`FDEA` is the current commanded into the metering unit on the pump. Rising
current for the same rail pressure means the pump is working harder - that is
wear, and it is readable years before a hard failure. Supplier range, probe it.

## 7. Electric power steering health

**in file**, EPS `742`, UDS. Log, and one-off after any battery work.

| Request | Name in the file | Scaling |
|---|---|---|
| `22 012D` | `MotorTemperature` | 1 byte, offset -50, C |
| `22 012E` | `ECUTemperature` | 1 byte, offset -50, C |
| `22 0160` | `DeratingAssistanceLevel` | 1 byte, % |
| `22 0115` | `AssistanceLevel` | 1 byte, % |
| `22 0172` | `MinimumVoltagebySteer` | 1 byte, 0.25, offset 11.5, V |
| `22 0173` | `BatteryCurrent` | 1 byte, 0.5, A |
| `22 011A` | `HEPSWarning` | list: NoWarning / MinorWarning / MajorWarning |
| `22 0192` | `DistanceTotalizer` | 4 bytes, signed, 0.01 km |

`DeratingAssistanceLevel` is the interesting one: the steering tells you it has
started to reduce assistance before the driver can feel it, usually because the
motor or the controller is getting hot, or because supply voltage sagged.
`MinimumVoltagebySteer` records how far the supply dropped during a hard
steering input, which is a second, independent check on the battery and the
main earth - and it is measured at a load nothing else in the van produces at
standstill.

## 8. Misfire counters and crank wheel adaptation

**in file**, ECM. One-off, plus idle logging.

| DID | Name in the file | Bytes |
|---|---|---|
| `2841` | `Misfire counter - cylinder 1` | 2 |
| `2842` | `Misfire counter - cylinder 2` | 2 |
| `2843` | `Misfire counter - cylinder 3` | 2 |
| `2844` | `Misfire counter - cylinder 4` | 2 |
| `2158` | `TLZ - Filtered adaptive value for cylinders 1 and 4 during slow adaptive process` | 4, signed |
| `2159` | `TLZ - Filtered adaptive value for cylinders 2 and 3 during slow adaptive process` | 4, signed |
| `215D` | `TLZ - TLZ adaptive correction` | 2, offset -32768 |
| `29DF` | `Distance driven since TLZ initialization` | 2, km |

Counters that never reach the DTC threshold still count. A cylinder that
accumulates misfires at ten times the rate of the other three is a story, and
the story is usually the same injector that shows up in entry 3.

The TLZ values are the crankshaft sensor wheel adaptation. They matter for a
second reason: misfire and fuel-balance detection are both built on that
adaptation, so if `29DF` shows the learning was reset recently, everything in
this entry and in entry 3 is not yet trustworthy.

## 9. Lambda sensor condition

**in file**, ECM. Log.

| DID | Name in the file | Bytes | Scaling |
|---|---|---|---|
| `251E` | `Raw UEGO Air Fuel ratio measurement` | 2 | 3.05176e-05 |
| `20D3` | `Factor to correct UEGO signal aging shifting` | 2 | 0.001 |
| `2519` | `Exhaust gaz oxygen concentration` | 2 | 0.1, unit % |
| `2ABD` | `Amplitude of upstream lambda richness signal` | - | - |

`20D3` is the point of this entry: the ECU's own correction factor for how far
the wideband sensor has drifted with age. It moves away from 1.000 as the
sensor ages, and when it saturates the sensor is finished. That is a
replacement you can schedule instead of discover.

`2519` as a plain oxygen percentage is also the clearest single confirmation
that a regeneration is really burning something.

(The file spells it "gaz". Quoted as written.)

## 10. Start and wear counters

**in file**, ECM and BCM. One-off.

| Module | DID | Name in the file | Bytes |
|---|---|---|---|
| ECM | `2168` | `Cumulative number of engine starts` | 3 |
| ECM | `216A` | `Cumulative number of engine starts, non-resettable` | 3 |
| ECM | `22AA` | `Cumulative number of engine starts, for starter reliability` | 3 |
| ECM | `22AB` | `Cumulative number of engine starts for high pressure fuel pump reliability` | 3 |
| BCM | `22 010F` | `STORED_STARTER_ACTIVATION_COUNTER_S` | 2, step 10 |
| BCM | `22 010E` | `STORED_HP_PUMP_ACTIVATION_COUNTER_S` | 2, step 10 |
| ECM | `2231` | `Counter of stalling of type clutch, failed starting up, due to the software lock` | 2 |

A van with stop-start has two completely different start counts: ignition
starts and automatic restarts. `216A` is the non-resettable one, so the
difference between `216A` and `2168` also tells you whether somebody has reset
the engine control unit, which is worth knowing before trusting any other
adaptation value.

The two BCM counters are the ones the manufacturer actually uses to decide the
starter and the high pressure pump have had enough cycles.

## 11. Brake servo vacuum

**in file**, ECM. `20AA` on the display while driving, `2892` one-off.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `20AA` | `Mastervac vacuum pressure by sensor` | 1 | 50 | hPa |
| `29E8` | `Mastervac vacuum relative pressure by analog sensor` | - | - | - |
| `2892` | `Cumulative run time for mastervac vacuum pump` | 2 | 100 | - |
| `2891` | `Mastervac vacuum pump activation request` | - | - | - |

A diesel with an electric assist pump loses braking assistance gradually. The
cumulative pump run time rising faster per kilometre than it used to means a
leak in the servo or a tired pump, and the first symptom otherwise is a hard
pedal at the worst moment. This is a safety item that is completely invisible
today.

## 12. Thermostat and warm-up

**in file**, ECM. Derived; see section B below.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `2871` | `Piloted thermostat PWM command.value` | 1 | 0.3921569 | % |
| `209E` | `Engine coolant temperature setpoint` | 2 | 0.3125, offset -40 | C |
| `2B35` | `Starting coolant temperature` | 2 | 0.1, offset -40 | C |

A thermostat stuck slightly open is the classic silent fault: nothing lights
up, the engine just never reaches target, fuel consumption rises and the DPF
regenerates more often. Having both the commanded position and the target
temperature alongside the coolant temperature we already read turns that into
a two-line test instead of a guess.

## 13. Glow plug system

**in file**, ECM. One-off at a cold start.

| DID | Name in the file | Bytes | Scaling |
|---|---|---|---|
| `201A` | `State of glow plug control actuator relay` | 1 | bit, offset 7 |
| `2052` | `Heating glow plugs PWM command.value` | 1 | 0.392156863 % |
| `219A` | `PWM diagnosis signal of the heating glow plugs` | 2 | 0.25 % |

`219A` is a diagnostic feedback duty cycle, not a command. Comparing it against
the commanded `2052` at a cold start is how a single failed plug shows up,
months before winter makes it a problem.

## 14. Dual-mass flywheel protection counters

**in file**, ECM. One-off.

| DID | Name in the file | Bytes | Scaling |
|---|---|---|---|
| `22B9` | `Synchronisation loss counter for Double Flywheel Clutch protection strategy` | 1 | raw |
| `22BA` | `Minimum derivate of engine speed recorded in Double Flywheel Clutch protection strategy` | 2 | offset -8192, rpm |
| `22BB` | `Minimum engine speed recorded in Double Flywheel Clutch protection strategy` | 2 | 0.25 rpm |

The ECU keeps a count of how often crankshaft synchronisation was lost in a way
that suggests flywheel damping trouble, and the worst engine speed drop it ever
recorded. A dual-mass flywheel is an expensive, labour-heavy repair and it
announces itself here as a rising count long before the rattle is audible.

## 15. Airbag firing-line impedances

**in file**, airbag `752`, legacy transport, `21 04` and `21 06`. One-off.

`21 04` `Reading of the firing line value` returns impedance per circuit, each
one byte divided by 25, in ohms, including:

- `Impedance for FL front driver airbag level 1` (offset 7)
- `Impedance for FL front passenger airbag level 1` (offset 5)
- `Impedance for FL pyro retractor left` / `right` (offsets 17, 18)
- `Impedance for FL front left curtain` / `right curtain` (offsets 11, 12)

`21 06` `Reading of the sensor value` adds buckle switch impedances (step 25
ohm), the door and B-pillar satellite readings, and `Power supply voltage
value` (divide by 10).

`21 02` `Reading of ECU state synthesis` gives `Number of present fault`,
`Number of memorized fault`, `calculator has to be changed`, and the belt
buckle states.

Squib circuits fail from connector corrosion under the seat, and the resistance
creeps up for a long time before it crosses the threshold that lights the lamp.
Reading the actual ohms once a year is a genuine safety check that no generic
scan tool offers on this van. Read only - nothing here should ever be written.

## 16. Brake hydraulics and sensor offsets

**in file**, ABS `740`. One-off after any suspension or brake work, and `4B17`
in the log.

| Request | Name in the file | Scaling |
|---|---|---|
| `22 4B17` | `Hydraulic unit temperature` | 1 byte, C |
| `22 4B0E` | `Braking pressure` | 1 byte, bar |
| `22 4B36` | `Steering wheel angle offset` | 0.2, offset -25.4, deg |
| `22 4B37` | `Lateral acceleration offset` | 0.01, offset -1.27, g |
| `22 4B38` | `Longitudinal acceleration offset` | 0.1, offset -12.7, m/s2 |
| `22 4B39` | `Braking pressure offset` | 0.2, offset -25.4, bar |

The four offsets are the calibration the stability system carries. Large or
changing offsets mean a sensor is drifting, and the first visible symptom is
usually ESP cutting in where it should not. Reading them is also the only way
to confirm a steering angle calibration actually took.

Hydraulic unit temperature plus braking pressure is how you catch a dragging
caliper on a long descent.

## 17. Water in fuel

**in file**, ECM, `22 FD22`. Display, as a flag.

`Water in fuel detection`, internal name `FuFilWtrDetn_stAct`, 2 bytes signed.
Supplier range, so probe it. The dashboard lamp for this exists but is easy to
miss; a persistent flag in the log tells you the filter has been sitting wet,
which is how injectors and the high pressure pump die on a van that does short
runs on cheap fuel.

## 18. EGR path, demanded against actual

**in file**, ECM. Log.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `2408` | `EGR valve position setpoint.value` | 2 | offset -32768, / 100 | % |
| `2491` | `EGR rate setpoint` | 2 | 0.01 | % |
| `2405` | `Inlet throttle position setpoint.value` | 2 | 0.0488 | % |
| `20C5` | `Down egr cooler temp sensor output signal value` | 2 | raw | mV |
| `20C7` | `Raw down EGR cooler temperature after linearization` | - | - | - |
| `28C9` | `temperature sensor after egr cooler` | - | - | - |
| `2B33` | `Distance between the last second HP EGR cooler anti-dirt strategy activation` | 2 | raw | - |

We read EGR position (`2407`) and throttle position (`2404`) but not what the
ECU asked for. The pair is what matters: a valve that no longer reaches its
commanded position is coking up, and that is the same story as the boost
regulation deviation that made this project useful in the first place.

`2B33` is interesting on its own - the ECU runs an anti-fouling strategy on the
EGR cooler and records the distance since the last one. Frequent activations
mean the cooler is clogging.

## 19. Alternator and charging

**in file**, ECM. Log.

| DID | Name in the file | Bytes | Scaling | Unit |
|---|---|---|---|---|
| `28DC` | `Alternator voltage set-point` | 2 | 0.01 | V |
| `2099` | `Alternator load` | 1 | 0.3921569 | % |
| `2A08` | `Filtered alternator load` | 1 | 0.3921568627 | % |
| `2057` | `Alternator power` | 2 | 10 | W |
| `28E9` | `Current Battery state of charge computed` | 2 | 0.001525902 | % |
| `28E8` | `Last estimated battery temperature` | 2 | 0.1, offset -273 | C |
| `28E5` | `Battery current send by LIN network` | 2 | 0.01, offset -327.675, signed | A |
| `28EE` | `No load battery voltage` | 2 | 0.001 | V |

The vehicle document already describes how the battery was reconfigured to AGM.
This is how you prove the charging strategy actually changed: the alternator
voltage setpoint should follow a different profile, and `28DC` shows it
directly instead of inferring it from `2005` battery voltage.

The `28Ex` group is the ECM's copy of what the battery sensor reports over LIN,
which is a convenient cross-check against the EMM block in entry 2 - two
modules measuring the same battery is the cheapest way to catch a failing
current sensor.

## 20. Tyre pressures from the body module

**fitment unknown**, BCM `745`. Display, if the hardware is there.

| Request | Name in the file | Scaling |
|---|---|---|
| `22 8010` | `TPMS_INTERNAL_PRES 1` | 2 bytes, 7.5 mbar |
| `22 8017` / `22 8024` / `22 8031` | `TPMS_INTERNAL_PRES 2` / `3` / `4` | 2 bytes, 7.5 mbar |
| `22 8011` / `8018` / `8025` / `8032` | `TPMS_INTERNAL_TEMP 1..4` | 1 byte, offset -30, C |
| `22 8009` / `8016` / `8023` / `8030` | `TPMS_INTERNAL_LOW_BATT_ON 1..4` | bit |
| `21 78` | `Read_TPMS_PRESSIONS_TEMPERATURES` | reference pressures, 7.5 mbar; reference temps, offset -30 |

The BCM description file carries a full direct TPMS implementation with live
per-wheel pressure and temperature, plus a low-battery flag per sensor. Whether
this particular van has the valve sensors is not established - a Talento panel
van may well have the indirect system or none. One probe settles it.

The low-battery flags are worth having on their own: TPMS sensor batteries die
after about seven years and the replacement is a tyre-off job you want to
combine with something else.

## 21. Injector programming check

**in file**, ECM. One-off, only after injector work.

| DID | Name in the file |
|---|---|
| `FDC4` | `IMA code of all injectors` (28 bytes, ASCII) |
| `FDC5` | `Factory correction of the injector.IMA.cylinder1` (7 bytes, ASCII) |
| `FD0E` / `FD0F` / `FD10` | same for cylinders 2, 3, 4 |
| `FE2F` / `FE32` / `FE30` / `FE31` | `Check missing injector adjustment value programming (for the cylinder 1 / 2 / 3 / 4)` |
| `283D` | `Injectors status` (1 byte, 6 bits, offset 2) |
| `2A91` | `Needle injector failure type` |

Not a monitoring item - a verification item. After anyone replaces an injector,
the `FE2x` checks say whether the new calibration code was actually programmed.
An unprogrammed injector runs, badly, and nothing warns you.

## 22. Swirl flap learned positions

**fitment unknown**, ECM. One-off.

| DID | Name in the file | Bytes | Scaling |
|---|---|---|---|
| `2581` | `Swirl Flap - Flag of the first closed thrust learnt and stored in EEPROM` | 1 | bit, list not learned / learned |
| `2582` | `Swirl Flap - Flag of the first opened thrust learnt and stored in EEPROM` | 1 | bit |
| `2586` | `Swirl Flap - Absolute position in percent of the sensor supply voltage` | 2 | 0.0488 % |
| `2588` | `Swirl Flap - Current value of the 'closed thrust position' used by the regulation system` | 2 | 0.0488 % |
| `258B` | `Swirl Flap - Current value of the 'last closed thrust position' learnt` | 2 | 0.0488 % |
| `2585` | `Swirl Flap - Number of first offset learning failures noticed on successive driving cycles (closed thrust)` | - | - |

Exactly the same structure as the turbo vane end stops already documented, and
exactly the same trap: comparing the first learned position with the current
one shows how far the flap shaft has coked up. Whether this engine has swirl
flaps at all is unconfirmed. If `2586` answers, it does.

## 23. Blow-by

**in file but meaning unverified**, ECM.

| DID | Name in the file | Bytes |
|---|---|---|
| `25BC` | `Max blow-by counter value of last start` | 2 |
| `FC61` | `Blow by heater (Raw)` | 2, signed, % |
| `FC62` | `Blow by heater (Physical)` | 1, signed |
| `FC84` | `Counter status for PCV limp home` | - |

Listed for honesty rather than enthusiasm. `25BC` is called a blow-by counter
but the file gives no unit, no scaling and no description beyond the internal
name `Vxx_blow_ctr_max_mem`, so what it counts is a guess. `FC61`/`FC62` are
the heated crankcase breather, which is a real part that freezes in winter and
then pressurises the crankcase until a seal lets go.

**Crankcase pressure itself: not found.** Searched the engine file for
crankcase, carter, PCV, CCV and breather; the only hits are the heater above
and `FC84`. There is no crankcase pressure sensor identifier in this file. If
crankcase pressure is wanted it has to be an added sensor, not a read.

---

## Derived values - no new identifiers needed

The one we already have is the boost deficit: setpoint minus actual, gated on
engine speed and pedal, which found a turbo regulation deviation before any
lamp came on. These are the same shape.

### A. Wheel speed spread (needs entry 5)

`max(FL,FR,RL,RR) - min(...)` normalised by the mean, over a window of straight
steady driving (filter on `22 4B0C` yaw speed near zero).

A soft tyre turns measurably faster than its neighbours. This is literally how
indirect tyre pressure monitoring works, and doing it ourselves means we see
the number instead of waiting for a warning lamp with a threshold somebody else
chose. The same spread, biased to one wheel and growing with brake use,
is a dragging caliper. Cross-check against `22 4B17` hydraulic unit
temperature.

### B. Warm-up slope and thermostat verdict (needs entry 12)

From `2B35` starting coolant temperature, time the climb of `2001` to within a
few degrees of `209E` setpoint, under a known ambient from `20DE`
(`Ambient temperature`, 2 bytes, 0.1, offset -273).

A thermostat stuck open shows as a warm-up that takes far longer than the same
trip did last winter, or as a steady-state coolant temperature that sits below
setpoint while `2871` commands full heating. Neither of those sets a code until
it is extreme.

### C. Filter temperature rise across the DPF

`csf_dn - csf_up`, which we already log and currently only display as two
separate numbers.

From the 24 November log, 975 samples, in the 604 samples where the upstream
temperature was above 250 C the downstream temperature sat a **median 22.8 C
below** the upstream - that is the normal passive case, the filter shedding
heat. A real regeneration drives this strongly positive. A catalyst that has
lost its coating cannot produce that rise any more, and the first symptom is
regenerations that run long and fail. One derived number, no new read, and it
is a better regeneration detector than `rgn_dur` alone.

### D. Back pressure per unit of air flow (needs entry 1)

`2448 pressure before turbine` divided by air mass flow (`2412`, already read,
or `2422` `Amount of air pumped into cylinder`, 2 bytes, 0.05 kg/h).

Raw back pressure means nothing without knowing how much air is going through.
The ratio is load-independent and makes a slow restriction - filter ash, a
collapsing flexible pipe - visible as a trend across months. This is the
measurement the soot estimate is pretending to be.

### E. Air mass error, already available

`2412 - 2413`, both already in the log, currently stored but not used.

From the same 24 November log: median -58 mg, 5th percentile -541 mg, 95th
+114 mg across 975 samples. Those tails are transients. What matters is the
steady-state value at a fixed operating point - a persistent negative bias is
an intake leak, a stuck-open EGR valve, or a drifting air mass sensor, and it
is the quantity the adaptation table in `2509` is slowly trying to cancel. If
the correction vector grows while this error stays large, the adaptation has
run out of authority.

### F. Cranking voltage drop (needs entry 2)

`V_U_LastBattSleep_mes` minus `Vxx_Umin_Cranking`, per start.

Resting voltage minus the minimum reached under the starter is an internal
resistance proxy. One figure per start, trended over a winter, predicts the
morning the van will not start. Nothing else in the vehicle shows this.

### G. Steering supply sag (needs entry 7)

`22 0172 MinimumVoltagebySteer` compared against `2005` battery voltage at the
same moment.

The difference is the drop across the main supply path under a load that occurs
at standstill. Growing difference with a healthy battery means a corroded earth
or a tired main connection - a fault that otherwise produces unrelated,
confusing symptoms across several modules.

### H. Injector balance spread (needs entry 3)

`max(FD38..FD3B) - min(FD38..FD3B)`, at warm idle only.

One number instead of four. Zero is perfect, growing is wear, and which
cylinder sits at the extreme names the injector.

### I. Rail error split by operating point (needs entry 6)

`2828` averaged separately at idle and at full load.

Error at idle but not at load points at injector leak-back. Error at load but
not at idle points at the pump or at supply. Same signal, two bins, two
different repairs. From the 24 November log the raw rail-minus-setpoint across
799 running samples was a median -1 bar with a 95th percentile of +152 bar, so
the transients dominate and the binning is what makes it readable.

### J. Vacuum pump duty per kilometre (needs entry 11)

Delta of `2892` cumulative run time divided by delta of `2006` odometer.

A single absolute run time is meaningless on a used van. The rate is not: it
should be roughly constant for the same kind of driving, and a rise means a
leak in the servo or the check valve.

---

## Driving against standing

Some of this is only meaningful in one state, and reading it in the wrong one
produces confident nonsense.

**Only while driving, under load:**

- back pressure and the back-pressure-per-air-flow ratio (entry 1, derived D) -
  at idle the exhaust flow is too small to show a restriction
- EGR and throttle demanded against actual (entry 18) - both sit at a parked
  position at idle
- wheel speed spread (entry 5, derived A) - needs steady straight driving above
  roughly 50 km/h, with yaw near zero
- EPS motor temperature and derating (entry 7) - though the worst case for
  `MinimumVoltagebySteer` is parking manoeuvres at standstill
- brake hydraulic unit temperature (entry 16)
- filter temperature rise (derived C) - needs real exhaust temperature

**Only at idle, warm, no load:**

- per-cylinder fuel balance (entry 3) - the regulator does not run under load,
  and the values are stale rather than wrong
- injector balance spread (derived H)
- idle speed against `2024` `Idle engine speed setpoint.value` (2 bytes, / 4,
  rpm) - a persistent gap is a load the ECU cannot overcome
- rail error at idle (derived I, first bin)

**Only at a cold start:**

- glow plug command against diagnostic feedback (entry 13)
- cranking minimum voltage and the voltage drop (entry 2, derived F) - a warm
  restart does not stress the battery
- `2B35` starting coolant temperature, which anchors the warm-up test (derived B)

**Engine off, ignition on:**

- airbag firing-line impedances (entry 15) - and the vehicle document already
  warns that service writes are refused with `7F 2E 22` unless the engine is
  off, which is the same state
- oil level (entry 4) - only settles when the engine has stood

**Ignores vehicle state entirely:**

- all the counters (entries 10, 14), the TPMS block (entry 20), the injector
  programming check (entry 21), and the learned positions (entry 22). These are
  stored values; read them whenever.

---

## What was looked for and is not there

- **Crankcase pressure.** No identifier in the engine file. See entry 23.
- **Brake pad or lining wear.** Searched the ABS and BCM files for pad, lining,
  wear and garniture. Nothing. This van signals pad wear with a plain wire to
  the instrument cluster, if at all; it is not a diagnostic read.
- **Boost regulation error `2437` and the stored deviation extremes `247C`
  `247D`.** They are in the file, they look perfect for this job, and the
  vehicle document already records that the 5P00 software refuses all of them
  with `7F 22 31`. Listed here so nobody rediscovers them and wastes an
  afternoon.
- **Clutch wear.** `2C16` `Clutch temperature` and friends exist in the engine
  file but belong to an automated gearbox this van does not have.

---

## Summary

**23 numbered entries.** Of those:

- **20 cite a request copied verbatim out of the pinned ECU description file**
  for the exact module and software - entries 1 to 19 and 21.
- **2 are fitment questions** - entry 20 (TPMS) and entry 22 (swirl flaps). The
  requests exist in the file; whether this van has the hardware is unconfirmed
  and one probe settles each.
- **1 is in the file but its meaning is a guess** - entry 23, blow-by, where
  the file gives a name and no scaling.
- **10 derived quantities** (A to J) need no new identifier; two of them are
  quantified against the 24 November drive log and the rest are reasoning.
- **2 things were searched for and genuinely not found** - crankcase pressure
  and brake pad wear - plus 2 more that are in the file but unusable here, the
  refused boost deviation identifiers and the automated-gearbox clutch block.
  All four are listed above rather than quietly dropped.

Nothing here has been read from the vehicle yet. Every identifier needs one
probe before it is built into anything, and the `FCxx`/`FDxx`/`FExx` supplier
range is the most likely to be refused.

### The first three

**1. Exhaust back pressure and filter differential pressure (entry 1).** Two
identifiers, both in the ordinary `24xx` range that this software has already
proven it answers, both slotting straight into the existing slow loop with no
new transport work. They fix a documented weakness: the soot mass we display
is a temperature-driven model that swung 8 g in an hour with nothing physically
changing, and `2443` is the actual sensor it is derived from. Best value per
line of code in the whole list.

**2. The energy module live block (entry 2).** One legacy frame, `21 34`, on a
transport the project already speaks for the battery configuration work. It
returns cranking minimum voltage, resting voltage, battery current, state of
charge and alternator load together. Battery and starter are what actually
strands a van, and the cranking voltage trend is the only honest early warning
there is. It also validates the AGM reconfiguration that was already done.

**3. Per-cylinder fuel balance (entry 3).** Four identifiers, read at warm idle,
reduced to one number. It is the only direct measure of injector wear available
without taking anything apart, on the component that is both the most expensive
common failure on this engine and the one that gives the longest warning. It is
third rather than first only because the supplier DID range may refuse to
answer, and that has to be established before anything is built on it.
