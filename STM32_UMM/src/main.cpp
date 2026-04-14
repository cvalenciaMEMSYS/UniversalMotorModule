// =============================================================================
// STM32 UMM — Minimal H-Bridge Motor Controller
// Board: Nucleo-L031K6 (STM32L031K6, Cortex-M0+)
// Purpose: Drive HarCo DRV88xx H-bridge modules via serial commands
// =============================================================================
//
// Commands match the main UMM implementation:
//   move <ms>    Timed move (positive=fwd, negative=rev), prints "Complete"
//   abs <pos>    Absolute move to position (same behavior)
//   stop         Emergency stop
//   brake        Brake (both HIGH)
//   wake         Wake module (nSLEEP HIGH)
//   sleep        Sleep module (nSLEEP LOW)
//   enable       Activate EN pin
//   disable      Deactivate EN pin
//   diag         Print diagnostics
//   help         List commands
//
// HarCo Modules:
//   DRV8837  (black) — IN1/IN2 only
//   DRV8832  (green) — IN1/IN2 only
//   DRV8210P (blue)  — IN1/IN2 only
//   DRV8839  (white) — IN1/IN2 + Enable pin
//
// Uncomment ONE module below to match your hardware:

// #define MODULE_DRV8837
// #define MODULE_DRV8832
// #define MODULE_DRV8210P
#define MODULE_DRV8839

// =============================================================================
// PIN CONFIGURATION (Nucleo-L031K6, Arduino Nano pinout)
// =============================================================================
//   IN1    → D3  (PB0, TIM2_CH3) — PWM @ 20kHz
//   IN2    → D6  (PB1, TIM2_CH4) — PWM @ 20kHz
//   nSLEEP → D2  (PA12)          — Digital
//   EN     → D4  (PB7)           — Digital (DRV8839 only)

#include <Arduino.h>
#include <HardwareTimer.h>

static const uint32_t PIN_IN1   = PB0;   // D3 — TIM2_CH3
static const uint32_t PIN_IN2   = PB1;   // D6 — TIM2_CH4
static const int      PIN_SLEEP = D2;    // PA12
static const int      PIN_EN    = D4;    // PB7

// TIM2 channel numbers for PB0 and PB1
static const uint32_t CH_IN1 = 3;  // TIM2_CH3
static const uint32_t CH_IN2 = 4;  // TIM2_CH4

#if defined(MODULE_DRV8839)
static const bool HAS_EN = true;
static const char MODULE_NAME[] = "DRV8839";
#elif defined(MODULE_DRV8837)
static const bool HAS_EN = false;
static const char MODULE_NAME[] = "DRV8837";
#elif defined(MODULE_DRV8832)
static const bool HAS_EN = false;
static const char MODULE_NAME[] = "DRV8832";
#elif defined(MODULE_DRV8210P)
static const bool HAS_EN = false;
static const char MODULE_NAME[] = "DRV8210P";
#else
#error "No module defined. Uncomment one MODULE_DRVxxxx at the top."
#endif

// PWM: 20kHz via HardwareTimer (TIM2)
static const uint32_t PWM_FREQ = 20000;

static HardwareTimer *pwmTimer = nullptr;

// =============================================================================
// STATE
// =============================================================================

static bool    awake        = false;
static bool    enabled      = false;
static int8_t  moveDir      = 0;       // -1, 0, +1
static bool    moving       = false;
static uint32_t moveDuration = 0;      // ms, 0 = indefinite
static uint32_t moveStart    = 0;
static int32_t position     = 0;       // Net time moved (ms), like stepper "steps"
static uint32_t lastPosUpdate = 0;     // For tracking position during moves
static int     currentDuty   = 100;    // Current PWM duty (0-100)

static char cmdBuf[64];
static int  cmdLen = 0;

// =============================================================================
// PWM HELPERS
// =============================================================================

static void initPWM() {
    pwmTimer = new HardwareTimer(TIM2);
    pwmTimer->setOverflow(PWM_FREQ, HERTZ_FORMAT);
    pwmTimer->setMode(CH_IN1, TIMER_OUTPUT_COMPARE_PWM1, PIN_IN1);
    pwmTimer->setMode(CH_IN2, TIMER_OUTPUT_COMPARE_PWM1, PIN_IN2);
    pwmTimer->setCaptureCompare(CH_IN1, 0, PERCENT_COMPARE_FORMAT);
    pwmTimer->setCaptureCompare(CH_IN2, 0, PERCENT_COMPARE_FORMAT);
    pwmTimer->resume();
}

// =============================================================================
// MOTOR CONTROL
// =============================================================================

static void driveForward(int duty) {
    pwmTimer->setCaptureCompare(CH_IN1, duty, PERCENT_COMPARE_FORMAT);
    pwmTimer->setCaptureCompare(CH_IN2, 0, PERCENT_COMPARE_FORMAT);
}

static void driveReverse(int duty) {
    pwmTimer->setCaptureCompare(CH_IN1, 0, PERCENT_COMPARE_FORMAT);
    pwmTimer->setCaptureCompare(CH_IN2, duty, PERCENT_COMPARE_FORMAT);
}

static void coast() {
    pwmTimer->setCaptureCompare(CH_IN1, 0, PERCENT_COMPARE_FORMAT);
    pwmTimer->setCaptureCompare(CH_IN2, 0, PERCENT_COMPARE_FORMAT);
}

static void brake() {
    pwmTimer->setCaptureCompare(CH_IN1, 100, PERCENT_COMPARE_FORMAT);
    pwmTimer->setCaptureCompare(CH_IN2, 100, PERCENT_COMPARE_FORMAT);
}

static void applyDirection() {
    if (moveDir > 0)       driveForward(currentDuty);
    else if (moveDir < 0)  driveReverse(currentDuty);
    else                   coast();
}

static void updatePosition() {
    // Accumulate time moved into position (net, like stepper steps)
    if (moving && moveDir != 0) {
        uint32_t now = millis();
        int32_t elapsed = (int32_t)(now - lastPosUpdate);
        position += moveDir * elapsed;
        lastPosUpdate = now;
    }
}

static void startMove(int32_t ms, int duty) {
    updatePosition();  // Flush any in-progress move
    if (ms == 0) return;
    moveDir = (ms > 0) ? 1 : -1;
    moveDuration = (uint32_t)abs(ms);
    moveStart = millis();
    lastPosUpdate = millis();
    moving = true;
    currentDuty = duty;
    applyDirection();
}

static void startRun(int8_t dir, int duty) {
    updatePosition();
    moveDir = dir;
    moveDuration = 0;  // Indefinite
    moveStart = millis();
    lastPosUpdate = millis();
    moving = true;
    currentDuty = duty;
    applyDirection();
}

static void stopMotor() {
    updatePosition();
    coast();
    moveDir = 0;
    moving = false;
    moveDuration = 0;
}

static void setSleep(bool doWake) {
    awake = doWake;
    digitalWrite(PIN_SLEEP, doWake ? HIGH : LOW);
}

static void setEnable(bool doEnable) {
    if (!HAS_EN) return;
    enabled = doEnable;
    digitalWrite(PIN_EN, doEnable ? LOW : HIGH);  // Active-LOW
}

// =============================================================================
// SERIAL COMMAND PARSER
// =============================================================================

static bool parseIntArg(const char* cmd, const char* prefix, int32_t* out) {
    int prefixLen = strlen(prefix);
    if (strncmp(cmd, prefix, prefixLen) != 0) return false;
    if (cmd[prefixLen] != ' ') return false;
    *out = atol(cmd + prefixLen + 1);
    return true;
}

static void processCommand(const char* cmd) {
    int32_t arg;

    // --- Motion ---
    if (parseIntArg(cmd, "move", &arg)) {
        // Timed move at max speed: duration in ms, sign = direction
        startMove(arg, 100);
    }
    else if (strncmp(cmd, "abs ", 4) == 0) {
        // abs <time_ms> <speed%>: timed move at specified speed
        int32_t timeMs = 0;
        int32_t speed = 100;
        const char* args = cmd + 4;
        // Parse first arg (time)
        timeMs = atol(args);
        // Find second arg (speed), optional
        const char* space = strchr(args, ' ');
        if (space) {
            speed = atol(space + 1);
        }
        if (speed < 0) speed = 0;
        if (speed > 100) speed = 100;
        startMove(timeMs, (int)speed);
    }
    else if (strcmp(cmd, "run f") == 0 || strcmp(cmd, "run forward") == 0) {
        startRun(1, 100);
    }
    else if (strcmp(cmd, "run b") == 0 || strcmp(cmd, "run backward") == 0) {
        startRun(-1, 100);
    }
    else if (strcmp(cmd, "stop") == 0) {
        Serial.println("Emergency stop!");
        stopMotor();
    }
    else if (strcmp(cmd, "brake") == 0) {
        Serial.println("Braking...");
        updatePosition();
        brake();
        moveDir = 0;
        moving = false;
        moveDuration = 0;
    }

    // --- Sleep ---
    else if (strcmp(cmd, "wake") == 0 || strcmp(cmd, "sleep on") == 0) {
        setSleep(true);
        Serial.println("Motor enabled");
    }
    else if (strcmp(cmd, "sleep") == 0 || strcmp(cmd, "sleep off") == 0) {
        setSleep(false);
        Serial.println("Motor disabled");
    }

    // --- Enable ---
    else if (strcmp(cmd, "enable") == 0) {
        setEnable(true);
        Serial.println("Motor enabled");
    }
    else if (strcmp(cmd, "disable") == 0) {
        setEnable(false);
        Serial.println("Motor disabled");
    }

    // --- Status ---
    else if (strcmp(cmd, "diag") == 0 || strcmp(cmd, "r") == 0 || strcmp(cmd, "?") == 0 || strcmp(cmd, "status") == 0) {
        updatePosition();
        Serial.print("Position: ");
        Serial.print(position);
        Serial.println(" ms");
        Serial.print("Moving: ");
        Serial.println(moving ? "yes" : "no");
        if (moving) {
            Serial.print("Direction: ");
            Serial.println(moveDir > 0 ? "FWD" : "REV");
            Serial.print("Duty: ");
            Serial.print(currentDuty);
            Serial.println("%");
        }
    }

    // --- Help ---
    else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        Serial.println("Commands:");
        Serial.println("  move <ms>         Timed move at 100% (+fwd, -rev)");
        Serial.println("  abs <ms> <speed%> Timed move at speed (0-100)");
        Serial.println("  run f             Forward indefinitely");
        Serial.println("  run b             Reverse indefinitely");
        Serial.println("  stop              Emergency stop");
        Serial.println("  brake             Brake (both HIGH)");
        Serial.println("  wake              nSLEEP HIGH");
        Serial.println("  sleep             nSLEEP LOW");
        Serial.println("  enable            EN active");
        Serial.println("  disable           EN inactive");
        Serial.println("  diag              Position & status");
    }

    else {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
    }
}

// =============================================================================
// SETUP & LOOP
// =============================================================================

void setup() {
    Serial.begin(115200);

    // Sleep pin — wake up
    pinMode(PIN_SLEEP, OUTPUT);
    setSleep(true);

    // Enable pin (DRV8839 only)
    if (HAS_EN) {
        pinMode(PIN_EN, OUTPUT);
        setEnable(true);
    }

    // 20kHz PWM on TIM2 channels
    initPWM();

    Serial.println();
    Serial.print("STM32 UMM [");
    Serial.print(MODULE_NAME);
    Serial.println("]");
    Serial.println("Type 'help' for commands");
}

void loop() {
    // Check timed move completion
    if (moving && moveDuration > 0) {
        if ((millis() - moveStart) >= moveDuration) {
            stopMotor();  // Also updates position
            Serial.println("Complete");
        }
    }

    // Read serial
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                processCommand(cmdBuf);
                cmdLen = 0;
            }
        } else if (cmdLen < (int)(sizeof(cmdBuf) - 1)) {
            cmdBuf[cmdLen++] = c;
        }
    }
}
