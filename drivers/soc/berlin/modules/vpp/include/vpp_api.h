// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Synaptics Incorporated */

#ifndef _VPP_API_H_
#define _VPP_API_H_

#include "vpp_mem.h"
#include "vpp_config.h"
#include "vpp_fb.h"
#include "vpp_defines.h"
#include "vpp_vbuf.h"
#include "hal_vpp.h"

#define VPP_SHM_4K_ALIGN_SIZE 4096
#define VPP_SHM_4K_ALIGN_ROUNDUP(size)  ((size + VPP_SHM_4K_ALIGN_SIZE - 1) & \
						(~(VPP_SHM_4K_ALIGN_SIZE - 1)))

#define VPP_SHM_STATIC		1
#define VPP_SHM_VIDEO_FB	2

#define MV_VPP_PANEL_RESOLUTION_NDX_START	2
#define VPP_FEATURE_HDMITX	(1<<0)
#define VPP_FEATURE_MIPI_DSI	(1<<1)
#define MAX_NUM_FEATURE_CFG	1

#define DEFAULT_CPCB_1_RESOLUTION_ID   RES_1080P60
#define IS_MIPI_ONLY_MODE(MODE)   (MODE == VPP_VOUT_SINGLE_MODE_SEC)

#define VPP_CA_INIT_MAGIC_NUM 0xFACE

#define HDMI_MAX_RES_ENABLED_60_30  61
#define HDMI_MAX_RES_ENABLED_50_25  59

#define VPP_FRAMEQ_MSGT_DISPLAY_FRAME	0x0
#define VPP_FRAMEQ_MSGT_STILL_PICTURE	0x1

/*Enable the macro when planes other than GFX is used for display with fastlogo.ta*/
//#define VPP_ENABLE_USE_SET_STILL_PICTURE

typedef void *SHM_HANDLE;

typedef struct mrvl_frame_size_t {
	int width;
	int height;
} mrvl_frame_size;


int MV_VPP_make_frame_data(unsigned int iVideo, unsigned int *pStartAddr,
			  unsigned int uiPicH, unsigned int uiLineV, unsigned int uiWidth,
			  unsigned int uiHeight, unsigned int uiPicA, unsigned int uiPicB);
int MV_VPP_Init(VPP_MEM_LIST *shm_list, vpp_config_params vpp_config_param);
void MV_VPP_Deinit(void);
void MV_VPP_DisplayFrame(int uiPlaneId, int isVideoFormat, VBUF_INFO *pVppDesc);
void MV_VPP_GetInputFrameSize(ENUM_PLANE_ID plane_id, int *width, int *height);
int MV_VPP_SetInputFrameSize(ENUM_PLANE_ID plane_id, int width, int height, bool isFromISR, bool bApply);
int MV_VPP_SetDisplayResolution(ENUM_CPCB_ID cpcbID,
		VPP_DISP_OUT_PARAMS dispParams, int bApply);
void MV_VPP_GetOutResolutionSize(ENUM_CPCB_ID cpcbID, int *p_width, int *p_height);
int MV_VPP_SetHdmiTxControl(int enable);
int MV_VPP_Config(ENUM_CPCB_ID cpcbID, ENUM_PLANE_ID plane_id, bool isVideo);
int MV_VPP_GetResIndex(int active_width, int active_height, int scan, int freq, int fps);
int is_vpp_driver_initialized(void);
int VPP_Clock_Set_Rate(unsigned int clk_rate);
int VPP_Clock_Set_Rate_Ext(unsigned int clk_rate);
int VPP_Is_Recovery_Mode(void);
int VPP_Is_Vpp_Ta(void);
void vpp_force_enable_recovery(bool enable);
int is_ampless_boot(void);
int MV_VPP_GetDispOutParams(int cpcbId, VPP_DISP_OUT_PARAMS* pDisplayOutParams);
#endif //_VPP_API_H_
