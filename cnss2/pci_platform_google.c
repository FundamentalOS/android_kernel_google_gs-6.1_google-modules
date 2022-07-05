// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, The Linux Foundation. All rights reserved. */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_reserved_mem.h>
#include <linux/exynos-pci-ctrl.h>
#include <linux/platform_data/sscoredump.h>
#include "pci_platform.h"
#include "debug.h"
#include "bus.h"

extern int exynos_pcie_pm_resume(int ch_num);
extern void exynos_pcie_pm_suspend(int ch_num);
extern void exynos_pcie_set_perst(int ch_num, bool on);
extern void exynos_pcie_set_perst_gpio(int ch_num, bool on);
extern int exynos_pcie_register_event(struct exynos_pcie_register_event *reg);
extern int exynos_pcie_deregister_event(struct exynos_pcie_register_event *reg);
extern void pm_system_wakeup(void);
extern int exynos_pcie_rc_l1ss_ctrl(int enable, int id, int ch_num);

static DEFINE_SPINLOCK(pci_link_down_lock);

static struct cnss_msi_config msi_config = {
	.total_vectors = 16,
	.total_users = MSI_USERS,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 8 },
		{ .name = "DP", .num_vectors = 7, .base_vector = 9 },
	},
};

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
	int ret = 0;
	ret = exynos_pcie_pm_resume(rc_num);
	return ret;
}

int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
	return -EOPNOTSUPP;
}

int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
	return 0;
}

int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				       u16 link_speed, u16 link_width)
{
	return 0;
}

int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				       u32 rc_num, u16 link_speed)
{
	return 0;
}

static void cnss_pci_event_cb(struct exynos_pcie_notify *notify)
{
	unsigned long flags;
	struct pci_dev *pci_dev;
	struct cnss_pci_data *pci_priv;
	struct cnss_plat_data *plat_priv;

	if (!notify)
		return;

	pci_dev = notify->user;
	if (!pci_dev)
		return;

	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv)
		return;

	plat_priv = pci_priv->plat_priv;
	switch (notify->event) {
//	case EXYNOS_PCIE_EVENT_CPL_TIMEOUT:
//               cnss_pr_err("Received PCI CPL timeout event, link possibly down\n");
               /* Fall through, handle it as link down */
	case EXYNOS_PCIE_EVENT_LINKDOWN:
		//exynos_pcie_set_perst(GOOGLE_RC_ID, false);
		exynos_pcie_set_perst_gpio(plat_priv->rc_num, false);
		if (test_bit(ENABLE_PCI_LINK_DOWN_PANIC,
			     &plat_priv->ctrl_params.quirks))
			panic("cnss: PCI link is down\n");

		spin_lock_irqsave(&pci_link_down_lock, flags);
		if (pci_priv->pci_link_down_ind) {
			cnss_pr_dbg("PCI link down recovery is in progress, ignore\n");
			spin_unlock_irqrestore(&pci_link_down_lock, flags);
			return;
		}
		pci_priv->pci_link_down_ind = true;
		spin_unlock_irqrestore(&pci_link_down_lock, flags);

		cnss_fatal_err("PCI link down, schedule recovery\n");
		cnss_schedule_recovery(&pci_dev->dev, CNSS_REASON_LINK_DOWN);
		break;
	default:
		cnss_pr_err("Received invalid PCI event: %d\n", notify->event);
	}
}

int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct exynos_pcie_register_event *pci_event;

	pci_event = &pci_priv->exynos_pci_event;
	pci_event->events = EXYNOS_PCIE_EVENT_LINKDOWN;
//		EXYNOS_PCIE_EVENT_CPL_TIMEOUT;
	pci_event->user = pci_priv->pci_dev;
	pci_event->mode = EXYNOS_PCIE_TRIGGER_CALLBACK;
	pci_event->callback = cnss_pci_event_cb;

	ret = exynos_pcie_register_event(pci_event);
	if (ret)
		cnss_pr_err("Failed to register exynos PCI event, err = %d\n",
			    ret);
	return ret;
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv)
{
	exynos_pcie_deregister_event(&pci_priv->exynos_pci_event);
}

int cnss_wlan_adsp_pc_enable(struct cnss_pci_data *pci_priv, bool control)
{
	return 0;
}

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
	cnss_pr_vdbg("%s PCI link\n", link_up ? "Resuming" : "Suspending");

	if (link_up) {
		return exynos_pcie_pm_resume(pci_priv->plat_priv->rc_num);
	} else {
		exynos_pcie_pm_suspend(pci_priv->plat_priv->rc_num);
		return 0;
	}
}

int cnss_pci_prevent_l1(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

void cnss_pci_allow_l1(struct device *dev)
{
	return;
}
EXPORT_SYMBOL(cnss_pci_allow_l1);

int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config;

	return 0;
}

int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	return 0;
}

int _cnss_pci_get_reg_dump(struct cnss_pci_data *pci_priv,
			   u8 *buf, u32 len)
{
	return 0;
}

int cnss_pci_of_reserved_mem_device_init(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv = cnss_bus_dev_to_plat_priv(NULL);
	struct device *dev = &pci_priv->pci_dev->dev;
	ret = of_reserved_mem_device_init_by_idx(dev, (&plat_priv->plat_dev->dev)->of_node, 0);
	if (ret)
		cnss_pr_err("Failed to init reserved mem device, err = %d\n", ret);
	if (dev->cma_area)
		cnss_pr_dbg("CMA area\n");

	return ret;
}

/*
 * The following functions are for ssrdump.
 */

#define DEVICE_NAME "wlan"

static struct sscd_platform_data sscd_pdata;

static struct platform_device sscd_dev = {
	.name            = DEVICE_NAME,
	.driver_override = SSCD_NAME,
	.id              = -1,
	.dev             = {
		.platform_data = &sscd_pdata,
		.release       = sscd_release,
    },
};

void cnss_register_sscd(void)
{
	memset(&sscd_pdata, 0, sizeof(struct sscd_platform_data));
	memset(&sscd_dev, 0, sizeof(struct platform_device));
	sscd_dev.name = DEVICE_NAME;
	sscd_dev.driver_override = SSCD_NAME;
	sscd_dev.id = -1;
	sscd_dev.dev.platform_data = &sscd_pdata;
	sscd_dev.dev.release = sscd_release;
	platform_device_register(&sscd_dev);
}

void cnss_unregister_sscd(void)
{
	platform_device_unregister(&sscd_dev);
}

void sscd_release(struct device *dev)
{
	cnss_pr_info("%s: enter\n", __FUNCTION__);
}

u8 *crash_info = 0;
void sscd_set_coredump(void *buf, int buf_len)
{
	struct sscd_platform_data *pdata = dev_get_platdata(&sscd_dev.dev);
	struct sscd_segment seg;

	if (pdata->sscd_report) {
		memset(&seg, 0, sizeof(seg));
		seg.addr = buf;
		seg.size = buf_len;
		if(crash_info) {
			pdata->sscd_report(&sscd_dev, &seg, 1, 0, crash_info);
			kfree(crash_info);
			crash_info = 0;
		} else {
			pdata->sscd_report(&sscd_dev, &seg, 1, 0, "Unknown");
		}
	}
}

void crash_info_handler(u8 *info)
{
	u32 string_len = 0;

	if (crash_info) {
		kfree(crash_info);
		crash_info = 0;
	}

	string_len = strlen(info);
	crash_info = kzalloc(string_len + 1, GFP_KERNEL);
	if (!crash_info)
		return;
	strncpy(crash_info, info, string_len);
	crash_info[string_len] = '\0';
}

int exynos_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_err("PCIe link is in suspend state\n");
		return -EIO;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return -EIO;
	}

	ret = exynos_pcie_rc_l1ss_ctrl(0, PCIE_L1SS_CTRL_WIFI, pci_priv->plat_priv->rc_num);
	return ret;
}

void exynos_pci_allow_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_dbg("PCIe link is in suspend state\n");
		return;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return;
	}

	exynos_pcie_rc_l1ss_ctrl(1, PCIE_L1SS_CTRL_WIFI, pci_priv->plat_priv->rc_num);
}
