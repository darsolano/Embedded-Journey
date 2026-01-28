# Embedded Journey

Code samples and reusable modules for the **Embedded Journey** projects. The repository is organized as a set of standalone components (networking, time, sensors, etc.) that can be pulled into embedded firmware projects as needed. Around STM32 B-L475E-IOT01A1 board.

## Repository layout

| Module | Purpose | Key paths |
| --- | --- | --- |
| `es_wifi/` | Drivers and wrappers for Inventek ISM43362/3 Wi‑Fi modules, including AT command parsing and socket/TLS helpers. | `es_wifi/Inc/`, `es_wifi/Src/` |
| `netsock/` | Lightweight socket abstraction utilities that sit on top of Wi‑Fi/transport stacks. | `netsock/inc/`, `netsock/src/` |
| `mqtt/` | MQTT client, packet, task, and example app components. | `mqtt/mqtt_client/`, `mqtt/mqtt_packet/`, `mqtt/mqtt_tasks/`, `mqtt/mqtt_apps/` |
| `Http/` | HTTP client/server helpers. | `Http/inc/`, `Http/src/` |
| `restAPI/` | REST API helpers and convenience wrappers. | `restAPI/rest_api.c`, `restAPI/rest_api.h` |
| `websocket/` | WebSocket client/server implementation, including crypto helpers and a test harness. | `websocket/ws_*.{c,h}`, `websocket/test_ws.c` |
| `ntp/` | NTP time synchronization utilities. | `ntp/inc/`, `ntp/src/` |
| `Time/` | Time/date utilities. | `Time/inc/`, `Time/src/` |
| `Sensors/` | Example sensor data structures and helpers. | `Sensors/sensors_data.c`, `Sensors/sensors_data.h` |
| `json/` | JSON parsing and serialization helpers. | `json/inc/`, `json/src/` |
| `common/` | Shared logging and message definitions used across modules. | `common/log.h`, `common/msg.h` |
| `mbedtls/` | Mbed TLS source and configuration assets. | `mbedtls/src/`, `mbedtls/mbedtls/`, `mbedtls/readme` |

## How to use

1. Pick the module(s) you need for your firmware build (for example, `es_wifi/` + `mqtt/`).
2. Add the corresponding `inc/` (or `Inc/`) directory to your compiler include paths.
3. Add the matching `src/` (or `Src/`) files to your build system.
4. Wire the module into your platform by providing any required low-level drivers (SPI, timers, etc.).

> **Note**: The modules are designed to be portable; integrate them into your project’s build system (Make/CMake/IDE) and adapt the low-level I/O hooks as needed for your board.

## Getting help

If you are following along with the Embedded Journey YouTube channel, reference the video or series that matches the module you are integrating for platform-specific guidance and configuration tips.
