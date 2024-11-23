# ESP32 Smart Shade Controller

A smart device controller for motorized window shades/blinds with WiFi connectivity, MQTT control, and OTA update capabilities.

## Features

- Stepper motor control for precise shade positioning
- WiFi connectivity with SmartConfig support
- MQTT integration for remote control and status updates
- Telegram notifications for status changes
- Over-the-Air (OTA) firmware updates
- LED status indication
- Position calibration
- Non-volatile storage for settings
- Physical initialization button

## Hardware Requirements

- ESP32 microcontroller
- Stepper motor
- LED status indicator
- Initialization button
- Power supply

## Pin Configuration

- SM_DIR: Stepper motor direction pin
- SM_STEP: Stepper motor step pin
- SM_nEN: Stepper motor enable pin
- LED_STATUS: Status LED pin

## Operating Modes

### Normal Operation
- Controls shade position (0-100%)
- Maintains current position in NVS memory
- Provides real-time status updates via MQTT and Telegram

### Calibration Mode
- Allows for system calibration
- Counts total steps for full range of motion
- Updates system parameters automatically

### LED Status Indicators

- Short single blink: SmartConfig initialization
- Fast double blink: SmartConfig device found
- Long blink: WiFi connection in progress
- Double short blink: OTA update starting
- Fast double blink: OTA server connected
- Solid LED: System reinitializing or OTA update complete

## MQTT Integration

The device publishes status updates to configured MQTT topics including:
- Current position
- Target position
- Movement status
- Device hostname

## Telegram Integration

Sends notifications for:
- Position changes
- Operating status updates
- System events

## OTA Updates

Supports secure HTTPS OTA updates with:
- Image validation
- Update progress monitoring
- Automatic restart after successful update
- Update timestamp storage
- Update URL storage in NVS

## Building and Flashing

[]

## Configuration

[Add configuration instructions including:
- WiFi setup
- MQTT broker settings
- Telegram bot setup]

## Dependencies

- ESP-IDF
- MQTT client library
- Telegram bot library
- ESP32 HTTPS OTA library

## License

[]
