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

Physical buttons, left to right: Back (PB7), Up (PB6), Down (PB5), Menu/Enter (PB4).
Proposed assignment (not final):

| Physical button | Role |
|---|---|
| Up | Vol + |
| Down | Vol - |
| Enter (rightmost) | Play/pause |
| Back (leftmost) | Next |

Up and Down are adjacent, so the volume buttons sit together. Timings are tunable constants.

## Pin map (ATmega644PA)

Sources: the stock KK2 firmware source (`daltonmatos/kk2-firmware`, files `hardware.asm`,
`setuphw.asm`, `io.c`, `sensorreading.asm`; no license, used only for pin facts) and a
second source, `aheyer/KK_Programmer_Replacement` (CC BY-SA 4.0), whose author traced the
LCD and button pins on a KK2.1HC with a multimeter. The two agree. Both are for the KK2
family, so confirm on this board with a test sketch before relying on it.

| Part | Pin(s) | Notes |
|---|---|---|
| LCD clock (SCLK) | PD4 | ST7565, SPI mode 3 (CPOL 1, CPHA 1); bit-banged in stock firmware |
| LCD data (MOSI) | PD1 | Also UART0 TX, so UART0 is not usable for the DFPlayer |
| LCD chip select | PD5 | |
| LCD reset | PD6 | |
| LCD A0 | PD7 | |
| Button Back | PB7 | Active low, internal pull-up. Shares the ISP SCK line |
| Button Up | PB6 | Active low. Shares the ISP MISO line |
| Button Down | PB5 | Active low. Shares the ISP MOSI line |
| Button Menu/Enter | PB4 | Active low |
| Buzzer | PB1 | Stock firmware only switches it on and off, so probably an active buzzer. To be tested |
| LED | PB3 | |
| Battery sense | ADC channel 3 (PA3) | Divider ratio not in the source; stock firmware calibrates with an offset |
| MPU-6050 | PC0 (SCL), PC1 (SDA) | I2C, unused |
| Receiver inputs | Throttle PD0, aileron/elevator PD2 and PD3, rudder PB0, aux PB2 | Aileron and elevator are INT0 and INT1 |
| Motor outputs M1 to M8 | PC6, PC4, PC2, PC3, PA4, PA5, PC7, PC5 | Spare general-purpose pins |

Consequences:

- **DFPlayer link:** use UART1, PD2 (RX) and PD3 (TX), on the aileron and elevator header
  pins. They are free if no receiver is connected. Which header pin is which still has to
  be found from the board labels or a continuity check.
- **Buttons and ISP:** PB5, PB6 and PB7 are the ISP MOSI, MISO and SCK lines. Do not press
  buttons while flashing, and keep them as inputs in the firmware.
- **Battery warning:** channel 3 reads the battery connector, so it reflects the real
  supply only if the player is powered from that input.
- **Confirmed on the board** with `hardware-test/` (flashed through the Uno ISP): the LED
  on PB3, the buzzer on PB1 and the four buttons on PB4 to PB7 work as mapped.
- **Still to test on the board:** buzzer type (active or passive; not yet recorded), exact
  physical button order, battery divider ratio, LCD controller variant and contrast, LCD
  backlight control (no pin found).

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

| Uno pin | KK2 ISP signal |
|---|---|
| D12 | MISO |
| 5V | VCC |
| D13 | SCK |
| D11 | MOSI |
| D10 | RESET |
| GND | GND |

The header is a 2x3 block. Its adjacent pairs are MISO next to VCC, SCK next to MOSI and
RESET next to GND. Pin numbers differ by source: the KK2.1HC author numbers it
1 MISO, 2 SCK, 3 RST, 4 VCC, 5 MOSI, 6 GND, while the standard AVR numbering is
1 MISO, 2 VCC, 3 SCK, 4 MOSI, 5 RST, 6 GND. It is the same physical arrangement, so rely
on the signal pairs, not the numbers. The wiring used here works (signature read verified).
Optional 10 uF capacitor between the Uno's RESET and GND, after uploading the sketch.

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

1. Toolchain: install MightyCore, build and flash a blink and buzzer test through the Uno ISP. Done (MightyCore 3.1.0, board `MightyCore:avr:644`, 20 MHz external clock, no bootloader; `hardware-test/` flashed and verified). Upload only. Never "Burn Bootloader", which would change the fuses.
2. Map the board's pins: done from the stock firmware source and a second source (see "Pin map"). Remaining: confirm on the board with a test sketch.
3. LCD and button handling (U8g2, debounce, long press).
4. DFPlayer link: play, pause, next, previous, volume.
5. Player UI on the LCD.
6. Earphone output wiring.
7. Power and enclosure.

Stages 1 to 3 need no extra hardware. Stage 4 needs the DFPlayer Mini, a microSD card
and a 1 kohm resistor. Stage 6 needs a 3.5 mm jack and two 100 ohm resistors.

## Open questions

- Confirming the pin map on the board, and which header pins are aileron (PD2/PD3) for the DFPlayer link.
- Final button assignment.
- Final power source.
- Whether to add Bluetooth later.
