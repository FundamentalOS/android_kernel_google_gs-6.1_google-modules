/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#include "nt36xxx.h"
#include <samsung/exynos_drm_connector.h> /* to_exynos_connector_state() */

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

	NVT_DBG("mask=0x%04X, ref=0x%04X, enable=%d\n",
		ts->bus_refmask, ref, enable);

	if ((enable && (ts->bus_refmask & ref)) ||
	    (!enable && !(ts->bus_refmask & ref))) {
		NVT_LOG("reference is unexpectedly set: mask=0x%04X, ref=0x%04X, enable=%d\n",
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
		wait_for_completion_timeout(&ts->bus_resumed, HZ);
		if (!ts->bTouchIsAwake) {
			NVT_ERR("Failed to wake the touch bus.\n");
			result = -ETIMEDOUT;
		}
	}

	return result;
}
