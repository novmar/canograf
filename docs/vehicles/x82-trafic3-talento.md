# Renault X82 - Trafic III, Fiat Talento, Opel Vivaro B, Nissan NV300

Verified on a Fiat Talento 2020, 2.0 diesel, engine control unit
**Bosch MD1CS006**, software `7A00 / 5P00`.

Everything below was read from a real vehicle. Scaling comes from the DDT4All
ECU description file and was cross-checked against plausible physical values.
Where a value looked wrong it is marked.

---

## Buses and addressing

All modules sit on one **HS CAN, 500 kbit/s, 11-bit** segment reachable from
the OBD socket on pins 6 and 14.

| Module | TX | RX | Transport |
|---|---|---|---|
| ECM - engine | `7E0` | `7E8` | UDS (`10 03`) |
| ABS / ESC | `740` | `760` | UDS |
| EPS - steering | `742` | `762` | UDS |
| BCM / UCH - body | `745` | `765` | legacy Renault (`10 C0`) |
| EMM - energy | `74D` | `76D` | legacy Renault (`10 C0`) |
| Airbag | `752` | `772` | legacy Renault (`10 C0`) |
| Instrument cluster | `743` | `763` | legacy, but answers `22 xxxx` too |
| SIB - gateway | `7C7` | `7D3` | no response observed |

**Half the vehicle is not UDS.** Body, energy and airbag modules use the older
Renault transport: session `10 C0`, reads `21 xx`, writes `3B xx`. A client that
only speaks UDS reaches the engine and brakes and nothing else.

## Engine - live values

Request is `22` plus the identifier. Up to **three identifiers** fit in one
request. Value is `raw * step + offset`.

| Key | DID | Bytes | Step | Offset | Unit | Meaning |
|---|---|---|---|---|---|---|
| `rpm` | `2002` | 2 | 0.25 | 0 | rpm | engine speed |
| `speed` | `2003` | 2 | 0.01 | 0 | km/h | vehicle speed |
| `pedal` | `202E` | 2 | 0.125 | 0 | % | accelerator, exceeds 100 on kickdown |
| `torque` | `2004` | 2 | 0.03125 | -400 | Nm | estimated effective torque |
| `coolant` | `2001` | 2 | 0.1 | -273 | C | coolant temperature |
| `vbat` | `2005` | 2 | 0.01 | 0 | V | battery voltage |
| `atmos` | `2009` | 2 | 1 | 0 | mbar | atmospheric pressure |
| `boost` | `2401` | 2 | 1 | 0 | mbar | boost pressure, absolute |
| `boost_sp` | `2402` | 2 | 1 | 0 | mbar | boost pressure setpoint |
| `manifold` | `240B` | 2 | 1 | 0 | mbar | intake manifold pressure |
| `iat` | `240D` | 2 | 0.1 | -273 | C | intake air temperature |
| `maf` | `2412` | 2 | 0.1 | 0 | mg | mass air flow |
| `maf_sp` | `2413` | 2 | 0.1 | 0 | mg | mass air flow setpoint |
| `throttle` | `2404` | 2 | 0.0488 | 0 | % | intake throttle position |
| `egr` | `2407` | 2 | 0.0488 | 0 | % | EGR valve position |
| `vnt` | `2527` | 2 | 0.01 | 0 | % | turbo vane PWM, before voltage correction |
| `turbo_pos` | `20F6` | 2 | 1 | 0 | mV | turbo actuator position feedback |
| `rail` | `2801` | 2 | 1 | 0 | bar | rail pressure |
| `rail_sp` | `2802` | 2 | 1 | 0 | bar | rail pressure setpoint |
| `boost_mode` | `241E` | 1 | 1 | 0 | - | boost control state, 0 at idle |
| `odometer` | `2006` | 3 | 1 | 0 | km | total distance |

Not supported on this software version, answer `7F 22 31`:
`247B` learned boost correction, `24C3` wastegate slow adaptive term,
`2437` boost regulation error, `247C` / `247D` stored maximum deviations.

## Particulate filter

| Key | DID | Bytes | Step | Unit | Meaning |
|---|---|---|---|---|---|
| `soot` | `242C` | 2 | 0.01 | g | soot mass, **see the warning below** |
| `csf_load` | `24BA` | 2 | 0.1 | g | equivalent load |
| `csf_up` | `2442` | 2 | 0.1 (-273) | C | temperature before the filter |
| `csf_dn` | `2441` | 2 | 0.1 (-273) | C | temperature after the filter |
| `csf_sp` | `2432` | 2 | 0.1 (-273) | C | target temperature for regeneration |
| `rgn_dur` | `2487` | 2 | 1 | s | current regeneration duration |
| `rgn_end` | `2557` | 3 | 0.1 | s | time since last regeneration ended |
| `burn` | `2559` | 2 | 0.001 | - | fraction of soot burnt in last attempt |
| `rgn_km` | `25AD` | 3 | 1 | km | odometer at last regeneration start |
| `left_after` | `24A5` | 2 | 0.01 | g | soot left after the last regeneration |
| starts | `25A3` | 2 | 1 | - | regeneration starts since oil change |
| high eff. | `25A7` | 2 | 1 | - | regenerations with high efficiency |
| low eff. | `25A5` | 2 | 1 | - | regenerations with low efficiency |

> **`242C` is not a measurement, it is an estimate from differential pressure,
> and it tracks exhaust temperature.** Over one hour of driving it swung between
> 7.7 and 15.7 g with no regeneration running. Correlation with the temperature
> before the filter was **r = +0.84**; with air mass flow only +0.22. Binned by
> temperature: 8.7 g at 80-150 C, 9.8 g at 150-250 C, 11.0 g at 250-350 C,
> 13.0 g at 350-470 C. Whenever the exhaust cooled the value returned to about
> 8.75 g.
>
> **Compare it only at matched temperature.** Cold idle is a usable reference.
> Measured accumulation that way was 0.043 g/km, which is normal.

## Fault memory

| Request | Meaning |
|---|---|
| `19 02 FF` | all known codes with status bytes |
| `19 02 01` | only what is failing right now - short answer, fast |
| `19 04 <dtc> FF` | freeze frame |
| `19 06 <dtc> FF` | extended records |
| `19 0A` | supported codes |

Status byte, ISO 14229:

```
bit0 0x01 testFailed          bit4 0x10 testNotCompletedSinceClear
bit1 0x02 failedThisCycle     bit5 0x20 testFailedSinceClear
bit2 0x04 pending             bit6 0x40 testNotCompletedThisCycle
bit3 0x08 confirmed           bit7 0x80 warningIndicatorRequested
```

`0x50` means "never ran since the last clear" - that is the resting state of
most codes, **not** a fault. Reading `19 02 FF` returns well over a hundred
entries and almost all of them are `0x50`. Filter on `0x2F` before showing
anything to a user.

### Extended record numbers

Named in the ECU description file, confirmed on the vehicle:

| Record | Meaning |
|---|---|
| `0x80` | odometer when the fault occurred, 3 bytes, km |
| `0x81` | aging counter |
| `0x82` | occurrence counter |
| `0x84` | occurrence counter, second variant |
| `0x85` | odometer when the fault last disappeared |
| `0x86` | **distance driven with the fault present**, km |
| `0x87` | odometer at first occurrence |
| `0x88` / `0x89` | IUPR numerator / denominator |

**These are destroyed when the fault self-heals**, not only when someone clears
the memory. Observed: a code went from status `0xE5` to `0x50` on its own and
the extended records came back empty. Read them early.

## Routines

The engine control unit implements exactly seven. There is **no turbo
adaptation routine** - no tool can run one.

| Request | Meaning |
|---|---|
| `31 01 02 0D` | start particulate filter regeneration |
| `31 02 02 0D` | stop regeneration |
| `31 03 02 0D` | regeneration status |
| `31 01 02 09` | start rail pressure test |
| `31 02 02 09` | stop rail pressure test |
| `31 03 02 09` | rail pressure test status |
| `31 00 ...` | generic routine control |

## Turbo vane learned end stops

Not a routine - values in EEPROM. This is the real "adaptation".

| DID | Meaning |
|---|---|
| `2903` | lower stop learned flag, bit 0: 1 = learned |
| `2904` | upper stop learned flag |
| `290C` | first learned closed position |
| `290D` | current closed position |
| `290E` | first learned open position |
| `290F` | current open position |
| `2909` | current position, % of sensor supply |

Positions are 2 bytes, step `0.0488`, unit % of sensor supply voltage.

Factory defaults written by the reset in DDT4All are `0x02CD` = **35.0 %**
closed and `0x05D8` = **73.0 %** open. A healthy learned unit differs from
these. One measured example: closed 34.06 %, open 83.25 %.

> The open stop was **10 points above the factory default**. Blindly resetting
> to defaults would have made the ECU believe the vane travel was far smaller
> than it is - exactly the kind of boost control deviation one might be trying
> to fix. Read before you reset.

## Air mass flow adaptation

| DID | Meaning |
|---|---|
| `2508` | learned breakpoint flags, **8 separate bytes**, bit 0 of each |
| `20D8` | second flag table, 1 byte |
| `2509` | correction vector, 8 x 2 bytes, step 0.1 kg/h |

`22 2508` returns eight flag bytes at offsets 4 to 11, one per breakpoint.
Reading only the first byte and concluding "nothing is learned" is wrong - that
mistake was made during this work.

After a factory reset of the engine control unit this table is empty and the
air path runs on base maps with no correction. Low breakpoints fill in during
ordinary driving; the high ones need sustained high load and may never fill in
city use.

## Oil service data

| DID | Meaning | Write |
|---|---|---|
| `2110` | odometer at last oil change, 3 bytes, km | `2E 2110 xxxxxx` |
| `2111` | oil change type, 0 workshop / 1 customer | `2E 2111 xx` |
| `2112` | re-initialise strategies after a service change | `2E 2112 01` |
| `2299` | calculated remaining oil potential, km | computed, see below |
| `211B` | oil dilution rate, % | |
| `25A3` `25A4` `25A5` `25A7` | regeneration counters since oil change | |
| `25AB` `25AC` | vehicle starts / stops since oil change | |

**Reset procedure**, mirrors the `IVP- Ecriture 1` screen in DDT4All:

```
10 03                     extended session
2E 2110 <odometer, 3 B>   set distance at last change
2E 2112 01                re-initialise
```

The unit then zeroes every derived counter by itself and sets the remaining
potential to 40000 km. Engine must be **off**, ignition on, otherwise the write
is refused with `7F 2E 22`.

**There is no settable service interval.** `2299` is a model output - writing to
it is accepted with `6E 2299` and then silently recomputed back. The 40000 km
ceiling is a calibration constant inside the ECU software.

## Energy module - battery configuration

Legacy transport. Read `21 16` (`Read_AlternatorManagement_CF`), write `3B 16`
with the **first 36 data bytes** of that response, modified.

| Field | Where | Meaning |
|---|---|---|
| `C_B_STOPSTARTInhibit` | byte 3, bit 1 | stop-start inhibited |
| `C_B_VRLA_Batt` | byte 3, bit 2 | 1 = AGM / VRLA, 0 = flooded |
| `C_K_BatteryType` | byte 4 | size class, 2 = L2 ... 6 = L6 |
| `C_K_res_CapaciteNominale` | byte 6 | resolved capacity, Ah |

Nominal capacity and cold cranking current per class, from the calibration
tables in `21 16` and `21 46`:

| class | flooded | AGM / VRLA |
|---|---|---|
| 2 | 60 Ah / 510 A | 60 Ah / 680 A |
| 3 | 70 Ah / 720 A | 70 Ah / 760 A |
| 4 | 85 Ah / 760 A | 80 Ah / 800 A |
| 5 | 95 Ah / 850 A | 95 Ah / 850 A |
| 6 | 110 Ah / 950 A | 110 Ah / 950 A |

Cold cranking current is in a different block: read `21 46`, write `3B 46`,
first 53 data bytes, resolved value at byte 3, step 10.

Bit offsets follow the DDT4All convention, counted **from the most significant
bit**: `value = (byte >> (8 - offset - width)) & mask`.

## Stop-start, if someone disabled it

Three parameters across two modules:

| Module | Parameter | Read |
|---|---|---|
| BCM `745` | `STOPSTART_TYPE_CF` | `22 00ED` |
| BCM `745` | `E_B_STOPSTARTINHIBIT_CF` | `22 00AD` |
| EMM `74D` | `C_B_STOPSTARTInhibit` | `21 6B` byte 3 bit 1 |

With the energy module inhibited it stores a confirmed fault
`9267-55 STOP_START` which requests the warning indicator. That lamp is
self-inflicted and will not clear on its own.

## Worked example - P2263 during a regeneration

Useful as a template for reading these systems.

`P2263-22`, named **Turbocharger Boost Control Function** in the ECU file.
Appeared once, status `0xE5`.

```
19 06 22 63 22 FF
59 06 22 63 22 e5
  80  01 68 30   odometer at occurrence     92208 km
  84  01         occurrence counter         1
  85  ff ff ff   odometer when it cleared   not set yet
  86  00 00 05   distance with fault        5 km
  87  01 68 30   odometer at first          92208 km
```

Freeze frame (`19 04`) at that moment: 2399 rpm, 131 km/h, 297 Nm, coolant
79 C, boost setpoint 2759 mbar, actual 2343 mbar - a **416 mbar shortfall** at
full load.

`25AD` said the last regeneration started at **92199 km**. The fault appeared
**9 km into a running regeneration**.

During regeneration the air path is driven for exhaust temperature, not for
boost: the intake throttle closes, EGR shifts, post injection runs. A sudden
full load demand in that state produces a deviation the boost monitor can read
as a fault. This unit does **not** inhibit the boost diagnostic during
regeneration - proven by the fact that the code was set at all.

Follow-up over 74 km of logging at 3.5 Hz showed the turbo itself was healthy:
peak boost 2853 mbar against a 2809 mbar setpoint, the vane actuator tracking
the command linearly at about 19 mV per percent with no dead zone, and the
regulator never saturating. Steady-state deviation fell monotonically with
engine speed - 708 mbar median at 1500-1800 rpm, 22 mbar at 2500-3000 - which
is spool-up, not a fault.

The code self-healed and the warning request was withdrawn without anyone
clearing anything.
