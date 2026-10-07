// KK2.1.5 hardware test
//
// Checks the pin map from docs/design-decisions.md using only the LED and buzzer:
//   LED    PB3 (output)
//   Buzzer PB1 (output)
//   Buttons PB7 Back, PB6 Up, PB5 Down, PB4 Enter (inputs, internal pull-ups, active low)
//
// At boot the buzzer is driven two ways to find out what type it is:
//   1. steady on for 300 ms
//   2. 2 kHz square wave for 300 ms
// An active buzzer sounds in 1 (and gives a rough buzz in 2). A passive buzzer is
// quiet or only clicks in 1 and gives a clear tone in 2.
//
// Then the LED blinks once a second. Pressing a button beeps its number of times:
//   Back = 1, Up = 2, Down = 3, Enter = 4 (left to right).
// The LCD pins (PD1, PD4 to PD7) and the other pins are not touched.

#define LED_BIT    PB3
#define BUZZER_BIT PB1

static const uint8_t BUTTON_BITS[4] = {PB7, PB6, PB5, PB4};  // Back, Up, Down, Enter

static void buzzerOn()  { PORTB |=  _BV(BUZZER_BIT); }
static void buzzerOff() { PORTB &= ~_BV(BUZZER_BIT); }

static void beep(uint16_t ms) {
  buzzerOn();
  delay(ms);
  buzzerOff();
}

static void tone2kHz(uint16_t ms) {
  uint32_t end = millis() + ms;
  while (millis() < end) {
    buzzerOn();
    delayMicroseconds(250);
    buzzerOff();
    delayMicroseconds(250);
  }
}

static bool pressed(uint8_t bit) {
  return !(PINB & _BV(bit));
}

void setup() {
  DDRB |= _BV(LED_BIT) | _BV(BUZZER_BIT);                      // outputs
  DDRB &= ~(_BV(PB4) | _BV(PB5) | _BV(PB6) | _BV(PB7));        // button inputs
  PORTB |= _BV(PB4) | _BV(PB5) | _BV(PB6) | _BV(PB7);          // pull-ups

  PORTB |= _BV(LED_BIT);
  beep(300);
  delay(300);
  tone2kHz(300);
  PORTB &= ~_BV(LED_BIT);
}

void loop() {
  static uint32_t lastBlink = 0;
  uint32_t now = millis();
  if (now - lastBlink >= 500) {
    lastBlink = now;
    PINB = _BV(LED_BIT);                                       // toggle LED
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (pressed(BUTTON_BITS[i])) {
      delay(20);                                               // debounce
      if (!pressed(BUTTON_BITS[i])) continue;
      for (uint8_t n = 0; n <= i; n++) {
        beep(80);
        delay(120);
      }
      while (pressed(BUTTON_BITS[i])) {}                       // wait for release
      delay(20);
    }
  }
}
