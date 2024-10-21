// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Synaptics Incorporated */

#include "vpp_api.h"
#include "vpp_isr.h"
#include "hal_dhub_wrap.h"
#include "hal_vpp_wrap.h"
#include "avio_sub_module.h"
#include "vpp_res_info.h"
#include "hal_mipi_wrap.h"

#include "linux/delay.h"

#define VPP_CPCBTG_RESET_WAIT_TIME_MS	(100)
#define VPP_CPCBTG_RESET_LOOP_DELAY_MS	(10)

static mrvl_frame_size curr_input_frame_size[MAX_NUM_PLANES];
static VPP_DISP_OUT_PARAMS curr_disp_res_params[MAX_NUM_CPCBS];
vpp_config_params vpp_config_param = { 0 };

int MV_VPP_make_frame_data(unsigned int iVideo, unsigned int *pStartAddr,
			  unsigned int uiPicH, unsigned int uiLineV, unsigned int uiWidth,
			  unsigned int uiHeight, unsigned int uiPicA, unsigned int uiPicB)
{
	unsigned int *Ptr;
	unsigned int VLines, VRep, HS, HRep, PixVal, VBlock, HBlock, PixelPerWord;
	int Ret = 0;

	if (!pStartAddr) {
		pr_err("invalid input parameters\n");
		return -1;
	}

	/**
	 * make the whole frame data
	 */
	Ptr = pStartAddr;

	if (iVideo)
		PixelPerWord = 2;
	else
		PixelPerWord = 1;

	VBlock = uiLineV;

	for (VLines = 0; VLines < uiHeight; VLines += VBlock) {
		if ((uiHeight - VLines) < VBlock)
			VBlock = uiHeight - VLines;

		for (VRep = 0; VRep < VBlock; VRep++) {
			HBlock = uiPicH * PixelPerWord;

			for (HS = 0; HS < uiWidth; HS += HBlock) {
				if ((VLines / uiLineV +
						HS/(uiPicH * PixelPerWord))
						& 0x1)
					PixVal = uiPicB;
				else
					PixVal = uiPicA;

				if ((uiWidth - HS) < HBlock)
					HBlock = uiWidth - HS;

				for (HRep = 0; HRep < HBlock/PixelPerWord;
						HRep++)
					*Ptr++ = PixVal;
			}
		}
	}
	return Ret;
}

static void MV_VPP_GetFrameSize(ENUM_PLANE_ID plane_id, int *pWidth, int *pHeight)
{
	*pWidth = curr_input_frame_size[plane_id].width;
	*pHeight = curr_input_frame_size[plane_id].height;
}

void MV_VPP_GetOutResolutionSize(ENUM_CPCB_ID cpcbID, int *p_width, int *p_height)
{
	VPP_RESOLUTION_DESCRIPTION  ResDesc;
	int res;

	res = wrap_MV_VPPOBJ_GetResolutionDescription(curr_disp_res_params[cpcbID].uiResId, &ResDesc);
	if (!res) {
		*p_width = ResDesc.uiActiveWidth;
		*p_height = ResDesc.uiActiveHeight;
	} else {
		/* Default values to FB size in case of failure */
        MV_VPP_GetFrameSize(PLANE_GFX1, p_width, p_height);
	}
}

void MV_VPP_GetInputFrameSize(ENUM_PLANE_ID plane_id, int *width, int *height)
{
	int res_width, res_height;

	MV_VPP_GetFrameSize(plane_id, width, height);
	MV_VPP_GetOutResolutionSize(CPCB_1, &res_width, &res_height);
	if (res_width < res_height)
		swap(*width, *height);
}

int MV_VPP_SetInputFrameSize(ENUM_PLANE_ID plane_id, int width, int height, bool isFromISR, bool bApply)
{
	int ret = 0;
	VPP_WIN fb_win;

	/* For NULL Frame no need to Update the window */
	if (!width || !height)
		return 0;

	if (curr_input_frame_size[plane_id].width != width ||
	        curr_input_frame_size[plane_id].height != height) {

		if (bApply) {
			if (isFromISR) {
				fb_win.x = 0;
				fb_win.y = 0;
				fb_win.width = width;
				fb_win.height = height;

				ret = wrap_MV_VPPOBJ_SetRefWindowFromISR(plane_id, &fb_win);
				if (ret) {
					pr_err("Failed to set ref window from ISR %d\n",
							plane_id);
					return ret;
				}
			}
		}

		curr_input_frame_size[plane_id].width = width;
		curr_input_frame_size[plane_id].height = height;
	}
	return ret;
}

int MV_VPP_GetDispOutParams(int cpcbId, VPP_DISP_OUT_PARAMS* pDisplayOutParams)
{
	int result = MV_VPP_EBADPARAM;

	if (pDisplayOutParams != NULL) {
		memcpy(pDisplayOutParams, &curr_disp_res_params[cpcbId], sizeof(VPP_DISP_OUT_PARAMS));
		result = MV_VPP_OK;
	}

	return result;
}

int MV_VPP_SetDisplayResolution(ENUM_CPCB_ID cpcbID,
		VPP_DISP_OUT_PARAMS dispParams, int bApply)
{
	int res = MV_VPP_OK;
	int pixel_clock;
	int status = -1;
	int wait_count = VPP_CPCBTG_RESET_WAIT_TIME_MS / VPP_CPCBTG_RESET_LOOP_DELAY_MS;

	if (curr_disp_res_params[cpcbID].uiResId != dispParams.uiResId ||
			curr_disp_res_params[cpcbID].uiDisplayMode != dispParams.uiDisplayMode ||
			curr_disp_res_params[cpcbID].uiBitDepth != dispParams.uiBitDepth ||
			curr_disp_res_params[cpcbID].uiColorFmt != dispParams.uiColorFmt) {

		/* First : put CPCB TG to reset before setting new timing */
		res = wrap_MV_VPPOBJ_GetBlockStatus(VPP_BLOCK_CPCB_TG, cpcbID, &status);
		if (res) {
			pr_err("%s %d> CPCB Status get failed\n", __FUNCTION__, __LINE__);
			return res;
		}

		if (status != STATUS_INACTIVE) {
			res = dispParams.uiResId;
			dispParams.uiResId = RES_RESET;
			wrap_MV_VPPOBJ_SetFormat(cpcbID, &dispParams);
			dispParams.uiResId = res;

			/* Wait until CPCB TG is reset, otherwise timeout after 100 ms */
			do {
				res = wrap_MV_VPPOBJ_GetBlockStatus(VPP_BLOCK_CPCB_TG, cpcbID, &status);
				if (status == STATUS_INACTIVE)
					break;
				else
					msleep(VPP_CPCBTG_RESET_LOOP_DELAY_MS);

				wait_count--;
			} while (wait_count);
		}

		if (!wait_count) {
			pr_err("%s %d> Reset Failed\n", __FUNCTION__, __LINE__);
			return MV_VPP_EIOFAIL;
		}

		memcpy(&curr_disp_res_params[cpcbID], &dispParams, sizeof(VPP_DISP_OUT_PARAMS));

		if (bApply) {
			//SetDisplayWindow applied to all planes by SetFormat
			wrap_MV_VPPOBJ_GetCPCBOutputPixelClock(dispParams.uiResId, &pixel_clock);

			if ((MAX_NUM_CPCBS == 1) || \
				((cpcbID == CPCB_1) &&\
				(!IS_MIPI_ONLY_MODE(dispParams.uiDisplayMode))))
					res = VPP_Clock_Set_Rate(pixel_clock*1000);
			else
				res = VPP_Clock_Set_Rate_Ext(pixel_clock*1000);

			res = wrap_MV_VPPOBJ_SetFormat(cpcbID, &dispParams);
			if (res != MV_VPP_OK) {
				pr_err("%s:%d: wrap_MV_VPPOBJ_SetFormat FAILED, error: 0x%x\n",
					__func__, __LINE__, res);
			} else {
				pr_info("%s %d> resiD %d cpcbID %d pixel clock %d\n",
					__FUNCTION__, __LINE__, dispParams.uiResId,
					cpcbID, pixel_clock);
			}
		}
	}

	return res;
}

void MV_VPP_DisplayFrame(int uiPlaneId, int isVideoFormat, VBUF_INFO *pVppDesc)
{
	if (!VPP_Is_Recovery_Mode()) {
		if (isVideoFormat)
			wrap_MV_VPPOBJ_DisplayFrame(uiPlaneId, pVppDesc);
		else
			wrap_MV_VPPOBJ_SetStillPicture(uiPlaneId, pVppDesc);
	} else {
#ifndef VPP_ENABLE_USE_SET_STILL_PICTURE
		VPP_PushFrameToInputQueue(uiPlaneId,
			VPP_FRAMEQ_MSGT_DISPLAY_FRAME, pVppDesc);
#else
		VPP_PushFrameToInputQueue(uiPlaneId,
			VPP_FRAMEQ_MSGT_STILL_PICTURE, pVppDesc);
#endif
	}
}

int MV_VPP_SetHdmiTxControl(int enable)
{
	return wrap_MV_VPPOBJ_SetHdmiTxControl(enable);
}

static int VPP_Init_Normal_vpp_ta(vpp_config_params vpp_config_params)
{
	VPP_DISP_OUT_PARAMS pdispParams[MAX_NUM_CPCBS];
	unsigned int vppInitParam[2];
	int ret = MV_VPP_OK, i;

	if (!VPP_Is_Recovery_Mode()) {
		vppInitParam[0] = VPP_CA_INIT_MAGIC_NUM;
		vppInitParam[1] = 0;

		ret = wrap_MV_VPP_InitVPPS(TA_UUID_VPP, vppInitParam);

		if (ret) {
			pr_err("%s:%d InitVPPS FAILED, error: 0x%x\n", __func__, __LINE__, ret);
			return -ENODEV;
		}

		// Retrieve following details from VPP TA and initialize disp params
		ret = wrap_MV_VPPOBJ_GetDispOutParams(pdispParams, MAX_NUM_CPCBS * sizeof(VPP_DISP_OUT_PARAMS));
		if (!ret) {
			for (i = CPCB_1; i < MAX_NUM_CPCBS; i++) {
				MV_VPP_SetDisplayResolution(i, pdispParams[i], 0);
			}
		}
	}

	return ret;
}

int __weak wrap_VPP_Init_Recovery(VPP_MEM_LIST *vpp_shm_list,
			int is_ampless_boot, vpp_config_params vpp_config_param)
{
	return 0;
}

void __weak wrap_VPP_DeInit_Recovery(void)
{
	return;
}

int __weak wrap_mipi_initialize(struct device *dev,
				vpp_config_params vpp_config_param)
{
	return 0;
}

void __weak wrap_mipi_deinit(void)
{
	return;
}

int MV_VPP_Init(VPP_MEM_LIST *shm_list, vpp_config_params vpp_config_params)
{
	int res = 0;

	memcpy(&vpp_config_param, &vpp_config_params, sizeof(vpp_config_params));
	if (VPP_Is_Recovery_Mode()) {
		pr_info("MV_VPP_Init - Normal/Recovery - libfastlogo.ta \n");
		res = avio_sub_module_dhub_init();
		if (res) {
			pr_info("%s: dhub open failed: %x\n", __func__, res);
			return res;
		}
		VPP_EnableDhubInterrupt(true);

		res = wrap_VPP_Init_Recovery(shm_list, is_ampless_boot(), vpp_config_params);

		if (!res)
			res = wrap_mipi_initialize(shm_list->dev, vpp_config_params);

		if (!res)
			VPP_CreateISRTask();
	} else {
		pr_info("MV_VPP_Init - Normal - libvpp.ta\n");
		res = VPP_Init_Normal_vpp_ta(vpp_config_params);
	}

	return res;
}

void MV_VPP_Deinit(void)
{
	if (VPP_Is_Recovery_Mode())
		VPP_EnableDhubInterrupt(false);
	wrap_MV_VPPOBJ_Destroy();
	if (VPP_Is_Recovery_Mode()) {
		VPP_StopISRTask();
		wrap_VPP_DeInit_Recovery();
	}

	wrap_mipi_deinit();
	wrap_MV_VPP_DeInit();
}

int __weak syna_get_res_index(int active_width, int active_height, int scan, int freq, int fps)
{
	return -1;
}

int MV_VPP_GetResIndex(int active_width, int active_height, int scan, int freq, int fps)
{
	return syna_get_res_index(active_width, active_height, scan, freq, fps);
}

EXPORT_SYMBOL(MV_VPP_Init);
EXPORT_SYMBOL(MV_VPP_DisplayFrame);
EXPORT_SYMBOL(MV_VPP_make_frame_data);
EXPORT_SYMBOL(MV_VPP_GetInputFrameSize);
EXPORT_SYMBOL(MV_VPP_SetInputFrameSize);
EXPORT_SYMBOL(MV_VPP_GetOutResolutionSize);
EXPORT_SYMBOL(MV_VPP_SetDisplayResolution);
EXPORT_SYMBOL(MV_VPP_SetHdmiTxControl);
EXPORT_SYMBOL(MV_VPP_GetResIndex);
EXPORT_SYMBOL(MV_VPP_GetDispOutParams);