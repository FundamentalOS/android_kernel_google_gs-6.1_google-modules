/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved. */

#ifndef _CNSS_PCI_PLATFORM_H
#define _CNSS_PCI_PLATFORM_H

#include <linux/platform_device.h>
#include <linux/pci.h>
#include "pci.h"

void cnss_shutdown(struct platform_device *plat_dev);
int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up);
int cnss_reg_pci_event(struct cnss_pci_data *pci_priv);
void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv);
void cnss_set_perst_gpio(struct cnss_plat_data *plat_priv);
//int platform_pci_init(struct cnss_plat_data *plat_priv);
//int cnss_pci_prevent_l1(struct device *dev);
//void cnss_pci_allow_l1(struct device *dev);
struct cnss_msi_config *cnss_get_msi_config(void);
int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv);
int cnss_pci_wake_gpio_init(struct cnss_pci_data *pci_priv);
void cnss_pci_wake_gpio_deinit(struct cnss_pci_data *pci_priv);
int cnss_pci_of_reserved_mem_device_init(struct cnss_pci_data *pci_priv);
int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote);
int check_id_table(struct cnss_wlan_driver *driver_ops);
int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num);
int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv);
int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				       u32 rc_num, u16 link_speed);
int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				       u16 link_speed, u16 link_width);
//int _cnss_pci_prevent_l1(struct cnss_pci_data *pci_priv);
#endif