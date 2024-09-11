// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 */

#include <drm/drm_crtc.h>
#include "drm_syna_drv.h"
#include <drm/drm_panel.h>

#include "vpp_api.h"

/* To accomodate lower pixel clocks, set pixel clock with mulitplier
 * Add Corresponding divider in the Driver
 */
#define PIXEL_CLOCK_MULTIPLIER				4
#define PIXEL_CLOCK_RATE(PIX_CLK)			(PIX_CLK * PIXEL_CLOCK_MULTIPLIER * 1000)

typedef struct dsi_desc_t {
	int    outformat;
	int    bpp;
	int    byteclock;
	int    lpcmd;
	int    non_continuous_clock;
	int    lanes;
	int    eotp_tx;
	int    datapolarity;
	int    eotp_rx;
	int    h_polarity;
	int    v_polarity;
	int    data_polarity;
	int    cmdsize;
	int    htotal;
	int    video_mode;
	int    receive_ack;
	int    is_18_loose;
	int    color_coding;
	int    chunks;
	int    null_pkt;
} SYNA_DSI_DESC;

static int lcdc_rgb_swap;

static const char *compatible_name[] = {
		"syna,drm-lcdc",
		"syna,drm-dsi"
};

static int syna_encoder_parse_lcdc_dt(void)
{
	struct device_node *lcdc_node;

	lcdc_node = of_find_compatible_node(NULL, NULL, "syna,drm-lcdc");

	if (!lcdc_node) {
		DRM_ERROR("LCDC node not found \n");
		return -ENODEV;
	}

	of_property_read_u32(lcdc_node, "rgbswap",  &lcdc_rgb_swap);

	return 0;
}

static void
syna_encoder_helper_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct syna_drm_private *dev_priv = encoder->dev->dev_private;
	SYNA_LCDC_CONFIG lcdcConfig = {0};
	struct drm_display_info *disp_info = &dev_priv->connector[encoder->index]->display_info;

	if (disp_info->num_bus_formats) {
		lcdcConfig.mode = *disp_info->bus_formats;
		lcdcConfig.bits_per_pixel = disp_info->bpc;
	}

	/*htotal = hsync_len + left_margin + xres + right_margin*/
	lcdcConfig.hsync_len = mode->hsync_end - mode->hsync_start;
	lcdcConfig.right_margin = mode->hsync_start - mode->hdisplay; //FP
	lcdcConfig.xres = mode->hdisplay;
	lcdcConfig.left_margin = mode->htotal - mode->hsync_end;  //BP

	/*vtotal = vsync_len + upper_margin + yres + lower_margin*/
	lcdcConfig.vsync_len = mode->vsync_end - mode->vsync_start;
	lcdcConfig.lower_margin = mode->vsync_start - mode->vdisplay; //FP
	lcdcConfig.yres = mode->vdisplay;
	lcdcConfig.upper_margin = mode->vtotal - mode->vsync_end; //BP
	lcdcConfig.pixclock = mode->clock ;

	if (encoder->index) {
		VPP_Clock_Set_Rate_Ext(PIXEL_CLOCK_RATE(lcdcConfig.pixclock));
	} else {
		lcdcConfig.rgb_swap = lcdc_rgb_swap;
		VPP_Clock_Set_Rate(PIXEL_CLOCK_RATE(lcdcConfig.pixclock));
	}

	if(dev_priv->connector[encoder->index] && dev_priv->panel[encoder->index])
		drm_panel_prepare(dev_priv->panel[encoder->index]);

	syna_vpp_load_config(encoder->index, &lcdcConfig);
}

static void syna_encoder_helper_enable(struct drm_encoder *encoder)
{
	struct syna_drm_private *dev_priv = encoder->dev->dev_private;

	if(dev_priv->connector[encoder->index] && dev_priv->panel[encoder->index])
		drm_panel_enable(dev_priv->panel[encoder->index]);
}

static void syna_encoder_helper_disable(struct drm_encoder *encoder)
{
	struct syna_drm_private *dev_priv = encoder->dev->dev_private;

	if(dev_priv->connector[encoder->index] && dev_priv->panel[encoder->index])
		drm_panel_disable(dev_priv->panel[encoder->index]);
}

static void syna_encoder_destroy(struct drm_encoder *encoder)
{
	if (encoder == NULL) {
		DRM_ERROR("%s: encoder is NULL!!\n", __func__);
		return;
	}
	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n", encoder->base.id, encoder->name);

	drm_encoder_cleanup(encoder);

	kfree(encoder);
}

static const struct drm_encoder_helper_funcs syna_encoder_helper_funcs = {
	.mode_set = syna_encoder_helper_mode_set,
	.enable = syna_encoder_helper_enable,
	.disable = syna_encoder_helper_disable,
};

static const struct drm_encoder_funcs syna_encoder_funcs = {
	.destroy = syna_encoder_destroy,
};

struct drm_encoder *syna_encoder_create(struct drm_device *dev,
		ENUM_VOUT_CONNECTOR vout_id, ENUM_CPCB_ID cpcb_id)
{
	struct drm_encoder *encoder;
	int err;
	int encoder_type = (cpcb_id == 0) ? \
						DRM_MODE_ENCODER_DPI :\
						DRM_MODE_ENCODER_DSI;

	if (!of_find_compatible_node(NULL, NULL, compatible_name[cpcb_id])) {
		DRM_ERROR("Node not found %s\n", compatible_name[cpcb_id]);
		return ERR_PTR(-ENODEV);
	}

	syna_encoder_parse_lcdc_dt();

	encoder = kzalloc(sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return ERR_PTR(-ENOMEM);

	err = drm_encoder_init(dev, encoder, &syna_encoder_funcs,
					encoder_type, NULL);
	if (err) {
		DRM_ERROR("Failed to initialise encoder - %d", vout_id);
		kfree(encoder);
		return ERR_PTR(err);
	} else {
		encoder->index = cpcb_id;
	}

	drm_encoder_helper_add(encoder, &syna_encoder_helper_funcs);

	/*
	 * This is a bit field that's used to determine which
	 * CRTCs can drive this encoder.
	 */
	encoder->possible_crtcs = 0x1 << cpcb_id;

	DRM_DEBUG_DRIVER("[ENCODER:%d:%s]\n", encoder->base.id, encoder->name);

	return encoder;
}

void drm_syna_encoder_suspend(struct syna_drm_private *dev_priv,
							bool suspend)
{
	int panelId;

	for (panelId = 0; panelId < MAX_PANELS; panelId++) {
		if (dev_priv->connector[panelId] && dev_priv->panel[panelId]) {
			if (suspend)
				drm_panel_unprepare(dev_priv->panel[panelId]);
			else
				drm_panel_prepare(dev_priv->panel[panelId]);
		}
	}
}
