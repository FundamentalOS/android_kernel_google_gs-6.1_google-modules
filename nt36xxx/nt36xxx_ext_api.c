// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2021 Novatek, Inc.
 *
 * $Revision: 83893 $
 * $Date: 2021-06-21 10:52:25 +0800 (週一, 21 六月 2021) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include "nt36xxx.h"

#define GET_CALIBRATION_ADDR              0x2B31A
#define GET_MODE_HISTORY_ADDR             0x2B32A
#define HEATMAP_ADDR                      0x2FE20
#define TOUCH_CMD_STATUS_ADDR             0x2FE5C
#define TOUCH_MODE_ADDR                   0x38D33
#define RAWDATA_UNIFORMITY_LIMIT          200
#define PALM_MODE_CMD_TEST_BIT            BIT(0)
#define HIGH_SENSI_MODE_CMD_TEST_BIT      BIT(1)
#define HOLSTER_MODE_CMD_TEST_BIT         BIT(3)
#define TOUCH_IDLE_MODE_CMD_TEST_BIT      BIT(4)
#define ER_MODE_CMD_TEST_BIT              (BIT(5) | BIT(6))
#define CONT_REPORT_MODE_CMD_TEST_BIT     BIT(9)
#define NOISE_MODE_CMD_TEST_BIT           BIT(10)
#define WATER_MODE_CMD_TEST_BIT           BIT(11)
#define GRIP_LEVEL_CMD_TEST_BIT           BIT(13)
#define SET_CANCEL_CMD_TEST_BIT           BIT(14)

enum {
	CMD_DISABLE = 0,
	MODE_1,
	CMD_ENABLE = 1,
	MODE_2,
	MODE_3,
	MODE_4,
	MODE_5,
	MODE_6,
	MODE_7,
	MODE_8,
	MODE_9,
	MODE_10,
	MODE_11,
	MODE_12,
	MODE_13,
	MODE_14,
	MODE_15
};

uint32_t heatmap_spi_buf_size;
uint8_t *heatmap_spi_buf;
uint32_t cc_uniformity_spi_buf_size;
uint32_t rawdata_uniformity_spi_buf_size;
uint8_t *cc_uniformity_spi_buf;
uint8_t *rawdata_uniformity_spi_buf;

ssize_t nvt_check_api_cmd_result(uint16_t cmd_test_bit, uint16_t pattern)
{
	uint8_t spi_buf[3] = {0}, shift = 0;
	uint16_t result;

	spi_buf[0] = TOUCH_CMD_STATUS_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	result = ((spi_buf[2] << 8) | spi_buf[1]) & cmd_test_bit;
	while ((cmd_test_bit & 1) == 0) {
		cmd_test_bit = cmd_test_bit >> 1;
		shift += 1;
	}
	if (result != pattern << shift)
		return -EINVAL;
	return 0;
}

static ssize_t nvt_get_mode_history_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t spi_buf[65] = {0};
	int32_t ret;

	NVT_LOG("++\n");

	mutex_lock(&ts->lock);
	nvt_set_page(GET_MODE_HISTORY_ADDR);
	spi_buf[0] = GET_MODE_HISTORY_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 65);
	ret = snprintf(buf, PAGE_SIZE, "%*ph\n", 64, &spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");
	return ret;
}

static ssize_t nvt_palm_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = PALM_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Palm Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB3;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Palm Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB4;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_high_sensi_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = HIGH_SENSI_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable High Sensitivity Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB1;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable High Sensitivity Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB2;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_holster_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = HOLSTER_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Holster Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB5;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Holster Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB6;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_touch_idle_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint32_t mode;
	uint8_t spi_buf[2] = {0};
	int32_t ret;

	NVT_LOG("++\n");

	mutex_lock(&ts->lock);
	nvt_set_page(TOUCH_MODE_ADDR);
	spi_buf[0] = TOUCH_MODE_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	mode = spi_buf[1];
	switch (mode) {
	case 0x3:
		NVT_LOG("normal active mode\n"); // Active mode
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "Normal_Active");
		break;
	case 0x4:
	case 0x6:
		NVT_LOG("normal idle mode\n"); // Idle mode
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "Normal_Idle");
		break;
	case 0xA:
		NVT_LOG("low power active mode\n"); // WKG mode
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "LowPower_Active");
		break;
	case 0x9:
	case 0xB:
		NVT_LOG("low power idle mode\n"); // FDM mode
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "LowPower_Idle");
		break;
	}
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");
	return ret;
}

static ssize_t nvt_touch_idle_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = TOUCH_IDLE_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Normal/LowPower Idle Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB7;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Normal/LowPower Idle Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB8;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_heatmap_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[6] = {0}, mode;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_3)
		return -EINVAL;


	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_DISABLE:
		NVT_LOG("Disable Heatmap Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x00;
		spi_buf[2] = 0xbb;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		ret = 0;
		break;
	case MODE_1: // Rawdata
		NVT_LOG("Enter Heatmap Rawdata Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x61;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x26;
		spi_buf[2] = 0xBB;
		spi_buf[3] = 0xAA;
		spi_buf[4] = 0x00;
		spi_buf[5] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 6);
		break;
	case MODE_2: // Baseline
		NVT_LOG("Enter Heatmap Baseline Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x61;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x26;
		spi_buf[2] = 0xBB;
		spi_buf[3] = 0xAA;
		spi_buf[4] = 0x01;
		spi_buf[5] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 6);
		break;
	case MODE_3: // Diff
		NVT_LOG("Enter Heatmap Diff Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x61;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x26;
		spi_buf[2] = 0xBB;
		spi_buf[3] = 0xAA;
		spi_buf[4] = 0x02;
		spi_buf[5] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 6);
		break;

	}
	if (mode != CMD_DISABLE) {
		msleep(20);
		spi_buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		spi_buf[1] = 0x00;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		ret = ((spi_buf[1] & 0xF0) != 0xA0);
	}
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		if (mode != CMD_DISABLE && !heatmap_spi_buf) {
			heatmap_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
			heatmap_spi_buf = kzalloc(heatmap_spi_buf_size, GFP_KERNEL);
		}
		NVT_LOG("--\n");
		return count;
	}
}


static ssize_t nvt_cont_report_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = CONT_REPORT_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Continuously Report Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x01;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Continuously Report Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_noise_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = NOISE_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Noise Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x11;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Noise Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x10;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_water_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = WATER_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Water Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x21;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Water Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x20;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_sw_reset_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t mode;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode != CMD_ENABLE)
		return -EINVAL;
	mutex_lock(&ts->lock);
	nvt_bootloader_reset();
	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");

	return count;
}

static ssize_t nvt_sensing_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Sensing Mode\n");
		ret = nvt_update_firmware(BOOT_UPDATE_FIRMWARE_NAME);
	case CMD_DISABLE:
		NVT_LOG("Disable Sensing Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x12;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 3);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	}
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_freq_hopping_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[4] = {0}, mode;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_4 || (mode == 0))
		return -EINVAL;

	mutex_lock(&ts->lock);
	if (nvt_switch_FreqHopEnDis(FREQ_HOP_DISABLE)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("switch frequency hopping disable failed!\n");
		return -EAGAIN;
	}

	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	switch (mode) {
	case MODE_1:
		NVT_LOG("Set Frequency Hopping to Mode 1\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x01;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		msleep(50);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	case MODE_2:
		NVT_LOG("Set Frequency Hopping to Mode 2\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x02;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		msleep(50);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	case MODE_3:
		NVT_LOG("Set Frequency Hopping to Mode 3\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x03;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		msleep(50);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	case MODE_4:
		NVT_LOG("Set Frequency Hopping to Mode 4\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x04;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		msleep(50);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	}
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_grip_level_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = GRIP_LEVEL_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_4)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	switch (mode) {
	case CMD_DISABLE:
		NVT_LOG("Disable Grip Level\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x40;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_1:
		NVT_LOG("Set Grip Level to Enable_weak\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x41;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_2:
		NVT_LOG("Set Grip Level to Enable_Small\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x42;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_3:
		NVT_LOG("Set Grip Level to Enable_Medium\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x43;
		break;
	case MODE_4:
		NVT_LOG("Set Grip Level to Enable_Strong\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x44;
		break;
	}
	mutex_unlock(&ts->lock);

	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode > 0);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_force_calibration_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode != CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	NVT_LOG("Force Calibration\n");
	spi_buf[0] = EVENT_MAP_HOST_CMD;
	spi_buf[1] = 0x23;
	spi_buf[2] = 0x00;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	spi_buf[0] = EVENT_MAP_HOST_CMD;
	spi_buf[1] = 0xFF;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	ret = (spi_buf[1] == 0) ? 0 : -EINVAL;

	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static ssize_t nvt_get_calibration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;
	uint8_t spi_buf[2] = {0};

	NVT_LOG("++\n");

	mutex_lock(&ts->lock);
	nvt_set_page(GET_CALIBRATION_ADDR);
	spi_buf[0] = GET_CALIBRATION_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOG("--\n");
	return ret;
}

static ssize_t nvt_sync_freq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOG("++\n");
	ret = snprintf(buf, PAGE_SIZE, "%s\n", "120Hz");
	NVT_LOG("--\n");

	return ret;
}

void cal_uniformity(uint8_t *arr, uint32_t size)
{
	uint8_t is_right_most, is_bottom;
	uint16_t res, i;

	for (i = 1; i < size; i += 2) {
		is_right_most = ((i + 1) % (ts->x_num  * 2) == 0);
		is_bottom = (size - i <= (ts->x_num * 2));
		if (!is_right_most && !is_bottom)
			res = ((abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
					- ((uint16_t)arr[i + 3] << 8) - arr[i + 2]) \
					+ abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
					- ((uint16_t)arr[i + 1 + (ts->x_num * 2)] << 8) - arr[i + (ts->x_num * 2)])) / 2);
		else if (is_right_most && !is_bottom)
			res = abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
					- ((uint16_t)arr[i + 1 + (ts->x_num * 2)] << 8) - arr[i + (ts->x_num * 2)]);
		else if (!is_right_most && is_bottom)
			res = abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
					- ((uint16_t)arr[i + 3] << 8) - arr[i + 2]);
		else
			res = 0;
		memcpy(&arr[i], &res, 2);
	}
}

static int32_t nvt_get_rawdata_uniformity(void)
{
	NVT_LOG("++\n");

	if (!rawdata_uniformity_spi_buf) {
		rawdata_uniformity_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		rawdata_uniformity_spi_buf = kzalloc(rawdata_uniformity_spi_buf_size, GFP_KERNEL);
	}

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0) {
		nvt_set_page(ts->mmap->RAW_PIPE0_ADDR);
		rawdata_uniformity_spi_buf[0] = ts->mmap->RAW_PIPE0_ADDR & 0x7F;
	} else {
		nvt_set_page(ts->mmap->RAW_PIPE1_ADDR);
		rawdata_uniformity_spi_buf[0] = ts->mmap->RAW_PIPE1_ADDR & 0x7F;
	}

	CTP_SPI_READ(ts->client, rawdata_uniformity_spi_buf, rawdata_uniformity_spi_buf_size);

	nvt_change_mode(NORMAL_MODE);
	mutex_unlock(&ts->lock);

	cal_uniformity(rawdata_uniformity_spi_buf, rawdata_uniformity_spi_buf_size);

	NVT_LOG("--\n");
	return 0;
}

static int32_t nvt_get_cc_uniformity(void)
{
	NVT_LOG("++\n");

	if (!cc_uniformity_spi_buf) {
		cc_uniformity_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		cc_uniformity_spi_buf = kzalloc(cc_uniformity_spi_buf_size, GFP_KERNEL);
	}

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	nvt_update_firmware(MP_UPDATE_FIRMWARE_NAME);
	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("get fw info failed!\n");
		return -EAGAIN;
	}
	if (nvt_check_fw_reset_state(RESET_STATE_REK)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	if (nvt_switch_FreqHopEnDis(FREQ_HOP_DISABLE)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("switch frequency hopping disable failed!\n");
		return -EAGAIN;
	}

	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	msleep(100);

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("clear fw status failed!\n");
		return -EAGAIN;
	}

	nvt_change_mode(MP_MODE_CC);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw status failed!\n");
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0) {
		nvt_set_page(ts->mmap->DIFF_PIPE1_ADDR);
		cc_uniformity_spi_buf[0] = ts->mmap->DIFF_PIPE1_ADDR & 0x7F;
	} else {
		nvt_set_page(ts->mmap->DIFF_PIPE0_ADDR);
		cc_uniformity_spi_buf[0] = ts->mmap->DIFF_PIPE0_ADDR & 0x7F;
	}

	CTP_SPI_READ(ts->client, cc_uniformity_spi_buf, cc_uniformity_spi_buf_size);

	nvt_change_mode(NORMAL_MODE);
	mutex_unlock(&ts->lock);

	cal_uniformity(cc_uniformity_spi_buf, cc_uniformity_spi_buf_size);

	NVT_LOG("--\n");
	return 0;
}

static ssize_t nvt_verify_calibration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t i, ret = 0, max = 0;

	NVT_LOG("++\n");

	if (nvt_get_rawdata_uniformity())
		return -EAGAIN;

	for (i = 1; i < rawdata_uniformity_spi_buf_size; i += 2) {
		if (((uint16_t)rawdata_uniformity_spi_buf[i + 1] << 8) + rawdata_uniformity_spi_buf[i] > max)
			max = ((uint16_t)rawdata_uniformity_spi_buf[i + 1] << 8) + rawdata_uniformity_spi_buf[i];
	}

	if (max > RAWDATA_UNIFORMITY_LIMIT)
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "Fail");
	else
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "Pass");

	NVT_LOG("max rawdata deviation = %d\n", max);

	NVT_LOG("--\n");

	return ret;
}

static ssize_t nvt_set_cancel_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = SET_CANCEL_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOG("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Cancel Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x51;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Cancel Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x50;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("%s failed, ret = %d\n", __func__, ret);
		return -EINVAL;
	} else {
		NVT_LOG("--\n");
		return count;
	}
}

static DEVICE_ATTR_RO(nvt_get_mode_history);
static DEVICE_ATTR_RO(nvt_sync_freq);
static DEVICE_ATTR_WO(nvt_palm_mode);
static DEVICE_ATTR_WO(nvt_high_sensi_mode);
static DEVICE_ATTR_WO(nvt_holster_mode);
static DEVICE_ATTR_WO(nvt_cont_report_mode);
static DEVICE_ATTR_WO(nvt_noise_mode);
static DEVICE_ATTR_WO(nvt_water_mode);
static DEVICE_ATTR_WO(nvt_sw_reset);
static DEVICE_ATTR_WO(nvt_sensing);
static DEVICE_ATTR_WO(nvt_freq_hopping);
static DEVICE_ATTR_RW(nvt_touch_idle_mode);
static DEVICE_ATTR_WO(nvt_force_calibration);
static DEVICE_ATTR_RO(nvt_get_calibration);
static DEVICE_ATTR_RO(nvt_verify_calibration);
static DEVICE_ATTR_WO(nvt_heatmap_mode);
static DEVICE_ATTR_WO(nvt_set_cancel);
static DEVICE_ATTR_WO(nvt_grip_level);

static struct attribute *nvt_api_attrs[] = {
	&dev_attr_nvt_get_mode_history.attr,
	&dev_attr_nvt_sync_freq.attr,
	&dev_attr_nvt_palm_mode.attr,
	&dev_attr_nvt_high_sensi_mode.attr,
	&dev_attr_nvt_holster_mode.attr,
	&dev_attr_nvt_touch_idle_mode.attr,
	&dev_attr_nvt_cont_report_mode.attr,
	&dev_attr_nvt_noise_mode.attr,
	&dev_attr_nvt_water_mode.attr,
	&dev_attr_nvt_sw_reset.attr,
	&dev_attr_nvt_sensing.attr,
	&dev_attr_nvt_heatmap_mode.attr,
	&dev_attr_nvt_freq_hopping.attr,
	&dev_attr_nvt_force_calibration.attr,
	&dev_attr_nvt_get_calibration.attr,
	&dev_attr_nvt_verify_calibration.attr,
	&dev_attr_nvt_set_cancel.attr,
	&dev_attr_nvt_grip_level.attr,
	NULL
};

static const struct attribute_group nvt_api_attribute_group = {
	.attrs = nvt_api_attrs,
};

static struct proc_dir_entry *NVT_proc_heatmap_entry;
static struct proc_dir_entry *NVT_proc_cc_uniformity_entry;

static int32_t c_show_heatmap(struct seq_file *m, void *v)
{
	uint32_t i;

	for (i = 1; i < heatmap_spi_buf_size; i += 2) {
		if ((i - 1) % (ts->x_num * 2) == 0 && i != 1)
			seq_printf(m, "\n");
		seq_printf(m, "%3d ", (int16_t)(heatmap_spi_buf[i+1] << 8) + heatmap_spi_buf[i]);
	}
	seq_printf(m, "\n");

    return 0;
}

static int32_t c_show_cc_uniformity(struct seq_file *m, void *v)
{
	uint32_t i;

	for (i = 1; i < cc_uniformity_spi_buf_size; i += 2) {
		if ((i - 1) % (ts->x_num * 2) == 0 && i != 1)
			seq_printf(m, "\n");
		seq_printf(m, "%3d ", (uint16_t)(cc_uniformity_spi_buf[i+1] << 8) + cc_uniformity_spi_buf[i]);
	}
	seq_printf(m, "\n");

    return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_heatmap_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show_heatmap
};

const struct seq_operations nvt_cc_uniformity_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show_cc_uniformity
};

static int32_t nvt_heatmap_open(struct inode *inode, struct file *file)
{
	uint32_t i, retry = 20;
	uint8_t spi_buf[3] = {0};

	if (!heatmap_spi_buf) {
		NVT_ERR("heatmap_mode not specified\n");
		return -EINVAL;
	}

	nvt_set_page(HEATMAP_ADDR);

	for (i = 0; i < retry; i++) {
		spi_buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		spi_buf[1] = 0x00;
		CTP_SPI_READ(ts->client, spi_buf, 2);
		if ((spi_buf[1] & 0xF0) == 0xA0)
			break;
		usleep_range(500, 500);
	}
	if (i == retry)
		return -EAGAIN;

	heatmap_spi_buf[0] = HEATMAP_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, heatmap_spi_buf, heatmap_spi_buf_size);
	spi_buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	spi_buf[1] = 0xBB;
	CTP_SPI_WRITE(ts->client, spi_buf, 2);

	return seq_open(file, &nvt_heatmap_seq_ops);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops nvt_heatmap_fops = {
	.proc_open = nvt_heatmap_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_heatmap_fops = {
	.owner = THIS_MODULE,
	.open = nvt_heatmap_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static int32_t nvt_cc_uniformity_open(struct inode *inode, struct file *file)
{
	NVT_LOG("++\n");
	if (nvt_get_cc_uniformity())
		return -EAGAIN;

	NVT_LOG("--\n");

	return seq_open(file, &nvt_cc_uniformity_seq_ops);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops nvt_cc_uniformity_fops = {
	.proc_open = nvt_cc_uniformity_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_cc_uniformity_fops = {
	.owner = THIS_MODULE,
	.open = nvt_cc_uniformity_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

int32_t nvt_extra_api_init(void)
{
	int32_t ret;

	NVT_LOG("++\n");

	ret = devm_device_add_group(&ts->client->dev, &nvt_api_attribute_group);
	if (ret)
		NVT_ERR("create sysfs nvt_api_attribute_group failed: %d\n", ret);

	NVT_proc_heatmap_entry = proc_create("nvt_heatmap", 0444, NULL, &nvt_heatmap_fops);
	if (NVT_proc_heatmap_entry == NULL)
		NVT_ERR("create /proc/nvt_heatmap Failed!\n");

	NVT_proc_heatmap_entry = proc_create("nvt_cc_uniformity", 0444, NULL, &nvt_cc_uniformity_fops);
	if (NVT_proc_cc_uniformity_entry == NULL)
		NVT_ERR("create /proc/nvt_cc_uniformity Failed!\n");

	NVT_LOG("--\n");

	return ret;
}

void nvt_extra_api_deinit(void)
{
	NVT_LOG("++\n");
	devm_device_remove_group(&ts->client->dev, &nvt_api_attribute_group);
	kfree(heatmap_spi_buf);
	heatmap_spi_buf = NULL;
	kfree(cc_uniformity_spi_buf);
	cc_uniformity_spi_buf = NULL;
	kfree(rawdata_uniformity_spi_buf);
	rawdata_uniformity_spi_buf = NULL;
	NVT_LOG("--\n");
}

