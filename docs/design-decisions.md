# KK2.1.5 MP3 Player: Design Decisions

Project: repurpose a KK2.1.5 flight controller board as an MP3 player.

## Goal

Play MP3 files from a microSD card, controlled with the board's four buttons and
shown on its LCD, with audio out to wired earphones.

## Hardware

| Part | Notes |
|---|---|
| KK2.1.5 board | ATmega644PA @ 20 MHz external crystal, 64 KB flash, 4 KB RAM, 2 KB EEPROM. 128x64 LCD, 4 buttons, buzzer, LED, MPU-6050 (unused). |
| DFPlayer Mini | Handles the microSD card and MP3 decoding. The ATmega cannot decode MP3 in software. |
| microSD card | FAT32. Tracks stored as `/mp3/0001.mp3`, `0002.mp3`, ... |
| Wired earphones | From the DFPlayer's DAC_L / DAC_R / GND to a 3.5 mm jack, about 100 ohm in series per channel. No speaker. |

DFPlayer link: 9600 baud serial, 10-byte command packets. The module uses 3.3 V logic,
so KK2 TX goes to module RX through a 1 kohm resistor. Module needs a stable 5 V.

## Decisions

- **Decoder:** DFPlayer Mini (chosen over VS1053 for simplicity; KK2 only sends serial commands).
- **Audio out:** wired earphones via the DAC pins. No speaker.
- **Bluetooth:** deferred. It would need an A2DP transmitter module fed from the same DAC pins. The ATmega and serial modules like HC-05 cannot send audio.
- **Toolchain:** arduino-cli with the MightyCore board package (ATmega644P @ 20 MHz).
- **Upload method:** upload using a programmer through the Uno-as-ISP. No bootloader is installed. Fuses are left unchanged.
- **LCD library:** U8g2 (ST7565-family controller). To be confirmed in Stage 2.
- **Track display:** track numbers only (no song titles), so the DFPlayer is kept. The LCD shows track number, volume and play state.
- **Features:** repeat/loop, remember last state, low-battery warning, buzzer feedback. See "Feature notes" and "Buzzer feedback". Shuffle was dropped.
- **Power:** undecided. The module is sensitive to supply noise, so a battery pack is likely cleaner than a USB power bank.

## Button mapping

| Button | Short press | Long press (about 0.8 s) | Held |
|---|---|---|---|
| Play/pause | play or pause | cycle repeat: off, all, one | none |
| Next | next track | previous track | none |
| Vol + | +1 | none | repeats about every 150 ms |
| Vol - | -1 | none | repeats about every 150 ms |

A long press gives a beep so it can be confirmed without looking at the LCD.
Repeat mode is shown as an icon on the LCD. "Stop" was dropped; pause does the same job.

Which physical button gets which role is decided in Stage 2, after the real pin
mapping is confirmed. Suggested: ENTER for play/pause, the rest by position, with vol+ and vol- adjacent.
Timings are tunable constants.

Button handling: a 1 kHz timer interrupt samples the buttons and runs a debounce
state machine (idle, pressed, held, released), so the LCD and serial link never block it.
Volume clamped to the DFPlayer range 0 to 30, starting at a modest level.

## Feature notes

- **Repeat:** long-press play/pause cycles off, repeat all (`0x11`), repeat one (`0x19`),
  using the DFPlayer's own commands. Command codes to be verified against the actual module.
- **Remember last state:** store volume, track and repeat mode in EEPROM. Write only after the
  value has been stable for a couple of seconds, never on every volume step, to limit EEPROM wear.
  Restore at power-up after the DFPlayer's start-up delay.
- **Low-battery warning:** use the board's battery voltage sensing (ADC). Pin and divider
  ratio to be confirmed in Stage 2. It reads the battery connector, so it only reflects the
  real supply if the player is powered from that input. Warn on the LCD below a threshold set after testing.

## Buzzer feedback

Beeps from the board's buzzer confirm actions without looking at the LCD. The buzzer is
not in the earphone path, so it is heard by people nearby, not through the earphones.

| Event | Pattern (proposed) |
|---|---|
| Boot complete | 1 short beep |
| Long press registered (repeat change, previous track) | 1 medium beep |
| Volume at minimum or maximum limit | 1 very short beep |
| No SD card or DFPlayer error | 3 short beeps |
| Low battery | 2 beeps, repeated at a long interval |
| Critical battery, saving and stopping | 1 long beep |

- Patterns use on/off timing only, so they work whether the buzzer is active (fixed tone)
  or passive (needs a tone signal). Which type it is gets confirmed in Stage 2; a passive
  buzzer would allow different pitches.
- Beeps are driven from the timer interrupt, not with delays, so they never block the
  buttons, LCD or serial link.
- Short presses do not beep, only the events above.
- A setting to mute the buzzer is worth adding, once there is a menu. Defaults to on.
- Pattern lengths are tunable constants.

## LCD

Use the original layout for now. A better-looking style can be chosen later (alternatives
considered: large track number, animated equalizer, cassette, minimal, vinyl, dense info, card).

```
┌────────────────────────────┐
│ ▶ PLAY            ↻ ALL  ▮▮▯│   status row: play state, repeat, battery
│                            │
│          007               │   track number
│         of 042             │
│                            │
│ VOL  ████████░░░░░░░  18   │   volume bar and number
└────────────────────────────┘
```

- Play state: playing, paused, idle. Repeat: none, ALL, ONE. Battery icon, flashing "LOW" when low.
- Temporary messages: "Starting..." at boot, "No SD card", feedback for long presses
  ("Repeat: ALL", "Previous track"), "Saving..." before a critical low-battery shutdown.
- No elapsed time or progress bar: the DFPlayer does not reliably report position or length.
- No song titles (track numbers only).
- Backlight idle dimming and contrast setting: to be decided after the pin mapping (Stage 2).
- Any animation (equalizer, reels) would be decorative only, since the module gives no audio data.

## Programming the board

Programmer: Arduino Uno running the ArduinoISP sketch (`arduino-isp/`).

| Uno pin | KK2 ISP header pin |
|---|---|
| D12 | 1 MISO |
| 5V | 2 VCC |
| D13 | 3 SCK |
| D11 | 4 MOSI |
| D10 | 5 RESET |
| GND | 6 GND |

Pin 1 is the marked corner of the header. Optional 10 uF capacitor between the Uno's
RESET and GND, after uploading the sketch.

Verified working: signature `1E 96 0A` read with

```
avrdude -c stk500v1 -P /dev/cu.usbmodem14201 -b 19200 -p m644p -v
```

Use `-p m644p`. The PA variant shares the P's signature and `m644pa` may not exist in
older avrdude. Do not use `-F` (hides signature failures).

## Backup of the stock firmware

`backups/kk2-1-5-original/` holds the original flash, EEPROM, fuses (`lfuse 0xD7`,
`hfuse 0xD1`, `efuse 0xFC`) and lock bits (`0xFF`, unlocked), with `SHA256SUMS`.
Flashing new firmware erases the stock firmware; this backup can restore it.

Do not rewrite fuses from the backup unless the chip stops responding.

## Stages

1. Toolchain: install MightyCore, build and flash a blink and buzzer test through the Uno ISP.
2. Map the board's pins: LCD, buttons, buzzer, LED, and free pins for the DFPlayer serial link. Use the KK2 schematic or open-source firmware; do not guess.
3. LCD and button handling (U8g2, debounce, long press).
4. DFPlayer link: play, pause, next, previous, volume.
5. Player UI on the LCD.
6. Earphone output wiring.
7. Power and enclosure.

Stages 1 to 3 need no extra hardware. Stage 4 needs the DFPlayer Mini, a microSD card
and a 1 kohm resistor. Stage 6 needs a 3.5 mm jack and two 100 ohm resistors.

## Open questions

- Real pin mapping (Stage 2), including which UART or pins are free for the DFPlayer.
- Final power source.
- Whether to add Bluetooth later.
