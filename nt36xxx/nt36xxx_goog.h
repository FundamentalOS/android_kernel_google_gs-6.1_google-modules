/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#ifndef _NT36XXX_GOOG_H_
#define _NT36XXX_GOOG_H_

struct nvt_ts_data; /* forward declaration */
#ifdef NVT_TS_PANEL_BRIDGE
int register_panel_bridge(struct nvt_ts_data *ts);
void unregister_panel_bridge(struct drm_bridge *bridge);
#else
#define register_panel_bridge(ts) do { 0; } while (0)
#define unregister_panel_bridge(bridge) do {} while (0)
#endif

#endif /* _NT36XXX_GOOG_H_ */
