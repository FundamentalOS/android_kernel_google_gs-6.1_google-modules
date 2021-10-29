// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved. */

#include "pci_platform.h"


static struct cnss_msi_config msi_config = {
	.total_vectors = 32,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
		{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
	},
};


void cnss_shutdown(struct platform_device *plat_dev) {}

static int cnss_set_pci_link_status(struct cnss_pci_data *pci_priv,
				    enum pci_link_status status)
{
	u16 link_speed, link_width;
	int ret;

	cnss_pr_vdbg("Set PCI link status to: %u\n", status);

	switch (status) {
	case PCI_GEN1:
		link_speed = PCI_EXP_LNKSTA_CLS_2_5GB;
		link_width = PCI_EXP_LNKSTA_NLW_X1 >> PCI_EXP_LNKSTA_NLW_SHIFT;
		break;
	case PCI_GEN2:
		link_speed = PCI_EXP_LNKSTA_CLS_5_0GB;
		link_width = PCI_EXP_LNKSTA_NLW_X1 >> PCI_EXP_LNKSTA_NLW_SHIFT;
		break;
	case PCI_DEF:
		link_speed = pci_priv->def_link_speed;
		link_width = pci_priv->def_link_width;
		if (!link_speed && !link_width) {
			cnss_pr_err("PCI link speed or width is not valid\n");
			return -EINVAL;
		}
		break;
	default:
		cnss_pr_err("Unknown PCI link status config: %u\n", status);
		return -EINVAL;
	}

	ret = cnss_pci_set_link_bandwidth(pci_priv, link_speed, link_width);
	if (!ret)
		pci_priv->cur_link_speed = link_speed;

	return ret;
}

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
	int ret = 0, retry = 0;

	cnss_pr_vdbg("%s PCI link\n", link_up ? "Resuming" : "Suspending");

	if (link_up) {
retry:
		ret = cnss_pci_set_link_up(pci_priv);
		if (ret && retry++ < LINK_TRAINING_RETRY_MAX_TIMES) {
			cnss_pr_dbg("Retry PCI link training #%d\n", retry);
			if (pci_priv->pci_link_down_ind)
				msleep(LINK_TRAINING_RETRY_DELAY_MS * retry);
			goto retry;
		}
	} else {
		/* Since DRV suspend cannot be done in Gen 3, set it to
		 * Gen 2 if current link speed is larger than Gen 2.
		 */
		if (pci_priv->drv_connected_last &&
		    pci_priv->cur_link_speed > PCI_EXP_LNKSTA_CLS_5_0GB)
			cnss_set_pci_link_status(pci_priv, PCI_GEN2);

		ret = cnss_pci_set_link_down(pci_priv);
	}

	if (pci_priv->drv_connected_last) {
		if ((link_up && !ret) || (!link_up && ret))
			cnss_set_pci_link_status(pci_priv, PCI_DEF);
	}

	return ret;
}

static bool cnss_pci_is_drv_supported(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *root_port = pcie_find_root_port(pci_priv->pci_dev);
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device_node *root_of_node;
	bool drv_supported = false;

	if (!root_port) {
		cnss_pr_err("PCIe DRV is not supported as root port is null\n");
		pci_priv->drv_supported = false;
		return drv_supported;
	}

	root_of_node = root_port->dev.of_node;

	if (root_of_node->parent)
		drv_supported = of_property_read_bool(root_of_node->parent,
						      "qcom,drv-supported");

	cnss_pr_dbg("PCIe DRV is %s\n",
		    drv_supported ? "supported" : "not supported");
	pci_priv->drv_supported = drv_supported;

	if (drv_supported) {
		plat_priv->cap.cap_flag |= CNSS_HAS_DRV_SUPPORT;
		cnss_set_feature_list(plat_priv, CNSS_DRV_SUPPORT_V01);
	}

	return drv_supported;
}

static void cnss_pci_event_cb(struct msm_pcie_notify *notify)
{
	struct pci_dev *pci_dev;
	struct cnss_pci_data *pci_priv;
	struct device *dev;

	if (!notify)
		return;

	pci_dev = notify->user;
	if (!pci_dev)
		return;

	pci_priv = cnss_get_pci_priv(pci_dev);
	if (!pci_priv)
		return;
	dev = &pci_priv->pci_dev->dev;

	switch (notify->event) {
	case MSM_PCIE_EVENT_LINKDOWN:
		cnss_pr_dbg("PCI link down event callback\n");
		cnss_pci_handle_linkdown(pci_priv);
		break;
	case MSM_PCIE_EVENT_WAKEUP:
		if ((cnss_pci_get_monitor_wake_intr(pci_priv) &&
		     cnss_pci_get_auto_suspended(pci_priv)) ||
		     dev->power.runtime_status == RPM_SUSPENDING) {
			cnss_pci_set_monitor_wake_intr(pci_priv, false);
			cnss_pci_pm_request_resume(pci_priv);
		}
		break;
	case MSM_PCIE_EVENT_DRV_CONNECT:
		cnss_pr_dbg("DRV subsystem is connected\n");
		cnss_pci_set_drv_connected(pci_priv, 1);
		break;
	case MSM_PCIE_EVENT_DRV_DISCONNECT:
		cnss_pr_dbg("DRV subsystem is disconnected\n");
		if (cnss_pci_get_auto_suspended(pci_priv))
			cnss_pci_pm_request_resume(pci_priv);
		cnss_pci_set_drv_connected(pci_priv, 0);
		break;
	default:
		cnss_pr_err("Received invalid PCI event: %d\n", notify->event);
	}
}

/**
 * cnss_reg_pci_event() - Register for PCIe events
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to register for PCIe events like link down or WAKE GPIO toggling etc.
 * The events should be based on PCIe root complex driver's capability.
 *
 * Return: 0 for success, negative value for error
 */
int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	int ret = 0;
	struct msm_pcie_register_event *pci_event;

	pci_event = &pci_priv->msm_pci_event;
	pci_event->events = MSM_PCIE_EVENT_LINKDOWN |
		MSM_PCIE_EVENT_WAKEUP;

	if (cnss_pci_is_drv_supported(pci_priv))
		pci_event->events = pci_event->events |
			MSM_PCIE_EVENT_DRV_CONNECT |
			MSM_PCIE_EVENT_DRV_DISCONNECT;

	pci_event->user = pci_priv->pci_dev;
	pci_event->mode = MSM_PCIE_TRIGGER_CALLBACK;
	pci_event->callback = cnss_pci_event_cb;
	pci_event->options = MSM_PCIE_CONFIG_NO_RECOVERY;

	ret = msm_pcie_register_event(pci_event);
	if (ret)
		cnss_pr_err("Failed to register MSM PCI event, err = %d\n",
			    ret);

	return ret;
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv)
{
	msm_pcie_deregister_event(&pci_priv->msm_pci_event);
}

void cnss_set_perst_gpio(struct cnss_plat_data *plat_priv) {}

// int platform_pci_init(struct cnss_plat_data *plat_priv) {
// 	struct device *dev = &plat_priv->plat_dev->dev;
// 	const __be32 *prop;
// 	int ret = 0, prop_len = 0, rc_count, i;

// 	prop = of_get_property(dev->of_node, "qcom,wlan-rc-num", &prop_len);
// 	if (!prop || !prop_len) {
// 		cnss_pr_err("Failed to get PCIe RC number from DT\n");
// 		return ENOENT;
// 	}

// 	rc_count = prop_len / sizeof(__be32);
// 	for (i = 0; i < rc_count; i++) {
// 		ret = cnss_pci_enumerate(plat_priv, be32_to_cpup(&prop[i]));
// 		if (!ret)
// 			break;
// 		else if (ret == -EPROBE_DEFER || (ret && i == rc_count - 1))
// 			break;
// 	}
// 	return ret;
// }

int cnss_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_dbg("PCIe link is in suspend state\n");
		return -EIO;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return -EIO;
	}

	ret = _cnss_pci_prevent_l1(pci_priv);
	if (ret == -EIO) {
		cnss_pr_err("Failed to prevent PCIe L1, considered as link down\n");
		cnss_pci_link_down(dev);
	}

	return ret;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

void cnss_pci_allow_l1(struct device *dev)
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

	_cnss_pci_allow_l1(pci_priv);
}
EXPORT_SYMBOL(cnss_pci_allow_l1)

struct cnss_msi_config *cnss_get_msi_config(void) {
	return &msi_config;
}

static int cnss_pci_smmu_fault_handler(struct iommu_domain *domain,
				       struct device *dev, unsigned long iova,
				       int flags, void *handler_token)
{
	struct cnss_pci_data *pci_priv = handler_token;

	cnss_fatal_err("SMMU fault happened with IOVA 0x%lx\n", iova);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	cnss_force_fw_assert(&pci_priv->pci_dev->dev);

	/* IOMMU driver requires -ENOSYS to print debug info. */
	return -ENOSYS;
}

int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device_node *of_node;
	struct resource *res;
	const char *iommu_dma_type;
	u32 addr_win[2];
	int ret = 0;

	of_node = of_parse_phandle(pci_dev->dev.of_node, "qcom,iommu-group", 0);
	if (!of_node)
		return ret;

	cnss_pr_dbg("Initializing SMMU\n");

	pci_priv->iommu_domain = iommu_get_domain_for_dev(&pci_dev->dev);
	ret = of_property_read_string(of_node, "qcom,iommu-dma",
				      &iommu_dma_type);
	if (!ret && !strcmp("fastmap", iommu_dma_type)) {
		cnss_pr_dbg("Enabling SMMU S1 stage\n");
		pci_priv->smmu_s1_enable = true;
		iommu_set_fault_handler(pci_priv->iommu_domain,
					cnss_pci_smmu_fault_handler, pci_priv);
	}

	ret = of_property_read_u32_array(of_node,  "qcom,iommu-dma-addr-pool",
					 addr_win, ARRAY_SIZE(addr_win));
	if (ret) {
		cnss_pr_err("Invalid SMMU size window, err = %d\n", ret);
		of_node_put(of_node);
		return ret;
	}

	pci_priv->smmu_iova_start = addr_win[0];
	pci_priv->smmu_iova_len = addr_win[1];
	cnss_pr_dbg("smmu_iova_start: %pa, smmu_iova_len: 0x%zx\n",
		    &pci_priv->smmu_iova_start,
		    pci_priv->smmu_iova_len);

	res = platform_get_resource_byname(plat_priv->plat_dev, IORESOURCE_MEM,
					   "smmu_iova_ipa");
	if (res) {
		pci_priv->smmu_iova_ipa_start = res->start;
		pci_priv->smmu_iova_ipa_current = res->start;
		pci_priv->smmu_iova_ipa_len = resource_size(res);
		cnss_pr_dbg("smmu_iova_ipa_start: %pa, smmu_iova_ipa_len: 0x%zx\n",
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
	}

	pci_priv->iommu_geometry = of_property_read_bool(of_node,
							 "qcom,iommu-geometry");
	cnss_pr_dbg("iommu_geometry: %d\n", pci_priv->iommu_geometry);

	of_node_put(of_node);

	return 0;
}


int cnss_pci_wake_gpio_init(struct cnss_pci_data *pci_priv)
{
	return 0;
}

void cnss_pci_wake_gpio_deinit(struct cnss_pci_data *pci_priv)
{
}

/**
 * cnss_pci_of_reserved_mem_device_init() - Assign reserved memory region
 *                                          to given PCI device
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding of_reserved_mem_device* API to
 * assign reserved memory region to PCI device based on where the memory is
 * defined and attached to (platform device of_node or PCI device of_node)
 * in device tree.
 *
 * Return: 0 for success, negative value for error
 */
static int cnss_pci_of_reserved_mem_device_init(struct cnss_pci_data *pci_priv)
{
	struct device *dev_pci = &pci_priv->pci_dev->dev;
	int ret;

	/* Use of_reserved_mem_device_init_by_idx() if reserved memory is
	 * attached to platform device of_node.
	 */
	ret = of_reserved_mem_device_init(dev_pci);
	if (ret)
		cnss_pr_err("Failed to init reserved mem device, err = %d\n",
			    ret);
	if (dev_pci->cma_area)
		cnss_pr_dbg("CMA area is %s\n",
			    cma_get_name(dev_pci->cma_area));

	return ret;
}

/**
 * cnss_pci_disable_pc() - Disable PCIe link power collapse from RC driver
 * @pci_priv: driver PCI bus context pointer
 * @vote: value to indicate disable (true) or enable (false)
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to disable PCIe power collapse. The purpose of this API is to avoid
 * root complex driver still controlling PCIe link from callbacks of
 * system suspend/resume. Device driver itself should take full control
 * of the link in such cases.
 *
 * Return: 0 for success, negative value for error
 */
int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;

	return msm_pcie_pm_control(vote ? MSM_PCIE_DISABLE_PC :
				   MSM_PCIE_ENABLE_PC,
				   pci_dev->bus->number, pci_dev, NULL,
				   PM_OPTIONS_DEFAULT);
}

int check_id_table(struct cnss_wlan_driver *driver_ops)
{
	const struct pci_device_id *id_table = driver_ops->id_table;
	if (!id_table || !pci_dev_present(id_table)) {
		/* id_table pointer will move from pci_dev_present(),
		 * so check again using local pointer.
		 */
		id_table = driver_ops->id_table;
		while (id_table->vendor) {
			cnss_pr_info("Host driver is built for PCIe device ID 0x%x\n",
				     id_table->device);
			id_table++;
		}
		cnss_pr_err("Enumerated PCIe device id is 0x%x, reject unsupported driver\n",
			    pci_priv->device_id);
		return -ENODEV;
	}
	return 0;
}

/**
 * _cnss_pci_enumerate() - Enumerate PCIe endpoints
 * @plat_priv: driver platform context pointer
 * @rc_num: root complex index that an endpoint connects to
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to power on root complex and enumerate the endpoint connected to it.
 *
 * Return: 0 for success, negative value for error
 */
int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
	return msm_pcie_enumerate(rc_num);
}

/**
 * cnss_pci_assert_perst() - Assert PCIe PERST GPIO
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to assert PCIe PERST GPIO.
 *
 * Return: 0 for success, negative value for error
 */
static int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;

	return msm_pcie_pm_control(MSM_PCIE_HANDLE_LINKDOWN,
				   pci_dev->bus->number, pci_dev, NULL,
				   PM_OPTIONS_DEFAULT);
}

/**
 * cnss_pci_set_max_link_speed() - Set the maximum speed PCIe can link up with
 * @pci_priv: driver PCI bus context pointer
 * @rc_num: root complex index that an endpoint connects to
 * @link_speed: PCIe link gen speed
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to update the maximum speed that PCIe can link up with.
 *
 * Return: 0 for success, negative value for error
 */
int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				       u32 rc_num, u16 link_speed)
{
	return msm_pcie_set_target_link_speed(rc_num, link_speed);
}

/**
 * cnss_pci_set_link_bandwidth() - Update number of lanes and speed of
 *                                 PCIe link
 * @pci_priv: driver PCI bus context pointer
 * @link_speed: PCIe link gen speed
 * @link_width: number of lanes for PCIe link
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to update number of lanes and speed of the link.
 *
 * Return: 0 for success, negative value for error
 */
int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				       u16 link_speed, u16 link_width)
{
	return msm_pcie_set_link_bandwidth(pci_priv->pci_dev,
					   link_speed, link_width);
}

/**
 * _cnss_pci_prevent_l1() - Prevent PCIe L1 and L1 sub-states
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to prevent PCIe link enter L1 and L1 sub-states. The APIs should also
 * bring link out of L1 or L1 sub-states if any and avoid synchronization
 * issues if any.
 *
 * Return: 0 for success, negative value for error
 */
static int _cnss_pci_prevent_l1(struct cnss_pci_data *pci_priv)
{
	return msm_pcie_prevent_l1(pci_priv->pci_dev);
}

/**
 * _cnss_pci_allow_l1() - Allow PCIe L1 and L1 sub-states
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to allow PCIe link enter L1 and L1 sub-states. The APIs should avoid
 * synchronization issues if any.
 *
 * Return: 0 for success, negative value for error
 */
static void _cnss_pci_allow_l1(struct cnss_pci_data *pci_priv)
{
	msm_pcie_allow_l1(pci_priv->pci_dev);
}

/**
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to Power on or resume PCIe link.
 *
 * Return: 0 for success, negative value for error
 */
static int cnss_pci_set_link_up(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	enum msm_pcie_pm_opt pm_ops = MSM_PCIE_RESUME;
	u32 pm_options = PM_OPTIONS_DEFAULT;
	int ret;

	ret = msm_pcie_pm_control(pm_ops, pci_dev->bus->number, pci_dev,
				  NULL, pm_options);
	if (ret)
		cnss_pr_err("Failed to resume PCI link with default option, err = %d\n",
			    ret);

	return ret;
}

/**
 * cnss_pci_set_link_down() - Power off or suspend PCIe link
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding PCIe root complex driver APIs
 * to power off or suspend PCIe link.
 *
 * Return: 0 for success, negative value for error
 */
static int cnss_pci_set_link_down(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	enum msm_pcie_pm_opt pm_ops;
	u32 pm_options = PM_OPTIONS_DEFAULT;
	int ret;

	if (pci_priv->drv_connected_last) {
		cnss_pr_vdbg("Use PCIe DRV suspend\n");
		pm_ops = MSM_PCIE_DRV_SUSPEND;
	} else {
		pm_ops = MSM_PCIE_SUSPEND;
	}

	ret = msm_pcie_pm_control(pm_ops, pci_dev->bus->number, pci_dev,
				  NULL, pm_options);
	if (ret)
		cnss_pr_err("Failed to suspend PCI link with default option, err = %d\n",
			    ret);

	return ret;
}
