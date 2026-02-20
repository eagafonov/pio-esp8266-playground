#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// Debug serial port configuration for binary protocol debugging
//
// When DEBUG_PROTOCOL is defined:
//   - Serial1 (GPIO2/TX) is used for debug output at 115200 baud
//   - Built-in LED on GPIO2 will NOT work in debug builds
//   - Only errors and major events are logged (minimal verbosity)
//   - User manually controls line endings with \r\n in format strings
//
// When DEBUG_PROTOCOL is undefined (production):
//   - All debug macros compile to nothing (zero overhead)
//   - GPIO2 available for LED or other purposes
//   - No debug output on Serial1
//
// Debug output format:
//   [DEBUG] message      - Major events (init, mode changes, state transitions)
//   [ERROR] message      - Errors (checksum fail, invalid commands, rejections)
//   [TRACE] message      - Verbose details (only with DEBUG_VERBOSE)
//
// Usage examples:
//   DEBUG_EVENT("Protocol initialized\r\n");
//   DEBUG_EVENT("Clock mode: %s\r\n", mode == AUTO ? "AUTO" : "MANUAL");
//   DEBUG_ERROR("Checksum failed: expected 0x%02X, got 0x%02X\r\n", exp, got);
//
// Build environments:
//   pio run -e logic-analyzer           (production, no debug)
//   pio run -e logic-analyzer-debug     (debug enabled, Serial1 output)

#ifdef DEBUG_PROTOCOL

  // Debug serial port (UART1, TX-only on GPIO2)
  #define DEBUG_SERIAL Serial1

  // Initialize debug serial (call in setup())
  #define DEBUG_BEGIN(baud) Serial1.begin(baud)

  // Major events (initialization, mode changes, state transitions)
  // User adds \r\n explicitly in format string
  #define DEBUG_EVENT(...) Serial1.printf("[DEBUG] " __VA_ARGS__)

  // Errors (checksum failures, invalid commands, rejections)
  // User adds \r\n explicitly in format string
  #define DEBUG_ERROR(...) Serial1.printf("[ERROR] " __VA_ARGS__)

  // Verbose logging (optional, enabled with DEBUG_VERBOSE_ENABLED flag)
  // For packet dumps, state transitions, detailed tracing
  #ifdef DEBUG_VERBOSE_ENABLED
    #define DEBUG_VERBOSE(...) Serial1.printf("[TRACE] " __VA_ARGS__)
  #else
    #define DEBUG_VERBOSE(...) do {} while(0)
  #endif

  #define DEBUG_ENABLED 1

#else

  // Production build - all macros compile to nothing (zero overhead)
  #define DEBUG_BEGIN(baud) do {} while(0)
  #define DEBUG_EVENT(...) do {} while(0)
  #define DEBUG_ERROR(...) do {} while(0)
  
  #ifndef DEBUG_VERBOSE
    #define DEBUG_VERBOSE(...) do {} while(0)
  #endif

  #undef DEBUG_ENABLED

#endif // DEBUG_PROTOCOL

#endif // DEBUG_H
