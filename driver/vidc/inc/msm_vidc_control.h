/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021,, The Linux Foundation. All rights reserved.
 */
/* Copyright (c) 2022. Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef _MSM_VIDC_CONTROL_H_
#define _MSM_VIDC_CONTROL_H_

#include "msm_vidc_inst.h"
#include "msm_vidc_internal.h"

int msm_vidc_ctrl_init(struct msm_vidc_inst *inst);
int msm_vidc_ctrl_deinit(struct msm_vidc_inst *inst);
int msm_v4l2_op_s_ctrl(struct v4l2_ctrl *ctrl);
int msm_v4l2_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl);
int msm_vidc_prepare_dependency_list(struct msm_vidc_inst *inst);
int msm_vidc_adjust_set_v4l2_properties(struct msm_vidc_inst *inst);
bool is_valid_cap_id(enum msm_vidc_inst_capability_type cap_id);
enum msm_vidc_inst_capability_type msm_vidc_get_cap_id(
	struct msm_vidc_inst *inst, u32 id);
#endif
