/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#include "nt36xxx.h"
#include <samsung/exynos_drm_connector.h> /* to_exynos_connector_state() */

#if defined(GOOG_HEATMAP)
static bool goog_v4l2_read_frame(struct v4l2_heatmap *v4l2)
{
	bool ret = false;

	if (ts->heatmap_updated) {
		if (ts->v4l2.width == ts->x_num &&
			ts->v4l2.height == ts->y_num) {
			memcpy(v4l2->frame, ts->heatmap_spi_buf + 1,
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
	heatmap_read(&ts->v4l2, ktime_to_ns(ts->timestamp));
}

void goog_heatmap_remove(struct nvt_ts_data *ts)
{
	heatmap_remove(&ts->v4l2);
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
		}
	}

	NVT_LOG("v4l2 W/H=(%lu, %lu).\n", ts->v4l2.width, ts->v4l2.height);
	return ret;
}
#endif

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
