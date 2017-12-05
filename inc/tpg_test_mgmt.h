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
 *     tpg_test_mgmt.h
 *
 * Description:
 *     WARP17 test manager.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     08/12/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_MGMT_
#define _H_TPG_TEST_MGMT_

/*****************************************************************************
 * Test module message types.
 ****************************************************************************/
enum test_mgmt_msg_types {

    MSG_TYPE_DEF_START_MARKER(TEST_MGMT),
    MSG_TEST_MGMT_START_TEST,
    MSG_TEST_MGMT_STOP_TEST,
    MSG_TYPE_DEF_END_MARKER(TEST_MGMT),

};

MSG_TYPE_MAX_CHECK(TEST_MGMT);

/*****************************************************************************
 * Test suites/entries/criteria definitions and initializers
 ****************************************************************************/
#define CRIT_RUN_TIME(time) ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__RUN_TIME, .tc_run_time = TPG_DELAY(time)})

#define CRIT_RUN_TIME_INFINITE() ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__RUN_TIME, .tc_run_time = TPG_DELAY_INF()})

#define CRIT_SRV_UP(cnt) ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__SRV_UP,  .tc_srv_up = (cnt)})

#define CRIT_CL_UP(cnt) ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__CL_UP, .tc_cl_up = (cnt)})

#define CRIT_CL_ESTAB(cnt) ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__CL_ESTAB, .tc_cl_estab = (cnt)})

#define CRIT_DATA_MB(cnt) ((tpg_test_criteria_t) \
    {.tc_crit_type = TEST_CRIT_TYPE__DATAMB_SENT, .tc_data_mb_sent = (cnt)})

/* Servers PASSED doesn't mean we're done with the server test case. Server
 * test cases must be explicitly stopped!
 */
#define TEST_CASE_RUNNING(te, state)             \
    ((state) == TEST_CASE_STATE__RUNNING ||      \
     ((te)->tc_type == TEST_CASE_TYPE__SERVER && \
      (state) == TEST_CASE_STATE__PASSED))

/*****************************************************************************
 * Test mgmt module message type definitions.
 ****************************************************************************/
typedef struct test_start_stop_msg_s {

    uint32_t tssm_eth_port;

} __tpg_msg test_start_stop_msg_t;

typedef test_start_stop_msg_t test_start_msg_t;
typedef test_start_stop_msg_t test_stop_msg_t;

/*****************************************************************************
 * Type definitions for the Test MGMT module.
 ****************************************************************************/
typedef struct test_env_s test_env_t;

typedef struct test_env_tmr_arg_s {

    uint32_t    teta_eth_port;
    uint32_t    teta_test_case_id;
    test_env_t *teta_test_env;

} test_env_tmr_arg_t;

/* Per test case operational structure. */
typedef struct test_env_oper_state_s {

    struct rte_timer      teos_timer;
    test_env_tmr_arg_t    teos_timer_arg;
    tpg_test_case_state_t teos_test_case_state;
    tpg_test_criteria_t   teos_result;

    uint64_t              teos_start_time;
    uint64_t              teos_stop_time;

    uint32_t              teos_configured   : 1;
    uint32_t              teos_update_rates : 1;
    /* uint32_t           teos_unused       : 30; */

} test_env_oper_state_t;

struct test_env_s {

    tpg_port_cfg_t        te_port_cfg;

    struct {

        tpg_test_case_t        cfg;
        test_env_oper_state_t  state;
        sockopt_t              sockopt;

    } te_test_cases[TPG_TEST_MAX_ENTRIES];

    uint32_t              te_test_cases_count;

    uint32_t              te_test_running       : 1;
    uint32_t              te_test_case_to_start : 1;
    /* uint32_t           te_unused             : 30; */

    uint32_t              te_test_case_next;

};

/*****************************************************************************
 * Test suites helpers
 ****************************************************************************/
#define TEST_CASE_FOREACH_START(tenv, tcid, tc, op_state)       \
    for ((tcid) = 0; (tcid) < TPG_TEST_MAX_ENTRIES; (tcid)++) { \
        (tc) = &tenv->te_test_cases[(tcid)].cfg;                \
        (op_state) = &tenv->te_test_cases[(tcid)].state;        \
        if (!(op_state)->teos_configured)                       \
            continue;

#define TEST_CASE_FOREACH_END()                           \
    }

static inline tpg_test_case_t *test_mgmt_test_case_first(test_env_t *tenv)
{
    uint32_t tcid;

    for (tcid = 0; tcid < TPG_TEST_MAX_ENTRIES; tcid++) {
        if (tenv->te_test_cases[tcid].state.teos_configured)
            return &tenv->te_test_cases[tcid].cfg;
    }
    return NULL;
}


/*****************************************************************************
 * Externals for tpg_test_mgmt.c
 ****************************************************************************/
extern bool test_mgmt_cli_init(void);
extern bool test_mgmt_init(void);
extern int  test_mgmt_loop(void *arg);

extern test_env_t *test_mgmt_get_port_env(uint32_t eth_port);

extern tpg_gen_stats_t  *test_mgmt_get_stats(uint32_t eth_port,
                                             uint32_t tc_id);
extern tpg_rate_stats_t *test_mgmt_get_rate_stats(uint32_t eth_port,
                                                  uint32_t tc_id);
extern tpg_app_stats_t  *test_mgmt_get_app_stats(uint32_t eth_port,
                                                 uint32_t tc_id);


#endif /* _H_TPG_TEST_MGMT_ */

