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
* Local modification (ZCU104): XFsbl_HookAfterBSDownload() drives
* PERST_A#/PERST_B# (M.2 Stack FMC, via a TCA9536 I2C I/O-expander) through a
* reset cycle so a slow-to-train endpoint (DeepX M1) comes up before Linux
* probes the XDMA PCIe root complex. Everything else is stock.
*
* Unlike the uzev design (dedicated AXI IIC in the PL), the ZCU104 routes the
* FMC I2C to the PS: the TCA9536 sits on the FMC_LPC bus behind the on-board
* TCA9548A I2C switch. We therefore use the PS I2C controller (XIicPs) and
* select the FMC channel on the TCA9548A first -- the same access path the
* board's VADJ code (xfsbl_board.c) uses to reach the FMC EEPROM.
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
#include "xparameters.h"   /* XPAR_XIICPS_0_*                            */
#include "sleep.h"         /* sleep() / usleep()                         */
#include "xiicps.h"        /* PS I2C driver                              */

/************************** Constant Definitions *****************************/

/**
 * Register: PMU_GLOBAL_DDR_CNTRL
 */
#define PMU_GLOBAL_DDR_CNTRL             ( ( PMU_GLOBAL_BASEADDR ) + ((u32)0X00000070U) )
#define PMU_GLOBAL_DDR_CNTRL_RET_MASK    ((u32)0X00000001U)

/* ---- M.2 PERST# via TCA9536 I2C I/O-expander, reached over PS I2C ---- */
/* PS I2C controller that reaches the FMC (same one the VADJ code uses).
 * On the ZCU104 device index 0 is the FMC-facing controller (PS I2C1). */
#ifndef SDT
#define IIC_DEVICE     XPAR_XIICPS_0_DEVICE_ID
#else
#define IIC_DEVICE     XPAR_XIICPS_0_BASEADDR
#endif
#define IIC_SCLK       400000u  /* 400 kHz fast-mode (mux + expander)     */
#define MUX_ADDR       0x74u    /* TCA9548A I2C switch (U34)              */
#define MUX_FMC_CH     0x20u    /* channel bit that routes to FMC_LPC     */
#define EXP_ADDR       0x41u    /* TCA9536 fixed 7-bit address            */
#define EXP_IN_REG     0x00u    /* input port  (P2 = PRSNT_M2C_L, act-low)*/
#define EXP_OUT_REG    0x01u    /* output port                            */
#define EXP_CFG_REG    0x03u    /* config (0=output, 1=input)             */
#define PERST_MASK     0x03u    /* P0=PERST_A#, P1=PERST_B#               */
#define PERST_HOLD_S   1u       /* reset hold (tune down once proven)     */

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

static XIicPs IicInstance;      /* PS I2C driver instance for the hook    */

/*****************************************************************************/
/* Bring up the PS I2C controller for the PERST# sequence. 0 ok, -1 fail.    */
static int XFsbl_IicInit(void)
{
    XIicPs_Config *Cfg;

    Cfg = XIicPs_LookupConfig(IIC_DEVICE);
    if (Cfg == NULL) {
        return -1;
    }
    if (XIicPs_CfgInitialize(&IicInstance, Cfg, Cfg->BaseAddress) != (s32)XST_SUCCESS) {
        return -1;
    }
    if (XIicPs_SetSClk(&IicInstance, IIC_SCLK) != (s32)XST_SUCCESS) {
        return -1;
    }
    return 0;
}

/* Point the TCA9548A at one channel (one-byte control write). 0 ok, -1 fail. */
static int XFsbl_MuxSelect(u8 Channel)
{
    if (XIicPs_MasterSendPolled(&IicInstance, &Channel, 1, MUX_ADDR) != (s32)XST_SUCCESS) {
        XFsbl_Printf(DEBUG_GENERAL,
            "[PERST] WARN: TCA9548A channel 0x%02x select failed\r\n", Channel);
        return -1;
    }
    while (XIicPs_BusIsBusy(&IicInstance) == TRUE) {
        /* MISRA-C */
    }
    return 0;
}

/* Write one expander register (reg + data) with STOP. 0 ok, -1 on failure.  */
static int XFsbl_ExpWrite(u8 Reg, u8 Data)
{
    u8 Buf[2];
    Buf[0] = Reg;
    Buf[1] = Data;
    if (XIicPs_MasterSendPolled(&IicInstance, Buf, 2, EXP_ADDR) != (s32)XST_SUCCESS) {
        XFsbl_Printf(DEBUG_GENERAL,
            "[PERST] WARN: write reg=0x%02x data=0x%02x failed\r\n", Reg, Data);
        return -1;
    }
    while (XIicPs_BusIsBusy(&IicInstance) == TRUE) {
        /* MISRA-C */
    }
    XFsbl_Printf(DEBUG_INFO, "[PERST] wr reg=0x%02x data=0x%02x OK\r\n", Reg, Data);
    return 0;
}

/* Read one expander register (write reg ptr, then read one byte). 0 ok.      */
static int XFsbl_ExpRead(u8 Reg, u8 *Out)
{
    if (XIicPs_MasterSendPolled(&IicInstance, &Reg, 1, EXP_ADDR) != (s32)XST_SUCCESS) {
        return -1;
    }
    while (XIicPs_BusIsBusy(&IicInstance) == TRUE) {
        /* MISRA-C */
    }
    if (XIicPs_MasterRecvPolled(&IicInstance, Out, 1, EXP_ADDR) != (s32)XST_SUCCESS) {
        return -1;
    }
    while (XIicPs_BusIsBusy(&IicInstance) == TRUE) {
        /* MISRA-C */
    }
    return 0;
}

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
    int st = 0;
    u8  In = 0xFFu;

    /* The PL bitstream is now configured, so the XDMA PCIe root complex is
     * up. Cycle PERST_A#/PERST_B# (P0/P1 on the TCA9536, reached over PS I2C
     * through the TCA9548A FMC channel) so the M.2 endpoints train before
     * Linux probes the root complex. */
    XFsbl_Printf(DEBUG_INFO,
        "[PERST] hook start: PS I2C dev=%d mux=0x%02x ch=0x%02x exp=0x%02x mask=0x%02x\r\n",
        (int)IIC_DEVICE, MUX_ADDR, MUX_FMC_CH, EXP_ADDR, PERST_MASK);

    if (XFsbl_IicInit() != 0) {
        XFsbl_Printf(DEBUG_GENERAL, "[PERST] WARN: PS I2C init failed; skipping\r\n");
        return Status;
    }

    /* Route the TCA9548A to the FMC_LPC bus before touching the expander. */
    if (XFsbl_MuxSelect(MUX_FMC_CH) != 0) {
        return Status;
    }

    /* Present-detect (P2, active low: 0 = module present). Informational. */
    if (XFsbl_ExpRead(EXP_IN_REG, &In) == 0) {
        XFsbl_Printf(DEBUG_INFO,
            "[PERST] input=0x%02x  PRSNT_M2C_L(P2)=%d (0=present)\r\n",
            In, (In >> 2) & 1u);
    } else {
        XFsbl_Printf(DEBUG_GENERAL, "[PERST] WARN: input-port read failed\r\n");
    }

    /* Preload output latch low BEFORE switching to outputs (avoids a brief
     * high glitch from the latch POR default), then drive P0/P1 low. */
    st |= XFsbl_ExpWrite(EXP_OUT_REG, 0x00u);
    st |= XFsbl_ExpWrite(EXP_CFG_REG, (u8)~PERST_MASK);  /* P0,P1 = outputs */
    XFsbl_Printf(DEBUG_INFO, "[PERST] both asserted LOW (in reset), holding %us\r\n",
                 PERST_HOLD_S);
    sleep(PERST_HOLD_S);

    st |= XFsbl_ExpWrite(EXP_OUT_REG, PERST_MASK);       /* drive P0,P1 high */
    XFsbl_Printf(DEBUG_INFO, "[PERST] de-asserted HIGH (operational), settle 100ms\r\n");
    usleep(100000);

    if (st != 0) {
        XFsbl_Printf(DEBUG_GENERAL, "[PERST] WARN: one or more IIC writes failed\r\n");
    } else {
        XFsbl_Printf(DEBUG_INFO, "[PERST] sequence OK\r\n");
    }

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
