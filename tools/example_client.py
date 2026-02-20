#!/usr/bin/env python3
"""
Simple example of using the Logic Analyzer binary protocol client.

This script demonstrates basic usage: connecting, testing the connection,
getting status, and streaming bus states.
"""

import sys
import time
from logic_analyzer_client import LogicAnalyzerClient

def main():
    # Configuration
    PORT = '/dev/ttyUSB0'  # Change to your serial port (COM3 on Windows)
    BAUDRATE = 115200

    # Create and connect client
    print(f"Connecting to {PORT} at {BAUDRATE} baud...")
    client = LogicAnalyzerClient(PORT, BAUDRATE)

    if not client.connect():
        print("Failed to connect!")
        sys.exit(1)

    print("Connected!")

    try:
        # Test connection
        print("\n--- Testing Connection ---")
        pong = client.ping()
        print(f"Device Version: {pong.version_major}.{pong.version_minor}.{pong.version_patch}")
        print(f"Device Uptime: {pong.uptime_ms/1000:.1f} seconds")

        # Get status
        print("\n--- Current Status ---")
        status = client.get_status()
        print(status)

        # Set to automatic mode
        print("\n--- Setting Clock Mode ---")
        client.set_clock_mode('automatic')
        print("Clock mode set to AUTOMATIC")

        # Set clock speed
        print("\n--- Setting Clock Speed ---")
        success, valid = client.set_clock_speed(1)  # 1 Hz for easy observation
        if success and valid:
            print("Clock speed set to 1 Hz (index 1)")
        elif success and not valid:
            print("ERROR: Invalid clock speed index")
        else:
            print("ERROR: Command failed")

        # Stream bus states
        print("\n--- Streaming Bus States ---")
        print("Press Ctrl+C to stop...\n")

        packet_count = 0

        def on_bus_state(state):
            nonlocal packet_count
            packet_count += 1

            # Only print instruction fetches (when SYNC is set)
            if state.sync:
                print(state)

        client.on_bus_state = on_bus_state
        client.start_streaming()

        # Stream for a while
        try:
            while True:
                client.update()
        except KeyboardInterrupt:
            print(f"\n\nReceived {packet_count} packets")

        # Stop streaming
        client.stop_streaming()
        print("Streaming stopped")

        # Get final status
        print("\n--- Final Status ---")
        status = client.get_status()
        print(status)

    finally:
        client.disconnect()
        print("\nDisconnected")

if __name__ == '__main__':
    main()
