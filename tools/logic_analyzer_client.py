#!/usr/bin/env python3
"""
Logic Analyzer Binary Protocol Client Library

This module provides a Python client for communicating with the ESP8266
logic analyzer using the binary protocol.

Usage:
    from logic_analyzer_client import LogicAnalyzerClient

    client = LogicAnalyzerClient('/dev/ttyUSB0', 115200)
    client.connect()

    # Set clock mode
    client.set_clock_mode('automatic')

    # Start streaming (shows bus states + clock mode/speed change events)
    def on_bus_state(state):
        print(f"[BUS] Clock: {state.clock_counter}, Addr: 0x{state.address_bus:04X}, Data: 0x{state.data_bus:02X}")

    def on_mode_changed(mode):
        print(f"[EVENT] Clock mode: {mode.name}")

    def on_speed_changed(index):
        print(f"[EVENT] Clock speed index: {index}")

    client.on_bus_state = on_bus_state
    client.on_clock_mode_changed = on_mode_changed
    client.on_clock_speed_changed = on_speed_changed
    client.start_streaming()

    # Keep receiving data
    while True:
        client.update()
"""

import serial
from serial import SerialException
import struct
import time
import logging
from enum import IntEnum
from typing import Optional, Callable, Tuple
from dataclasses import dataclass
from crc import Calculator, Configuration

# Initialize logger
logger = logging.getLogger(__name__)

# Protocol Constants
PROTOCOL_START_BYTE = 0xAA
PROTOCOL_VERSION_MAJOR = 2
PROTOCOL_VERSION_MINOR = 0
PROTOCOL_VERSION_PATCH = 0

# CRC-8 configuration (polynomial: 0x07, init: 0x00, no XOR out)
# CRC-8-CCITT: commonly used, good error detection properties
# This matches the firmware's CRC-8 implementation
CRC8_CONFIG = Configuration(
    width=8,
    polynomial=0x07,
    init_value=0x00,
    final_xor_value=0x00,
    reverse_input=False,
    reverse_output=False,
)

# Packet structure: START(1) + TYPE(1) + LENGTH(1) + PAYLOAD(0-255) + CHECKSUM(1)
PROTOCOL_MAX_PAYLOAD_SIZE = 255   # Maximum payload size (0-255 bytes)


class CommandType(IntEnum):
    """Commands from host to ESP8266"""
    SET_CLOCK_MODE = 0x01
    SET_CLOCK_SPEED = 0x02
    CLOCK_PULSE = 0x03
    START_STREAMING = 0x04
    STOP_STREAMING = 0x05
    GET_STATUS = 0x06
    RESET_COUNTER = 0x07
    PING = 0x08


class ResponseType(IntEnum):
    """Responses/Events from ESP8266 to host"""
    ACK = 0x80
    ERROR = 0x81
    STATUS_REPORT = 0x82
    BUS_STATE = 0x83
    PONG = 0x84

    # Events
    EVENT_CLOCK_MODE_CHANGED = 0x90
    EVENT_CLOCK_SPEED_CHANGED = 0x91


class ClockMode(IntEnum):
    """Clock operating modes"""
    MANUAL = 0x00
    AUTOMATIC = 0x01


class ErrorCode(IntEnum):
    """Protocol error codes"""
    INVALID_COMMAND = 0x01
    INVALID_PARAMETER = 0x02
    WRONG_MODE = 0x03
    CHECKSUM_ERROR = 0x04
    BUFFER_OVERFLOW = 0x05


@dataclass
class StatusReport:
    """Current analyzer status"""
    mode: ClockMode
    clock_index: int
    clock_counter: int
    address_bus: int
    data_bus: int
    flags: int

    def __str__(self):
        mode_str = "Automatic" if self.mode == ClockMode.AUTOMATIC else "Manual"
        return (f"Mode: {mode_str}, Clock Index: {self.clock_index}, "
                f"Counter: {self.clock_counter}, "
                f"Addr: 0x{self.address_bus:04X}, Data: 0x{self.data_bus:02X}, "
                f"Flags: 0x{self.flags:02X}")


@dataclass
class BusState:
    """Bus state event"""
    clock_counter: int
    address_bus: int
    data_bus: int
    flags: int

    @property
    def rwb(self) -> bool:
        """Read/Write bar (True = Read, False = Write)"""
        return bool(self.flags & 0x01)

    @property
    def sync(self) -> bool:
        """SYNC flag (instruction fetch)"""
        return bool(self.flags & 0x02)

    @property
    def irqb(self) -> bool:
        """IRQ bar (active low)"""
        return bool(self.flags & 0x04)

    @property
    def resetb(self) -> bool:
        """Reset bar (active low)"""
        return bool(self.flags & 0x08)

    @property
    def clock(self) -> bool:
        """Clock signal"""
        return bool(self.flags & 0x80)

    def __str__(self):
        rw_char = 'R' if self.rwb else 'W'
        sync_char = 'S' if self.sync else '-'
        return (f"[{self.clock_counter:5d}] {rw_char}{sync_char} "
                f"Addr: 0x{self.address_bus:04X} Data: 0x{self.data_bus:02X} "
                f"Flags: 0x{self.flags:02X}")


@dataclass
class PongResponse:
    """Response to PING command"""
    version_major: int
    version_minor: int
    version_patch: int
    uptime_ms: int

    def __str__(self):
        return (f"Version: {self.version_major}.{self.version_minor}.{self.version_patch}, "
                f"Uptime: {self.uptime_ms}ms ({self.uptime_ms/1000:.1f}s)")


class ProtocolError(Exception):
    """Protocol-related error"""
    def __init__(self, error_code: ErrorCode, failed_command: int, message: str = ""):
        self.error_code = error_code
        self.failed_command = failed_command
        self.message = message or f"Error {error_code.name} for command 0x{failed_command:02X}"
        super().__init__(self.message)


class LogicAnalyzerClient:
    """Client for ESP8266 Logic Analyzer binary protocol"""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 1.0):
        """
        Initialize the client.

        Args:
            port: Serial port path (e.g., '/dev/ttyUSB0' or 'COM3')
            baudrate: Serial baudrate (default: 115200)
            timeout: Read timeout in seconds (default: 1.0)
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial: Optional[serial.Serial] = None

        # CRC-8 calculator (using optimized table-based implementation)
        self._crc_calculator = Calculator(CRC8_CONFIG, optimized=True)

        # Parser state
        self._buffer = bytearray()
        self._message_type = 0
        self._payload_length = 0
        self._state = 'WAIT_START'
        self._discarded_bytes = bytearray()

        # Callbacks
        self.on_bus_state: Optional[Callable[[BusState], None]] = None
        self.on_status_report: Optional[Callable[[StatusReport], None]] = None
        self.on_pong: Optional[Callable[[PongResponse], None]] = None
        self.on_error: Optional[Callable[[ProtocolError], None]] = None
        self.on_clock_mode_changed: Optional[Callable[[ClockMode], None]] = None
        self.on_clock_speed_changed: Optional[Callable[[int], None]] = None

        # Response waiting
        self._waiting_response = False
        self._response_data = None
        self._response_timeout = 0

    def connect(self) -> bool:
        """
        Open serial connection.

        Returns:
            True if connected successfully
        """
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            time.sleep(0.1)  # Allow connection to stabilize
            return True
        except Exception as e:
            print(f"Failed to connect: {e}")
            return False

    def disconnect(self):
        """Close serial connection"""
        if self.serial and self.serial.is_open:
            self.serial.close()

    def _send_packet(self, message_type: int, payload: bytes = b''):
        """
        Send a protocol packet.

        Args:
            message_type: Message type byte
            payload: Payload bytes (optional, 0-255 bytes max)

        Raises:
            RuntimeError: If serial port is not open, or if serial write/flush fails
            ValueError: If payload exceeds maximum size (255 bytes)
        """
        if not self.serial or not self.serial.is_open:
            raise RuntimeError("Serial port not open")

        payload_length = len(payload)  # Can be > 255 in Python!

        # Validate payload size (Python doesn't have uint8_t constraint)
        if payload_length > PROTOCOL_MAX_PAYLOAD_SIZE:
            raise ValueError(f"Payload too large: {payload_length} bytes (max {PROTOCOL_MAX_PAYLOAD_SIZE})")

        # Calculate CRC-8 checksum (type + length + payload)
        # Use bytearray to avoid temporary object creation during concatenation
        checksum_data = bytearray([message_type, payload_length])
        checksum_data.extend(payload)
        checksum = self._crc_calculator.checksum(checksum_data)

        # Build complete packet: START + TYPE + LENGTH + PAYLOAD + CHECKSUM
        # Use bytearray to avoid temporary object creation during concatenation
        packet = bytearray([PROTOCOL_START_BYTE, message_type, payload_length])
        packet.extend(payload)
        packet.append(checksum)

        # Send packet with exception handling
        try:
            self.serial.write(packet)
            self.serial.flush()
        except SerialException as e:
            logger.error(f"Failed to send packet (type=0x{message_type:02X}, length={payload_length}): {e}")
            raise RuntimeError(f"Failed to send packet (type=0x{message_type:02X}): {e}") from e
        except OSError as e:
            logger.error(f"I/O error while sending packet (type=0x{message_type:02X}, length={payload_length}): {e}")
            raise RuntimeError(f"I/O error while sending packet (type=0x{message_type:02X}): {e}") from e

    def _parse_packet(self, packet: bytes):
        """
        Parse and handle a received packet.

        Args:
            packet: Complete packet bytes (without start byte)
                    Format: TYPE(1) + LENGTH(1) + PAYLOAD(0-255) + CHECKSUM(1)
        """
        if len(packet) < 3:
            return

        message_type = packet[0]      # TYPE comes first
        payload_length = packet[1]    # LENGTH is payload size only (0-255)

        # Extract payload and checksum
        if payload_length > 0:
            if len(packet) < 2 + payload_length + 1:
                print(f"Incomplete packet: expected {2 + payload_length + 1} bytes, got {len(packet)}")
                return
            payload = packet[2:2+payload_length]
            checksum = packet[2+payload_length]
        else:
            payload = b''
            checksum = packet[2]

        # Verify checksum (type + length + payload)
        checksum_data = packet[:2 + payload_length]  # type + length + payload
        expected_checksum = self._crc_calculator.checksum(checksum_data)

        if checksum != expected_checksum:
            print(f"CRC-8 checksum error: expected 0x{expected_checksum:02X}, got 0x{checksum:02X}")
            return

        # Handle message types
        if message_type == ResponseType.ACK:
            self._handle_ack(payload)
        elif message_type == ResponseType.ERROR:
            self._handle_error(payload)
        elif message_type == ResponseType.STATUS_REPORT:
            self._handle_status_report(payload)
        elif message_type == ResponseType.BUS_STATE:
            self._handle_bus_state(payload)
        elif message_type == ResponseType.PONG:
            self._handle_pong(payload)
        elif message_type == ResponseType.EVENT_CLOCK_MODE_CHANGED:
            self._handle_clock_mode_changed(payload)
        elif message_type == ResponseType.EVENT_CLOCK_SPEED_CHANGED:
            self._handle_clock_speed_changed(payload)
        else:
            print(f"Unknown message type: 0x{message_type:02X} {payload_length}: {payload.hex()}")

    def _process_discarded_bytes(self):
        """Process bytes that were discarded while waiting for start byte (for debugging)"""
        if self._discarded_bytes:

            # Try to decode as ASCII for debugging
            try:
                ascii_str = self._discarded_bytes.decode('ascii', errors='replace')
                print(f"Discarded TXT({len(self._discarded_bytes)}): {ascii_str}")
            except Exception as e:
                print(f"Discarded BIN({len(self._discarded_bytes)}): {self._discarded_bytes.hex()}")

            self._discarded_bytes.clear()


    def _handle_ack(self, payload: bytes):
        """Handle ACK response"""
        if len(payload) >= 1:
            acked_cmd = payload[0]
            # print(f"ACK for command 0x{acked_cmd:02X}")
            # For CLOCK_PULSE, payload[1] contains status: 0x00=success/executed, 0x01=error/ignored
            status_byte = payload[1] if len(payload) >= 2 else None
            self._response_data = ('ACK', acked_cmd, status_byte)

    def _handle_error(self, payload: bytes):
        """Handle ERROR response"""
        if len(payload) >= 2:
            failed_cmd = payload[0]
            error_code = ErrorCode(payload[1])
            error = ProtocolError(error_code, failed_cmd)
            print(f"Protocol Error: {error}")
            if self.on_error:
                self.on_error(error)
            self._response_data = ('ERROR', error)

    def _handle_status_report(self, payload: bytes):
        """Handle STATUS_REPORT response"""
        if len(payload) >= 10:
            mode, clock_index, counter, address, data, flags = struct.unpack('<BBIHBB', payload)
            status = StatusReport(
                mode=ClockMode(mode),
                clock_index=clock_index,
                clock_counter=counter,
                address_bus=address,
                data_bus=data,
                flags=flags
            )
            if self.on_status_report:
                self.on_status_report(status)
            self._response_data = ('STATUS', status)

    def _handle_bus_state(self, payload: bytes):
        """Handle BUS_STATE event"""
        if len(payload) >= 8:
            counter, address, data, flags = struct.unpack('<IHBB', payload)
            bus_state = BusState(
                clock_counter=counter,
                address_bus=address,
                data_bus=data,
                flags=flags
            )
            if self.on_bus_state:
                self.on_bus_state(bus_state)

    def _handle_pong(self, payload: bytes):
        """Handle PONG response"""
        if len(payload) >= 7:
            major, minor, patch, uptime = struct.unpack('<BBBI', payload)
            pong = PongResponse(
                version_major=major,
                version_minor=minor,
                version_patch=patch,
                uptime_ms=uptime
            )
            if self.on_pong:
                self.on_pong(pong)
            self._response_data = ('PONG', pong)

    def _handle_clock_mode_changed(self, payload: bytes):
        """Handle EVENT_CLOCK_MODE_CHANGED event"""
        if len(payload) >= 1:
            mode = ClockMode(payload[0])
            if self.on_clock_mode_changed:
                self.on_clock_mode_changed(mode)

    def _handle_clock_speed_changed(self, payload: bytes):
        """Handle EVENT_CLOCK_SPEED_CHANGED event"""
        if len(payload) >= 1:
            clock_index = payload[0]
            if self.on_clock_speed_changed:
                self.on_clock_speed_changed(clock_index)

    def update(self, timeout: Optional[float] = None):
        """
        Process one byte of incoming data (call regularly in a loop).

        This method reads and processes a single byte from the serial port.
        It should be called repeatedly in a loop to continuously process data.

        Args:
            timeout: Override default read timeout in seconds (optional).
                     If None, uses the serial port's default timeout.
                     If 0, performs non-blocking read.
                     If > 0, waits up to timeout seconds for a byte.

        Note:
            Serial communication errors (SerialException, OSError) are caught
            and logged at DEBUG level, but not raised. This allows streaming
            loops to continue gracefully if the device disconnects temporarily.
            Check serial.is_open if you need to detect disconnection.
        """
        if not self.serial or not self.serial.is_open:
            return

        # Set timeout if specified
        old_timeout = None
        if timeout is not None:
            old_timeout = self.serial.timeout
            self.serial.timeout = timeout

        # Read and process one byte with exception handling
        try:
            byte = self.serial.read(1)
            if byte:
                self._process_byte(byte[0])
        except SerialException as e:
            # Device disconnected or communication error
            # Log but don't raise - allows graceful degradation in streaming loops
            # Calling code can check serial.is_open if needed
            logger.debug(f"Serial read error (device may be disconnected): {e}")
        except OSError as e:
            # I/O error - also handle gracefully
            logger.debug(f"I/O error during serial read: {e}")

        # Restore original timeout
        if old_timeout is not None:
            self.serial.timeout = old_timeout

    def _process_byte(self, byte: int):
        """Process a single received byte"""

        if self._state == 'WAIT_START':
            if byte == PROTOCOL_START_BYTE:
                # process previously discarded bytes for debugging
                self._process_discarded_bytes()

                self._buffer = bytearray([byte])
                self._state = 'WAIT_TYPE'  # TYPE comes after START
            else:
                # Collect discarded bytes for debugging
                self._discarded_bytes.append(byte)

        elif self._state == 'WAIT_TYPE':
            self._message_type = byte
            self._buffer.append(byte)
            self._state = 'WAIT_LENGTH'  # LENGTH comes after TYPE

        elif self._state == 'WAIT_LENGTH':
            self._payload_length = byte  # LENGTH is payload size directly (0-255)
            self._buffer.append(byte)
            self._state = 'WAIT_DATA'  # Wait for PAYLOAD + CHECKSUM

        elif self._state == 'WAIT_DATA':
            self._buffer.append(byte)
            # Expected: TYPE(1) + LENGTH(1) + PAYLOAD(n) + CHECKSUM(1)
            # We have START in buffer[0], so total expected = 1 + 1 + 1 + n + 1 = 4 + n
            expected_len = 4 + self._payload_length
            if len(self._buffer) >= expected_len:
                # Complete packet received
                self._parse_packet(bytes(self._buffer[1:]))  # Skip start byte
                self._state = 'WAIT_START'

        else:
            print(f"Invalid parser state: {self._state}")

    def _send_and_wait(self, message_type: int, payload: bytes = b'', timeout: float = 1.0) -> Tuple:
        """
        Send command and wait for response.

        Args:
            message_type: Command type
            payload: Command payload
            timeout: Response timeout in seconds

        Returns:
            Tuple of (response_type, response_data)
        """
        self._response_data = None
        self._send_packet(message_type, payload)

        # Wait for response
        start_time = time.time()
        while time.time() - start_time < timeout:
            self.update(timeout=0.01)
            if self._response_data:
                return self._response_data
            time.sleep(0.001)

        raise TimeoutError(f"No response received for command 0x{message_type:02X}")

    # Command methods

    def set_clock_mode(self, mode: str) -> bool:
        """
        Set clock mode.

        Args:
            mode: 'manual' or 'automatic'

        Returns:
            True if successful
        """
        mode_value = ClockMode.AUTOMATIC if mode.lower() == 'automatic' else ClockMode.MANUAL
        payload = struct.pack('B', mode_value)
        response = self._send_and_wait(CommandType.SET_CLOCK_MODE, payload)
        return response[0] == 'ACK'

    def set_clock_speed(self, index: int) -> tuple[bool, bool]:
        """
        Set clock speed by index (0-11).

        Args:
            index: Clock speed index (0=stopped, 1=1Hz, 2=2Hz, ... 11=100Hz)

        Returns:
            Tuple of (success, valid) where:
            - success: True if command was acknowledged
            - valid: True if index was valid and speed changed, False if index was invalid
              Status byte: 0x00 = success/valid, 0x01 = error/invalid index
        """
        payload = struct.pack('B', index)
        response = self._send_and_wait(CommandType.SET_CLOCK_SPEED, payload)

        if response[0] == 'ACK':
            # response is ('ACK', acked_cmd, status_byte)
            status_byte = response[2] if len(response) >= 3 and response[2] is not None else None
            valid = (status_byte == 0x00) if status_byte is not None else False
            return (True, valid)

        return (False, False)

    def clock_pulse(self, duration: int = 100) -> tuple[bool, bool]:
        """
        Trigger single clock pulse (manual mode only).

        Args:
            duration: Pulse duration in milliseconds (default: 100)

        Returns:
            Tuple of (success, executed) where:
            - success: True if command was acknowledged
            - executed: True if pulse was executed, False if ignored (automatic mode)
              Status byte: 0x00 = success/executed, 0x01 = error/ignored (wrong mode)
        """
        payload = struct.pack('<H', duration)
        response = self._send_and_wait(CommandType.CLOCK_PULSE, payload)

        if response[0] == 'ACK':
            # response is ('ACK', acked_cmd, status_byte)
            status_byte = response[2] if len(response) >= 3 and response[2] is not None else None
            executed = (status_byte == 0x00) if status_byte is not None else False
            return (True, executed)

        return (False, False)

    def start_streaming(self, flags: int = 0) -> bool:
        """
        Start streaming bus state changes.

        Args:
            flags: Reserved for future use (default: 0)

        Returns:
            True if successful
        """
        payload = struct.pack('B', flags)
        response = self._send_and_wait(CommandType.START_STREAMING, payload)
        return response[0] == 'ACK'

    def stop_streaming(self) -> bool:
        """
        Stop streaming bus state changes.

        Returns:
            True if successful
        """
        response = self._send_and_wait(CommandType.STOP_STREAMING)
        return response[0] == 'ACK'

    def get_status(self) -> StatusReport:
        """
        Get current analyzer status.

        Returns:
            StatusReport object
        """
        response = self._send_and_wait(CommandType.GET_STATUS)
        if response[0] == 'STATUS':
            return response[1]
        raise RuntimeError("Failed to get status")

    def reset_counter(self) -> bool:
        """
        Reset clock counter to 0.

        Returns:
            True if successful
        """
        response = self._send_and_wait(CommandType.RESET_COUNTER)
        return response[0] == 'ACK'

    def ping(self) -> PongResponse:
        """
        Ping the device.

        Returns:
            PongResponse with version and uptime
        """
        response = self._send_and_wait(CommandType.PING)
        if response[0] == 'PONG':
            return response[1]
        raise RuntimeError("Failed to ping device")


# Command-line interface
if __name__ == '__main__':
    import argparse
    import sys

    parser = argparse.ArgumentParser(description='Logic Analyzer Client')
    parser.add_argument('port', help='Serial port (e.g., /dev/ttyUSB0 or COM3)')
    parser.add_argument('--baudrate', type=int, default=115200, help='Baudrate (default: 115200)')
    parser.add_argument('--ping', action='store_true', help='Ping the device')
    parser.add_argument('--status', action='store_true', help='Get status')
    parser.add_argument('--mode', choices=['manual', 'automatic'], help='Set clock mode')
    parser.add_argument('--speed', type=int, metavar='INDEX', help='Set clock speed index (0-11)')
    parser.add_argument('--pulse', type=int, metavar='DURATION', nargs='?', const=100, help='Single clock pulse')
    parser.add_argument('--stream', action='store_true', help='Start streaming (shows bus states and events)')
    parser.add_argument('--reset', action='store_true', help='Reset clock counter')

    args = parser.parse_args()

    # Create client
    client = LogicAnalyzerClient(args.port, args.baudrate)

    if not client.connect():
        print(f"Failed to connect to {args.port}")
        sys.exit(1)

    print(f"Connected to {args.port} at {args.baudrate} baud")

    try:
        # Execute commands
        if args.ping:
            pong = client.ping()
            print(f"PONG: {pong}")

        if args.status:
            status = client.get_status()
            print(f"Status: {status}")

        if args.mode:
            if client.set_clock_mode(args.mode):
                print(f"Clock mode set to {args.mode}")

        if args.speed is not None:
            success, valid = client.set_clock_speed(args.speed)
            if success and valid:
                print(f"Clock speed set to index {args.speed}")
            elif success and not valid:
                print(f"ERROR: Invalid clock speed index {args.speed}")
            else:
                print(f"ERROR: Failed to set clock speed")

        if args.pulse is not None:
            success, executed = client.clock_pulse(args.pulse)
            if success and executed:
                print(f"Clock pulse triggered ({args.pulse}ms)")
            elif success and not executed:
                print(f"Clock pulse ignored (device in automatic mode)")
            else:
                print(f"ERROR: Failed to trigger clock pulse")

        if args.reset:
            if client.reset_counter():
                print("Clock counter reset")

        if args.stream:
            print("Starting streaming... (Press Ctrl+C to stop)")

            # Subscribe to ALL callbacks - bus states AND events
            def print_bus_state(state: BusState):
                print(f"[BUS]   {state}")

            def print_clock_mode_changed(mode: ClockMode):
                mode_str = "Automatic" if mode == ClockMode.AUTOMATIC else "Manual"
                print(f"[EVENT] Clock mode: {mode_str}")

            def print_clock_speed_changed(index: int):
                print(f"[EVENT] Clock speed: index {index}")

            # Register all callbacks
            client.on_bus_state = print_bus_state
            client.on_clock_mode_changed = print_clock_mode_changed
            client.on_clock_speed_changed = print_clock_speed_changed

            # Start streaming
            client.start_streaming()

            try:
                while True:
                    client.update()
            except KeyboardInterrupt:
                print("\nStopping streaming...")
                client.stop_streaming()

    finally:
        client.disconnect()
        print("Disconnected")
