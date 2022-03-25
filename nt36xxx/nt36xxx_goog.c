/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#include "nt36xxx.h"
#include <linux/input/mt.h>
#include <samsung/exynos_drm_connector.h> /* to_exynos_connector_state() */


void nvt_heatmap_decode(
		const uint8_t *in, const uint32_t in_sz,
		const uint8_t *out, const uint32_t out_sz)
{
	const u16 ESCAPE_MASK = 0xF000;
	const u16 ESCAPE_BIT = 0x8000;
	const u16 *in_array = (u16 *)in;
	u16 *out_array = (u16 *)out;
	const int in_array_size = in_sz / 2;
	const int out_array_max_size = out_sz / 2;

	int i;
	int j;
	int out_array_size = 0;
	u16 prev_word = 0;
	u16 repetition = 0;

	for (i = 0; i < in_array_size; i++) {
		u16 curr_word = in_array[i];

		if ((curr_word & ESCAPE_MASK) == ESCAPE_BIT) {
			repetition = (curr_word & ~ESCAPE_MASK);
			if (out_array_size + repetition > out_array_max_size)
				break;
			for (j = 0; j < repetition; j++) {
				*out_array++ = prev_word;
				out_array_size++;
			}
		} else {
			if (out_array_size >= out_array_max_size)
				break;
			*out_array++ = curr_word;
			out_array_size++;
			prev_word = curr_word;
		}
	}

	if (i != in_array_size || out_array_size != out_array_max_size) {
		NVT_DBG("%d (in=%d, out=%d, rep=%d, out_max=%d).\n",
				i, in_array_size, out_array_size,
				repetition, out_array_max_size);
	}
}

#ifdef GOOG_TOUCH_INTERFACE
int nvt_get_channel_data(void *private_data,
			u32 type, u8 **ptr, u32 *size)
{
	int ret = 0;
	struct nvt_ts_data *ts = (struct nvt_ts_data *)private_data;
	uint32_t page_addr = HM_TOUCH_DIFF_ADDR;
	uint8_t *spi_buf = ts->heatmap_spi_buf;
	uint32_t spi_buf_size = ts->heatmap_spi_buf_size;

	/* Only support mutual data currently. */
	if (!(type & TOUCH_SCAN_TYPE_MUTUAL))
		return -ENODATA;

	/* Only support strength currently. */
	switch (type & ~TOUCH_SCAN_TYPE_MUTUAL) {
	case TOUCH_DATA_TYPE_RAW:
		ret = -ENODATA;
		page_addr = HM_RAWDATA_ADDR;
		spi_buf = ts->extra_spi_buf;
		spi_buf_size = ts->extra_spi_buf_size;
		break;
	case TOUCH_DATA_TYPE_BASELINE:
		ret = -ENODATA;
		page_addr = HM_BASELINE_ADDR;
		spi_buf = ts->extra_spi_buf;
		spi_buf_size = ts->extra_spi_buf_size;
		break;
	case TOUCH_DATA_TYPE_STRENGTH:
		if (ts->heatmap_en && ts->heatmap_addr)
			page_addr = ts->heatmap_addr;
		else
			page_addr = HM_TOUCH_DIFF_ADDR;
		spi_buf = ts->heatmap_spi_buf;
		spi_buf_size = ts->heatmap_spi_buf_size;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	if (ret)
		return ret;

	/* Extra 1 byte for SPI header. */
	if (page_addr == HM_TOUCH_DIFF_ADDR && ts->touch_heatmap_comp_len)
		spi_buf_size = ts->touch_heatmap_comp_len + 1;

	nvt_set_page(page_addr);
	spi_buf[0] = page_addr & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, spi_buf_size);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	if (page_addr == HM_TOUCH_DIFF_ADDR && ts->touch_heatmap_comp_len) {
		/* Skip 1 byte header to the data start. */
		nvt_heatmap_decode(spi_buf + 1, ts->touch_heatmap_comp_len,
				ts->heatmap_out_buf, ts->heatmap_out_buf_size);
		*ptr = ts->heatmap_out_buf;
		*size = ts->heatmap_out_buf_size;
	} else {
		*ptr = spi_buf + 1;
		*size = ts->x_num * ts->y_num * 2;
	}

	return ret;
}

int nvt_callback(void *private_data,
		u32 cmd, u32 sub_cmd, u8 **buffer, u32 *size)
{
	int ret = -EOPNOTSUPP;
	struct nvt_ts_data *ts = (struct nvt_ts_data *)private_data;

	switch (cmd) {
	case GTI_CMD_GET_SENSOR_DATA:
		ret = nvt_get_channel_data(ts, sub_cmd, buffer, size);
		break;
	case GTI_CMD_SET_GRIP: {
		#define GRIP_ENABLE  0x41
		#define GRIP_DISABLE 0x40
		uint8_t spi_buf[3] = {EVENT_MAP_HOST_CMD, 0x70, GRIP_DISABLE};
		uint8_t fw_cmd = GRIP_DISABLE;

		if (sub_cmd == GTI_SUB_CMD_ENABLE)
			fw_cmd = GRIP_ENABLE;
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		spi_buf[2] = fw_cmd;
		CTP_SPI_WRITE(ts->client, spi_buf, sizeof(spi_buf));
		ret = 0;
		NVT_LOG("grip %s.\n", (fw_cmd == GRIP_ENABLE) ? "enable" : "disable");
	}
		break;
	case GTI_CMD_SET_PALM: {
		#define PALM_ENABLE  0xB3
		#define PALM_DISABLE 0xB4
		uint8_t spi_buf[3] = {EVENT_MAP_HOST_CMD, PALM_DISABLE, 0};
		uint8_t fw_cmd = PALM_DISABLE;

		if (sub_cmd == GTI_SUB_CMD_ENABLE)
			fw_cmd = PALM_ENABLE;
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		spi_buf[1] = fw_cmd;
		CTP_SPI_WRITE(ts->client, spi_buf, sizeof(spi_buf));
		ret = 0;
		NVT_LOG("palm %s.\n", (fw_cmd == PALM_ENABLE) ? "enable" : "disable");
	}
		break;
	case GTI_CMD_SET_CONTINUOUS_REPORT: {
		#define CONTINUOUS_ENABLE  0x01
		#define CONTINUOUS_DISABLE 0x00
		uint8_t spi_buf[3] = {EVENT_MAP_HOST_CMD, 0x70, CONTINUOUS_DISABLE};
		uint8_t fw_cmd = CONTINUOUS_DISABLE;

		if (sub_cmd == GTI_SUB_CMD_ENABLE)
			fw_cmd = CONTINUOUS_ENABLE;
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		spi_buf[2] = fw_cmd;
		CTP_SPI_WRITE(ts->client, spi_buf, sizeof(spi_buf));
		ret = 0;
		NVT_DBG("continuous report %s.\n",
				(fw_cmd == GTI_SUB_CMD_ENABLE) ? "enable" : "disable");
	}
		break;
	default:
		NVT_ERR("unsupport request cmd %#x!\n", cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

#endif /* GOOG_TOUCH_INTERFACE */

#if defined(CONFIG_SOC_GOOGLE)

static void panel_bridge_enable(struct drm_bridge *bridge)
{
	struct nvt_ts_data *ts =
		container_of(bridge, struct nvt_ts_data, panel_bridge);

	NVT_LOG("\n");
	if (!ts->is_panel_lp_mode)
		nvt_ts_set_bus_ref(ts, NVT_BUS_REF_SCREEN_ON, true);
}

static void panel_bridge_disable(struct drm_bridge *bridge)
{
	struct nvt_ts_data *ts =
		container_of(bridge, struct nvt_ts_data, panel_bridge);

	if (bridge->encoder && bridge->encoder->crtc) {
		const struct drm_crtc_state *crtc_state = bridge->encoder->crtc->state;

		if (drm_atomic_crtc_effectively_active(crtc_state))
			return;
	}

	NVT_LOG("\n");
	nvt_ts_set_bus_ref(ts, NVT_BUS_REF_SCREEN_ON, false);
}

struct drm_connector *get_bridge_connector(struct drm_bridge *bridge)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(bridge->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->encoder == bridge->encoder)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);
	return connector;
}

static bool bridge_is_lp_mode(struct drm_connector *connector)
{
	if (connector && connector->state) {
		struct exynos_drm_connector_state *s =
			to_exynos_connector_state(connector->state);
		return s->exynos_mode.is_lp_mode;
	}
	return false;
}

static void panel_bridge_mode_set(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  const struct drm_display_mode *adjusted_mode)
{
	bool is_panel_lp_mode;
	struct nvt_ts_data *ts =
		container_of(bridge, struct nvt_ts_data, panel_bridge);

	if (!ts->connector || !ts->connector->state)
		ts->connector = get_bridge_connector(bridge);

	is_panel_lp_mode = bridge_is_lp_mode(ts->connector);

	NVT_LOG("LP from %d to %d\n", ts->is_panel_lp_mode, is_panel_lp_mode);
	if (ts->is_panel_lp_mode != is_panel_lp_mode)
		nvt_ts_set_bus_ref(ts, NVT_BUS_REF_SCREEN_ON, !is_panel_lp_mode);

	ts->is_panel_lp_mode = is_panel_lp_mode;
}

static const struct drm_bridge_funcs panel_bridge_funcs = {
	.enable = panel_bridge_enable,
	.disable = panel_bridge_disable,
	.mode_set = panel_bridge_mode_set,
};

#ifdef NVT_TS_PANEL_BRIDGE
int register_panel_bridge(struct nvt_ts_data *ts)
{
	NVT_LOG("\n");
#ifdef CONFIG_OF
	ts->panel_bridge.of_node = ts->client->dev.of_node;
#endif
	ts->panel_bridge.funcs = &panel_bridge_funcs;
	drm_bridge_add(&ts->panel_bridge);

	return 0;
}

void unregister_panel_bridge(struct drm_bridge *bridge)
{
	struct drm_bridge *node;

	NVT_LOG("\n");
	drm_bridge_remove(bridge);

	if (!bridge->dev) /* not attached */
		return;

	drm_modeset_lock(&bridge->dev->mode_config.connection_mutex, NULL);
	list_for_each_entry(node, &bridge->encoder->bridge_chain, chain_node) {
		if (node == bridge) {
			if (bridge->funcs->detach)
				bridge->funcs->detach(bridge);
			list_del(&bridge->chain_node);
			break;
		}
	}
	drm_modeset_unlock(&bridge->dev->mode_config.connection_mutex);
	bridge->dev = NULL;
}
#endif /* #ifdef NVT_TS_PANEL_BRIDGE */

void nvt_ts_aggregate_bus_state(struct nvt_ts_data *ts)
{
	/* Complete or cancel any outstanding transitions */
	cancel_delayed_work_sync(&ts->suspend_work);
	cancel_delayed_work_sync(&ts->resume_work);

	if ((ts->bus_refmask == 0 && ts->bTouchIsAwake == false) ||
	    (ts->bus_refmask && ts->bTouchIsAwake))
		return;

	if (ts->bus_refmask == 0) {
		queue_delayed_work(ts->event_wq, &ts->suspend_work,
				msecs_to_jiffies(NVT_SUSPEND_WORK_MS_DELAY));
		msleep(NVT_SUSPEND_POST_MS_DELAY);
	} else {
		queue_delayed_work(ts->event_wq, &ts->resume_work,
				msecs_to_jiffies(NVT_RESUME_WORK_MS_DELAY));
	}
}

int nvt_ts_set_bus_ref(struct nvt_ts_data *ts, u32 ref, bool enable)
{
	int result = 0;

	mutex_lock(&ts->bus_mutex);

	NVT_DBG("bus_refmask=0x%04X, ref=0x%04X, enable=%d\n",
		ts->bus_refmask, ref, enable);

	if ((enable && (ts->bus_refmask & ref)) ||
	    (!enable && !(ts->bus_refmask & ref))) {
		NVT_LOG("unexpected ref: bus_refmask=0x%04X, ref=0x%04X, enable=%d\n",
		ts->bus_refmask, ref, enable);
		mutex_unlock(&ts->bus_mutex);
		return -EINVAL;
	}

	if (enable) {
		/*
		 * IRQs can only keep the bus active. IRQs received while the
		 * bus is transferred to AOC should be ignored.
		 */
		if (ref == NVT_BUS_REF_IRQ && ts->bus_refmask == 0)
			result = -EAGAIN;
		else
			ts->bus_refmask |= ref;
	} else
		ts->bus_refmask &= ~ref;
	nvt_ts_aggregate_bus_state(ts);

	mutex_unlock(&ts->bus_mutex);

	/*
	 * When triggering a wake, wait up to one second to resume. SCREEN_ON
	 * and IRQ references do not need to wait.
	 */
	if (enable &&
	    ref != NVT_BUS_REF_SCREEN_ON && ref != NVT_BUS_REF_IRQ) {
		if (!ts->bTouchIsAwake &&
			!completion_done(&ts->bus_resumed)) {
			NVT_LOG("Wait for bus resume.\n");
			wait_for_completion_timeout(&ts->bus_resumed,
				msecs_to_jiffies(MSEC_PER_SEC));
		}
		if (!ts->bTouchIsAwake) {
			NVT_ERR("Failed to wake the touch bus.\n");
			result = -ETIMEDOUT;
		}
	}

	return result;
}

ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOG("++\n");

	ret = scnprintf(buf, PAGE_SIZE, "bus_refmask %#x\n", ts->bus_refmask);

	NVT_LOG("--\n");
	return ret;
}

ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 mode;
	int ret;
	bool active;
	u32 ref = 0;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 0, &mode)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}

	switch (mode) {
	case 0x10:
		ref = NVT_BUS_REF_FORCE_ACTIVE;
		active = false;
		break;
	case 0x11:
		ref = NVT_BUS_REF_FORCE_ACTIVE;
		active = true;
		break;
	case 0x20:
		ref = NVT_BUS_REF_BUGREPORT;
		active = false;
		ts->bugreport_ktime_start = 0;
		break;
	case 0x21:
		ref = NVT_BUS_REF_BUGREPORT;
		active = true;
		ts->bugreport_ktime_start = ktime_get();
		break;
	}

	if (ref == 0) {
		NVT_ERR("invalid input %#x.\n", mode);
		return -EINVAL;
	}

	NVT_LOG("%s ref %#x\n",
		(active) ? "enable" : "disable", ref);

	if (active) {
		if (!ts->bTouchIsAwake) {
			input_report_key(ts->input_dev, KEY_WAKEUP, true);
			input_sync(ts->input_dev);
			input_report_key(ts->input_dev, KEY_WAKEUP, false);
			input_sync(ts->input_dev);
			NVT_LOG("KEY_WAKEUP triggered.\n");
		}
		pm_stay_awake(&ts->client->dev);
	} else {
		pm_relax(&ts->client->dev);
	}

	if (!ts->bTouchIsAwake)
		msleep(NVT_FORCE_ACTIVE_MS_DELAY);

	ret = nvt_ts_set_bus_ref(ts, ref, active);

	if (ret)
		NVT_ERR("failed, ret %d bus_ref %#x!\n", ret, ts->bus_refmask);

	NVT_LOG("--\n");
	return count;
}

ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOG("++\n");

	ret = scnprintf(buf, PAGE_SIZE, "force_release_fw %d\n", ts->force_release_fw);

	NVT_LOG("--\n");
	return ret;
}

ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 mode;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 0, &mode)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}

	ts->force_release_fw = (mode) ? 1 : 0;
	if (ts->force_release_fw)
		update_firmware_release();

	NVT_LOG("--\n");
	return count;
}

int nvt_ts_pm_suspend(struct device *dev)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);

	NVT_LOG("bus_refmask %#x\n", ts->bus_refmask);

	/* Flush work in case a suspend is in progress */
	flush_workqueue(ts->event_wq);

	if (ts->bTouchIsAwake) {
		NVT_ERR("can't suspend because touch bus is in use, bus_refmask %#x!\n",
			ts->bus_refmask);
		if (ts->bus_refmask & NVT_BUS_REF_BUGREPORT) {
			s64 delta_ms = ktime_ms_delta(ktime_get(),
							ts->bugreport_ktime_start);
			if (delta_ms > 30 * MSEC_PER_SEC) {
				nvt_ts_set_bus_ref(ts, NVT_BUS_REF_BUGREPORT, false);
				pm_relax(&ts->client->dev);
				ts->bugreport_ktime_start = 0;
				NVT_ERR("force release NVT_BUS_REF_BUGREPORT(delta: %lld)!\n",
					delta_ms);
			}
		}
		return -EBUSY;
	}

	return 0;
}

int nvt_ts_pm_resume(struct device *dev)
{
	return 0;
}

#endif /* defined(CONFIG_SOC_GOOGLE) */
