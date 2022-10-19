/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021,, The Linux Foundation. All rights reserved.
 */

#ifndef _MSM_VIDC_MEMORY_H_
#define _MSM_VIDC_MEMORY_H_

#include "msm_vidc_internal.h"

struct msm_vidc_core;
struct msm_vidc_inst;

#define MSM_MEM_POOL_PACKET_SIZE 1024

struct msm_memory_dmabuf {
	struct list_head       list;
	struct dma_buf        *dmabuf;
	u32                    refcount;
};

enum msm_memory_pool_type {
	MSM_MEM_POOL_BUFFER  = 0,
	MSM_MEM_POOL_MAP,
	MSM_MEM_POOL_ALLOC,
	MSM_MEM_POOL_TIMESTAMP,
	MSM_MEM_POOL_DMABUF,
	MSM_MEM_POOL_PACKET,
	MSM_MEM_POOL_BUF_TIMER,
	MSM_MEM_POOL_BUF_STATS,
	MSM_MEM_POOL_MAX,
};

struct msm_memory_alloc_header {
	struct list_head       list;
	u32                    type;
	bool                   busy;
	void                  *buf;
};

struct msm_memory_pool {
	u32                    size;
	char                  *name;
	struct list_head       free_pool; /* list of struct msm_memory_alloc_header */
	struct list_head       busy_pool; /* list of struct msm_memory_alloc_header */
};

int msm_vidc_memory_alloc(struct msm_vidc_core *core,
	struct msm_vidc_alloc *alloc);
int msm_vidc_memory_free(struct msm_vidc_core *core,
	struct msm_vidc_alloc *alloc);
int msm_vidc_memory_map(struct msm_vidc_core *core,
	struct msm_vidc_map *map);
int msm_vidc_memory_unmap(struct msm_vidc_core *core,
	struct msm_vidc_map *map);
struct dma_buf *msm_vidc_dma_buf_get(struct msm_vidc_inst *inst,
	int fd);
void msm_vidc_dma_buf_put(struct msm_vidc_inst *inst,
	struct dma_buf *dmabuf);
void msm_vidc_dma_buf_put_completely(struct msm_vidc_inst *inst,
	struct msm_memory_dmabuf *buf);
int msm_vidc_pools_init(struct msm_vidc_inst *inst);
void msm_vidc_pools_deinit(struct msm_vidc_inst *inst);
void *msm_vidc_pool_alloc(struct msm_vidc_inst *inst,
	enum msm_memory_pool_type type);
void msm_vidc_pool_free(struct msm_vidc_inst *inst, void *vidc_buf);
int msm_vidc_vmem_alloc(unsigned long size, void **mem, const char *msg);
void msm_vidc_vmem_free(void **addr);
struct dma_buf_attachment *msm_vidc_dma_buf_attach(struct msm_vidc_core *core,
	struct dma_buf *dbuf, struct device *dev);
int msm_vidc_dma_buf_detach(struct msm_vidc_core *core, struct dma_buf *dbuf,
	struct dma_buf_attachment *attach);
struct sg_table *msm_vidc_dma_buf_map_attachment(struct msm_vidc_core *core,
	struct dma_buf_attachment *attach);
int msm_vidc_dma_buf_unmap_attachment(struct msm_vidc_core *core,
	struct dma_buf_attachment *attach, struct sg_table *table);
u32 msm_vidc_buffer_region(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type);

#endif // _MSM_VIDC_MEMORY_H_