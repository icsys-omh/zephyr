/*
 * Copyright (c) 2024 Ian Morris
 * Copyright (c) 2024 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for Renesas RA4M1 family processor
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <cmsis_core.h>
#include <zephyr/arch/arm/nmi.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

#include "bsp_cfg.h"
#include <bsp_api.h>

#define HOCO_FREQ DT_PROP(DT_PATH(clocks, clock_hoco), clock_frequency)

#if HOCO_FREQ == MHZ(24)
#define OFS1_HOCO_FREQ		0
#elif HOCO_FREQ == MHZ(32)
#define OFS1_HOCO_FREQ		2
#elif HOCO_FREQ == MHZ(48)
#define OFS1_HOCO_FREQ		4
#elif HOCO_FREQ == MHZ(64)
#define OFS1_HOCO_FREQ		5
#else
#error "Unsupported HOCO frequency"
#endif

struct ofs0_reg {
	uint32_t RSVD1: 1;
	uint32_t IWDTSTRT: 1;
	uint32_t IWDTTOPS: 2;
	uint32_t IWDTCKS: 4;
	uint32_t IWDTRPES: 2;
	uint32_t IWDTRPSS: 2;
	uint32_t IWDTRSTIRQS: 1;
	uint32_t RSVD2: 1;
	uint32_t IWDTSTPCTL: 1;
	uint32_t RSVD3: 2;
	uint32_t WDTSTRT: 1;
	uint32_t WDTTOPS: 2;
	uint32_t WDTCKS: 4;
	uint32_t WDTRPES: 2;
	uint32_t WDTRPSS: 2;
	uint32_t WDTRSTIRQS: 1;
	uint32_t RSVD4: 1;
	uint32_t WDTSTPCTL: 1;
	uint32_t RSVD5: 1;
};

struct ofs1_reg {
	uint32_t RSVD1: 2;
	uint32_t LVDAS: 1;
	uint32_t VDSEL1: 3;
	uint32_t RSVD2: 2;
	uint32_t HOCOEN: 1;
	uint32_t RSVD3: 3;
	uint32_t HOCOFRQ1: 3;
	uint32_t RSVD4: 17;
};

struct mpu_regs {
	uint32_t SECMPUPCSO;
	uint32_t SECMPUPCEO;
	uint32_t SECMPUPCS1;
	uint32_t SECMPUPCE1;
	uint32_t SECMPUS0;
	uint32_t SECMPUE0;
	uint32_t SECMPUS1;
	uint32_t SECMPUE1;
	uint32_t SECMPUS2;
	uint32_t SECMPUE2;
	uint32_t SECMPUS3;
	uint32_t SECMPUE3;
	uint32_t SECMPUAC;
};

struct opt_set_mem {
	struct ofs0_reg ofs0;
	struct ofs1_reg ofs1;
	struct mpu_regs mpu;
};

#ifdef CONFIG_SOC_OPTION_SETTING_MEMORY
const struct opt_set_mem ops __attribute__((section(".rom_registers"))) = {
	.ofs0 = {
			/*
			 * Initial settings for watchdog timers. Set all fields to 1,
			 * disabling watchdog functionality as config options have not
			 * yet been implemented.
			 */
			.RSVD1 = 0x1,       .IWDTSTRT = 0x1, /* Disable independent watchdog timer
							      */
			.IWDTTOPS = 0x3,    .IWDTCKS = 0xf,  .IWDTRPES = 0x3,   .IWDTRPSS = 0x3,
			.IWDTRSTIRQS = 0x1, .RSVD2 = 0x1,    .IWDTSTPCTL = 0x1, .RSVD3 = 0x3,
			.WDTSTRT = 0x1, /* Stop watchdog timer following reset */
			.WDTTOPS = 0x3,     .WDTCKS = 0xf,   .WDTRPES = 0x3,    .WDTRPSS = 0x3,
			.WDTRSTIRQS = 0x1,  .RSVD4 = 0x1,    .WDTSTPCTL = 0x1,  .RSVD5 = 0x1,
		},
	.ofs1 = {
			.RSVD1 = 0x3,
			.LVDAS = 0x1, /* Disable voltage monitor 0 following reset */
			.VDSEL1 = 0x3,
			.RSVD2 = 0x3,
			.HOCOEN = !DT_NODE_HAS_STATUS(DT_PATH(clocks, clock_hoco), okay),
			.RSVD3 = 0x7,
			.HOCOFRQ1 = OFS1_HOCO_FREQ,
			.RSVD4 = 0x1ffff,
		},
	.mpu = {
		/*
		 * Initial settings for MPU. Set all areas to maximum values
		 * essentially disabling MPU functionality as config options
		 * have not yet been implemented.
		 */
		.SECMPUPCSO = 0x00fffffc,
		.SECMPUPCEO = 0x00ffffff,
		.SECMPUPCS1 = 0x00fffffc,
		.SECMPUPCE1 = 0x00ffffff,
		.SECMPUS0 = 0x00fffffc,
		.SECMPUE0 = 0x00ffffff,
		.SECMPUS1 = 0x200ffffc,
		.SECMPUE1 = 0x200fffff,
		.SECMPUS2 = 0x407ffffc,
		.SECMPUE2 = 0x407fffff,
		.SECMPUS3 = 0x40dffffc,
		.SECMPUE3 = 0x40dfffff,
		.SECMPUAC = 0xffffffff,
	}};
#endif

uint32_t SystemCoreClock BSP_SECTION_EARLY_INIT;

volatile uint32_t g_protect_pfswe_counter BSP_SECTION_EARLY_INIT;

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * This needs to be run from the very beginning.
 */
void soc_early_init_hook(void)
{
	uint32_t key;

	key = irq_lock();

	SystemCoreClock = BSP_MOCO_HZ;
	g_protect_pfswe_counter = 0;

	irq_unlock(key);
}
