# Particulate filter regeneration - when it is allowed

Scope: Fiat Talento 2020 = Renault Trafic III (X82) phase 2, 2.0 dCi, engine
control unit **Bosch MD1CS006**, software `7A00 / 5P00`, UDS on `7E0` / `7E8`.

Three sources are used and they are **never mixed**:

| Tag | Meaning | Weight |
|---|---|---|
| **[ECU]** | Read out of `MD1CS006_7A00_5P0_V1.0_20200619T135243.json` in `ddt4all/ecu.zip`, plus its `.layout` | Strongest. This is what the unit itself implements. |
| **[DOC]** | Manufacturer or tool documentation (Renault X82 ph2 owner manual, CLIP scenario reimplemented in `pyren`) | Strong, but describes the procedure, not the calibration. |
| **[LIT]** | General diesel literature and workshop practice | Weakest. Generic numbers, quoted with a source. |
| **[FORUM]** | Reports from people who did it | Anecdote. Useful as a sanity check only. |

Every identifier below was looked up in the ECU file. Anything not present is
marked **not found**.

---

## 1. What the unit checks and reports by itself

This is the important part of the whole document: **the unit does not only
refuse, it says why.** We do not have to guess thresholds for most of it.

### 1.1 `2550` - CSF - Regeneration authorization status [ECU]

Request `22 2550`, reply `62 2550 xx`, 1 bit, bit 0 of the data byte.

| Value | Meaning in the file |
|---|---|
| `0` | off |
| `1` | on |

French comment in the file: `FaP - Regenerations autorisees --- interdites`
("regenerations authorized / forbidden"). So `0` = forbidden, `1` = authorized.
It is a single global gate and it carries **no reason**.

### 1.2 `2B01` - Variable indicating Manual CSF statemachine state value [ECU]

Readable with `22 2B01`, **writable** with `2E 2B01 xx` (reply `6E 2B01`).
Internal name `Vnx_pft_stt_rgn_mnl_mod`.

| Value | Meaning in the file |
|---|---|
| `0` | manual RGN to charge state |
| `32` | manual - charge to RGN state |
| `255` | state machine |

`255` is the normal case: the unit runs its own state machine. `32` forces the
transition loading -> regeneration, `0` forces regeneration -> loading. The
values line up with the state machine encoding in `254F` below (`32` =
`Nsx_pft_rgn_req`).

This matters because the DDT4All screen **`Fap state`** does **not** use the
routine at all. Its only writable field is `2B01`, and its button sends
`StartDiagnosticSession.ExtendedDiagnostic` followed by
`DataWrite.Variable indicating Manual CSF statemachine state value`. [ECU, layout]

The routine path is on a different screen, **`Routines UDS`**, whose buttons send
`10 03` and then `31 01 02 0D` / `31 02 02 0D` / `31 03 02 0D`. [ECU, layout]
The routine answers with `RoutineStatusRecord`, 4 bits:
`0` routine completed, `1` routine aborted or disabled, `2` routine running.

So there are **two different ways to force it** and they are not the same
mechanism. The routine is the after-sales one; `2B01` pokes the state machine
directly.

### 1.3 `246D` - CSF - After-sales regeneration status [ECU] - the reason code

This is the single most valuable identifier found. Request `22 246D`, one byte.

| Value | Meaning in the file |
|---|---|
| `0` | Waiting status (initialization in RGN_REQ_ASD state) |
| `1` | Successful RGN (initialization in RGN_END state) |
| `2` | Failure on threshold of minimal CSF upstream temperature during RGN state |
| `3` | Failure on threshold of minimal temperature before turbine during RGN state |
| `4` | Failure on threshold of maximal CSF temperature reached during RGN state (thermal shock) OR Failure on the exhaust line fuel injector during RGN state |
| `5` | Threshold of engine cooling temperature not reached |
| `6` | Threshold of differential pressure not reached |
| `7` | Engine speed deviation |
| `8` | Engine in stalled state |
| `9` | Engine in stopped state |
| `10` | Force RGN release forbidden regeneration |
| `11` | Urea injection disabled during the routine |
| `12` | NOx sensor disabled during diagnosis |
| `13` | Conditions for urea quality diagnosis unfulfilled |
| `14` | Diagnosis done and ok |
| `15` | Failure on threshold of minimal CSF downstream NOx during RGN state |
| `16` | Failure on threshold of minimal SCR downstream NOx during RGN state |

Values `5`, `6`, `7`, `8`, `9`, `10` are precondition failures in plain words:
coolant too cold, differential pressure too low, engine speed wrong, engine
stalled, engine off, forcing refused. Values `2`, `3`, `4`, `15`, `16` are
in-run failures.

**Consequence for the user interface: we do not need to invent a vocabulary of
excuses. We read `246D` after a refused or aborted attempt and show the unit's
own sentence.** The open question that only the vehicle can answer is whether
`246D` is already populated *before* a start attempt or only after one. That is
a two-minute test: read it cold, start, read it again.

### 1.4 `25B3` - Indicator of success or failure of VRAFS regeneration [ECU]

VRAFS = "regeneration apres-vente en roulant", after-sales regeneration **with
the vehicle driving**. One byte, seven named bits, `1` = condition fulfilled,
`0` = not fulfilled.

| Bit | `0` means | `1` means |
|---|---|---|
| 0 | Minimum vehicle speed condition not fulfilled | fulfilled |
| 1 | Tester connection condition not fulfilled | fulfilled |
| 2 | Maximum time in idle speed condition not fulfilled | fulfilled |
| 3 | Engine state condition not fulfilled | fulfilled |
| 4 | Maximum regeneration time condition not fulfilled | fulfilled |
| 5 | Minimum soot mass condition not fulfilled | fulfilled |
| 6 | Maximum soot mass condition not fulfilled | fulfilled |

Bit numbering: the file gives `firstbyte 4` with `bitoffset 7..1`, which under
the DDT4All convention `(byte >> (8 - offset - width)) & mask` maps offset 7 to
bit 0 and offset 1 to bit 6.

This is a **second, finer-grained reason register**, and it proves the unit
distinguishes a *driving* after-sales regeneration from a stationary one. It
also confirms the unit watches the tester connection: if the diagnostic client
goes away, the condition drops.

### 1.5 Soot-mass gates [ECU]

| DID | Name in file | `0` | `1` |
|---|---|---|---|
| `25AE` | Boolean indicating if the soot mass is low enough to authorize an after-sales DPF regeneration | soot mass too high | soot mass enough low |
| `25B5` | Boolean indicating if the soot mass is low enough to authorize a vehicule running after-sales DPF regeneration | deactivated | activated |

Both are single bits. Note the asymmetry: there is a separate authorization for
the stationary case and for the driving case. The thresholds themselves are
**not found** as readable identifiers.

`25AF` - "Boolean indicating the will to force an after-sales DPF regeneration",
`0` = No aftersales forced RGN request, `1` = Aftersales forced RGN request. This
is the request flag, readable, and it should become `1` once a start succeeds.

### 1.6 `24D8` - particles filter status during after sales request [ECU]

| Value | Meaning in the file |
|---|---|
| `0` | Unknown status, second regeneration request |
| `1` | The particles filter is clean |
| `2` | The particles filter need to be changed (it is clogged up or broken) |

`2` is a hard stop: the unit has already concluded the filter is beyond burning.
`24DB` carries the same three-way enumeration for the load-speed reference.

### 1.7 State machines and history [ECU]

| DID | Name | Values |
|---|---|---|
| `254F` | CSF - CSF statemachine actual state | `0` Nsx_pft_load, `8` Nsx_pft_spd_req, `16` Nsx_pft_chk_temp, `24` Nsx_pft_cata_hot, `32` Nsx_pft_rgn_req, `56` Nsx_pft_rgn_rest, `64` Nsx_pft_rgn_ok, `80` Nsx_pft_rgn_fail |
| `2434` | CSF statemachine status for aftersale diagnosis regeneration | `0` Waiting, `1` Heating, `2` Regeneration, `3` Cooling, `4` Successful regeneration, `5` Failure |
| `25B4` | CSF statemachine status for VRAFS diagnosis regeneration | 3 bits, **no value list in the file** |
| `2486` | CSF - Progress level of the after-sales regeneration | 1 byte, step 0.3921569, `%` |
| `246B` | CSF history - 20 last stored CSF statemachine states | `0` charging, `1` road request, `2` check temperature, `3` catalyst heating, `4` regeneration request, `5` temperature limitation, `6` regeneration, `8` regeneration success, `10` regeneration failure, `12` spontaneous regeneration |
| `2551` | CSF - Engine operating point | `0` Nul, `1` Cold, `2` Medium, `3` Hot |
| `2056` | Combustion mode | `1` regular injection, `2` start particulate filter regeneration, `3` particulate filter regeneration, `4` particulate filter protection, `7` starting, ... |

`2434` plus `2486` together are the live progress display for a stationary
after-sales regeneration: phase and percentage. `pyren`'s CLIP scenario uses
exactly this shape - a phase state, a result state, and a loop. [DOC]

### 1.8 Why the last ones started and stopped [ECU]

| DID | Name | Values |
|---|---|---|
| `25A2` | Last 10 reasons for starting a DPF regeneration | `0` others reasons, `1` starting due to high soot mass, `2` regeneration restart, `3` start due to a speed request, `4` start due to a NOx-trap need (DeSOx need), `5` start due to a SCR request |
| `255C` | Last 10 reasons for stopping a DPF regeneration | `0` (unnamed), `1` engine stopped or stalled, `2` normal regeneration stop, `3` maximum regeneration time reached, `4` low temperature conditions, `5` combustion impossibility to continue regeneration, `6` end of NOx-Trap request |
| `24A4` | CSF - 10 last values of regeneration failure status | `0` temperature sufficient, `1` temperature unsufficient |
| `2467` | CSF history - 10 last stored values of synthesis of criteria for each regeneration demand | `0` soot mass, `1` simulated soot mass, `2` driven distance, `3` external event |

All four are ten 3-bit or 1-bit fields packed into a 10-byte payload, entry
`.00` at the last byte and `.09` at the first. **The file does not say which end
is the most recent** - that has to be established on the car by triggering one
regeneration and watching which slot changes.

`255C` is the post-mortem counterpart of `246D`. After the P2263 incident this
vehicle should have a `255C` slot explaining how that regeneration ended.

### 1.9 Other regeneration-relevant identifiers found [ECU]

| DID | Name | Scaling |
|---|---|---|
| `2010` | Engine status | `0` engine stopped, `16` engine driven, `32` engine stalled, `48` engine autonomious |
| `2439` | Temperature before turbine | 2 B, 0.1, offset -273, C |
| `2438` | Temperature before turbine setpoint | 2 B, 0.1, offset -273, C |
| `2542` | CSF - Particulate differential pressure | 2 B, offset -32768, hPa |
| `2435` / `2436` | CSF differential pressure before / after after-sales regeneration | 2 B, offset -32768, mbar |
| `2430` / `2431` | CSF volumic air flow before / after an after-sales regeneration | |
| `2485` | CSF - Indicates the number of regeneration failures since the last sucess | 1 B |
| `2554` | CSF - Distance since last regeneration succes Partial or total to allow regeneration | 2 B, km |
| `2558` | Distance since last regeneration end | 2 B, km |
| `2492` | CSF diagnosis inhibitions state | `0` not inhibited, `1` inhibited |
| `2124` | Combustion mode - particulate filter regeneration authorization state | `0` forbidden, `1` authorized |
| `2127` | Combustion mode - Start particule filter regeneration mode authorization state | `0` forbidden, `1` authorized |
| `2123` | Combustion mode - particulate filter protection authorization state | `0` forbidden, `1` authorized |
| `24BE` | CSF - Maximum oil dilution rate during the calibrated distance | 2 B, 0.001525902, `%` |
| `24EC` | CSF - Oil dilution raw rate | 4 B signed, 1e-06, offset 2147.83648, `%` |
| `281B` | Fuel low level | `0` ok, `1` low |
| `287F` | Memorized variable of the fuel level | 1 B, raw, **no unit in the file** |
| `F42F` | `$01 - Fuel Level Input` (OBD PID `2F`) | 1 B, 0.3921568627, `%` |
| `2028` | MIL lamp request state | `0` off, `1` on, `2` blinking, `3` auto-test |
| `2057` | Alternator power | 2 B, step 10, W |
| `2007` | Oil temperature - engine | 2 B, 0.1, offset -273, C |
| `20DE` | Ambient temperature | 2 B, 0.1, offset -273, C |
| `FD25` | Forced regeneration demand Switch (`DpfRgnReq_stMonDDSens`) | 4 B raw |
| `FD5A` / `FE7F` | Forced regeneration LED command, raw / physical | |

The device list in the same file contains **"DPF forced regeneration switch
located on the dashboard"**, which is the driver-side manual regeneration
described in section 3.

### 1.10 What is NOT in the file

Searched, **not found**:

- Any readable calibration giving the **minimum coolant temperature** for a
  regeneration. Only `246D = 5` tells you it was not reached.
- Any readable **minimum fuel level** threshold.
- Any readable **soot mass window** (min/max grams). Only the booleans `25AE`,
  `25B5` and the `25B3` bits.
- Any readable **minimum/maximum engine speed** for the forced routine.
- A **negative response code vocabulary** specific to this routine. The unit
  uses the standard UDS set; this vehicle has already answered
  `7F .. 22 conditionsNotCorrect` to a write attempted with the engine running,
  so `22` is the expected refusal for a precondition failure.

The only thresholds that exist as named objects are `24A6` (threshold on
estimated combusted soot mass for regeneration end detection) and `24A7` (same,
for partial regeneration detection) - both are *end* criteria, not *start*
criteria.

---

## 2. General conditions from the literature

Generic, not Renault-specific. Each line: the value, why it exists, where it
came from.

| Condition | Value quoted | Why | Source |
|---|---|---|---|
| Coolant temperature | above 80 C, or 70 C minimum | A cold engine cannot hold the post-injection heat balance; the exhaust never reaches the 600 C+ the soot needs and the regeneration fails or aborts early. | mechanicwiki.com "How to Force DPF Regeneration on Hyundai County"; dieselschrauber.org VCDS article (70 C, measuring block 2) |
| Coolant temperature, heavy duty | 160 F (71 C) minimum, 170-200 F preferred; Detroit refuses below 150 F | Same reason, stated as an OEM refusal threshold. | heavydutytruckdiagnostics.com "What You Need for DPF Regens" |
| Fuel level | at least 1/4 tank, or >= 25 % | Regeneration burns extra diesel through post injection. Running dry mid-cycle aborts the burn and leaves a half-processed filter. | mechanicwiki.com; obdllm.com "Forced DPF Regeneration with a Scan Tool" |
| Engine oil level | at least the minimum mark | Post injection dilutes the oil with diesel. Starting low makes a bad situation worse. | obdllm.com |
| Oil dilution | do not force with heavily diluted oil | Diluted oil can lead to a hydraulic lock or a runaway. | mechanicwiki.com "Force DPF Regeneration on Toyota Fortuner" |
| No active DTCs | no active faults on EGR, boost, injectors, exhaust temperature sensors | Those are exactly the actuators the regeneration uses. A unit with a live fault in one of them will refuse or will abort. | mechanicwiki.com (Hyundai County, Toyota Fortuner) |
| Soot mass window | forced regeneration in roughly 24-40 g; above ~40-45 g do not force | Too little soot and there is nothing to burn; too much and the exotherm can melt the substrate. | mechanicwiki.com; obdllm.com ("do not run at soot > 45 g") |
| Soot mass vs method | with ~30-40 g the burn can be done stationary or driving; higher, driving only | A moving vehicle carries away heat and supplies air flow, which bounds the peak temperature. | dieselschrauber.org VCDS article |
| Location | on asphalt, 10 m from buildings, nothing combustible nearby | Exhaust reaches 650-700 C. | obdllm.com |
| Duration | 30-60 min depending on starting soot load | | heavydutytruckdiagnostics.com |
| Stable battery voltage, tester connected, vehicle stationary | stated as a precondition set | A dropout kills the routine mid-burn. | diagwiki.com "Renault Nissan Diesel Particulate Filter DPF Regeneration" (RenCOM) |

**These gram figures are generic diesel numbers and must not be applied to this
vehicle.** This filter was read at 5.72 g and the vehicle document already shows
that `242C` on this unit is a temperature-driven model output that swung between
7.7 and 15.7 g in one hour of driving with no regeneration running. Any absolute
gram threshold imported from a VAG or truck article is meaningless here. The
unit's own `25AE` / `25B5` booleans are the correct gate.

---

## 3. What applies to this engine

### 3.1 Must the vehicle stand still or drive? Both exist. [DOC]

The Renault X82 phase 2 owner manual (chapter links `X82PH2`, "Special features
of Diesel versions") documents a **stationary manual regeneration** that the
driver starts from the instrument cluster:

- park off the road, at least **three metres** from anything that can ignite or
  melt;
- apply the parking brake;
- gear lever in neutral (N on automatic);
- engine running;
- deactivate Stop and Start;
- instrument panel menu, "Vehicle" tab, press 3 or 4 until
  "Regenerate DPF [long press]" appears, then hold 2 until
  "DPF regeneration Ongoing" / "Regen 40min-to stop press brake".

During it: engine speed first rises to **about 3000 rpm** to heat up, then
settles to **about 2000 rpm**; the cooling fans run; white smoke and smell are
normal. It lasts **about 40 minutes**. Pressing any pedal interrupts it and the
regeneration is then **partial**.

The manual states the regeneration is **not possible**:

- if the fuel level is too low;
- if the engine is too cold;
- in extreme weather (cold, hot, or very high altitude);

and it says not to start it at all with insufficient oil or coolant level.
If conditions fail before or during the burn, the cluster shows
**"DPF Regeneration Interrupted"** and cancels.

The same manual documents the **driving** regeneration separately: when the
filter lamp comes on, keep driving and **do not let engine speed fall below
2000 rpm**; the lamp goes out after 10-20 minutes. The tip is a 20-minute main
road run every 200 km.

Source: `user-manual.renault.com`, Renault Trafic, "Special features of Diesel
versions", X82PH2 chapters.

This matches the ECU file exactly: `FD25` "Forced regeneration demand Switch",
`FD5A` / `FE7F` "Forced regeneration LED command", and the device name
"DPF forced regeneration switch located on the dashboard" are the dashboard
path. [ECU]

**So for this vehicle: the documented manual regeneration is STATIONARY, engine
running, parking brake on, neutral, outdoors.** The ECU additionally implements
a *driving* after-sales regeneration (VRAFS, `25B3` / `25B4` / `25B5`) whose
very first condition bit is "minimum vehicle speed". Which of the two the
`31 01 02 0D` routine triggers is **not stated in the file**. The reason list of
`246D` ("Engine in stopped state", "Engine speed deviation", "Threshold of
engine cooling temperature not reached") reads like the stationary one, and
`25B3` is clearly the driving one. This should be resolved by reading `2434`
and `25B4` while a routine runs.

### 3.2 What the Renault tool does [DOC]

`pyren/pyren/scen_ecri_fap5.py` is a reimplementation of the CLIP scenario
`scen_ecri_fap5`. Its logic, in order:

1. Load the scenario XML and read limits from it:
   `AltValueMin` / `AltValueMax` for **alternator power**, and
   `s_masse_suie_actif` / `s_masse_suie_max` for the **soot mass window**.
2. Show three fixed warning screens (message ids 1144, 1145, 1146, then 1147,
   1148) before anything else.
3. Loop on a **condition check** until engine state equals `TOURNANT`
   (running), displaying engine state, soot mass and alternator power live.
   The operator may override with `A`.
4. Ask for an explicit `yes`.
5. Send the start command, then poll phase and result states until a terminal
   phase, with `Q` sending the stop command.

So the official tool gates on: **engine running**, **soot mass inside a
calibrated window**, **alternator power inside a calibrated window**, operator
confirmation. The numeric limits and the warning texts live in the CLIP
database, which is **not present in this repository** - the names are known, the
values are not.

The engine-state object maps onto `2010` "Engine status", value `16`
"engine driven". Alternator power maps onto `2057`, step 10, W. [ECU]

### 3.3 Community reports [FORUM]

- A Renault forum thread on the DDT4All "Stan DPF/FAP" screens reports forcing a
  regeneration at about **8 g** of soot by activating the option on the
  `CSF_component_5` screen, with the burn then happening **while driving** and
  the flag clearing itself afterwards.
  (megane.com.pl thread "DDT4ALL Stan DPF/FAP", page 3.) Treat as anecdote: it
  is a Megane-family car, not an X82, and the screen named there is not the one
  that writes `2B01`.
- Renault owner forums repeatedly warn that repeated tool-forced regenerations
  shorten filter life and burn less effectively than a proper motorway run.
  (renaultforums.co.uk, "How to force regeneration on fap/dpf".)
- Renolink's walkthrough for Renault forced regeneration describes engine speed
  rising and holding at **2000-2500 rpm**, which is consistent with the Renault
  manual's 3000 then 2000 rpm. (renolink.store.)

No X82-specific, MD1CS006-specific forum account of `31 01 02 0D` was found.
That gap is real and should be stated to the user rather than papered over.

---

## 4. Proposed pre-flight check

Rule: **read everything first, decide, then offer.** If anything in the
"mandatory" group fails, the primary button must not start a regeneration; it
opens the second dialog listing the concrete failures. Every message below
contains the measured value and the required value.

Column "today" = the identifier is already documented and read in
`docs/vehicles/x82-trafic3-talento.md`. "add" = we have to add the read.

### 4.1 Mandatory - do not offer at all if these fail

| # | Read | Condition | Today? | Message |
|---|---|---|---|---|
| 1 | `2010` Engine status | must be `16` "engine driven" | add | "Engine is not running - the unit reports '<state>'. Start the engine and let it idle." |
| 2 | `2550` Regeneration authorization status | must be `1` | add | "The engine unit reports regeneration forbidden (2550 = off). It will refuse the request." |
| 3 | `25AE` soot mass low enough for after-sales regeneration | must be `1` | add | "Soot load is outside the window the unit accepts for a forced burn (25AE = soot mass too high)." |
| 4 | `24D8` filter status during after sales request | must not be `2` | add | "The unit has recorded the filter as clogged or broken (24D8 = needs changing). A forced burn will not fix it." |
| 5 | `2001` coolant temperature | >= 70 C, warn below 80 C | today | "Coolant is 61 C, at least 70 C is needed." [LIT threshold] |
| 6 | Fuel: `281B` low-fuel flag `0`; if `287F` / `F42F` prove usable, >= 25 % | | add | "Fuel level is low - the unit flags a low tank. Refuel to at least a quarter before starting; the burn needs extra diesel." |
| 7 | `19 02 01` confirmed faults | no confirmed emission-relevant fault | today | "Active fault <code> <name>. Regeneration uses the same air path - fix this first." |
| 8 | Outdoors and clear, parking brake on, neutral | cannot be read - explicit user confirmation | n/a | "Confirm: outdoors, at least 3 m clear of anything flammable, parking brake on, gearbox in neutral, nobody near the exhaust." [DOC] |

Items 1-4 are the unit's own opinion and are not negotiable. Item 5 uses a
literature threshold because **the ECU file exposes no readable coolant
threshold** - only the after-the-fact code `246D = 5`. Item 6 likewise: the
quarter-tank figure is literature, the Renault manual only says "too low".

### 4.2 Strongly recommended - offer, but say it in the second dialog

| # | Read | Condition | Today? | Message |
|---|---|---|---|---|
| 9 | `2003` vehicle speed | `0` for the stationary procedure | today | "Vehicle is moving at 14 km/h. The documented procedure for this van is stationary." |
| 10 | `2005` battery voltage | >= 13.0 V with the engine running | today | "Battery 12.1 V with the engine running - the alternator is not charging properly. A 40 minute burn needs a stable supply." |
| 11 | `2057` alternator power | plausible, non-zero | add | "Alternator output reads 0 W. The Renault procedure checks this before starting." [DOC] |
| 12 | `2442` CSF upstream temperature | plausible, moving with load | today | "The temperature sensor before the filter reads -40 C. Without it the unit cannot control the burn." |
| 13 | `2028` MIL lamp request state | `0` off | add | "The engine warning lamp is requested on. Read the fault memory first." |
| 14 | `2485` regeneration failures since last success | `0`; if > 0 show the count | add | "The last 3 regenerations failed. 24A4 says the temperature was insufficient - a forced attempt is likely to fail the same way." |
| 15 | `211B` oil dilution rate | show it; no threshold is readable | today | "Oil dilution is 6.2 %. Each regeneration adds to it. Check the oil level and consider a change first." [LIT reasoning] |
| 16 | `2492` CSF diagnosis inhibitions state | `0` not inhibited | add | "Filter diagnostics are currently inhibited in the unit - readings may not be trustworthy." |
| 17 | `20DE` ambient temperature | plausible, flag extremes | add | "Outside temperature -18 C. The manual says regeneration is not possible in extreme cold." [DOC] |
| 18 | `2557` / `2558` time and distance since last regeneration end | informational | today | "Last regeneration ended 42 km ago. Another one now will not achieve much." |

### 4.3 Live during the run, and the refusal path

| Read | Use | Today? |
|---|---|---|
| `2434` after-sales state machine | phase: waiting / heating / regeneration / cooling / success / failure | add |
| `2486` progress level, `%` | progress bar | add |
| `246D` after-sales regeneration status | **the reason, when it refuses or fails** | add |
| `25B3` VRAFS condition bits | per-condition reason, if the driving mode is the one in play | add |
| `2487` current regeneration duration | elapsed | today |
| `2442` / `2441` / `2432` upstream, downstream, setpoint | is it actually heating | today |
| `2559` ratio of soot burnt | did it achieve anything | today |
| `255C` last 10 stop reasons | post-mortem | add |
| `31 03 02 0D` RoutineStatusRecord | `0` completed, `1` aborted or disabled, `2` running | today |

If the start request returns `7F 31 22` (conditionsNotCorrect), read `246D`
immediately and show its text verbatim. That is the unit telling us the reason,
and it beats anything we could infer.

---

## 5. What can go wrong

Factual, for the warning dialog. No drama.

- **An interrupted regeneration leaves a partly burnt filter.** The soot that
  did not burn stays in the channels, the next attempt starts from a worse
  position, and the unit counts a failure (`2485`, `24A4`). On this vehicle the
  manual is explicit: pressing a pedal during the stationary procedure makes the
  regeneration partial. [DOC]
- **Fuel ends up in the oil.** Post injection puts diesel past the rings. The
  unit models this (`211B`, `24BE`, `24EC`). Repeated or repeatedly interrupted
  regenerations raise oil level and lower its viscosity; literature warns that a
  heavily diluted oil is a hydraulic-lock and runaway risk. [LIT]
- **The exhaust gets hot enough to start a fire under the van.** 600 C and more
  at the filter. The manual's three-metre rule and the "do not leave the vehicle
  unsupervised" instruction exist for that reason. [DOC]
- **Running with too much soot can overheat the substrate.** The unit guards
  this with `25AE` / `25B5` and reports a thermal-shock failure as `246D = 4`.
  Overriding the unit's own soot gate is the one thing we must never offer. [ECU]
- **A regeneration stresses more than the filter.** This is not hypothetical on
  this vehicle. `P2263-22` (Turbocharger Boost Control Function) was set at
  92208 km, and `25AD` shows the regeneration then running had started at
  92199 km - **the fault appeared 9 km into a running regeneration**. During a
  regeneration the air path is driven for exhaust temperature, not for boost:
  the intake throttle closes, EGR shifts, post injection runs. A sudden full
  load demand in that state produces a boost deviation the monitor can read as a
  fault, and this unit does **not** inhibit the boost diagnostic during
  regeneration - proven by the fact that the code was set at all. Expect that a
  regeneration can set unrelated codes, and read the fault memory before and
  after so the difference is attributable.
- **A regeneration started and abandoned is worse than none.** If the engine is
  switched off mid-burn, `255C` records "engine stopped or stalled" and the
  failure counter increments. Forty minutes must be available before the button
  is offered at all.

---

## 6. Conclusion

**The three checks without which we should not offer a regeneration at all:**

1. **`2010` Engine status = 16, "engine driven".** The unit refuses otherwise
   and reports it as `246D = 9` ("Engine in stopped state") or `246D = 8`
   ("stalled"). This vehicle has already answered `7F .. 22` to a write made
   under the wrong engine state, so the unit demonstrably enforces its own
   preconditions. [ECU + observed on this vehicle]
2. **`2550` Regeneration authorization status = 1, combined with `25AE` = 1 and
   `24D8` != 2.** These are the unit's own "may I" flags: globally allowed, soot
   mass inside the accepted window, and the filter not already written off.
   Starting against them is the only way to turn a service action into damage.
   [ECU]
3. **Coolant at or above 70 C, with no confirmed emission-relevant fault in
   `19 02 01`.** The coolant number is literature, not ECU calibration - the
   unit exposes no readable threshold, only the after-the-fact code
   `246D = 5` ("Threshold of engine cooling temperature not reached"). The fault
   check is the part the user will not do by themselves, and this vehicle has
   already shown that a regeneration running with a disturbed air path sets
   turbo codes. [LIT + ECU]

Everything else in section 4 belongs in the second dialog as a named, numeric
complaint, not as a blocker.

**Can the unit state the reason itself? Yes, and this is the main finding.**

- `246D` "CSF - After-sales regeneration status" is a 17-value enumeration whose
  values `5` to `10` are precondition failures in plain language - coolant
  threshold not reached, differential pressure threshold not reached, engine
  speed deviation, stalled, stopped, forcing forbidden - and whose values `2`,
  `3`, `4`, `15`, `16` are in-run failures.
- `25B3` adds a seven-bit per-condition register for the driving variant:
  minimum vehicle speed, tester connection, maximum idle time, engine state,
  maximum regeneration time, minimum soot mass, maximum soot mass.
- `255C` records why each of the last ten regenerations ended.

So the second dialog should **not** be built on guessed thresholds. It should be
built on these three registers, with our own numeric checks (coolant, fuel,
faults) only filling the gaps the unit does not expose before a start attempt.

Two things must be settled on the vehicle before the feature ships:

1. Is `246D` populated **before** a start attempt, or only after one? If before,
   it becomes the primary pre-flight read and most of section 4.1 becomes a
   fallback.
2. Does `31 01 02 0D` trigger the **stationary** after-sales regeneration
   (`2434`, `246D`) or the **driving** one (`25B4`, `25B3`)? Read both state
   machines while the routine runs.
