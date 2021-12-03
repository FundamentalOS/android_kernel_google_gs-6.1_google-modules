/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#ifndef _NT36XXX_GOOG_H_
#define _NT36XXX_GOOG_H_

#if defined(CONFIG_SOC_GOOGLE) && defined(CONFIG_TOUCHSCREEN_HEATMAP)
#define GOOG_HEATMAP 1
#endif

#if defined(CONFIG_SOC_GOOGLE) && defined(CONFIG_TOUCHSCREEN_OFFLOAD)
#define GOOG_OFFLOAD 1
#endif

#if defined(GOOG_HEATMAP)
#include <heatmap.h>
#endif

#if defined(GOOG_OFFLOAD)
#include <touch_offload.h>
#endif

/*
 * Structures and functions declarations.
 */
struct nvt_ts_data; /* forward declaration */

struct nvt_ts_coordinate {
	u8 status;
	u16 x;
	u16 y;
	u8 pressure;
	u8 major;

	/* for debug purpose. */
	u16 x_pressed;	/* x coord on first down timing. */
	u16 y_pressed;	/* y coord on first down timing. */
	ktime_t ktime_pressed;
	ktime_t ktime_released;
};

#if defined(GOOG_OFFLOAD)
inline bool goog_offload_is_off(struct nvt_ts_data *ts);
inline void goog_input_lock(struct nvt_ts_data *ts);
inline void goog_input_unlock(struct nvt_ts_data *ts);
inline void goog_offload_update_coords(struct nvt_ts_data *ts);
void goog_offload_push_frame(struct nvt_ts_data *ts);
int32_t goog_offload_remove(struct nvt_ts_data *ts);
int32_t goog_offload_probe(struct nvt_ts_data *ts);
#else
static inline bool goog_offload_is_off(struct nvt_ts_data *ts)
{
	return 1;
}
static inline void goog_input_lock(struct nvt_ts_data *ts)
{
}
static inline void goog_input_unlock(struct nvt_ts_data *ts)
{
}
static inline void goog_offload_update_coords(struct nvt_ts_data *ts)
{
}
static inline void goog_offload_push_frame(struct nvt_ts_data *ts)
{
}
static inline int32_t goog_offload_remove(struct nvt_ts_data *ts)
{
	return 0;
}
static inline int32_t goog_offload_probe(struct nvt_ts_data *ts)
{
	return 0;
}
#endif

#if defined(GOOG_HEATMAP)
inline void goog_heatmap_read(struct nvt_ts_data *ts);
inline void goog_heatmap_remove(struct nvt_ts_data *ts);
int32_t goog_heatmap_probe(struct nvt_ts_data *ts);
#else
static inline void goog_heatmap_read(struct nvt_ts_data *ts)
{
}
static inline void goog_heatmap_remove(struct nvt_ts_data *ts)
{
}
static inline int32_t goog_heatmap_probe(struct nvt_ts_data *ts)
{
	return 0;
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
inline void goog_update_press_coord(struct nvt_ts_data *ts, uint32_t slot,
		uint32_t x, uint32_t y, uint32_t major, uint32_t pressure);
inline void goog_update_release_coord(struct nvt_ts_data *ts, uint32_t slot);
void nvt_ts_aggregate_bus_state(struct nvt_ts_data *ts);
int nvt_ts_set_bus_ref(struct nvt_ts_data *ts, u32 ref, bool enable);
ssize_t goog_offload_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t goog_offload_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
ssize_t goog_v4l2_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t goog_v4l2_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
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
static inline void goog_update_press_coord(struct nvt_ts_data *ts, uint32_t slot,
		uint32_t x, uint32_t y, uint32_t major, uint32_t pressure)
{
}
static inline void goog_update_release_coord(struct nvt_ts_data *ts, uint32_t slot)
{
}
static inline void nvt_ts_aggregate_bus_state(struct nvt_ts_data *ts)
{
}
static inline int nvt_ts_set_bus_ref(struct nvt_ts_data *ts, u32 ref, bool enable)
{
	return 0;
}
static inline ssize_t goog_offload_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t goog_offload_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}
static inline ssize_t goog_v4l2_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t goog_v4l2_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
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
