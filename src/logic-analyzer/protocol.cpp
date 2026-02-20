// #define DEBUG_VERBOSE_ENABLED

#include "protocol.h"
#include "debug.h"

Protocol::Protocol()
  : state(STATE_WAIT_START),
    bufferIndex(0),
    messageType(0),
    payloadLength(0),
    streaming(false) {
}

void Protocol::begin() {
  resetParser();
  streaming = false;
}

void Protocol::resetParser() {
  state = STATE_WAIT_START;
  bufferIndex = 0;
  messageType = 0;
  payloadLength = 0;
}

uint8_t Protocol::update() {
  uint8_t bytesProcessed = 0;

  while (Serial.available() > 0 && bytesProcessed < PROTOCOL_MAX_BYTES_PER_UPDATE) {
    uint8_t byte = Serial.read();
    bytesProcessed++;

    switch (state) {
      case STATE_WAIT_START:
        if (byte == PROTOCOL_START_BYTE) {
          // Don't store start byte in buffer - buffer is for payload only
          bufferIndex = 0;
          state = STATE_WAIT_TYPE;  // TYPE comes after START
        }
        // If not start byte, check if it's a text command (backward compatibility)
        // 0x20 = first printable ASCII, 0x7E - end of 7-bit ASCII
        else if (byte >= 0x20 && byte <= 0x7E) {
          // Text command byte - return it for legacy text handler
          return byte;
        }
        // Other bytes (non-printable, non-protocol) are silently ignored
        break;

      case STATE_WAIT_TYPE:
        messageType = byte;
        // Don't store type in buffer - it's in the messageType field
        state = STATE_WAIT_LENGTH;  // LENGTH comes after TYPE
        break;

      case STATE_WAIT_LENGTH:
        payloadLength = byte;  // LENGTH byte is payload size directly (0-255)

        if (payloadLength == 0) {
          // No payload, go directly to checksum
          state = STATE_WAIT_CHECKSUM;
        } else {
          // Have payload, read it next
          state = STATE_WAIT_PAYLOAD;
        }
        break;

      case STATE_WAIT_PAYLOAD:
        // Overflow protection - check BEFORE writing to buffer
        if (bufferIndex >= PROTOCOL_RX_BUFFER_SIZE) {
          DEBUG_ERROR("Buffer overflow prevented in PAYLOAD state\r\n");
          sendError(messageType, ERR_BUFFER_OVERFLOW);
          resetParser();
          break;
        }

        buffer[bufferIndex++] = byte;

        // Check if we've received all payload bytes
        if (bufferIndex >= payloadLength) {
          state = STATE_WAIT_CHECKSUM;
        }
        break;

      case STATE_WAIT_CHECKSUM:
        // Don't store checksum in buffer, just validate it
        // Calculate expected CRC-8 checksum (type + length + payload)
        uint8_t expectedChecksum = crc8_update(CRC8_INIT, messageType);
        expectedChecksum = crc8_update(expectedChecksum, payloadLength);

        // Add payload to CRC if present
        if (payloadLength > 0) {
          expectedChecksum = crc8_update(expectedChecksum, buffer, payloadLength);
        }

        // Compare with received checksum byte
        if (byte == expectedChecksum) {
          // Valid packet, process it
          processPacket();
        } else {
          // Checksum failed
          DEBUG_ERROR("CRC-8 checksum mismatch: expected=0x%02X got=0x%02X\r\n",
                     expectedChecksum, byte);
          sendError(messageType, ERR_CHECKSUM_ERROR);
        }

        resetParser();
        break;
    }
  }
  return 0;  // No text byte encountered, all bytes consumed by protocol
}

void Protocol::processPacket() {
  const uint8_t* payload = buffer; // Payload starts at buffer[0] (no header bytes stored)

  switch (messageType) {
    case CMD_SET_CLOCK_MODE:
      handleSetClockMode(payload, payloadLength);
      break;

    case CMD_SET_CLOCK_SPEED:
      handleSetClockSpeed(payload, payloadLength);
      break;

    case CMD_CLOCK_PULSE:
      handleClockPulse(payload, payloadLength);
      break;

    case CMD_START_STREAMING:
      handleStartStreaming(payload, payloadLength);
      break;

    case CMD_STOP_STREAMING:
      handleStopStreaming(payload, payloadLength);
      break;

    case CMD_GET_STATUS:
      handleGetStatus(payload, payloadLength);
      break;

    case CMD_RESET_COUNTER:
      handleResetCounter(payload, payloadLength);
      break;

    case CMD_PING:
      handlePing(payload, payloadLength);
      break;

    default:
      DEBUG_ERROR("[ERROR] Invalid command: 0x%02X\r\n", messageType);
      sendError(messageType, ERR_INVALID_COMMAND);
      break;
  }
}

void Protocol::sendPacket(uint8_t msgType, const uint8_t* payload, uint8_t payloadLen) {
  DEBUG_VERBOSE("[TRACE] Sending packet: type=0x%02X payloadLen=%d\r\n", msgType, payloadLen);

  // Calculate CRC-8 checksum (type + length + payload)
  uint8_t checksum = crc8_update(CRC8_INIT, msgType);
  checksum = crc8_update(checksum, payloadLen);

  // Add payload to CRC if present
  if (payloadLen > 0 && payload != nullptr) {
    checksum = crc8_update(checksum, payload, payloadLen);
  }

  // Build complete packet in buffer for single write
  // Packet structure: START(1) + TYPE(1) + LENGTH(1) + PAYLOAD(0-255) + CHECKSUM(1)
  uint8_t packet[PROTOCOL_TX_BUFFER_SIZE];
  uint8_t idx = 0;

  packet[idx++] = PROTOCOL_START_BYTE;
  packet[idx++] = msgType;       // TYPE comes first
  packet[idx++] = payloadLen;    // LENGTH is payload size only (0-255)

  if (payloadLen > 0 && payload != nullptr) {
    memcpy(packet + idx, payload, payloadLen);
    idx += payloadLen;
  }

  packet[idx++] = checksum;

  // Single write for better performance
  Serial.write(packet, idx);

  DEBUG_VERBOSE("[TRACE] Packet sent: checksum=0x%02X totalBytes=%d\r\n", checksum, idx);
}

// Response senders
void Protocol::sendAck(uint8_t ackedCommand) {
  uint8_t payload[1] = { ackedCommand };
  sendPacket(RESP_ACK, payload, 1);
}

void Protocol::sendAck(uint8_t ackedCommand, uint8_t dataByte) {
  uint8_t payload[2] = { ackedCommand, dataByte };
  sendPacket(RESP_ACK, payload, 2);
}

void Protocol::sendError(uint8_t failedCommand, uint8_t errorCode) {
  ErrorPayload payload;
  payload.failedCommand = failedCommand;
  payload.errorCode = errorCode;
  sendPacket(RESP_ERROR, (uint8_t*)&payload, sizeof(payload));
}

void Protocol::sendStatusReport(uint8_t mode, uint8_t clockIndex, uint32_t clockCounter,
                                uint16_t addressBus, uint8_t dataBus, uint8_t flags) {
  StatusReportPayload payload;
  payload.mode = mode;
  payload.clockIndex = clockIndex;
  payload.clockCounter = clockCounter;
  payload.addressBus = addressBus;
  payload.dataBus = dataBus;
  payload.flags = flags;

  DEBUG_VERBOSE("[TRACE] StatusReportPayload size=%d\r\n", sizeof(StatusReportPayload));
  DEBUG_VERBOSE("[TRACE] payload size=%d\r\n", sizeof(payload));

  sendPacket(RESP_STATUS_REPORT, (uint8_t*)&payload, sizeof(payload));
}

void Protocol::sendBusState(uint32_t clockCounter, uint16_t addressBus, uint8_t dataBus, uint8_t flags) {
  BusStatePayload payload;
  payload.clockCounter = clockCounter;
  payload.addressBus = addressBus;
  payload.dataBus = dataBus;
  payload.flags = flags;
  sendPacket(RESP_BUS_STATE, (uint8_t*)&payload, sizeof(payload));
}

void Protocol::sendPong() {
  PongPayload payload;
  payload.versionMajor = PROTOCOL_VERSION_MAJOR;
  payload.versionMinor = PROTOCOL_VERSION_MINOR;
  payload.versionPatch = PROTOCOL_VERSION_PATCH;
  payload.uptimeMs = millis();
  sendPacket(RESP_PONG, (uint8_t*)&payload, sizeof(payload));
}

// Command handlers
void Protocol::handleSetClockMode(const uint8_t* payload, uint8_t length) {
  if (length != sizeof(SetClockModePayload)) {
    DEBUG_ERROR("[ERROR] SET_CLOCK_MODE: Invalid payload length: %d\r\n", length);
    sendError(CMD_SET_CLOCK_MODE, ERR_INVALID_PARAMETER);
    return;
  }

  const SetClockModePayload* cmd = (const SetClockModePayload*)payload;

  if (cmd->mode != PROTO_CLOCK_MANUAL && cmd->mode != PROTO_CLOCK_AUTOMATIC) {
    DEBUG_ERROR("[ERROR] SET_CLOCK_MODE: Invalid mode: %d\r\n", cmd->mode);
    sendError(CMD_SET_CLOCK_MODE, ERR_INVALID_PARAMETER);
    return;
  }

  if (onSetClockMode) {
    onSetClockMode(cmd->mode);
  }

  sendAck(CMD_SET_CLOCK_MODE, cmd->mode);
}

void Protocol::handleSetClockSpeed(const uint8_t* payload, uint8_t length) {
  if (length != sizeof(SetClockSpeedPayload)) {
    DEBUG_ERROR("[ERROR] SET_CLOCK_SPEED: Invalid payload length: %d\r\n", length);
    sendError(CMD_SET_CLOCK_SPEED, ERR_INVALID_PARAMETER);
    return;
  }

  const SetClockSpeedPayload* cmd = (const SetClockSpeedPayload*)payload;

  bool valid = false;
  if (onSetClockSpeed) {
    valid = onSetClockSpeed(cmd->index);
  }

  // Send ACK with status byte: 0x00 = success (valid index), 0x01 = error (invalid index)
  sendAck(CMD_SET_CLOCK_SPEED, valid ? 0x00 : 0x01);
}

void Protocol::handleClockPulse(const uint8_t* payload, uint8_t length) {
  uint16_t duration = 100; // default

  if (length == sizeof(ClockPulsePayload)) {
    const ClockPulsePayload* cmd = (const ClockPulsePayload*)payload;
    duration = cmd->duration;
  } else if (length != 0) {
    DEBUG_ERROR("[ERROR] CLOCK_PULSE: Invalid payload length: %d\r\n", length);
    sendError(CMD_CLOCK_PULSE, ERR_INVALID_PARAMETER);
    return;
  }

  bool executed = false;
  if (onClockPulse) {
    executed = onClockPulse(duration);
  }

  // Send ACK with status byte: 0x00 = success (executed), 0x01 = ignored (wrong mode)
  sendAck(CMD_CLOCK_PULSE, executed ? 0x00 : 0x01);
}

void Protocol::handleStartStreaming(const uint8_t* payload, uint8_t length) {
  if (length != sizeof(StartStreamingPayload)) {
    DEBUG_ERROR("[ERROR] START_STREAMING: Invalid payload length: %d\r\n", length);
    sendError(CMD_START_STREAMING, ERR_INVALID_PARAMETER);
    return;
  }

  const StartStreamingPayload* cmd = (const StartStreamingPayload*)payload;
  streaming = true;

  if (onStartStreaming) {
    onStartStreaming(cmd->flags);
  }

  sendAck(CMD_START_STREAMING);
}

void Protocol::handleStopStreaming(const uint8_t* payload, uint8_t length) {
  if (length != 0) {
    DEBUG_ERROR("[ERROR] STOP_STREAMING: Invalid payload length: %d\r\n", length);
    sendError(CMD_STOP_STREAMING, ERR_INVALID_PARAMETER);
    return;
  }

  streaming = false;

  if (onStopStreaming) {
    onStopStreaming();
  }

  sendAck(CMD_STOP_STREAMING);
}

void Protocol::handleGetStatus(const uint8_t* payload, uint8_t length) {
  if (length != 0) {
    DEBUG_ERROR("[ERROR] GET_STATUS: Invalid payload length: %d\r\n", length);
    sendError(CMD_GET_STATUS, ERR_INVALID_PARAMETER);
    return;
  }

  if (onGetStatus) {
    onGetStatus();
  }

  // Status report will be sent by callback
}

void Protocol::handleResetCounter(const uint8_t* payload, uint8_t length) {
  if (length != 0) {
    DEBUG_ERROR("[ERROR] RESET_COUNTER: Invalid payload length: %d\r\n", length);
    sendError(CMD_RESET_COUNTER, ERR_INVALID_PARAMETER);
    return;
  }

  if (onResetCounter) {
    onResetCounter();
  }

  sendAck(CMD_RESET_COUNTER);
}

void Protocol::handlePing(const uint8_t* payload, uint8_t length) {
  if (length != 0) {
    DEBUG_ERROR("[ERROR] PING: Invalid payload length: %d\r\n", length);
    sendError(CMD_PING, ERR_INVALID_PARAMETER);
    return;
  }

  if (onPing) {
    onPing();
  }

  sendPong();
}
