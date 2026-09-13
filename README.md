# IoT-Based Smart Power Grid Fault Detection System (Mini SCADA)

A low-cost, ESP32-based mini SCADA system for real-time monitoring, fault detection, and fault-section localization on a two-point distribution test line.

## Overview

Undetected faults are a persistent problem in low-voltage distribution networks, especially where a full-scale SCADA setup isn't affordable. This project implements a compact, dual-channel sensing and communication platform on a single ESP32 microcontroller that:

- Monitors voltage and current at both the **sending end (S)** and **receiving end (R)** of a distribution test line
- Detects **over-voltage**, **under-voltage**, **over-current**, **short-circuit**, and **line-failure** conditions
- Localizes faults to a section (within S–R, or upstream of S) by comparing readings from both ends
- Drives a local HMI/alarm panel (16×2 LCD, buzzer, bi-color LEDs)
- Automatically isolates the faulted section via relays
- Streams live data to a cloud backend over Wi-Fi (MQTT), with a web/mobile dashboard for real-time graphs, alerts, and remote relay control
- Keeps logging through short mains outages via a Li-ion battery backup

In lab testing, all five targeted fault categories were detected reliably with correct section-level localization.

## Authors

- M. Shoyab Sadik
- Mausofor Rahman Sohan
- Sadia Zaman Atoshi
- Israt Jahan
- Moumita Samadder

## Features

- Dual-channel (S/R) voltage, current, power, and energy monitoring
- On-board, network-independent fault classification (no cloud round-trip needed for local alarming/isolation)
- Sending-end/receiving-end comparison for fault localization
- Local 16×2 I2C LCD + LED status indicators + active buzzer
- Relay-based automatic isolation (BC547/BC557 transistor drivers, 1N4007 flyback protection)
- Wi-Fi/MQTT cloud communication with live web dashboard
- Push notifications on fault events
- Remote relay control from the dashboard
- Li-ion battery backup for outage ride-through
- Local web server (`192.168.4.1`) for on-site status access

## System Architecture

The system is built around a single **ESP32 (CP2102)** dev board handling five functional blocks:

1. **Dual-channel sensing** — sending end (S) and receiving end (R)
2. **Fault classification & localization** — on-board threshold logic
3. **Local HMI/alarm panel** — LCD, LEDs, buzzer, push-buttons
4. **Relay-based actuation** — automatic line isolation
5. **Wi-Fi communication** — cloud database + dashboard

### Fault Classification Thresholds

| Parameter | Margin |
|---|---|
| Over-voltage (δOV) | +10% of Vn |
| Under-voltage (δUV) | −15% of Vn |
| Over-current (δOC) | +20% of In |
| Short-circuit | I > 3×In **and** V < 0.5×Vn |
| Line-failure | No data from R for Ttimeout (1 s) |

### Fault Localization Logic

| Condition | Result |
|---|---|
| S normal, R abnormal | Fault within section S–R → isolate R |
| S and R both abnormal | Fault upstream of S → isolate S |
| Both normal | Normal operation |

## Hardware

### Bill of Materials

| Component | Role | Qty |
|---|---|---|
| ESP32 Dev Board (CP2102) | Central controller / Wi-Fi | 1 |
| ACS712 5A Current Sensor | Current sensing (S & R) | 2 |
| Voltage Sensor 0–25V DC | Voltage sensing (S & R) | 2 |
| 5V DC Relay Module | Line isolation / aux. output | 5 |
| BC547 NPN Transistor | Relay/LED driver | 5 |
| BC557 PNP Transistor | Relay/LED driver | 5 |
| 1N4007 Diode | Rectifier bridge / flyback | 6 |
| 16x2 LCD + I2C Module | Local readout | 1 + 1 |
| Active Buzzer | Audible fault alarm | 1 |
| 5mm LED (Red/Green) | Per-fault status pairs | 5 + 5 |
| 5mm LED (Blue) | Relay-state indicator | 5 |
| 5mm LED (White) | Power-on / backlight | 6 |
| Digital Volt/Ammeter (100V) | Analog redundant readout | 3 |
| Big Push-Button Switch | Reset / silence alarm | 2 |
| Rocker On/Off Switch | Power & fault emulation | 3 |
| 12V/1A Step-down Transformer | Test-line supply | 1 |
| Electrolytic Capacitor | Supply filtering | 2 |
| AC-DC Converter (220V→5V) | Logic power supply | 1 |
| 2-pin AC Plug / 220V Cord | Mains input | 1 / 1 |
| AC Light Holder (pin) | Mains-present lamp | 2 |
| 2S 18650 Battery Case + Cells | Battery backup | 1 + 2 |
| Resistor 1/4W (assorted) | Bias / current-limit | 20 |
| Veroboard 12x18cm + Dot/Line Bd. | Circuit prototyping | 1 + 2 |
| PVC Board Frame (5mm) | Enclosure / panel | 3 |
| M4 Nut-Bolt / Mini Screw | Mechanical assembly | 10 + 10 |

### Power Supply

- **Logic rail:** 220V AC → 5V DC converter (powers ESP32, LCD, sensors, relay coils)
- **Test-line rail:** 12V/1A step-down transformer → 1N4007 full-wave bridge → capacitor filter (the actual monitored DC bus)
- **Backup:** 2S 18650 Li-ion battery pack for short-duration ride-through during mains loss

## Software / Communication

- ESP32 publishes each channel's readings over **MQTT** to a cloud broker (every 1 s normally, immediately on a fault flag)
- Cloud-hosted real-time database feeds a **web dashboard** (JS charting library) and companion mobile view
- Dashboard shows live voltage/current/power/energy graphs, a color-coded status map, and a fault-alert log
- Remote relay control buttons mirror the physical panel push-buttons
- Local web server available at `192.168.4.1` for on-site diagnostics

## Results

### Steady-State Readings (Normal Operation)

| Channel | V (V) | I (A) | P (W) | E (Wh) |
|---|---|---|---|---|
| S (Sending) | 14.8 | 1.22 | 18.1 | 3.4 |
| R (Receiving) | 14.2 | 1.18 | 16.8 | 3.1 |

### Fault Test Results

| Fault Type | Detected | Latency (ms) | Location |
|---|---|---|---|
| Over-voltage | Yes | 170 | S–R section |
| Under-voltage | Yes | 195 | S–R section |
| Over-current | Yes | 140 | S–R section |
| Short circuit | Yes | 85 | S–R section |
| Line failure | Yes | 1010* | S–R section |

\* Line-failure latency is bounded by the communication timeout (1 s), since it's inferred from data loss rather than an out-of-range reading.

### Sensor Accuracy

- Voltage channel: max error **1.8%**
- Current channel: max error **2.6%**
- Attributed mainly to ADC quantization and ACS712 zero-current offset drift; well within the 10–20% fault thresholds, so classification was unaffected.

## Future Work

- Additional intermediate sensing points for finer-grained fault localization
- Replace Wi-Fi with LoRa for longer feeder distances
- Solar-powered supply for field deployment
- Machine-learning-based classification to distinguish transient disturbances from sustained faults
- 

Department of Electrical and Electronic Engineering, IUBAT — International University of Business Agriculture and Technology, Dhaka, Bangladesh

## References

See the full reference list in the project report (IEEE-formatted), covering power quality, distribution handbooks, ESP32/ESP8266 datasheets, IEEE protective relay guides, MQTT specification, smart grid communication standards, and the ACS712 datasheet.

## License

Add your preferred license here (e.g., MIT, Apache 2.0).
