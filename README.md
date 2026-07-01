# Novo Remote Bridge

An ESPHome-based RF bridge that emulates **Novo D33100** remote controls for controlling motorized blinds and skylights. This project allows you to integrate RF-controlled blinds (Novo, Maxxmarr, Leroy Merlin, Kurlax, etc.) into Home Assistant.

![front](pics/front.jpg)

## Features

Remote control:

- Emulate up to 3 different remote controls
- Control up to 6 independent blind channels
- Open, Stop, Close, and Pair operations

Bridge:

- Built-in web server for standalone control
- Home Assistant and Native ESPHome/API support
- Over-the-air firmware updates

## Hardware Requirements

1. ESP32-C6 - Main controller
2. CC1101 RF Transceiver Module - 868 MHz radio

**Please note: most CC1101 modules are designed to broadcast at 433MHz, you'll need a module that a RF circuit for 868MHz**.
This board worked well for me: [Sub‑GHz 855–925 MHz Bridge – ESP32‑C6 WiFi/Zigbee](https://www.tindie.com/products/elecram/subghz-855925-mhz-bridge-esp32c6-wifizigbee/)

The code assumes the following pinmapping:

| ESP32-C6 Pin | CC1101 Pin | Function          |
|--------------|------------|-------------------|
| GPIO6        | SCK        | SPI Clock         |
| GPIO7        | MOSI       | SPI Data Out      |
| GPIO2        | MISO       | SPI Data In       |
| GPIO18       | CSN        | SPI Chip Select   |
| GPIO15       | GDO0       | Radio Interrupt 0 |
| GPIO14       | GDO2       | Radio Interrupt 2 |
| GND          | GND        | Ground            |
| 3.3V         | VCC        | Power (3.3V)      |

## Installation

### Prerequisites

1. ESP32 toolchain configured
2. ESPHome installed, see [installation guide](https://esphome.io/guides/getting_started_command_line.html)
3. Optional: Home Assistant

### Setup

1. Clone this repository:

   ```bash
   git clone https://github.com/erikbos/esphome-novo.git
   cd esphome-novo
   ```

2. Create `secrets.yaml` and fill in your wifi details:

   ```yaml
   wifi_ssid: "YourWiFiSSID"
   wifi_password: "YourWiFiPassword"
   ```

3. Compile and flash:

   ```bash
   esphome run novo-bridge.yaml
   ```

   First-time flashing requires USB connection. Subsequent updates can use OTA.

4. Web-based control:

    Access the device's web interface at [http://novo-bridge.local](http://novo-bridge.local) (or its IP address) to control the blinds without Home Assistant.

5. Add to Home Assistant:

   The device should automatically appear in Home Assistant's ESPHome integration after it connects to your network.

## Configuration

### Selecting Remote ID

The bridge can emulate 3 different remote controls. Each has a unique device ID:

- **Remote 1** - Device ID: `0xacc5c4`
- **Remote 2** - Device ID: `0x6a1a58`
- **Remote 3** - Device ID: `0x98de74`

Use the **Novo Remote** dropdown in Home Assistant to select which remote to emulate.

### Selecting Channel

Control individual blind channels (1-6) or all channels at once:

- Channels 1-6: Individual blind control
- All Channels: Broadcast to all paired blinds

Use the **Novo Channel** dropdown to select the target channel.

### Pairing with Your Blind

Before controlling a blind, you must pair it with the bridge:

1. Set the bridge to your desired **Remote** and **Channel**
2. Press **P1** in battery compartment of a remote
3. Press the **Pair** button in Home Assistant

The blind will now respond to commands from this remote/channel combination.

## Protocol Analysis

This project includes a complete reverse-engineering analysis of the Novo D33100 protocol. See [frame analysis.md](frame%20analysis.md) for:

- Complete frame format specifications (Format A, B, C)
- Rolling code algorithm
- Checksum calculation
- Timing analysis
- Python reference implementation

The [arduino/](Arduino folder) contains a sketch that I used to validate the protocol before doing the ESPHome.

## Contributing

Contributions are welcome! Areas of interest:

- Support for additional Novo protocol details (Format B, C). Please include RF captures in your PR :)
- Additional ESP32 board support

## References

- [Novo (Guangzhou) official site](https://en.gznovo.com)
- [D33100 Product Page](https://en.gznovo.com/transmitter/135.html)
- [D33100 Manual (PDF)](https://en.gznovo.com/uploads/upload/files/20250917/e01c1021ace78fe78c008c64c53a26ed.pdf)
- [ESPHome Documentation](https://esphome.io)

## Disclaimer

I started this project as a bridge was not available. This is a reverse-engineered implementation. Use at your own risk.
