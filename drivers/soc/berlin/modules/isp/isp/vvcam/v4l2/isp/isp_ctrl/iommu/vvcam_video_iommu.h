/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __VVCAM_VIDEO_IOMMU_H__
#define __VVCAM_VIDEO_IOMMU_H__

#define VVCAM_VIDEO_CID_IOMMU_ENABLED      (VVCAM_VIDEO_CID_IOMMU_BASE + 0x0000)

int vvcam_video_iommu_ctrl_count(void);
int vvcam_video_iommu_ctrl_create(struct vvcam_isp_dev *isp_dev);

#endif
