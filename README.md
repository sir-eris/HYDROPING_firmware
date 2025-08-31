# HydroPing iOS App (_PRIVATE PROJECT_)

> _NOTE: This repo is not meant for public use and is only made available publicly for job acquisition purposes only._

**Project Overview**
---
- LOC: 355
- Team: 1
- Timeline: 2mo
- Language: Swift
- Main Packages: I2C Wire, Sync Webserver, Json

<sup>*</sup>Count excludes, .gitignore and other irrelevant files and directories.

**What's HydroPing**
---
The HydroPing system is an end-to-end solution that integrates custom embedded hardware, mobile applications, and a cloud backend to deliver reliable soil moisture monitoring and analytics. At its core, the STM32 microcontroller runs optimized firmware that manages power-efficient sensor sampling, data preprocessing, and wireless communication. The iOS app pairs with the device by activating Access Point (AP) Mode, providing users with real-time readings, local persistence, and a responsive dashboard for visualization. On the backend, a serverless architecture built with AWS Lambda and API Gateway securely ingests sensor data, performs time-series analysis, and exposes RESTful endpoints for app synchronization. The system design emphasizes modularity: the firmware ensures low-power continuous operation, the app handles user experience and offline caching, and the backend manages scalability and cross-device access. Together, these layers form a tightly integrated pipeline from hardware sensing to cloud intelligence, giving users actionable insights through a seamless and efficient interface.


**What's HydroPing APP**
---
The Hydroping firmware is built for the ESP32 microcontroller and written in C/C++ using the ESP-IDF framework. It acts as the system’s control layer, handling sensor acquisition, power management, and communication with the backend through a lightweight HTTP-based API.

Key features of the firmware include:
- Sensor Handling: The ESP32 reads soil moisture and related environmental data through its ADCs and digital interfaces. Basic filtering and validation routines are applied to ensure stable readings before transmission.
- Wi-Fi Setup & Modes: During initial configuration, the ESP32 operates in AP mode, broadcasting its own Wi-Fi hotspot so the iOS app can provision network credentials. Once configured, it switches to STA mode and remains connected to the home router for backend communication.
- HTTP Data Exchange: Instead of using heavier protocols, the firmware communicates directly with the backend via RESTful HTTP requests. Each measurement cycle posts sensor values, battery status, and device metadata. If the network is unavailable, the firmware retries transmission with backoff until successful, ensuring reliable delivery.
- Power Efficiency: To conserve energy, the ESP32 spends most of its time in deep sleep, waking periodically to capture sensor data and send it to the server. Voltage monitoring is also performed to track battery health and optimize replacement or recharge cycles.
- System Reliability: A watchdog timer and structured error handling protect against lockups or corrupted readings. The firmware is designed for unattended operation, making the device resilient in real-world environments. By combining accurate sensing, efficient power use, and simple yet robust HTTP communication, the ESP32 firmware forms the core intelligence of Hydroping, seamlessly linking hardware, mobile app, and backend services.


**Purchase the App**
---
The probes are currently available to [pre-order](https://hydroping.com)
