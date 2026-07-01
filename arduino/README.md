# Arduino Novo Remote Emulator

This Arduino sketch (`novoremote.ino`) was used to validate the Novo D33100 protocol before implementing the ESPHome version.

## Features

- Emulates all 3 Novo D33100 remotes (Format A protocol)
- Serial command interface for controlling blinds
- Receive mode for capturing and decoding Novo RF frames
- Rolling code implementation with per-remote counter tracking

## Hardware

- Arduino Nano (or compatible)
- CC1101 RF Transceiver Module (868 MHz)

## Wiring

| Arduino Pin | CC1101 Pin |
|-------------|------------|
| 3V3         | VCC        |
| GND         | GND        |
| D10         | CSN        |
| D13         | SCK        |
| D11         | MOSI       |
| D12         | MISO       |
| D2          | GDO0       |
| D3          | GDO2       |

## Serial Commands (115200 baud)

- `r<1|2|3>` - Select remote (e.g., `r2`)
- `c<1-6>` - Select channel (e.g., `c1`)
- `u` - Send Open command
- `s` - Send Stop command
- `d` - Send Close command
- `p` - Send Pair command
- `x` - Toggle receive mode (capture/decode frames)
