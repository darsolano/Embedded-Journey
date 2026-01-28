# mbedTLS
 
mbedTLS is the lightweight TLS/SSL and cryptography library used in this project to
secure network communications on resource-constrained devices. It provides the
core security building blocks—TLS handshakes, certificate validation, encryption,
message integrity, and key management—that allow the rest of the stack to create
trusted, authenticated, and encrypted connections.

## Role in secured connected projects

mbedTLS enables secure transport for common IoT and embedded protocols, including:

- **MQTTS (MQTT over TLS)**
  - Establishes a TLS session between the device and the broker.
  - Verifies broker certificates (or uses mutual TLS with client certificates).
  - Protects MQTT payloads from eavesdropping and tampering.

- **HTTPS**
  - Wraps HTTP traffic in TLS to provide confidentiality and integrity.
  - Ensures the device is communicating with the legitimate server through
    certificate validation.

- **Secure WebSocket (wss://)**
  - Uses the same TLS primitives as HTTPS to secure WebSocket upgrades and
    bidirectional messaging.

- **REST API clients**
  - Secures REST requests/responses over HTTPS.
  - Enables authentication patterns such as mutual TLS when required by the API.

## Why it matters for embedded systems

- **Footprint-friendly**: optimized for limited RAM/flash budgets.
- **Configurable**: compile-time options let you enable only the required ciphers
  and features.
- **Portable**: runs across a wide range of MCU/RTOS environments.

In short, mbedTLS is the security foundation that allows the networking examples
in this repository to communicate safely with cloud services and brokers.

