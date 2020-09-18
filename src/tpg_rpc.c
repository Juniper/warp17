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
 *     tpg_rpc.c
 *
 * Description:
 *     RPC server implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     01/11/2016
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ****************************************************************************/
#include <protobuf-c-rpc/protobuf-c-rpc.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Local defines
 ****************************************************************************/
/* TPG RPC TCP port. */
#define TPG_RPC_TCP_PORT "42424"

/*****************************************************************************
 * Macro helpers for hiding the RPC internals
 ****************************************************************************/
#define RPC_REQUEST_INIT(in_type, in, out) \
    tpg_xlate_protoc_##in_type((in), (out))

#define RPC_INIT_DEFAULT(out_type, out) \
    (tpg_xlate_default_##out_type(out))    \

#define RPC_STORE_RETCODE(err_fld, err) \
    ((err_fld).e_code = (err))

#define RPC_REPLY(result_type, result, result_init, tpg_result)         \
    do {                                                                \
        (result) = (__typeof__(result_type)) result_init;               \
        if (tpg_xlate_tpg_##result_type(&(tpg_result), &(result)) == 0) \
            closure(&(result), closure_data);                           \
    } while (0)

#define RPC_CLEANUP(in_rpc_type, tpg_input, out_rpc_type, result) \
    do {                                                          \
        tpg_xlate_tpg_free_##in_rpc_type(&(tpg_input));           \
        tpg_xlate_free_##out_rpc_type(&(result));                 \
    } while (0)

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static void tpg_rpc__get_version(Warp17_Service *service,
                                 const GetVersionArg *input,
                                 VersionResult_Closure closure,
                                 void *closure_data);

static void tpg_rpc__configure_port(Warp17_Service *service, const PortCfg *input,
                                    Error_Closure closure,
                                    void *closure_data);

static void tpg_rpc__get_port_cfg(Warp17_Service *service,
                                  const PortArg *input,
                                  PortCfgResult_Closure closure,
                                  void *closure_data);

static void tpg_rpc__configure_l3_intf(Warp17_Service *service,
                                       const L3IntfArg *input,
                                       Error_Closure closure,
                                       void *closure_data);

static void tpg_rpc__configure_l3_gw(Warp17_Service *service,
                                     const L3GwArg *input,
                                     Error_Closure closure,
                                     void *closure_data);

static void tpg_rpc__configure_test_case(Warp17_Service *service,
                                         const TestCase *input,
                                         Error_Closure closure,
                                         void *closure_data);

static void tpg_rpc__get_test_case(Warp17_Service *service,
                                   const TestCaseArg *input,
                                   TestCaseResult_Closure closure,
                                   void *closure_data);

static void tpg_rpc__del_test_case(Warp17_Service *service,
                                   const TestCaseArg *input,
                                   Error_Closure closure,
                                   void *closure_data);

static void tpg_rpc__configure_imix_group(Warp17_Service *service,
                                         const ImixGroup *input,
                                         Error_Closure closure,
                                         void *closure_data);

static void tpg_rpc__get_imix_group(Warp17_Service *service,
                                   const ImixArg *input,
                                   ImixResult_Closure closure,
                                   void *closure_data);

static void tpg_rpc__del_imix_group(Warp17_Service *service,
                                    const ImixArg *input,
                                    Error_Closure closure,
                                    void *closure_data);

static void tpg_rpc__update_test_case(Warp17_Service *service,
                                      const UpdateArg *input,
                                      Error_Closure closure,
                                      void *closure_data);

static void tpg_rpc__get_test_case_app(Warp17_Service *service,
                                       const TestCaseArg *input,
                                       TestCaseAppResult_Closure closure,
                                       void *closure_data);

static void tpg_rpc__update_test_case_app(Warp17_Service *service,
                                          const UpdateAppArg *input,
                                          Error_Closure closure,
                                          void *closure_data);

static void tpg_rpc__set_port_options(Warp17_Service *service,
                                      const PortOptionsArg *input,
                                      Error_Closure closure,
                                      void *closure_data);

static void tpg_rpc__get_port_options(Warp17_Service *service,
                                      const PortArg *input,
                                      PortOptionsResult_Closure closure,
                                      void *closure_data);

static void tpg_rpc__set_tcp_sockopt(Warp17_Service *service,
                                     const TcpSockoptArg *input,
                                     Error_Closure closure,
                                     void *closure_data);

static void tpg_rpc__get_tcp_sockopt(Warp17_Service *service,
                                     const TestCaseArg *input,
                                     TcpSockoptResult_Closure closure,
                                     void *closure_data);

static void tpg_rpc__set_ipv4_sockopt(Warp17_Service *service,
                                      const Ipv4SockoptArg *input,
                                      Error_Closure closure,
                                      void *closure_data);

static void tpg_rpc__get_ipv4_sockopt(Warp17_Service *service,
                                      const TestCaseArg *input,
                                      Ipv4SockoptResult_Closure closure,
                                      void *closure_data);

static void tpg_rpc__set_vlan_sockopt(Warp17_Service *service,
                                      const VlanSockoptArg *input,
                                      Error_Closure closure,
                                      void *closure_data);

static void tpg_rpc__get_vlan_sockopt(Warp17_Service *service,
                                      const TestCaseArg *input,
                                      VlanSockoptResult_Closure closure,
                                      void *closure_data);

static void tpg_rpc__port_start(Warp17_Service *service,
                                const PortArg *input,
                                Error_Closure closure,
                                void *closure_data);

static void tpg_rpc__port_stop(Warp17_Service *service,
                               const PortArg *input,
                               Error_Closure closure,
                               void *closure_data);

static void tpg_rpc__get_statistics(Warp17_Service *service,
                                    const PortArg *input,
                                    StatsResult_Closure  closure,
                                    void *closure_data);

static void tpg_rpc__clear_statistics(Warp17_Service *service,
                                const PortArg *input,
                                Error_Closure closure,
                                void *closure_data);

static void tpg_rpc__get_test_status(Warp17_Service *service,
                                     const TestCaseArg *input,
                                     TestStatusResult_Closure closure,
                                     void *closure_data);

static void tpg_rpc__get_imix_statistics(Warp17_Service *service,
                                         const ImixArg *input,
                                         ImixStatsResult_Closure closure,
                                         void *closure_data);


/*****************************************************************************
 * Globals
 ****************************************************************************/
static Warp17_Service tpg_service = WARP17__INIT(tpg_rpc__);

static ProtobufC_RPC_Server *rpc_server;

static rte_atomic32_t        rpc_server_initialized;

/*****************************************************************************
 * Externals
 ****************************************************************************/
/*****************************************************************************
 * rpc_init()
 ****************************************************************************/
bool rpc_init(void)
{
    if (!rte_atomic32_cmpset((volatile uint32_t *)&rpc_server_initialized.cnt,
                             false,
                             true)) {
        /* Someone already initialized the RPC server! */
        return false;
    }

    /* protobuf_c_rpc_dispatch_default is a singleton but let's make sure we
     * can initialize it!
     */
    if (!protobuf_c_rpc_dispatch_default())
        return false;

    rpc_server = protobuf_c_rpc_server_new(PROTOBUF_C_RPC_ADDRESS_TCP,
                                           TPG_RPC_TCP_PORT,
                                           (ProtobufCService *) &tpg_service,
                                           protobuf_c_rpc_dispatch_default());
    if (!rpc_server)
        return false;

    return true;
}

/*****************************************************************************
 * rpc_dispatch()
 ****************************************************************************/
void rpc_dispatch(void)
{
    /* No timeout option for running protobuf server in a non-blocking mode.
     * So we do this hack..
     */
    void empty(ProtobufCRPCDispatch *d __rte_unused, void *arg __rte_unused) {}

    protobuf_c_rpc_dispatch_add_idle(protobuf_c_rpc_dispatch_default(),
                                     empty,
                                     NULL);
    protobuf_c_rpc_dispatch_run(protobuf_c_rpc_dispatch_default());
}

/*****************************************************************************
 * rpc_destroy()
 ****************************************************************************/
void rpc_destroy(void)
{
    if (!rte_atomic32_cmpset((volatile uint32_t *)&rpc_server_initialized.cnt,
                             true,
                             false)) {
        /* Someone already initialized the RPC server! */
        return;
    }

    if (!rpc_server)
        return;

    protobuf_c_rpc_server_destroy(rpc_server, false);
}

/*****************************************************************************
 * RPC Union Translations
 ****************************************************************************/
/*****************************************************************************
 * tpg_xlate_tpg_union_Ip()
 ****************************************************************************/
int tpg_xlate_tpg_union_Ip(const tpg_ip_t *in, Ip *out)
{
    if (in->ip_version == IP_V__IPV4)
        TPG_XLATE_UNION_SET_FIELD(out, in, ip_v4);
    else
        return -EINVAL;
    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_L4Client()
 ****************************************************************************/
int tpg_xlate_tpg_union_L4Client(const tpg_l4_client_t *in, L4Client *out)
{
    if (in->l4c_proto == L4_PROTO__UDP ||
        in->l4c_proto == L4_PROTO__TCP) {

        out->l4c_tcp_udp = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->l4c_tcp_udp), 0);

        if (!out->l4c_tcp_udp)
            return -ENOMEM;

        tpg_xlate_tpg_TcpUdpClient(&in->l4c_tcp_udp, out->l4c_tcp_udp);
    } else
        return -EINVAL;

    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_L4Server()
 ****************************************************************************/
int tpg_xlate_tpg_union_L4Server(const tpg_l4_server_t *in, L4Server *out)
{
    if (in->l4s_proto == L4_PROTO__UDP || in->l4s_proto == L4_PROTO__TCP) {
        out->l4s_tcp_udp = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->l4s_tcp_udp),
                                       0);
        if (!out->l4s_tcp_udp)
            return -ENOMEM;

        tpg_xlate_tpg_TcpUdpServer(&in->l4s_tcp_udp, out->l4s_tcp_udp);
    } else
        return -EINVAL;

    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_TestCase()
 ****************************************************************************/
int tpg_xlate_tpg_union_TestCase(const tpg_test_case_t *in, TestCase *out)
{
    if (in->tc_type == TEST_CASE_TYPE__CLIENT) {
        out->tc_client = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tc_client), 0);
        if (!out->tc_client)
            return -ENOMEM;

        tpg_xlate_tpg_Client(&in->tc_client, out->tc_client);
    } else if (in->tc_type == TEST_CASE_TYPE__SERVER) {
        out->tc_server = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tc_server), 0);
        if (!out->tc_server)
            return -ENOMEM;

        tpg_xlate_tpg_Server(&in->tc_server, out->tc_server);
    } else {
        return -EINVAL;
    }
    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_App()
 ****************************************************************************/
int tpg_xlate_tpg_union_App(const tpg_app_t *in, App *out)
{
    switch (in->app_proto) {
    case APP_PROTO__RAW_CLIENT:
        out->app_raw_client =
            rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_raw_client), 0);
        if (!out->app_raw_client)
            return -ENOMEM;

        tpg_xlate_tpg_RawClient(&in->app_raw_client, out->app_raw_client);
        break;
    case APP_PROTO__RAW_SERVER:
        out->app_raw_server =
            rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_raw_server), 0);
        if (!out->app_raw_server)
            return -ENOMEM;

        tpg_xlate_tpg_RawServer(&in->app_raw_server, out->app_raw_server);
        break;
    case APP_PROTO__HTTP_CLIENT:
        out->app_http_client =
            rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_http_client), 0);
        if (!out->app_http_client)
            return -ENOMEM;

        tpg_xlate_tpg_HttpClient(&in->app_http_client, out->app_http_client);
        break;
    case APP_PROTO__HTTP_SERVER:
        out->app_http_server =
            rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_http_server), 0);
        if (!out->app_http_server)
            return -ENOMEM;

        tpg_xlate_tpg_HttpServer(&in->app_http_server, out->app_http_server);
        break;
    case APP_PROTO__IMIX:
        out->app_imix = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_imix), 0);
        if (!out->app_imix)
            return -ENOMEM;

        tpg_xlate_tpg_Imix(&in->app_imix, out->app_imix);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_TestCriteria()
 ****************************************************************************/
int tpg_xlate_tpg_union_TestCriteria(const tpg_test_criteria_t *in,
                                     TestCriteria *out)
{
    switch (in->tc_crit_type) {
    case TEST_CRIT_TYPE__RUN_TIME:
        TPG_XLATE_UNION_SET_FIELD(out, in, tc_run_time_s);
        break;
    case TEST_CRIT_TYPE__SRV_UP:
        TPG_XLATE_UNION_SET_FIELD(out, in, tc_srv_up);
        break;
    case TEST_CRIT_TYPE__CL_UP:
        TPG_XLATE_UNION_SET_FIELD(out, in, tc_cl_up);
        break;
    case TEST_CRIT_TYPE__CL_ESTAB:
        TPG_XLATE_UNION_SET_FIELD(out, in, tc_cl_estab);
        break;
    case TEST_CRIT_TYPE__DATAMB_SENT:
        TPG_XLATE_UNION_SET_FIELD(out, in, tc_data_mb_sent);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpgTestAppStats_by_proto()
 ****************************************************************************/
static int
tpg_xlate_tpgTestAppStats_by_proto(const tpg_app_stats_t *in, AppStats *out,
                                   tpg_app_proto_t app_proto)
{
    int err;

    *out = (AppStats)APP_STATS__INIT;
    switch (app_proto) {
    case APP_PROTO__RAW_CLIENT:
        /* Fallthrough */
    case APP_PROTO__RAW_SERVER:
        out->as_raw = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_raw), 0);
        if (!out->as_raw)
            return -ENOMEM;

        err = tpg_xlate_tpg_RawStats(&in->as_raw, out->as_raw);
        if (err)
            return err;
        break;
    case APP_PROTO__HTTP_CLIENT:
        /* Fallthrough */
    case APP_PROTO__HTTP_SERVER:
        out->as_http = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_http), 0);
        if (!out->as_http)
            return -ENOMEM;

        err = tpg_xlate_tpg_HttpStats(&in->as_http, out->as_http);
        if (err)
            return err;
        break;
    case APP_PROTO__IMIX:
        out->as_imix = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_imix), 0);
        if (!out->as_imix)
            return -ENOMEM;
        err = tpg_xlate_tpg_ImixStats(&in->as_imix, out->as_imix);
        if (err)
            return err;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * tpg_LatencyStats_adjust()
 *  NOTE: zero out min latency if we have no samples
 ****************************************************************************/
static void tpg_LatencyStats_adjust(LatencyStats *stats)
{
    if (stats->ls_samples_count == 0)
        stats->ls_min_latency = 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_TestStatusResult()
 ****************************************************************************/
int tpg_xlate_tpg_TestStatusResult(const tpg_test_status_result_t *in,
                                   TestStatusResult *out)
{
    int err;

    *out = (TestStatusResult)TEST_STATUS_RESULT__INIT;
    out->tsr_error = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tsr_error), 0);
    if (!out->tsr_error)
        return -ENOMEM;

    err = tpg_xlate_tpg_Error(&in->tsr_error, out->tsr_error);
    if (err)
        return err;

    /* Stop if an error code was set. */
    if (in->tsr_error.e_code != 0)
        return 0;

    out->tsr_state = in->tsr_state;
    out->tsr_type = in->tsr_type;
    out->tsr_l4_proto = in->tsr_l4_proto;
    out->tsr_app_proto = in->tsr_app_proto;

    out->tsr_stats = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tsr_stats), 0);

    out->tsr_rate_stats = rte_zmalloc("TPG_RPC_GEN",
                                      sizeof(*out->tsr_rate_stats),
                                      0);

    out->tsr_app_stats = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tsr_app_stats),
                                     0);

    if (!out->tsr_stats || !out->tsr_rate_stats || !out->tsr_app_stats)
        return -ENOMEM;

    /* Translate GenStats manually. */
    *out->tsr_stats = (GenStats)GEN_STATS__INIT;

    out->tsr_stats->gs_up = in->tsr_stats.gs_up;
    out->tsr_stats->gs_estab = in->tsr_stats.gs_estab;
    out->tsr_stats->gs_down = in->tsr_stats.gs_down;
    out->tsr_stats->gs_failed = in->tsr_stats.gs_failed;
    out->tsr_stats->gs_data_failed = in->tsr_stats.gs_data_failed;
    out->tsr_stats->gs_data_null = in->tsr_stats.gs_data_null;
    out->tsr_stats->gs_data_failed = in->tsr_stats.gs_data_failed;
    out->tsr_stats->gs_data_null = in->tsr_stats.gs_data_null;

    out->tsr_stats->gs_start_time = in->tsr_stats.gs_start_time / cycles_per_us;
    out->tsr_stats->gs_end_time = in->tsr_stats.gs_end_time / cycles_per_us;

    out->tsr_stats->gs_latency_stats =
        rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tsr_stats->gs_latency_stats),
                    0);
    if (!out->tsr_stats->gs_latency_stats)
        return -ENOMEM;

    err = tpg_xlate_tpg_GenLatencyStats(&in->tsr_stats.gs_latency_stats,
                                        out->tsr_stats->gs_latency_stats);
    if (err)
        return err;

    /* Zero out min latency stats if we don't have any samples. */
    tpg_LatencyStats_adjust(out->tsr_stats->gs_latency_stats->gls_stats);

    /* Zero out recent latency stats if we don't have any samples. */
    tpg_LatencyStats_adjust(out->tsr_stats->gs_latency_stats->gls_sample_stats);

    /* Translate RateStats. */
    err = tpg_xlate_tpg_RateStats(&in->tsr_rate_stats, out->tsr_rate_stats);
    if (err)
        return err;

    /* Translate AppStats manually. */
    err = tpg_xlate_tpgTestAppStats_by_proto(&in->tsr_app_stats,
                                             out->tsr_app_stats,
                                             in->tsr_app_proto);
    if (err)
        return err;

    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_ImixAppStats()
 ****************************************************************************/
int tpg_xlate_tpg_ImixAppStats(const tpg_imix_app_stats_t *in,
                               ImixAppStats *out)
{
    uint32_t i;

    *out = (ImixAppStats)IMIX_APP_STATS__INIT;

    out->ias_imix_id = in->ias_imix_id;

    out->ias_app_protos =
        rte_zmalloc("TPG_RPC_GEN",
                    sizeof(*out->ias_app_protos) * in->ias_app_protos_count,
                    0);
    out->ias_apps =
        rte_zmalloc("TPG_RPC_GEN", sizeof(*out->ias_apps) * in->ias_apps_count,
                    0);
    if (!out->ias_app_protos || !out->ias_apps)
        return -ENOMEM;

    out->n_ias_app_protos = in->ias_app_protos_count;
    out->n_ias_apps = in->ias_apps_count;

    for (i = 0; i < in->ias_app_protos_count; i++) {
        int err;

        out->ias_app_protos[i] = in->ias_app_protos[i];

        out->ias_apps[i] =
            rte_zmalloc("TPG_RPC_GEN", sizeof(*out->ias_apps[i]), 0);
        if (!out->ias_apps[i])
            return -ENOMEM;

        err = tpg_xlate_tpgTestAppStats_by_proto(&in->ias_apps[i],
                                                 out->ias_apps[i],
                                                 in->ias_app_protos[i]);
        if (err)
            return err;
    }

    return 0;
}

/*****************************************************************************
 * RPC Callbacks
 ****************************************************************************/
/*****************************************************************************
 * tpg_rpc__get_version()
 ****************************************************************************/
static void tpg_rpc__get_version(Warp17_Service *service __rte_unused,
                                 const GetVersionArg *input __rte_unused,
                                 VersionResult_Closure closure,
                                 void *closure_data)
{
    VersionResult result = VERSION_RESULT__INIT;
    int           ver_len;

    ver_len = snprintf(NULL, 0, TPG_VERSION_PRINTF_STR,
                       TPG_VERSION_PRINTF_ARGS);
    if (ver_len >= 0) {
        char version[ver_len + 1];

        snprintf(version, ver_len + 1, TPG_VERSION_PRINTF_STR,
                 TPG_VERSION_PRINTF_ARGS);
        result.vr_version = version;
        closure(&result, closure_data);
    } else {
        result.vr_version = NULL;
        closure(&result, closure_data);
    }
}

/*****************************************************************************
 * tpg_rpc__configure_port()
 ****************************************************************************/
static void tpg_rpc__configure_port(Warp17_Service *service __rte_unused,
                                    const PortCfg *input,
                                    Error_Closure closure,
                                    void *closure_data)
{
    tpg_port_cfg_t port_cfg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(PortCfg, input, &port_cfg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_add_port_cfg(port_cfg.pc_eth_port, &port_cfg,
                                             NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortCfg, port_cfg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_port_cfg()
 ****************************************************************************/
static void tpg_rpc__get_port_cfg(Warp17_Service *service __rte_unused,
                                  const PortArg *input,
                                  PortCfgResult_Closure closure,
                                  void *closure_data)
{
    tpg_port_arg_t         port_arg;
    tpg_port_cfg_result_t  tpg_result;
    PortCfgResult          protoc_result;
    const tpg_port_cfg_t  *port_cfg;

    RPC_INIT_DEFAULT(PortCfgResult, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    port_cfg = test_mgmt_get_port_cfg(port_arg.pa_eth_port, NULL);
    if (port_cfg) {
        RPC_STORE_RETCODE(tpg_result.pcr_error, 0);
        tpg_result.pcr_cfg = *port_cfg;
    } else {
        RPC_STORE_RETCODE(tpg_result.pcr_error, -ENOENT);
    }
    RPC_REPLY(PortCfgResult, protoc_result, PORT_CFG_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(PortArg, port_arg, PortCfgResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__configure_l3_intf()
 ****************************************************************************/
static void tpg_rpc__configure_l3_intf(Warp17_Service *service __rte_unused,
                                       const L3IntfArg *input,
                                       Error_Closure closure,
                                       void *closure_data)
{
    tpg_l3_intf_arg_t l3_intf_arg;
    tpg_error_t       tpg_result;
    Error             protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(L3IntfArg, input, &l3_intf_arg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_add_port_cfg_l3_intf(l3_intf_arg.lia_eth_port,
                                                     &l3_intf_arg.lia_l3_intf,
                                                     NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(L3IntfArg, l3_intf_arg, Error, protoc_result);
}


/*****************************************************************************
 * tpg_rpc__configure_l3_gw()
 ****************************************************************************/
static void tpg_rpc__configure_l3_gw(Warp17_Service *service __rte_unused,
                                     const L3GwArg *input,
                                     Error_Closure closure,
                                     void *closure_data)
{
    tpg_l3_gw_arg_t l3_gw_arg;
    tpg_error_t     tpg_result;
    Error           protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(L3GwArg, input, &l3_gw_arg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_add_port_cfg_l3_gw(l3_gw_arg.lga_eth_port,
                                                   &l3_gw_arg.lga_gw,
                                                   NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(L3GwArg, l3_gw_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__update_test_case()
 ****************************************************************************/
static void tpg_rpc__update_test_case(Warp17_Service *service __rte_unused,
                                      const UpdateArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_update_arg_t upd_arg;
    tpg_error_t      tpg_result;
    Error            protoc_result;
    int              err;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(UpdateArg, input, &upd_arg))
        return;

    err = test_mgmt_update_test_case(upd_arg.ua_tc_arg.tca_eth_port,
                                     upd_arg.ua_tc_arg.tca_test_case_id,
                                     &upd_arg,
                                     NULL);

    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(UpdateArg, upd_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_test_case_app()
 ****************************************************************************/
static void tpg_rpc__get_test_case_app(Warp17_Service *service __rte_unused,
                                       const TestCaseArg *input,
                                       TestCaseAppResult_Closure closure,
                                       void *closure_data)
{
    tpg_test_case_arg_t        tc_arg;
    tpg_test_case_app_result_t tpg_result;
    TestCaseAppResult          protoc_result;
    int                        err;

    RPC_INIT_DEFAULT(TestCaseAppResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_test_case_app_cfg(tc_arg.tca_eth_port,
                                          tc_arg.tca_test_case_id,
                                          &tpg_result.tcar_app,
                                          NULL);

    RPC_STORE_RETCODE(tpg_result.tcar_error, err);

    RPC_REPLY(TestCaseAppResult, protoc_result, TEST_CASE_APP_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, TestCaseAppResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__update_test_case_app()
 ****************************************************************************/
static void tpg_rpc__update_test_case_app(Warp17_Service *service __rte_unused,
                                          const UpdateAppArg *input,
                                          Error_Closure closure,
                                          void *closure_data)
{
    tpg_update_app_arg_t update_arg;
    tpg_error_t          tpg_result;
    Error                protoc_result;
    int                  err;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(UpdateAppArg, input, &update_arg))
        return;

    err = test_mgmt_update_test_case_app(update_arg.uaa_tc_arg.tca_eth_port,
                                         update_arg.uaa_tc_arg.tca_test_case_id,
                                         &update_arg.uaa_app,
                                         NULL);

    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(UpdateAppArg, update_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__configure_test_case()
 ****************************************************************************/
static void tpg_rpc__configure_test_case(Warp17_Service *service __rte_unused,
                                         const TestCase *input,
                                         Error_Closure closure,
                                         void *closure_data)
{
    tpg_test_case_t test_case;
    tpg_error_t     tpg_result;
    Error           protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(TestCase, input, &test_case))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_add_test_case(test_case.tc_eth_port,
                                              &test_case,
                                              NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(TestCase, test_case, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_test_case_cfg()
 ****************************************************************************/
static void tpg_rpc__get_test_case(Warp17_Service *service __rte_unused,
                                   const TestCaseArg *input,
                                   TestCaseResult_Closure closure,
                                   void *closure_data)
{
    tpg_test_case_arg_t    tc_cfg_get;
    tpg_test_case_result_t tpg_result;
    TestCaseResult         protoc_result;
    int                    err;

    RPC_INIT_DEFAULT(TestCaseResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_cfg_get))
        return;

    err = test_mgmt_get_test_case_cfg(tc_cfg_get.tca_eth_port,
                                      tc_cfg_get.tca_test_case_id,
                                      &tpg_result.tcr_cfg,
                                      NULL);

    RPC_STORE_RETCODE(tpg_result.tcr_error, err);

    RPC_REPLY(TestCaseResult, protoc_result, TEST_CASE_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_cfg_get, TestCaseResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__del_test_case()
 ****************************************************************************/
static void tpg_rpc__del_test_case(Warp17_Service *service __rte_unused,
                                   const TestCaseArg *input,
                                   Error_Closure closure,
                                   void *closure_data)
{
    tpg_test_case_arg_t msg;
    tpg_error_t         tpg_result;
    Error               protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &msg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_del_test_case(msg.tca_eth_port,
                                              msg.tca_test_case_id,
                                              NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(TestCaseArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__configure_imix_group()
 ****************************************************************************/
static void tpg_rpc__configure_imix_group(Warp17_Service *service __rte_unused,
                                         const ImixGroup *input,
                                         Error_Closure closure,
                                         void *closure_data)
{
    tpg_imix_group_t imix_group;
    tpg_error_t      tpg_result;
    Error            protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(ImixGroup, input, &imix_group))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_add_imix_group(imix_group.imix_id, &imix_group,
                                               NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(ImixGroup, imix_group, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_imix_group()
 ****************************************************************************/
static void tpg_rpc__get_imix_group(Warp17_Service *service __rte_unused,
                                   const ImixArg *input,
                                   ImixResult_Closure closure,
                                   void *closure_data)
{
    tpg_imix_arg_t    tc_imix_get;
    tpg_imix_result_t tpg_result;
    ImixResult        protoc_result;
    int               err;

    RPC_INIT_DEFAULT(ImixResult, &tpg_result);
    if (RPC_REQUEST_INIT(ImixArg, input, &tc_imix_get))
        return;

    err = test_mgmt_get_imix_group(tc_imix_get.ia_imix_id,
                                   &tpg_result.ir_imix_group,
                                   NULL);

    RPC_STORE_RETCODE(tpg_result.ir_error, err);

    RPC_REPLY(ImixResult, protoc_result, IMIX_RESULT__INIT, tpg_result);
    RPC_CLEANUP(ImixArg, tc_imix_get, ImixResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__del_imix_group()
 ****************************************************************************/
static void tpg_rpc__del_imix_group(Warp17_Service *service __rte_unused,
                                    const ImixArg *input,
                                    Error_Closure closure,
                                    void *closure_data)
{
    tpg_imix_arg_t msg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(ImixArg, input, &msg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_del_imix_group(msg.ia_imix_id, NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(ImixArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__set_port_options()
 ****************************************************************************/
static void tpg_rpc__set_port_options(Warp17_Service *service __rte_unused,
                                      const PortOptionsArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_port_options_arg_t msg;
    tpg_error_t            tpg_result;
    Error                  protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(PortOptionsArg, input, &msg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_set_port_options(msg.poa_port.pa_eth_port,
                                                 &msg.poa_opts,
                                                 NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortOptionsArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_port_options()
 ****************************************************************************/
static void tpg_rpc__get_port_options(Warp17_Service *service __rte_unused,
                                      const PortArg *input,
                                      PortOptionsResult_Closure closure,
                                      void *closure_data)
{
    tpg_port_arg_t            port_arg;
    tpg_port_options_result_t tpg_result;
    PortOptionsResult         protoc_result;
    int                       err;

    RPC_INIT_DEFAULT(PortOptionsResult, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    err = test_mgmt_get_port_options(port_arg.pa_eth_port,
                                     &tpg_result.por_opts,
                                     NULL);
    RPC_STORE_RETCODE(tpg_result.por_error, err);

    RPC_REPLY(PortOptionsResult, protoc_result, PORT_OPTIONS_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(PortArg, port_arg, PortOptionsResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__set_tcp_sockopt()
 ****************************************************************************/
static void tpg_rpc__set_tcp_sockopt(Warp17_Service *service __rte_unused,
                                     const TcpSockoptArg *input,
                                     Error_Closure closure,
                                     void *closure_data)
{
    tpg_tcp_sockopt_arg_t msg;
    tpg_error_t           tpg_result;
    Error                 protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(TcpSockoptArg, input, &msg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_set_tcp_sockopt(msg.toa_tc_arg.tca_eth_port,
                                                msg.toa_tc_arg.tca_test_case_id,
                                                &msg.toa_opts,
                                                NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(TcpSockoptArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_tcp_sockopt()
 ****************************************************************************/
static void tpg_rpc__get_tcp_sockopt(Warp17_Service *service __rte_unused,
                                     const TestCaseArg *input,
                                     TcpSockoptResult_Closure closure,
                                     void *closure_data)
{
    tpg_test_case_arg_t      tc_arg;
    tpg_tcp_sockopt_result_t tpg_result;
    TcpSockoptResult         protoc_result;
    int                      err;

    RPC_INIT_DEFAULT(TcpSockoptResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_tcp_sockopt(tc_arg.tca_eth_port,
                                    tc_arg.tca_test_case_id,
                                    &tpg_result.tor_opts,
                                    NULL);
    RPC_STORE_RETCODE(tpg_result.tor_error, err);

    RPC_REPLY(TcpSockoptResult, protoc_result, TCP_SOCKOPT_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, TcpSockoptResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__set_ipv4_sockopt()
 ****************************************************************************/
static void tpg_rpc__set_ipv4_sockopt(Warp17_Service *service __rte_unused,
                                      const Ipv4SockoptArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_ipv4_sockopt_arg_t msg;
    tpg_error_t            tpg_result;
    Error                  protoc_result;
    int                    err;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(Ipv4SockoptArg, input, &msg))
        return;

    err = test_mgmt_set_ipv4_sockopt(msg.i4sa_tc_arg.tca_eth_port,
                                     msg.i4sa_tc_arg.tca_test_case_id,
                                     &msg.i4sa_opts,
                                     NULL);
    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(Ipv4SockoptArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_ipv4_sockopt()
 ****************************************************************************/
static void tpg_rpc__get_ipv4_sockopt(Warp17_Service *service __rte_unused,
                                      const TestCaseArg *input,
                                      Ipv4SockoptResult_Closure closure,
                                      void *closure_data)
{
    tpg_test_case_arg_t       tc_arg;
    tpg_ipv4_sockopt_result_t tpg_result;
    Ipv4SockoptResult         protoc_result;
    int                       err = 0;

    RPC_INIT_DEFAULT(Ipv4SockoptResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_ipv4_sockopt(tc_arg.tca_eth_port,
                                     tc_arg.tca_test_case_id,
                                     &tpg_result.i4sr_opts,
                                     NULL);
    RPC_STORE_RETCODE(tpg_result.i4sr_error, err);

    RPC_REPLY(Ipv4SockoptResult, protoc_result, IPV4_SOCKOPT_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, Ipv4SockoptResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__set_vlan_sockopt()
 ****************************************************************************/
static void tpg_rpc__set_vlan_sockopt(Warp17_Service *service __rte_unused,
                                      const VlanSockoptArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_vlan_sockopt_arg_t msg;
    tpg_error_t            tpg_result;
    Error                  protoc_result;
    int                    err;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(VlanSockoptArg, input, &msg))
        return;

    err = test_mgmt_set_vlan_sockopt(msg.vosa_tc_arg.tca_eth_port,
                                     msg.vosa_tc_arg.tca_test_case_id,
                                     &msg.vosa_opts,
                                     NULL);
    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(VlanSockoptArg, msg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_vlan_sockopt()
 ****************************************************************************/
static void tpg_rpc__get_vlan_sockopt(Warp17_Service *service __rte_unused,
                                      const TestCaseArg *input,
                                      VlanSockoptResult_Closure closure,
                                      void *closure_data)
{
    tpg_test_case_arg_t       tc_arg;
    tpg_vlan_sockopt_result_t tpg_result;
    VlanSockoptResult         protoc_result;
    int                       err = 0;

    RPC_INIT_DEFAULT(VlanSockoptResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_vlan_sockopt(tc_arg.tca_eth_port,
                                     tc_arg.tca_test_case_id,
                                     &tpg_result.vosr_opts,
                                     NULL);
    RPC_STORE_RETCODE(tpg_result.vosr_error, err);

    RPC_REPLY(VlanSockoptResult, protoc_result, VLAN_SOCKOPT_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, VlanSockoptResult, protoc_result);
}
/*****************************************************************************
 * tpg_rpc__port_start()
 ****************************************************************************/
static void tpg_rpc__port_start(Warp17_Service *service __rte_unused,
                                const PortArg *input,
                                Error_Closure closure,
                                void *closure_data)
{
    tpg_port_arg_t port_arg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_start_port(port_arg.pa_eth_port, NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortArg, port_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__port_stop()
 ****************************************************************************/
static void tpg_rpc__port_stop(Warp17_Service *service __rte_unused,
                               const PortArg *input,
                               Error_Closure closure,
                               void *closure_data)
{
    tpg_port_arg_t port_arg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result, test_mgmt_stop_port(port_arg.pa_eth_port,
                                                      NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortArg, port_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_statistics()
 ****************************************************************************/
static void tpg_rpc__get_statistics(Warp17_Service *service __rte_unused,
                                    const PortArg *input,
                                    StatsResult_Closure  closure,
                                    void *closure_data)
{
    tpg_port_arg_t        port_arg;
    tpg_stats_result_t    tpg_result;
    StatsResult           protoc_result;

    RPC_INIT_DEFAULT(StatsResult, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_port_stats(port_arg.pa_eth_port,
                                               &tpg_result.sr_port, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_phy_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_phy, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_phy_rate_stats(port_arg.pa_eth_port,
                                                   &tpg_result.sr_phy_rate, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_eth_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_eth, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_arp_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_arp, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_route_stats(port_arg.pa_eth_port,
                                                &tpg_result.sr_route, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_ipv4_stats(port_arg.pa_eth_port,
                                               &tpg_result.sr_ipv4, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_udp_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_udp, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_tcp_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_tcp, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_tsm_stats(port_arg.pa_eth_port,
                                              &tpg_result.sr_tsm, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_msg_stats(&tpg_result.sr_msg, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

    RPC_STORE_RETCODE(tpg_result.sr_error,
                      test_mgmt_get_timer_stats(port_arg.pa_eth_port,
                                                &tpg_result.sr_timer, NULL));

    if (tpg_result.sr_error.e_code != 0)
        goto done;

done:
    RPC_REPLY(StatsResult, protoc_result, STATS_RESULT__INIT, tpg_result);
    RPC_CLEANUP(PortArg, port_arg, StatsResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__clear_statistics()
 ****************************************************************************/
static void tpg_rpc__clear_statistics(Warp17_Service *service __rte_unused,
                                      const PortArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_port_arg_t port_arg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    RPC_INIT_DEFAULT(Error, &tpg_result);
    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_clear_statistics(port_arg.pa_eth_port,
                                                 NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortArg, port_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_test_status()
 ****************************************************************************/
static void tpg_rpc__get_test_status(Warp17_Service *service __rte_unused,
                                     const TestCaseArg *input,
                                     TestStatusResult_Closure closure,
                                     void *closure_data)
{
    tpg_test_case_arg_t      test_case_arg;
    tpg_test_status_result_t tpg_result;
    TestStatusResult         protoc_result;
    tpg_test_case_t          tc_entry;
    test_env_oper_state_t    op_state;
    int                      err = 0;

    RPC_INIT_DEFAULT(TestStatusResult, &tpg_result);
    if (RPC_REQUEST_INIT(TestCaseArg, input, &test_case_arg))
        return;

    err = test_mgmt_get_test_case_cfg(test_case_arg.tca_eth_port,
                                      test_case_arg.tca_test_case_id,
                                      &tc_entry,
                                      NULL);
    if (err)
        goto done;

    err = test_mgmt_get_test_case_state(test_case_arg.tca_eth_port,
                                        test_case_arg.tca_test_case_id,
                                        &op_state,
                                        NULL);
    if (err)
        goto done;

    tpg_result.tsr_state = op_state.teos_test_case_state;
    tpg_result.tsr_type = tc_entry.tc_type;

    switch (tc_entry.tc_type) {
    case TEST_CASE_TYPE__SERVER:
        tpg_result.tsr_l4_proto = tc_entry.tc_server.srv_l4.l4s_proto;
        break;
    case TEST_CASE_TYPE__CLIENT:
        tpg_result.tsr_l4_proto = tc_entry.tc_client.cl_l4.l4c_proto;
        break;
    default:
        err = -EINVAL;
        goto done;
    }

    tpg_result.tsr_app_proto = tc_entry.tc_app.app_proto;

    err = test_mgmt_get_test_case_stats(test_case_arg.tca_eth_port,
                                        test_case_arg.tca_test_case_id,
                                        &tpg_result.tsr_stats,
                                        NULL);
    if (err)
        goto done;

    err = test_mgmt_get_test_case_rate_stats(test_case_arg.tca_eth_port,
                                             test_case_arg.tca_test_case_id,
                                             &tpg_result.tsr_rate_stats,
                                             NULL);
    if (err)
        goto done;

    err = test_mgmt_get_test_case_app_stats(test_case_arg.tca_eth_port,
                                            test_case_arg.tca_test_case_id,
                                            &tpg_result.tsr_app_stats,
                                            NULL);
    if (err)
        goto done;

done:
    RPC_STORE_RETCODE(tpg_result.tsr_error, err);
    RPC_REPLY(TestStatusResult, protoc_result, TEST_STATUS_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, test_case_arg, TestStatusResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_test_status()
 ****************************************************************************/
static void tpg_rpc__get_imix_statistics(Warp17_Service *service __rte_unused,
                                         const ImixArg *input,
                                         ImixStatsResult_Closure closure,
                                         void *closure_data)
{
    tpg_imix_arg_t          imix_arg;
    tpg_imix_stats_result_t tpg_result;
    ImixStatsResult         protoc_result;
    int                     err;

    RPC_INIT_DEFAULT(ImixStatsResult, &tpg_result);
    if (RPC_REQUEST_INIT(ImixArg, input, &imix_arg))
        return;

    err = test_mgmt_get_imix_stats(imix_arg.ia_imix_id, &tpg_result.isr_stats,
                                   NULL);

    RPC_STORE_RETCODE(tpg_result.isr_error, err);
    RPC_REPLY(ImixStatsResult, protoc_result, IMIX_STATS_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(ImixArg, imix_arg, ImixStatsResult, protoc_result);
}

