/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2017, Juniper Networks, Inc. All rights reserved.
 *
 *
 * The contents of this file are subject to the terms of the BSD 3 clause
 * License (the "License"). You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at
 * https://github.com/Juniper/warp17/blob/master/LICENSE.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * File name:
 *     tpg_rate.h
 *
 * Description:
 *     Rate limiting interface.
 *
 * Author:
 *     Dumitru Ceara
 *
 * Initial Created:
 *     10/12/2017
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_RATE_
#define _H_TPG_RATE_

/*****************************************************************************
 * Definitions
 ****************************************************************************/

typedef struct rate_limit_cfg_s {

    uint32_t rlc_count;
    uint32_t rlc_target;
    uint32_t rlc_slots[GCFG_RATE_MAX_SLOT_COUNT];

} rate_limit_cfg_t;

#define RATE_CFG_ZERO() (rate_limit_cfg_t) { \
    .rlc_count = 1,                          \
    .rlc_target = 0,                         \
    .rlc_slots = {0}                         \
}

#define RATE_CFG_INF() (rate_limit_cfg_t) {  \
    .rlc_count = 1,                          \
    .rlc_target = TPG_RATE_LIM_INFINITE_VAL, \
    .rlc_slots = {TPG_RATE_LIM_INFINITE_VAL} \
}

#define RATE_CFG_IS_ZERO(cfg) \
    ((cfg)->rlc_count == 1 && (cfg)->rlc_target == 0)
#define RATE_CFG_IS_INF(cfg)  \
    ((cfg)->rlc_count == 1 && (cfg)->rlc_target == TPG_RATE_LIM_INFINITE_VAL)

typedef struct rate_limit_s {

    uint32_t  rl_current_rate;
    uint16_t  rl_count;
    uint16_t  rl_current_index;
    uint16_t  rl_max_burst;

    uint32_t *rl_slots; /* Max GCFG_RATE_MAX_SLOT_COUNT. */

} rate_limit_t;

/*****************************************************************************
 * Global functions
 ****************************************************************************/
extern void rate_limit_cfg_init(const tpg_rate_t *target,
                                rate_limit_cfg_t *cfg);

/* Initialize a "per-core" rate based on a global config.
 * WARNING: the config itself will be changed based on the desired rate.
 */
extern int rate_limit_init(rate_limit_t *rl, rate_limit_cfg_t *cfg,
                           uint32_t lcore_id,
                           uint32_t displacement,
                           uint32_t desired,
                           uint32_t max_burst);

extern void rate_limit_free(rate_limit_t *rl);

extern uint32_t rate_limit_interval_us(rate_limit_t *rl);

/*****************************************************************************
 * Static inlines
 ****************************************************************************/

/*****************************************************************************
 * rate_limit_reached()
 ****************************************************************************/
static inline bool rate_limit_reached(const rate_limit_t *rl)
{
    return unlikely(rl->rl_current_rate == 0);
}

/*****************************************************************************
 * rate_limit_advance_interval()
 ****************************************************************************/
static inline void rate_limit_advance_interval(rate_limit_t *rl)
{
    rl->rl_current_index = (rl->rl_current_index + 1) % rl->rl_count;
    rl->rl_current_rate = rl->rl_slots[rl->rl_current_index];
}

/*****************************************************************************
 * rate_limit_available()
 ****************************************************************************/
static inline uint32_t rate_limit_available(const rate_limit_t *rl)
{
    if (likely(rl->rl_max_burst <= rl->rl_current_rate))
        return rl->rl_max_burst;

    return rl->rl_current_rate;
}

/*****************************************************************************
 * rate_limit_consume()
 ****************************************************************************/
static inline void rate_limit_consume(rate_limit_t *rl, uint32_t count)
{
    rl->rl_current_rate -= count;
}

#endif /* _H_TPG_RATE_ */

