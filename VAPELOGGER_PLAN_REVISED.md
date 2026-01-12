# VapeLogger Implementation Plan (REVISED)
**Branch:** vapelogger
**Date:** 2026-01-12
**Base:** USB Microphone Firmware (usb_mic_xiao)

---

## Existing Firmware Analysis

### ✅ Already Working
1. **USB Audio Device** - 48 kHz, 16-bit mono PDM microphone streaming
2. **Serial Console** - UART0 enabled via `CONFIG_STDOUT_CONSOLE=y` at 115200 baud (TX=P1.11, RX=P1.12)
3. **Pin Definitions** - Complete `xiao_ble-pinctrl.dtsi` with all peripherals defined
4. **Logging System** - Zephyr `LOG_INF()` outputs to serial console
5. **Audio Thread** - Dedicated thread for PDM→USB audio pipeline
6. **USB Callbacks** - Connection status and data request handlers

### 📋 Available Hardware Resources (from pinctrl.dtsi)
- **UART0:** TX=P1.11, RX=P1.12 ✅ (console active)
- **I2C0:** SDA=P0.07, SCL=P0.27 (for IMU)
- **I2C1:** SDA=P0.04, SCL=P0.05 (alternative bus)
- **ADC:** Pin A0 (user confirmed)
- **PDM:** CLK=P1.00, DIN=P0.16 ✅ (mic active)

### 🔧 What Needs to Be Added
1. ~~Serial UART setup~~ - ✅ **Already working** (just need data formatting)
2. **IMU (LSM6DS3)** via I2C0
3. **ADC** on pin A0 for coil monitoring
4. **Data formatting** to match python_datacap protocol
5. **PING/PONG** handler for connection testing

---

## Python DataCap Protocol Requirements

### Serial Data Formats Expected
```
PING          → firmware responds with PONG
IMU:ax,ay,az,gx,gy,gz
COIL:adc_value
DATA:ax,ay,az,gx,gy,gz,adc   ← PREFERRED FORMAT
```

### Data Specifications
- **Sample Rate:** ~100 Hz for IMU/ADC
- **ADC:** 12-bit (0-4095), 3.3V reference
- **UART:** 115200 baud ✅ (already set)
- **Audio:** USB device ✅ (python uses sounddevice to capture)
- **Timestamp:** Handled by python (tracks relative time from start)

---

## Implementation Phases

## Phase 1: Add I2C and IMU (LSM6DS3)

### 1.1 Enable I2C in Device Tree
**File:** `boards/xiao_ble_nrf52840_sense.overlay`

Add after the PDM section:
```dts
/* Enable I2C0 for IMU */
&i2c0 {
    compatible = "nordic,nrf-twim";
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";

    lsm6ds3: lsm6ds3@6a {
        compatible = "st,lsm6dsl";
        reg = <0x6a>;
        status = "okay";
        /* INT1 pin on P0.11 (optional) */
    };
};
```

### 1.2 Enable I2C and Sensor in Config
**File:** `prj.conf`

Add:
```conf
# I2C and IMU sensor
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_LSM6DSL=y
```

### 1.3 Add IMU Reading Code
**File:** `src/main.c`

Add near top with other includes:
```c
#include <zephyr/drivers/sensor.h>
```

Add global variables after existing ones:
```c
static const struct device *imu_dev;
static struct sensor_value accel[3], gyro[3];
```

Add IMU init function before `main()`:
```c
static int init_imu(void)
{
    imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3));
    if (!device_is_ready(imu_dev)) {
        LOG_ERR("IMU device not ready");
        return -ENODEV;
    }
    LOG_INF("IMU initialized");
    return 0;
}
```

Add IMU read function:
```c
static int read_imu(float *acc_x, float *acc_y, float *acc_z,
                    float *gyr_x, float *gyr_y, float *gyr_z)
{
    int ret;

    ret = sensor_sample_fetch(imu_dev);
    if (ret < 0) {
        return ret;
    }

    sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro);

    *acc_x = sensor_value_to_double(&accel[0]);
    *acc_y = sensor_value_to_double(&accel[1]);
    *acc_z = sensor_value_to_double(&accel[2]);

    *gyr_x = sensor_value_to_double(&gyro[0]);
    *gyr_y = sensor_value_to_double(&gyro[1]);
    *gyr_z = sensor_value_to_double(&gyro[2]);

    return 0;
}
```

---

## Phase 2: Add ADC for Coil Monitoring

### 2.1 Configure ADC in Device Tree
**File:** `boards/xiao_ble_nrf52840_sense.overlay`

Add after I2C section:
```dts
/* Enable ADC for coil monitoring on A0 */
&adc {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;

    channel@0 {
        reg = <0>;
        zephyr,gain = "ADC_GAIN_1_6";
        zephyr,reference = "ADC_REF_INTERNAL";
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,input-positive = <NRF_SAADC_AIN0>; /* A0 pin */
        zephyr,resolution = <12>;
    };
};
```

### 2.2 Enable ADC in Config
**File:** `prj.conf`

Add:
```conf
# ADC for coil monitoring
CONFIG_ADC=y
CONFIG_ADC_NRFX_SAADC=y
```

### 2.3 Add ADC Reading Code
**File:** `src/main.c`

Add include:
```c
#include <zephyr/drivers/adc.h>
```

Add global variables:
```c
static const struct device *adc_dev;
static struct adc_channel_cfg adc_cfg = {
    .gain = ADC_GAIN_1_6,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
    .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0, /* A0 */
};
static int16_t adc_sample_buffer;
static struct adc_sequence adc_seq = {
    .channels = BIT(0),
    .buffer = &adc_sample_buffer,
    .buffer_size = sizeof(adc_sample_buffer),
    .resolution = 12,
};
```

Add ADC init function:
```c
static int init_adc(void)
{
    int ret;

    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    ret = adc_channel_setup(adc_dev, &adc_cfg);
    if (ret < 0) {
        LOG_ERR("ADC channel setup failed: %d", ret);
        return ret;
    }

    LOG_INF("ADC initialized on A0");
    return 0;
}
```

Add ADC read function:
```c
static int read_adc(uint16_t *adc_value)
{
    int ret;

    ret = adc_read(adc_dev, &adc_seq);
    if (ret < 0) {
        return ret;
    }

    /* Convert to 0-4095 range */
    *adc_value = (uint16_t)adc_sample_buffer;
    return 0;
}
```

---

## Phase 3: Add Data Output Thread

### 3.1 Add PING/PONG Handler
**File:** `src/main.c`

Add after includes:
```c
#include <zephyr/sys/printk.h>
#include <zephyr/console/console.h>
```

Add global for command handling:
```c
static bool data_streaming = false;
```

Add console input thread:
```c
static void console_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    char line[32];

    console_getline_init();

    while (1) {
        printk("\n"); /* Ensure we're on new line */
        char *s = console_getline();

        if (s && strlen(s) > 0) {
            if (strcmp(s, "PING") == 0) {
                printk("PONG\n");
            } else if (strcmp(s, "START") == 0) {
                data_streaming = true;
                printk("OK\n");
            } else if (strcmp(s, "STOP") == 0) {
                data_streaming = false;
                printk("OK\n");
            }
        }

        k_sleep(K_MSEC(10));
    }
}

K_THREAD_DEFINE(console_tid, 1024, console_thread, NULL, NULL, NULL, 8, 0, 0);
```

### 3.2 Add Data Streaming Thread
**File:** `src/main.c`

Add data thread:
```c
static void data_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    float acc_x, acc_y, acc_z;
    float gyr_x, gyr_y, gyr_z;
    uint16_t coil_adc;
    int ret;

    /* Wait for devices to be ready */
    k_sleep(K_SECONDS(2));

    while (1) {
        if (data_streaming) {
            /* Read IMU */
            ret = read_imu(&acc_x, &acc_y, &acc_z, &gyr_x, &gyr_y, &gyr_z);
            if (ret < 0) {
                LOG_ERR("IMU read failed: %d", ret);
            }

            /* Read ADC */
            ret = read_adc(&coil_adc);
            if (ret < 0) {
                LOG_ERR("ADC read failed: %d", ret);
            }

            /* Send combined data packet */
            printk("DATA:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u\n",
                   acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z, coil_adc);
        }

        /* Sample at 100 Hz */
        k_sleep(K_MSEC(10));
    }
}

K_THREAD_DEFINE(data_tid, 2048, data_thread, NULL, NULL, NULL, 8, 0, 0);
```

---

## Phase 4: Update Main Function

### 4.1 Modify main() Function
**File:** `src/main.c`

Update the `main()` function:
```c
int main(void)
{
    int ret;

    LOG_INF("VapeLogger starting...");

    /* Initialize console for PING/PONG */
    console_init();

    /* Initialize IMU */
    ret = init_imu();
    if (ret < 0) {
        LOG_ERR("IMU init failed: %d", ret);
        /* Continue anyway - optional sensor */
    }

    /* Initialize ADC */
    ret = init_adc();
    if (ret < 0) {
        LOG_ERR("ADC init failed: %d", ret);
        /* Continue anyway - optional sensor */
    }

    /* Initialize audio (existing) */
    ret = init_dmic();
    if (ret < 0) {
        LOG_ERR("DMIC init failed: %d", ret);
        return ret;
    }

    ret = init_usb_audio();
    if (ret < 0) {
        LOG_ERR("USB Audio init failed: %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(1000));

    ret = start_audio();
    if (ret < 0) {
        LOG_ERR("Audio start failed: %d", ret);
        return ret;
    }

    LOG_INF("VapeLogger ready - send PING to test, START to stream data");

    while (1) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
```

---

## Phase 5: Update Project Config

### 5.1 Final prj.conf
**File:** `prj.conf`

Complete configuration:
```conf
CONFIG_STDOUT_CONSOLE=y

# Console for PING/PONG
CONFIG_CONSOLE=y
CONFIG_CONSOLE_GETLINE=y

#USB related configs
CONFIG_USB_DEVICE_STACK=y
CONFIG_DEPRECATION_TEST=y
CONFIG_USB_DEVICE_PRODUCT="XIAO VapeLogger"
CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=n
CONFIG_USB_DEVICE_STACK_NEXT=n

#LOG subsystem related configs
CONFIG_LOG=y
CONFIG_USB_DRIVER_LOG_LEVEL_ERR=y
CONFIG_USB_DEVICE_LOG_LEVEL_ERR=y

#net buf options
CONFIG_NET_BUF=y

#USB audio related configs
CONFIG_USB_DEVICE_AUDIO=y

#Audio and DMIC configs
CONFIG_AUDIO=y
CONFIG_AUDIO_DMIC=y

# I2C and IMU sensor
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_LSM6DSL=y

# ADC for coil monitoring
CONFIG_ADC=y
CONFIG_ADC_NRFX_SAADC=y

# Printf formatting
CONFIG_CBPRINTF_FP_SUPPORT=y
```

---

## Testing Plan

### Step 1: Build and Flash
```bash
west build -b xiao_ble/nrf52840/sense --pristine
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/zephyr/zephyr.hex build/vapelogger.zip
adafruit-nrfutil dfu serial -pkg build/vapelogger.zip -p COM5 -b 115200
```

### Step 2: Test Serial Connection
Connect via serial terminal (115200 baud):
```
> PING
< PONG
> START
< OK
< DATA:0.02,-0.01,9.81,0.5,-0.3,0.1,0
< DATA:0.03,-0.02,9.80,0.4,-0.2,0.2,5
```

### Step 3: Test with Python
Run `python_datacap/vape_logger_gui.py`:
- Device should auto-detect
- PING/PONG should work
- IMU and COIL data should display
- Audio device should appear in Windows

---

## File Changes Summary

### Files to Modify
1. `boards/xiao_ble_nrf52840_sense.overlay` - Add I2C, ADC config
2. `prj.conf` - Enable I2C, sensor, ADC, console
3. `src/main.c` - Add IMU, ADC, data streaming, PING/PONG

### No New Files Needed
All changes go into existing files!

---

## Data Flow Architecture

```
┌─────────────────────────────────────────┐
│  XIAO nRF52840 Sense (VapeLogger)       │
├─────────────────────────────────────────┤
│                                         │
│  [PDM Mic] ──→ audio_thread ──→ USB    │
│                                    ↓    │
│  [LSM6DS3] ──→ data_thread ───→ UART   │
│  [ADC A0]  ──→                         │
│                                         │
│  UART ←──── console_thread (PING/PONG) │
│                                         │
└─────────────────────────────────────────┘
                    │
                    ↓ USB Cable
                    ↓
┌─────────────────────────────────────────┐
│  PC (python_datacap)                    │
├─────────────────────────────────────────┤
│                                         │
│  USB Audio ←── sounddevice library     │
│  Serial ────── pyserial (PING/DATA)    │
│                                         │
│  vape_logger_gui.py                    │
│  - Real-time plots                      │
│  - Data recording                       │
│  - Edge Impulse export                  │
│                                         │
└─────────────────────────────────────────┘
```

---

## Key Points

### Threads (4 total)
1. **audio_thread** (existing) - PDM → USB audio (priority 7)
2. **console_thread** (new) - Handle PING/PONG commands (priority 8)
3. **data_thread** (new) - Read sensors, output DATA packets (priority 8)
4. **main** - Initialization and idle loop

### Memory Impact
- IMU driver: ~1 KB
- ADC code: ~512 bytes
- Console buffer: ~256 bytes
- **Total new: ~2 KB** (very reasonable)

### Performance
- Audio: 48 kHz × 16-bit × 1 ch = 96 KB/s (USB)
- Serial: ~100 Hz × ~40 bytes = 4 KB/s (UART)
- CPU: <20% (audio is DMA-based)

---

## Next Steps

1. ✅ Branch created (vapelogger)
2. Update `boards/xiao_ble_nrf52840_sense.overlay`
3. Update `prj.conf`
4. Update `src/main.c`
5. Build and test
6. Test with python_datacap

Simple, focused implementation! 🎯
