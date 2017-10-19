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
 * rate_limit_init()
 ****************************************************************************/
int rate_limit_init(rate_limit_t *rl, uint32_t desired, uint32_t max_burst)
{
    global_config_t *gc;

    if (!rl)
        return -EINVAL;

    gc = cfg_get_config();
    if (unlikely(gc == NULL)) {
        TPG_ERROR_ABORT("[%d:%s()] NULL Global Config!\n",
                        rte_lcore_index(rte_lcore_id()),
                        __func__);
    }

    bzero(rl, sizeof(*rl));

    /* Zero means zero.. */
    if (desired == 0)
        return 0;

    rl->rl_max_burst = max_burst;

    /* UINT32_MAX is a special case corresponding to "infinity" and we just
     * do our best and send as fast as possible. We don't need multiple
     * intervals, one is enough.
     */
    if (desired == UINT32_MAX) {
        rl->rl_rate_high = UINT32_MAX;
        rl->rl_current_rate = rl->rl_rate_high;
        rl->rl_interval_count = 1;
        rl->rl_rate_low_index = rl->rl_interval_count;
        return 0;
    }

    rl->rl_interval_count = TPG_SEC_TO_USEC / gc->gcfg_rate_min_interval_size;

    if (desired % rl->rl_interval_count == 0) {
        rl->rl_rate_high = desired / rl->rl_interval_count;
        rl->rl_rate_low_index = rl->rl_interval_count;
    } else {
        rl->rl_rate_high =
            (desired + rl->rl_interval_count - 1) / rl->rl_interval_count;
        rl->rl_rate_low_index = (desired % rl->rl_interval_count);
        assert(rl->rl_rate_high * rl->rl_rate_low_index +
               (rl->rl_rate_high - 1) *
                    (rl->rl_interval_count - rl->rl_rate_low_index) == desired);
    }

    rl->rl_current_rate = rl->rl_rate_high;

    return 0;
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
    if (rl->rl_interval_count == 0)
        return 0;

    /* If the current rate limiting is "unlimited" then we should "relax"
     * the interval length too as there's no point to check the rate too
     * often.
     */
    if (rl->rl_rate_high == UINT32_MAX && rl->rl_interval_count == 1)
        return gc->gcfg_rate_no_lim_interval_size;

    /* TODO: for now we return a fixed interval length. Might be that in the
     * future we return this times a multiplier to emulate different rate
     * limiting patterns.
     */
    return gc->gcfg_rate_min_interval_size;
}


