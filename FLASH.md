# Flash USB Microphone to XIAO nRF52840 Sense

## Build Output
- **Flash**: 59,672 bytes / 788 KB (7.4%)
- **RAM**: 43,776 bytes / 256 KB (16.7%)
- **Build artifacts**: `build/usb_mic_xiao/zephyr/zephyr.hex`, `build/usb_mic_xiao/zephyr/zephyr.uf2`

## Flashing

### Generate DFU Package
```powershell
cd m:\nrfprojects\usb_mic_xiao
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build\usb_mic_xiao\zephyr\zephyr.hex usb_mic.zip
```

### Flash to Board
```powershell
adafruit-nrfutil dfu serial --package usb_mic.zip -p COM5 -b 115200
```

## Testing

After flashing, disconnect and reconnect the board via USB. The device should appear as a USB microphone on your computer.

### Windows
1. Right-click sound icon -> Sound settings
2. Go to "Input" section
3. You should see "Zephyr USB audio sample" or similar
4. Select it and test with recording software like Audacity

### Linux
```bash
# List audio devices
arecord -l

# Record test (adjust hw:X,0 to match your device)
arecord -D hw:2,0 -f S16_LE -r 16000 -c 1 test.wav

# Play back
aplay test.wav
```

### Audacity
1. Edit -> Preferences -> Audio Settings
2. Recording Device: Select "USB Audio Device" or "XIAO USB Microphone"
3. Click record button to test

## Specifications
- **Sample Rate**: 16 kHz
- **Bit Depth**: 16-bit
- **Channels**: Mono (1 channel)
- **Interface**: USB Audio Class (UAC)
- **Microphone**: Built-in PDM microphone on XIAO nRF52840 Sense
