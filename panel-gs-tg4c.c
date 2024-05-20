/* SPDX-License-Identifier: MIT */
#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"
#include "trace/panel_trace.h"

#define TG4C_DDIC_ID_LEN 8
#define TG4C_DIMMING_FRAME 32
#define WIDTH_MM 64
#define HEIGHT_MM 145
#define PROJECT "TG4C"

static const struct gs_dsi_cmd tg4c_lp_cmds[] = {
	/* Disable the Black insertion in AoD */
	GS_DSI_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	GS_DSI_CMD(0xC0, 0x54),

	/* disable dimming */
	GS_DSI_CMD(0x53, 0x20),
	/* Settings AOD Hclk */
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	GS_DSI_CMD(0x6F, 0x0E),
	GS_DSI_CMD(0xF5, 0x20),
	/* enter AOD */
	GS_DSI_CMD(MIPI_DCS_ENTER_IDLE_MODE),
	/* DVDD Strong */
	GS_DSI_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	GS_DSI_CMD(0x6F, 0x03),
	GS_DSI_CMD(0xC5, 0x24, 0x29, 0x29),
};
static DEFINE_GS_CMDSET(tg4c_lp);

static const struct gs_dsi_cmd tg4c_lp_off_cmds[] = {
	GS_DSI_CMD(0x6F, 0x04),
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x00),
};

static const struct gs_dsi_cmd tg4c_lp_night_cmds[] = {
	/* 2 nit */
	GS_DSI_CMD(0x6F, 0x04),
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x03),
};

static const struct gs_dsi_cmd tg4c_lp_low_cmds[] = {
	/* 10 nit */
	GS_DSI_CMD(0x6F, 0x04),
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x07, 0xB2),
};

static const struct gs_dsi_cmd tg4c_lp_high_cmds[] = {
	/* 50 nit */
	GS_DSI_CMD(0x6F, 0x04),
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFE),
};

static const struct gs_binned_lp tg4c_binned_lp[] = {
	BINNED_LP_MODE("off", 0, tg4c_lp_off_cmds),
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 104, tg4c_lp_night_cmds, 0, 45),
	/* rising = 0, falling = 45 */
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 932, tg4c_lp_low_cmds, 0, 45),
	BINNED_LP_MODE_TIMING("high", 3574, tg4c_lp_high_cmds, 0, 45),
};

static const struct gs_dsi_cmd tg4c_off_cmds[] = {
	GS_DSI_DELAY_CMD(100, MIPI_DCS_SET_DISPLAY_OFF),
	GS_DSI_DELAY_CMD(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(tg4c_off);

static const struct gs_dsi_cmd tg4c_init_cmds[] = {
	/* CMD2, Page1 */
	GS_DSI_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	GS_DSI_CMD(0xC3, 0xDD),
	GS_DSI_CMD(0xE5, 0x00),
	/* CMD2, Page8 */
	GS_DSI_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x08),
	GS_DSI_CMD(0xE8, 0x13, 0x90, 0x80, 0x00),
	GS_DSI_CMD(0xF0, 0x55, 0xAA, 0x52, 0x00, 0x00),
	/* CMD3, Page0 */
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x80),
	GS_DSI_CMD(0x6F, 0x16),
	GS_DSI_CMD(0xF4, 0x02, 0x74),
	GS_DSI_CMD(0x6F, 0x31),
	GS_DSI_CMD(0xF8, 0x01, 0x74),
	GS_DSI_CMD(0x6F, 0x15),
	GS_DSI_CMD(0xF8, 0x01, 0x8D),
	GS_DSI_CMD(0x6F, 0x47),
	GS_DSI_CMD(0xF2, 0x30),
	GS_DSI_CMD(0x6F, 0x37),
	GS_DSI_CMD(0xF3, 0xCC, 0xCC),
	GS_DSI_CMD(0x6F, 0x3C),
	GS_DSI_CMD(0xF3, 0x0C),
	GS_DSI_CMD(0x6F, 0x3E),
	GS_DSI_CMD(0xF3, 0x00),
	GS_DSI_CMD(0x6F, 0x02),
	GS_DSI_CMD(0xF4, 0x00),
	GS_DSI_CMD(0x6F, 0x37),
	GS_DSI_CMD(0xF4, 0xF6, 0xF6),
	/* CMD3, Page1 */
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	GS_DSI_CMD(0x6F, 0x0E),
	GS_DSI_CMD(0xF5, 0x2A),
	GS_DSI_CMD(0x6F, 0x0D),
	GS_DSI_CMD(0xFB, 0x80),
	GS_DSI_CMD(0x6F, 0x0F),
	GS_DSI_CMD(0xF5, 0x22),
	GS_DSI_CMD(0x6F, 0x02),
	GS_DSI_CMD(0xF9, 0x84),
	GS_DSI_CMD(0x5F, 0x00, 0x00),
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	GS_DSI_CMD(0x6F, 0x0B),
	GS_DSI_CMD(0xFD, 0x04),
	/* CMD3, Page2 */
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x82),
	GS_DSI_CMD(0x6F, 0x09),
	GS_DSI_CMD(0xF2, 0xFF),
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x83),
	/* CMD3, Page4 */
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x84),
	GS_DSI_CMD(0x6F, 0x26),
	GS_DSI_CMD(0xF2, 0x00),
	GS_DSI_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x00),
	GS_DSI_CMD(0x35),
	GS_DSI_CMD(0x6F, 0x01),
	GS_DSI_CMD(0x35, 0x2D),
	GS_DSI_CMD(0x55, 0x00),
	/* BC Dimming OFF */
	GS_DSI_CMD(0x53, 0x20),
	GS_DSI_CMD(0x2A, 0x00, 0x00, 0x04, 0x37),
	GS_DSI_CMD(0x2B, 0x00, 0x00, 0x09, 0x77),
	/* Normal GMA */
	GS_DSI_CMD(0x26, 0x00),
	GS_DSI_CMD(0x51, 0x0E, 0x2C),
	GS_DSI_CMD(0x6F, 0x04),
	GS_DSI_CMD(0x51, 0x0F, 0xFE),
	GS_DSI_CMD(0x81, 0x01, 0x19),
	GS_DSI_CMD(0x88, 0x01, 0x02, 0x1C, 0x06, 0xDD, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMD(0x90, 0x03, 0x43),
	GS_DSI_CMD(0x91, 0x89, 0xA8, 0x00, 0x18, 0xC2, 0x00, 0x02, 0x0E, 0x02, 0x4C, 0x00, 0x07,
				0x04, 0x2D, 0x04, 0x3D, 0x10, 0xF0),
	/* 60Hz */
	GS_DSI_CMD(0x2F, 0x02),

	GS_DSI_DELAY_CMD(60, MIPI_DCS_EXIT_SLEEP_MODE)
};
static DEFINE_GS_CMDSET(tg4c_init);

static void tg4c_update_te2(struct gs_panel *ctx)
{
	struct gs_panel_te2_timing timing;
	struct device *dev = ctx->dev;
	u8 width = 0x2D; /* default width 45H */
	u32 rising = 0, falling;
	int ret;

	ret = gs_panel_get_current_mode_te2(ctx, &timing);
	if (!ret) {
		falling = timing.falling_edge;
		if (falling >= timing.rising_edge) {
			rising = timing.rising_edge;
			width = falling - rising;
		} else {
			dev_warn(dev, "invalid timing, use default setting\n");
		}
	} else if (ret == -EAGAIN) {
		dev_dbg(dev, "Panel is not ready, use default setting\n");
	} else {
		return;
	}

	dev_dbg(dev, "TE2 updated: rising= 0x%x, width= 0x%x", rising, width);

	GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_TEAR_SCANLINE, 0x00, rising);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_TEAR_ON, 0x00, width);
}

static void tg4c_update_irc(struct gs_panel *ctx, const enum gs_hbm_mode hbm_mode,
							const int vrefresh)
{
	struct device *dev = ctx->dev;
	const u16 level = gs_panel_get_brightness(ctx);

	if (!GS_IS_HBM_ON(hbm_mode)) {
		dev_dbg(dev, "hbm is off, skip update irc\n");
		return;
	}
	if (GS_IS_HBM_ON_IRC_OFF(hbm_mode)) {
		/* sync from bigSurf : to achieve the max brightness with IRC off which
		 * need to set dbv to 0xFFF */
		if (level == ctx->desc->brightness_desc->brt_capability->hbm.level.max)
			GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);

		/* IRC Off */
		GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x01, 0x00);
		if (vrefresh == 120)
			GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x00);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x02);
	} else {
		const u8 val1 = level >> 8;
		const u8 val2 = level & 0xff;

		/* IRC ON */
		GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x00, 0x00);
		if (vrefresh == 120)
			GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x00);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x02);

		/* sync from bigSurf : restore the dbv value while IRC ON */
		GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, val1, val2);
	}
	/* Empty command is for flush */
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x00);
}

static void tg4c_set_local_hbm_background_brightness(struct gs_panel *ctx, u16 br)
{
	u16 level;
	u8 val1, val2;
	struct device *dev = ctx->dev;

	if (GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode) &&
		br == ctx->desc->brightness_desc->brt_capability->hbm.level.max)
		br = 0x0FFF;

	level = br * 4;
	val1 = level >> 8;
	val2 = level & 0xff;

	/* set LHBM background brightness */
	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x4C);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xDF, val1, val2, val1, val2, val1, val2);
}


#define LHBM_GAMMASET2 2774
#define LHBM_GAMMASET3 2186

static void tg4c_set_local_hbm_gamma(struct gs_panel *ctx, u16 br)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x07);
	if (GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode))
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x00);
	else if (GS_IS_HBM_ON(ctx->hbm_mode))
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x01);
	else if (br > LHBM_GAMMASET2)
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x02);
	else if (br > LHBM_GAMMASET3)
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x03);
	else
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x04);
}

static void tg4c_set_local_hbm_mode(struct gs_panel *ctx, bool local_hbm_en)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (local_hbm_en) {
		u16 level = gs_panel_get_brightness(ctx);

		if (GS_IS_HBM_ON(ctx->hbm_mode)) {
			tg4c_update_irc(ctx, ctx->hbm_mode, vrefresh);
		} else if (vrefresh == 120) {
			GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
			GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x04);
			GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x75);
		} else {
			dev_warn(dev, "enable LHBM at unexpected state (HBM: %d, vrefresh: %dhz)\n",
					 ctx->hbm_mode, vrefresh);
		}
		tg4c_set_local_hbm_background_brightness(ctx, level);
		tg4c_set_local_hbm_gamma(ctx, level);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x87, 0x05);
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0x87, 0x00);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x2F, 0x00);
	}
}

static void tg4c_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	int vrefresh = drm_mode_vrefresh(&pmode->mode);
	struct device *dev = ctx->dev;

	if (vrefresh != 60 && vrefresh != 120) {
		dev_warn(dev, "%s: invalid refresh rate %dhz\n", __func__, vrefresh);
		return;
	}

	if (vrefresh != 120 && (!gs_is_local_hbm_disabled(ctx))) {
		dev_err(dev, "%s: switch to %dhz will fail when LHBM is on, disable LHBM\n",
				__func__, vrefresh);
		tg4c_set_local_hbm_mode(ctx, false);
		ctx->lhbm.effective_state = GLOCAL_HBM_DISABLED;
	}

	if (!GS_IS_HBM_ON(ctx->hbm_mode)) {
		if (vrefresh == 120)
			GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x2F, 0x00);
		else
			GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x2F, 0x02);
	} else {
		tg4c_update_irc(ctx, ctx->hbm_mode, vrefresh);
	}

	dev_dbg(dev, "%s: change to %dhz\n", __func__, vrefresh);
}

static void tg4c_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip dimming update\n");
		return;
	}

	ctx->dimming_on = dimming_on;
	GS_DCS_WRITE_CMD(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, ctx->dimming_on ? 0x28 : 0x20);
	dev_dbg(dev, "%s dimming_on=%d\n", __func__, dimming_on);
}

#define EXIT_IDLE_DELAY_FRAME 2
#define EXIT_IDLE_DELAY_DELTA 100
static void tg4c_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!gs_is_panel_active(ctx))
		return;
	/* exit AOD */
	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x54);
	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x03);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x00, 0x24, 0x24);
	GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_EXIT_IDLE_MODE);
	GS_DCS_BUF_ADD_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x0E);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xF5, 0x2B);
	tg4c_change_frequency(ctx, pmode);

	/* Delay setting dimming on AOD exit until brightness has stabilized. */
	ctx->timestamps.idle_exit_dimming_delay_ts = ktime_add_us(
		ktime_get(), 100 + GS_VREFRESH_TO_PERIOD_USEC(vrefresh) * EXIT_IDLE_DELAY_FRAME);

	dev_info(dev, "exit LP mode\n");
}

static void tg4c_dimming_frame_setting(struct gs_panel *ctx, u8 dimming_frame)
{
	struct device *dev = ctx->dev;

	/* Fixed time 1 frame */
	if (!dimming_frame)
		dimming_frame = 0x01;

	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0xB2, 0x19);
	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x05);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xB2, dimming_frame, dimming_frame);
}

static int tg4c_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_dbg(dev, "%s\n", __func__);

	/* toggle reset gpio */
	gs_panel_reset_helper(ctx);

	/* toggle reset gpio */
	gs_panel_send_cmdset(ctx, &tg4c_init_cmdset);

	/* frequency */
	tg4c_change_frequency(ctx, pmode);

	/* dimming frame */
	tg4c_dimming_frame_setting(ctx, TG4C_DIMMING_FRAME);
	ctx->timestamps.idle_exit_dimming_delay_ts = 0;

	if (pmode->gs_mode.is_lp_mode)
		gs_panel_set_lp_mode_helper(ctx, pmode);

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int tg4c_atomic_check(struct gs_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->gs_connector->base;
	struct drm_connector_state *new_conn_state = drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;

	if (!ctx->current_mode || drm_mode_vrefresh(&ctx->current_mode->mode) == 120 ||
		!new_conn_state || !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;
	if (!drm_atomic_crtc_effectively_active(old_crtc_state) ||
		(ctx->current_mode->gs_mode.is_lp_mode &&
			drm_mode_vrefresh(&new_crtc_state->mode) == 60)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;
		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			ctx->gs_connector->needs_commit = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n", mode->name,
					!drm_atomic_crtc_effectively_active(old_crtc_state) ? "resume" : "lp exit");
		}
	} else if (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock) {
		/* b/282222114: clock hacked in last commit due to resume or lp exit, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		ctx->gs_connector->needs_commit = false;
		dev_dbg(ctx->dev, "restore mode (%s) clock after resume or lp exit\n",
				new_crtc_state->mode.name);
	}
	return 0;
}

#define MAX_BR_HBM_IRC_OFF 4095
static int tg4c_set_brightness(struct gs_panel *ctx, u16 br)
{
	struct device *dev = ctx->dev;
	u16 brightness;

	if (ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode) &&
		br == ctx->desc->brightness_desc->brt_capability->hbm.level.max) {
		br = MAX_BR_HBM_IRC_OFF;
		dev_dbg(dev, "apply max DBV when reach hbm max with irc off\n");
	}

	if (!gs_is_local_hbm_disabled(ctx))
		tg4c_set_local_hbm_background_brightness(ctx, br);

	if (ctx->timestamps.idle_exit_dimming_delay_ts &&
		(ktime_sub(ctx->timestamps.idle_exit_dimming_delay_ts, ktime_get()) <= 0)) {
		GS_DCS_WRITE_CMD(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY,
						ctx->dimming_on ? 0x28 : 0x20);
		ctx->timestamps.idle_exit_dimming_delay_ts = 0;
	}

	brightness = swab16(br);
	return gs_dcs_set_brightness(ctx, brightness);
}

static void tg4c_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode hbm_mode)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (ctx->hbm_mode == hbm_mode)
		return;

	tg4c_update_irc(ctx, hbm_mode, vrefresh);

	ctx->hbm_mode = hbm_mode;
	dev_info(dev, "hbm_on=%d hbm_ircoff=%d\n", GS_IS_HBM_ON(ctx->hbm_mode),
			 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void tg4c_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	tg4c_change_frequency(ctx, pmode);
}

static void tg4c_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;

	gs_panel_get_panel_rev(ctx, main | sub);
}

static int tg4c_read_id(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	char buf[TG4C_DDIC_ID_LEN] = {0};
	int ret;

	GS_DCS_WRITE_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	ret = mipi_dsi_dcs_read(dsi, 0xF2, buf, TG4C_DDIC_ID_LEN);
	if (ret != TG4C_DDIC_ID_LEN) {
		dev_warn(dev, "Unable to read DDIC id (%d)\n", ret);
		goto done;
	} else {
		ret = 0;
	}

	bin2hex(ctx->panel_id, buf, TG4C_DDIC_ID_LEN);
done:
	GS_DCS_WRITE_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x00);
	return ret;
}

static const struct gs_display_underrun_param underrun_param = {
	.te_idle_us = 2510,
	.te_var = 1,
};

/* Truncate 8-bit signed value to 6-bit signed value */
#define TO_6BIT_SIGNED(v) ((v) & 0x3F)

static const struct drm_dsc_config tg4c_dsc_cfg = {
	.first_line_bpg_offset = 12,
	.rc_range_params = {
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{3, 10, TO_6BIT_SIGNED(-10)},
		{5, 10, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{9, 12, TO_6BIT_SIGNED(-12)},
		{12, 13, TO_6BIT_SIGNED(-12)},
	},
	/* Used DSC v1.2 */
	.dsc_version_major = 1,
	.dsc_version_minor = 2,
	.slice_count = 2,
	.slice_height = 24,
};
#define TG4C_DSC {\
		.enabled = true, \
		.dsc_count = 2, \
		.cfg = &tg4c_dsc_cfg, \
}
static const struct gs_panel_mode_array tg4c_modes = {
	.num_modes = 2,
	.modes = {
		{
			.mode = {
				.name = "1080x2424@60:60",
				DRM_MODE_TIMING(60, 1080, 32, 12, 16, 2424, 12, 4, 15),
				/* aligned to bootloader setting */
				.type = DRM_MODE_TYPE_PREFERRED,
				.width_mm = WIDTH_MM,
				.height_mm = HEIGHT_MM,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = 8450,
				.bpc = 8,
				.dsc = TG4C_DSC,
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = 0,
				.falling_edge = 45,
			},
		},
		{
			.mode = {
				.name = "1080x2424@120:120",
				DRM_MODE_TIMING(120, 1080, 32, 12, 16, 2424, 12, 4, 15),
				.width_mm = WIDTH_MM,
				.height_mm = HEIGHT_MM,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = 276,
				.bpc = 8,
				.dsc = TG4C_DSC,
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = 0,
				.falling_edge = 45,
			},
		},
	},/* modes */
};

static const struct gs_panel_mode_array tg4c_lp_modes = {
	.num_modes = 1,
	.modes = {
		{
			.mode = {
				.name = "1080x2424@30:30",
				DRM_MODE_TIMING(30, 1080, 32, 12, 16, 2424, 12, 4, 15),
				.type = DRM_MODE_TYPE_DRIVER,
				.width_mm = WIDTH_MM,
				.height_mm = HEIGHT_MM,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.bpc = 8,
				.dsc = TG4C_DSC,
				.underrun_param = &underrun_param,
				.is_lp_mode = true,
			},
		},
	},/* modes */
};

static void tg4c_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	gs_panel_debugfs_create_cmdset(csroot, &tg4c_init_cmdset, "init");
	dput(csroot);
panel_out:
	dput(panel_root);
}

static void tg4c_panel_init(struct gs_panel *ctx)
{
	tg4c_dimming_frame_setting(ctx, TG4C_DIMMING_FRAME);
}

static int tg4c_panel_probe(struct mipi_dsi_device *dsi)
{
	struct gs_panel *panel;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	return gs_dsi_panel_common_init(dsi, panel);
}

static const struct drm_panel_funcs tg4c_drm_funcs = {
	.disable = gs_panel_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = tg4c_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = tg4c_debugfs_init,
};

static int tg4c_panel_config(struct gs_panel *ctx);

static const struct gs_panel_funcs tg4c_gs_funcs = {
	.set_brightness = tg4c_set_brightness,
	.set_lp_mode = gs_panel_set_lp_mode_helper,
	.set_nolp_mode = tg4c_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_local_hbm_mode = tg4c_set_local_hbm_mode,
	.set_hbm_mode = tg4c_set_hbm_mode,
	.set_dimming = tg4c_set_dimming,
	.is_mode_seamless = gs_panel_is_mode_seamless_helper,
	.mode_set = tg4c_mode_set,
	.panel_init = tg4c_panel_init,
	.panel_config = tg4c_panel_config,
	.get_panel_rev = tg4c_get_panel_rev,
	.get_te2_edges = gs_panel_get_te2_edges_helper,
	.set_te2_edges = gs_panel_set_te2_edges_helper,
	.update_te2 = tg4c_update_te2,
	.read_id = tg4c_read_id,
	.atomic_check = tg4c_atomic_check,
};

static const struct gs_brightness_configuration tg4c_btr_configs[] = {
	{
		.panel_rev = PANEL_REV_LATEST,
		.default_brightness = 1816,
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 1200,
				},
				.level = {
					.min = 1,
					.max = 3628,
				},
				.percentage = {
					.min = 0,
					.max = 67,
				},
			},
			.hbm = {
				.nits = {
					.min = 1200,
					.max = 1800,
				},
				.level = {
					.min = 3629,
					.max = 3939,
				},
				.percentage = {
					.min = 67,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc tg4c_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
};

static struct gs_panel_reg_ctrl_desc tg4c_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
};

static struct gs_panel_lhbm_desc tg4c_lhbm_desc = {
	.lhbm_on_delay_frames = 2,
};

static struct gs_panel_desc gs_tg4c = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &tg4c_brightness_desc,
	.modes = &tg4c_modes,
	.off_cmdset = &tg4c_off_cmdset,
	.lp_modes = &tg4c_lp_modes,
	.lp_cmdset = &tg4c_lp_cmdset,
	.binned_lp = tg4c_binned_lp,
	.num_binned_lp = ARRAY_SIZE(tg4c_binned_lp),
	.lhbm_desc = &tg4c_lhbm_desc,
	.has_off_binned_lp_entry = true,
	.reg_ctrl_desc = &tg4c_reg_ctrl_desc,
	.panel_func = &tg4c_drm_funcs,
	.gs_panel_func = &tg4c_gs_funcs,
	.reset_timing_ms = {1, 1, 20},
	.refresh_on_lp = true,
};

static int tg4c_panel_config(struct gs_panel *ctx)
{
	gs_panel_model_init(ctx, PROJECT, 0);
	return gs_panel_update_brightness_desc(&tg4c_brightness_desc, tg4c_btr_configs,
						ARRAY_SIZE(tg4c_btr_configs), ctx->panel_rev);
}

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-tg4c", .data = &gs_tg4c },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = tg4c_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-tg4c",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Shin-Yu Wang <shinyuw@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google tg4c panel driver");
MODULE_LICENSE("Dual MIT/GPL");
