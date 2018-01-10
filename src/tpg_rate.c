/*
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * Copyright (c) 2016, Juniper Networks, Inc. All rights reserved.
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
 *     tpg_rate.c
 *
 * Description:
 *     Rate limiting implementation
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
 * Include files
 ****************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Global functions
 ****************************************************************************/

/*****************************************************************************
 * rate_limit_cfg_init()
 ****************************************************************************/
void rate_limit_cfg_init(const tpg_rate_t *target, rate_limit_cfg_t *cfg)
{
    uint32_t rate;
    uint32_t min_value;
    uint32_t index;
    uint32_t step;
    uint32_t i;

    bzero(cfg, sizeof(*cfg));

    if (TPG_RATE_IS_INF(target)) {
        *cfg = RATE_CFG_INF();
        return;
    }

    rate = TPG_RATE_VAL(target);

    if (rate == 0) {
        *cfg = RATE_CFG_ZERO();
        return;
    }

    /* If the rate is less then the max number of slots then use less slots. */
    cfg->rlc_count = TPG_MIN(rate, GCFG_RATE_MAX_SLOT_COUNT);
    cfg->rlc_target = rate;

    /* First divide equally what we can. */
    min_value = rate / cfg->rlc_count;
    for (i = 0; i < cfg->rlc_count; i++)
        cfg->rlc_slots[i] = min_value;

    /* Now divide the rest in a uniform way. */
    rate = rate % cfg->rlc_count;

    if (rate == 0)
        return;

    step = cfg->rlc_count / rate;
    while (rate) {

        index = cfg->rlc_count;
        for (;;) {
            for (i = 0; i < cfg->rlc_count; i++) {
                if (cfg->rlc_slots[i] == min_value)
                    break;
            }
            if (i != cfg->rlc_count) {
                index = i;
                break;
            }

            min_value++;
        }
        assert(index < cfg->rlc_count);

        for (i = index; i < cfg->rlc_count && rate; i += step) {
            cfg->rlc_slots[i]++;
            rate--;
        }

        step++;
    }
}

/*****************************************************************************
 * rate_limit_init()
 ****************************************************************************/
int rate_limit_init(rate_limit_t *rl, rate_limit_cfg_t *cfg,
                    uint32_t lcore_id,
                    uint32_t displacement,
                    uint32_t desired,
                    uint32_t max_burst)
{
    uint32_t step;
    uint32_t i;

    bzero(rl, sizeof(*rl));

    if (desired == 0)
        return 0;

    rl->rl_slots = rte_zmalloc_socket("rate_limit",
                                      cfg->rlc_count * sizeof(*rl->rl_slots),
                                      RTE_CACHE_LINE_SIZE,
                                      rte_lcore_to_socket_id(lcore_id));
    if (rl->rl_slots == NULL)
        return -ENOMEM;

    rl->rl_max_burst = max_burst;

    if (desired == TPG_RATE_LIM_INFINITE_VAL) {
        rl->rl_count = 1;
        rl->rl_slots[0] = TPG_RATE_LIM_INFINITE_VAL;
        rl->rl_current_rate = TPG_RATE_LIM_INFINITE_VAL;
        return 0;
    }

    step = displacement;

    while (desired) {
        for (i = 0; i < cfg->rlc_count; i++)
            if (cfg->rlc_slots[i] > 0)
                break;

        for (; i < cfg->rlc_count && desired; i += step) {
            if (cfg->rlc_slots[i]) {
                rl->rl_slots[i]++;
                cfg->rlc_slots[i]--;
                desired--;
            }
        }

        step++;
        step %= cfg->rlc_count;

        if (step == 0)
            step++;
    }

    assert(desired == 0);

    rl->rl_current_rate = rl->rl_slots[0];
    rl->rl_count = cfg->rlc_count;
    rl->rl_current_index = 0;


    return 0;
}

/*****************************************************************************
 * rate_limit_free()
 ****************************************************************************/
void rate_limit_free(rate_limit_t *rl)
{
    rte_free(rl->rl_slots);

    bzero(rl, sizeof(*rl));
}

/*****************************************************************************
 * rate_limit_interval_us()
 ****************************************************************************/
uint32_t rate_limit_interval_us(rate_limit_t *rl)
{
    global_config_t *gc;

    gc = cfg_get_config();
    if (unlikely(gc == NULL)) {
        TPG_ERROR_ABORT("[%d:%s()] NULL Global Config!\n",
                        rte_lcore_index(rte_lcore_id()),
                        __func__);
    }

    /* If the current rate limiting is 0 the interval size is also 0. */
    if (rl->rl_count == 0)
        return 0;

    /* If the current rate limiting is "unlimited" then we should "relax"
     * the interval length too as there's no point to check the rate too
     * often.
     */
    if (rl->rl_count == 1 && rl->rl_slots[0] == TPG_RATE_LIM_INFINITE_VAL)
        return gc->gcfg_rate_no_lim_interval_size;

    /* Split the second in subintervals based on rl_count. */
    return TPG_SEC_TO_USEC / rl->rl_count;
}


