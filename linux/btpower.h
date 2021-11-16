/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_BLUETOOTH_POWER_H
#define __LINUX_BLUETOOTH_POWER_H

#include <linux/types.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/qmp.h>

/*
 * voltage regulator information required for configuring the
 * bluetooth chipset
 */
enum bt_power_modes {
	BT_POWER_DISABLE = 0,
	BT_POWER_ENABLE,
	BT_POWER_RETENTION
};

struct log_index {
	int init;
	int crash;
};

struct bt_power_vreg_data {
	struct regulator *reg;  /* voltage regulator handle */
	const char *name;       /* regulator name */
	u32 min_vol;            /* min voltage level */
	u32 max_vol;            /* max voltage level */
	u32 load_curr;          /* current */
	bool is_enabled;        /* is this regulator enabled? */
	bool is_retention_supp; /* does this regulator support retention mode */
	struct log_index indx;  /* Index for reg. w.r.t init & crash */
};

#define MAX_PROP_SIZE 32
struct bt_power {
	const char compatible[MAX_PROP_SIZE];
	struct bt_power_vreg_data *vregs;
	int num_vregs;
};

struct bt_power_clk_data {
	struct clk *clk;  /* clock regulator handle */
	const char *name; /* clock name */
	bool is_enabled;  /* is this clock enabled? */
};

/* total number of power src */
#define BT_POWER_SRC_SIZE           28

/*
 * Platform data for the bluetooth power driver.
 */
struct btpower_platform_data {
	struct platform_device *pdev;
	struct rfkill *rfkill;
	struct device *slim_dev;

	int chipset_version;
	char compatible[MAX_PROP_SIZE];        /*Bluetooth SoC name */

	/* GPIOs */
	int bt_gpio_sys_rst;                   /* Bluetooth reset gpio */
	int wl_gpio_sys_rst;                   /* Wlan reset gpio */
	int bt_gpio_sw_ctrl;                   /* Bluetooth sw_ctrl gpio */
	int bt_gpio_debug;                     /* Bluetooth debug gpio */
	int xo_gpio_clk;                       /* XO clock gpio*/

	int num_vregs;
	struct bt_power_vreg_data *vreg_info;  /* VDDIO voltage regulator */
	struct bt_power_clk_data *bt_chip_clk; /* bluetooth reference clock */

	int (*bt_power_setup)(struct btpower_platform_data *drvdata,
		enum bt_power_modes mode);     /* Bluetooth power setup function */
	enum bt_power_modes pwr_state;
	int bt_power_src_status[BT_POWER_SRC_SIZE];

	struct mbox_client mbox_client_data;
	struct mbox_chan *mbox_chan;
	const char *vreg_ipa;
	bool vreg_ipa_configured;
};

extern int btpower_register_slimdev(struct device *dev);
extern int btpower_get_chipset_version(struct btpower_platform_data *drvdata);
extern int btpower_aop_mbox_init(struct btpower_platform_data *drvdata);

#define BT_CMD_SLIM_TEST		0xbfac
#define BT_CMD_PWR_CTRL			0xbfad
#define BT_CMD_CHIPSET_VERS		0xbfae
#define BT_CMD_GET_CHIPSET_ID	0xbfaf
#define BT_CMD_CHECK_SW_CTRL	0xbfb0
#define BT_CMD_GETVAL_POWER_SRCS	0xbfb1
#define BT_CMD_SET_IPA_TCS_INFO  0xbfc0

#endif /* __LINUX_BLUETOOTH_POWER_H */
