#!/usr/bin/env python3
"""
motor_cycle_test.py — Bare-bones: connect, setup, send move commands, repeat.

Usage:  python -u motor_cycle_test.py
"""

import time
import serial

COM_PORT = "COM5"
TRAVEL =60_000
CYCLES = 50
MOVE_TIME_S = 80


def main() -> None:
    print(f"Motor Cycle Test — {CYCLES}× back-and-forth, {TRAVEL} steps each way")
    print(f"Port: {COM_PORT}  |  Move time: ~{MOVE_TIME_S}s")
    print("-" * 60)

    ser = serial.Serial(COM_PORT, 115200, timeout=0.2)
    ser.dtr = False
    ser.reset_input_buffer()
    print(f"Connected to {COM_PORT}")
    time.sleep(1.0)
    print("Starting cycles…\n")

    try:
        for cycle in range(1, CYCLES + 1):
            print(f"[{cycle:03d}/{CYCLES}] Backward -{TRAVEL}", flush=True)
            ser.write(f"move {-TRAVEL}\n".encode())
            ser.flush()
            time.sleep(MOVE_TIME_S)

            print(f"[{cycle:03d}/{CYCLES}] Forward  +{TRAVEL}", flush=True)
            ser.write(f"move {TRAVEL}\n".encode())
            ser.flush()
            time.sleep(MOVE_TIME_S)

    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        ser.close()
        print("Done.")


if __name__ == "__main__":
    main()
