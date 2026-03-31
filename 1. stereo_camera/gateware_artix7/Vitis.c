/******************************************************************************
* Copyright (C) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/
/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */
#define __MICROBLAZE__


/*****************************************************************************/
//includes
/*****************************************************************************/
#include <stdio.h>
#include "xparameters.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xvidc.h"
#include "sleep.h"
// #include <xintc_drv_config.h>
// #include <xstatus.h>

#include "xintc.h"
#include "xcsi.h"
#include "xv_demosaic.h"
#include "xv_gamma_lut.h"
#include "xvprocss_vdma.h"
#include "xvtc.h"

/*****************************************************************************/
// defines
/*****************************************************************************/
#define HACTIVE 640
#define VACTIVE 480
#define SENSOR_WIDTH    640
#define SENSOR_HEIGHT   480
#define STITCHED_WIDTH  1280 // (640 * 2)
#define BYTES_PER_PIXEL 1    // mono8
// Total bytes per line in the destination memory
#define FULL_STRIDE     (STITCHED_WIDTH * BYTES_PER_PIXEL) 
// Bytes to transfer per sensor per line
#define SENSOR_BYTES    (SENSOR_WIDTH * BYTES_PER_PIXEL)
#define MEMORY_BASE		XPAR_MIG_0_BASEADDRESS

typedef enum
{
  XSYS_VPSS_STREAM_IN = 0,
  XSYS_VPSS_STREAM_OUT = 1
}XSys_StreamDirection;

/*****************************************************************************/
// variables
/*****************************************************************************/
int status;

XIntc IntcInstance;

XCsi_Config *csi_Config0, *csi_Config1;
XCsi Csi0, Csi1;

XV_demosaic_Config  *demosaic_Config0,*demosaic_Config1;
XV_demosaic         demosaic0, demosaic1;

XV_gamma_lut_Config     *gamma_lut_Config0, *gamma_lut_Config1;
XV_gamma_lut            gamma_lut0, gamma_lut1;

XVprocSs_Config *vpss_Config0, *vpss_Config1;
XVprocSs *vpssPtr0, *vpssPtr1;

XVtc_Config         *vtc_Config;
XVtc                vtc;

XAxiVdma_Config *vdma_Config0, *vdma_Config1, *vdma_Config2;
XAxiVdma vdma0, vdma1, vdma2;
XAxiVdma_DmaSetup WriteCfg0, WriteCfg1, ReadCfg;
XAxiVdma_FrameCounter FrameCntCfg;
volatile int FrameDoneCount = 0;
volatile int Write3FramesDone = 0;


void XSys_SetStreamParam(XVprocSs *pVprocss, u16 Direction, u16 Width,
			u16 Height, XVidC_FrameRate FrameRate,
			XVidC_ColorFormat cfmt, u16 IsInterlaced)
{
	XVidC_VideoMode resId;
	XVidC_VideoStream Stream;
	XVidC_VideoTiming const *TimingPtr;

	resId = XVidC_GetVideoModeId(Width, Height, FrameRate, IsInterlaced);
	if (resId == XVIDC_VM_NOT_SUPPORTED){
        xil_printf("ERROR:: XST_INVALID_PARAM\r\n");
		return;
    }
	TimingPtr = XVidC_GetTimingInfo(resId);

	/* Setup Video Processing Subsystem */
	Stream.VmId           = resId;
	Stream.Timing         = *TimingPtr;
	Stream.ColorFormatId  = cfmt;
	Stream.ColorDepth     = pVprocss->Config.ColorDepth;
	Stream.PixPerClk      = pVprocss->Config.PixPerClock;
	Stream.FrameRate      = FrameRate;
	Stream.IsInterlaced   = IsInterlaced;

	if (Direction == XSYS_VPSS_STREAM_IN)
		XVprocSs_SetVidStreamIn(pVprocss, &Stream);
	else
		XVprocSs_SetVidStreamOut(pVprocss, &Stream);

}

// Callback for Write VDMA Interrupt
static void WriteCallBack(void *CallbackRef, u32 Mask) {
    if (Mask & XAXIVDMA_IXR_FRMCNT_MASK) {
        FrameDoneCount++;
        
        // Check if we have finished the initial batch (e.g., 3 frames)
        if (FrameDoneCount >= 1) { // Note: If FrameCounter is set to 3, this ISR fires ONCE after 3 frames.
            Write3FramesDone = 1; 
        }
    }
}

int SetupIntrSystem(u16 WriteIntrId)
{
	/* Initialize the interrupt controller and connect the ISRs */
	status = XIntc_Initialize(&IntcInstance, XPAR_XINTC_0_BASEADDR);
	if (status != XST_SUCCESS) {
		xil_printf( "Failed init intc\r\n");
		return XST_FAILURE;
	}

    // Connect VDMA 1 (Sensor 1) to Controller
	status = XIntc_Connect(&IntcInstance, WriteIntrId, (XInterruptHandler)XAxiVdma_WriteIntrHandler, &vdma1);
	if (status != XST_SUCCESS) {
		xil_printf("Failed write channel connect intc %d\r\n", status);
		return XST_FAILURE;
	}

	/* Start the interrupt controller */
	status = XIntc_Start(&IntcInstance, XIN_REAL_MODE);
	if (status != XST_SUCCESS) {
		xil_printf( "Failed to start intc\r\n");
		return XST_FAILURE;
	}

	/* Enable interrupts from the hardware */
	XIntc_Enable(&IntcInstance, WriteIntrId);

	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler)XIntc_InterruptHandler,
			(void *)&IntcInstance);

	Xil_ExceptionEnable();

	/* Register call-back functions	 */
	XAxiVdma_SetCallBack(&vdma1, XAXIVDMA_HANDLER_GENERAL, WriteCallBack, (void *)&vdma1, XAXIVDMA_WRITE);

	return XST_SUCCESS;
}

// reset: Scenario A: It connects to a Processor System Reset block (which connects to the Zynq PS).
// Result: You do not strictly need the code. The IP resets when the board boots. However, if the video crashes, you have to reboot the whole board.
// no reset function here, not Scenario B: It connects to an AXI GPIO block.


int init_config(){
    // mipi csi
    csi_Config0 = XCsi_LookupConfig(XPAR_XV_CSC_0_BASEADDR);
    if (!csi_Config0) {
        xil_printf("ERROR:: Csi device0 not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    csi_Config1 = XCsi_LookupConfig(XPAR_XV_CSC_1_BASEADDR);
    if (!csi_Config1) {
        xil_printf("ERROR:: Csi device1 not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    status = XCsi_CfgInitialize(&Csi0, csi_Config0, csi_Config0->BaseAddr);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: Csi0 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }
    status = XCsi_CfgInitialize(&Csi1, csi_Config1, csi_Config1->BaseAddr);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: Csi1 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }

    // demosaic
    demosaic_Config0 = XV_demosaic_LookupConfig(XPAR_XV_DEMOSAIC_0_BASEADDR);
    if(!demosaic_Config0)
    {
        xil_printf("ERROR:: Demosaic0 device not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    demosaic_Config1 = XV_demosaic_LookupConfig(XPAR_XV_DEMOSAIC_1_BASEADDR);
    if(!demosaic_Config1)
    {
        xil_printf("ERROR:: Demosaic1 device not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    status = XV_demosaic_CfgInitialize(&demosaic0, demosaic_Config0, demosaic_Config0->BaseAddress);
    if(status != XST_SUCCESS)
    {
        xil_printf("ERROR:: Demosaic0 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }
    status = XV_demosaic_CfgInitialize(&demosaic1, demosaic_Config1, demosaic_Config1->BaseAddress);
    if(status != XST_SUCCESS)
    {
        xil_printf("ERROR:: Demosaic1 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }
    
    // gamma
    gamma_lut_Config0 = XV_gamma_lut_LookupConfig(XPAR_XV_GAMMA_LUT_0_BASEADDR);
    if(!gamma_lut_Config0)
    {
        xil_printf("ERROR:: Gamma LUT device0 not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    gamma_lut_Config1 = XV_gamma_lut_LookupConfig(XPAR_XV_GAMMA_LUT_1_BASEADDR);
    if(!gamma_lut_Config1)
    {
        xil_printf("ERROR:: Gamma LUT device1 not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    status = XV_gamma_lut_CfgInitialize(&gamma_lut0, gamma_lut_Config0, gamma_lut_Config0->BaseAddress);
    if(status != XST_SUCCESS)
    {
        xil_printf("ERROR:: Gamma LUT0 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }
    status = XV_gamma_lut_CfgInitialize(&gamma_lut1, gamma_lut_Config1, gamma_lut_Config1->BaseAddress);
    if(status != XST_SUCCESS)
    {
        xil_printf("ERROR:: Gamma LUT1 Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }
    
    //vpss
    vpss_Config0 = XVprocSs_LookupConfig(XPAR_XVPROCSS_0_BASEADDR);
    if(!vpss_Config0)
    {
        xil_printf("ERR:: VprocSs device0 not found\r\n");
        return (XST_DEVICE_NOT_FOUND);
    }
    vpss_Config1 = XVprocSs_LookupConfig(XPAR_XVPROCSS_1_BASEADDR);
    if(!vpss_Config1)
    {
        xil_printf("ERR:: VprocSs device1 not found\r\n");
        return (XST_DEVICE_NOT_FOUND);
    }
    status = XVprocSs_CfgInitialize(vpssPtr0, vpss_Config0, vpss_Config0->BaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: VprocSs0 Initialization failed %d\r\n", status);
        return XST_FAILURE;
    }
    status = XVprocSs_CfgInitialize(vpssPtr1, vpss_Config1, vpss_Config1->BaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: VprocSs1 Initialization failed %d\r\n", status);
        return XST_FAILURE;
    }

    // vtc
    vtc_Config = XVtc_LookupConfig(XPAR_XVTC_0_BASEADDR);
    if(!vtc_Config) {
        xil_printf("ERROR:: VTC device not found\r\n");
        return(XST_DEVICE_NOT_FOUND);
    }
    status = XVtc_CfgInitialize(&vtc, vtc_Config, vtc_Config->BaseAddress);
    if(status != XST_SUCCESS) {
        xil_printf("ERROR:: VTC Initialization failed %d\r\n", status);
        return(XST_FAILURE);
    }

    //vdma
    vdma_Config0 = XAxiVdma_LookupConfig(XPAR_AXI_VDMA_0_BASEADDR);
	if (!vdma_Config0) {
		xil_printf("ERROR:: VDMA device0 not found %d\r\n" );
		return (XST_DEVICE_NOT_FOUND);
    }
    status = XAxiVdma_CfgInitialize(&vdma0, vdma_Config0, vdma_Config0->BaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: VDMA Initialization failed %d\r\n", status);
        return XST_FAILURE;
    }
    vdma_Config1 = XAxiVdma_LookupConfig(XPAR_AXI_VDMA_1_BASEADDR);
	if (!vdma_Config1) {
		xil_printf("ERROR:: VDMA device1 not found %d\r\n" );
		return (XST_DEVICE_NOT_FOUND);
    }
    status = XAxiVdma_CfgInitialize(&vdma1, vdma_Config1, vdma_Config1->BaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: VDMA Initialization failed %d\r\n", status);
        return XST_FAILURE;
    }
    vdma_Config2 = XAxiVdma_LookupConfig(XPAR_AXI_VDMA_2_BASEADDR);
	if (!vdma_Config2) {
		xil_printf("ERROR:: VDMA device2 not found %d\r\n" );
		return (XST_DEVICE_NOT_FOUND);
    }
    status = XAxiVdma_CfgInitialize(&vdma2, vdma_Config2, vdma_Config2->BaseAddress);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: VDMA Initialization failed %d\r\n", status);
        return XST_FAILURE;
    }
    
    return(XST_SUCCESS);
}


int main(){   
    // cache
    #ifdef __MICROBLAZE__
    #ifdef XPAR_MICROBLAZE_USE_ICACHE
        Xil_ICacheEnable();
    #endif
    #ifdef XPAR_MICROBLAZE_USE_DCACHE
        Xil_DCacheEnable();
    #endif
    #endif
    
    // init all the configs from hardware
    init_config();

/*****************************************************************************/
    // start write VDMAs
    // vdma
    // ==========================================================
    // 1. Configure SENSOR 1 (Left Side of Image)
    // ==========================================================
    WriteCfg0.VertSizeInput = SENSOR_HEIGHT;
    WriteCfg0.HoriSizeInput = SENSOR_BYTES;
    WriteCfg0.Stride = FULL_STRIDE;
    WriteCfg0.FrameDelay = 0;
    WriteCfg0.EnableCircularBuf = 1; // Circular Mode
    WriteCfg0.EnableSync = 1;        // Genlock (Master), Software: must set EnableSync to 1 for all, but configure FrameDelay differently.
    WriteCfg1.GenLockRepeat = 0; // Don't repeat, keep writing new data
    WriteCfg0.PointNum = 0;
    WriteCfg0.EnableFrameCounter = 0;
    WriteCfg0.FixedFrameStoreAddr = 0;

    // Assign addresses for 3 buffers
    for (int i = 0; i < 3; i++) {
        WriteCfg0.FrameStoreStartAddr[i] = MEMORY_BASE +  (i * FULL_STRIDE * SENSOR_HEIGHT);
    }
    
    status = XAxiVdma_DmaConfig(&vdma0, XAXIVDMA_WRITE, &WriteCfg0);
    if (status != XST_SUCCESS) return XST_FAILURE;

    XAxiVdma_DmaSetBufferAddr(&vdma0, XAXIVDMA_WRITE, WriteCfg1.FrameStoreStartAddr);

   
    // ==========================================================
    // 2. Configure SENSOR 2 (Right Side of Image)
    // ==========================================================
    WriteCfg1.VertSizeInput = SENSOR_HEIGHT;
    WriteCfg1.HoriSizeInput = SENSOR_BYTES;   // Transfer 640 * 4 bytes
    WriteCfg1.Stride        = FULL_STRIDE;    // Jump 1280 * 4 bytes to next line
    
    WriteCfg1.FrameDelay = 0; //Must write to the SAME buffer index as VDMA 1 at the same time. Therefore, FrameDelay = 0.
    WriteCfg1.EnableCircularBuf = 1;
    WriteCfg1.EnableSync = 1;
    WriteCfg1.GenLockRepeat = 0; // Don't repeat, keep writing new data
    WriteCfg1.PointNum = 0;
    WriteCfg1.EnableFrameCounter = 1; // <-- only second sensor has frame counter
    WriteCfg1.FixedFrameStoreAddr = 0;

    // Set Addresses for Sensor 2 (Starts at offset 640 pixels)
    for (int i = 0; i < 3; i++) {
        // Calculate Base Frame Address + Offset for Right half
        u32 FrameStart = MEMORY_BASE + (i * FULL_STRIDE * SENSOR_HEIGHT);
        WriteCfg1.FrameStoreStartAddr[i] = FrameStart + SENSOR_BYTES; 
    }

    status = XAxiVdma_DmaConfig(&vdma1, XAXIVDMA_WRITE, &WriteCfg1);
    if (status != XST_SUCCESS) return XST_FAILURE;

    // Set buffer addresses in hardware
    XAxiVdma_DmaSetBufferAddr(&vdma1, XAXIVDMA_WRITE, WriteCfg1.FrameStoreStartAddr);

    // ==========================================================
    // 3. Configure READER (Reads the full stitched frame)
    // ==========================================================
    ReadCfg.VertSizeInput = SENSOR_HEIGHT;      // 480 lines
    ReadCfg.HoriSizeInput = FULL_STRIDE;        // Read full 1280 * 4 bytes
    ReadCfg.Stride        = FULL_STRIDE;        // Stride matches width
    
    ReadCfg.FrameDelay = 1; // Must read from a DIFFERENT buffer than the one being written (to avoid tearing). Usually, it reads the previous frame. 
    ReadCfg.EnableCircularBuf = 1;
    ReadCfg.EnableSync = 1;
    ReadCfg.GenLockRepeat = 1;      // If sensor stops, keep showing the last valid frame.
    ReadCfg.PointNum = 0;
    ReadCfg.EnableFrameCounter = 0;
    ReadCfg.FixedFrameStoreAddr = 0;

    // Reader sees the buffer starting at 0, containing combined data
    for (int i = 0; i < 3; i++) {
        ReadCfg.FrameStoreStartAddr[i] = MEMORY_BASE + (i * FULL_STRIDE * SENSOR_HEIGHT);
    }

    status = XAxiVdma_DmaConfig(&vdma2, XAXIVDMA_READ, &ReadCfg);
    if (status != XST_SUCCESS) return XST_FAILURE;

    // Set buffer addresses in hardware
    XAxiVdma_DmaSetBufferAddr(&vdma2, XAXIVDMA_READ, ReadCfg.FrameStoreStartAddr);


    // --- 4. Configure Interrupts for Write VDMA ---
    // We want ONE interrupt after 3 frames are written
    FrameCntCfg.ReadFrameCount = 1;  // Even if you don't use interrupts for reading, the hardware needs a value > 0.
    FrameCntCfg.ReadDelayTimerCount = 0; // Disable timeout interrupts
    FrameCntCfg.WriteFrameCount = 3; // Interrupt triggers after 3 frames
    FrameCntCfg.WriteDelayTimerCount = 0;

    XAxiVdma_SetFrameCounter(&vdma1, &FrameCntCfg);
    // Enable Interrupts (Ensure your Intc is set up to call WriteCallBack)
    XAxiVdma_IntrEnable(&vdma1, XAXIVDMA_IXR_FRMCNT_MASK, XAXIVDMA_WRITE); // By default, VDMA keeps interrupts silent. this line allows sending the Frame Count signal to the Interrupt Controller.

    SetupIntrSystem(XPAR_INTC_0_VDMA_1_S2MM_INTROUT_VEC_ID);

    // --- 5. Start Sequence ---
    /* Start the DMA engine to transfer */
	status =  XAxiVdma_DmaStart(&vdma1, XAXIVDMA_WRITE); // start the slave first
	if (status != XST_SUCCESS) {
        xil_printf("DMA1 start failed Error\r\n");
		return XST_FAILURE;
	}
    status = XAxiVdma_DmaStart(&vdma0, XAXIVDMA_WRITE); // start the master later to kick off slave
	if (status != XST_SUCCESS) {
        xil_printf("DMA0 start failed Error\r\n");
		return XST_FAILURE;
	}

    xil_printf("write VDMA started\r\n");
/*****************************************************************************/
    // vpss
    XSys_SetStreamParam(vpssPtr0, XSYS_VPSS_STREAM_IN, HACTIVE, VACTIVE, 200, XVIDC_CSF_RGB, FALSE);
    XSys_SetStreamParam(vpssPtr0, XSYS_VPSS_STREAM_OUT, HACTIVE, VACTIVE, 200, XVIDC_CSF_YCRCB_422, FALSE);
    XVprocSs_SetSubsystemConfig(vpssPtr0);
    XVprocSs_Start(vpssPtr0);
    XSys_SetStreamParam(vpssPtr1, XSYS_VPSS_STREAM_IN, HACTIVE, VACTIVE, 200, XVIDC_CSF_RGB, FALSE);
    XSys_SetStreamParam(vpssPtr1, XSYS_VPSS_STREAM_OUT, HACTIVE, VACTIVE, 200, XVIDC_CSF_YCRCB_422, FALSE);
    XVprocSs_SetSubsystemConfig(vpssPtr1);
    XVprocSs_Start(vpssPtr1);
    xil_printf("VPSS Activated: RGB -> YUV422\r\n");
    // gamma lut
    XV_gamma_lut_Set_HwReg_width(&gamma_lut0, HACTIVE);
    XV_gamma_lut_Set_HwReg_height(&gamma_lut0, VACTIVE);
    XV_gamma_lut_Set_HwReg_video_format(&gamma_lut0, 0);
    XV_gamma_lut_EnableAutoRestart(&gamma_lut0);
    XV_gamma_lut_Start(&gamma_lut0);
    XV_gamma_lut_Set_HwReg_width(&gamma_lut1, HACTIVE);
    XV_gamma_lut_Set_HwReg_height(&gamma_lut1, VACTIVE);
    XV_gamma_lut_Set_HwReg_video_format(&gamma_lut1, 0);
    XV_gamma_lut_EnableAutoRestart(&gamma_lut1);
    XV_gamma_lut_Start(&gamma_lut1);
    xil_printf("gamma lut Activated\r\n");
    // demosaic
    XV_demosaic_Set_HwReg_width(&demosaic0, HACTIVE);
    XV_demosaic_Set_HwReg_height(&demosaic0, VACTIVE);
    XV_demosaic_Set_HwReg_bayer_phase(&demosaic0, 0);
    XV_demosaic_EnableAutoRestart(&demosaic0);
    XV_demosaic_Start(&demosaic0);
    XV_demosaic_Set_HwReg_width(&demosaic1, HACTIVE);
    XV_demosaic_Set_HwReg_height(&demosaic1, VACTIVE);
    XV_demosaic_Set_HwReg_bayer_phase(&demosaic1, 0);
    XV_demosaic_EnableAutoRestart(&demosaic1);
    XV_demosaic_Start(&demosaic1);
    xil_printf("demosaic Activated\r\n");
    // csi2rx
    status = XCsi_Configure(&Csi0); //configure activate lanes
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: CSI-2 Rx configuration0 with 4 lanes failed.\r\n");
        return XST_FAILURE;
    }
    status = XCsi_Configure(&Csi1); //configure activate lanes
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: CSI-2 Rx configuration1 with 4 lanes failed.\r\n");
        return XST_FAILURE;
    }

    status = XCsi_Activate(&Csi0, XCSI_ENABLE);     // This releases the internal reset and enables the D-PHY.
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: CSI-2 Rx activation0 with 4 lanes failed.\r\n");
        return XST_FAILURE;
    }
    status = XCsi_Activate(&Csi1, XCSI_ENABLE);     // This releases the internal reset and enables the D-PHY.
    if (status != XST_SUCCESS) {
        xil_printf("ERROR:: CSI-2 Rx activation1 with 4 lanes failed.\r\n");
        return XST_FAILURE;
    }
    xil_printf("CSI-2 Rx Activated\r\n");
/*****************************************************************************/
    // TODO: send trigger to USB code to configure I2C and start sensor

    // waiting for triple buffer
    xil_printf("Waiting for buffering...\r\n");

    // Wait for 3 complete frames to settle in DDR
    while(!Write3FramesDone) {
        asm("nop"); // Just wait
    }

    // start read vdma then vtc
    status =  XAxiVdma_DmaStart(&vdma2, XAXIVDMA_READ);
	if (status != XST_SUCCESS) {
        xil_printf("DMA2 start failed Error\r\n");
		return XST_FAILURE;
	}

    // vtc, no change from vivado config
    XVtc_Enable(&vtc);
    XVtc_EnableGenerator(&vtc);
    xil_printf("VTC Configured\r\n");

/*****************************************************************************/
	/* Infinite while loop to let it run */
	while(1){};

    #ifdef __MICROBLAZE__
    #ifdef XPAR_MICROBLAZE_USE_DCACHE
        Xil_DCacheDisable();
    #endif
    #ifdef XPAR_MICROBLAZE_USE_ICACHE
        Xil_ICacheDisable();
    #endif
    #endif
    return 0;
}
