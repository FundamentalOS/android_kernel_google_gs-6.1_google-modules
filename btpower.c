// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

/*
 * Bluetooth Power Switch Module
 * controls power to external Bluetooth device
 * with interface to power management device
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/btpower.h>
#include <linux/of_device.h>
#include <soc/qcom/cmd-db.h>

#if IS_ENABLED(CONFIG_BT_SLIM_QCA6390) || \
	IS_ENABLED(CONFIG_BT_SLIM_QCA6490) || \
	IS_ENABLED(CONFIG_BTFM_SLIM_WCN3990) || \
	IS_ENABLED(CONFIG_BTFM_SLIM_WCN7850)
#include "btfm_slim.h"
#endif
#include <linux/fs.h>

#define PWR_SRC_NOT_AVAILABLE -2
#define DEFAULT_INVALID_VALUE -1
#define PWR_SRC_INIT_STATE_IDX 0
#define BTPOWER_MBOX_MSG_MAX_LEN 64
#define BTPOWER_MBOX_TIMEOUT_MS 1000
#define XO_CLK_RETRY_COUNT_MAX 5

/**
 * enum btpower_vreg_param: Voltage regulator TCS param
 * @BTPOWER_VREG_VOLTAGE: Provides voltage level to be configured in TCS
 * @BTPOWER_VREG_MODE: Regulator mode
 * @BTPOWER_VREG_ENABLE: Set Voltage regulator enable config in TCS
 * @BTPOWER_VREG_PARAM_MAX: vreg param boundary
 */
enum btpower_vreg_param {
	BTPOWER_VREG_VOLTAGE = 0,
	BTPOWER_VREG_MODE,
	BTPOWER_VREG_ENABLE,
	BTPOWER_VREG_PARAM_MAX,
};
static const char vreg_param_str[BTPOWER_VREG_PARAM_MAX] = {'v', 'm', 'e'};

/**
 * enum btpower_tcs_seq: TCS sequence ID for trigger
 * @BTPOWER_TCS_UP_SEQ: TCS Sequence based on up trigger / Wake TCS
 * @BTPOWER_TCS_DOWN_SEQ: TCS Sequence based on down trigger / Sleep TCS
 * @BTPOWER_TCS_ALL_SEQ: Update for both up and down triggers
 * @BTPOWER_TCS_SEQ_MAX: TCS sequence ID boundary
 */
enum btpower_tcs_seq {
	BTPOWER_TCS_UP_SEQ = 0,
	BTPOWER_TCS_DOWN_SEQ,
	BTPOWER_TCS_ALL_SEQ,
	BTPOWER_TCS_SEQ_MAX,
};
static const char *const tcs_seq_str[BTPOWER_TCS_SEQ_MAX] =
	{"upval", "dwnval", "enable"};

enum power_src_pos {
	BT_RESET_GPIO = PWR_SRC_INIT_STATE_IDX,
	BT_SW_CTRL_GPIO,
	BT_VDD_AON_LDO,
	BT_VDD_DIG_LDO,
	BT_VDD_RFA1_LDO,
	BT_VDD_RFA2_LDO,
	BT_VDD_ASD_LDO,
	BT_VDD_XTAL_LDO,
	BT_VDD_PA_LDO,
	BT_VDD_CORE_LDO,
	BT_VDD_IO_LDO,
	BT_VDD_LDO,
	BT_VDD_RFA_0p8,
	BT_VDD_RFACMN,
	// these indexes GPIOs/regs value are fetched during crash.
	BT_RESET_GPIO_CURRENT,
	BT_SW_CTRL_GPIO_CURRENT,
	BT_VDD_AON_LDO_CURRENT,
	BT_VDD_DIG_LDO_CURRENT,
	BT_VDD_RFA1_LDO_CURRENT,
	BT_VDD_RFA2_LDO_CURRENT,
	BT_VDD_ASD_LDO_CURRENT,
	BT_VDD_XTAL_LDO_CURRENT,
	BT_VDD_PA_LDO_CURRENT,
	BT_VDD_CORE_LDO_CURRENT,
	BT_VDD_IO_LDO_CURRENT,
	BT_VDD_LDO_CURRENT,
	BT_VDD_RFA_0p8_CURRENT,
	BT_VDD_RFACMN_CURRENT
};

// Regulator structure for QCA6174/QCA9377/QCA9379 BT SoC series
static struct bt_power_vreg_data bt_vregs_info_qca61x4_937x[] = {
	{NULL, "qcom,bt-vdd-aon", 928000, 928000, 0, false, false,
		{BT_VDD_AON_LDO, BT_VDD_AON_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-io", 1710000, 3460000, 0, false, false,
		{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-core", 3135000, 3465000, 0, false, false,
		{BT_VDD_CORE_LDO, BT_VDD_CORE_LDO_CURRENT}},
};

// Regulator structure for QCA6390 and QCA6490 BT SoC series
static struct bt_power_vreg_data bt_vregs_info_qca6x9x[] = {
	{NULL, "qcom,bt-vdd-io",      1800000, 1800000, 0, false, true,
		{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-aon",     966000,  966000,  0, false, true,
		{BT_VDD_AON_LDO, BT_VDD_AON_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfacmn",  950000,  950000,  0, false, true,
		{BT_VDD_RFACMN, BT_VDD_RFACMN_CURRENT}},
	/* BT_CX_MX */
	{NULL, "qcom,bt-vdd-dig",      966000,  966000,  0, false, true,
		{BT_VDD_DIG_LDO, BT_VDD_DIG_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa-0p8",  950000,  952000,  0, false, true,
		{BT_VDD_RFA_0p8, BT_VDD_RFA_0p8_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa1",     1900000, 1900000, 0, false, true,
		{BT_VDD_RFA1_LDO, BT_VDD_RFA1_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa2",     1900000, 1900000, 0, false, true,
		{BT_VDD_RFA2_LDO, BT_VDD_RFA2_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-asd",      2800000, 2800000, 0, false, true,
		{BT_VDD_ASD_LDO, BT_VDD_ASD_LDO_CURRENT}},
};


// Regulator structure for WCN7850 BT SoC series
static struct bt_power_vreg_data bt_vregs_info_wcn7850[] = {
	{NULL, "qcom,bt-vdd-io",      1800000, 1800000, 0, false, true,
		{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-aon",     950000,  950000,  0, false, true,
		{BT_VDD_AON_LDO, BT_VDD_AON_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfacmn",  950000,  950000,  0, false, true,
		{BT_VDD_RFACMN, BT_VDD_RFACMN_CURRENT}},
	/* BT_CX_MX */
	{NULL, "qcom,bt-vdd-dig",      950000,  950000,  0, false, true,
		{BT_VDD_DIG_LDO, BT_VDD_DIG_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa-0p8",  950000,  952000,  0, false, true,
		{BT_VDD_RFA_0p8, BT_VDD_RFA_0p8_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa1",     1900000, 1900000, 0, false, true,
		{BT_VDD_RFA1_LDO, BT_VDD_RFA1_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa2",     1350000, 1350000, 0, false, true,
		{BT_VDD_RFA2_LDO, BT_VDD_RFA2_LDO_CURRENT}},
};

// Regulator structure for WCN399x BT SoC series
static const struct bt_power bt_vreg_info_wcn399x = {
	.compatible = "qcom,wcn3990",
	.vregs = (struct bt_power_vreg_data []) {
		{NULL, "qcom,bt-vdd-io",   1700000, 1900000, 0, false, false,
			{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-core", 1304000, 1304000, 0, false, false,
			{BT_VDD_CORE_LDO, BT_VDD_CORE_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-pa",   3000000, 3312000, 0, false, false,
			{BT_VDD_PA_LDO, BT_VDD_PA_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-xtal", 1700000, 1900000, 0, false, false,
			{BT_VDD_XTAL_LDO, BT_VDD_XTAL_LDO_CURRENT}},
	},
	.num_vregs = 4,
};

static const struct bt_power bt_vreg_info_qca6174 = {
	.compatible = "qcom,qca6174",
	.vregs = bt_vregs_info_qca61x4_937x,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_qca61x4_937x),
};

static const struct bt_power bt_vreg_info_qca6390 = {
	.compatible = "qcom,qca6390",
	.vregs = bt_vregs_info_qca6x9x,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_qca6x9x),
};

static const struct bt_power bt_vreg_info_qca6490 = {
	.compatible = "qcom,qca6490",
	.vregs = bt_vregs_info_qca6x9x,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_qca6x9x),
};

static const struct bt_power bt_vreg_info_wcn7850 = {
	.compatible = "qcom,wcn7850",
	.vregs = bt_vregs_info_wcn7850,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_wcn7850),
};

static const struct of_device_id bt_power_match_table[] = {
	{	.compatible = "qcom,qca6174", .data = &bt_vreg_info_qca6174},
	{	.compatible = "qcom,wcn3990", .data = &bt_vreg_info_wcn399x},
	{	.compatible = "qcom,qca6390", .data = &bt_vreg_info_qca6390},
	{	.compatible = "qcom,qca6490", .data = &bt_vreg_info_qca6490},
	{	.compatible = "qcom,wcn7850", .data = &bt_vreg_info_wcn7850},
	{},
};

static int bt_power_vreg_set(struct btpower_platform_data *drvdata,
			     enum bt_power_modes mode);
static int btpower_enable_ipa_vreg(struct btpower_platform_data *drvdata);

static int bt_vreg_enable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	pr_debug("%s: vreg_en for : %s\n", __func__, vreg->name);

	if (vreg->is_enabled)
		return rc;

	if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
		rc = regulator_set_voltage(vreg->reg, vreg->min_vol,
					vreg->max_vol);
		if (rc < 0) {
			pr_err("%s: regulator_enable(%s) failed. rc=%d\n",
				__func__, vreg->name, rc);
			goto out;
		}
	}

	if (vreg->load_curr >= 0) {
		rc = regulator_set_load(vreg->reg, vreg->load_curr);
		if (rc < 0) {
			pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
			goto out;
		}
	}

	rc = regulator_enable(vreg->reg);
	if (rc < 0) {
		pr_err("%s: regulator_enable(%s) failed. rc=%d\n",
			__func__, vreg->name, rc);
		goto out;
	}
	vreg->is_enabled = true;

out:
	return rc;
}

static int bt_vreg_enable_retention(const struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	if (!vreg)
		return rc;

	pr_debug("%s: enable_retention for : %s\n", __func__, vreg->name);

	if (!vreg->is_enabled || !vreg->is_retention_supp)
		return rc;

	if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
		/* Set the min voltage to 0 */
		rc = regulator_set_voltage(vreg->reg, 0, vreg->max_vol);
		if (rc < 0) {
			pr_err("%s: regulator_set_voltage(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
			goto out;
		}
	}
	if (vreg->load_curr >= 0) {
		rc = regulator_set_load(vreg->reg, 0);
		if (rc < 0) {
			pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
		}
	}

out:
	return rc;
}

static int bt_vreg_disable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	if (!vreg)
		return rc;

	pr_debug("%s: vreg_off for : %s\n", __func__, vreg->name);

	if (!vreg->is_enabled)
		return rc;

	rc = regulator_disable(vreg->reg);
	if (rc < 0) {
		pr_err("%s: regulator_disable(%s) failed. rc=%d\n",
			__func__, vreg->name, rc);
		goto out;
	}
	vreg->is_enabled = false;

	if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
		/* Set the min voltage to 0 */
		rc = regulator_set_voltage(vreg->reg, 0, vreg->max_vol);
		if (rc < 0) {
			pr_err("%s: regulator_set_voltage(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
			goto out;
		}
	}
	if (vreg->load_curr >= 0) {
		rc = regulator_set_load(vreg->reg, 0);
		if (rc < 0) {
			pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
		}
	}

out:
	return rc;
}

static int bt_clk_enable(struct bt_power_clk_data *clk)
{
	int rc = 0;

	pr_debug("%s: %s\n", __func__, clk->name);

	/* Get the clock handle for vreg */
	if (!clk->clk || clk->is_enabled) {
		pr_err("%s: error - node: %p, clk->is_enabled:%d\n",
			__func__, clk->clk, clk->is_enabled);
		return -EINVAL;
	}

	rc = clk_prepare_enable(clk->clk);
	if (rc) {
		pr_err("%s: failed to enable %s, rc(%d)\n",
			__func__, clk->name, rc);
		return rc;
	}

	clk->is_enabled = true;
	return rc;
}

static int bt_clk_disable(struct bt_power_clk_data *clk)
{
	pr_debug("%s: %s\n", __func__, clk->name);

	/* Get the clock handle for vreg */
	if (!clk->clk || !clk->is_enabled) {
		pr_err("%s: error - node: %p, clk->is_enabled:%d\n",
			__func__, clk->clk, clk->is_enabled);
		return -EINVAL;
	}
	clk_disable_unprepare(clk->clk);

	clk->is_enabled = false;
	return 0;
}

static void btpower_set_xo_clk_gpio_state(struct btpower_platform_data *drvdata,
					  bool enable)
{
	int xo_clk_gpio = drvdata->xo_gpio_clk;
	int retry = 0;
	int rc = 0;

	if (xo_clk_gpio < 0)
		return;

retry_gpio_req:
	rc = gpio_request(xo_clk_gpio, "bt_xo_clk_gpio");
	if (rc) {
		if (retry++ < XO_CLK_RETRY_COUNT_MAX) {
			/* wait for ~(10 - 20) ms */
			usleep_range(10000, 20000);
			goto retry_gpio_req;
		}
	}

	if (rc) {
		pr_err("%s: unable to request XO clk gpio %d (%d)\n",
			__func__, xo_clk_gpio, rc);
		return;
	}

	if (enable) {
		gpio_direction_output(xo_clk_gpio, 1);
		/*XO CLK must be asserted for some time before BT_EN */
		usleep_range(100, 200);
	} else {
		/* Assert XO CLK ~(2-5)ms before off for valid latch in HW */
		usleep_range(4000, 6000);
		gpio_direction_output(xo_clk_gpio, 0);
	}

	pr_debug("%s: gpio(%d) success\n", __func__, xo_clk_gpio);

	gpio_free(xo_clk_gpio);
}

static int bt_configure_gpios(struct btpower_platform_data *drvdata, bool on)
{
	int rc = 0;
	int bt_reset_gpio = drvdata->bt_gpio_sys_rst;
	int wl_reset_gpio = drvdata->wl_gpio_sys_rst;
	int bt_sw_ctrl_gpio = drvdata->bt_gpio_sw_ctrl;
	int bt_debug_gpio = drvdata->bt_gpio_debug;
	int assert_dbg_gpio = 0;

	pr_info("%s: BT_EN GPIO(%d) value(%d) enabling: %s\n", __func__,
		bt_reset_gpio, gpio_get_value(bt_reset_gpio),
		(on ? "True" : "False"));

	if (!on) {
		gpio_set_value(bt_reset_gpio, 0);
		pr_debug("%s: BT-OFF bt-reset-gpio(%d)\n", __func__,
			bt_reset_gpio);
		msleep(100);
		if (bt_sw_ctrl_gpio >= 0) {
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
				gpio_get_value(bt_sw_ctrl_gpio);
			pr_debug("%s: BT-OFF bt-sw-ctrl-gpio(%d) value(%d)\n",
				__func__, bt_sw_ctrl_gpio,
				drvdata->bt_power_src_status[BT_SW_CTRL_GPIO]);
		}
		return 0;
	}

	rc = gpio_request(bt_reset_gpio, "bt_sys_rst_n");
	if (rc) {
		pr_err("%s: unable to request gpio(%d) (%d)\n", __func__,
			bt_reset_gpio, rc);
		return rc;
	}

	rc = gpio_direction_output(bt_reset_gpio, 0);
	if (rc) {
		pr_err("%s: unable to set direction gpio(%d) (%d)\n",
			__func__, bt_reset_gpio, rc);
		return rc;
	}
	drvdata->bt_power_src_status[BT_RESET_GPIO] = 0;
	pr_debug("%s: BT-ON turns off bt-reset-gpio(%d)\n", __func__,
		bt_reset_gpio);
	msleep(50);

	if (bt_sw_ctrl_gpio >= 0) {
		drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
			gpio_get_value(bt_sw_ctrl_gpio);
		pr_debug("%s: BT-ON bt-sw-ctrl-gpio(%d) value(%d)\n",
			__func__, bt_sw_ctrl_gpio,
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO]);
	}
	if (wl_reset_gpio >= 0)
		pr_debug("%s: BT-ON wl-reset-gpio(%d) value(%d)\n",
			__func__, wl_reset_gpio, gpio_get_value(wl_reset_gpio));

	if ((wl_reset_gpio < 0) ||
		((wl_reset_gpio >= 0) && gpio_get_value(wl_reset_gpio))) {
		btpower_set_xo_clk_gpio_state(drvdata, true);
		pr_info("%s: BT-ON asserting BT_EN (with WLAN)\n", __func__);
		rc = gpio_direction_output(bt_reset_gpio, 1);
		if (rc) {
			pr_err("%s: unable to set direction gpio(%d) (%d)\n",
				__func__, bt_reset_gpio, rc);
			return rc;
		}
		drvdata->bt_power_src_status[BT_RESET_GPIO] = 1;
		btpower_set_xo_clk_gpio_state(drvdata, false);
	}
	if ((wl_reset_gpio >= 0) && (gpio_get_value(wl_reset_gpio) == 0)) {
		if (gpio_get_value(bt_reset_gpio)) {
			pr_warn("%s: WLAN OFF / BT ON too close. Delay BT_EN\n",
				__func__);
			rc = gpio_direction_output(bt_reset_gpio, 0);
			if (rc) {
				pr_err("%s: unable to set direction gpio(%d) (%d)\n",
					__func__, bt_reset_gpio, rc);
				return rc;
			}
			drvdata->bt_power_src_status[BT_RESET_GPIO] = 0;
			msleep(100);
			pr_warn("%s: 100ms delay for AON output to fully discharge\n",
				__func__);
		}
		btpower_set_xo_clk_gpio_state(drvdata, true);
		pr_info("%s: BT-ON asserting BT_EN without WLAN\n", __func__);
		rc = gpio_direction_output(bt_reset_gpio, 1);
		if (rc) {
			pr_err("%s: unable to set direction gpio(%d) (%d)\n",
				__func__, bt_reset_gpio, rc);
			return rc;
		}
		drvdata->bt_power_src_status[BT_RESET_GPIO] = 1;
		btpower_set_xo_clk_gpio_state(drvdata, false);
	}

	msleep(50);

	/* Check if SW_CTRL is asserted */
	if (bt_sw_ctrl_gpio >= 0) {
		rc = gpio_direction_input(bt_sw_ctrl_gpio);
		if (rc) {
			pr_err("%s: unable to set direction gpio(%d) (%d)\n",
				__func__, bt_sw_ctrl_gpio, rc);
		} else if (!gpio_get_value(bt_sw_ctrl_gpio)) {
			/* SW_CTRL not asserted, assert debug GPIO */
			if (bt_debug_gpio >= 0)
				assert_dbg_gpio = 1;
		}
	}
	if (assert_dbg_gpio) {
		rc = gpio_request(bt_debug_gpio, "bt_debug_n");
		if (rc) {
			pr_err("%s: unable to request gpio(%d) (%d)\n",
				__func__, bt_debug_gpio, rc);
		} else {
			rc = gpio_direction_output(bt_debug_gpio, 1);
			if (rc)
				pr_err("%s: unable to set direction gpio(%d) (%d)\n",
					__func__, bt_debug_gpio, rc);
		}
	}

	if (bt_sw_ctrl_gpio >= 0) {
		drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
			gpio_get_value(bt_sw_ctrl_gpio);
		pr_debug("%s: BT-ON bt-sw-ctrl-gpio(%d) value(%d)\n",
			__func__, bt_sw_ctrl_gpio,
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO]);
	}
	pr_debug("%s: BT-ON bt-reset-gpio(%d)\n", __func__, bt_reset_gpio);

	return rc;
}

static int bluetooth_power(struct btpower_platform_data *drvdata,
			   enum bt_power_modes mode)
{
	int rc = 0;

	if (!drvdata) {
		pr_err("%s: device not ready\n", __func__);
		return -ENODEV;
	}

	pr_debug("%s: mode %d -> %d\n", __func__, drvdata->pwr_state, mode);

	switch (mode) {
	case BT_POWER_DISABLE:
		if (drvdata->bt_gpio_sys_rst > 0)
			bt_configure_gpios(drvdata, false);
		drvdata->pwr_state = BT_POWER_DISABLE;
		goto gpio_free;
	case BT_POWER_ENABLE:
		rc = bt_power_vreg_set(drvdata, BT_POWER_ENABLE);
		if (rc < 0) {
			pr_err("%s: bt_power regulators config failed\n",
				__func__);
			goto vreg_disable;
		}
		/* Parse dt_info and check if a target requires clock voting.
		 * Enable BT clock when BT is on and disable it when BT is off
		 */
		if (drvdata->bt_chip_clk) {
			rc = bt_clk_enable(drvdata->bt_chip_clk);
			if (rc < 0) {
				pr_err("%s: bt_power gpio config failed\n",
					__func__);
				goto vreg_disable;
			}
		}
		if (drvdata->bt_gpio_sys_rst > 0) {
			drvdata->bt_power_src_status[BT_RESET_GPIO] =
				DEFAULT_INVALID_VALUE;
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
				DEFAULT_INVALID_VALUE;
			rc = bt_configure_gpios(drvdata, true);
			if (rc < 0) {
				pr_err("%s: bt_power gpio config failed\n",
					__func__);
				goto gpio_free;
			}
		}
		drvdata->pwr_state = BT_POWER_ENABLE;
		return rc;
	case BT_POWER_RETENTION:
		bt_power_vreg_set(drvdata, BT_POWER_RETENTION);
		drvdata->pwr_state = BT_POWER_RETENTION;
		return rc;
	default:
		pr_err("%s: Invalid power mode: %d\n", __func__, mode);
		return -1;
	}

gpio_free:
	if (drvdata->bt_gpio_sys_rst > 0)
		gpio_free(drvdata->bt_gpio_sys_rst);
	if (drvdata->bt_gpio_debug > 0)
		gpio_free(drvdata->bt_gpio_debug);
	if (drvdata->bt_chip_clk)
		bt_clk_disable(drvdata->bt_chip_clk);
vreg_disable:
	bt_power_vreg_set(drvdata, BT_POWER_DISABLE);
	return rc;
}

static int btpower_toggle_radio(void *data, bool blocked)
{
	struct btpower_platform_data *drvdata = data;
	/* BT-OFF: true; BT-ON: false */
	bool previous_blocked = drvdata->pwr_state == BT_POWER_DISABLE;

	if (previous_blocked != blocked)
		return drvdata->bt_power_setup(drvdata, !blocked);
	return 0;
}

static const struct rfkill_ops btpower_rfkill_ops = {
	.set_block = btpower_toggle_radio,
};

static ssize_t extldo_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "false\n");
}
static DEVICE_ATTR_RO(extldo);

static int btpower_rfkill_probe(struct platform_device *pdev,
				struct btpower_platform_data *drvdata)
{
	struct rfkill *rfkill;
	int ret;

	rfkill = rfkill_alloc("bt_power", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			      &btpower_rfkill_ops, drvdata);
	if (!rfkill) {
		dev_err(&pdev->dev, "rfkill allocate failed\n");
		return -ENOMEM;
	}

	/* add file into rfkill to handle LDO27 */
	ret = device_create_file(&pdev->dev, &dev_attr_extldo);
	if (ret < 0)
		pr_warn("%s: device create LDO file error (%d)\n",
			__func__, ret);

	/* force Bluetooth off during init to allow for user control */
	rfkill_init_sw_state(rfkill, true);
	drvdata->pwr_state = BT_POWER_DISABLE;
	drvdata->bt_power_setup(drvdata, BT_POWER_DISABLE);

	ret = rfkill_register(rfkill);
	if (ret) {
		dev_err(&pdev->dev, "rfkill register failed=%d\n", ret);
		rfkill_destroy(rfkill);
		return ret;
	}

	drvdata->rfkill = rfkill;

	return 0;
}

static void btpower_rfkill_remove(struct platform_device *pdev)
{
	struct btpower_platform_data *drvdata = platform_get_drvdata(pdev);
	struct rfkill *rfkill;

	pr_debug("%s\n", __func__);

	if (!drvdata || !drvdata->rfkill)
		return;

	rfkill = drvdata->rfkill;
	drvdata->rfkill = NULL;
	device_remove_file(&pdev->dev, &dev_attr_extldo);
	rfkill_unregister(rfkill);
	rfkill_destroy(rfkill);
}

static int btpower_open(struct inode *inode, struct file *filp);
static long btpower_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static const struct file_operations bt_dev_fops = {
	.owner = THIS_MODULE,
	.open = btpower_open,
	.unlocked_ioctl = btpower_ioctl,
	.compat_ioctl = btpower_ioctl,
};

static int btpower_chardev_create(struct btpower_platform_data *drvdata)
{
	dev_t bpdevt;
	struct class *bpcls;
	struct device *bpdev;
	int ret = 0;

	ret = alloc_chrdev_region(&bpdevt, 0, 1, "bt");
	if (ret || MAJOR(bpdevt) < 0) {
		pr_err("%s: failed to register chardev number (%d)\n",
			__func__, ret);
		return ret;
	}
	cdev_init(&drvdata->cdev, &bt_dev_fops);
	drvdata->cdev.owner = THIS_MODULE;
	ret = cdev_add(&drvdata->cdev, bpdevt, 1);
	if (ret) {
		pr_err("%s: failed to add chardev (%d)\n", __func__, ret);
                goto class_err;
	}
	pr_debug("%s: registered chardev number %d:%d\n", __func__,
		MAJOR(drvdata->cdev.dev), MINOR(drvdata->cdev.dev));

	bpcls = class_create(THIS_MODULE, "bt-dev");
	if (IS_ERR_OR_NULL(bpcls)) {
		ret = PTR_ERR(bpcls);
		pr_err("%s: can't create class (%d)\n", __func__, ret);
		goto class_err;
	}

	bpdev = device_create(bpcls, NULL, drvdata->cdev.dev,
			drvdata, "btpower");
	if (IS_ERR_OR_NULL(bpdev)) {
		ret = PTR_ERR(bpdev);
		pr_err("%s: failed to create device with sysfs (%d)\n",
			__func__, ret);
		goto device_err;
	}
	drvdata->cls = bpcls;
	return 0;

device_err:
	class_destroy(bpcls);
class_err:
	unregister_chrdev(MAJOR(drvdata->cdev.dev), "bt");
	return ret;
}

static void btpower_chardev_remove(struct btpower_platform_data *drvdata)
{
	if (!drvdata || !drvdata->cls)
		return;

	device_destroy(drvdata->cls, drvdata->cdev.dev);
	class_destroy(drvdata->cls);
	drvdata->cls = NULL;
	unregister_chrdev(MAJOR(drvdata->cdev.dev), "bt");
}

static int bt_dt_parse_vreg_info(struct device *dev,
				 struct bt_power_vreg_data *vreg)
{
	int len, ret = 0;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];
	struct device_node *np = dev->of_node;
	const char *vreg_name = vreg->name;

	pr_debug("%s: vreg device tree parse for %s\n", __func__, vreg_name);

	snprintf(prop_name, sizeof(prop_name), "%s-supply", vreg_name);
	if (!of_parse_phandle(np, prop_name, 0)) {
		pr_warn("%s: %s is not provided in device tree\n", __func__,
			prop_name);
		return ret;
	}

	vreg->reg = regulator_get(dev, vreg_name);
	if (IS_ERR(vreg->reg)) {
		ret = PTR_ERR(vreg->reg);
		vreg->reg = NULL;
		pr_warn("%s: failed to get: %s error:%d\n", __func__,
			vreg_name, ret);
		return ret;
	}

	snprintf(prop_name, sizeof(prop_name), "%s-config", vreg_name);
	prop = of_get_property(dev->of_node, prop_name, &len);
	if (!prop || len != (4 * sizeof(__be32))) {
		pr_info("%s: Property %s %s, use default\n", __func__,
			prop_name, prop ? "invalid format" : "doesn't exist");
	} else {
		vreg->min_vol = be32_to_cpup(&prop[0]);
		vreg->max_vol = be32_to_cpup(&prop[1]);
		vreg->load_curr = be32_to_cpup(&prop[2]);
		vreg->is_retention_supp = be32_to_cpup(&prop[3]);
	}

	pr_debug("%s: Got regulator: %s, min_vol: %u, max_vol: %u, load_curr: %u, is_retention_supp: %u\n",
		__func__, vreg->name, vreg->min_vol, vreg->max_vol,
		vreg->load_curr, vreg->is_retention_supp);
	return ret;
}

static int bt_dt_parse_clk_info(struct device *dev,
				struct bt_power_clk_data **clk_data)
{
	int ret = -EINVAL;
	struct bt_power_clk_data *clk = NULL;
	struct device_node *np = dev->of_node;

	pr_debug("%s\n", __func__);

	*clk_data = NULL;
	if (!of_parse_phandle(np, "clocks", 0)) {
		pr_err("%s: clocks is not provided in device tree\n", __func__);
		return ret;
	}

	clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return -ENOMEM;

	/* Parse clock name from node */
	ret = of_property_read_string_index(np, "clock-names", 0, &(clk->name));
	if (ret < 0) {
		pr_err("%s: reading 'clock-names' failed ret=%d\n",
			__func__, ret);
		goto err;
	}

	clk->clk = devm_clk_get(dev, clk->name);
	if (IS_ERR(clk->clk)) {
		ret = PTR_ERR(clk->clk);
		pr_err("%s: failed to get %s ret=%d\n", __func__, clk->name, ret);
		clk->clk = NULL;
		goto err;
	}

	*clk_data = clk;

	return ret;

err:
	devm_kfree(dev, clk);
	return ret;
}

static int bt_power_vreg_get(struct platform_device *pdev,
			     struct btpower_platform_data *drvdata)
{
	int num_vregs, i, ret = 0;
	const struct bt_power *pwrdata = of_device_get_match_data(&pdev->dev);

	if (!pwrdata) {
		pr_err("%s: failed to get dev node\n", __func__);
		return -EINVAL;
	}

	memcpy(&drvdata->compatible, &pwrdata->compatible, sizeof(drvdata->compatible));
	drvdata->vreg_info = pwrdata->vregs;
	num_vregs = drvdata->num_vregs = pwrdata->num_vregs;
	for (i = 0; i < num_vregs; i++) {
		ret = bt_dt_parse_vreg_info(&(pdev->dev), &drvdata->vreg_info[i]);
		/* No point to go further if failed to get regulator handler */
		if (ret)
			break;
	}

	return ret;
}

static int bt_power_vreg_set(struct btpower_platform_data *drvdata,
			     enum bt_power_modes mode)
{
	int num_vregs, i, ret = 0;
	int log_indx;
	struct bt_power_vreg_data *vreg_info = NULL;

	num_vregs = drvdata->num_vregs;
	switch (mode) {
	case BT_POWER_DISABLE:
		for (i = 0; i < num_vregs; i++) {
			vreg_info = &drvdata->vreg_info[i];
			ret = bt_vreg_disable(vreg_info);
		}
		break;
	case BT_POWER_ENABLE:
		for (i = 0; i < num_vregs; i++) {
			vreg_info = &drvdata->vreg_info[i];
			if (!vreg_info->reg)
				continue;
			log_indx = vreg_info->indx.init;
			drvdata->bt_power_src_status[log_indx] =
				DEFAULT_INVALID_VALUE;
			ret = bt_vreg_enable(vreg_info);
			if (ret < 0)
				return ret;
			if (!vreg_info->is_enabled)
				continue;
			drvdata->bt_power_src_status[log_indx] =
				regulator_get_voltage(vreg_info->reg);
		}
		break;
	case BT_POWER_RETENTION:
		for (i = 0; i < num_vregs; i++) {
			vreg_info = &drvdata->vreg_info[i];
			ret = bt_vreg_enable_retention(vreg_info);
		}
		break;
	default:
		pr_err("%s: Invalid power mode: %d\n", __func__, mode);
		ret = -1;
	}
	return ret;
}

static void bt_power_vreg_put(struct btpower_platform_data *drvdata)
{
	int i;
	const struct bt_power_vreg_data *vreg_info = NULL;
	int num_vregs;

	if (!drvdata)
		return;

	num_vregs = drvdata->num_vregs;
	for (i = 0; i < num_vregs; i++) {
		vreg_info = &drvdata->vreg_info[i];
		if (vreg_info->reg)
			regulator_put(vreg_info->reg);
	}
}

static int bt_power_populate_dt_pinfo(struct platform_device *pdev,
				      struct btpower_platform_data *drvdata)
{
	int rc;

	pr_debug("%s\n", __func__);

	if (!drvdata)
		return -ENOMEM;

	if (!pdev->dev.of_node)
		return 0;

	rc = bt_power_vreg_get(pdev, drvdata);
	if (rc)
		return rc;

	drvdata->bt_gpio_sys_rst =
		of_get_named_gpio(pdev->dev.of_node, "qcom,bt-reset-gpio", 0);
	if (drvdata->bt_gpio_sys_rst < 0)
		pr_warn("%s: bt-reset-gpio not provided in device tree\n",
			__func__);

	drvdata->wl_gpio_sys_rst =
		of_get_named_gpio(pdev->dev.of_node, "qcom,wl-reset-gpio", 0);
	if (drvdata->wl_gpio_sys_rst < 0)
		pr_warn("%s: wl-reset-gpio not provided in device tree\n",
			__func__);

	drvdata->bt_gpio_sw_ctrl =
		of_get_named_gpio(pdev->dev.of_node, "qcom,bt-sw-ctrl-gpio",  0);
	if (drvdata->bt_gpio_sw_ctrl < 0)
		pr_warn("%s: bt-sw-ctrl-gpio not provided in device tree\n",
			__func__);

	drvdata->bt_gpio_debug =
		of_get_named_gpio(pdev->dev.of_node, "qcom,bt-debug-gpio",  0);
	if (drvdata->bt_gpio_debug < 0)
		pr_warn("%s: bt-debug-gpio not provided in device tree\n",
			__func__);

	drvdata->xo_gpio_clk =
		of_get_named_gpio(pdev->dev.of_node, "qcom,xo-clk-gpio", 0);
	if (drvdata->xo_gpio_clk < 0)
		pr_warn("%s: xo-clk-gpio not provided in device tree\n",
			__func__);

	rc = bt_dt_parse_clk_info(&pdev->dev, &drvdata->bt_chip_clk);
	if (rc < 0)
		pr_warn("%s: clock not provided in device tree\n", __func__);

	drvdata->bt_power_setup = bluetooth_power;

	return 0;
}

static int bt_power_probe(struct platform_device *pdev)
{
	struct btpower_platform_data *drvdata, *pdata;
	int ret = 0;
	int itr;

	pr_debug("%s\n", __func__);

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->pdev = pdev;
	/* Fill whole array with -2 i.e NOT_AVAILABLE state by default
	 * for any GPIO or Reg handle.
	 */
	for (itr = PWR_SRC_INIT_STATE_IDX; itr < BT_POWER_SRC_SIZE; ++itr)
		drvdata->bt_power_src_status[itr] = PWR_SRC_NOT_AVAILABLE;

	if (pdev->dev.of_node) {
		ret = bt_power_populate_dt_pinfo(pdev, drvdata);
		if (ret < 0) {
			pr_err("%s: Failed to populate device tree info\n",
				__func__);
			goto free_pdata;
		}
		pdev->dev.platform_data = drvdata;
	} else if (pdev->dev.platform_data) {
		pdata = pdev->dev.platform_data;
		/* Optional data set to default if not provided */
		if (!pdata->bt_power_setup)
			pdata->bt_power_setup = bluetooth_power;

		memcpy(drvdata, pdata, sizeof(*drvdata));
	} else {
		pr_err("%s: Failed to get platform data\n", __func__);
		goto free_pdata;
	}
	drvdata->pwr_state = BT_POWER_DISABLE;

	ret = btpower_rfkill_probe(pdev, drvdata);
	if (ret < 0)
		goto free_pdata;

	ret = btpower_chardev_create(drvdata);
	if (ret) {
		btpower_rfkill_remove(pdev);
		goto free_pdata;
	}

	btpower_aop_mbox_init(drvdata);

	platform_set_drvdata(pdev, drvdata);

	return 0;

free_pdata:
	kfree(drvdata);
	return ret;
}

static int bt_power_remove(struct platform_device *pdev)
{
	struct btpower_platform_data *drvdata = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	if (!drvdata)
		return 0;

	btpower_chardev_remove(drvdata);
	btpower_rfkill_remove(pdev);
	bt_power_vreg_put(drvdata);

	kfree(drvdata);

	return 0;
}

int btpower_register_slimdev(struct device *dev)
{
	struct btpower_platform_data *drvdata;

	pr_debug("%s\n", __func__);
	if (dev == NULL || dev_get_drvdata(dev) == NULL) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		return -EINVAL;
	}
	drvdata = dev_get_drvdata(dev);
	drvdata->slim_dev = dev;
	return 0;
}
EXPORT_SYMBOL(btpower_register_slimdev);

int btpower_get_chipset_version(struct btpower_platform_data *drvdata)
{
	pr_debug("%s\n", __func__);
	return drvdata->chipset_version;
}
EXPORT_SYMBOL(btpower_get_chipset_version);

static void set_pwr_srcs_status(struct btpower_platform_data *drvdata,
				const struct bt_power_vreg_data *handle)
{
	int ldo_index;

	if (!handle)
		return;

	ldo_index = handle->indx.crash;
	if (handle->is_enabled && (regulator_is_enabled(handle->reg))) {
		drvdata->bt_power_src_status[ldo_index] =
			(int)regulator_get_voltage(handle->reg);
		pr_debug("%s(%pK) value(%d)\n", handle->name,
			handle, drvdata->bt_power_src_status[ldo_index]);
	} else {
		drvdata->bt_power_src_status[ldo_index] = DEFAULT_INVALID_VALUE;
		pr_err("%s:%s is_enabled: %d\n", __func__, handle->name,
			handle->is_enabled);
	}
}

static void set_gpios_srcs_status(struct btpower_platform_data *drvdata,
				  char *gpio_name, int gpio_index, int handle)
{
	if (handle < 0) {
		drvdata->bt_power_src_status[gpio_index] = DEFAULT_INVALID_VALUE;
		pr_err("%s: %s not configured\n", __func__, gpio_name);
		return;
	}

	drvdata->bt_power_src_status[gpio_index] = gpio_get_value(handle);
	pr_debug("%s(%d) value(%d)\n", gpio_name, handle,
		drvdata->bt_power_src_status[gpio_index]);
}

static int btpower_open(struct inode *inode, struct file *filp)
{
	filp->private_data =
		container_of(inode->i_cdev, struct btpower_platform_data, cdev);
	return 0;
}

static long btpower_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct btpower_platform_data *drvdata = file->private_data;
	int ret = 0, pwr_cntrl = 0;
	int chipset_version = 0;
	int itr, num_vregs;
	const struct bt_power_vreg_data *vreg_info = NULL;

	if (!drvdata) {
		pr_err("%s: device not ready\n", __func__);
		return -ENODEV;
	}

	switch (cmd) {
	case BT_CMD_SLIM_TEST:
#if IS_ENABLED(CONFIG_BT_SLIM_QCA6390) || \
	IS_ENABLED(CONFIG_BT_SLIM_QCA6490) || \
	IS_ENABLED(CONFIG_BTFM_SLIM_WCN3990) || \
	IS_ENABLED(CONFIG_BTFM_SLIM_WCN7850)
		if (!drvdata->slim_dev) {
			pr_err("%s: slim_dev is null\n", __func__);
			return -EINVAL;
		}
		ret = btfm_slim_hw_init(drvdata->slim_dev->platform_data);
#endif
		break;
	case BT_CMD_PWR_CTRL:
		pwr_cntrl = (enum bt_power_modes)arg;
		if (drvdata->pwr_state == pwr_cntrl) {
			pr_warn("%s: BT_CMD_PWR_CTRL state(%d) already\n",
				__func__, drvdata->pwr_state);
			ret = 0;
			break;
		}
		pr_info("%s: BT_CMD_PWR_CTRL pwr_cntrl: %d\n", __func__,
			pwr_cntrl);
		ret = bluetooth_power(drvdata, pwr_cntrl);
		break;
	case BT_CMD_CHIPSET_VERS:
		chipset_version = (int)arg;
		if (!chipset_version) {
			pr_err("%s: got invalid soc version %x\n", __func__,
				chipset_version);
			drvdata->chipset_version = 0;
			break;
		}
		drvdata->chipset_version = chipset_version;
		pr_info("%s: unified Current SOC Version : %x\n", __func__,
			drvdata->chipset_version);
		break;
	case BT_CMD_GET_CHIPSET_ID:
		if (copy_to_user((void __user *)arg,
				drvdata->compatible, MAX_PROP_SIZE)) {
			ret = -EFAULT;
		}
		break;
	case BT_CMD_CHECK_SW_CTRL:
		/* Check if SW_CTRL is asserted */
		pr_debug("%s: BT_CMD_CHECK_SW_CTRL\n", __func__);
		if (drvdata->bt_gpio_sw_ctrl <= 0) {
			pr_err("%s: bt_gpio_sw_ctrl not configured\n", __func__);
			return -EINVAL;
		}
		ret = gpio_direction_input(drvdata->bt_gpio_sw_ctrl);
		if (ret) {
			pr_err("%s: unable to set direction gpio(%d) (%d)\n",
				__func__, drvdata->bt_gpio_sw_ctrl, ret);
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
				DEFAULT_INVALID_VALUE;
			break;
		}
		drvdata->bt_power_src_status[BT_SW_CTRL_GPIO] =
			gpio_get_value(drvdata->bt_gpio_sw_ctrl);
		pr_debug("%s: bt-sw-ctrl-gpio(%d) value(%d)\n", __func__,
			drvdata->bt_gpio_sw_ctrl,
			drvdata->bt_power_src_status[BT_SW_CTRL_GPIO]);
		break;
	case BT_CMD_GETVAL_POWER_SRCS:
		pr_debug("%s: BT_CMD_GETVAL_POWER_SRCS\n", __func__);
		set_gpios_srcs_status(drvdata, "BT_RESET_GPIO",
			BT_RESET_GPIO_CURRENT, drvdata->bt_gpio_sys_rst);
		set_gpios_srcs_status(drvdata, "SW_CTRL_GPIO",
			BT_SW_CTRL_GPIO_CURRENT, drvdata->bt_gpio_sw_ctrl);
		num_vregs = drvdata->num_vregs;
		for (itr = 0; itr < num_vregs; itr++) {
			vreg_info = &drvdata->vreg_info[itr];
			set_pwr_srcs_status(drvdata, vreg_info);
		}
		if (copy_to_user((void __user *)arg,
				 drvdata->bt_power_src_status,
				 sizeof(drvdata->bt_power_src_status))) {
			ret = -EFAULT;
		}
		break;
	case BT_CMD_SET_IPA_TCS_INFO:
		pr_debug("%s: BT_CMD_SET_IPA_TCS_INFO\n", __func__);
		btpower_enable_ipa_vreg(drvdata);
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ret;
}

static struct platform_driver bt_power_driver = {
	.probe = bt_power_probe,
	.remove = bt_power_remove,
	.driver = {
		.name = "bt_power",
		.of_match_table = bt_power_match_table,
	},
};

static int __init btpower_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&bt_power_driver);
	if (ret)
		pr_err("%s: platform_driver_register error: %d\n",
			__func__, ret);
	return ret;
}

int btpower_aop_mbox_init(struct btpower_platform_data *drvdata)
{
	struct mbox_client *mbox = &drvdata->mbox_client_data;
	struct mbox_chan *chan;
	int ret = 0;

	mbox->dev = &drvdata->pdev->dev;
	mbox->tx_block = true;
	mbox->tx_tout = BTPOWER_MBOX_TIMEOUT_MS;
	mbox->knows_txdone = false;

	drvdata->mbox_chan = NULL;
	chan = mbox_request_channel(mbox, 0);
	if (IS_ERR(chan)) {
		pr_err("%s: failed to get mbox channel\n", __func__);
		return PTR_ERR(chan);
	}
	drvdata->mbox_chan = chan;

	ret = of_property_read_string(drvdata->pdev->dev.of_node,
		"qcom,vreg_ipa", &drvdata->vreg_ipa);
	if (ret)
		pr_warn("%s: vreg for iPA not provided in device tree\n",
			__func__);
	else
		pr_debug("%s: Mbox channel initialized\n", __func__);

	return 0;
}

static int btpower_aop_set_vreg_param(struct btpower_platform_data *drvdata,
		const char *vreg_name, enum btpower_vreg_param param,
		enum btpower_tcs_seq seq, int val)
{
	struct qmp_pkt pkt;
	char mbox_msg[BTPOWER_MBOX_MSG_MAX_LEN];
	int ret = 0;

	if (!vreg_name || param >= BTPOWER_VREG_PARAM_MAX ||
	    seq >= BTPOWER_TCS_SEQ_MAX)
		return -EINVAL;

	snprintf(mbox_msg, BTPOWER_MBOX_MSG_MAX_LEN,
		 "{class: wlan_pdc, res: %s.%c, %s: %d}", vreg_name,
		 vreg_param_str[param], tcs_seq_str[seq], val);
	pr_debug("%s: sending AOP Mbox msg: %s\n", __func__, mbox_msg);
	pkt.size = BTPOWER_MBOX_MSG_MAX_LEN;
	pkt.data = mbox_msg;
	ret = mbox_send_message(drvdata->mbox_chan, &pkt);
	if (ret < 0)
		pr_err("%s: Failed to send AOP mbox msg(%s) err(%d)\n",
			__func__, mbox_msg, ret);

	return ret;
}

static int btpower_enable_ipa_vreg(struct btpower_platform_data *drvdata)
{
	int ret = 0;

	if (drvdata->vreg_ipa_configured) {
		pr_debug("%s: IPA Vreg already configured\n", __func__);
		return 0;
	}

	if (!drvdata->vreg_ipa || !drvdata->mbox_chan) {
		pr_debug("%s: mbox/iPA vreg not specified\n", __func__);
		return ret;
	}

	ret = btpower_aop_set_vreg_param(drvdata, drvdata->vreg_ipa,
		BTPOWER_VREG_ENABLE, BTPOWER_TCS_UP_SEQ, 1);
	if (ret >= 0) {
		pr_debug("%s: Enabled iPA\n", __func__);
		drvdata->vreg_ipa_configured = true;
	}

	return ret;
}

static void __exit btpower_exit(void)
{
	platform_driver_unregister(&bt_power_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM Bluetooth power control driver");

module_init(btpower_init);
module_exit(btpower_exit);
