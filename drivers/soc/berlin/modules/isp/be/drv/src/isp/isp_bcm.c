// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ispSS_reg.h"
#include "ispSS_bcmbuf.h"
#include "ispSS_bcmbuf_debug.h"
#include "ispSS_api_dhub.h"
#include "isp_bcm.h"
#include "ispSS_shm.h"
#include "ispbe_err.h"
#include "ispSS_api_dhub.h"


#define ISP_QUEUE0          BCM_SCHED_Q0
#define ISP_QUEUE1          BCM_SCHED_Q1
#define ISP_QUEUE2          BCM_SCHED_Q2

/* Interrupt Source Macros*/
#define ISP_INTR_SRC0      0x0
#define ISP_INTR_SRC1      0x1
#define ISP_INTR_SRC2      0x1F
#define CSI_DEVICES_MAX    0x2
#define BCM_MAX_BUFFERS    0x4

#define ISP_MP0_PATH 1
#define ISP_SP1_PATH 2
#define ISP_SP2_PATH 3

#define CONFIG_DOLPHIN_ISPSS_REGAREA_BASE          0xf9100000

#define ISP_BCM_REG_OFFSET_BYTE_MAX      (0x67200)
#define MCM_WR_AUTO_UPDATE_MASK          0x00000001U
#define MCM_WR_CFG_UPD_MASK              0x00000004U

#define REG_WRITE(addr, value)      (((*((volatile unsigned int*)(addr))) = \
			((unsigned int)(value))))
#define REG_READ(addr, value)       ((*((unsigned int *)value)) = \
		(*((volatile unsigned int*)(addr))))

#define MEMMAP_ISP_BCM_REG_BASE  (CONFIG_DOLPHIN_ISPSS_REGAREA_BASE + \
		ISPSS_MEMMAP_GLB_REG_BASE + RA_IspMISC_Isp2BcmIrq)
#define MP_YCBCR_FRAME_END_BIT  0
#define SP1_YCBCR_FRAME_END_BIT 3
#define SP2_YCBCR_FRAME_END_BIT 4

struct IspCoreBcmBuf {
	SHM_HANDLE shm_handle;
	SHM_HANDLE shmcfgQ_handle;
	void *bcm_phy_addr;
	void *cfg_phy_addr;
	struct BCMBUF  *core_bcm_buf;
	struct DHUB_CFGQ *core_bcm_cfgQ;
};

struct Isp_Bcm_Ctx {
	uint8_t         bcm_usr_cnt;
	uint8_t         curr_buffer;
	struct IspCoreBcmBuf   buffers[BCM_MAX_BUFFERS]; //MP0/SP1/SP2 registers
	struct mutex bcm_lock;
};

static struct Isp_Bcm_Ctx *gpBcmCtx;

//static void  ISPSS_BCMBUF_Flush(void);
/*****************************************************************************
 *
 *****************************************************************************/
static void isp_bcm_reset(struct IspCoreBcmBuf *bcm_buf)
{
	struct BCMBUF *d_bcm_buf = bcm_buf->core_bcm_buf;
	struct DHUB_CFGQ *d_bcm_cfgQ = bcm_buf->core_bcm_cfgQ;

	ISPSS_BCMBUF_Reset(d_bcm_buf);
	ISPSS_BCMBUF_Select(d_bcm_buf, CPCB_1);
	d_bcm_cfgQ->len = 0;
}

/*****************************************************************************
 *
 *****************************************************************************/
static int isp_bcm_allocate_bcm_buffer(struct IspCoreBcmBuf *pBcmBuf)
{
	SHM_HANDLE shm_handle;
	SHM_HANDLE shmcfgQ_handle;
	INT32 res = ISPSS_OK;

	if (ispSS_SHM_Allocate(SHM_NONSECURE, sizeof(struct BCMBUF), 1024,
				&shm_handle) != SUCCESS) {
		pr_err("Failed to allocate memory\n");
		res = ISPSS_ENOMEM;
	}
	pBcmBuf->shm_handle = shm_handle;
	ispSS_SHM_GetPhysicalAddress(shm_handle, 0, (void *)&pBcmBuf->bcm_phy_addr);
	ispSS_SHM_GetVirtualAddress(shm_handle, 0, (void *)&pBcmBuf->core_bcm_buf);
	memset(pBcmBuf->core_bcm_buf, 0, sizeof(struct BCMBUF));

	if (ispSS_SHM_Allocate(SHM_NONSECURE, sizeof(struct DHUB_CFGQ), 1024,
				&shmcfgQ_handle) != SUCCESS) {
		pr_err("Failed to allocate memory\n");
		/* Destroy created buffers */
		ispSS_SHM_Release (shm_handle);
		res = ISPSS_ENOMEM;
	}
	pBcmBuf->shmcfgQ_handle = shmcfgQ_handle;
	ispSS_SHM_GetPhysicalAddress(shmcfgQ_handle, 0, (void *)&pBcmBuf->cfg_phy_addr);
	ispSS_SHM_GetVirtualAddress(shmcfgQ_handle, 0, (void *)&pBcmBuf->core_bcm_cfgQ);
	memset(pBcmBuf->core_bcm_cfgQ, 0, sizeof(struct DHUB_CFGQ));

	return res;
}

/*****************************************************************************
 *
 *****************************************************************************/
static int isp_bcm_create_bcm_buffer(struct IspCoreBcmBuf *pBcmBuf)
{
	INT32 res = ISPSS_OK;

	res = isp_bcm_allocate_bcm_buffer(pBcmBuf);
	if (res == ISPSS_OK) {
		if (ISPSS_BCMBUF_Create(pBcmBuf->core_bcm_buf, BCM_BUFFER_SIZE) !=
				ISPSS_OK) {
			pr_err("Failed to create BCM buffer\n");
			res = ISPSS_ENOMEM;
		}
		ISPSS_BCMBUF_Reset(pBcmBuf->core_bcm_buf);
		ISPSS_BCMBUF_Select(pBcmBuf->core_bcm_buf, CPCB_1);
		if (ISPSS_CFGQ_Create(pBcmBuf->core_bcm_cfgQ, DMA_CMD_BUFFER_SIZE) !=
				ISPSS_OK) {
			pr_err("Failed to create configQ\n");
			res = ISPSS_ENOMEM;
			/* Destroy created buffers */
			ISPSS_BCMBUF_Destroy(pBcmBuf->core_bcm_buf);
		}
	}
	isp_bcm_reset(pBcmBuf);
	return res;
}

/*****************************************************************************
 *
 *****************************************************************************/
static void isp_bcm_destroy_bcm_buffer(struct IspCoreBcmBuf *pBcmBuf)
{
	if (pBcmBuf->core_bcm_cfgQ) {
		ISPSS_CFGQ_Destroy(pBcmBuf->core_bcm_cfgQ);
		pBcmBuf->core_bcm_cfgQ = NULL;
		ispSS_SHM_Release(pBcmBuf->shmcfgQ_handle);
	}

	if (pBcmBuf->core_bcm_buf) {
		ISPSS_BCMBUF_Destroy(pBcmBuf->core_bcm_buf);
		ispSS_SHM_Release(pBcmBuf->shm_handle);
	}
}

/*****************************************************************************
 *
 *****************************************************************************/
void isp_bcm_open(void)
{
	struct Isp_Bcm_Ctx *pBcmCtx = NULL;
	int32_t res;
	int i;

	pBcmCtx = gpBcmCtx;
	if (pBcmCtx == NULL) {
		pBcmCtx = kzalloc(sizeof(*pBcmCtx), GFP_KERNEL);
		if (!pBcmCtx) {
			res = -ENOMEM;
			pr_err("%s: fails memory for BCM CTX\n", __func__);
			return;
		}
		memset(pBcmCtx, 0, sizeof(struct Isp_Bcm_Ctx));
		gpBcmCtx = pBcmCtx;
	}

	if (pBcmCtx->bcm_usr_cnt == 0)
		mutex_init(&pBcmCtx->bcm_lock);

	pBcmCtx->curr_buffer = -1; // initializing
	for (i = 0; i < BCM_MAX_BUFFERS; i++) {
		if (isp_bcm_create_bcm_buffer(&pBcmCtx->buffers[i]) != ISPSS_OK)
			return;

	}
	pBcmCtx->bcm_usr_cnt++;
}

/*****************************************************************************
 *
 *****************************************************************************/
void isp_bcm_close(void)
{
	struct Isp_Bcm_Ctx *pBcmCtx = gpBcmCtx;
	int i;

	pBcmCtx->bcm_usr_cnt--;
	if (pBcmCtx->bcm_usr_cnt <= 0) {
		for (i = 0; i < BCM_MAX_BUFFERS; i++)
			isp_bcm_destroy_bcm_buffer(&pBcmCtx->buffers[i]);

		//ISPSS_BCMBUF_Flush();
		mutex_destroy(&pBcmCtx->bcm_lock);
		kfree(pBcmCtx);
		pBcmCtx = NULL;
		gpBcmCtx = pBcmCtx;
	}
}

void isp_bcm_configure(int path)
{
	uint16_t flag = 0x1;
	struct Isp_Bcm_Ctx *pBcmCtx = gpBcmCtx;
	uint16_t qid = BCM_SCHED_Q9;

	BCM_SCHED_Flush(1 << qid);

	switch (path) {
	case ISP_MP0_PATH:
		flag = 1 << MP_YCBCR_FRAME_END_BIT;
		break;
	case ISP_SP1_PATH:
		flag = 1 << SP1_YCBCR_FRAME_END_BIT;
		break;
	case ISP_SP2_PATH:
		flag = 1 << SP2_YCBCR_FRAME_END_BIT;
		break;
	default:
		break;
	}

	mutex_lock(&pBcmCtx->bcm_lock);
	// TODO Use src2
	BCM_SchedSetMux(qid, ISP_INTR_SRC1);
	// connect second interrupt source to isp queue
	//BCM_SCHED_SetMux(ISP_QUEUE0, ISP_INTR_SRC2);
	ISPSS_REG_WRITE32(MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Ctrl, 0x0);
	ISPSS_REG_WRITE32(MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Mask1, flag);
	//Msk2 enable mode 0 for mask2
	ISPSS_REG_WRITE32(MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Ctrl, 0x2);
	//BCM_SCHED_SetMux(BCM_SCHED_Q9, ISP_INTR_SRC0);
	//ISPSS_REG_WRITE32(MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Ctrl, 0x1);
	//ISPSS_REG_WRITE32(MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Mask0, flag);
	mutex_unlock(&pBcmCtx->bcm_lock);
}

void isp_bcm_deconfigure(void)
{
	struct Isp_Bcm_Ctx *pBcmCtx = gpBcmCtx;
	uint16_t qid = BCM_SCHED_Q9;

	BCM_SCHED_Flush(1 << qid);

	mutex_lock(&pBcmCtx->bcm_lock);
	// TODO Use src2
	BCM_SchedSetMux(qid, 0x1F);
	ISPSS_REG_WRITE32( MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Ctrl, 0x0);
	ISPSS_REG_WRITE32( MEMMAP_ISP_BCM_REG_BASE + RA_ISP2BCMIntCtrl_Mask1, 0);
	mutex_unlock(&pBcmCtx->bcm_lock);
}

/*****************************************************************************
 *
 *****************************************************************************/
void *isp_bcm_get_next_bcmbuf(void)
{
	struct Isp_Bcm_Ctx *pBcmCtx = gpBcmCtx;

	pBcmCtx->curr_buffer++;
	if (pBcmCtx->curr_buffer < 0 || pBcmCtx->curr_buffer >= BCM_MAX_BUFFERS)
		pBcmCtx->curr_buffer = 0;
	return pBcmCtx->buffers[pBcmCtx->curr_buffer].core_bcm_buf;
}
EXPORT_SYMBOL(isp_bcm_get_next_bcmbuf);

/*****************************************************************************
 *
 *****************************************************************************/
int isp_bcm_commit(void *buf, bool is_immediate)
{
	int i;
	struct Isp_Bcm_Ctx *pBcmCtx = gpBcmCtx;
	int ret = 0;
	struct DHUB_CFGQ *d_bcm_cfgQ;
	uint16_t qid = BCM_SCHED_Q9;
	struct IspCoreBcmBuf *pCoreBcmBuf = NULL;
	struct BCMBUF *pBcmBuf = (struct BCMBUF *)buf;

	for (i = 0; i < BCM_MAX_BUFFERS; i++) {
		if (pBcmCtx->buffers[i].core_bcm_buf == pBcmBuf)
			pCoreBcmBuf = &pBcmCtx->buffers[i];
	}
	if (pCoreBcmBuf == NULL)
		return -1;

	mutex_lock(&pBcmCtx->bcm_lock);
	if (is_immediate) {
		//ISPSS_BCMBUF_LogPrint(pBcmBuf);
		ISPSS_BCMBUF_HardwareTrans(pBcmBuf, 0x0C);
		isp_bcm_reset(pCoreBcmBuf);
	} else {
		d_bcm_cfgQ = pCoreBcmBuf->core_bcm_cfgQ;
		if (ISPSS_BCMBUF_To_CFGQ(pBcmBuf, d_bcm_cfgQ) != ISPSS_OK)
			pr_err("ISPSS_BCMBUF_To_CFGQ error!!!\n");

		//ISPSS_BCMBUF_LogPrint(pBcmBuf);
		//ISPSS_CFGQ_LogPrint(d_bcm_cfgQ);
		if (ISPSS_BCMDHUB_CFGQ_Commit(d_bcm_cfgQ, CPCB_1, qid, 1) != ISPSS_OK) {
			isp_bcm_reset(pCoreBcmBuf);
			pr_err("ISPSS_BCMDHUB_CFGQ_Commit error!!!\n");
		} else {
			isp_bcm_reset(pCoreBcmBuf);
			pr_debug("Commit queue success...qid[%d]\n", qid);
		}
	}
	mutex_unlock(&pBcmCtx->bcm_lock);
	return ret;
}
EXPORT_SYMBOL(isp_bcm_commit);

unsigned int is_isp_bcm_buffer_full(void)
{
	unsigned int sched_qid = BCM_SCHED_Q9;
	UNSG32 FullSts;

	BCM_SCHED_GetFullSts(sched_qid, &FullSts);

	return FullSts;
}
EXPORT_SYMBOL(is_isp_bcm_buffer_full);
