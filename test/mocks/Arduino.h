/*
 * =============================================================================
 * ARDUINO MOCK - Minimal Arduino API for native testing
 * =============================================================================
 * 
 * Provides stubs for Arduino functions that are used in the codebase.
 * This allows unit testing on PC without actual Arduino hardware.
 * 
 * =============================================================================
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <string>
#include <iostream>
#include <algorithm>

// =============================================================================
// BASIC TYPES
// =============================================================================

typedef uint8_t byte;

// =============================================================================
// MATH FUNCTIONS
// =============================================================================

#ifndef min
template <typename T>
inline T min(T a, T b) { return (a < b) ? a : b; }
#endif

#ifndef max
template <typename T>
inline T max(T a, T b) { return (a > b) ? a : b; }
#endif

#ifndef constrain
template <typename T>
inline T constrain(T x, T low, T high) {
    return (x < low) ? low : ((x > high) ? high : x);
}
#endif

// Use std::abs for numeric types
using std::abs;

// sqrtf is provided by cmath, no need to redefine

// =============================================================================
// GPIO MOCK
// =============================================================================

constexpr int INPUT = 0;
constexpr int OUTPUT = 1;
constexpr int INPUT_PULLUP = 2;
constexpr int INPUT_PULLDOWN = 3;

constexpr int HIGH = 1;
constexpr int LOW = 0;

inline void pinMode(uint8_t pin, uint8_t mode) {
    (void)pin; (void)mode;
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    (void)pin; (void)val;
}

inline int digitalRead(uint8_t pin) {
    (void)pin;
    return LOW;
}

// =============================================================================
// TIME MOCK
// =============================================================================

static uint32_t _mockMillis = 0;
static uint32_t _mockMicros = 0;

inline uint32_t millis() { return _mockMillis; }
inline uint32_t micros() { return _mockMicros; }
inline void delay(uint32_t ms) { _mockMillis += ms; _mockMicros += ms * 1000; }
inline void delayMicroseconds(uint32_t us) { _mockMicros += us; }

// Test helper to advance mock time
inline void mockAdvanceTime(uint32_t ms) { _mockMillis += ms; _mockMicros += ms * 1000; }
inline void mockSetTime(uint32_t ms) { _mockMillis = ms; _mockMicros = ms * 1000; }

// =============================================================================
// SERIAL MOCK
// =============================================================================

class MockSerial {
public:
    void begin(uint32_t baud) { (void)baud; }
    void print(const char* s) { std::cout << s; }
    void print(int val) { std::cout << val; }
    void print(float val) { std::cout << val; }
    void print(unsigned int val) { std::cout << val; }
    void println(const char* s) { std::cout << s << std::endl; }
    void println(int val) { std::cout << val << std::endl; }
    void println(float val) { std::cout << val << std::endl; }
    void println() { std::cout << std::endl; }
    void flush() { std::cout.flush(); }
    bool operator!() { return false; }
    explicit operator bool() { return true; }
};

// UART serial mock (for TMC2209)
class MockHardwareSerial : public MockSerial {
public:
    void begin(uint32_t baud, int config, int rxPin, int txPin) {
        (void)baud; (void)config; (void)rxPin; (void)txPin;
    }
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t) { return 1; }
};

extern MockSerial Serial;
extern MockHardwareSerial Serial1;

// UART config constant
constexpr int SERIAL_8N1 = 0;

// =============================================================================
// STRING CLASS (Simplified Arduino String)
// =============================================================================

class String {
public:
    String() : _str() {}
    String(const char* s) : _str(s ? s : "") {}
    String(const std::string& s) : _str(s) {}
    String(int val) : _str(std::to_string(val)) {}
    String(float val) : _str(std::to_string(val)) {}
    String(unsigned int val) : _str(std::to_string(val)) {}
    
    const char* c_str() const { return _str.c_str(); }
    size_t length() const { return _str.length(); }
    
    bool startsWith(const String& prefix) const {
        return _str.rfind(prefix._str, 0) == 0;
    }
    
    bool startsWith(const char* prefix) const {
        return _str.rfind(prefix, 0) == 0;
    }
    
    String substring(size_t from) const {
        if (from >= _str.length()) return String();
        return String(_str.substr(from));
    }
    
    String substring(size_t from, size_t to) const {
        if (from >= _str.length()) return String();
        return String(_str.substr(from, to - from));
    }
    
    int indexOf(char c) const {
        auto pos = _str.find(c);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    
    int indexOf(char c, size_t from) const {
        auto pos = _str.find(c, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    
    void trim() {
        // Trim leading whitespace
        auto start = _str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            _str = "";
            return;
        }
        // Trim trailing whitespace
        auto end = _str.find_last_not_of(" \t\n\r");
        _str = _str.substr(start, end - start + 1);
    }
    
    void toLowerCase() {
        std::transform(_str.begin(), _str.end(), _str.begin(), ::tolower);
    }
    
    int toInt() const {
        try {
            return std::stoi(_str);
        } catch (...) {
            return 0;
        }
    }
    
    float toFloat() const {
        try {
            return std::stof(_str);
        } catch (...) {
            return 0.0f;
        }
    }
    
    bool operator==(const String& other) const { return _str == other._str; }
    bool operator==(const char* other) const { return _str == other; }
    bool operator!=(const String& other) const { return _str != other._str; }
    
    String operator+(const String& other) const { return String(_str + other._str); }
    String operator+(const char* other) const { return String(_str + other); }
    
    char operator[](size_t idx) const { return _str[idx]; }
    
private:
    std::string _str;
};

// =============================================================================
// LEDC (PWM) MOCK
// =============================================================================

inline void ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution) {
    (void)channel; (void)freq; (void)resolution;
}

inline void ledcAttachPin(uint8_t pin, uint8_t channel) {
    (void)pin; (void)channel;
}

inline void ledcWrite(uint8_t channel, uint32_t duty) {
    (void)channel; (void)duty;
}
