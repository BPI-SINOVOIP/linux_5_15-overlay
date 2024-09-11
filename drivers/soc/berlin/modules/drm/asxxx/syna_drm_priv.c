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
#include "hal_mipi_wrap.h"

static const ENUM_PLANE_ID syna_primary_plane_id[MAX_CRTC] = {
	PLANE_GFX0,
	PLANE_GFX1,
};

VPP_BUILD_IN_FRAME_TYPE syna_get_buidin_frame_type(ENUM_PLANE_ID plane_id)
{
	return VPP_BUILD_IN_FRAME_TYPE_GFX;
}

void syna_vpp_dev_init_priv(struct drm_device *dev)
{
	syna_vpp_push_buildin_frame(PLANE_GFX0);
	syna_vpp_push_buildin_frame(PLANE_GFX1);
}

void syna_read_config_priv(vpp_config_params *p_vpp_config_param)
{
	p_vpp_config_param->active_planes = (1 << PLANE_GFX0);
	p_vpp_config_param->active_planes |= (1 << PLANE_GFX1);
}

int syna_modeset_createEntries(struct syna_drm_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int err = 0;
	ENUM_PLANE_ID plane_id;
	unsigned int plane_possible_crtc_mask;
	enum drm_plane_type plane_type;
	int panelId;

	for (plane_id = FIRST_PLANE; plane_id < MAX_CRTC; plane_id++) {
		plane_type = DRM_PLANE_TYPE_PRIMARY;
		plane_possible_crtc_mask = (1 << plane_id);
		dev_priv->plane[plane_id] = syna_plane_create(dev,
				plane_possible_crtc_mask, plane_id, plane_type);
		if (IS_ERR(dev_priv->plane[plane_id])) {
			 DRM_ERROR("failed to create a %d plane\n", plane_type);
			 err = PTR_ERR(dev_priv->plane[plane_id]);
			 goto err_syna_modeset_createEntries;
		}
	}

	for (panelId = 0; panelId < MAX_PANELS; panelId++) {
		plane_id = syna_primary_plane_id[panelId];
		dev_priv->crtc[panelId] = syna_crtc_create(dev, panelId, dev_priv->plane[plane_id]);
		if (IS_ERR(dev_priv->crtc[panelId])) {
			DRM_ERROR("failed to create a CRTC\n");
			err = PTR_ERR(dev_priv->crtc[panelId]);
			goto err_syna_modeset_createEntries;
		}
	}

	for (panelId = 0; panelId < MAX_PANELS; panelId++) {
		dev_priv->encoder[panelId] = syna_encoder_create(dev, panelId, panelId);

		if (IS_ERR(dev_priv->encoder[panelId])) {
			DRM_ERROR("failed to create a connector %d\n", panelId);
		} else {
			dev_priv->connector[panelId] = syna_lcdc_connector_create(dev);

			if (IS_ERR(dev_priv->connector[panelId])) {
				DRM_ERROR("failed to create an encoder %d\n", panelId);
			} else {
				err = drm_connector_attach_encoder(dev_priv->connector[panelId],
								dev_priv->encoder[panelId]);

				if (err) {
					DRM_ERROR
						("failed to attach [ENCODER:%d:%s] to [CONNECTOR:%d:%s] (err=%d)\n",
						dev_priv->encoder[panelId]->base.id,
						dev_priv->encoder[panelId]->name,
						dev_priv->connector[panelId]->base.id,
						dev_priv->connector[panelId]->name, err);
					goto err_syna_modeset_createEntries;
				}
			}
		}
	}
err_syna_modeset_createEntries:
	return err;
}

int syna_vpp_get_bm_details(struct dma_buf *dma_buf,
		       struct bm_pt_param *pt_param,
		       struct berlin_meta **bm_meta)
{
	return -1;
}

int syna_dsi_panel_send_cmd (unsigned int cmdsize, unsigned char *pcmd)
{
	return wrap_mipi_send_panel_cmd(cmdsize, pcmd);
}

void syna_push_buildin_frame(u32 plane)
{
	syna_vpp_push_buildin_null_frame(plane);
}
