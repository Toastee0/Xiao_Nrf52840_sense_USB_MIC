/*
 * USB Microphone for XIAO nRF52840 Sense
 * Streams PDM microphone data as USB Audio device
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_audio.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_mic, LOG_LEVEL_INF);

#define AUDIO_SAMPLE_RATE 16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)
#define BLOCK_SIZE (BYTES_PER_SAMPLE * (AUDIO_SAMPLE_RATE / 100))
#define BLOCK_COUNT 8

K_MEM_SLAB_DEFINE_STATIC(audio_mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *dmic_dev;
static const struct device *mic_usb_dev;
static bool audio_running = false;
static bool usb_enabled = false;

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
	LOG_INF("USB status changed: %d", status);
	
	/* USB_DC_CONFIGURED = 6 */
	if (status == 6) {
		LOG_INF("USB configured - will start streaming in 2 seconds");
		/* Give host time to set up, then start streaming */
		k_sleep(K_MSEC(2000));
		usb_enabled = true;
	}
}

/* USB Audio callbacks */
static void data_request(const struct device *dev)
{
	if (!usb_enabled) {
		usb_enabled = true;
		LOG_INF("USB audio stream enabled by host via data_request");
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

static int start_audio(void)
{
	int ret;

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("DMIC trigger start failed: %d", ret);
		return ret;
	}

	audio_running = true;
	LOG_INF("Audio streaming started");
	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("USB Microphone starting...");

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

	LOG_INF("USB Microphone ready - waiting for host to open stream");

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
