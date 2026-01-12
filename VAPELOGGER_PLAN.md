# VapeLogger Implementation Plan
**Branch:** vapelogger
**Date:** 2026-01-12
**Base:** USB Microphone Firmware (usb_mic_xiao)

## Project Overview

### Goal
Extend the existing USB microphone firmware (usb_mic_xiao) to feed data to the python_datacap application by:
1. Maintaining existing USB audio streaming (PDM microphone)
2. Adding serial UART communication for sensor data
3. Adding ADC reading for vape coil monitoring
4. Formatting data to match python_datacap expectations

### Python DataCap Requirements Analysis

The `vape_logger.py` expects the following data streams:

**Serial Data Formats:**
- `IMU:acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z` - 6-axis IMU data (LSM6DS3)
- `COIL:adc_value` - ADC value from coil voltage/current sensing
- `DATA:acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,coil_adc` - Combined format (preferred)
- `PING/PONG` - Connection test protocol

**Audio Stream:**
- USB audio device (already working in current firmware!)
- 16 kHz sample rate (python uses 16k, firmware is 48k - needs consideration)
- 1 channel (mono)

**Data Requirements:**
- Timestamp synchronization
- 12-bit ADC values (0-4095 range)
- 3.3V ADC reference voltage
- Real-time streaming via UART at 115200 baud

### Hardware Available on XIAO nRF52840 Sense
- ✅ PDM microphone (MP34DT06JTR) - **currently working**
- ✅ 6-axis IMU (LSM6DS3TR-C) - **needs to be added**
- ✅ ADC channels (0-5, 12-bit, 3.3V reference) - **needs to be added**
- ✅ UART (TX/RX pins) - **needs to be added**
- ✅ USB interface - **currently working**

---

## Phase 1: Add Serial UART Communication

### 1.1 Configure UART in Device Tree
**File:** `app.overlay`

Add UART configuration:
```dts
&uart0 {
    compatible = "nordic,nrf-uarte";
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-1 = <&uart0_sleep>;
    pinctrl-names = "default", "sleep";
};
```

### 1.2 Enable UART in Project Config
**File:** `prj.conf`

Add:
```
# UART configuration
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_LINE_CTRL=y

# Ring buffer for UART
CONFIG_RING_BUFFER=y
```

### 1.3 Implement Serial Communication
**File:** `src/serial_comm.h` (new)

```c
#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <zephyr/kernel.h>

int serial_comm_init(void);
int serial_send_data(const char *format, ...);
int serial_send_imu_data(float acc_x, float acc_y, float acc_z,
                         float gyr_x, float gyr_y, float gyr_z);
int serial_send_coil_data(uint16_t adc_value);
int serial_send_combined_data(float acc_x, float acc_y, float acc_z,
                               float gyr_x, float gyr_y, float gyr_z,
                               uint16_t coil_adc);

#endif
```

**File:** `src/serial_comm.c` (new)

Implement:
- UART device initialization
- Ring buffer for outgoing data
- Thread-safe serial transmission
- PING/PONG handler
- Formatted data output functions

---

## Phase 2: Add IMU (LSM6DS3) Support

### 2.1 Configure I2C for IMU
**File:** `app.overlay`

Add I2C and LSM6DS3 node:
```dts
&i2c0 {
    compatible = "nordic,nrf-twim";
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;

    lsm6ds3@6a {
        compatible = "st,lsm6dsl";
        reg = <0x6a>;
        status = "okay";
        irq-gpios = <&gpio0 11 GPIO_ACTIVE_HIGH>;
    };
};
```

### 2.2 Enable I2C and Sensor in Config
**File:** `prj.conf`

Add:
```
# I2C and sensor configuration
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_LSM6DSL=y
CONFIG_LSM6DSL_TRIGGER_GLOBAL_THREAD=y
```

### 2.3 Implement IMU Reading
**File:** `src/imu_reader.h` (new)

```c
#ifndef IMU_READER_H
#define IMU_READER_H

#include <zephyr/kernel.h>

struct imu_data {
    float acc_x, acc_y, acc_z;  // in g
    float gyr_x, gyr_y, gyr_z;  // in dps
    int64_t timestamp;
};

int imu_reader_init(void);
int imu_read(struct imu_data *data);
void imu_start_sampling(void);

#endif
```

**File:** `src/imu_reader.c` (new)

Implement:
- IMU device initialization
- Data reading and conversion
- Sampling thread (100 Hz recommended)
- Data buffering

---

## Phase 3: Add ADC for Coil Monitoring

### 3.1 Configure ADC Channel
**File:** `app.overlay`

Add ADC configuration:
```dts
&adc {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    channel@0 {
        reg = <0>;
        zephyr,gain = "ADC_GAIN_1_6";
        zephyr,reference = "ADC_REF_INTERNAL";
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,input-positive = <NRF_SAADC_AIN0>; /* P0.02 / A0 */
        zephyr,resolution = <12>;
    };
};
```

### 3.2 Enable ADC in Config
**File:** `prj.conf`

Add:
```
# ADC configuration
CONFIG_ADC=y
CONFIG_ADC_NRFX_SAADC=y
```

### 3.3 Implement ADC Reading
**File:** `src/adc_reader.h` (new)

```c
#ifndef ADC_READER_H
#define ADC_READER_H

#include <zephyr/kernel.h>

struct adc_data {
    uint16_t raw_value;     // 0-4095 (12-bit)
    float voltage;          // in volts (0-3.3V)
    int64_t timestamp;
};

int adc_reader_init(void);
int adc_read_coil(struct adc_data *data);
void adc_start_sampling(void);

#endif
```

**File:** `src/adc_reader.c` (new)

Implement:
- ADC channel initialization
- 12-bit ADC reading (0-4095 range)
- Voltage conversion (3.3V reference)
- Sampling thread (100 Hz)
- Moving average filter (optional, for noise reduction)

---

## Phase 4: Integrate All Data Streams

### 4.1 Create Main Data Thread
**File:** `src/data_manager.h` (new)

```c
#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <zephyr/kernel.h>
#include "imu_reader.h"
#include "adc_reader.h"

int data_manager_init(void);
void data_manager_start(void);

#endif
```

**File:** `src/data_manager.c` (new)

Implement:
- Coordinated sampling from IMU and ADC
- Timestamp synchronization
- Combined data packet formatting
- Thread to send data via serial at regular intervals (100 Hz)
- Format: `DATA:acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,coil_adc`

### 4.2 Modify Main Application
**File:** `src/main.c`

Changes needed:
1. Include new headers
2. Initialize all subsystems in order:
   - Serial communication
   - IMU reader
   - ADC reader
   - USB Audio (existing)
   - Data manager
3. Start all threads
4. Keep USB audio thread running

Pseudocode:
```c
int main(void) {
    LOG_INF("VapeLogger starting...");

    // Initialize subsystems
    serial_comm_init();
    imu_reader_init();
    adc_reader_init();
    init_dmic();           // existing
    init_usb_audio();      // existing

    // Start data collection
    imu_start_sampling();
    adc_start_sampling();
    data_manager_start();

    // Start audio (existing)
    start_audio();

    LOG_INF("VapeLogger ready");

    // Main loop
    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
```

---

## Phase 5: Testing and Validation

### 5.1 Unit Testing
- Test serial output with minicom/putty at 115200 baud
- Verify IMU data ranges (±2g, ±245dps default)
- Test ADC readings with known voltage inputs
- Verify USB audio still works

### 5.2 Integration Testing
- Run python_datacap with simulator first
- Connect firmware and verify:
  - PING/PONG handshake works
  - IMU data is received and parsed
  - COIL ADC data is received
  - Audio device is detected by OS
  - Audio stream is captured by sounddevice

### 5.3 Performance Validation
- Check data rate (target: 100 Hz for sensor data)
- Monitor memory usage (watch for overflows)
- Verify USB audio doesn't glitch
- Check CPU utilization

---

## Phase 6: Optimization (Optional)

### Potential Improvements
1. **Audio Sample Rate Matching**
   - Python expects 16 kHz, firmware outputs 48 kHz
   - Consider adding CONFIG option to switch rates
   - Or document that python should use 48 kHz

2. **Data Rate Tuning**
   - Adjust IMU/ADC sampling rate based on battery life
   - Add configurable sampling rates via serial commands

3. **Power Management**
   - Add sleep modes when not logging
   - Battery level reporting via serial

4. **Data Buffering**
   - Add ring buffers for burst tolerance
   - Prevent data loss during USB transfers

---

## File Structure Summary

```
usb_mic_xiao/
├── src/
│   ├── main.c              (modified - main integration)
│   ├── serial_comm.h       (new - serial interface)
│   ├── serial_comm.c       (new - UART implementation)
│   ├── imu_reader.h        (new - IMU interface)
│   ├── imu_reader.c        (new - LSM6DS3 driver wrapper)
│   ├── adc_reader.h        (new - ADC interface)
│   ├── adc_reader.c        (new - ADC implementation)
│   ├── data_manager.h      (new - data coordination)
│   └── data_manager.c      (new - data formatting/sending)
├── app.overlay             (modified - add UART, I2C, ADC)
├── prj.conf                (modified - enable new subsystems)
├── CMakeLists.txt          (modified - add new source files)
└── VAPELOGGER_PLAN.md      (this file)
```

---

## Implementation Order

### Priority 1 (Core Functionality)
1. ✅ Create branch and plan (DONE)
2. Add serial UART communication (Phase 1)
3. Add IMU support (Phase 2)
4. Add ADC support (Phase 3)

### Priority 2 (Integration)
5. Integrate all data streams (Phase 4)
6. Test with python_datacap (Phase 5.2)

### Priority 3 (Polish)
7. Optimize and tune (Phase 6)
8. Document final setup

---

## Key Considerations

### Threading Architecture
- **audio_thread** (existing, priority 7) - PDM to USB audio
- **serial_tx_thread** (new, priority 8) - UART transmission
- **imu_sample_thread** (new, priority 9) - IMU sampling @ 100Hz
- **adc_sample_thread** (new, priority 9) - ADC sampling @ 100Hz
- **data_manager_thread** (new, priority 8) - Coordinate & format data

### Memory Budget
- Audio uses 8 blocks × 960 bytes = 7.7 KB (existing)
- Serial ring buffer: 2 KB
- IMU data buffer: 1 KB
- ADC data buffer: 512 bytes
- **Total new memory: ~3.5 KB** (should be OK for nRF52840)

### Data Rate Calculation
- IMU: 6 floats × 4 bytes × 100 Hz = 2.4 KB/s
- ADC: 1 uint16 × 2 bytes × 100 Hz = 200 B/s
- Serial format overhead: ~30 bytes per packet
- **Total serial: ~6 KB/s @ 115200 baud (14.4 KB/s max) ✅**

### Compatibility Notes
1. **Audio sample rate mismatch:**
   - Firmware: 48 kHz (CD quality)
   - Python: 16 kHz (Edge Impulse compatible)
   - Solution: Python should use 48k, or we add downsampling

2. **ADC pin selection:**
   - Use A0 (P0.02/AIN0) as default
   - Document which pin user should connect coil sense to

3. **IMU orientation:**
   - LSM6DS3 coordinate system may differ from python expectations
   - May need axis remapping in firmware or python

---

## Next Steps

Once plan is approved:
1. Implement Phase 1 (Serial UART)
2. Test serial output with terminal
3. Implement Phase 2 (IMU)
4. Test IMU readings
5. Implement Phase 3 (ADC)
6. Test ADC readings
7. Integrate all (Phase 4)
8. Full system test with python_datacap

---

## Questions to Resolve

1. **Which ADC pin to use for coil sensing?**
   - Suggest A0 (P0.02) - needs confirmation

2. **Desired data rate?**
   - Recommend 100 Hz for IMU/ADC - acceptable?

3. **Audio sample rate?**
   - Keep 48 kHz or change to 16 kHz to match python?

4. **UART pins?**
   - Default UART0 (TX=P0.06, RX=P0.08) or custom?

5. **Power supply for coil sensing?**
   - Need voltage divider circuit if coil voltage > 3.3V?
