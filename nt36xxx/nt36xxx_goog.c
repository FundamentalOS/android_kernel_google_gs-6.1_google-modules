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

	NVT_LOG("\n");
}

static void panel_bridge_disable(struct drm_bridge *bridge)
{
	if (bridge->encoder && bridge->encoder->crtc) {
		const struct drm_crtc_state *crtc_state = bridge->encoder->crtc->state;

		if (drm_atomic_crtc_effectively_active(crtc_state))
			return;
	}

	NVT_LOG("\n");
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
	struct nvt_ts_data *ts =
		container_of(bridge, struct nvt_ts_data, panel_bridge);

	if (!ts->connector || !ts->connector->state)
		ts->connector = get_bridge_connector(bridge);

	ts->is_panel_lp_mode = bridge_is_lp_mode(ts->connector);
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
