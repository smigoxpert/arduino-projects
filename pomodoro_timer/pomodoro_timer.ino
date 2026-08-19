/*
  POMODORO TIMER v3  -  settings menu + EEPROM + tomato icons
  -----------------------------------------------------------
  Everything from v2, plus:

  SETTINGS (last entry in the menu):
     edit WORK / BREAK / LONG BRK durations on the device itself.
     UP/DOWN adjust (hold to scroll fast), SELECT moves to the next
     field, SELECT on "SAVE & EXIT" stores everything in EEPROM -
     the chip's built-in permanent memory - so your durations
     survive unplugging. The CUSTOM duration is also remembered.

  TOMATOES:
     every completed WORK session earns a tomato. The menu screen
     shows a tomato icon and your lifetime total, which is stored
     in EEPROM and just keeps climbing forever.

  Controls (unchanged from v2):
     menu: UP/DOWN pick, SELECT start
     running: SELECT tap = pause, SELECT hold = abort,
              DOWN tap = +5 bonus minutes
     alarm: SELECT = start suggested next, UP = back to menu

  Wiring: LCD 12,11,5,4,3,2 / buttons SEL=9 UP=8 DOWN=7 / buzzer=10
  TESTING: MS_PER_SEC = 50 for 20x fast-forward.
*/

#include <LiquidCrystal.h>
#include <EEPROM.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int BTN_SELECT = 9;
const int BTN_UP     = 8;
const int BTN_DOWN   = 7;
const int BUZZER     = 10;

const unsigned long MS_PER_SEC = 1000;
const unsigned int  SESSIONS_BEFORE_LONG = 4;
const unsigned long HOLD_ABORT_MS = 1500;

// ------------------------------------------------ modes --------
// indexes 0-4 are startable modes; 5 is the settings entry
const byte N_ITEMS = 6;
const char* modeName[N_ITEMS] = {"WORK", "BREAK", "LONG BRK", "STOPWATCH", "CUSTOM", "SETTINGS"};
unsigned int modeMin[N_ITEMS]  = {25, 5, 15, 0, 30, 0};
const byte M_WORK = 0, M_BREAK = 1, M_LONG = 2, M_WATCH = 3, M_CUSTOM = 4, M_SET = 5;

// ------------------------------------------------ EEPROM -------
// layout: [0]=magic  [1]=work [2]=break [3]=long [4]=custom
//         [5..6]=lifetime tomato count (uint16)
const byte EE_MAGIC = 0x42;
unsigned int totalPomodoros = 0;

void loadSettings() {
  if (EEPROM.read(0) == EE_MAGIC) {
    modeMin[M_WORK]   = EEPROM.read(1);
    modeMin[M_BREAK]  = EEPROM.read(2);
    modeMin[M_LONG]   = EEPROM.read(3);
    modeMin[M_CUSTOM] = EEPROM.read(4);
    EEPROM.get(5, totalPomodoros);
  } else {
    saveSettings();                 // first boot: write defaults
    EEPROM.put(5, totalPomodoros);
  }
}
void saveSettings() {
  EEPROM.update(0, EE_MAGIC);       // update = only writes if changed
  EEPROM.update(1, modeMin[M_WORK]);
  EEPROM.update(2, modeMin[M_BREAK]);
  EEPROM.update(3, modeMin[M_LONG]);
  EEPROM.update(4, modeMin[M_CUSTOM]);
}

// ------------------------------------------------ state --------
enum State { MENU, SET_CUSTOM, SETTINGS_EDIT, RUNNING, PAUSED, ALARM };
State state = MENU;

byte modeIndex = 0;                 // menu highlight / running mode
byte suggested = M_BREAK;
byte setField  = 0;                 // which settings field is being edited
unsigned int worksDone = 0;         // this power-on, for the x/4 cycle

unsigned long phaseStart = 0, phaseDuration = 0, pausedElapsed = 0;
unsigned long alarmLastBeep = 0;
long lastShownSec = -1;
bool needRedraw = true;

// ------------------------------------------------ buttons ------
struct Btn {
  byte pin;
  bool stable, lastRead, pressEvent, holdConsumed;
  unsigned long tChange, tPress;

  Btn(byte p) : pin(p), stable(HIGH), lastRead(HIGH),
                pressEvent(false), holdConsumed(false),
                tChange(0), tPress(0) {}

  void update() {
    pressEvent = false;
    bool raw = digitalRead(pin);
    unsigned long now = millis();
    if (raw != lastRead) { lastRead = raw; tChange = now; }
    if (now - tChange > 25 && raw != stable) {
      stable = raw;
      if (stable == LOW) { tPress = now; pressEvent = true; holdConsumed = false; }
    }
  }

  unsigned long heldFor() { return (stable == LOW) ? millis() - tPress : 0; }
};
Btn bSel(BTN_SELECT);
Btn bUp(BTN_UP);
Btn bDn(BTN_DOWN);

// UP/DOWN spinner with hold-to-accelerate. Returns -1, 0 or +1.
int spinDelta() {
  static unsigned long lastStep = 0;
  int d = 0;
  if (bUp.pressEvent) d = +1;
  if (bDn.pressEvent) d = -1;
  unsigned long hUp = bUp.heldFor(), hDn = bDn.heldFor();
  unsigned long h = hUp > hDn ? hUp : hDn;
  if (h > 400) {
    unsigned int interval = (h > 1500) ? 60 : 180;
    if (millis() - lastStep > interval) d = (hUp > hDn) ? +1 : -1;
  }
  if (d) lastStep = millis();
  return d;
}

// ------------------------------------------------ buzzer -------
void beep(unsigned int f, unsigned int d) { tone(BUZZER, f, d); }

void melodyDone(bool wasWork) {
  static const unsigned int upN[]   = {523, 659, 784, 1047};
  static const unsigned int downN[] = {784, 659, 523, 392};
  const unsigned int *m = wasWork ? downN : upN;
  for (byte i = 0; i < 4; i++) { tone(BUZZER, m[i]); delay(160); }
  noTone(BUZZER);
}

// ------------------------------------------------ display ------
const byte CH_TOMATO = 6;

void makeGlyphs() {
  for (byte n = 1; n <= 5; n++) {          // progress bar cells
    byte g[8]; byte row = ((1 << n) - 1) << (5 - n);
    for (byte r = 0; r < 8; r++) g[r] = row;
    lcd.createChar(n, g);
  }
  byte tomato[8] = {                        // 5x8 tomato, stem on top
    B00100,
    B01110,
    B11111,
    B11111,
    B11111,
    B11111,
    B01110,
    B00000
  };
  lcd.createChar(CH_TOMATO, tomato);
}

void drawBar(unsigned long elapsed) {
  unsigned long px = (elapsed * 80UL) / phaseDuration;
  if (px > 80) px = 80;
  byte full = px / 5, rem = px % 5;
  lcd.setCursor(0, 1);
  for (byte i = 0; i < full; i++) lcd.write((byte)255);
  if (rem && full < 16) lcd.write(rem);
  for (byte i = full + (rem ? 1 : 0); i < 16; i++) lcd.print(" ");
}

void printRow(byte row, const char *txt) {
  char buf[17];
  snprintf(buf, 17, "%-16s", txt);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

// ------------------------------------------------ transitions --
void gotoMenu() { state = MENU; needRedraw = true; noTone(BUZZER); lcd.clear(); }

void startMode(byte m) {
  modeIndex = m;
  phaseStart = millis();
  pausedElapsed = 0;
  phaseDuration = (m == M_WATCH) ? 0UL
                : (unsigned long)modeMin[m] * 60UL * MS_PER_SEC;
  lastShownSec = -1;
  state = RUNNING; needRedraw = true; lcd.clear();
  beep(880, 60);
}

void enterAlarm() {
  bool wasWork = (modeIndex == M_WORK || modeIndex == M_CUSTOM);
  if (modeIndex == M_WORK) {
    worksDone++;
    totalPomodoros++;
    EEPROM.put(5, totalPomodoros);          // tomato earned forever
    suggested = (worksDone % SESSIONS_BEFORE_LONG == 0) ? M_LONG : M_BREAK;
  } else {
    suggested = M_WORK;
  }
  state = ALARM; needRedraw = true; lcd.clear();
  melodyDone(wasWork);
  alarmLastBeep = millis();
}

// ------------------------------------------------ setup --------
void setup() {
  lcd.begin(16, 2);
  makeGlyphs();
  loadSettings();
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BUZZER,     OUTPUT);
  printRow(0, "POMODORO v3");
  lcd.setCursor(0, 1);
  lcd.write(CH_TOMATO);
  char b[16]; snprintf(b, 16, " total: %u", totalPomodoros);
  lcd.print(b);
  delay(1800);
  gotoMenu();
}

// ------------------------------------------------ loop ---------
void loop() {
  bSel.update(); bUp.update(); bDn.update();
  unsigned long now = millis();

  // ---------- MENU ----------
  if (state == MENU) {
    if (bUp.pressEvent) { modeIndex = (modeIndex + N_ITEMS - 1) % N_ITEMS; needRedraw = true; beep(660, 30); }
    if (bDn.pressEvent) { modeIndex = (modeIndex + 1) % N_ITEMS;           needRedraw = true; beep(660, 30); }
    if (bSel.pressEvent) {
      if      (modeIndex == M_CUSTOM) { state = SET_CUSTOM;    needRedraw = true; lcd.clear(); }
      else if (modeIndex == M_SET)    { state = SETTINGS_EDIT; setField = 0; needRedraw = true; lcd.clear(); }
      else startMode(modeIndex);
      return;
    }
    if (needRedraw) {
      needRedraw = false;
      char r0[17];
      if (modeIndex == M_WATCH || modeIndex == M_SET)
           snprintf(r0, 17, "<%-14s>", modeName[modeIndex]);
      else snprintf(r0, 17, "<%-9s %3um>", modeName[modeIndex], modeMin[modeIndex]);
      printRow(0, r0);
      // row 1: prompt + lifetime tomato counter
      lcd.setCursor(0, 1);
      lcd.print("SEL=start  ");
      lcd.write(CH_TOMATO);
      char cnt[6]; snprintf(cnt, 6, "%-4u", totalPomodoros > 9999 ? 9999 : totalPomodoros);
      lcd.print(cnt);
    }
  }

  // ---------- SET_CUSTOM ----------
  else if (state == SET_CUSTOM) {
    int d = spinDelta();
    if (d != 0) {
      int v = (int)modeMin[M_CUSTOM] + d;
      if (v < 1) v = 1; if (v > 180) v = 180;
      modeMin[M_CUSTOM] = v; needRedraw = true;
    }
    if (bSel.pressEvent) { saveSettings(); startMode(M_CUSTOM); return; }
    if (needRedraw) {
      needRedraw = false;
      char r0[17];
      snprintf(r0, 17, "CUSTOM   %3u min", modeMin[M_CUSTOM]);
      printRow(0, r0);
      printRow(1, "UP/DN  SEL=start");
    }
  }

  // ---------- SETTINGS_EDIT ----------
  else if (state == SETTINGS_EDIT) {
    // fields: 0=WORK 1=BREAK 2=LONG BRK 3=SAVE & EXIT
    static const byte fieldMode[3] = {M_WORK, M_BREAK, M_LONG};
    static const unsigned int fieldMax[3] = {120, 60, 90};

    if (setField < 3) {
      int d = spinDelta();
      if (d != 0) {
        int v = (int)modeMin[fieldMode[setField]] + d;
        if (v < 1) v = 1;
        if (v > (int)fieldMax[setField]) v = fieldMax[setField];
        modeMin[fieldMode[setField]] = v;
        needRedraw = true;
      }
    }
    if (bSel.pressEvent) {
      if (setField < 3) {              // advance to next field
        setField++;
        needRedraw = true;
        beep(setField == 3 ? 660 : 880, 40);
      } else {                         // a fresh press ON the save screen
        saveSettings();
        beep(1047, 120);
        gotoMenu();
        return;
      }
    }
    if (needRedraw) {
      needRedraw = false;
      char r0[17];
      if (setField < 3) {
        snprintf(r0, 17, "SET %-8s %3u", modeName[fieldMode[setField]], modeMin[fieldMode[setField]]);
        printRow(0, r0);
        printRow(1, "UP/DN   SEL=next");
      } else {
        printRow(0, "SAVE & EXIT");
        printRow(1, "SEL=save to chip");
      }
    }
  }

  // ---------- RUNNING ----------
  else if (state == RUNNING) {
    if (bSel.heldFor() > HOLD_ABORT_MS && !bSel.holdConsumed) {
      bSel.holdConsumed = true; beep(330, 120); gotoMenu(); return;
    }
    static bool selWasDown = false;
    if (bSel.stable == LOW) selWasDown = true;
    else if (selWasDown) {
      selWasDown = false;
      if (!bSel.holdConsumed) {
        pausedElapsed = now - phaseStart;
        state = PAUSED; needRedraw = true; beep(440, 40);
        return;
      }
    }
    unsigned long elapsed = now - phaseStart;

    if (modeIndex == M_WATCH) {
      long sec = elapsed / MS_PER_SEC;
      if (sec != lastShownSec) {
        lastShownSec = sec;
        char r0[17];
        unsigned int hh = sec / 3600, mm = (sec / 60) % 60, ss = sec % 60;
        if (hh > 0) snprintf(r0, 17, "WATCH   %u:%02u:%02u", hh, mm, ss);
        else        snprintf(r0, 17, "WATCH      %2u:%02u", mm, ss);
        printRow(0, r0);
        printRow(1, "SEL=pause");
      }
    }
    else {
      if (bDn.pressEvent) {
        phaseDuration += 5UL * 60UL * MS_PER_SEC;
        lastShownSec = -1; beep(988, 60);
      }
      if (elapsed >= phaseDuration) { enterAlarm(); return; }
      long secLeft = (phaseDuration - elapsed + MS_PER_SEC - 1) / MS_PER_SEC;
      if (secLeft != lastShownSec) {
        lastShownSec = secLeft;
        char r0[17], label[12];
        if (modeIndex == M_WORK)
          snprintf(label, 12, "WORK %u/%u", (worksDone % SESSIONS_BEFORE_LONG) + 1, SESSIONS_BEFORE_LONG);
        else snprintf(label, 12, "%s", modeName[modeIndex]);
        snprintf(r0, 17, "%-9s %3lu:%02lu", label, secLeft / 60, secLeft % 60);
        printRow(0, r0);
        drawBar(elapsed);
      }
    }
  }

  // ---------- PAUSED ----------
  else if (state == PAUSED) {
    if (bSel.heldFor() > HOLD_ABORT_MS && !bSel.holdConsumed) {
      bSel.holdConsumed = true; beep(330, 120); gotoMenu(); return;
    }
    static bool selWasDown2 = false;
    if (bSel.stable == LOW) selWasDown2 = true;
    else if (selWasDown2) {
      selWasDown2 = false;
      if (!bSel.holdConsumed) {
        phaseStart = now - pausedElapsed;
        lastShownSec = -1;
        state = RUNNING; needRedraw = true; beep(660, 40);
        return;
      }
    }
    if ((now / 500) % 2 == 0) printRow(0, "PAUSED");
    else                      printRow(0, "");
    printRow(1, "SEL=go  hold=out");
  }

  // ---------- ALARM ----------
  else if (state == ALARM) {
    if (bSel.pressEvent) { startMode(suggested); return; }
    if (bUp.pressEvent || bDn.pressEvent) { gotoMenu(); return; }
    if (now - alarmLastBeep > 3000) {
      alarmLastBeep = now;
      beep(1047, 80); delay(110); beep(1047, 80);
    }
    bool wasWork = (suggested != M_WORK);
    if ((now / 400) % 2 == 0) {
      if (wasWork) {
        lcd.setCursor(0, 0);
        lcd.write(CH_TOMATO);
        lcd.print(" WORK DONE!    ");
      } else printRow(0, "BREAK OVER!");
    } else printRow(0, "");
    char r1[17];
    snprintf(r1, 17, "SEL=%-9s UP", modeName[suggested]);
    printRow(1, r1);
  }
}
