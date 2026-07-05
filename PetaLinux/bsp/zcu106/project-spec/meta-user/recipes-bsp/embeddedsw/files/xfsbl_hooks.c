/******************************************************************************
* Copyright (c) 2015 - 2023 Xilinx, Inc.  All rights reserved.
* Copyright (c) 2022 - 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xfsbl_hooks.c
*
* This is the file which contains FSBL hook functions.
*
* Local modification -- ZCU106 base "zcu106" target only (FPGA Drive FMC Gen4 on
* the HPC1 slot). XFsbl_HookAfterBSDownload() cycles PERST_A#/PERST_B# through a
* reset (asserted low -> hold -> de-asserted high) via the fpga_drive_gpio AXI
* GPIO, so a slow-to-train endpoint (DeepX M1 / MemryX MX3) on the FPGA Drive FMC
* comes up before Linux probes the XDMA PCIe root complex. Everything else is
* stock.
*
* Unlike the "zcu106_hpc0" target (M.2 M-key Stack FMC, PERST# via a TCA9536 I2C
* expander -- see bsp/zcu106_hpc0), the FPGA Drive FMC's PERST#/detect signals
* are on PL I/O, driven by the dual-channel AXI GPIO added in
* Vivado/src/bd/bd_zynqmp.tcl:
*   ch1 (outputs): [0]=perst_a, [1]=perst_b, [2]=disable_ssd2_pwr
*   ch2 (inputs):  [0]=pedet_a, [1]=pedet_b
* The GPIO's power-on default (C_DOUT_DEFAULT=0x3) already de-asserts both PERST#
* at PL config; this hook adds an explicit reset *pulse* after bitstream download
* for endpoints that need PERST# to toggle late. bit[2] (disable_ssd2_pwr) is
* kept low throughout so SSD2 power stays enabled.
*
******************************************************************************/

/* Enable INFO-level FSBL prints for this file (before xfsbl_hw.h pulls in
 * xfsbl_debug.h). Remove or move to -DFSBL_DEBUG_INFO in the recipe once
 * the PERST sequence is proven. */
#ifndef FSBL_DEBUG_INFO
#define FSBL_DEBUG_INFO
#endif

/***************************** Include Files *********************************/
#include "xfsbl_hw.h"
#include "xfsbl_hooks.h"
#include "psu_init.h"
#include "xparameters.h"   /* XPAR_* base addresses                       */
#include "xil_io.h"        /* Xil_In32() / Xil_Out32()                    */
#include "sleep.h"         /* sleep() / usleep()                          */

/************************** Constant Definitions *****************************/

/**
 * Register: PMU_GLOBAL_DDR_CNTRL
 */
#define PMU_GLOBAL_DDR_CNTRL             ( ( PMU_GLOBAL_BASEADDR ) + ((u32)0X00000070U) )
#define PMU_GLOBAL_DDR_CNTRL_RET_MASK    ((u32)0X00000001U)

/* ---- FPGA Drive FMC PERST# via the fpga_drive_gpio AXI GPIO ---- */
#if defined(XPAR_FPGA_DRIVE_GPIO_BASEADDR)
# define FPGA_DRIVE_GPIO_BASE   XPAR_FPGA_DRIVE_GPIO_BASEADDR
#elif defined(XPAR_FPGA_DRIVE_GPIO_0_BASEADDR)
# define FPGA_DRIVE_GPIO_BASE   XPAR_FPGA_DRIVE_GPIO_0_BASEADDR
#else
# error "fpga_drive_gpio base not found in xparameters.h -- check the generated \
XPAR_*_BASEADDR for the FPGA Drive FMC AXI GPIO and set FPGA_DRIVE_GPIO_BASE."
#endif

#define GPIO_DATA_OFFSET    0x0U   /* AXI GPIO channel 1 data reg (outputs) */
#define GPIO2_DATA_OFFSET   0x8U   /* AXI GPIO channel 2 data reg (inputs)  */

#define PERST_A             0x1U   /* ch1[0] -> PERST_A# (1 = de-asserted)  */
#define PERST_B             0x2U   /* ch1[1] -> PERST_B#                     */
#define PERST_BOTH          (PERST_A | PERST_B)  /* 0x3 = both operational   */
                                   /* ch1[2] disable_ssd2_pwr kept 0 (power on) */
#define PEDET_A             0x1U   /* ch2[0] -> pedet_a (active low)         */
#define PEDET_B             0x2U   /* ch2[1] -> pedet_b (active low)         */
#define PERST_HOLD_S        1u     /* reset hold (tune down once proven)    */

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

#ifdef XFSBL_BS
u32 XFsbl_HookBeforeBSDownload(void )
{
    u32 Status = XFSBL_SUCCESS;

    /**
     * Add the code here
     */


    return Status;
}


u32 XFsbl_HookAfterBSDownload(void )
{
    u32 Status = XFSBL_SUCCESS;
    u32 Pedet;

    /* The PL bitstream is now configured, so the fpga_drive_gpio (and the XDMA
     * PCIe root complex) are up. Pulse PERST_A#/PERST_B# on the FPGA Drive FMC
     * so the M.2 endpoints re-train before Linux probes the root complex. */
    XFsbl_Printf(DEBUG_INFO,
        "[PERST] hook start: fpga_drive_gpio base=0x%08x\r\n",
        (u32)FPGA_DRIVE_GPIO_BASE);

    /* Present-detect (ch2, active low: 0 = card present). Informational. */
    Pedet = Xil_In32((u32)FPGA_DRIVE_GPIO_BASE + GPIO2_DATA_OFFSET);
    XFsbl_Printf(DEBUG_INFO,
        "[PERST] pedet=0x%02x  A=%d B=%d (0=present)\r\n",
        Pedet & 0x3U,
        (int)((Pedet & PEDET_A) ? 1 : 0),
        (int)((Pedet & PEDET_B) ? 1 : 0));

    /* Assert both PERST# low (in reset), hold, then de-assert high. bit[2] held
     * 0 so SSD2 power stays enabled throughout. */
    Xil_Out32((u32)FPGA_DRIVE_GPIO_BASE + GPIO_DATA_OFFSET, 0x0U);
    XFsbl_Printf(DEBUG_INFO, "[PERST] both asserted LOW (in reset), holding %us\r\n",
                 PERST_HOLD_S);
    sleep(PERST_HOLD_S);

    Xil_Out32((u32)FPGA_DRIVE_GPIO_BASE + GPIO_DATA_OFFSET, PERST_BOTH);
    XFsbl_Printf(DEBUG_INFO, "[PERST] de-asserted HIGH (operational), settle 100ms\r\n");
    usleep(100000);

    XFsbl_Printf(DEBUG_INFO, "[PERST] hook done\r\n");

    return Status;
}
#endif

u32 XFsbl_HookBeforeHandoff(u32 EarlyHandoff)
{
    u32 Status = XFSBL_SUCCESS;

    /**
     * Add the code here
     */

    return Status;
}

/*****************************************************************************/
/**
 * This is a hook function where user can include the functionality to be run
 * before FSBL fallback happens
 *
 * @param none
 *
 * @return error status based on implemented functionality (SUCCESS by default)
 *
  *****************************************************************************/

u32 XFsbl_HookBeforeFallback(void)
{
    u32 Status = XFSBL_SUCCESS;

    /**
     * Add the code here
     */

    return Status;
}

/*****************************************************************************/
/**
 * Remove DDR IOs from retention
 *
 * @param none
 *
 * @return none
 *****************************************************************************/
static void XFsbl_IoRetentionClear(void)
{
    u32 RegVal = Xil_In32(PMU_GLOBAL_DDR_CNTRL);

    RegVal &= ~PMU_GLOBAL_DDR_CNTRL_RET_MASK;

    Xil_Out32(PMU_GLOBAL_DDR_CNTRL, RegVal);
}

/*****************************************************************************/
/**
 * This function facilitates users to define different variants of psu_init()
 * functions based on different configurations in Vivado. The default call to
 * psu_init() can then be swapped with the alternate variant based on the
 * requirement.
 *
 * @param none
 *
 * @return error status based on implemented functionality (SUCCESS by default)
 *
  *****************************************************************************/

u32 XFsbl_HookPsuInit(void)
{
    u32 Status;
#ifdef XFSBL_ENABLE_DDR_SR
    u32 RegVal;
#endif

    /* Add the code here */

#ifdef XFSBL_ENABLE_DDR_SR
    /* Check if DDR is in self refresh mode */
    RegVal = Xil_In32(XFSBL_DDR_STATUS_REGISTER_OFFSET) &
        DDR_STATUS_FLAG_MASK;
    if (RegVal) {
        Status = (u32)psu_init_ddr_self_refresh();
    } else {
        /* Remove DDR IOs from retention */
        XFsbl_IoRetentionClear();
        Status = (u32)psu_init();
    }
#else
    /* Remove DDR IOs from retention */
    XFsbl_IoRetentionClear();
    Status = (u32)psu_init();
#endif

    if (XFSBL_SUCCESS != Status) {
            XFsbl_Printf(DEBUG_GENERAL,"XFSBL_PSU_INIT_FAILED\n\r");
            /**
             * Need to check a way to communicate both FSBL code
             * and PSU init error code
             */
            Status = XFSBL_PSU_INIT_FAILED + Status;
    }

    /**
     * PS_SYSMON_ANALOG_BUS register determines mapping between SysMon supply
     * sense channel to SysMon supply registers inside the IP. This register
     * must be programmed to complete SysMon IP configuration.
     * The default register configuration after power-up is incorrect.
     * Hence, fix this by writing the correct value - 0x3210.
     */

    XFsbl_Out32(AMS_PS_SYSMON_ANALOG_BUS, PS_SYSMON_ANALOG_BUS_VAL);

    return Status;
}

/*****************************************************************************/
/**
 * This function detects type of boot based on information from
 * PMU_GLOBAL_GLOB_GEN_STORAGE1. If Power Off Suspend is supported FSBL must
 * wait for PMU to detect boot type and provide that information using register.
 * In case of resume from Power Off Suspend PMU will wait for FSBL to confirm
 * detection by writing 1 to PMU_GLOBAL_GLOB_GEN_STORAGE2.
 *
 * @return Boot type, 0 in case of cold boot, 1 for warm boot
 *
 * @note none
 *****************************************************************************/
#ifdef ENABLE_POS
u32 XFsbl_HookGetPosBootType(void)
{
    u32 WarmBoot = 0;
    u32 RegValue = 0;

    do {
        RegValue = XFsbl_In32(PMU_GLOBAL_GLOB_GEN_STORAGE1);
    } while (0U == RegValue);

    /* Clear Gen Storage register so it can be used later in system */
    XFsbl_Out32(PMU_GLOBAL_GLOB_GEN_STORAGE1, 0U);

    WarmBoot = RegValue - 1;

    /* Confirm detection in case of resume from Power Off Suspend */
    if (0 != RegValue) {
        XFsbl_Out32(PMU_GLOBAL_GLOB_GEN_STORAGE2, 1U);
    }

    return WarmBoot;
}
#endif
