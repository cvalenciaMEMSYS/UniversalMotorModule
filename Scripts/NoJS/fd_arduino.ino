// =============================================================================
// FD Setup Arduino Controller - NoJS Version
// =============================================================================
// Simplified command protocol for Force-Deflection measurements.
// Designed to work with fd_server_nojs.py via serial communication.
//
// Commands:
//   M              - Enter MONITOR mode (force streaming, no auto-movement)
//   D              - Enter DRIVE mode (movement with auto force streaming)
//   V <speed>      - Set linear speed in mm/s
//   <number>       - Manual JOG: move that distance in mm (setup/positioning)
//   G <distance>   - DRIVE mode: move forward, stream force, then retract
//   START          - Start force data streaming (MONITOR mode)
//   STOP           - Stop force data streaming
//   F              - Get single force reading
//   Z              - Zero/tare the load cell
//   ?              - Get current status
//
// Data stream format (when streaming):
//   T<ms>,F<force_N>
//   Example: T1234,F0.532
// =============================================================================

#include "HX711.h"

// =============================================================================
// HARDWARE CONFIGURATION (preserved from original)
// =============================================================================

// Stepper motor pins
#define DIR_PIN 2                    // Direction pin
#define STEP_PIN 3                   // Step pin
#define STEPS_PER_REV 6400           // Steps per revolution (set on driver)
#define SCREW_PITCH 1.0              // Lead screw pitch in mm/rev

// Load cell pins (HX711)
#define LOADCELL_DOUT 5
#define LOADCELL_SCK 6
#define CALIBRATION_FACTOR 439.0     // Calibration factor for the load cell

// Safety limits
#define FORCE_MAX 100.0              // Maximum force in N (safety cutoff)
#define SPEED_MIN 0.1                // Minimum speed mm/s
#define SPEED_MAX 25.0               // Maximum speed mm/s

// Retraction speed multiplier (faster retract after measurement)
#define RETRACT_SPEED_MULT 2.0

// =============================================================================
// GLOBAL STATE
// =============================================================================

HX711 loadCell;

// Operating mode
enum Mode { MODE_MONITOR, MODE_DRIVE };
Mode currentMode = MODE_MONITOR;

// Speed settings
float linearSpeed = 2.0;             // Current speed in mm/s
int stepDelayUs = 0;                 // Calculated step delay in microseconds

// Streaming state
bool isStreaming = false;
unsigned long streamStartTime = 0;
unsigned long sampleCount = 0;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

void updateStepDelay() {
    // Calculate step delay for given linear speed
    // stepDelay = 1,000,000 / (2 * stepsPerRev * speed)
    stepDelayUs = (int)(1000000.0 / (2.0 * STEPS_PER_REV * linearSpeed));
}

float readForce(int samples = 1) {
    // Read force from load cell
    // Returns force in Newtons
    float units = loadCell.get_units(samples);  // In grams
    float force = units / 1000.0 * 9.81;        // Convert to Newtons (F = m*g)
    return force;
}

void moveStepper(float distanceMm, int delayUs) {
    // Move stepper motor a given distance
    // Positive = extend (down), Negative = retract (up)
    
    if (distanceMm >= 0) {
        digitalWrite(DIR_PIN, HIGH);  // Extend
    } else {
        digitalWrite(DIR_PIN, LOW);   // Retract
    }
    
    long steps = (long)(abs(distanceMm) * (STEPS_PER_REV / SCREW_PITCH));
    
    for (long i = 0; i < steps; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(delayUs);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(delayUs);
    }
}

void sendForceSample() {
    // Send a single force sample in streaming format
    unsigned long timestamp = millis() - streamStartTime;
    float force = readForce(1);
    
    Serial.print("T");
    Serial.print(timestamp);
    Serial.print(",F");
    Serial.println(force, 5);
    
    sampleCount++;
}

void sendStatus() {
    // Send current status as JSON
    Serial.print("STATUS|{\"mode\":\"");
    Serial.print(currentMode == MODE_MONITOR ? "MONITOR" : "DRIVE");
    Serial.print("\",\"speed\":");
    Serial.print(linearSpeed, 2);
    Serial.print(",\"streaming\":");
    Serial.print(isStreaming ? "true" : "false");
    Serial.print(",\"samples\":");
    Serial.print(sampleCount);
    Serial.println("}");
}

// =============================================================================
// DRIVE MODE: Move with force streaming
// =============================================================================

void driveWithForce(float distanceMm) {
    // Move forward while streaming force data, then retract at higher speed
    // This is the main measurement function for DRIVE mode
    
    if (distanceMm <= 0) {
        Serial.println("ERROR|Distance must be positive for DRIVE");
        return;
    }
    
    // Start streaming
    isStreaming = true;
    streamStartTime = millis();
    sampleCount = 0;
    
    Serial.println("DRIVE_START");
    
    // Calculate step delays
    int forwardDelay = stepDelayUs;
    int retractDelay = (int)(stepDelayUs / RETRACT_SPEED_MULT);
    
    // Calculate steps per sample (aim for ~10Hz sampling)
    // At 10Hz with 1mm/s speed, we move 0.1mm per sample
    float sampleInterval = 0.1;  // seconds between samples
    float distancePerSample = linearSpeed * sampleInterval;
    long stepsPerSample = (long)(distancePerSample * (STEPS_PER_REV / SCREW_PITCH));
    if (stepsPerSample < 1) stepsPerSample = 1;
    
    long totalSteps = (long)(distanceMm * (STEPS_PER_REV / SCREW_PITCH));
    
    // === FORWARD MOVEMENT with force streaming ===
    digitalWrite(DIR_PIN, HIGH);  // Extend
    
    for (long step = 0; step < totalSteps; step++) {
        // Check for abort (if force exceeds limit)
        if (step % stepsPerSample == 0) {
            float force = readForce(1);
            unsigned long timestamp = millis() - streamStartTime;
            
            Serial.print("T");
            Serial.print(timestamp);
            Serial.print(",F");
            Serial.println(force, 5);
            sampleCount++;
            
            if (abs(force) >= FORCE_MAX) {
                Serial.println("ABORT|Max force exceeded");
                // Emergency retract
                moveStepper(-step * SCREW_PITCH / STEPS_PER_REV, retractDelay);
                isStreaming = false;
                Serial.print("STOPPED|");
                Serial.println(sampleCount);
                return;
            }
        }
        
        // Step
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(forwardDelay);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(forwardDelay);
        
        // Check for serial abort command
        if (Serial.available() > 0) {
            char c = Serial.peek();
            if (c == 'X' || c == 'x') {
                Serial.read();  // Consume the character
                Serial.println("ABORT|User cancelled");
                moveStepper(-step * SCREW_PITCH / STEPS_PER_REV, retractDelay);
                isStreaming = false;
                Serial.print("STOPPED|");
                Serial.println(sampleCount);
                return;
            }
        }
    }
    
    // Final force reading at max extension
    sendForceSample();
    
    Serial.println("RETRACTING");
    
    // === RETRACTION at higher speed (no force streaming) ===
    moveStepper(-distanceMm, retractDelay);
    
    // Stop streaming
    isStreaming = false;
    
    Serial.print("STOPPED|");
    Serial.println(sampleCount);
}

// =============================================================================
// MONITOR MODE: Force streaming without movement
// =============================================================================

void streamLoop() {
    // Called repeatedly when streaming in MONITOR mode
    // Sends force samples at approximately 10Hz (limited by HX711)
    
    static unsigned long lastSample = 0;
    unsigned long now = millis();
    
    // Sample at ~10Hz (100ms interval)
    if (now - lastSample >= 100) {
        sendForceSample();
        lastSample = now;
    }
    
    // Check for STOP command
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        
        if (cmd == "STOP") {
            isStreaming = false;
            Serial.print("STOPPED|");
            Serial.println(sampleCount);
        }
    }
}

// =============================================================================
// COMMAND PROCESSING
// =============================================================================

void processCommand(String cmd) {
    cmd.trim();
    
    if (cmd.length() == 0) return;
    
    // Get first character for single-char commands
    char firstChar = cmd.charAt(0);
    
    // Check for mode commands
    if (cmd == "M" || cmd == "m") {
        currentMode = MODE_MONITOR;
        Serial.println("OK_MONITOR");
        return;
    }
    
    if (cmd == "D" || cmd == "d") {
        currentMode = MODE_DRIVE;
        Serial.println("OK_DRIVE");
        return;
    }
    
    // Speed command: V <value>
    if ((firstChar == 'V' || firstChar == 'v') && cmd.length() > 1) {
        float newSpeed = cmd.substring(1).toFloat();
        if (newSpeed >= SPEED_MIN && newSpeed <= SPEED_MAX) {
            linearSpeed = newSpeed;
            updateStepDelay();
            Serial.print("OK_SPEED|");
            Serial.println(linearSpeed, 2);
        } else {
            Serial.print("ERROR|Speed must be between ");
            Serial.print(SPEED_MIN);
            Serial.print(" and ");
            Serial.println(SPEED_MAX);
        }
        return;
    }
    
    // Drive command: G <distance>
    if ((firstChar == 'G' || firstChar == 'g') && cmd.length() > 1) {
        if (currentMode != MODE_DRIVE) {
            Serial.println("ERROR|Must be in DRIVE mode. Send 'D' first.");
            return;
        }
        float distance = cmd.substring(1).toFloat();
        driveWithForce(distance);
        return;
    }
    
    // Start streaming (MONITOR mode)
    if (cmd.equalsIgnoreCase("START")) {
        if (currentMode != MODE_MONITOR) {
            Serial.println("ERROR|START only works in MONITOR mode");
            return;
        }
        isStreaming = true;
        streamStartTime = millis();
        sampleCount = 0;
        Serial.println("STREAMING");
        return;
    }
    
    // Stop streaming
    if (cmd.equalsIgnoreCase("STOP")) {
        if (isStreaming) {
            isStreaming = false;
            Serial.print("STOPPED|");
            Serial.println(sampleCount);
        } else {
            Serial.println("OK|Not streaming");
        }
        return;
    }
    
    // Force reading
    if (cmd == "F" || cmd == "f") {
        float force = readForce(1);
        Serial.print("FORCE|");
        Serial.println(force, 5);
        return;
    }
    
    // Zero/tare load cell
    if (cmd == "Z" || cmd == "z") {
        Serial.println("ZEROING...");
        loadCell.tare();
        Serial.println("OK_ZERO");
        return;
    }
    
    // Status query
    if (cmd == "?") {
        sendStatus();
        return;
    }
    
    // Numeric input = manual JOG
    float distance = cmd.toFloat();
    if (distance != 0.0 || cmd == "0" || cmd == "0.0") {
        Serial.print("JOG|");
        Serial.println(distance, 2);
        moveStepper(distance, stepDelayUs);
        Serial.println("OK_JOG");
        return;
    }
    
    // Unknown command
    Serial.print("ERROR|Unknown command: ");
    Serial.println(cmd);
}

// =============================================================================
// SETUP AND MAIN LOOP
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    
    // Initialize load cell
    loadCell.begin(LOADCELL_DOUT, LOADCELL_SCK);
    loadCell.power_down();
    delay(200);
    loadCell.power_up();
    delay(200);
    loadCell.set_scale(CALIBRATION_FACTOR);
    
    // Initialize stepper pins
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    
    // Calculate initial step delay
    updateStepDelay();
    
    // Ready message
    Serial.println("FD_READY");
    sendStatus();
}

void loop() {
    // If streaming in MONITOR mode, handle that
    if (isStreaming && currentMode == MODE_MONITOR) {
        streamLoop();
        return;
    }
    
    // Otherwise, process commands
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }
}
