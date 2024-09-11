// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 */

#include "drm_syna_drv.h"

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include "syna_vpp.h"
#include "syna_hdmi_config.h"
#include "vpp_api.h"

#define IS_FORCE_GFX_FRAME_TO_PIP(plane, displayMode) \
	((plane == PLANE_PIP) && (displayMode == VPP_VOUT_DUAL_MODE_PIP))

static const ENUM_PLANE_ID syna_primary_plane_id[MAX_NUM_CPCBS] = {
	PLANE_GFX1,
#ifdef USE_DOLPHIN
	PLANE_PIP,
#endif
};

VPP_BUILD_IN_FRAME_TYPE syna_get_buidin_frame_type(ENUM_PLANE_ID plane_id)
{
	return (plane_id == PLANE_MAIN ? VPP_BUILD_IN_FRAME_TYPE_VID :
			VPP_BUILD_IN_FRAME_TYPE_GFX);
}

void syna_vpp_dev_init_priv(struct drm_device *dev)
{
	syna_vpp_push_buildin_frame(PLANE_GFX1);
	syna_vpp_push_buildin_null_frame(PLANE_MAIN);
#ifdef USE_DOLPHIN
	//TBD: Is proper frame needed for CPCB2 case?
	syna_vpp_push_buildin_null_frame(PLANE_PIP);
#endif
}

void syna_read_config_priv(vpp_config_params *p_vpp_config_param)
{
	p_vpp_config_param->active_planes = (1 << PLANE_GFX1);
	p_vpp_config_param->active_planes |= (1 << PLANE_MAIN);
#ifdef USE_DOLPHIN
	p_vpp_config_param->active_planes |= (1 << PLANE_PIP);
#endif
}

enum drm_plane_type syna_modeset_getPlaneType(struct syna_drm_private *dev_priv, ENUM_PLANE_ID plane_id)
{
	ENUM_CPCB_ID cpcb_id;


	for (cpcb_id = FIRST_CPCB; cpcb_id < MAX_NUM_CPCBS; cpcb_id++) {
		if ((dev_priv->vpp_config_param.display_mode != \
			VPP_VOUT_DUAL_MODE_PIP) && \
			(cpcb_id > FIRST_CPCB))
				break;

		//Ensure one TYPE_PRIMARY per CPCB, rest can be TYPE_OVERLAY
		if (syna_primary_plane_id[cpcb_id] == plane_id)
			return DRM_PLANE_TYPE_PRIMARY;
	}

	return DRM_PLANE_TYPE_OVERLAY;
}


int syna_modeset_createEntries(struct syna_drm_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int err = 0;
	ENUM_PLANE_ID plane_id;
	ENUM_CPCB_ID cpcb_id;
	ENUM_VOUT_CONNECTOR vout_id;
	unsigned int plane_possible_crtc_mask;
	unsigned int pip_plane_possible_crtc_mask;
	enum drm_plane_type plane_type;

	plane_possible_crtc_mask = (1 << 0);

#ifdef USE_DOLPHIN
	pip_plane_possible_crtc_mask = (dev_priv->vpp_config_param.display_mode == \
							VPP_VOUT_DUAL_MODE_PIP) ? (1 << CPCB_2) :\
							(1 << CPCB_1);
#else
	pip_plane_possible_crtc_mask = plane_possible_crtc_mask;
#endif

	for (plane_id = FIRST_PLANE; plane_id < MAX_NUM_PLANES; plane_id++) {
#ifdef USE_PLATYPUS
		if (plane_id == PLANE_PIP)
			continue;
#endif
		plane_type = syna_modeset_getPlaneType(dev_priv, plane_id);
		dev_priv->plane[plane_id] = syna_plane_create(dev,
				(plane_id != PLANE_PIP) ? plane_possible_crtc_mask : \
				pip_plane_possible_crtc_mask,
				plane_id, plane_type);
		if (IS_ERR(dev_priv->plane[plane_id])) {
			 DRM_ERROR("failed to create a %d plane\n", plane_type);
			 err = PTR_ERR(dev_priv->plane[plane_id]);
			 goto err_syna_modeset_createEntries;
		}
	}

	for (cpcb_id = FIRST_CPCB; cpcb_id < MAX_NUM_CPCBS; cpcb_id++) {
		plane_id = syna_primary_plane_id[cpcb_id];
		if ((dev_priv->vpp_config_param.display_mode != \
			VPP_VOUT_DUAL_MODE_PIP) && \
			(cpcb_id > FIRST_CPCB))
				continue;

		dev_priv->crtc[cpcb_id] = syna_crtc_create(dev, cpcb_id, dev_priv->plane[plane_id]);
		if (IS_ERR(dev_priv->crtc[cpcb_id])) {
			DRM_ERROR("failed to create a CRTC\n");
			err = PTR_ERR(dev_priv->crtc[cpcb_id]);
			goto err_syna_modeset_createEntries;
		}
	}

	for (vout_id = FIRST_VOUT_CONNECTOR, cpcb_id = FIRST_CPCB;
		vout_id < MAX_NUM_VOUT_CONNECTORS; vout_id++, cpcb_id++) {
		cpcb_id %= MAX_NUM_CPCBS;

		if ((dev_priv->vpp_config_param.display_mode != \
			VPP_VOUT_DUAL_MODE_PIP) && \
			(dev_priv->vpp_config_param.display_mode !=\
			vout_id))
				continue;
		else if((dev_priv->vpp_config_param.display_mode != \
				VPP_VOUT_DUAL_MODE_PIP))
					cpcb_id = FIRST_CPCB;

		if (VOUT_CONNECTOR_HDMI == vout_id)
			dev_priv->connector[vout_id] = syna_hdmi_connector_create(dev);
		else
			dev_priv->connector[vout_id] = syna_dsi_connector_create(dev);

		if (IS_ERR(dev_priv->connector[vout_id])) {
			DRM_ERROR("failed to create a connector\n");
			err = PTR_ERR(dev_priv->connector[vout_id]);
			goto err_syna_modeset_createEntries;
		}

		dev_priv->encoder[vout_id] = syna_tmds_encoder_create(dev, vout_id, cpcb_id);
		if (IS_ERR(dev_priv->encoder[vout_id])) {
			DRM_ERROR("failed to create an encoder\n");
			err = PTR_ERR(dev_priv->encoder[vout_id]);
			goto err_syna_modeset_createEntries;
		}

		err = drm_connector_attach_encoder(dev_priv->connector[vout_id],
						   dev_priv->encoder[vout_id]);
		if (err) {
			DRM_ERROR
				("failed to attach [ENCODER:%d:%s] to [CONNECTOR:%d:%s] (err=%d)\n",
				 dev_priv->encoder[vout_id]->base.id,
				 dev_priv->encoder[vout_id]->name,
				 dev_priv->connector[vout_id]->base.id,
				 dev_priv->connector[vout_id]->name, err);
			goto err_syna_modeset_createEntries;
		}
	}

err_syna_modeset_createEntries:
	return err;
}

int syna_vpp_get_bm_details(struct dma_buf *dma_buf,
		       struct bm_pt_param *pt_param,
		       struct berlin_meta **bm_meta)
{
	int ret;

	memset(pt_param, 0, sizeof(struct bm_pt_param));

	ret = bm_fetch_pt(dma_buf, pt_param);
	if (!ret)
		*bm_meta = bm_fetch_meta(dma_buf);

	return ret;
}

int syna_dsi_panel_send_cmd (unsigned int cmdsize, unsigned char *pcmd)
{
	/* Currently VSXXX has the TA to send the commands */
	return 0;
}

void syna_push_buildin_frame(u32 plane)
{
	VPP_DISP_OUT_PARAMS dispParams;

	MV_VPP_GetDispOutParams(CPCB_1, &dispParams);

	if (plane == PLANE_GFX1 || \
			IS_FORCE_GFX_FRAME_TO_PIP(plane, dispParams.uiDisplayMode))
		syna_vpp_push_buildin_frame(plane);
	else
		syna_vpp_push_buildin_null_frame(plane);
}

MODULE_IMPORT_NS(SYNA_BM);
