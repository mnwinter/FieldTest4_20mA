#include <SPI.h>                   // SPI library for TFT
#include <Adafruit_GFX.h>          // Core graphics library
#include <Adafruit_ST7789.h>       // ST7789 TFT driver

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  HARDWARE PINOUT
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
#define TFT_CS     10             // TFT CS pin → 10
#define TFT_DC      9             // TFT DC pin → 9
#define TFT_RST     8             // TFT RST pin → 8
#define TFT_BL      7             // TFT backlight pin → 7

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define JOY_UP      2             // Joy-Hat UP (A) → pin 2
#define JOY_RIGHT   12             // Joy-Hat RIGHT (B) → pin 3
#define JOY_LEFT    4             // Joy-Hat LEFT (C) → pin 4 (Exit)
#define JOY_DOWN    5             // Joy-Hat DOWN (D) → pin 5
#define JOY_CENTER  6             // Joy-Hat CENTER (press) → pin 6

#define POT_PIN     A0            // Potentiometer → A0 (for setpoint/time adjust)
#define CURRENT_OUT_PIN  3        // PWM pin → 4–20 mA driver

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  COLOR DEFINITIONS
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
#define DARKGREY     0x7BEF       // RGB565 “dark grey” for highlighting

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  MODES AND STATE TRACKERS
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
enum Mode { MODE_SWEEP = 0, MODE_MANUAL = 1, MODE_RAMP = 2 };
const char *modeNames[] = { "Sweep", "Manual", "Ramp" };

Mode     selectedMode    = MODE_MANUAL; // default to Manual
bool     inSubMenu       = false;       // are we inside a submenu?
bool     outputEnabled   = false;       // for Manual: is output enabled?
float    currentSetpoint = 4.00f;       // current mA setpoint

// Sweep parameters
unsigned long sweepDuration = 10000;    // default 10 seconds (ms) one-way
unsigned long sweepStartTime = 0;       // millis() of beginning of current sweep leg
bool     sweepUpward    = true;         // direction flag
bool     sweepRunning   = false;        // is sweeping active?
unsigned long lastSweepPause = 0;       // time when paused
int      lastSweepSecs   = -1;          // cache last displayed sweep time in sec

// Ramp parameters
float    rampStopPoint   = 20.00f;      // Final current for Ramp mode (20 mA)
unsigned long rampDuration = 5000;      // default 5 seconds (ms)
unsigned long rampStartTime = 0;        // millis() of ramp start or resume
bool     rampRunning    = false;        // is ramp active?
unsigned long lastRampPause = 0;        // time when paused
bool     rampComplete   = false;        // has ramp reached 20 mA?
int      lastRampSecs    = -1;          // cache last displayed ramp time in sec

// Flicker‐free caches
float    lastHeaderSetpt = -1;
float    lastProgPct     = -1;

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  SETUP()
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void setup() {
  Serial.begin(115200);

  // Turn backlight on
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Joy‐Hat pins → INPUT_PULLUP
  pinMode(JOY_UP,     INPUT_PULLUP);
  pinMode(JOY_RIGHT,  INPUT_PULLUP);
  pinMode(JOY_LEFT,   INPUT_PULLUP);
  pinMode(JOY_DOWN,   INPUT_PULLUP);
  pinMode(JOY_CENTER, INPUT_PULLUP);

  // Potentiometer input
  pinMode(POT_PIN, INPUT);

  // 4–20 mA output (PWM)
  pinMode(CURRENT_OUT_PIN, OUTPUT);
  analogWrite(CURRENT_OUT_PIN, 0); // start at 0 mA

  // TFT initialization
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  // Brief splash screen
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(20, 80);
  tft.println("4 to 20mA");
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.println("Signal Generator");
  tft.setCursor(20, 160);
  tft.println("By MNWCreations");
  delay(3500);
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  MAIN LOOP()
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void loop() {
  if (!inSubMenu) {
    displayMainMenu();
    handleJoystickMenu();
  }
  delay(200);
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  DRAW HEADER: Show current mA setpoint only (24px tall)
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void drawHeader() {
  if (fabs(currentSetpoint - lastHeaderSetpt) > 0.01f) {
    lastHeaderSetpt = currentSetpoint;

    // Clear top 24px
    tft.fillRect(0, 0, 240, 24, ST77XX_BLACK);

    // Draw “O:xx.xmA”
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(0, 0);
    tft.print("O:");
    tft.print(currentSetpoint, 1);
    tft.print("mA");

    // Divider line at y=23
    tft.drawFastHLine(0, 23, 240, ST77XX_WHITE);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  MAIN MENU: show 3 modes, highlight selectedMode
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void displayMainMenu() {
  tft.fillScreen(ST77XX_BLACK);

  // Force header redraw
  lastHeaderSetpt = -1;
  lastProgPct = -1;
  drawHeader();

  // Title “Select Mode:”
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 28);
  tft.print("Select Mode:");

  // List each mode (40px spacing)
  for (int i = 0; i < 3; i++) {
    int y = 60 + i * 40;
    if (i == selectedMode) {
      // Highlight background
      tft.fillRect(0, y - 4, 240, 32, DARKGREY);
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(20, y);
    tft.setTextSize(3);
    tft.print(modeNames[i]);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  HANDLE JOY‐HAT NAVIGATION IN MAIN MENU
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void handleJoystickMenu() {
  bool up     = !digitalRead(JOY_UP);
  bool down   = !digitalRead(JOY_DOWN);
  bool center = !digitalRead(JOY_CENTER);

  // Scroll up
  if (up) {
    selectedMode = (Mode)((selectedMode + 2) % 3);
    delay(150);
    displayMainMenu();
  }
  // Scroll down
  if (down) {
    selectedMode = (Mode)((selectedMode + 1) % 3);
    delay(150);
    displayMainMenu();
  }
  // Enter selected mode
  if (center) {
    while (!digitalRead(JOY_CENTER)); // wait for release
    delay(100);
    inSubMenu = true;
    switch (selectedMode) {
      case MODE_SWEEP:  sweepMode();  break;
      case MODE_MANUAL: manualMode(); break;
      case MODE_RAMP:   rampMode();   break;
    }
    inSubMenu = false;
    delay(200);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  MANUAL MODE: Center toggles ON/OFF, Left exits
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void manualMode() {
  tft.fillScreen(ST77XX_BLACK);
  lastHeaderSetpt = -1;

  drawHeader();
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 40);
  tft.print("Manual Mode");
  tft.setTextSize(1);
  tft.setCursor(10, 72);
  tft.print("Center: On/Off");
  tft.setCursor(10, 88);
  tft.print("Left: Exit");

  outputEnabled = false;
  float lastMA = -1;

  while (true) {
    // Exit if Left pressed
    if (!digitalRead(JOY_LEFT)) {
      while (!digitalRead(JOY_LEFT));
      delay(100);
      updateOutput(0.0f);
      returnToMenu();
      return;
    }
    // Toggle enable/disable if Center pressed
    if (!digitalRead(JOY_CENTER)) {
      while (!digitalRead(JOY_CENTER));
      delay(100);
      outputEnabled = !outputEnabled;
      if (!outputEnabled) {
        // If disabling, zero output and show “Paused”
        updateOutput(0.0f);
        tft.fillRect(10, 120, 220, 30, ST77XX_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(10, 120);
        tft.print("Paused   ");
        delay(500);
      } else {
        // Clearing “Paused” so new mA can show
        tft.fillRect(10, 120, 220, 30, ST77XX_BLACK);
      }
    }

    // If enabled, read pot and update 4–20 mA
    if (outputEnabled) {
      int raw = analogRead(POT_PIN);
      int mA_int = map(raw, 0, 1023, 400, 2000); // integer from 400→2000
      float mA = mA_int / 100.0f;                // float 4.00→20.00
      if (mA_int >= 2000) { mA = 20.00f; }

      if (mA != lastMA) {
        lastMA = mA;
        currentSetpoint = mA;
        updateOutput(mA);

        // Redraw numeric area
        tft.fillRect(10, 120, 220, 30, ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 120);
        tft.print(mA, 2);
        tft.print(" mA");
      }
    }

    drawHeader();
    delay(50);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  SWEEP MODE: Ping-pong 4↔20 mA, pot sets sweep time
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void sweepMode() {
  tft.fillScreen(ST77XX_BLACK);
  lastHeaderSetpt = -1;
  lastProgPct = -1;
  lastSweepSecs = -1;

  drawHeader();
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 40);
  tft.print("Sweep Mode");
  tft.setTextSize(1);
  tft.setCursor(10, 72);
  tft.print("Pot: Time (sec)");
  tft.setCursor(10, 88);
  tft.print("Center: Start/Pause");
  tft.setCursor(10, 104);
  tft.print("Left: Exit");

  sweepRunning = false;

  // Initial time selection: pot sets sweepDuration (1–20 seconds)
  while (!digitalRead(JOY_CENTER)) {
    if (!digitalRead(JOY_LEFT)) {
      while (!digitalRead(JOY_LEFT));
      delay(100);
      returnToMenu();
      return;
    }
    int raw = analogRead(POT_PIN);
    int secs = map(raw, 0, 1023, 1, 20);       // 1–20 seconds
    if (secs != lastSweepSecs) {
      // Update only when pot changes integer seconds
      lastSweepSecs = secs;
      sweepDuration = secs * 1000UL;            // convert to ms

      // Display selected time at y=120
      tft.fillRect(10, 120, 220, 16, ST77XX_BLACK);
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(10, 120);
      tft.print("Cycle: ");
      tft.print(secs);
      tft.print(" s   ");
    }
    drawHeader();
    delay(50);
  }
  while (!digitalRead(JOY_CENTER)); // wait for release
  delay(100);

  // Initialize sweep
  sweepUpward = true;
  sweepStartTime = millis();
  sweepRunning = true;

  while (true) {
    // Allow time adjustment while paused
    if (!sweepRunning) {
      int raw = analogRead(POT_PIN);
      int secs = map(raw, 0, 1023, 1, 20);
      if (secs != lastSweepSecs) {
        lastSweepSecs = secs;
        sweepDuration = secs * 1000UL;
        tft.fillRect(10, 120, 220, 16, ST77XX_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 120);
        tft.print("Cycle: ");
        tft.print(secs);
        tft.print(" s   ");
      }
    }

    // Exit if Left pressed (hold current output)
    if (!digitalRead(JOY_LEFT)) {
      while (!digitalRead(JOY_LEFT));
      delay(100);
      updateOutput(currentSetpoint);
      returnToMenu();
      return;
    }
    // Toggle pause/resume if Center pressed
    if (!digitalRead(JOY_CENTER)) {
      while (!digitalRead(JOY_CENTER));
      delay(100);
      sweepRunning = !sweepRunning;
      if (sweepRunning) {
        // Clear “Paused”
        tft.fillRect(10, 136, 220, 16, ST77XX_BLACK);
        // Adjust start time by paused duration
        unsigned long now = millis();
        unsigned long paused = now - lastSweepPause;
        sweepStartTime += paused;
      } else {
        // Pausing: record pause time and hold currentSetpoint
        lastSweepPause = millis();
        updateOutput(currentSetpoint);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(10, 136);
        tft.print("Paused   ");
      }
    }

    if (sweepRunning) {
      unsigned long now = millis();
      unsigned long elapsed = now - sweepStartTime;
      unsigned long cycle = sweepDuration * 2UL;
      unsigned long pos = elapsed % cycle;

      float fraction;
      if (pos <= sweepDuration) {
        // Ascending 4→20
        fraction = pos / (float)sweepDuration;
        sweepUpward = true;
      } else {
        // Descending 20→4
        fraction = (pos - sweepDuration) / (float)sweepDuration;
        sweepUpward = false;
      }

      float mA;
      if (sweepUpward) {
        mA = 4.0f + fraction * 16.0f;
      } else {
        mA = 20.0f - fraction * 16.0f;
      }
      currentSetpoint = mA;
      updateOutput(mA);

      // Show progress (0→100)
      float pct = (pos <= sweepDuration) ? (fraction * 100.0f) : ((1.0f - fraction) * 100.0f);
      if (fabs(pct - lastProgPct) > 0.5f) {
        lastProgPct = pct;
        showProgress(pct);
      }

      drawHeader();
    }

    delay(50);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  RAMP MODE: Pot sets time, Center start/stop, Left exit
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void rampMode() {
  tft.fillScreen(ST77XX_BLACK);
  lastHeaderSetpt = -1;
  lastProgPct = -1;
  lastRampSecs = -1;
  rampComplete = false;

  drawHeader();
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 40);
  tft.print("Ramp Mode");
  tft.setTextSize(1);
  tft.setCursor(10, 72);
  tft.print("Pot: Time (sec)");
  tft.setCursor(10, 88);
  tft.print("Center: Start/Stop");
  tft.setCursor(10, 104);
  tft.print("Left: Exit");

  rampRunning = false;
  lastProgPct = -1;

  // Initial time selection: pot sets rampDuration (1–20 seconds)
  while (!digitalRead(JOY_CENTER)) {
    if (!digitalRead(JOY_LEFT)) {
      while (!digitalRead(JOY_LEFT));
      delay(100);
      returnToMenu();
      return;
    }
    int raw = analogRead(POT_PIN);
    int secs = map(raw, 0, 1023, 1, 20);
    if (secs != lastRampSecs) {
      lastRampSecs = secs;
      rampDuration = secs * 1000UL; // ms

      // Display chosen time at y=120
      tft.fillRect(10, 120, 220, 16, ST77XX_BLACK);
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(10, 120);
      tft.print("Cycle: ");
      tft.print(secs);
      tft.print(" s   ");
    }
    drawHeader();
    delay(50);
  }
  while (!digitalRead(JOY_CENTER));
  delay(100);

  // Initialize: set to 4.00 mA and show below time
  currentSetpoint = 4.00f;
  updateOutput(4.00f);
  tft.fillRect(10, 136, 220, 30, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 136);
  tft.print("4.00 mA");
  delay(500);

  rampStartTime = millis();
  rampRunning = true;

  while (true) {
    // Allow pot adjustment when paused or after complete
    if (!rampRunning || rampComplete) {
      int raw = analogRead(POT_PIN);
      int secs = map(raw, 0, 1023, 1, 20);
      if (secs != lastRampSecs) {
        lastRampSecs = secs;
        rampDuration = secs * 1000UL; // ms

        // Update displayed ramp time
        tft.fillRect(10, 120, 220, 16, ST77XX_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 120);
        tft.print("Cycle: ");
        tft.print(secs);
        tft.print(" s   ");
      }
    }

    // Exit if Left pressed, hold currentSetpoint
    if (!digitalRead(JOY_LEFT)) {
      while (!digitalRead(JOY_LEFT));
      delay(100);
      updateOutput(currentSetpoint);
      returnToMenu();
      return;
    }
    // Toggle pause/resume on Center or restart if complete
    if (!digitalRead(JOY_CENTER)) {
      while (!digitalRead(JOY_CENTER));
      delay(100);
      if (rampComplete) {
        // Restart entire ramp
        rampComplete = false;
        rampRunning = true;
        rampStartTime = millis();
        currentSetpoint = 4.00f;
        updateOutput(4.00f);
        tft.fillRect(10, 136, 220, 30, ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 136);
        tft.print("4.00 mA");
        continue;
      }
      rampRunning = !rampRunning;
      if (!rampRunning) {
        lastRampPause = millis();
        updateOutput(currentSetpoint);
        tft.setTextSize(2);
        tft.setTextColor(ST77XX_RED);
        tft.setCursor(10, 136);
        tft.print("Paused    ");
      } else {
        // Clear “Paused”
        tft.fillRect(10, 136, 220, 30, ST77XX_BLACK);
        // Adjust start time by paused duration
        unsigned long now = millis();
        unsigned long paused = now - lastRampPause;
        rampStartTime += paused;
      }
    }

    if (rampRunning && !rampComplete) {
      unsigned long now = millis();
      float elapsed = now - rampStartTime;
      if (elapsed >= rampDuration) {
        // Ramp complete: exactly 20.00 mA
        currentSetpoint = rampStopPoint;
        updateOutput(rampStopPoint);
        showProgress(100.0f);
        rampComplete = true;
        // Display final 20.00 value
        tft.fillRect(10, 136, 220, 30, ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(10, 136);
        tft.print("20.00 mA");
        drawHeader();
        delay(50);
        continue;
      }

      float fraction = elapsed / (float)rampDuration; // 0→1
      float mA = 4.0f + fraction * (rampStopPoint - 4.0f); // ramp 4→20
      currentSetpoint = mA;
      updateOutput(mA);

      float pct = fraction * 100.0f;
      if (fabs(pct - lastProgPct) > 0.5f) {
        lastProgPct = pct;
        showProgress(pct);
      }

      // Update mA display below time
      tft.fillRect(10, 136, 220, 30, ST77XX_BLACK);
      tft.setTextSize(3);
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(10, 136);
      tft.print(mA, 2);
      tft.print(" mA");

      drawHeader();
    }

    delay(50);
  }
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  DRAW PROGRESS BAR (0–100%)
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void showProgress(float pct) {
  tft.fillRect(10, 160, 220, 20, ST77XX_BLACK);      // Clear old bar
  tft.drawRect(10, 160, 220, 20, ST77XX_WHITE);      // Outline
  int len = (int)(pct / 100.0f * 218.0f);
  tft.fillRect(11, 161, len, 18, ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(90, 184);
  tft.print((int)pct);
  tft.print("%");
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  RETURN TO MAIN MENU
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void returnToMenu() {
  inSubMenu = false;
  updateOutput(0.0f);                  // Turn off output
  tft.fillScreen(ST77XX_BLACK);
  lastHeaderSetpt = -1;                // Reset cache
  lastProgPct = -1;
}

//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
//  UPDATE OUTPUT: mA → PWM duty (4.00mA=51, 20.00mA=255)
//–––––––––––––––––––––––––––––––––––––––––––––––––––––––
void updateOutput(float mA) {
  if (mA <= 0.0f) {
    analogWrite(CURRENT_OUT_PIN, 0);
  } else {
    int pwm = map((int)(mA * 100), 400, 2000, 51, 255);
    pwm = constrain(pwm, 51, 255);
    analogWrite(CURRENT_OUT_PIN, pwm);
  }
}
