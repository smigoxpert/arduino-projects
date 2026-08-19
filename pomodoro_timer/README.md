# Pomodoro Timer v3

An Arduino-based Pomodoro productivity timer with an LCD screen, buzzer, and EEPROM-backed settings. Supports five timer modes, an on-device settings menu, and a lifetime tomato (session) counter.

## Features

- **5 timer modes**: Work (25 min), Break (5 min), Long Break (15 min), Stopwatch, and Custom
- **Settings menu**: Edit Work / Break / Long Break durations directly on the device
- **EEPROM persistence**: Durations and lifetime tomato count survive power cycles
- **Tomato counter**: Tracks every completed Work session, displayed on boot and in the menu
- **Progress bar**: Visual fill bar on the second LCD row while a timer runs
- **Hold-to-abort**: Hold SELECT for 1.5 s to cancel and return to menu
- **+5 min bonus**: Press DOWN while a timer is running to add 5 minutes
- **Melodies**: Rising melody on break start, descending on work end

## Hardware

| Component | Details |
|-----------|---------|
| Arduino Uno (or compatible) | Any board with enough digital pins |
| 16×2 LCD (HD44780) | 4-bit parallel mode |
| 3× push buttons | Momentary normally-open |
| Passive buzzer | PWM-capable pin |
| Resistors / potentiometer | LCD contrast + button pull-downs (or use INPUT_PULLUP) |

## Wiring

```
LCD   RS  → pin 12
LCD   EN  → pin 11
LCD   D4  → pin 5
LCD   D5  → pin 4
LCD   D6  → pin 3
LCD   D7  → pin 2

Button SELECT → pin 9   (INPUT_PULLUP, active LOW)
Button UP     → pin 8   (INPUT_PULLUP, active LOW)
Button DOWN   → pin 7   (INPUT_PULLUP, active LOW)

Buzzer        → pin 10
```

## Controls

| Context | Action | Result |
|---------|--------|--------|
| Menu | UP / DOWN | Scroll through modes |
| Menu | SELECT | Start selected mode (or enter settings) |
| Running | SELECT tap | Pause |
| Running | SELECT hold (1.5 s) | Abort → back to menu |
| Running | DOWN tap | Add 5 bonus minutes |
| Paused | SELECT tap | Resume |
| Paused | SELECT hold | Abort → back to menu |
| Alarm | SELECT | Start suggested next session |
| Alarm | UP | Back to menu |
| Settings | UP / DOWN | Adjust current field (hold to scroll fast) |
| Settings | SELECT | Next field |
| Settings → Save & Exit | SELECT | Write to EEPROM and exit |

## Libraries

- `LiquidCrystal` — bundled with the Arduino IDE
- `EEPROM` — bundled with the Arduino IDE

No external library installation needed.

## Notes

- **Fast-forward testing**: Change `MS_PER_SEC` from `1000` to `50` for a 20× speedup during testing. Remember to revert before real use.
- **Tomato counter**: Stored in EEPROM bytes 5–6 as a `uint16` (max 65 535). It only increments on a full Work session completion.
