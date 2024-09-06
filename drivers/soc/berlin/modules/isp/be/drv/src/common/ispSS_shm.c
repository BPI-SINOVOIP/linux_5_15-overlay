// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/dma-buf.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>

#include "ispSS_shm.h"
#include "com_type.h"

DEFINE_MUTEX(ispSS_shm_mutex);

struct ispSS_shm_data {
	struct device mem_device;
	struct dma_heap *heap;
};

struct ispSS_shm_buf {
	struct device               *dev;
	void                        *vaddr; // Virtual address of kernel space only frmae
	void                        *paddr; // Physical address frame
	unsigned long               size;
	void                        *cookie;
	struct sg_table             *dma_sgt;
	refcount_t                  refcount;

	/* DMABUF related */
	struct dma_buf_attachment   *db_attach;
	struct dma_buf_map          *map;
};

static struct ispSS_shm_data *ispSS_shm_data;

static void shm_release(struct device *dev)
{
	/*
	 * Nothing to do, just unregister sysfs here
	 *
	 */
}

/*********************************************/
/*         APIs for all SHM                  */
/*********************************************/
int ispSS_SHM_Init(struct device *dev)
{
	int ret = 0;
	const char *cma_heap_name = "CMA-CUST-reserved";

	ret = mutex_lock_interruptible(&ispSS_shm_mutex);

	if (ret) {
		pr_err("%s: can't lock isp shm mutex\n", __func__);
		ret = -EINVAL;
		goto lock_fail;
	}

	ispSS_shm_data = devm_kzalloc(dev, sizeof(struct ispSS_shm_data), GFP_KERNEL);
	if (!ispSS_shm_data) {
		ret = -ENOMEM;
		goto mem_fail;
	}

	device_initialize(&ispSS_shm_data->mem_device);
	dev_set_name(&ispSS_shm_data->mem_device, "isp_shm");
	ispSS_shm_data->mem_device.release = shm_release;

	ret = dma_coerce_mask_and_coherent(&ispSS_shm_data->mem_device, DMA_BIT_MASK(32));
	if (ret) {
		pr_err("set dma mask 32b failed: %d\n", ret);
		goto device_free;
	}

	ispSS_shm_data->heap = dma_heap_find(cma_heap_name);
	if (!ispSS_shm_data->heap) {
		goto device_free;
		ret = -ENOMEM;
	}

	mutex_unlock(&ispSS_shm_mutex);
	return 0;

device_free:
	put_device(&ispSS_shm_data->mem_device);
	kfree(ispSS_shm_data);
mem_fail:
	mutex_unlock(&ispSS_shm_mutex);
lock_fail:
	return ret;
}

int ispSS_SHM_Deinit(struct device *dev)
{
	int ret = 0;

	ret = mutex_lock_interruptible(&ispSS_shm_mutex);

	if (ret) {
		pr_err("%s: can't lock isp shm mutex\n", __func__);
		ret = -EINVAL;
		goto lock_fail;
	}

	put_device(&ispSS_shm_data->mem_device);
	devm_kfree(dev, ispSS_shm_data);
	ispSS_shm_data = NULL;

	mutex_unlock(&ispSS_shm_mutex);
lock_fail:
	return ret;
}

int ispSS_SHM_Allocate(unsigned int  eMemType, unsigned int uiSize,
		unsigned int uiAlign, SHM_HANDLE *phShm)
{
	struct ispSS_shm_buf *buf;
	struct device *dev = &ispSS_shm_data->mem_device;
	int ret = 0;

	ret = mutex_lock_interruptible(&ispSS_shm_mutex);

	if (ret) {
		pr_err("%s: can't lock isp shm mutex\n", __func__);
		ret = -EINVAL;
		goto failed_lock;
	}

	buf = kzalloc(sizeof(struct ispSS_shm_buf), GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto failed_buf;
	}

	buf->map = kzalloc(sizeof(struct dma_buf_map), GFP_KERNEL);
	if (IS_ERR_OR_NULL(buf->map)) {
		pr_err("%s: kzalloc failed for dma_map\n", __func__);
		ret = -ENOMEM;
		goto failed_alloc1;
	}

	buf->cookie = dma_heap_buffer_alloc(ispSS_shm_data->heap, uiSize, 0, 0);

	if (IS_ERR_OR_NULL(buf->cookie)) {
		pr_err("%s: alloc of size %d failed: %ld\n", __func__,
				uiSize, PTR_ERR(buf->cookie));
		ret = -ENOMEM;
		goto failed_alloc2;
	}

	buf->db_attach = dma_buf_attach(buf->cookie, dev);
	if (IS_ERR_OR_NULL(buf->db_attach)) {
		pr_err("%s:dma attach failed\n",  __func__);
		ret = -ENOMEM;
		goto failed_attach;
	}

	buf->dma_sgt = dma_buf_map_attachment(buf->db_attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(buf->dma_sgt)) {
		pr_err("%s: dma map attachment failed for sgt\n", __func__);
		ret = -ENOMEM;
		goto failed_attachment;
	}

	if (dma_buf_begin_cpu_access(buf->cookie, DMA_BIDIRECTIONAL) != 0) {
		pr_err("%s: dma_buf_begin_cpu_access failed\n", __func__);
		ret = -ENOMEM;
		goto failed_cpu_access;
	}

	ret = dma_buf_vmap(buf->db_attach->dmabuf, buf->map);
	buf->vaddr = buf->map->vaddr;
	if (buf->vaddr == NULL) {
		pr_err("%s: dma_buf_vmap failed\n", __func__);
		ret = -ENOMEM;
		goto failed_vmap;
	}

	pr_debug("The new mapped Kaddr %lx and cookie %lx\n",
			(unsigned long)buf->vaddr, (unsigned long)buf->cookie);

	//Get the physical address of the memory
	buf->paddr = (void *)sg_dma_address(buf->dma_sgt->sgl);

	/* Prevent the device from being released while the buffer is used */
	buf->dev = get_device(dev);
	buf->size = uiSize;

	pr_debug("%s :The vaddr %lx paddr %lx size %d\n",
			__func__, (unsigned long)buf->vaddr, (unsigned long)buf->paddr, uiSize);

	refcount_set(&buf->refcount, 1);

	*phShm = (SHM_HANDLE) buf;
	mutex_unlock(&ispSS_shm_mutex);
	return 0;

failed_vmap:
	dma_buf_vunmap(buf->cookie, buf->map);
	dma_buf_end_cpu_access(buf->cookie, DMA_BIDIRECTIONAL);
failed_cpu_access:
	dma_buf_unmap_attachment(buf->db_attach, buf->dma_sgt, DMA_BIDIRECTIONAL);
failed_attachment:
	dma_buf_detach(buf->cookie, buf->db_attach);
failed_attach:
	dma_heap_buffer_free(buf->cookie);
failed_alloc2:
	kfree(buf->map);
failed_alloc1:
	kfree(buf);
failed_buf:
	mutex_unlock(&ispSS_shm_mutex);
failed_lock:
	*phShm = (SHM_HANDLE) NULL;
	return ret;
}
EXPORT_SYMBOL(ispSS_SHM_Allocate);

int ispSS_SHM_Release(SHM_HANDLE phShm)
{
	struct ispSS_shm_buf *buf = (struct ispSS_shm_buf *)phShm;
	int ret = 0;

	ret = mutex_lock_interruptible(&ispSS_shm_mutex);

	if (ret) {
		pr_err("%s: can't lock isp shm mutex\n", __func__);
		ret = -EINVAL;
		goto lock_fail;
	}

	if (!refcount_dec_and_test(&buf->refcount))
		goto non_zero_ref;

	if (buf->vaddr)
		dma_buf_vunmap(buf->cookie, buf->map);

	if (buf->db_attach) {
		dma_buf_unmap_attachment(buf->db_attach, buf->dma_sgt, DMA_BIDIRECTIONAL);
		dma_buf_detach(buf->cookie, buf->db_attach);
		buf->db_attach = NULL;
	}

	if (buf->cookie)
		dma_heap_buffer_free(buf->cookie);

	/* heap_extra.free_cb() would free its mem_id */
	dma_buf_put(buf->cookie);

	put_device(buf->dev);
	kfree(buf->map);
	kfree(buf);

non_zero_ref:
	mutex_unlock(&ispSS_shm_mutex);
lock_fail:
	return ret;
}
EXPORT_SYMBOL(ispSS_SHM_Release);

int ispSS_SHM_CleanCache(SHM_HANDLE phShm, unsigned int offset, unsigned int size)
{
	struct ispSS_shm_buf *buf;
	int ret = 0;

	buf =  (struct ispSS_shm_buf *) phShm;
	if (!buf) {
		pr_err("%s: Invalid handle\n", __func__);
		goto out;
	}
	dma_buf_end_cpu_access(buf->cookie, DMA_BIDIRECTIONAL);

out:
	return ret;
}
EXPORT_SYMBOL(ispSS_SHM_CleanCache);

int ispSS_SHM_GetVirtualAddress(SHM_HANDLE phShm, int uiOffset, void **pVirtAddr)
{
	struct ispSS_shm_buf *buf;
	int ret = 0;

	buf =  (struct ispSS_shm_buf *)phShm;
	if (!buf) {
		ret = -EINVAL;
		pr_err("%s: Invalid handle\n", __func__);
		goto out;
	}
	*pVirtAddr = buf->vaddr;
out:
	return ret;
}
EXPORT_SYMBOL(ispSS_SHM_GetVirtualAddress);

int ispSS_SHM_GetPhysicalAddress(SHM_HANDLE phShm, int uiOffset, void **pPhyAddr)
{
	struct ispSS_shm_buf *buf;
	int ret = 0;

	buf =  (struct ispSS_shm_buf *)phShm;
	if (!buf) {
		ret = -EINVAL;
		pr_err("%s: Invalid handle\n", __func__);
		goto out;
	}
	*pPhyAddr = buf->paddr;
out:
	return ret;
}
EXPORT_SYMBOL(ispSS_SHM_GetPhysicalAddress);
