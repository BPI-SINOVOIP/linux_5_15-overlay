// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Synaptics Incorporated
 *
 */
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include "drm_syna_drv.h"
#include "drm_syna_gem.h"
#include "syna_vpp.h"
#include "syna_drm_plane_priv.h"
#include "drm_syna_port.h"

static void syna_plane_set_surface(struct drm_crtc *crtc, struct drm_plane *plane,
				struct drm_framebuffer *fb,
				const uint32_t src_x, const uint32_t src_y)
{
	struct syna_plane *syna_plane = to_syna_plane(plane);
	struct syna_crtc *syna_crtc = to_syna_crtc(crtc);
	struct syna_framebuffer *syna_fb = to_syna_framebuffer(fb);

	if (!syna_crtc || !syna_fb) {
		DRM_ERROR("%s %d  syna crtc or fb is NULL!!\n",
			  __func__, __LINE__);
		return;
	}

	switch (syna_drm_fb_format(fb)) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		break;
	default:
		DRM_ERROR("unsupported pixel format (format = %d)\n",
			  syna_drm_fb_format(fb));
		return;
	}

	syna_vpp_set_surface(plane->dev,
				 syna_crtc->syna_reg,
				 syna_plane->plane_id,
				 fb,
				 src_x, src_y);
}

static int syna_plane_helper_atomic_check(struct drm_plane *plane,
					  syna_drm_plane_state *state)
{
	struct drm_plane_state *plane_state = syna_get_drm_plane_state(state, plane);
	struct drm_crtc_state *crtc_new_state;

	if (!plane_state->crtc)
		return 0;

	crtc_new_state = drm_atomic_get_new_crtc_state(plane_state->state,
							   plane_state->crtc);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_new_state,
						   0,
						   INT_MAX,
						   true, true);
}

static void syna_plane_helper_atomic_update(struct drm_plane *plane,
					  syna_drm_plane_state *state)
{
	struct drm_plane_state *plane_state =
		syna_get_drm_plane_state_by_new(state, plane);
	struct drm_framebuffer *fb = plane_state->fb;

	if (fb) {
		DRM_DEBUG_ATOMIC
			("%s:%d %d %d %d %d -> %d %d %d %d  rotation=%d\n",
			 __func__, __LINE__, plane_state->src_x,
			 plane_state->src_y, plane_state->src_w, plane_state->src_h,
			 plane_state->crtc_x, plane_state->crtc_y,
			 plane_state->crtc_w, plane_state->crtc_h,
			 plane_state->rotation);
		syna_plane_set_surface(plane_state->crtc, plane, fb,
					   plane_state->src_x, plane_state->src_y);
	}
}

static const struct drm_plane_helper_funcs syna_plane_helper_funcs = {
	SYNA_DRM_DRIVER_GEM_PLANE_INTERFACES()
	.atomic_check = syna_plane_helper_atomic_check,
	.atomic_update = syna_plane_helper_atomic_update,
};

static const struct drm_plane_funcs syna_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_primary_helper_destroy,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

struct drm_plane *syna_plane_create(struct drm_device *dev, int crtc_index,
					ENUM_PLANE_ID plane_id, enum drm_plane_type type)
{
	struct syna_plane *syna_plane;
	struct drm_plane *plane;
	uint32_t const *plane_supported_formats;

	int err;

	syna_plane = kzalloc(sizeof(struct syna_plane), GFP_KERNEL);
	if (!syna_plane) {
		err = -ENOMEM;
		goto err_exit;
	}
	//Store plane_id in syna_plane
	plane = &syna_plane->base;
	syna_plane->plane_id = plane_id;

	//Get the formats supported by the plane
	plane_supported_formats = SYNA_GET_PLANE_SUPPORTED_FORMAT(plane_id);

	err = drm_universal_plane_init(dev, plane, crtc_index, &syna_plane_funcs,
					   plane_supported_formats,
					   plane_supported_formats_size[plane_id],
					   NULL, type, plane_name[plane_id]);
	if (err)
		goto err_plane_free;

	drm_plane_helper_add(plane, &syna_plane_helper_funcs);
	DRM_DEBUG_ATOMIC("%s %d\n", __func__, __LINE__);

	DRM_DEBUG_DRIVER("[PLANE] type:  %d\n", plane->type);
	DRM_DEBUG_DRIVER("[PLANE] index: %d\n", plane->index);
	DRM_DEBUG_DRIVER("[PLANE] name:  %s\n", plane->name);
	DRM_DEBUG_DRIVER("num_total_plane %d\n",
			 dev->mode_config.num_total_plane);

	return plane;

err_plane_free:
	kfree(syna_plane);
err_exit:
	return ERR_PTR(err);
}
