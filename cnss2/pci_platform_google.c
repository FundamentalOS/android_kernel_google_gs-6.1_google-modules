// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved. */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_reserved_mem.h>
#include <linux/exynos-pci-ctrl.h>
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
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 5, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 8 },
		{ .name = "DP", .num_vectors = 7, .base_vector = 9 },
	},
};

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


void cnss_set_perst_gpio(struct cnss_plat_data *plat_priv)
{
	exynos_pcie_set_perst_gpio(plat_priv->rc_num, false);
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

struct cnss_msi_config *cnss_get_msi_config(void)
{
	return &msi_config;
}

int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv) {
	return 0;
}


static irqreturn_t cnss_pci_wake_handler(int irq, void *data)
{
	struct cnss_pci_data *pci_priv = data;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	pci_priv->wake_counter++;
	cnss_pr_dbg("WLAN PCI wake IRQ (%u) is asserted #%u\n",
		    pci_priv->wake_irq, pci_priv->wake_counter);

	/* Make sure abort current suspend */
	cnss_pm_stay_awake(plat_priv);
	cnss_pm_relax(plat_priv);
	/* Above two pm* API calls will abort system suspend only when
	 * plat_dev->dev->ws is initiated by device_init_wakeup() API, and
	 * calling pm_system_wakeup() is just to guarantee system suspend
	 * can be aborted if it is not initiated in any case.
	 */
	pm_system_wakeup();

	if (cnss_pci_get_monitor_wake_intr(pci_priv) &&
	    cnss_pci_get_auto_suspended(pci_priv)) {
		cnss_pci_set_monitor_wake_intr(pci_priv, false);
		cnss_pci_pm_request_resume(pci_priv);
	}

	return IRQ_HANDLED;
}

/**
 * cnss_pci_wake_gpio_init() - Setup PCI wake GPIO for WLAN
 * @pci_priv: driver PCI bus context pointer
 *
 * This function initializes WLAN PCI wake GPIO and corresponding
 * interrupt. It should be used in non-MSM platforms whose PCIe
 * root complex driver doesn't handle the GPIO.
 *
 * Return: 0 for success or skip, negative value for error
 */
int cnss_pci_wake_gpio_init(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device *dev = &plat_priv->plat_dev->dev;
	int ret = 0;

	pci_priv->wake_gpio = of_get_named_gpio(dev->of_node,
						"wlan-pci-wake-gpio", 0);
	if (pci_priv->wake_gpio < 0)
		goto out;

	cnss_pr_dbg("Get PCI wake GPIO (%d) from device node\n",
		    pci_priv->wake_gpio);

	ret = gpio_request(pci_priv->wake_gpio, "wlan_pci_wake_gpio");
	if (ret) {
		cnss_pr_err("Failed to request PCI wake GPIO, err = %d\n",
			    ret);
		goto out;
	}

	gpio_direction_input(pci_priv->wake_gpio);
	pci_priv->wake_irq = gpio_to_irq(pci_priv->wake_gpio);

	ret = request_irq(pci_priv->wake_irq, cnss_pci_wake_handler,
			  IRQF_TRIGGER_FALLING, "wlan_pci_wake_irq", pci_priv);
	if (ret) {
		cnss_pr_err("Failed to request PCI wake IRQ, err = %d\n", ret);
		goto free_gpio;
	}

	ret = enable_irq_wake(pci_priv->wake_irq);
	if (ret) {
		cnss_pr_err("Failed to enable PCI wake IRQ, err = %d\n", ret);
		goto free_irq;
	}

	return 0;

free_irq:
	free_irq(pci_priv->wake_irq, pci_priv);
free_gpio:
	gpio_free(pci_priv->wake_gpio);
out:
	return ret;
}

void cnss_pci_wake_gpio_deinit(struct cnss_pci_data *pci_priv)
{
	if (pci_priv->wake_gpio < 0)
		return;

	disable_irq_wake(pci_priv->wake_irq);
	free_irq(pci_priv->wake_irq, pci_priv);
	gpio_free(pci_priv->wake_gpio);
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

int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
	return 0;
}

int check_id_table(struct cnss_wlan_driver *driver_ops)
{
	return 0;
}

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
	int ret = 0;
	ret = exynos_pcie_pm_resume(rc_num);
	cnss_pr_err("ret of exynos_pcie_pm_resume: %d\n", ret);
	return ret;
}

int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
	return -EOPNOTSUPP;
}

int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				       u32 rc_num, u16 link_speed)
{
	return 0;
}

int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				       u16 link_speed, u16 link_width)
{
	return 0;
}

// static int _cnss_pci_prevent_l1(struct cnss_pci_data *pci_priv)
// {
// 	return 0;
// }

// static void _cnss_pci_allow_l1(struct cnss_pci_data *pci_priv) {}

// static int cnss_pci_set_link_up(struct cnss_pci_data *pci_priv)
// {
// 	return 0;
// }

// static int cnss_pci_set_link_down(struct cnss_pci_data *pci_priv)
// {
// 	return 0;
// }