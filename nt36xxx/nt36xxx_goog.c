/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#include "nt36xxx.h"
#include <linux/input/mt.h>
#include <samsung/exynos_drm_connector.h> /* to_exynos_connector_state() */

#if defined(GOOG_OFFLOAD)
bool goog_offload_is_off(struct nvt_ts_data *ts)
{
	return !(ts->offload.offload_running);
}

void goog_input_lock(struct nvt_ts_data *ts)
{
	mutex_lock(&ts->input_report_lock);
}

void goog_input_unlock(struct nvt_ts_data *ts)
{
	mutex_unlock(&ts->input_report_lock);
}

void goog_offload_update_coords(struct nvt_ts_data *ts)
{
	int i;

	for (i = 0 ; i < TOUCH_MAX_FINGER_NUM ; i++) {
		ts->offload.coords[i].x = ts->coords[i].x;
		ts->offload.coords[i].y = ts->coords[i].y;
		ts->offload.coords[i].major = ts->coords[i].major;
		ts->offload.coords[i].minor = 0;
		ts->offload.coords[i].pressure = ts->coords[i].pressure;
		ts->offload.coords[i].status = ts->coords[i].status;
	}
}

static void goog_offload_populate_coordinate_channel(struct nvt_ts_data *ts,
		struct touch_offload_frame *frame, int channel)
{
	int i;

	struct TouchOffloadDataCoord *dc =
		(struct TouchOffloadDataCoord *)frame->channel_data[channel];
	memset(dc, 0, frame->channel_data_size[channel]);
	dc->header.channel_type = TOUCH_DATA_TYPE_COORD;
	dc->header.channel_size = TOUCH_OFFLOAD_FRAME_SIZE_COORD;

	for (i = 0; i < MAX_COORDS; i++) {
		dc->coords[i].x = ts->offload.coords[i].x;
		dc->coords[i].y = ts->offload.coords[i].y;
		dc->coords[i].major = ts->offload.coords[i].major;
		dc->coords[i].minor = ts->offload.coords[i].minor;
		dc->coords[i].pressure = ts->offload.coords[i].pressure;
		dc->coords[i].status = ts->offload.coords[i].status;
	}
}

static void goog_offload_populate_mutual_channel(struct nvt_ts_data *ts,
		struct touch_offload_frame *frame, int channel)
{
	uint32_t page_addr = HM_DIFF_ADDR;
	uint8_t *ms_data = ts->heatmap_spi_buf + 1;	/* skip 1st stuffing byte */
	uint32_t ms_data_sz = ts->x_num * ts->y_num * 2;

	struct TouchOffloadData2d *mutual_strength =
		(struct TouchOffloadData2d *)frame->channel_data[channel];

	switch (frame->channel_type[channel] & ~TOUCH_SCAN_TYPE_MUTUAL) {
	case TOUCH_DATA_TYPE_RAW:
		page_addr = HM_RAWDATA_ADDR;
		break;
	case TOUCH_DATA_TYPE_BASELINE:
		page_addr = HM_BASELINE_ADDR;
		break;
	case TOUCH_DATA_TYPE_STRENGTH:
		page_addr = HM_DIFF_ADDR;
		break;
	}

	mutual_strength->tx_size = ts->x_num;
	mutual_strength->rx_size = ts->y_num;
	mutual_strength->header.channel_type = frame->channel_type[channel];
	mutual_strength->header.channel_size =
		TOUCH_OFFLOAD_FRAME_SIZE_2D(mutual_strength->rx_size,
			mutual_strength->tx_size);

	/* Read strength data for offload and v4l2. */
	nvt_set_page(ts->heatmap_addr);
	ts->heatmap_spi_buf[0] = ts->heatmap_addr & 0x7F;
	CTP_SPI_READ(ts->client, ts->heatmap_spi_buf, ts->heatmap_spi_buf_size);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	/* Read non-strength data by request for offload. */
	if (page_addr != ts->heatmap_addr) {
		nvt_set_page(page_addr);
		ts->heatmap_spi_buf[0] = page_addr & 0x7F;
		CTP_SPI_READ(ts->client, ts->offload_spi_buf, ts->offload_spi_buf_size);
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		ms_data = ts->offload_spi_buf + 1;
	}

	/* Copy data into offload frame. */
	memcpy(mutual_strength->data, ms_data, ms_data_sz);
}

static void goog_offload_populate_frame(struct nvt_ts_data *ts,
		struct touch_offload_frame *frame)
{
	static u64 index;
	u8 channel_type;
	int i;

	frame->header.index = index++;
	frame->header.timestamp = ts->timestamp;

	/* Populate all channels */
	for (i = 0; i < frame->num_channels; i++) {
		channel_type = frame->channel_type[i];
		if (channel_type == TOUCH_DATA_TYPE_COORD)
			goog_offload_populate_coordinate_channel(ts, frame, i);
		else if ((channel_type&TOUCH_SCAN_TYPE_MUTUAL) != 0)
			goog_offload_populate_mutual_channel(ts, frame, i);
	}
}

static void goog_offload_set_running(struct nvt_ts_data *ts, bool running)
{
	if (ts->offload.offload_running != running) {
		ts->offload.offload_running = running;
	/*
	 * TODO(b/193467748):
	 * Enable/disable FW grip/palm for offload.
	 */
/*
		if (running)
			nvt_ts_enable_fw_grip(ts, false);
		else
			nvt_ts_enable_fw_grip(ts, true);
*/
	}
}

void goog_offload_push_frame(struct nvt_ts_data *ts)
{
	int ret;
	struct touch_offload_frame *frame = NULL;

	if (!ts->offload_enable)
		return;

	ret = touch_offload_reserve_frame(&ts->offload, &frame);
	if (ret != 0) {
		NVT_ERR("could not reserve a frame, ret=%d.\n", ret);
		/* Stop offload when there are no buffers available. */
		goog_offload_set_running(ts, false);
		/*
		 * TODO(b/193467748):
		 * How to handle current coord if offload running
		 * terminating in the halfway(not beginning case)?
		 */
	} else {
		goog_offload_set_running(ts, true);
		goog_offload_populate_frame(ts, frame);
		ret = touch_offload_queue_frame(&ts->offload, frame);
		if (ret != 0)
			NVT_ERR("failed to queue reserved frame, ret=%d.\n", ret);
	}
}

static void goog_offload_report(void *handle,
			struct TouchOffloadIocReport *report)
{
	struct nvt_ts_data *ts = (struct nvt_ts_data *)handle;
	bool touch_down = 0;
	int i;

	goog_input_lock(ts);
	input_set_timestamp(ts->input_dev, report->timestamp);
	for (i = 0; i < TOUCH_MAX_FINGER_NUM; i++) {
		if (report->coords[i].status == COORD_STATUS_FINGER) {
			input_mt_slot(ts->input_dev, i);
			touch_down = 1;
			input_report_key(ts->input_dev, BTN_TOUCH, touch_down);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
				report->coords[i].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
				report->coords[i].y);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
				report->coords[i].major);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
				report->coords[i].pressure);
		} else {
			input_mt_slot(ts->input_dev, i);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, -1);
		}
	}
	input_report_key(ts->input_dev, BTN_TOUCH, touch_down);
	input_sync(ts->input_dev);
	goog_input_unlock(ts);

	/*
	 * TODO(b/193467748):
	 * There could be one race condition that 'heatmap_spi_buf'
	 * already updated by 'T + 1' frame. But, what v4l2 needed
	 * is 'T' frame heatmap. This could be resolved that the
	 * 'report' is not only with coords, but also includes
	 * corresponding 'frame' with heatmap.
	 */
	if (ts->v4l2_enable && touch_down)
		heatmap_read(&ts->v4l2, ktime_to_ns(report->timestamp));

	/*
	 * TODO(b/193467748):
	 * Disable the firmware motion filter during single touch.
	 */
//	update_motion_filter(ts, touch_id);
}

int32_t goog_offload_remove(struct nvt_ts_data *ts)
{
	int32_t ret;

	ret = touch_offload_cleanup(&ts->offload);
	return ret;
}

int32_t goog_offload_probe(struct nvt_ts_data *ts)
{
	int32_t ret;
	struct device_node *np = ts->client->dev.of_node;

	if (of_property_read_u8_array(np, "goog,touch_offload_id",
			ts->offload_id_byte, 4)) {
		NVT_LOG("set default offload id!\n");
		ts->offload_id_byte[0] = 't';
		ts->offload_id_byte[1] = '6';
		ts->offload_id_byte[2] = '0';
		ts->offload_id_byte[3] = '0';
	}

	ts->offload.caps.touch_offload_major_version = 1;
	ts->offload.caps.touch_offload_minor_version = 0;
	ts->offload.caps.device_id = ts->offload_id;
	ts->offload.caps.display_width = ts->touch_width;
	ts->offload.caps.display_height = ts->touch_height;
	ts->offload.caps.tx_size = ts->v4l2.width;
	ts->offload.caps.rx_size = ts->v4l2.height;
	ts->offload.caps.heatmap_size = HEATMAP_SIZE_FULL;
	ts->offload.caps.bus_type = BUS_TYPE_SPI;
	ts->offload.caps.bus_speed_hz = ts->client->max_speed_hz;

	/* Currently can only reliably read mutual heatmap each frame.
	 * Cannot support other formats due to penalties associated
	 * with switching data types.
	 */
	ts->offload.caps.touch_data_types =
		TOUCH_DATA_TYPE_COORD | TOUCH_DATA_TYPE_STRENGTH |
		TOUCH_DATA_TYPE_RAW | TOUCH_DATA_TYPE_BASELINE;
	ts->offload.caps.touch_scan_types =
		TOUCH_SCAN_TYPE_MUTUAL;

	ts->offload.caps.continuous_reporting = true;
	ts->offload.caps.noise_reporting = false;
	ts->offload.caps.cancel_reporting = false;
	ts->offload.caps.size_reporting = true;
	ts->offload.caps.filter_grip = true;
	ts->offload.caps.filter_palm = true;
	ts->offload.caps.num_sensitivity_settings = 1;

	ts->offload.hcallback = (void *)ts;
	ts->offload.report_cb = goog_offload_report;
	ret = touch_offload_init(&ts->offload);
	if (!ret && !ts->offload_spi_buf) {
		/* Need one stuffing byte for heatmap I/O transfer. */
		ts->offload_spi_buf_size = ts->v4l2.width * ts->v4l2.height * 2 + 1;
		ts->offload_spi_buf = devm_kzalloc(&ts->client->dev,
				ts->offload_spi_buf_size, GFP_KERNEL);
		if (!ts->offload_spi_buf) {
			NVT_ERR("failed to alloc offload buf!\n");
			ret = -ENOMEM;
		}
	}

	ts->offload_enable = of_property_read_bool(np, "goog,offload-enable");
	NVT_LOG("offload ID: \"%c%c%c%c\" / 0x%08X, offload_enable=%d.\n",
		ts->offload_id_byte[0], ts->offload_id_byte[1], ts->offload_id_byte[2],
		ts->offload_id_byte[3], ts->offload_id, ts->offload_enable);
	return ret;
}
#endif

#if defined(GOOG_HEATMAP)
static bool goog_v4l2_read_frame(struct v4l2_heatmap *v4l2)
{
	struct nvt_ts_data *ts = container_of(v4l2, struct nvt_ts_data, v4l2);
	const uint8_t *heatmap_buf = ts->heatmap_spi_buf + 1; /* skip 1st stuffing byte */
	bool ret = false;

	if (ts->heatmap_updated) {
		if (ts->v4l2.width == ts->x_num &&
			ts->v4l2.height == ts->y_num) {
			memcpy(v4l2->frame, heatmap_buf,
			   ts->v4l2.width * ts->v4l2.height * 2);
			ts->heatmap_updated = false;
			ret = true;
		} else {
			NVT_ERR("size mismatched, (%lu, %lu) vs (%u, %u)!\n",
			ts->v4l2.width, ts->v4l2.height,
			ts->x_num, ts->y_num);
		}
	}
	return ret;
}

void goog_heatmap_read(struct nvt_ts_data *ts)
{
	if (ts->v4l2_enable)
		heatmap_read(&ts->v4l2, ktime_to_ns(ts->timestamp));
}

void goog_heatmap_remove(struct nvt_ts_data *ts)
{
	heatmap_remove(&ts->v4l2);
	ts->v4l2_enable = false;
}

int32_t goog_heatmap_probe(struct nvt_ts_data *ts)
{
	int32_t ret;
	u32 width, height;
	struct device_node *np = ts->client->dev.of_node;

	/*
	 * Heatmap_probe must be called before irq routine is registered,
	 * because heatmap_read is called from the irq context.
	 * If the ISR runs before heatmap_probe is finished, it will invoke
	 * heatmap_read and cause NPE, since read_frame would not yet be set.
	 */
	ts->v4l2.parent_dev = &ts->client->dev;
	ts->v4l2.input_dev = ts->input_dev;
	ts->v4l2.read_frame = goog_v4l2_read_frame;
	if (of_property_read_u32(np, "goog,v4l2-width", &width))
		ts->v4l2.width = NVT_V4L2_DEFAULT_WIDTH;
	else
		ts->v4l2.width = width;
	if (of_property_read_u32(np, "goog,v4l2-height", &height))
		ts->v4l2.height = NVT_V4L2_DEFAULT_HEIGHT;
	else
		ts->v4l2.height = height;
	/* 120 Hz operation */
	ts->v4l2.timeperframe.numerator = 1;
	ts->v4l2.timeperframe.denominator = 120;
	ret = heatmap_probe(&ts->v4l2);
	if (!ret && !ts->heatmap_spi_buf) {
		/* Need one stuffing byte for heatmap I/O transfer. */
		ts->heatmap_spi_buf_size = ts->v4l2.width * ts->v4l2.height * 2 + 1;
		ts->heatmap_spi_buf = devm_kzalloc(&ts->client->dev,
				ts->heatmap_spi_buf_size, GFP_KERNEL);
		if (!ts->heatmap_spi_buf) {
			NVT_ERR("failed to alloc heatmap buf!\n");
			ret = -ENOMEM;
		} else {
			ts->heatmap_addr = HM_DIFF_ADDR;
			ts->heatmap_en = 1;
			ts->v4l2_enable = of_property_read_bool(np, "goog,v4l2-enable");
		}
	}

	NVT_LOG("v4l2 W/H=(%lu, %lu), v4l2_enable=%d.\n",
		ts->v4l2.width, ts->v4l2.height, ts->v4l2_enable);
	return ret;
}
#endif

#if defined(CONFIG_SOC_GOOGLE)

void goog_update_press_coord(struct nvt_ts_data *ts, uint32_t slot,
		uint32_t x, uint32_t y, uint32_t major, uint32_t pressure)
{
	if (slot < TOUCH_MAX_FINGER_NUM) {
		ts->coords[slot].x = x;
		ts->coords[slot].y = y;
		ts->coords[slot].major = major;
		ts->coords[slot].pressure = pressure;
		if (!ts->coords[slot].status) {
			ts->coords[slot].x_pressed = x;
			ts->coords[slot].y_pressed = y;
			ts->coords[slot].ktime_pressed = ktime_get();
		} else {
			ts->coords[slot].status = 1;
		}
	}
}

void goog_update_release_coord(struct nvt_ts_data *ts, uint32_t slot)
{
	if (slot < TOUCH_MAX_FINGER_NUM) {
		if (ts->coords[slot].status) {
			NVT_LOG("force to release slot %d!\n", slot);
			ts->coords[slot].ktime_released = ktime_get();
			ts->coords[slot].status = 0;
			ts->coords[slot].major = 0;
			ts->coords[slot].pressure = 0;
		}
	}
}

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

	if (ts->bus_refmask == 0)
		queue_delayed_work(ts->event_wq, &ts->suspend_work,
				msecs_to_jiffies(NVT_SUSPEND_WORK_MS_DELAY));
	else
		queue_delayed_work(ts->event_wq, &ts->resume_work,
				msecs_to_jiffies(NVT_RESUME_WORK_MS_DELAY));
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

ssize_t goog_offload_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;

	ret = scnprintf(buf, PAGE_SIZE, "offload_enable: %u\n", ts->offload_enable);
	return ret;
}
ssize_t goog_offload_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (kstrtobool(buf, &ts->offload_enable)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}
#if defined(GOOG_OFFLOAD)
	if (!ts->offload_enable && ts->offload.offload_running) {
		NVT_LOG("terminate offload!\n");
		ts->offload.offload_running = false;
	}
#endif
	return count;
}

ssize_t goog_v4l2_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;

	ret = scnprintf(buf, PAGE_SIZE, "v4l2_enable: %d\n", ts->v4l2_enable);
	return ret;
}
ssize_t goog_v4l2_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (kstrtobool(buf, &ts->v4l2_enable)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}
	return count;
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
			input_report_key(ts->input_dev, KEY_WAKEUP, false);
			input_sync(ts->input_dev);
			input_report_key(ts->input_dev, KEY_WAKEUP, true);
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
