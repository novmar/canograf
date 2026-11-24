# Waveshare ESP32-S3-Touch-LCD-4.3B

Revision **1.1** (printed inside the enclosure). SKU 27848, or 28141 with case.

Everything with a number in it was measured on this board unless marked
otherwise.

---

## What is on it

| | |
|---|---|
| Module | `ESP32-S3-WROOM-1-N16R8`, dual Xtensa LX7 at 240 MHz |
| Memory | 16 MB flash, 8 MB PSRAM (octal) |
| Display | 4.3" IPS, 800x480, RGB interface, ST7262, 550 cd/m2 |
| Touch | GT911, capacitive, 5 point, tempered glass |
| CAN | TJA1051T/3 |
| RS485 | SP3485 |
| Expander | CH422G on I2C |
| RTC | PCF85063 with CR927 backup |
| Storage | microSD |
| Power | USB-C 5 V or 7-36 V DC |
| Battery | MX1.25 connector and CS8501 charger, **cell not included** |
| Audio | **none** - no codec, no amplifier, no I2S |
| Temperature | 0 to 65 C |

## Pin map

| Function | Pins |
|---|---|
| RGB panel | `0 1 2 3 5 7 10 14 17 18 21 38 39 40 41 42 45 46 47 48` |
| Touch | `4` IRQ, `8` SDA, `9` SCL |
| USB | `19` D-, `20` D+ |
| microSD | `11` MOSI, `12` SCK, `13` MISO |
| RS485 | `43` RX, `44` TX |
| **CAN** | **`15` TX, `16` RX** |
| RTC | `8` / `9`, shares the I2C bus |
| Flash + octal PSRAM | `26` to `37` |

Through the CH422G expander: `EXIO0` digital input 0, `EXIO1` touch reset,
`EXIO2` **backlight enable**, `EXIO4` SD chip select, `EXIO5` digital input 1,
`OD0` / `OD1` digital outputs.

### Exactly one pin is free

```
GPIO6
```

Everything else is taken. Plan accordingly - this is the only option for any
added signal, including a hardware backlight dimming modification.

## Terminal block

Order along the edge:

```
7-36V   VIN  GND
I2C     VOUT GND SDA SCL
CAN     L    H
RS485   B    A
I/O     DO0  DO1  DI_COM  GND  DI0  DI1
```

`VOUT` is 5 V or 3.3 V selected by an on-board resistor. Digital outputs are
open drain through optocouplers, 5-36 V, 450 mA sink. Digital inputs are
bidirectional optocouplers; `DI_COM` decides the mode - floating for dry
contact, to supply positive for NPN, **to supply negative for PNP**, which is
what an ignition-sense wire needs.

## Three switches, easy to confuse

| Switch | Location | Purpose |
|---|---|---|
| Battery | on the outer edge | connects the lithium cell |
| CAN termination | next to the CAN terminals | 120 ohm across CANH and CANL |
| RS485 termination | next to the RS485 terminals | same for RS485 |

**How to tell whether CAN termination is on**, without opening anything:
measure resistance between CANH and CANL with power off.

- **about 28 kohm** - termination **off**. This is the TJA1051 differential
  input impedance and it is always there.
- **about 120 ohm** - termination on.

For a vehicle it must be **off**; the bus already has two terminators.

## Power

Catalogue figure is 5 V / 450 mA. The schematic notes the panel alone draws
close to 550 mA, so budget **600-700 mA at 5 V, roughly 3 W**.

From 12 V through the wide-range input, allowing for converter efficiency,
that is about **260 mA**, or **6.2 Ah per day**. On an 82 Ah vehicle battery
that becomes a starting problem in under a week. Use ignition sensing or unplug.

A 24 V / 0.35 A supply (8.4 W) is comfortable for bench work.

**When powering from a computer USB port, switch the battery off.** Panel at
550 mA plus 580 mA charging exceeds what a port will give.

## Backlight cannot be dimmed in software

```
ESP_PANEL_BOARD_BACKLIGHT_TYPE = ESP_PANEL_BACKLIGHT_TYPE_SWITCH_EXPANDER
ESP_PANEL_BOARD_BACKLIGHT_IO   = 2        (EXIO2 on the CH422G)
```

It is a switch on an I2C expander. Measurements from the software PWM attempt:

| | |
|---|---|
| One toggle over I2C | **174 us** - the bus is not the limit |
| 100 Hz, any duty below full | black |
| 50 Hz, any duty below full | black |
| 25 Hz at 70 % (28 ms on) | barely flickers into life |
| 25 Hz below 70 % | black |

The AP3032 boost driver needs on the order of tens of milliseconds to light up.
Useful dimming would require under 30 Hz, which flickers visibly.
**Software PWM is a dead end on this board.**

The hardware fix is to cut `EXIO2` from the driver control pin and drive it from
`GPIO6` with LEDC. Not attempted here.

There is no contrast control. On an RGB panel there is no register for it.

## Flashing

Enumerates as `303a:1001`, the ESP32-S3 built-in USB Serial/JTAG - there is no
bridge chip, so the port is `/dev/ttyACM*`, not `/dev/ttyUSB*`.

Reading flash over this interface is unreliable; `flash-id` and `read-flash`
often fail halfway. Writing works. If upload fails with
`Packet content transfer stopped`, something else has the port open - check
before blaming the board.
