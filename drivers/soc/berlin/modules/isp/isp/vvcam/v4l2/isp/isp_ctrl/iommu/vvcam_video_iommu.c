// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <media/v4l2-ioctl.h>
#include "vvcam_isp_driver.h"
#include "vvcam_isp_ctrl.h"
#include "vvcam_video_iommu.h"

static int vvcam_video_iommu_s_ctrl(struct v4l2_ctrl *ctrl)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev =
        container_of(ctrl->handler, struct vvcam_isp_dev, ctrl_handler);

    switch (ctrl->id) {
    case VVCAM_VIDEO_CID_IOMMU_ENABLED:
        if (isp_dev->ctrl_pad == VVCAM_ISP_PAD_SOURCE_P0SP1 ||
            isp_dev->ctrl_pad == VVCAM_ISP_PAD_SOURCE_P1SP1) {
            isp_dev->mmu_enabled = 0;
            dev_warn(isp_dev->dev, "%s MMU not supported on SP1 path\n", __func__);
        } else {
            isp_dev->mmu_enabled = ctrl->val;
        }
        dev_dbg(isp_dev->dev, "%s mmu_enabled: %d\n", __func__, isp_dev->mmu_enabled);
        break;
    default:
        dev_err(isp_dev->dev, "unknown v4l2 ctrl id %d\n", ctrl->id);
        return -EACCES;
    }

    return ret;
}

static int vvcam_video_iommu_g_ctrl(struct v4l2_ctrl *ctrl)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev =
        container_of(ctrl->handler, struct vvcam_isp_dev, ctrl_handler);

    switch (ctrl->id) {
    case VVCAM_VIDEO_CID_IOMMU_ENABLED:
        dev_dbg(isp_dev->dev, "%s mmu_enabled: %d\n", __func__, isp_dev->mmu_enabled);
        ctrl->val = isp_dev->mmu_enabled;
        ret = 0;
        break;

    default:
        dev_err(isp_dev->dev, "unknown v4l2 ctrl id %d\n", ctrl->id);
        return -EACCES;
    }

    return ret;
}

static const struct v4l2_ctrl_ops vvcam_video_iommu_ctrl_ops = {
    .s_ctrl = vvcam_video_iommu_s_ctrl,
    .g_volatile_ctrl = vvcam_video_iommu_g_ctrl,
};

const struct v4l2_ctrl_config vvcam_video_iommu_ctrl = {
    .ops   = &vvcam_video_iommu_ctrl_ops,
    .id    = VVCAM_VIDEO_CID_IOMMU_ENABLED,
    .type  = V4L2_CTRL_TYPE_BOOLEAN,
    .flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
    .name  = "mmu_enable",
    .step  = 1,
    .min   = 0,
    .max   = 1,
    .def   = 1,
};

int vvcam_video_iommu_ctrl_count(void)
{
    //return ARRAY_SIZE(vvcam_video_iommu_ctrl);
    return 1;
}

int vvcam_video_iommu_ctrl_create(struct vvcam_isp_dev *isp_dev)
{
    dev_dbg(isp_dev->dev, "%s mmu id: 0x%x\n", __func__, vvcam_video_iommu_ctrl.id);
    v4l2_ctrl_new_custom(&isp_dev->ctrl_handler,
                         &vvcam_video_iommu_ctrl, NULL);
    isp_dev->mmu_enabled = 1;
    if (isp_dev->ctrl_handler.error) {
        dev_err(isp_dev->dev, "register mmu ctrl failed %d.\n",
                isp_dev->ctrl_handler.error);
        return isp_dev->ctrl_handler.error;
    }

    return 0;
}
