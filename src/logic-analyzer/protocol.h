#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "crc8.h"

// Compile-time endianness check - This protocol assumes little-endian architecture
// All multi-byte fields (uint16_t, uint32_t) in packet structures are transmitted in little-endian byte order
// Target platforms: ESP8266, ESP32, ARM Cortex-M (all little-endian)
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "This protocol implementation requires a little-endian architecture (ESP8266, ESP32, ARM)"
  #endif
#elif defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN)
  #error "This protocol implementation requires a little-endian architecture (ESP8266, ESP32, ARM)"
#elif defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
  #error "This protocol implementation requires a little-endian architecture (ESP8266, ESP32, ARM)"
#endif
// Note: If compilation succeeds, the architecture is little-endian or endianness cannot be determined at compile-time
// ESP8266 and ESP32 (Xtensa) and ARM Cortex-M are all little-endian by default

// Protocol constants
#define PROTOCOL_START_BYTE 0xAA
#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0
#define PROTOCOL_VERSION_PATCH 0

// Packet structure: START(1) + TYPE(1) + LENGTH(1) + PAYLOAD(0-255) + CHECKSUM(1)
#define PROTOCOL_RX_BUFFER_SIZE 255       // Receive buffer stores payload only (0-255 bytes max)
#define PROTOCOL_TX_BUFFER_SIZE 259       // Transmit buffer for complete packet (4 + 255)
#define PROTOCOL_MAX_BYTES_PER_UPDATE 32  // Limit bytes processed per update() call to keep main loop responsive

// Message types - Commands (Host → ESP8266) [0x00-0x7F]
enum CommandType : uint8_t {
  CMD_SET_CLOCK_MODE = 0x01,
  CMD_SET_CLOCK_SPEED = 0x02,
  CMD_CLOCK_PULSE = 0x03,
  CMD_START_STREAMING = 0x04,
  CMD_STOP_STREAMING = 0x05,
  CMD_GET_STATUS = 0x06,
  CMD_RESET_COUNTER = 0x07,
  CMD_PING = 0x08,
};

// Message types - Responses/Events (ESP8266 → Host) [0x80-0xFF]
enum ResponseType : uint8_t {
  RESP_ACK = 0x80,
  RESP_ERROR = 0x81,
  RESP_STATUS_REPORT = 0x82,
  RESP_BUS_STATE = 0x83,
  RESP_PONG = 0x84,

  EVENT_CLOCK_MODE_CHANGED = 0x90,
  EVENT_CLOCK_SPEED_CHANGED = 0x91,
};

// Clock modes
enum ProtocolClockMode : uint8_t {
  PROTO_CLOCK_MANUAL = 0x00,
  PROTO_CLOCK_AUTOMATIC = 0x01,
  PROTO_CLOCK_EXT = 0x02,
};

// Error codes
enum ErrorCode : uint8_t {
  ERR_INVALID_COMMAND = 0x01,
  ERR_INVALID_PARAMETER = 0x02,
  ERR_WRONG_MODE = 0x03,
  ERR_CHECKSUM_ERROR = 0x04,
  ERR_BUFFER_OVERFLOW = 0x05,
};

// Packet structure (packed for exact memory layout)
#pragma pack(push, 1)

struct PacketHeader {
  uint8_t startByte;
  uint8_t length;
  uint8_t messageType;
};

struct SetClockModePayload {
  uint8_t mode;
};

struct SetClockSpeedPayload {
  uint8_t index;
};

struct ClockPulsePayload {
  uint16_t duration; // milliseconds (little-endian)
};

struct StartStreamingPayload {
  uint8_t flags; // Reserved for future filtering options
};

struct ErrorPayload {
  uint8_t failedCommand;
  uint8_t errorCode;
};

struct StatusReportPayload {
  uint8_t mode;
  uint8_t clockIndex;
  uint32_t clockCounter; // little-endian
  uint16_t addressBus;   // little-endian
  uint8_t dataBus;
  uint8_t flags;
};

struct BusStatePayload {
  uint32_t clockCounter; // little-endian
  uint16_t addressBus;   // little-endian
  uint8_t dataBus;
  uint8_t flags;
};

struct PongPayload {
  uint8_t versionMajor;
  uint8_t versionMinor;
  uint8_t versionPatch;
  uint32_t uptimeMs; // little-endian
};

#pragma pack(pop)

// Protocol parser state machine
enum ParserState {
  STATE_WAIT_START,
  STATE_WAIT_LENGTH,
  STATE_WAIT_TYPE,
  STATE_WAIT_PAYLOAD,
  STATE_WAIT_CHECKSUM,
};

// Protocol class for handling binary communication
class Protocol {
public:
  Protocol();

  // Initialize protocol
  void begin();

  // Process incoming serial data
  // IMPORTANT: Must be called from main loop only (not thread-safe, not ISR-safe)
  // The parser maintains state across calls and is not protected by locks
  // Returns: text byte (0x20-0x7E) if encountered and not consumed by protocol, or 0
  uint8_t update();

  // Send responses/events
  void sendAck(uint8_t ackedCommand);
  void sendAck(uint8_t ackedCommand, uint8_t dataByte);
  void sendEvent(uint8_t eventType, uint8_t eventData);
  void sendError(uint8_t failedCommand, uint8_t errorCode);
  void sendStatusReport(uint8_t mode, uint8_t clockIndex, uint32_t clockCounter,
                        uint16_t addressBus, uint8_t dataBus, uint8_t flags);
  void sendBusState(uint32_t clockCounter, uint16_t addressBus, uint8_t dataBus, uint8_t flags);
  void sendPong();

  // Command callbacks (set these to handle commands)
  void (*onSetClockMode)(uint8_t mode) = nullptr;
  bool (*onSetClockSpeed)(uint8_t index) = nullptr;  // Returns true if valid index, false if invalid
  bool (*onClockPulse)(uint16_t duration) = nullptr;  // Returns true if pulse executed, false if ignored
  void (*onStartStreaming)(uint8_t flags) = nullptr;
  void (*onStopStreaming)() = nullptr;
  void (*onGetStatus)() = nullptr;
  void (*onResetCounter)() = nullptr;
  void (*onPing)() = nullptr;

  // Get streaming state
  bool isStreaming() const { return streaming; }

  void setStreaming(bool enable) { streaming = enable; }

private:
  // Parser state
  ParserState state;
  uint8_t buffer[PROTOCOL_RX_BUFFER_SIZE];  // Buffer stores payload only (0-255 bytes)
  uint16_t bufferIndex;
  uint8_t messageType;
  uint8_t payloadLength;

  // Streaming control
  bool streaming;

  // Helper functions
  void resetParser();
  void processPacket();
  void sendPacket(uint8_t messageType, const uint8_t* payload, uint8_t payloadLength);

  // Command handlers
  void handleSetClockMode(const uint8_t* payload, uint8_t length);
  void handleSetClockSpeed(const uint8_t* payload, uint8_t length);
  void handleClockPulse(const uint8_t* payload, uint8_t length);
  void handleStartStreaming(const uint8_t* payload, uint8_t length);
  void handleStopStreaming(const uint8_t* payload, uint8_t length);
  void handleGetStatus(const uint8_t* payload, uint8_t length);
  void handleResetCounter(const uint8_t* payload, uint8_t length);
  void handlePing(const uint8_t* payload, uint8_t length);
};

#endif // PROTOCOL_H
