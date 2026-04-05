# STM32WB05KN BLE NCP over UART

Demonstrates BLE connectivity, GPIO control and environmental sensing using the **STM32WB05KN** as a Bluetooth Low Energy Network Coprocessor (NCP), controlled by a **NUCLEO-C031C6** host MCU over UART.

---

## What It Does

The STM32WB05KN runs ST's full BLE stack internally and acts as a radio coprocessor. The host MCU sends HCI/ACI commands to it over UART, and the WB05KN handles all BLE work — advertising, connections, GATT — sending events back to the host.

On startup the device advertises as **WB05TEST**. Once a phone connects and subscribes to notifications, it receives live temperature and humidity readings every 2 seconds from an HTU21D sensor. The phone can also send commands over BLE to control an external LED. All activity is logged to PuTTY via ST-LINK VCP.

---

## Features

- BLE advertising and connectable mode
- GATT service with two characteristics (UUID A000)
  - A001 — live temperature and humidity sent to phone every 2 seconds (notifications)
  - A002 — LED control commands received from phone (write)
- HTU21D temperature and humidity sensor over I2C
- External LED control via BLE commands (case insensitive)
- Auto-restarts advertising after disconnect
- Debug logs via ST-LINK VCP at 115200 baud

---

## Hardware

- **Host MCU:** NUCLEO-C031C6
- **BLE Coprocessor:** STM32WB05KN module flashed with NCP firmware
- **Sensor:** HTU21D temperature and humidity (I2C)
- **External LED:** Connected to PA6 via resistor
- **Interface to coprocessor:** UART at 921600 baud
- **Debug logs:** ST-LINK VCP at 115200 baud (PuTTY)

### Wiring — Host to Coprocessor

| NUCLEO-C031C6 | WB05KN |
|---|---|
| PA0 (USART1 TX) | PB0 (UART RX) |
| PA1 (USART1 RX) | PA1 (UART TX) |
| 3.3V | VDD |
| GND | GND |

### Wiring — HTU21D Sensor

| NUCLEO-C031C6 | HTU21D |
|---|---|
| 3.3V | VCC |
| GND | GND |
| PB6 (I2C1 SCL) | SCL |
| PB7 (I2C1 SDA) | SDA |

The HTU21D requires external pull-up resistors on SDA and SCL if the breakout board does not already have them. Connect a 4.7k ohm resistor from 3.3V to PB6 and another from 3.3V to PB7.

### Wiring — External LED

```
PA6 ──── 330 ohm resistor ──── LED anode (+)
                                LED cathode (-) ──── GND
```

---

## WB05KN Firmware

Flash `DTM_UART_WITH_UPDATER.hex` from the X-CUBE-WB05N package (DTM Location Essentials folder) onto the WB05KN using STM32CubeProgrammer via SWD. This puts the WB05KN in NCP mode — it runs the full BLE stack and waits for HCI commands from the host over UART.

---

## CubeMX Configuration

| Peripheral | Pins | Config |
|---|---|---|
| USART1 | PA0 TX, PA1 RX | 921600 baud — WB05KN HCI |
| USART2 | PA2 TX, PA3 RX | 115200 baud — debug logs |
| I2C1 | PB6 SCL, PB7 SDA | Standard mode 100kHz |
| GPIO Output | PA6 | External LED |

Configure PA6 as GPIO Output, Push Pull, No pull, Low speed, default Low in CubeMX. Configure I2C1 in standard mode and generate code.

---

## How to Use

1. Flash the firmware
2. Open PuTTY on the ST-LINK VCP COM port at 115200 baud
3. Install nRF Connect on your phone
4. Scan for **WB05TEST** and connect
5. Open Unknown Service (UUID A000)
6. Tap the **A001 row** to open the detail view
7. Toggle the **Notify switch ON** — sensor readings begin immediately
8. Set the value display format to **UTF-8** to see plain text
9. Go to A002, tap write, select UTF-8, send LED commands

| Command | Action |
|---|---|
| ON | Turns LED on |
| OFF | Turns LED off |
| TOGGLE | Flips LED state |

Commands are case insensitive. LED state changes are not reflected on A001 — A001 is reserved for sensor data only.

---

## Expected Behaviour

PuTTY logs:
```
================================
  BLE LED + Sensor Control
================================
Initializing HTU21D...
  HTU21D ready
...
>>> CONNECTED (enhanced)!
>>> Phone SUBSCRIBED
T:24.5C H:63%
T:24.5C H:63%
>>> Received: On
  LED ON
T:24.6C H:63%
>>> Received: Toggle
  LED TOGGLED
>>> DISCONNECTED
```

Phone (nRF Connect on A001 in UTF-8 format):
```
T:24.5C H:63%
T:24.5C H:63%
T:24.6C H:63%
```

---

## References

- [X-CUBE-WB05N Software Package](https://www.st.com/en/embedded-software/x-cube-wb05n.html)
- [STM32WB05KN Product Page](https://www.st.com/en/wireless-transceivers-mcus-and-modules/stm32wb05kn.html)
- [HTU21D Datasheet](https://www.te.com/usa-en/product-CAT-HSC0004.html)
