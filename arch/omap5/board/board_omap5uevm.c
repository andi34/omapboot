/*
 * Copyright (C) 2010 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <aboot/aboot.h>
#include <aboot/io.h>

#include <common/common_proc.h>
#include <common/omap_rom.h>
#include <common/usbboot_common.h>

static struct partition partitions[] = {
	{ "-", 128 },
	{ "xloader", 256 },
	{ "bootloader", 256 },
	/* "misc" partition is required for recovery */
	{ "misc", 128 },
	{ "-", 384 },
	{ "efs", 16384 },
	{ "crypto", 16 },
	{ "recovery", 8*1024 },
	{ "boot", 8*1024 },
	{ "system", 512*1024 },
	{ "cache", 256*1024 },
	{ "userdata", 0},
	{ 0, 0 },
};

static struct partition * omap5uevm_get_partition(void)
{
	return partitions;
}

/* Use CH (configuration header) to do the settings */
static void omap5uevm_late_init(void)
{
	/* enable uart3 console */
	writel(2, 0x4A009550);
}

static void omap5uevm_scale_cores(void)
{
	/* Use default OMAP voltage */
	scale_vcores();
}

static void omap5uevm_gpmc_init(void)
{
	/* Use default OMAP gpmc init function */
	gpmc_init();
}

static int omap5uevm_mmc_init(u8 mmc_device)
{
	return mmc_init(mmc_device);
}

static int omap5uevm_check_fastboot(void)
{
	return 0;
}

static u8 omap5uevm_get_flash_slot(void)
{
	return DEVICE_EMMC;
}

static void omap5uevm_prcm_init(void)
{
	return;
}

static struct board_specific_functions omap5uevm_funcs = {
	.board_get_flash_slot = omap5uevm_get_flash_slot,
	.board_user_fastboot_request = omap5uevm_check_fastboot,
	.board_late_init = omap5uevm_late_init,
	.board_get_part_tbl = omap5uevm_get_partition,
	.board_prcm_init = omap5uevm_prcm_init,
	.board_scale_vcores = omap5uevm_scale_cores,
	.board_gpmc_init = omap5uevm_gpmc_init,
	.board_storage_init = omap5uevm_mmc_init,
};

void* init_board_funcs(void)
{
	return &omap5uevm_funcs;
}
