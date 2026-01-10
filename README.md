# USB Microphone for XIAO nRF52840 Sense

USB Audio Class device implementation using the built-in PDM microphone on the Seeed XIAO nRF52840 Sense board. This project turns the board into a USB microphone that works with audio recording applications like Audacity.

## Hardware Requirements

- **Seeed XIAO nRF52840 Sense** board with built-in PDM microphone
- USB-C cable for connection and power

## Software Requirements

- **nRF Connect SDK v3.2.1** or later
- **Python 3.10+** for build tools
- **adafruit-nrfutil** for flashing firmware

## Features

- 16 kHz sample rate, 16-bit mono audio
- USB Audio Class device (UAC) compatible
- Works with Windows, Linux, and macOS
- Low latency audio capture
- Uses Zephyr RTOS

## Setup Instructions

### 1. Install nRF Connect SDK

1. Download and install [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop)
2. Open nRF Connect for Desktop and install the **Toolchain Manager**
3. In Toolchain Manager, install **nRF Connect SDK v3.2.1** (or later)
   - This will install all necessary tools including:
     - Zephyr SDK
     - ARM GNU Toolchain
     - CMake, Ninja, Python, etc.
4. Note the installation path (typically `C:\ncs\v3.2.1` on Windows)

### 2. Install Flashing Tools

Install adafruit-nrfutil for DFU flashing:

```bash
pip install adafruit-nrfutil
```

### 3. Set Up Environment

Open a terminal and set up the nRF Connect SDK environment:

**Windows (PowerShell):**
```powershell
cd C:\ncs\v3.2.1
.\zephyr\zephyr-env.ps1
```

**Linux/macOS:**
```bash
cd ~/ncs/v3.2.1
source zephyr/zephyr-env.sh
```

### 4. Clone This Repository

```bash
cd /path/to/your/projects
git clone https://github.com/Toastee0/Xiao_Nrf52840_sense_USB_MIC.git
cd Xiao_Nrf52840_sense_USB_MIC
```

### 5. Build the Firmware

```bash
west build -b xiao_ble/nrf52840/sense
```

The build output will be in the `build/` directory.

### 6. Flash to the Board

#### Method 1: DFU Bootloader (Recommended)

1. Double-tap the **RESET** button on the XIAO board to enter bootloader mode
   - The board should appear as a COM port (e.g., COM5 on Windows)
   - You may see a pulsing orange LED

2. Create the DFU package:
   ```bash
   adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/usb_mic_xiao/zephyr/zephyr.hex build/zephyr.zip
   ```

3. Flash the firmware (replace COM5 with your port):
   ```bash
   adafruit-nrfutil dfu serial -pkg build/zephyr.zip -p COM5 -b 115200
   ```

#### Method 2: UF2 Bootloader

If your XIAO has the UF2 bootloader:

1. Double-tap RESET - the board will appear as a USB drive
2. Copy `build/usb_mic_xiao/zephyr/zephyr.uf2` to the drive

### 7. Connect and Use

1. After flashing, the board will reboot and appear as a **USB Audio Device**
2. Open your audio recording software (Audacity, Audition, etc.)
3. Select the XIAO USB Microphone as your input device
4. Start recording!

## Monitoring Serial Output

To view debug logs, connect to the serial port:

**Windows:**
```powershell
# Find available ports
[System.IO.Ports.SerialPort]::getportnames()

# Connect with a terminal program (e.g., PuTTY, Tera Term)
# Baud rate: 115200
```

**Linux:**
```bash
screen /dev/ttyACM0 115200
# or
minicom -D /dev/ttyACM0 -b 115200
```

You should see:
```
USB Microphone starting...
DMIC configured: 16000 Hz, 16-bit
USB Audio initialized
Audio streaming started
USB Microphone ready - waiting for host to open stream
USB status changed: 6
USB configured - will start streaming in 2 seconds
Read 320 bytes from DMIC, sending to USB
```

## Troubleshooting

### Device Not Recognized

- Make sure you're using a **data-capable USB cable** (not charge-only)
- Try a different USB port
- Check Device Manager (Windows) or `lsusb` (Linux) to verify the device appears

### Build Errors

- Verify nRF Connect SDK is properly installed
- Make sure environment variables are set (run `zephyr-env` script)
- Clean build: `west build -b xiao_ble/nrf52840/sense --pristine`

### No Audio in Recording Application

- Check that the device is selected as the audio input
- Verify the device appears in Windows Sound settings or `arecord -l` (Linux)
- Monitor serial output to confirm audio is being read from the microphone
- Try unplugging and reconnecting the device

### Flashing Fails

- Ensure board is in bootloader mode (double-tap RESET)
- Check that the correct COM port is specified
- On Linux, you may need to add your user to the `dialout` group:
  ```bash
  sudo usermod -a -G dialout $USER
  ```

## Project Structure

```
.
├── CMakeLists.txt              # Main build configuration
├── prj.conf                    # Kconfig options
├── boards/                     # Board-specific configuration
│   ├── xiao_ble_nrf52840_sense.overlay  # Device tree overlay
│   └── xiao_ble-pinctrl.dtsi   # Pin control definitions
└── src/
    └── main.c                  # Application code
```

## Technical Details

### Audio Configuration

- **Sample Rate:** 16 kHz
- **Bit Depth:** 16-bit
- **Channels:** Mono (left channel from PDM mic)
- **Block Size:** 320 bytes (10ms of audio)
- **PDM Clock:** 1-3.5 MHz
- **PDM Pins:** CLK=P1.00, DIN=P0.16

### USB Configuration

- **USB VID:** 0x2886 (Seeed)
- **USB PID:** 0x0009
- **Device Class:** USB Audio Class (UAC)
- **Interface:** Microphone input only

## Known Limitations

- Uses legacy (deprecated) Zephyr USB device stack due to USB Audio Class requirements
- Sample rate fixed at 16 kHz (configurable in source if needed)
- Mono only (single PDM microphone)

## License

MIT License - feel free to use and modify for your projects.

## Credits

- Based on Zephyr RTOS USB Audio sample
- Uses Seeed XIAO nRF52840 Sense hardware
- nRF Connect SDK by Nordic Semiconductor

## Contributing

Pull requests welcome! For major changes, please open an issue first to discuss what you would like to change.
