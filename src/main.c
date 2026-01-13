/*
 * VapeLogger for XIAO nRF52840 Sense
 * Streams PDM microphone data as USB Audio device
 * Streams ADC coil data via serial UART
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_audio.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(vapelogger, LOG_LEVEL_INF);

#define AUDIO_SAMPLE_RATE 48000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define BLOCK_SIZE (BYTES_PER_SAMPLE * (AUDIO_SAMPLE_RATE / 100))
#define BLOCK_COUNT 8

K_MEM_SLAB_DEFINE_STATIC(audio_mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *dmic_dev;
static const struct device *mic_usb_dev;
static bool audio_running = false;
static bool usb_enabled = false;

/* ADC device for coil monitoring */
static const struct device *adc_dev = NULL;
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

/* IMU device for motion tracking */
static const struct device *imu_dev = NULL;

/* Latest sensor values for combined output */
static uint16_t latest_coil_adc = 0;
static int32_t latest_accel[3] = {0, 0, 0};
static int32_t latest_gyro[3] = {0, 0, 0};
static K_MUTEX_DEFINE(sensor_mutex);

/* DMIC configuration */
static struct pcm_stream_cfg stream = {
	.pcm_width = SAMPLE_BIT_WIDTH,
	.mem_slab = &audio_mem_slab,
};

static struct dmic_cfg cfg = {
	.io = {
		.min_pdm_clk_freq = 1000000,
		.max_pdm_clk_freq = 3500000,
		.min_pdm_clk_dc = 40,
		.max_pdm_clk_dc = 60,
	},
	.streams = &stream,
	.channel = {
		.req_num_streams = 1,
	},
};

/* USB device status callback */
static void usb_status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
	printk("USB status: %d\n", status);
	LOG_INF("USB status changed: %d", status);
	
	switch (status) {
	case USB_DC_CONNECTED:
		printk("USB connected\n");
		break;
	case USB_DC_CONFIGURED:
		printk("USB configured - waiting for host to request audio\n");
		LOG_INF("USB configured - device ready");
		break;
	case USB_DC_DISCONNECTED:
		printk("USB disconnected\n");
		usb_enabled = false;
		if (audio_running && dmic_dev) {
			dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
			audio_running = false;
		}
		break;
	default:
		break;
	}
}

/* USB Audio callbacks */
static void data_request(const struct device *dev)
{
	if (!usb_enabled) {
		printk("Host requesting audio - starting DMIC\n");
		LOG_INF("USB audio stream enabled by host");
		
		/* Start DMIC streaming now that host wants data */
		if (dmic_dev && !audio_running) {
			int ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
			if (ret == 0) {
				audio_running = true;
				printk("DMIC started successfully\n");
				LOG_INF("Audio streaming started");
			} else {
				printk("DMIC start failed: %d\n", ret);
				LOG_ERR("DMIC trigger failed: %d", ret);
			}
		}
		usb_enabled = true;
	}
}

static void feature_update(const struct device *dev,
                           const struct usb_audio_fu_evt *evt)
{
	LOG_INF("Control selector %d for channel %d updated",
		evt->cs, evt->channel);
}

static const struct usb_audio_ops mic_ops = {
	.data_request_cb = data_request,
	.feature_update_cb = feature_update,
};

/* Thread to read PDM data and send to USB */
static void audio_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	void *buffer;
	uint32_t size;
	int ret;
	uint32_t count = 0;

	while (1) {
		if (!audio_running || !usb_enabled) {
			k_sleep(K_MSEC(100));
			continue;
		}

		ret = dmic_read(dmic_dev, 0, &buffer, &size, 1000);
		if (ret < 0) {
			if (ret != -EAGAIN) {
				LOG_ERR("DMIC read failed: %d", ret);
			}
			k_sleep(K_MSEC(1));
			continue;
		}

		if ((count++ % 100) == 0) {
			LOG_INF("Read %d bytes from DMIC, sending to USB", size);
		}

		ret = usb_audio_send(mic_usb_dev, buffer, size);
		if (ret < 0) {
			if ((count % 100) == 0) {
				LOG_ERR("USB audio send failed: %d", ret);
			}
		}

		k_mem_slab_free(&audio_mem_slab, buffer);
	}
}

K_THREAD_DEFINE(audio_tid, 2048, audio_thread, NULL, NULL, NULL, 7, 0, 0);

/* Thread to read ADC and update shared state */
static void adc_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	uint16_t adc_val;
	int ret;

	/* Wait for system to stabilize and ADC to be initialized */
	k_sleep(K_SECONDS(3));

	/* Check if ADC was initialized */
	if (adc_dev == NULL) {
		LOG_WRN("ADC not initialized, thread exiting");
		return;
	}

	LOG_INF("ADC monitoring started");

	while (1) {
		ret = adc_read(adc_dev, &adc_seq);
		if (ret < 0) {
			LOG_ERR("ADC read failed: %d", ret);
		} else {
			adc_val = (uint16_t)adc_sample_buffer;
			k_mutex_lock(&sensor_mutex, K_FOREVER);
			latest_coil_adc = adc_val;
			k_mutex_unlock(&sensor_mutex);
		}

		k_sleep(K_MSEC(50));  /* Match IMU rate */
	}
}

K_THREAD_DEFINE(adc_tid, 1024, adc_thread, NULL, NULL, NULL, 8, 0, 0);

/* Thread to read IMU and output combined data to serial */
static void imu_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct sensor_value accel_x, accel_y, accel_z;
	struct sensor_value gyro_x, gyro_y, gyro_z;
	int ret;

	/* Wait for system to stabilize and IMU to be initialized */
	k_sleep(K_SECONDS(3));

	/* Check if IMU was initialized */
	if (imu_dev == NULL) {
		LOG_WRN("IMU not initialized, thread exiting");
		return;
	}

	LOG_INF("IMU monitoring started");

	while (1) {
		ret = sensor_sample_fetch(imu_dev);
		if (ret < 0) {
			LOG_ERR("IMU fetch failed: %d", ret);
			k_sleep(K_MSEC(100));
			continue;
		}

		/* Read accelerometer (m/s²) */
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &accel_x);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &accel_y);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &accel_z);

		/* Read gyroscope (rad/s) */
		sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_X, &gyro_x);
		sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Y, &gyro_y);
		sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Z, &gyro_z);

		/* Convert to integers (multiply by 100 to keep 2 decimal places) */
		int32_t ax = (accel_x.val1 * 100) + (accel_x.val2 / 10000);
		int32_t ay = (accel_y.val1 * 100) + (accel_y.val2 / 10000);
		int32_t az = (accel_z.val1 * 100) + (accel_z.val2 / 10000);
		int32_t gx = (gyro_x.val1 * 100) + (gyro_x.val2 / 10000);
		int32_t gy = (gyro_y.val1 * 100) + (gyro_y.val2 / 10000);
		int32_t gz = (gyro_z.val1 * 100) + (gyro_z.val2 / 10000);

		/* Get latest coil ADC value */
		uint16_t coil_adc;
		k_mutex_lock(&sensor_mutex, K_FOREVER);
		coil_adc = latest_coil_adc;
		k_mutex_unlock(&sensor_mutex);

		/* Print combined data in single line: COIL,IMU_DATA */
		/* Format: DATA:coil,ax,ay,az,gx,gy,gz */
		printk("DATA:%u,%d.%02d,%d.%02d,%d.%02d,%d.%02d,%d.%02d,%d.%02d\n",
			coil_adc,
			ax/100, abs(ax%100), ay/100, abs(ay%100), az/100, abs(az%100),
			gx/100, abs(gx%100), gy/100, abs(gy%100), gz/100, abs(gz%100));

		k_sleep(K_MSEC(50));  /* 20 Hz update rate */
	}
}

K_THREAD_DEFINE(imu_tid, 1536, imu_thread, NULL, NULL, NULL, 8, 0, 0);

static int init_dmic(void)
{
	int ret;

	dmic_dev = DEVICE_DT_GET(DT_NODELABEL(pdm0));
	if (!device_is_ready(dmic_dev)) {
		LOG_ERR("DMIC device not ready");
		return -ENODEV;
	}

	cfg.channel.req_num_chan = 1;
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
	cfg.streams[0].pcm_rate = AUDIO_SAMPLE_RATE;
	cfg.streams[0].block_size = BLOCK_SIZE;

	ret = dmic_configure(dmic_dev, &cfg);
	if (ret < 0) {
		LOG_ERR("DMIC configure failed: %d", ret);
		return ret;
	}

	LOG_INF("DMIC configured: %d Hz, %d-bit", AUDIO_SAMPLE_RATE, SAMPLE_BIT_WIDTH);
	return 0;
}

static int init_usb_audio(void)
{
	int ret;

	mic_usb_dev = DEVICE_DT_GET_ONE(usb_audio_mic);
	if (!device_is_ready(mic_usb_dev)) {
		LOG_ERR("USB Microphone device not ready");
		return -ENODEV;
	}

	usb_audio_register(mic_usb_dev, &mic_ops);

	ret = usb_enable(usb_status_cb);
	if (ret < 0) {
		LOG_ERR("USB enable failed: %d", ret);
		return ret;
	}

	LOG_INF("USB Audio initialized");
	return 0;
}

static int init_adc(void)
{
	int ret;

	adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not ready");
		adc_dev = NULL;
		return -ENODEV;
	}

	ret = adc_channel_setup(adc_dev, &adc_cfg);
	if (ret < 0) {
		LOG_ERR("ADC channel setup failed: %d", ret);
		adc_dev = NULL;
		return ret;
	}

	LOG_INF("ADC initialized on A0");
	return 0;
}

static int init_imu(void)
{
	int ret;
	struct sensor_value odr, range;

	imu_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));
	if (!device_is_ready(imu_dev)) {
		LOG_ERR("IMU device not ready");
		imu_dev = NULL;
		return -ENODEV;
	}

	/* Set accelerometer ODR to 104 Hz */
	odr.val1 = 104;
	odr.val2 = 0;
	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (ret < 0) {
		LOG_WRN("Failed to set accel ODR: %d", ret);
	}

	/* Set accelerometer range to ±2g */
	range.val1 = 2;
	range.val2 = 0;
	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_FULL_SCALE, &range);
	if (ret < 0) {
		LOG_WRN("Failed to set accel range: %d", ret);
	}

	/* Set gyroscope ODR to 104 Hz */
	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	if (ret < 0) {
		LOG_WRN("Failed to set gyro ODR: %d", ret);
	}

	/* Set gyroscope range to ±250 dps */
	range.val1 = 250;
	range.val2 = 0;
	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ,
			      SENSOR_ATTR_FULL_SCALE, &range);
	if (ret < 0) {
		LOG_WRN("Failed to set gyro range: %d", ret);
	}

	LOG_INF("IMU (LSM6DS3) initialized and configured");
	printk("IMU configured: 104Hz, ±2g accel, ±250dps gyro\n");
	return 0;
}

/* Audio is now started from usb_status_cb when USB_DC_CONFIGURED */

int main(void)
{
	int ret;

	printk("\n\n=== VapeLogger Starting ===\n");
	LOG_INF("VapeLogger starting...");

	/* Small delay for USB to stabilize */
	k_sleep(K_MSEC(500));

	/* Initialize USB first for device enumeration */
	ret = init_usb_audio();
	if (ret < 0) {
		LOG_ERR("USB Audio init failed: %d", ret);
		printk("ERROR: USB init failed: %d\n", ret);
		/* Don't return - continue in degraded mode */
	} else {
		printk("USB Audio initialized OK\n");
	}

	/* Initialize DMIC */
	ret = init_dmic();
	if (ret < 0) {
		LOG_ERR("DMIC init failed: %d", ret);
		printk("ERROR: DMIC init failed: %d\n", ret);
		/* Don't return - continue without audio */
	} else {
		printk("DMIC initialized OK\n");
	}

	/* Initialize ADC (non-critical) */
	ret = init_adc();
	if (ret < 0) {
		LOG_WRN("ADC init failed: %d - continuing without ADC", ret);
		printk("WARNING: ADC init failed: %d\n", ret);
	} else {
		printk("ADC initialized OK\n");
	}

	/* Initialize IMU (non-critical) */
	ret = init_imu();
	if (ret < 0) {
		LOG_WRN("IMU init failed: %d - continuing without IMU", ret);
		printk("WARNING: IMU init failed: %d\n", ret);
	} else {
		printk("IMU initialized OK\n");
	}

	k_sleep(K_MSEC(1000));

	/* Audio will start automatically when USB host configures the device */
	printk("\n=== VapeLogger Ready ===\n");
	printk("Waiting for USB host to configure audio device...\n");
	printk("Audio will start automatically when host is ready\n");
	LOG_INF("VapeLogger ready - waiting for USB host configuration");

	/* Main loop - NEVER return from main() */
	while (1) {
		k_sleep(K_SECONDS(5));
		printk(".");  /* Heartbeat */
	}

	/* Should never reach here */
	return 0;
}
