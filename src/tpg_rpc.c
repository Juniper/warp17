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
#include <google/protobuf-c/protobuf-c-rpc.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Local defines
 ****************************************************************************/
/* TPG RPC TCP port. */
#define TPG_RPC_TCP_PORT "42424"

/*****************************************************************************
 * Macro for translating active union fields.
 ****************************************************************************/
#define TPG_XLATE_UNION_SET_FIELD(out, in, field) \
    do {                                          \
        (out)->field = (in)->field;               \
        (out)->has_##field = true;                \
    } while (0)


/*****************************************************************************
 * Macro helpers for hiding the RPC internals
 ****************************************************************************/
#define RPC_REQUEST_INIT(in_type, in, out) \
    tpg_xlate_protoc_##in_type((in), (out))

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

static void tpg_rpc__update_test_case(Warp17_Service *service,
                                      const UpdateArg *input,
                                      Error_Closure closure,
                                      void *closure_data);

static void tpg_rpc__get_test_case_app_client(Warp17_Service *service,
                                              const TestCaseArg *input,
                                              TestCaseClientResult_Closure closure,
                                              void *closure_data);

static void tpg_rpc__get_test_case_app_server(Warp17_Service *service,
                                              const TestCaseArg *input,
                                              TestCaseServerResult_Closure closure,
                                              void *closure_data);

static void tpg_rpc__update_test_case_app_client(Warp17_Service *service,
                                                 const UpdClientArg *input,
                                                 Error_Closure closure,
                                                 void *closure_data);

static void tpg_rpc__update_test_case_app_server(Warp17_Service *service,
                                                 const UpdServerArg *input,
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

static void tpg_rpc__port_start(Warp17_Service *service,
                                const PortArg *input,
                                Error_Closure closure,
                                void *closure_data);

static void tpg_rpc__port_stop(Warp17_Service *service,
                               const PortArg *input,
                               Error_Closure closure,
                               void *closure_data);

static void tpg_rpc__get_test_status(Warp17_Service *service,
                                     const TestCaseArg *input,
                                     TestStatusResult_Closure closure,
                                     void *closure_data);

static void tpg_rpc__clear_statistics(Warp17_Service *service,
                                const PortArg *input,
                                Error_Closure closure,
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

    /* protobuf_c_dispatch_default is a singleton but let's make sure we
     * can initialize it!
     */
    if (!protobuf_c_dispatch_default())
        return false;

    rpc_server = protobuf_c_rpc_server_new(PROTOBUF_C_RPC_ADDRESS_TCP,
                                           TPG_RPC_TCP_PORT,
                                           (ProtobufCService *) &tpg_service,
                                           protobuf_c_dispatch_default());
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
    void empty(ProtobufCDispatch *d __rte_unused, void *arg __rte_unused) {}

    protobuf_c_dispatch_add_idle(protobuf_c_dispatch_default(),
                                 empty,
                                 NULL);
    protobuf_c_dispatch_run(protobuf_c_dispatch_default());
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
 * tpg_xlate_tpg_union_AppClient()
 ****************************************************************************/
int tpg_xlate_tpg_union_AppClient(const tpg_app_client_t *in, AppClient *out)
{
    switch (in->ac_app_proto) {
    case APP_PROTO__RAW:
        out->ac_raw = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->ac_raw), 0);
        if (!out->ac_raw)
            return -ENOMEM;

        tpg_xlate_tpg_RawClient(&in->ac_raw, out->ac_raw);
        break;
    case APP_PROTO__HTTP:
        out->ac_http = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->ac_http), 0);
        if (!out->ac_http)
            return -ENOMEM;

        tpg_xlate_tpg_HttpClient(&in->ac_http, out->ac_http);
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/*****************************************************************************
 * tpg_xlate_tpg_union_AppServer()
 ****************************************************************************/
int tpg_xlate_tpg_union_AppServer(const tpg_app_server_t *in, AppServer *out)
{
    switch (in->as_app_proto) {
    case APP_PROTO__RAW:
        out->as_raw = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_raw), 0);
        if (!out->as_raw)
            return -ENOMEM;

        tpg_xlate_tpg_RawServer(&in->as_raw, out->as_raw);
        break;
    case APP_PROTO__HTTP:
        out->as_http = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_http), 0);
        if (!out->as_http)
            return -ENOMEM;

        tpg_xlate_tpg_HttpServer(&in->as_http, out->as_http);
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

    out->tsr_link_stats = rte_zmalloc("TPG_RPC_GEN",
                                      sizeof(*out->tsr_link_stats),
                                      0);

    out->tsr_ip_stats = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->tsr_ip_stats),
                                    0);

    if (!out->tsr_stats || !out->tsr_rate_stats || !out->tsr_app_stats ||
        !out->tsr_link_stats || !out->tsr_ip_stats)
        return -ENOMEM;

    /* Translate TestCaseStats manually. */
    *out->tsr_stats = (TestCaseStats)TEST_CASE_STATS__INIT;

    out->tsr_stats->tcs_data_failed = in->tsr_stats.tcs_data_failed;
    out->tsr_stats->tcs_data_null = in->tsr_stats.tcs_data_null;

    out->tsr_stats->tcs_start_time = in->tsr_stats.tcs_start_time / cycles_per_us;
    out->tsr_stats->tcs_end_time = in->tsr_stats.tcs_end_time / cycles_per_us;

    switch (in->tsr_type) {
    case TEST_CASE_TYPE__SERVER:
        out->tsr_stats->tcs_server = rte_zmalloc("TPG_RPC_GEN",
                                                 sizeof(*out->tsr_stats->tcs_server),
                                                 0);
        if (!out->tsr_stats->tcs_server)
            return -ENOMEM;

        err = tpg_xlate_tpg_TestCaseServerStats(&in->tsr_stats.tcs_server,
                                                out->tsr_stats->tcs_server);
        if (err)
            return err;
    break;
    case TEST_CASE_TYPE__CLIENT:
        out->tsr_stats->tcs_client = rte_zmalloc("TPG_RPC_GEN",
                                                 sizeof(*out->tsr_stats->tcs_client),
                                                 0);
        if (!out->tsr_stats->tcs_client)
            return -ENOMEM;

        err = tpg_xlate_tpg_TestCaseClientStats(&in->tsr_stats.tcs_client,
                                                out->tsr_stats->tcs_client);
        if (err)
            return err;
    break;
    default:
        return -EINVAL;
    }

    /* Translate TestCaseRateStats. */
    err = tpg_xlate_tpg_TestCaseRateStats(&in->tsr_rate_stats,
                                          out->tsr_rate_stats);
    if (err)
        return err;

    /* Translate TestCaseAppStats manually. */
    *out->tsr_app_stats = (TestCaseAppStats)TEST_CASE_APP_STATS__INIT;
    switch (in->tsr_app_proto) {
    case APP_PROTO__RAW:
        out->tsr_app_stats->tcas_raw = rte_zmalloc("TPG_RPC_GEN",
                                                   sizeof(*out->tsr_app_stats->tcas_raw),
                                                   0);
        if (!out->tsr_app_stats->tcas_raw)
            return -ENOMEM;

        err = tpg_xlate_tpg_RawStats(&in->tsr_app_stats.tcas_raw,
                                     out->tsr_app_stats->tcas_raw);
        if (err)
            return err;
        break;
    case APP_PROTO__HTTP:
        out->tsr_app_stats->tcas_http = rte_zmalloc("TPG_RPC_GEN",
                                                    sizeof(*out->tsr_app_stats->tcas_http),
                                                    0);
        if (!out->tsr_app_stats->tcas_http)
            return -ENOMEM;

        err = tpg_xlate_tpg_HttpStats(&in->tsr_app_stats.tcas_http,
                                      out->tsr_app_stats->tcas_http);
        if (err)
            return err;
        break;
    default:
        return -EINVAL;
    }

    /* Translate LinkStats. */
    err = tpg_xlate_tpg_LinkStats(&in->tsr_link_stats, out->tsr_link_stats);
    if (err)
        return err;

    /* Translate IpStats. */
    err = tpg_xlate_tpg_IpStats(&in->tsr_ip_stats, out->tsr_ip_stats);
    if (err)
        return err;

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
 * tpg_rpc__get_test_case_app_client()
 ****************************************************************************/
static void tpg_rpc__get_test_case_app_client(Warp17_Service *service __rte_unused,
                                              const TestCaseArg *input,
                                              TestCaseClientResult_Closure closure,
                                              void *closure_data)
{
    tpg_test_case_arg_t           tc_arg;
    tpg_test_case_client_result_t tpg_result;
    TestCaseClientResult          protoc_result;
    int                           err;

    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_test_case_app_client_cfg(tc_arg.tca_eth_port,
                                                 tc_arg.tca_test_case_id,
                                                 &tpg_result.tccr_cl_app,
                                                 NULL);

    RPC_STORE_RETCODE(tpg_result.tccr_error, err);

    RPC_REPLY(TestCaseClientResult, protoc_result,
              TEST_CASE_CLIENT_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, TestCaseClientResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__get_test_case_app_server()
 ****************************************************************************/
static void tpg_rpc__get_test_case_app_server(Warp17_Service *service __rte_unused,
                                              const TestCaseArg *input,
                                              TestCaseServerResult_Closure closure,
                                              void *closure_data)
{
    tpg_test_case_arg_t           tc_arg;
    tpg_test_case_server_result_t tpg_result;
    TestCaseServerResult          protoc_result;
    int                           err;

    if (RPC_REQUEST_INIT(TestCaseArg, input, &tc_arg))
        return;

    err = test_mgmt_get_test_case_app_server_cfg(tc_arg.tca_eth_port,
                                                 tc_arg.tca_test_case_id,
                                                 &tpg_result.tcsr_srv_app,
                                                 NULL);

    RPC_STORE_RETCODE(tpg_result.tcsr_error, err);

    RPC_REPLY(TestCaseServerResult, protoc_result,
              TEST_CASE_SERVER_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, tc_arg, TestCaseServerResult, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__update_test_case_app_client()
 ****************************************************************************/
static void tpg_rpc__update_test_case_app_client(Warp17_Service *service __rte_unused,
                                                 const UpdClientArg *input,
                                                 Error_Closure closure,
                                                 void *closure_data)
{
    tpg_upd_client_arg_t client_arg;
    tpg_error_t          tpg_result;
    Error                protoc_result;
    int                  err;

    if (RPC_REQUEST_INIT(UpdClientArg, input, &client_arg))
        return;

    err = test_mgmt_update_test_case_app_client(client_arg.uca_tc_arg.tca_eth_port,
                                                client_arg.uca_tc_arg.tca_test_case_id,
                                                &client_arg.uca_cl_app,
                                                NULL);

    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(UpdClientArg, client_arg, Error, protoc_result);
}

/*****************************************************************************
 * tpg_rpc__update_test_case_app_server()
 ****************************************************************************/
static void tpg_rpc__update_test_case_app_server(Warp17_Service *service __rte_unused,
                                                 const UpdServerArg *input,
                                                 Error_Closure closure,
                                                 void *closure_data)
{
    tpg_upd_server_arg_t server_arg;
    tpg_error_t          tpg_result;
    Error                protoc_result;
    int                  err;

    if (RPC_REQUEST_INIT(UpdServerArg, input, &server_arg))
        return;

    err = test_mgmt_update_test_case_app_server(server_arg.usa_tc_arg.tca_eth_port,
                                                server_arg.usa_tc_arg.tca_test_case_id,
                                                &server_arg.usa_srv_app,
                                                NULL);

    RPC_STORE_RETCODE(tpg_result, err);

    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(UpdServerArg, server_arg, Error, protoc_result);
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

    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result, test_mgmt_stop_port(port_arg.pa_eth_port,
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
    struct rte_eth_link      link_info;
    struct rte_eth_stats     link_stats;
    struct rte_eth_stats     link_rate_stats;
    ipv4_statistics_t        ipv4_stats;
    int                      err = 0;

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
        tpg_result.tsr_app_proto = tc_entry.tc_server.srv_app.as_app_proto;
        break;
    case TEST_CASE_TYPE__CLIENT:
        tpg_result.tsr_l4_proto = tc_entry.tc_client.cl_l4.l4c_proto;
        tpg_result.tsr_app_proto = tc_entry.tc_client.cl_app.ac_app_proto;
        break;
    default:
        err = -EINVAL;
        goto done;
    }

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

    port_link_info_get(test_case_arg.tca_eth_port, &link_info);
    port_link_stats_get(test_case_arg.tca_eth_port, &link_stats);
    port_link_rate_stats_get(test_case_arg.tca_eth_port, &link_rate_stats);
    ipv4_total_stats_get(test_case_arg.tca_eth_port, &ipv4_stats);

    tpg_result.tsr_link_stats.ls_rx_pkts = link_stats.ipackets;
    tpg_result.tsr_link_stats.ls_rx_bytes = link_stats.ibytes;
    tpg_result.tsr_link_stats.ls_tx_pkts = link_stats.opackets;
    tpg_result.tsr_link_stats.ls_tx_bytes = link_stats.obytes;
    tpg_result.tsr_link_stats.ls_rx_errors = link_stats.ierrors;
    tpg_result.tsr_link_stats.ls_tx_errors = link_stats.oerrors;
    tpg_result.tsr_link_stats.ls_link_speed = link_info.link_speed;

    tpg_result.tsr_ip_stats.is_rx_pkts = ipv4_stats.ips_received_pkts;
    tpg_result.tsr_ip_stats.is_rx_bytes = ipv4_stats.ips_received_bytes;

done:
    RPC_STORE_RETCODE(tpg_result.tsr_error, err);
    RPC_REPLY(TestStatusResult, protoc_result, TEST_STATUS_RESULT__INIT,
              tpg_result);
    RPC_CLEANUP(TestCaseArg, test_case_arg, TestStatusResult, protoc_result);
}


static void tpg_rpc__clear_statistics(Warp17_Service *service __rte_unused,
                                      const PortArg *input,
                                      Error_Closure closure,
                                      void *closure_data)
{
    tpg_port_arg_t port_arg;
    tpg_error_t    tpg_result;
    Error          protoc_result;

    if (RPC_REQUEST_INIT(PortArg, input, &port_arg))
        return;

    RPC_STORE_RETCODE(tpg_result,
                      test_mgmt_clear_statistics(port_arg.pa_eth_port,
                                                 NULL));
    RPC_REPLY(Error, protoc_result, ERROR__INIT, tpg_result);
    RPC_CLEANUP(PortArg, port_arg, Error, protoc_result);
}
