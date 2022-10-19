// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/mem-buf.h>
#include <soc/qcom/secure_buffer.h>

#include "msm_vidc_core.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_driver.h"
#include "msm_vidc_events.h"
#include "msm_vidc_platform.h"

static bool is_non_secure_buffer(struct dma_buf *dmabuf)
{
	return mem_buf_dma_buf_exclusive_owner(dmabuf);
}

struct dma_buf_attachment *msm_vidc_dma_buf_attach_ext(struct msm_vidc_core *core,
	struct dma_buf *dbuf, struct device *dev)
{
	int rc = 0;
	struct dma_buf_attachment *attach = NULL;
	struct context_bank_info *cb = NULL;

	if (!core || !dbuf || !dev) {
		d_vpr_e("%s: invalid params\n", __func__);
		return NULL;
	}

	cb = msm_vidc_get_context_bank_for_device(core, dev);
	if (!cb) {
		d_vpr_e("%s: Failed to get context bank device for %s\n",
			 __func__, dev_name(dev));
		return NULL;
	}

	/* reject non-secure mapping request for a secure buffer(or vice versa) */
	if (cb->region == MSM_VIDC_NON_SECURE || cb->region == MSM_VIDC_NON_SECURE_PIXEL) {
		if (!is_non_secure_buffer(dbuf)) {
			d_vpr_e("%s: secure buffer mapping to non-secure region %d not allowed\n",
				__func__, cb->region);
			return NULL;
		}
	} else {
		if (is_non_secure_buffer(dbuf)) {
			d_vpr_e("%s: non-secure buffer mapping to secure region %d not allowed\n",
				__func__, cb->region);
			return NULL;
		}
	}

	attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		rc = PTR_ERR(attach) ? PTR_ERR(attach) : -1;
		d_vpr_e("Failed to attach dmabuf, error %d\n", rc);
		return NULL;;
	}

	/*
	 * We do not need dma_map function to perform cache operations
	 * on the whole buffer size and hence pass skip sync flag.
	 */
	attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	/*
	 * Get the scatterlist for the given attachment
	 * Mapping of sg is taken care by map attachment
	 */
	attach->dma_map_attrs |= DMA_ATTR_DELAYED_UNMAP;
	if (is_sys_cache_present(core))
		attach->dma_map_attrs |= DMA_ATTR_IOMMU_USE_UPSTREAM_HINT;

	return attach;
}

int msm_vidc_memory_free_ext(struct msm_vidc_core *core, struct msm_vidc_alloc *mem)
{
	int rc = 0;

	if (!mem || !mem->dmabuf) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	d_vpr_h(
		"%s: dmabuf %pK, size %d, kvaddr %pK, buffer_type %s, secure %d, region %d\n",
		__func__, mem->dmabuf, mem->size, mem->kvaddr, buf_name(mem->type),
		mem->secure, mem->region);

	trace_msm_vidc_dma_buffer("FREE", mem->dmabuf, mem->size, mem->kvaddr,
		buf_name(mem->type), mem->secure, mem->region);

	if (mem->kvaddr) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,15,0))
		dma_buf_vunmap(mem->dmabuf, mem->kvaddr);
#else
		dma_buf_vunmap(mem->dmabuf, &mem->dmabuf_map);
#endif
		mem->kvaddr = NULL;
		dma_buf_end_cpu_access(mem->dmabuf, DMA_BIDIRECTIONAL);
	}

	if (mem->dmabuf) {
		dma_heap_buffer_free(mem->dmabuf);
		mem->dmabuf = NULL;
	}

	return rc;
}

int msm_vidc_memory_alloc_ext(struct msm_vidc_core *core, struct msm_vidc_alloc *mem)
{
	int rc = 0;
	int size = 0;
	struct dma_heap *heap;
	char *heap_name = NULL;
	struct mem_buf_lend_kernel_arg lend_arg;
	int vmids[1];
	int perms[1];

	if (!mem) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	size = ALIGN(mem->size, SZ_4K);

	if (mem->secure) {
		switch (mem->region) {
		case MSM_VIDC_SECURE_PIXEL:
			heap_name = "qcom,secure-pixel";
			break;
		case MSM_VIDC_SECURE_NONPIXEL:
			heap_name = "qcom,secure-non-pixel";
			break;
		case MSM_VIDC_SECURE_BITSTREAM:
			heap_name = "qcom,system";
			break;
		default:
			d_vpr_e("invalid secure region : %#x\n", mem->region);
			return -EINVAL;
		}
	} else {
		heap_name = "qcom,system";
	}

	heap = dma_heap_find(heap_name);
	mem->dmabuf = dma_heap_buffer_alloc(heap, size, 0, 0);
	if (IS_ERR_OR_NULL(mem->dmabuf)) {
		d_vpr_e("%s: dma heap %s alloc failed\n", __func__, heap_name);
		mem->dmabuf = NULL;
		rc = -ENOMEM;
		goto error;
	}

	if (mem->secure && mem->type == MSM_VIDC_BUF_BIN)
	{
		vmids[0] = VMID_CP_BITSTREAM;
		perms[0] = PERM_READ | PERM_WRITE;

		lend_arg.nr_acl_entries = ARRAY_SIZE(vmids);
		lend_arg.vmids = vmids;
		lend_arg.perms = perms;

		rc = mem_buf_lend(mem->dmabuf, &lend_arg);
		if (rc) {
			d_vpr_e("%s: BIN dmabuf %pK LEND failed, rc %d heap %s\n",
				__func__, mem->dmabuf, rc, heap_name);
			goto error;
		}
	}

	if (mem->map_kernel) {
		dma_buf_begin_cpu_access(mem->dmabuf, DMA_BIDIRECTIONAL);

	/*
	 * Waipio uses Kernel version 5.10.x,
	 * Kalama uses Kernel Version 5.15.x,
	 * Pineapple uses Kernel Version 5.18.x
	 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,15,0))
		mem->kvaddr = dma_buf_vmap(mem->dmabuf);
		if (!mem->kvaddr) {
			d_vpr_e("%s: kernel map failed\n", __func__);
			rc = -EIO;
			goto error;
		}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0))
		rc = dma_buf_vmap(mem->dmabuf, &mem->dmabuf_map);
		if (rc) {
			d_vpr_e("%s: kernel map failed\n", __func__);
			rc = -EIO;
			goto error;
		}
		mem->kvaddr = mem->dmabuf_map.vaddr;
#else
		rc = dma_buf_vmap(mem->dmabuf, &mem->dmabuf_map);
		if (rc) {
			d_vpr_e("%s: kernel map failed\n", __func__);
			rc = -EIO;
			goto error;
		}
		mem->kvaddr = mem->dmabuf_map.vaddr;
#endif
	}

	d_vpr_h(
		"%s: dmabuf %pK, size %d, kvaddr %pK, buffer_type %s, secure %d, region %d\n",
		__func__, mem->dmabuf, mem->size, mem->kvaddr, buf_name(mem->type),
		mem->secure, mem->region);
	trace_msm_vidc_dma_buffer("ALLOC", mem->dmabuf, mem->size, mem->kvaddr,
		buf_name(mem->type), mem->secure, mem->region);

	return 0;

error:
    msm_vidc_memory_free_ext(core, mem);
	return rc;
}

int msm_vidc_memory_map_ext(struct msm_vidc_core *core, struct msm_vidc_map *map)
{
	int rc = 0;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *table = NULL;
	struct context_bank_info *cb = NULL;

	if (!core || !map) {
		d_vpr_e("%s: invalid params\n", __func__);
		return -EINVAL;
	}

	if (map->refcount) {
		map->refcount++;
		goto exit;
	}

	/* reject non-secure mapping request for a secure buffer(or vice versa) */
	if (map->region == MSM_VIDC_NON_SECURE || map->region == MSM_VIDC_NON_SECURE_PIXEL) {
		if (!is_non_secure_buffer(map->dmabuf)) {
			d_vpr_e("%s: secure buffer mapping to non-secure region %d not allowed\n",
				__func__, map->region);
			return -EINVAL;
		}
	} else {
		if (is_non_secure_buffer(map->dmabuf)) {
			d_vpr_e("%s: non-secure buffer mapping to secure region %d not allowed\n",
				__func__, map->region);
			return -EINVAL;
		}
	}

	cb = msm_vidc_get_context_bank_for_region(core, map->region);
	if (!cb) {
		d_vpr_e("%s: Failed to get context bank device\n",
			 __func__);
		rc = -EIO;
		goto error_cb;
	}

	/* Prepare a dma buf for dma on the given device */
	attach = msm_vidc_dma_buf_attach_ext(core, map->dmabuf, cb->dev);
	if (IS_ERR_OR_NULL(attach)) {
		rc = PTR_ERR(attach) ? PTR_ERR(attach) : -ENOMEM;
		d_vpr_e("Failed to attach dmabuf\n");
		goto error_attach;
	}

	table = msm_vidc_dma_buf_map_attachment(core, attach);
	if (IS_ERR_OR_NULL(table)) {
		rc = PTR_ERR(table) ? PTR_ERR(table) : -ENOMEM;
		d_vpr_e("Failed to map table\n");
		goto error_table;
	}

	map->device_addr = table->sgl->dma_address;
	map->table = table;
	map->attach = attach;
	map->refcount++;

exit:
	d_vpr_l("%s: type %11s, device_addr %#llx, refcount %d, region %d\n",
		__func__, buf_name(map->type), map->device_addr, map->refcount, map->region);

	return 0;

error_table:
    msm_vidc_dma_buf_detach(core, map->dmabuf, attach);
error_attach:
error_cb:
	return rc;
}

u32 msm_vidc_buffer_region_ext(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type)
{
	u32 region = MSM_VIDC_NON_SECURE;

	if (!is_secure_session(inst)) {
		switch (buffer_type) {
		case MSM_VIDC_BUF_ARP:
			region = MSM_VIDC_SECURE_NONPIXEL;
			break;
		case MSM_VIDC_BUF_INPUT:
			if (is_encode_session(inst))
				region = MSM_VIDC_NON_SECURE_PIXEL;
			else
				region = MSM_VIDC_NON_SECURE;
			break;
		case MSM_VIDC_BUF_OUTPUT:
			if (is_encode_session(inst))
				region = MSM_VIDC_NON_SECURE;
			else
				region = MSM_VIDC_NON_SECURE_PIXEL;
			break;
		case MSM_VIDC_BUF_DPB:
		case MSM_VIDC_BUF_VPSS:
		case MSM_VIDC_BUF_PARTIAL_DATA:
			region = MSM_VIDC_NON_SECURE_PIXEL;
			break;
		case MSM_VIDC_BUF_INPUT_META:
		case MSM_VIDC_BUF_OUTPUT_META:
		case MSM_VIDC_BUF_BIN:
		case MSM_VIDC_BUF_COMV:
		case MSM_VIDC_BUF_NON_COMV:
		case MSM_VIDC_BUF_LINE:
		case MSM_VIDC_BUF_PERSIST:
			region = MSM_VIDC_NON_SECURE;
			break;
		default:
			i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
				__func__, buffer_type);
		}
	} else {
		switch (buffer_type) {
		case MSM_VIDC_BUF_INPUT:
			if (is_encode_session(inst))
				region = MSM_VIDC_SECURE_PIXEL;
			else
				region = MSM_VIDC_SECURE_BITSTREAM;
			break;
		case MSM_VIDC_BUF_OUTPUT:
			if (is_encode_session(inst))
				region = MSM_VIDC_SECURE_BITSTREAM;
			else
				region = MSM_VIDC_SECURE_PIXEL;
			break;
		case MSM_VIDC_BUF_INPUT_META:
		case MSM_VIDC_BUF_OUTPUT_META:
			region = MSM_VIDC_NON_SECURE;
			break;
		case MSM_VIDC_BUF_DPB:
		case MSM_VIDC_BUF_VPSS:
		case MSM_VIDC_BUF_PARTIAL_DATA:
			region = MSM_VIDC_SECURE_PIXEL;
			break;
		case MSM_VIDC_BUF_BIN:
			region = MSM_VIDC_SECURE_BITSTREAM;
			break;
		case MSM_VIDC_BUF_ARP:
		case MSM_VIDC_BUF_COMV:
		case MSM_VIDC_BUF_NON_COMV:
		case MSM_VIDC_BUF_LINE:
		case MSM_VIDC_BUF_PERSIST:
			region = MSM_VIDC_SECURE_NONPIXEL;
			break;
		default:
			i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
				__func__, buffer_type);
		}
	}

	return region;
}
