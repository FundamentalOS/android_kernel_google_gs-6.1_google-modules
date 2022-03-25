/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#ifndef _NT36XXX_GOOG_H_
#define _NT36XXX_GOOG_H_

#ifdef GOOG_TOUCH_INTERFACE
#include <goog_touch_interface.h>
#else
#include <goog_touch_interface_nop.h>
#endif

/*
 * Structures and functions declarations.
 */
struct nvt_ts_data; /* forward declaration */

void nvt_heatmap_decode(
		const uint8_t *in, const uint32_t in_sz,
		const uint8_t *out, const uint32_t out_sz);

#ifdef GOOG_TOUCH_INTERFACE
int nvt_get_channel_data(void *private_data,
		u32 type, u8 **buffer, u32 *size);
int nvt_callback(void *private_data,
		u32 cmd, u32 sub_cmd, u8 **buffer, u32 *size);
#else
static inline int nvt_get_channel_data(void *private_data,
		u32 type, u8 **buffer, u32 *size)
{
	return -ENODATA;
}
static inline int nvt_callback(void *private_data,
		u32 cmd, u32 sub_cmd, u8 **buffer, u32 *size)
{
	return -ENODATA;
}
#endif

#if defined(CONFIG_SOC_GOOGLE) && defined(NVT_TS_PANEL_BRIDGE)
int register_panel_bridge(struct nvt_ts_data *ts);
void unregister_panel_bridge(struct drm_bridge *bridge);
#else
static inline int register_panel_bridge(struct nvt_ts_data *ts)
{
	return 0;
}
static inline void unregister_panel_bridge(struct drm_bridge *bridge)
{
}
#endif /* defined(CONFIG_SOC_GOOGLE) && defined(NVT_TS_PANEL_BRIDGE) */

#if defined(CONFIG_SOC_GOOGLE)
void nvt_ts_aggregate_bus_state(struct nvt_ts_data *ts);
int nvt_ts_set_bus_ref(struct nvt_ts_data *ts, u32 ref, bool enable);
ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
int nvt_ts_pm_suspend(struct device *dev);
int nvt_ts_pm_resume(struct device *dev);
#else
static inline void nvt_ts_aggregate_bus_state(struct nvt_ts_data *ts)
{
}
static inline int nvt_ts_set_bus_ref(struct nvt_ts_data *ts, u32 ref, bool enable)
{
	return 0;
}
static inline ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}
static inline ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}
static inline int nvt_ts_pm_suspend(struct device *dev)
{
	return 0;
}
static inline int nvt_ts_pm_resume(struct device *dev)
{
	return 0;
}
#endif /* defined(CONFIG_SOC_GOOGLE) */

/*
 * Enums, and constants.
 */

#define NVT_SUSPEND_WORK_MS_DELAY	0
#define NVT_SUSPEND_POST_MS_DELAY	80
#define NVT_RESUME_WORK_MS_DELAY	0
#define NVT_FORCE_ACTIVE_MS_DELAY	500
#define NVT_PINCTRL_US_DELAY		(10*1000)

#define NVT_V4L2_DEFAULT_WIDTH		32
#define NVT_V4L2_DEFAULT_HEIGHT		50

enum {
	NVT_BUS_REF_SCREEN_ON		= 0x0001,
	NVT_BUS_REF_IRQ			= 0x0002,
	NVT_BUS_REF_FW_UPDATE		= 0x0004,
	NVT_BUS_REF_FORCE_ACTIVE	= 0x0008,
	NVT_BUS_REF_BUGREPORT		= 0x8000
};

#endif /* _NT36XXX_GOOG_H_ */
