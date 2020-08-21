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
 *     tpg_test_http_1_1_app.c
 *
 * Description:
 *     HTTP 1.1 application implementation.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     03/03/2016
 *
 * Notes:
 *     Only GET/HEAD and 200OK/404NOT-FOUND supported for now but adding the
 *     others should be straight forward.
 *     An important restriction is that we assume that the Content-Length
 *     field is always set in the header if the request/response has a body.
 *     This is not the case if the Transfer Encoding field is present.
 *     Therefore we count whenever Transfer Encoding is received and close the
 *     connection.
 *
 */

#include <ctype.h>

#include <rte_errno.h>

#include "tcp_generator.h"

/*****************************************************************************
 * Local Definitions.
 ****************************************************************************/

#define HTTP_TPG_USER_AGENT "WARP17 HTTP CL"

#define HTTP_DEFAULT_OBJECT_NAME    "/index.html"
#define HTTP_DEFAULT_HOST_NAME      "www.foo42.net"
#define HTTP_DEFAULT_USER_FIELDS    "ContentType: plain/text"
#define HTTP_CONTENT_LENGTH_INVALID UINT32_MAX

#define HTTP_USER_AGENT_STR     "User-Agent:"
#define HTTP_HOST_STR           "Host:"
#define HTTP_SERVER_STR         "Server:"
#define HTTP_CONTENT_LEN_STR    "Content-Length:"
#define HTTP_TRANSFER_ENC_STR   "Transfer-Encoding:"

#define HTTP_OBJECT_NAME(name) \
    ((name) ? (name) : HTTP_DEFAULT_OBJECT_NAME)

#define HTTP_HOST_NAME(name) \
    ((name) ? (name) : HTTP_DEFAULT_HOST_NAME)

#define HTTP_FIELDS(str) \
    ((str) ? (str) : HTTP_DEFAULT_USER_FIELDS)

static const char *http_method_names[HTTP_METHOD__HTTP_METHOD_MAX] = {
    [HTTP_METHOD__GET]     = "GET",
    [HTTP_METHOD__HEAD]    = "HEAD",
    [HTTP_METHOD__POST]    = "POST",
    [HTTP_METHOD__PUT]     = "PUT",
    [HTTP_METHOD__DELETE]  = "DELETE",
    [HTTP_METHOD__CONNECT] = "CONNECT",
    [HTTP_METHOD__OPTIONS] = "OPTIONS",
    [HTTP_METHOD__TRACE]   = "TRACE",
};

static const char *http_status_names[HTTP_STATUS_CODE__HTTP_STATUS_CODE_MAX] = {
    [HTTP_STATUS_CODE__OK_200]        = "200 OK",
    [HTTP_STATUS_CODE__NOT_FOUND_404] = "404 NOT FOUND",
    [HTTP_STATUS_CODE__FORBIDDEN_403] = "403 FORBIDDEN",
};

/*****************************************************************************
 * Forward references.
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * Globals.
 ****************************************************************************/
/* Define HTTP global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(http_statistics_t);

/*****************************************************************************
 * Static functions
 ****************************************************************************/

/*****************************************************************************
 * http_client_cfg_init()
 ****************************************************************************/
static int http_client_cfg_init(tpg_http_client_t *http_cfg,
                                tpg_http_method_t method,
                                const char *obj_name,
                                const char *host_name,
                                const char *fields,
                                uint32_t req_size)
{
    http_cfg->hc_req_method = method;

    if (obj_name) {
        http_cfg->hc_req_object_name = strdup(obj_name);
        if (!http_cfg->hc_req_object_name)
            return -ENOMEM;
    }

    if (host_name) {
        http_cfg->hc_req_host_name = strdup(host_name);
        if (!http_cfg->hc_req_host_name) {
            free(http_cfg->hc_req_object_name);
            return -ENOMEM;
        }
    }

    if (fields) {
        http_cfg->hc_req_fields = strdup(fields);
        if (!http_cfg->hc_req_fields) {
            free(http_cfg->hc_req_object_name);
            free(http_cfg->hc_req_host_name);
            return -ENOMEM;
        }
    }

    http_cfg->hc_req_size = req_size;
    return 0;
}

/*****************************************************************************
 * http_client_cfg_free()
 ****************************************************************************/
static void http_client_cfg_free(tpg_http_client_t *http_cfg)
{
    if (http_cfg->hc_req_object_name)
        free(http_cfg->hc_req_object_name);

    if (http_cfg->hc_req_host_name)
        free(http_cfg->hc_req_host_name);

    if (http_cfg->hc_req_fields)
        free(http_cfg->hc_req_fields);
}

/*****************************************************************************
 * http_server_cfg_init()
 ****************************************************************************/
static int http_server_cfg_init(tpg_http_server_t *http_cfg,
                                tpg_http_status_code_t status_code,
                                const char *fields,
                                uint32_t resp_size)
{
    http_cfg->hs_resp_code = status_code;

    if (fields) {
        http_cfg->hs_resp_fields = strdup(fields);
        if (!http_cfg->hs_resp_fields)
            return -ENOMEM;
    }

    http_cfg->hs_resp_size = resp_size;
    return 0;
}

/*****************************************************************************
 * http_server_cfg_free()
 ****************************************************************************/
static void http_server_cfg_free(tpg_http_server_t *http_cfg)
{
    if (http_cfg->hs_resp_fields)
        free(http_cfg->hs_resp_fields);
}

/*****************************************************************************
 * http_validate_fields()
 ****************************************************************************/
static bool http_validate_fields(printer_arg_t *printer_arg,
                                 const char *http_fields)
{
    /* Content-length cannot be specified explicitly by the user! */
    if (http_fields && strcasestr(http_fields, HTTP_CONTENT_LEN_STR)) {
        tpg_printf(printer_arg,
                   "ERROR: HTTP Content-Length must not be explicitly specified!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * http_write_str()
 *  NOTES:
 *      - if there's not enough room in the current mbuf, a new mbuf
 *        will be allocated and appended to the chain
 *      - WARNING: this function is slow and shouldn't be called in fast-path!
 ****************************************************************************/
static int http_write_str(struct rte_mbuf *mbuf, const char *fmt, ...)
{
    va_list ap;
    va_list temp_ap;
    int     datalen;
    int     err;

    if (!mbuf || !fmt)
        return -EINVAL;

    va_start(ap, fmt);
    va_copy(temp_ap, ap);

    /* Trick the library to compute the length (excluding the null byte
     * terminator, therefore the +1) for us..
     */
    datalen = vsnprintf(NULL, 0, fmt, temp_ap) + 1;

    va_end(temp_ap);
    if (datalen > 0) {
        char data[datalen];
        char *data_p = &data[0];

        vsnprintf(data, datalen, fmt, ap);
        va_end(ap);

        /* Skip the NULL terminator. */
        datalen--;

        while (datalen) {
            uint16_t            tailroom = rte_pktmbuf_tailroom(mbuf);
            char               *mbuf_data;
            struct rte_mbuf    *new_mbuf;

            if (datalen <= tailroom) {
                mbuf_data = rte_pktmbuf_append(mbuf, datalen);
                rte_memcpy(mbuf_data, data_p, datalen);
                return 0;
            } else {
                mbuf_data = rte_pktmbuf_append(mbuf, tailroom);
                rte_memcpy(mbuf_data, data_p, tailroom);
                data_p += tailroom;
                datalen -= tailroom;

                new_mbuf = pkt_mbuf_alloc(mem_get_mbuf_local_pool());
                if (!new_mbuf)
                    return -ENOMEM;
                err = rte_pktmbuf_chain(mbuf, new_mbuf);
                if (err)
                    return err;
            }
        }
    } else {
        va_end(ap);
        return -EINVAL;
    }

    return 0;
}

/*****************************************************************************
 * http_build_req_pkt()
 *  NOTES:
 *      - if mbuf is NULL a new mbuf will be allocated.
 *      - if there's not enough room in the current mbuf, a new mbuf
 *        will be allocated and appended to the chain
 *      - WARNING: this function is slow and shouldn't be called in fast-path!
 *      - Other required request fields should be added to this function.
 ****************************************************************************/
static int http_build_req_pkt(struct rte_mbuf **mbuf, struct rte_mempool *pool,
                              const tpg_http_client_t *client_cfg,
                              bool header_only)
{
    struct rte_mbuf *mbuf_p;
    struct rte_mbuf *data;
    int              err;

    if (!mbuf || !pool || !client_cfg)
        return -EINVAL;

    if (*mbuf == NULL) {
        *mbuf = pkt_mbuf_alloc(pool);
        if (*mbuf == NULL)
            return -ENOMEM;
    }

    mbuf_p = *mbuf;

    /* Write the obj-name: "METHOD OBJ-NAME HTTP/1.1" */
    err = http_write_str(mbuf_p, "%s %s HTTP/1.1\r\n",
                         http_method_names[client_cfg->hc_req_method],
                         HTTP_OBJECT_NAME(client_cfg->hc_req_object_name));
    if (err)
        goto error;

    /* Write the user-agent: "User-Agent: USER-AGENT" */
    err = http_write_str(mbuf_p, "%s %s\r\n", HTTP_USER_AGENT_STR,
                         HTTP_TPG_USER_AGENT);
    if (err)
        goto error;

    /* Write the host: "Host: HOST" */
    err = http_write_str(mbuf_p, "%s %s\r\n", HTTP_HOST_STR,
                         HTTP_HOST_NAME(client_cfg->hc_req_host_name));
    if (err)
        goto error;

    /* Write the additional custom headers supplied by the user. */
    if (HTTP_FIELDS(client_cfg->hc_req_fields)) {
        err = http_write_str(mbuf_p, "%s\r\n",
                             HTTP_FIELDS(client_cfg->hc_req_fields));
        if (err)
            goto error;
    }

    /* Write the content length: "Content-Length: CONTENT-LENGTH" */
    err = http_write_str(mbuf_p, "%s %"PRIu32"\r\n",
                         HTTP_CONTENT_LEN_STR,
                         client_cfg->hc_req_size);
    if (err)
        goto error;

    /* Write the end of the header: "\r\n" */
    err = http_write_str(mbuf_p, "\r\n");
    if (err)
        goto error;

    /* Simulate the body with random data. */
    if (client_cfg->hc_req_size) {
        if (!header_only) {
            data = data_alloc_chain(pool, client_cfg->hc_req_size);
            if (!data)
                goto error;

            rte_pktmbuf_lastseg(mbuf_p)->next = data;
            mbuf_p->nb_segs += data->nb_segs;
            mbuf_p->pkt_len += data->pkt_len;
        } else {
            mbuf_p->pkt_len += client_cfg->hc_req_size;
        }
    }

    return 0;

error:
    /* Frees all the segments! */
    pkt_mbuf_free(mbuf_p);
    return err;
}

/*****************************************************************************
 * http_build_resp_pkt()
 *  NOTES:
 *      - if mbuf is NULL a new mbuf will be allocated.
 *      - if there's not enough room in the current mbuf, a new mbuf
 *        will be allocated and appended to the chain
 *      - WARNING: this function is slow and shouldn't be called in fast-path!
 * TODO: check what other fields we need!
 ****************************************************************************/
static int http_build_resp_pkt(struct rte_mbuf **mbuf, struct rte_mempool *pool,
                               const tpg_http_server_t *server_cfg,
                               bool header_only)
{
    struct rte_mbuf *mbuf_p;
    struct rte_mbuf *data;
    int              err;

    if (!mbuf || !pool || !server_cfg)
        return -EINVAL;

    if (!*mbuf) {
        *mbuf = pkt_mbuf_alloc(pool);
        if (*mbuf == NULL)
            return -ENOMEM;
    }

    mbuf_p = *mbuf;

    /* Write the status: "HTTP/1.1 STATUS" */
    err = http_write_str(mbuf_p, "HTTP/1.1 %s\r\n",
                         http_status_names[server_cfg->hs_resp_code]);
    if (err)
        goto error;

    /* Write the server: "Server: SERVER" */
    err = http_write_str(mbuf_p, "%s %s\r\n", HTTP_SERVER_STR,
                         HTTP_TPG_USER_AGENT);
    if (err)
        goto error;

    /* Write the additional custom headers supplied by the user. */
    if (HTTP_FIELDS(server_cfg->hs_resp_fields)) {
        err = http_write_str(mbuf_p, "%s\r\n",
                             HTTP_FIELDS(server_cfg->hs_resp_fields));
        if (err)
            goto error;
    }

    /* Write the content length: "Content-Length: CONTENT-LENGTH" */
    err = http_write_str(mbuf_p, "%s %"PRIu32"\r\n",
                         HTTP_CONTENT_LEN_STR,
                         server_cfg->hs_resp_size);
    if (err)
        goto error;

    /* Write the end of the header: "\r\n" */
    err = http_write_str(mbuf_p, "\r\n");
    if (err)
        goto error;

    /* Simulate the body with random data. */
    if (server_cfg->hs_resp_size) {
        if (!header_only) {
            data = data_alloc_chain(pool, server_cfg->hs_resp_size);
            if (!data)
                goto error;

            rte_pktmbuf_lastseg(mbuf_p)->next = data;
            mbuf_p->nb_segs += data->nb_segs;
            mbuf_p->pkt_len += data->pkt_len;
        } else {
            mbuf_p->pkt_len += server_cfg->hs_resp_size;
        }
    }

    return 0;

error:
    /* Frees all the segments! */
    pkt_mbuf_free(mbuf_p);
    return err;
}

/*****************************************************************************
 * http_init_req_msg()
 ****************************************************************************/
static int http_init_req_msg(http_storage_t *http_storage,
                             const tpg_http_client_t *client_cfg)
{
    int err;

    assert(http_storage->http_mbuf == NULL);

    err = http_build_req_pkt(&http_storage->http_mbuf,
                             mem_get_mbuf_local_pool(),
                             client_cfg,
                             FALSE);

    if (err != 0)
        INC_STATS(STATS_LOCAL(http_statistics_t, 0), hts_req_err);

    return err;
}

/*****************************************************************************
 * http_free_req_msg()
 ****************************************************************************/
static void http_free_req_msg(http_storage_t *http_storage)
{
    pkt_mbuf_free(http_storage->http_mbuf);
    http_storage->http_mbuf = NULL;
}

/*****************************************************************************
 * http_init_resp_msg()
 ****************************************************************************/
static int http_init_resp_msg(http_storage_t *http_storage,
                              const tpg_http_server_t *server_cfg)
{
    int err;

    err = http_build_resp_pkt(&http_storage->http_mbuf,
                              mem_get_mbuf_local_pool(),
                              server_cfg,
                              FALSE);

    if (err != 0)
        INC_STATS(STATS_LOCAL(http_statistics_t, 0), hts_resp_err);

    return err;
}

/*****************************************************************************
 * http_free_resp_msg()
 ****************************************************************************/
static void http_free_resp_msg(http_storage_t *http_storage)
{
    pkt_mbuf_free(http_storage->http_mbuf);
    http_storage->http_mbuf = NULL;
}

/*****************************************************************************
 * http_client_goto_send_req()
 ****************************************************************************/
static void http_client_goto_send_req(l4_control_block_t *l4,
                                      http_app_t *http_client_app,
                                      http_storage_t *http_storage)
{
    struct rte_mbuf *request = http_storage->http_mbuf;

    http_client_app->ha_state = HTTPS_CL_SEND_REQ;

    /* Send pointer should point to the beginning of the request. */
    APP_SEND_PTR_INIT(&http_client_app->ha_send, request);
    http_client_app->ha_content_length =
        APP_SEND_PTR_LEN(&http_client_app->ha_send);

    /* Notify the stack that we need to send data. */
    TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
}

/*****************************************************************************
 * http_server_goto_send_resp()
 ****************************************************************************/
static void http_server_goto_send_resp(l4_control_block_t *l4,
                                       http_app_t *http_server_app,
                                       http_storage_t *http_storage)
{
    struct rte_mbuf *response = http_storage->http_mbuf;

    http_server_app->ha_state = HTTPS_SRV_SEND_RESP;

    /* Write pointer should point to the beginning of the response. */
    APP_SEND_PTR_INIT(&http_server_app->ha_send, response);
    http_server_app->ha_content_length =
        APP_SEND_PTR_LEN(&http_server_app->ha_send);

    /* Notify the stack that we need to send data. */
    TEST_NOTIF(TEST_NOTIF_APP_SEND_START, l4);
}

/*****************************************************************************
 * http_goto_recv_headers()
 ****************************************************************************/
static void http_goto_recv_headers(l4_control_block_t *l4 __rte_unused,
                                   http_app_t *http_app,
                                   http_state_t state)
{
    http_app->ha_state = state;
    /* We expect to be able to compute the content length from the headers. */
    http_app->ha_content_length = HTTP_CONTENT_LENGTH_INVALID;
}

/*****************************************************************************
 * http_goto_recv_body()
 ****************************************************************************/
static void http_goto_recv_body(l4_control_block_t *l4 __rte_unused,
                                http_app_t *http_app,
                                http_state_t state)
{
    http_app->ha_state = state;
}

/*****************************************************************************
 * http_consume_ws()
 ****************************************************************************/
static bool http_consume_ws(struct rte_mbuf **line, uint32_t *offset,
                            uint32_t *line_len)
{
    char     *part_data;
    uint32_t  part_len;
    uint32_t  i;

    APP_FOR_EACH_DATA_BUF_START(*line, *offset, *line, part_data, part_len) {
        for (i = 0; i < part_len && *line_len; i++, (*line_len)--, (*offset)++) {
            if (!isblank(part_data[i]))
                break;
        }
        if (i < part_len || *line_len == 0)
            break;
    } APP_FOR_EACH_DATA_BUF_END()

    return *line_len != 0;
}

/*****************************************************************************
 * http_parse_uint32()
 ****************************************************************************/
static bool http_parse_uint32(struct rte_mbuf **line, uint32_t *offset,
                              uint32_t *line_len, uint32_t *result)
{
    char     *part_data;
    uint32_t  part_len;
    uint32_t  i;

    *result = 0;

    APP_FOR_EACH_DATA_BUF_START(*line, *offset, *line, part_data, part_len) {
        for (i = 0; i < part_len && *line_len; i++, (*line_len)--, (*offset)++) {
            if (isdigit(part_data[i]))
                *result = *result * 10 + (part_data[i] - '0');
            else
                break;
        }
        if (i < part_len || *line_len == 0)
            break;
    } APP_FOR_EACH_DATA_BUF_END()

    return *line_len != 0;
}

/*****************************************************************************
 * http_consume_string()
 ****************************************************************************/
static bool http_consume_string(const char *val, uint32_t val_len,
                                struct rte_mbuf **line,
                                uint32_t *offset,
                                uint32_t *line_len)
{
    uint32_t  cmp_len;
    char     *part_data;
    uint32_t  part_len;

    APP_FOR_EACH_DATA_BUF_START(*line, *offset, *line, part_data, part_len) {

        cmp_len = TPG_MIN(val_len, TPG_MIN(part_len, *line_len));
        if (strncmp(val, part_data, cmp_len))
            return false;

        val += cmp_len;
        val_len -= cmp_len;
        *line_len -= cmp_len;
        *offset += cmp_len;

        if (!val_len || !*line_len)
            break;
    } APP_FOR_EACH_DATA_BUF_END()

    return val_len == 0;
}

/*****************************************************************************
 * http_parse_content_length()
 ****************************************************************************/
static bool http_parse_content_length(http_app_t *http_app, struct rte_mbuf *line,
                                      uint32_t offset,
                                      uint32_t len)
{
    uint32_t content_len;

    if (!http_consume_string(HTTP_CONTENT_LEN_STR,
                             sizeof(HTTP_CONTENT_LEN_STR) - 1,
                             &line,
                             &offset,
                             &len))
        return false;

    if (!http_consume_ws(&line, &offset, &len))
        return false;

    if (!http_parse_uint32(&line, &offset, &len, &content_len)) {
        return false;
    } else if (len == 2) {
        /* Only accept an uint followed by CRLF as valid content length. */
        http_app->ha_content_length = content_len;
    }

    return true;
}

/*****************************************************************************
 * http_parse_transfer_enc()
 ****************************************************************************/
static bool http_parse_transfer_enc(http_app_t *http_app __rte_unused,
                                    struct rte_mbuf *line,
                                    uint32_t offset,
                                    uint32_t len)
{
    if (!http_consume_string(HTTP_TRANSFER_ENC_STR,
                             sizeof(HTTP_TRANSFER_ENC_STR) - 1,
                             &line,
                             &offset,
                             &len))
        return false;

    if (!http_consume_ws(&line, &offset, &len))
        return false;

    return true;
}

/*****************************************************************************
 * http_parse_hdr_line()
 ****************************************************************************/
static void http_parse_hdr_line(http_app_t *http_app, tpg_http_stats_t *stats,
                                struct rte_mbuf *hdr_line,
                                uint32_t offset,
                                uint32_t len,
                                bool *invalid)
{
    /* will return the mbuf corresponding to the given offset and will store at
     * &offset the offset inside the returned mbuf corresponding to the given
     * input offset.
     */
    hdr_line = app_data_get_from_offset(hdr_line, offset, &offset);

    /* TODO: change the one by one compare with something smarter.. maybe a
     * bsearch?
     */
    if (http_parse_content_length(http_app, hdr_line, offset, len)) {
        TRACE_FMT(HTTP, DEBUG, "Parsed CONTENT-LENGTH: %u",
                  http_app->ha_content_length);
    } else if (http_parse_transfer_enc(http_app, hdr_line, offset, len)) {
        TRACE_FMT(HTTP, ERROR, "Parsed TRANSFER-ENCODING: %s",
                  "Not supported!");
        *invalid = true;
        INC_STATS(stats, hsts_transfer_enc_cnt);
    }
}

/*****************************************************************************
 * http_scan_line()
 *      Scans the line in http_app->ha_recv.
 *      Returns true if a complete line was scanned, false otherwise.
 *      Stores in *line_len the length of the line (including CRLF).
 ****************************************************************************/
static uint32_t http_scan_line(http_app_t *http_app, struct rte_mbuf *data,
                               uint32_t offset)
{
    app_recv_ptr_t  *recv_ptr = &http_app->ha_recv;
    struct rte_mbuf *mbuf;
    uint32_t         line_len;
    char             last_char = 0;
    char            *content;
    uint32_t         content_len;

    offset += APP_RECV_SCAN_OFFSET(recv_ptr);

    /* will return the mbuf corresponding to the given offset and will store at
     * &offset the offset inside the returned mbuf corresponding to the given
     * input offset.
     */
    data = app_data_get_from_offset(data, offset, &offset);

    line_len = APP_RECV_SCAN_OFFSET(recv_ptr);

    APP_FOR_EACH_DATA_BUF_START(data, offset, mbuf, content, content_len) {
        uint32_t i;

        for (i = 0; i < content_len; i++) {
            if (unlikely(content[i] == '\n' && last_char == '\r')) {
                APP_RECV_SCAN_OFFSET(recv_ptr) = 0;
                return line_len + i + 1;
            }
            last_char = content[i];
        }
        line_len += content_len;
    } APP_FOR_EACH_DATA_BUF_END()

    if (unlikely(last_char == '\r'))
        APP_RECV_SCAN_OFFSET(recv_ptr) = line_len - 1;
    else
        APP_RECV_SCAN_OFFSET(recv_ptr) = line_len;

    return 0;
}

/*****************************************************************************
 * http_parse_hdr()
 *      Returns true if the header was completely parsed.
 *      Updates *delivered with the number of bytes successfully parsed.
 ****************************************************************************/
static bool http_parse_hdr(http_app_t *http_app, tpg_http_stats_t *stats,
                           struct rte_mbuf *data,
                           uint32_t *delivered,
                           bool *invalid)
{
    uint32_t line_len;
    uint32_t offset = 0;

    while (!(*invalid) && offset < data->pkt_len &&
           (line_len = http_scan_line(http_app, data, offset))) {
        *delivered += line_len;

        /* If the line is only CRLF then we're done. */
        if (line_len == 2)
            return true;

        http_parse_hdr_line(http_app, stats, data, offset, line_len, invalid);
        offset += line_len;
    }

    return false;
}

/*****************************************************************************
 * http_consume_body()
 *      Returns true if the body was completely consumed.
 ****************************************************************************/
static bool http_consume_body(http_app_t *http_app __rte_unused,
                              struct rte_mbuf *rx_data,
                              uint32_t offset,
                              uint32_t *delivered)
{
    uint32_t consumed;

    /* Just consume everything so we are as fast as possible! */
    consumed = rx_data->pkt_len - offset;
    *delivered += consumed;

    /* In case of a malformed HTTP packet the real payload might be more than
     * the content length stored in the header. Just consume everything.
     */
    if (unlikely(consumed > http_app->ha_content_length))
        consumed = http_app->ha_content_length;

    http_app->ha_content_length -= consumed;
    return http_app->ha_content_length == 0;
}

/*****************************************************************************
 * Application callbacks
 ****************************************************************************/
/*****************************************************************************
 * http_client_default_cfg()
 ****************************************************************************/
void http_client_default_cfg(tpg_test_case_t *cfg)
{
    tpg_http_client_t *http_client = &cfg->tc_app.app_http_client;

    http_client->hc_req_method = HTTP_METHOD__GET;
    http_client->hc_req_object_name = NULL;
    http_client->hc_req_host_name = NULL;
    http_client->hc_req_size = 0;
    http_client->hc_req_fields = NULL;
}

/*****************************************************************************
 * http_server_default_cfg()
 ****************************************************************************/
void http_server_default_cfg(tpg_test_case_t *cfg)
{
    tpg_http_server_t *http_server = &cfg->tc_app.app_http_server;

    http_server->hs_resp_code = HTTP_STATUS_CODE__NOT_FOUND_404;
    http_server->hs_resp_size = 0;
    http_server->hs_resp_fields = NULL;
}

/*****************************************************************************
 * http_client_validate_cfg()
 ****************************************************************************/
bool http_client_validate_cfg(const tpg_test_case_t *cfg __rte_unused,
                              const tpg_app_t *app_cfg,
                              printer_arg_t *printer_arg)
{
    const tpg_http_client_t *http_client = &app_cfg->app_http_client;

    if (http_client->hc_req_method >= HTTP_METHOD__HTTP_METHOD_MAX) {
        tpg_printf(printer_arg, "ERROR: Invalid HTTP Request method!\n");
        return false;
    }

    /* Only GET and HEAD supported for now. */
    if (http_client->hc_req_method != HTTP_METHOD__GET &&
            http_client->hc_req_method != HTTP_METHOD__HEAD) {
        tpg_printf(printer_arg, "ERROR: Unsupported HTTP Request method!\n");
        return false;
    }

    if (http_client->hc_req_size >= HTTP_CONTENT_LENGTH_INVALID) {
        tpg_printf(printer_arg, "ERROR: Invalid HTTP Request size!\n");
        return false;
    }

    if (!http_validate_fields(printer_arg, http_client->hc_req_fields))
        return false;

    return true;
}

/*****************************************************************************
 * http_server_validate_cfg()
 ****************************************************************************/
bool http_server_validate_cfg(const tpg_test_case_t *cfg __rte_unused,
                              const tpg_app_t *app_cfg,
                              printer_arg_t *printer_arg)
{
    const tpg_http_server_t *http_server = &app_cfg->app_http_server;

    if (http_server->hs_resp_code >= HTTP_STATUS_CODE__HTTP_STATUS_CODE_MAX) {
        tpg_printf(printer_arg, "ERROR: Invalid HTTP Response type!\n");
        return false;
    }

    /* Only 200 OK and 404 NOT FOUND supported for now. */
    if (http_server->hs_resp_code != HTTP_STATUS_CODE__OK_200 &&
            http_server->hs_resp_code != HTTP_STATUS_CODE__NOT_FOUND_404) {
        tpg_printf(printer_arg, "ERROR: Unsupported HTTP Response type!\n");
        return false;
    }

    if (http_server->hs_resp_size >= HTTP_CONTENT_LENGTH_INVALID) {
        tpg_printf(printer_arg, "ERROR: Invalid HTTP Response size!\n");
        return false;
    }

    if (!http_validate_fields(printer_arg, http_server->hs_resp_fields))
        return false;

    return true;
}

/*****************************************************************************
 * http_client_print_cfg()
 ****************************************************************************/
void http_client_print_cfg(const tpg_app_t *app_cfg,
                           printer_arg_t *printer_arg)
{
    const tpg_http_client_t *http_client = &app_cfg->app_http_client;

    tpg_printf(printer_arg, "HTTP CLIENT:\n");
    tpg_printf(printer_arg, "%-20s: %s\n", "Request Method",
               http_method_names[http_client->hc_req_method]);
    tpg_printf(printer_arg, "%-20s: %s\n", "Request Object",
               HTTP_OBJECT_NAME(http_client->hc_req_object_name));
    tpg_printf(printer_arg, "%-20s: %s\n", "Request Host",
               HTTP_HOST_NAME(http_client->hc_req_host_name));
    tpg_printf(printer_arg, "%-20s: %"PRIu32"\n", "Request Size",
               http_client->hc_req_size);
    tpg_printf(printer_arg, "%-20s:\n%s\n", "Request HTTP Fields",
               HTTP_FIELDS(http_client->hc_req_fields));
}

/*****************************************************************************
 * http_server_print_cfg()
 ****************************************************************************/
void http_server_print_cfg(const tpg_app_t *app_cfg,
                           printer_arg_t *printer_arg)
{
    const tpg_http_server_t *http_server = &app_cfg->app_http_server;

    tpg_printf(printer_arg, "HTTP SERVER:\n");
    tpg_printf(printer_arg, "%-20s: %s\n", "Response Status",
               http_status_names[http_server->hs_resp_code]);
    tpg_printf(printer_arg, "%-20s: %"PRIu32"\n", "Response Size",
               http_server->hs_resp_size);
    tpg_printf(printer_arg, "%-20s:\n%s\n", "Response HTTP Fields",
               HTTP_FIELDS(http_server->hs_resp_fields));
}

/*****************************************************************************
 * http_client_add_cfg()
 ****************************************************************************/
void http_client_add_cfg(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg __rte_unused)
{
    /* Nothing to alloc here. Everything is preallocated before the config is
     * built.
     */
}

/*****************************************************************************
 * http_client_delete_cfg()
 ****************************************************************************/
void http_client_delete_cfg(const tpg_test_case_t *cfg __rte_unused,
                            const tpg_app_t *app_cfg)
{
    const tpg_http_client_t *http_client = &app_cfg->app_http_client;

    if (http_client->hc_req_object_name)
        free(http_client->hc_req_object_name);

    if (http_client->hc_req_host_name)
        free(http_client->hc_req_host_name);

    if (http_client->hc_req_fields)
        free(http_client->hc_req_fields);
}

/*****************************************************************************
 * http_server_add_cfg()
 ****************************************************************************/
void http_server_add_cfg(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg __rte_unused)
{
    /* Nothing to alloc here. Everything is preallocated before the config is
     * built.
     */
}

/*****************************************************************************
 * http_server_delete_cfg()
 ****************************************************************************/
void http_server_delete_cfg(const tpg_test_case_t *cfg __rte_unused,
                            const tpg_app_t *app_cfg)
{
    const tpg_http_server_t *http_server = &app_cfg->app_http_server;

    if (http_server->hs_resp_fields)
        free(http_server->hs_resp_fields);
}

/*****************************************************************************
 * http_client_pkts_per_send()
 ****************************************************************************/
uint32_t http_client_pkts_per_send(const tpg_test_case_t *cfg __rte_unused,
                                   const tpg_app_t *app_cfg,
                                   uint32_t max_pkt_size)
{
    struct rte_mbuf *cfg_mbuf = NULL;
    uint32_t         pkt_count;
    int              err;

    err = http_build_req_pkt(&cfg_mbuf, mem_get_mbuf_cfg_pool(),
                             &app_cfg->app_http_client,
                             TRUE);

    if (err != 0 || !cfg_mbuf)
        TPG_ERROR_ABORT("[%s()]: Failed to allocate config mbuf!\n", __func__);

    pkt_count = (cfg_mbuf->pkt_len + max_pkt_size - 1) / max_pkt_size;

    pkt_mbuf_free(cfg_mbuf);
    return pkt_count;
}

/*****************************************************************************
 * http_server_pkts_per_send()
 ****************************************************************************/
uint32_t http_server_pkts_per_send(const tpg_test_case_t *cfg __rte_unused,
                                   const tpg_app_t *app_cfg,
                                   uint32_t max_pkt_size)
{
    struct rte_mbuf *cfg_mbuf = NULL;
    uint32_t         pkt_count;
    int              err;

    err = http_build_resp_pkt(&cfg_mbuf, mem_get_mbuf_cfg_pool(),
                              &app_cfg->app_http_server,
                              TRUE);

    if (err != 0 || !cfg_mbuf)
        TPG_ERROR_ABORT("[%s()]: Failed to allocate config mbuf!\n", __func__);

    pkt_count = (cfg_mbuf->pkt_len + max_pkt_size - 1) / max_pkt_size;

    pkt_mbuf_free(cfg_mbuf);
    return pkt_count;
}

/*****************************************************************************
 * http_client_server_init()
 ****************************************************************************/
void http_client_server_init(app_data_t *app_data __rte_unused,
                             const tpg_app_t *app_cfg __rte_unused)
{
    /* We don't need to do anything on client/server init. Most of the
     * initialization will be done when the L4 connection goes to established
     * (in http_*conn_up).
     */
}

/*****************************************************************************
 * http_client_tc_start()
 ****************************************************************************/
void http_client_tc_start(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                          app_storage_t *app_storage)
{
    const tpg_http_client_t *client_cfg = &app_cfg->app_http_client;
    http_storage_t          *http_storage = &app_storage->ast_http;

    int err;

    err = http_init_req_msg(http_storage, client_cfg);

    if (err) {
        TPG_ERROR_ABORT("[%d:%s()] Failed to initialize HTTP request. "
                        "Error: %d(%s) Port: %"PRIu32 " TCID: %"PRIu32"\n",
                        rte_lcore_index(rte_lcore_id()),
                        __func__,
                        -err,
                        rte_strerror(-err),
                        cfg->tc_eth_port,
                        cfg->tc_id);
    }
}

/*****************************************************************************
 * http_server_tc_start()
 ****************************************************************************/
void http_server_tc_start(const tpg_test_case_t *cfg, const tpg_app_t *app_cfg,
                          app_storage_t *app_storage)
{
    const tpg_http_server_t *server_cfg = &app_cfg->app_http_server;
    http_storage_t          *http_storage = &app_storage->ast_http;

    int err;

    err = http_init_resp_msg(http_storage, server_cfg);

    if (err) {
        TPG_ERROR_ABORT("[%d:%s()] Failed to initialize HTTP responses. "
                        "Error: %d(%s) Port: %"PRIu32 " TCID: %"PRIu32"\n",
                        rte_lcore_index(rte_lcore_id()),
                        __func__,
                        -err,
                        rte_strerror(-err),
                        cfg->tc_eth_port,
                        cfg->tc_id);
    }
}

/*****************************************************************************
 * http_client_tc_stop()
 ****************************************************************************/
void http_client_tc_stop(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg __rte_unused,
                         app_storage_t *app_storage)
{
    http_storage_t *http_storage = &app_storage->ast_http;

    http_free_req_msg(http_storage);
}

/*****************************************************************************
 * http_server_tc_stop()
 ****************************************************************************/
void http_server_tc_stop(const tpg_test_case_t *cfg __rte_unused,
                         const tpg_app_t *app_cfg __rte_unused,
                         app_storage_t *app_storage)
{
    http_storage_t *http_storage = &app_storage->ast_http;

    http_free_resp_msg(http_storage);
}

/*****************************************************************************
 * http_client_conn_up()
 ****************************************************************************/
void http_client_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                         tpg_app_stats_t *stats __rte_unused)
{
    APP_RECV_PTR_INIT(&app_data->ad_http.ha_recv);
    http_client_goto_send_req(l4, &app_data->ad_http,
                              &app_data->ad_storage.ast_http);
}

/*****************************************************************************
 * http_server_conn_up()
 ****************************************************************************/
void http_server_conn_up(l4_control_block_t *l4, app_data_t *app_data,
                         tpg_app_stats_t *stats __rte_unused)
{
    APP_RECV_PTR_INIT(&app_data->ad_http.ha_recv);
    http_goto_recv_headers(l4, &app_data->ad_http, HTTPS_SRV_PARSE_REQ_HDR);
}

/*****************************************************************************
 * http_client_server_conn_down()
 ****************************************************************************/
void http_client_server_conn_down(l4_control_block_t *l4 __rte_unused,
                                  app_data_t *app_data __rte_unused,
                                  tpg_app_stats_t *stats __rte_unused)
{
    /* Normally we would need to free everything allocated when the connection
     * was initialized or went to established but we don't have anything in this
     * case.
     */

    if (unlikely(app_data->ad_http.ha_req_cnt == 0))
        INC_STATS(&stats->as_http, hsts_no_req);

    if (unlikely(app_data->ad_http.ha_resp_cnt == 0))
        INC_STATS(&stats->as_http, hsts_no_resp);

}

/*****************************************************************************
 * http_client_deliver_data()
 ****************************************************************************/
uint32_t http_client_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                  tpg_app_stats_t *stats,
                                  struct rte_mbuf *rx_data,
                                  uint64_t rx_tstamp __rte_unused)
{
    http_app_t       *http_client_data = &app_data->ad_http;
    http_storage_t   *http_storage = &app_data->ad_storage.ast_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    uint32_t          delivered = 0;
    bool              invalid = false;
    bool              header_done;

    switch (http_client_data->ha_state) {
    case HTTPS_CL_PARSE_RESP_HDR:
        header_done = http_parse_hdr(http_client_data, http_stats, rx_data,
                                     &delivered,
                                     &invalid);

        /* If the header is invalid close the underlying connection! */
        if (unlikely(invalid)) {
            /* Notify the stack that the connection should be closed. */
            TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
            INC_STATS(http_stats, hsts_invalid_msg_cnt);
            return 0;
        }

        /* If we didn't parse the whole header yet, announce how much we
         * consumed.
         */
        if (unlikely(!header_done))
            return delivered;

        /* If we're done with the headers check if we could compute the content
         * length! For now the content-length is a mandatory field for us!
         */
        if (unlikely(http_client_data->ha_content_length == HTTP_CONTENT_LENGTH_INVALID)) {
            /* Notify the stack that the connection should be closed. */
            TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
            INC_STATS(http_stats, hsts_no_content_len_cnt);
            return 0;
        }

        TRACE_FMT(HTTP, DEBUG, "CL HDR parsed. Content-Length: %"PRIu32,
                  http_client_data->ha_content_length);

        /* Goto state receiving body and consume it! */
        http_goto_recv_body(l4, http_client_data, HTTPS_CL_RECV_RESP_BODY);

        /* If we don't have any data left return. Careful with empty body
         * packets!
         */
        if (http_client_data->ha_content_length != 0 &&
                unlikely(delivered == rx_data->pkt_len))
            return delivered;

        /* Otherwise we have part of the body so consume it here:
         * FALLTHROUGH!!
         */
    case HTTPS_CL_RECV_RESP_BODY:
        /* delivered is a non-zero offset if we first processed the header
         * above!!
         */
        if (http_consume_body(http_client_data, rx_data, delivered,
                              &delivered)) {
            /* If we're done with the body then we need to send a new request. */
            INC_STATS(http_stats, hsts_resp_cnt);
            http_client_data->ha_resp_cnt++;

            TRACE_FMT(HTTP, DEBUG, "CL Body: %s", "Recvd");

            http_client_goto_send_req(l4, http_client_data, http_storage);
        }
        return delivered;
    default:
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        /* Notify the stack that the connection should be closed. */
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return 0;
    }
}

/*****************************************************************************
 * http_server_deliver_data()
 ****************************************************************************/
uint32_t http_server_deliver_data(l4_control_block_t *l4, app_data_t *app_data,
                                  tpg_app_stats_t *stats,
                                  struct rte_mbuf *rx_data,
                                  uint64_t rx_tstamp __rte_unused)
{
    http_app_t       *http_server_data = &app_data->ad_http;
    http_storage_t   *http_storage = &app_data->ad_storage.ast_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    uint32_t          delivered = 0;
    bool              invalid = false;
    bool              header_done; /* We reach the end of the header */

    switch (http_server_data->ha_state) {
    case HTTPS_SRV_PARSE_REQ_HDR:

        header_done = http_parse_hdr(http_server_data, http_stats, rx_data,
                                     &delivered,
                                     &invalid);

        /* If the header is invalid close the underlying connection! */
        if (unlikely(invalid)) {
            /* Notify the stack that the connection should be closed. */
            TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
            INC_STATS(http_stats, hsts_invalid_msg_cnt);
            return 0;
        }

        /* If we didn't parse the whole header yet, announce how much we
         * consumed.
         */
        if (unlikely(!header_done))
            return delivered;

        /* If we're done with the headers check if we could compute the content
         * length! For now the content-length is a mandatory field for us!
         */
        if (unlikely(http_server_data->ha_content_length == HTTP_CONTENT_LENGTH_INVALID)) {
            INC_STATS(http_stats, hsts_no_content_len_cnt);
            /* WARNING! Assuming no content-length implies length 0! */
            http_server_data->ha_content_length = 0;
            http_server_goto_send_resp(l4, http_server_data, http_storage);
            return delivered;
        }

        TRACE_FMT(HTTP, DEBUG, "SRV HDR parsed. Content-Length: %"PRIu32,
                  http_server_data->ha_content_length);

        /* Goto state receiving body and consume it! */
        http_goto_recv_body(l4, http_server_data, HTTPS_SRV_RECV_REQ_BODY);

        /* If we don't have any data left return. Careful with empty body
         * packets!
         */
        if (http_server_data->ha_content_length != 0 &&
                unlikely(delivered == rx_data->pkt_len))
            return delivered;

        /* Otherwise we have part of the body so consume it here:
         * FALLTHROUGH!!
         */
    case HTTPS_SRV_RECV_REQ_BODY:
        /* delivered is a non-zero offset if we first processed the header
         * above!!
         */
        if (http_consume_body(http_server_data, rx_data, delivered,
                              &delivered)) {
            /* If we're done with the body then we need to send a response. */
            INC_STATS(http_stats, hsts_req_cnt);
            http_server_data->ha_req_cnt++;

            TRACE_FMT(HTTP, DEBUG, "SRV Body: %s", "Recvd");

            http_server_goto_send_resp(l4, http_server_data, http_storage);
        }
        return delivered;
    default:
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        /* Notify the stack that the connection should be closed. */
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return 0;
    }
}

/*****************************************************************************
 * http_client_send_data()
 ****************************************************************************/
struct rte_mbuf *http_client_send_data(l4_control_block_t *l4 __rte_unused,
                                       app_data_t *app_data,
                                       tpg_app_stats_t *stats,
                                       uint32_t max_tx_size)
{
    http_app_t       *http_client_data = &app_data->ad_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    app_send_ptr_t   *send_ptr = &http_client_data->ha_send;

    switch (http_client_data->ha_state) {
    case HTTPS_CL_SEND_REQ:
        max_tx_size = TPG_MIN(APP_SEND_PTR_LEN(send_ptr), max_tx_size);

        return data_chain_from_static_chain(APP_SEND_PTR(send_ptr),
                                            APP_SEND_PTR_OFFSET(send_ptr),
                                            max_tx_size);
    default:
        /* Notify the stack that the connection should be closed. */
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return NULL;
    }
}

/*****************************************************************************
 * http_server_send_data()
 ****************************************************************************/
struct rte_mbuf *http_server_send_data(l4_control_block_t *l4 __rte_unused,
                                       app_data_t *app_data,
                                       tpg_app_stats_t *stats,
                                       uint32_t max_tx_size)
{
    http_app_t       *http_server_data = &app_data->ad_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    app_send_ptr_t   *send_ptr = &http_server_data->ha_send;

    switch (http_server_data->ha_state) {
    case HTTPS_SRV_SEND_RESP:
        max_tx_size = TPG_MIN(APP_SEND_PTR_LEN(send_ptr), max_tx_size);

        return data_chain_from_static_chain(APP_SEND_PTR(send_ptr),
                                            APP_SEND_PTR_OFFSET(send_ptr),
                                            max_tx_size);
    default:
        /* Notify the stack that the connection should be closed. */
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return NULL;
    }
}

/*****************************************************************************
 * http_client_data_sent()
 ****************************************************************************/
bool http_client_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                           tpg_app_stats_t *stats,
                           uint32_t bytes_sent)
{
    http_app_t       *http_client_data = &app_data->ad_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    app_send_ptr_t   *send_ptr = &http_client_data->ha_send;

    switch (http_client_data->ha_state) {
    case HTTPS_CL_SEND_REQ:
        app_send_ptr_advance(send_ptr, bytes_sent);
        http_client_data->ha_content_length -= bytes_sent;

        /* If done sending the request we can wait for a the response. */
        if (http_client_data->ha_content_length == 0) {
            INC_STATS(http_stats, hsts_req_cnt);
            http_client_data->ha_req_cnt++;

            TRACE_FMT(HTTP, DEBUG, "Client Request: %s", "Sent");

            http_goto_recv_headers(l4, http_client_data,
                                   HTTPS_CL_PARSE_RESP_HDR);

            /* Notify the stack that we're done sending data. */
            TEST_NOTIF(TEST_NOTIF_APP_SEND_STOP, l4);
            return true;
        }
        break;
    default:
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        /* Notify the stack that the connection should be closed. */
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return true;
    }

    return false;
}
/*****************************************************************************
 * http_server_data_sent()
 ****************************************************************************/
bool http_server_data_sent(l4_control_block_t *l4, app_data_t *app_data,
                           tpg_app_stats_t *stats,
                           uint32_t bytes_sent)
{
    http_app_t       *http_server_data = &app_data->ad_http;
    tpg_http_stats_t *http_stats = &stats->as_http;
    app_send_ptr_t   *send_ptr = &http_server_data->ha_send;

    switch (http_server_data->ha_state) {
    case HTTPS_SRV_SEND_RESP:
        app_send_ptr_advance(send_ptr, bytes_sent);
        http_server_data->ha_content_length -= bytes_sent;

        /* If done sending the response we can wait for a new request. */
        if (http_server_data->ha_content_length == 0) {
            INC_STATS(http_stats, hsts_resp_cnt);
            http_server_data->ha_resp_cnt++;

            TRACE_FMT(HTTP, DEBUG, "Server Response: %s", "Sent");

            http_goto_recv_headers(l4, http_server_data,
                                   HTTPS_SRV_PARSE_REQ_HDR);
            /* Notify the stack that we're done sending data. */
            TEST_NOTIF(TEST_NOTIF_APP_SEND_STOP, l4);
            return true;
        }
        break;
    default:
        /* Notify the stack that the connection should be closed. */
        INC_STATS(http_stats, hsts_invalid_msg_cnt);
        TEST_NOTIF(TEST_NOTIF_APP_CLOSE, l4);
        return true;
    }

    return false;
}

/*****************************************************************************
 * http_stats_init()
 ****************************************************************************/
void http_stats_init(const tpg_app_t *app_cfg __rte_unused,
                     tpg_app_stats_t *stats)
{
    bzero(stats, sizeof(*stats));
}

/*****************************************************************************
 * http_stats_copy()
 ****************************************************************************/
void http_stats_copy(tpg_app_stats_t *dest, const tpg_app_stats_t *src)
{
    *dest = *src;
}


/*****************************************************************************
 * http_stats_add()
 ****************************************************************************/
void http_stats_add(tpg_app_stats_t *total, const tpg_app_stats_t *elem)
{
    tpg_http_stats_t       *htotal = &total->as_http;
    const tpg_http_stats_t *helem = &elem->as_http;

    htotal->hsts_req_cnt += helem->hsts_req_cnt;
    htotal->hsts_resp_cnt += helem->hsts_resp_cnt;
    htotal->hsts_invalid_msg_cnt += helem->hsts_invalid_msg_cnt;
    htotal->hsts_no_content_len_cnt += helem->hsts_no_content_len_cnt;
    htotal->hsts_transfer_enc_cnt += helem->hsts_transfer_enc_cnt;
    htotal->hsts_no_req += helem->hsts_no_req;
    htotal->hsts_no_resp += helem->hsts_no_resp;
}

/*****************************************************************************
 * http_stats_print()
 ****************************************************************************/
void http_stats_print(const tpg_app_stats_t *stats, printer_arg_t *printer_arg)
{
    tpg_printf(printer_arg, "%13s %13s %13s %13s %13s\n", "Requests",
               "Responses",
               "Invalid Msg",
               "No Len",
               "Trans-Enc");
    tpg_printf(printer_arg,
               "%13"PRIu64" %13"PRIu64" %13"PRIu32" %13"PRIu32" %13"PRIu32"\n",
               stats->as_http.hsts_req_cnt,
               stats->as_http.hsts_resp_cnt,
               stats->as_http.hsts_invalid_msg_cnt,
               stats->as_http.hsts_no_content_len_cnt,
               stats->as_http.hsts_transfer_enc_cnt);

    tpg_printf(printer_arg, "%27s %27s\n",
               "Closed (No Request)",
               "Closed (No Response)");

    tpg_printf(printer_arg, "%27"PRIu32" %27"PRIu32"\n",
           stats->as_http.hsts_no_req,
           stats->as_http.hsts_no_resp);
}

/*****************************************************************************
 * http_init()
 ****************************************************************************/
bool http_init(void)
{
    /*
     * Add HTTP module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add HTTP specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for HTTP statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(http_statistics_t, "http_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating HTTP statistics memory!\n");
        return false;
    }

    return true;
}

/*****************************************************************************
 * http_lcore_init()
 ****************************************************************************/
void http_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(http_statistics_t, "http_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore http_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
    }
}

/*****************************************************************************
 * CLI
 ****************************************************************************/
/****************************************************************************
 * - "set tests client http port <port> test-case-id <tcid>
 *      GET|HEAD <host-name> <obj-name> req-size <req-size>"
 ****************************************************************************/
 struct cmd_tests_set_app_http_client_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t client;
    cmdline_fixed_string_t http;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t method;
    cmdline_fixed_string_t obj_name;
    cmdline_fixed_string_t host_name;
    cmdline_fixed_string_t content_kw;
    cmdline_fixed_string_t content;

    cmdline_fixed_string_t req_size_kw;
    uint32_t               req_size;
};

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_client =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, client,
                             TEST_CASE_CLIENT_CLI_STR);
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_http =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, http, "http");

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_method =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, method, "GET#HEAD");

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_host_name =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, host_name, NULL);
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_obj_name =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, obj_name, NULL);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_T_req_size_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_result, req_size_kw, "req-size");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_T_req_size =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_result, req_size, UINT32);

static void cmd_tests_set_app_http_client_parsed(void *parsed_result, struct cmdline *cl,
                                                 void *data __rte_unused)
{
    printer_arg_t     parg;
    tpg_app_t         app_cfg;
    tpg_http_method_t http_method;
    int               err;
    http_flags_type_t flags;

    struct cmd_tests_set_app_http_client_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (http_flags_type_t)(uintptr_t)data;

    bzero(&app_cfg, sizeof(app_cfg));

    app_cfg.app_proto = APP_PROTO__HTTP_CLIENT;

    if (strncmp(pr->method, "GET", strlen("GET") + 1) == 0)
        http_method = HTTP_METHOD__GET;
    else if (strncmp(pr->method, "HEAD", strlen("HEAD") + 1) == 0)
        http_method = HTTP_METHOD__HEAD;
    else
        assert(false);

    /* The config memory will be freed once the test case is deleted/updated. */
    if (http_client_cfg_init(&app_cfg.app_http_client, http_method,
                             pr->obj_name, pr->host_name, NULL,
                             pr->req_size)) {
        cmdline_printf(cl,
                       "ERROR: Failed allocating config for test case %"PRIu32
                       " on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
        return;
    }

    if (!HTTP_IMIX_ISSET(flags)) {
        err =  test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }

    if (err != 0) {
        /* If an error happened, free the config memory here. */
        http_client_cfg_free(&app_cfg.app_http_client);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_http_client = {
    .f = cmd_tests_set_app_http_client_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_NONE,
    .help_str = "set tests client http port <port> test-case-id <tcid> "
                "GET|HEAD <host-name> <obj-name> req-size <req-size>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_client_T_set,
        (void *)&cmd_tests_set_app_http_client_T_tests,
        (void *)&cmd_tests_set_app_http_client_T_client,
        (void *)&cmd_tests_set_app_http_client_T_http,
        (void *)&cmd_tests_set_app_http_client_T_port_kw,
        (void *)&cmd_tests_set_app_http_client_T_port,
        (void *)&cmd_tests_set_app_http_client_T_tcid_kw,
        (void *)&cmd_tests_set_app_http_client_T_tcid,
        (void *)&cmd_tests_set_app_http_client_T_method,
        (void *)&cmd_tests_set_app_http_client_T_host_name,
        (void *)&cmd_tests_set_app_http_client_T_obj_name,
        (void *)&cmd_tests_set_app_http_client_T_req_size_kw,
        (void *)&cmd_tests_set_app_http_client_T_req_size,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_http_client_imix = {
    .f = cmd_tests_set_app_http_client_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> client http "
                "GET|HEAD <host-name> <obj-name> req-size <req-size>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_client_T_set,
        (void *)&cmd_tests_set_app_http_client_T_tests,
        (void *)&cmd_tests_set_app_http_client_T_imix,
        (void *)&cmd_tests_set_app_http_client_T_app_index_kw,
        (void *)&cmd_tests_set_app_http_client_T_app_index,
        (void *)&cmd_tests_set_app_http_client_T_client,
        (void *)&cmd_tests_set_app_http_client_T_http,
        (void *)&cmd_tests_set_app_http_client_T_method,
        (void *)&cmd_tests_set_app_http_client_T_host_name,
        (void *)&cmd_tests_set_app_http_client_T_obj_name,
        (void *)&cmd_tests_set_app_http_client_T_req_size_kw,
        (void *)&cmd_tests_set_app_http_client_T_req_size,
        NULL,
    },
};

/****************************************************************************
 * - "set tests client http port <port> test-case-id <tcid>
 *      http-field <http-field>"
 ****************************************************************************/
 struct cmd_tests_set_app_http_client_field_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t client;
    cmdline_fixed_string_t http;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t http_field_kw;
    cmdline_fixed_string_t http_field;
};

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_client =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, client,
                             TEST_CASE_CLIENT_CLI_STR);
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_http =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, http, "http");

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_field_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_field_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_http_client_field_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_field_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, http_field_kw, "http-field");
static cmdline_parse_token_string_t cmd_tests_set_app_http_client_field_T_field =
    TOKEN_QUOTED_STRING_INITIALIZER(struct cmd_tests_set_app_http_client_field_result, http_field);

static void cmd_tests_set_app_http_client_field_parsed(void *parsed_result, struct cmdline *cl,
                                                       void *data __rte_unused)
{
    printer_arg_t      parg;
    tpg_app_t          app_cfg;
    tpg_http_client_t *http_cfg;
    char              *http_fields = NULL;
    int                err;
    http_flags_type_t  flags;

    struct cmd_tests_set_app_http_client_field_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (http_flags_type_t)(uintptr_t)data;

    if (!HTTP_IMIX_ISSET(flags)) {
        err = test_mgmt_get_test_case_app_cfg(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err) {
            cmdline_printf(cl, "ERROR: Failed fetching test case %"PRIu32
                        " config on port %"PRIu32"\n",
                        pr->tcid,
                        pr->port);
            return;
        }
    } else {
        err = imix_cli_get_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err) {
            cmdline_printf(cl,
                           "ERROR: Failed fetching IMIX app-index %"PRIu32" app config!\n",
                           pr->app_index);
            return;
        }
    }

    if (app_cfg.app_proto != APP_PROTO__HTTP_CLIENT) {
        cmdline_printf(cl,
                       "ERROR: Trying to update NON HTTP-CLIENT test case!\n");
        return;
    }

    http_cfg = &app_cfg.app_http_client;

    /* Append new field to old ones (if any). */
    if (http_cfg->hc_req_fields) {
        if (asprintf(&http_fields, "%s\r\n%s", http_cfg->hc_req_fields,
                     pr->http_field) == -1)
            http_fields = NULL;
    } else {
        http_fields = strdup(pr->http_field);
    }

    if (!http_fields) {
        cmdline_printf(cl, "ERROR: Failed allocating http fields!\n");
        return;
    }

    if (http_client_cfg_init(http_cfg, http_cfg->hc_req_method,
                             http_cfg->hc_req_object_name,
                             http_cfg->hc_req_host_name,
                             http_fields,
                             http_cfg->hc_req_size)) {
        cmdline_printf(cl, "ERROR: Failed allocating http client config!\n");

        /* Free the memory allocated by asprintf. */
        free(http_fields);
        return;
    }

    /* Free the memory allocated by asprintf. */
    free(http_fields);

    if (!HTTP_IMIX_ISSET(flags)) {
        err =  test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }

    if (err != 0) {
        /* If an error happened, free the config memory here. */
        http_client_cfg_free(&app_cfg.app_http_client);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_http_field_client = {
    .f = cmd_tests_set_app_http_client_field_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_NONE,
    .help_str = "set tests client http port <port> test-case-id <tcid> "
                "http-field <http-field>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_client_field_T_set,
        (void *)&cmd_tests_set_app_http_client_field_T_tests,
        (void *)&cmd_tests_set_app_http_client_field_T_client,
        (void *)&cmd_tests_set_app_http_client_field_T_http,
        (void *)&cmd_tests_set_app_http_client_field_T_port_kw,
        (void *)&cmd_tests_set_app_http_client_field_T_port,
        (void *)&cmd_tests_set_app_http_client_field_T_tcid_kw,
        (void *)&cmd_tests_set_app_http_client_field_T_tcid,
        (void *)&cmd_tests_set_app_http_client_field_T_field_kw,
        (void *)&cmd_tests_set_app_http_client_field_T_field,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_http_field_client_imix = {
    .f = cmd_tests_set_app_http_client_field_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> client http "
                "http-field <http-field>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_client_field_T_set,
        (void *)&cmd_tests_set_app_http_client_field_T_tests,
        (void *)&cmd_tests_set_app_http_client_field_T_imix,
        (void *)&cmd_tests_set_app_http_client_field_T_app_index_kw,
        (void *)&cmd_tests_set_app_http_client_field_T_app_index,
        (void *)&cmd_tests_set_app_http_client_field_T_client,
        (void *)&cmd_tests_set_app_http_client_field_T_http,
        (void *)&cmd_tests_set_app_http_client_field_T_field_kw,
        (void *)&cmd_tests_set_app_http_client_field_T_field,
        NULL,
    },
};

/****************************************************************************
 * - "set tests server http port <port> test-case-id <tcid>
 *      GET|HEAD <obj-name> req-size <req-size>"
 ****************************************************************************/
 struct cmd_tests_set_app_http_server_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t server;
    cmdline_fixed_string_t http;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t resp_code;

    cmdline_fixed_string_t resp_size_kw;
    uint32_t               resp_size;
};

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_server =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, server,
                             TEST_CASE_SERVER_CLI_STR);
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_http =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, http, "http");

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_resp_code =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, resp_code, "200-OK#404-NOT-FOUND");


static cmdline_parse_token_string_t cmd_tests_set_app_http_server_T_resp_size_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_result, resp_size_kw, "resp-size");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_T_resp_size =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_result, resp_size, UINT32);

static void cmd_tests_set_app_http_server_parsed(void *parsed_result, struct cmdline *cl,
                                                 void *data __rte_unused)
{
    printer_arg_t          parg;
    tpg_app_t              app_cfg;
    tpg_http_status_code_t http_status_code;
    int                    err;
    http_flags_type_t      flags;

    struct cmd_tests_set_app_http_server_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (http_flags_type_t)(uintptr_t)data;

    bzero(&app_cfg, sizeof(app_cfg));

    app_cfg.app_proto = APP_PROTO__HTTP_SERVER;

    if (strncmp(pr->resp_code, "200-OK", strlen("200-OK") + 1) == 0)
        http_status_code = HTTP_STATUS_CODE__OK_200;
    else if (strncmp(pr->resp_code, "404-NOT-FOUND",
                     strlen("404-NOT-FOUND") + 1) == 0)
        http_status_code = HTTP_STATUS_CODE__NOT_FOUND_404;
    else
        assert(false);

    /* The config memory will be freed once the test case is deleted/updated. */
    if (http_server_cfg_init(&app_cfg.app_http_server, http_status_code, NULL,
                             pr->resp_size)) {
        cmdline_printf(cl,
                       "ERROR: Failed allocating config for test case %"PRIu32
                       " on port %"PRIu32"\n",
                       pr->tcid,
                       pr->port);
        return;
    }

    if (!HTTP_IMIX_ISSET(flags)) {
        err =  test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }

    if (err != 0) {
        /* If an error happened, free the config memory here. */
        http_server_cfg_free(&app_cfg.app_http_server);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_http_server = {
    .f = cmd_tests_set_app_http_server_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_NONE,
    .help_str = "set tests server http port <port> test-case-id <tcid> "
                "200-OK|404-NOT-FOUND resp-size <resp-size>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_server_T_set,
        (void *)&cmd_tests_set_app_http_server_T_tests,
        (void *)&cmd_tests_set_app_http_server_T_server,
        (void *)&cmd_tests_set_app_http_server_T_http,
        (void *)&cmd_tests_set_app_http_server_T_port_kw,
        (void *)&cmd_tests_set_app_http_server_T_port,
        (void *)&cmd_tests_set_app_http_server_T_tcid_kw,
        (void *)&cmd_tests_set_app_http_server_T_tcid,
        (void *)&cmd_tests_set_app_http_server_T_resp_code,
        (void *)&cmd_tests_set_app_http_server_T_resp_size_kw,
        (void *)&cmd_tests_set_app_http_server_T_resp_size,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_http_server_imix = {
    .f = cmd_tests_set_app_http_server_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> server http "
                "200-OK|404-NOT-FOUND resp-size <resp-size>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_server_T_set,
        (void *)&cmd_tests_set_app_http_server_T_tests,
        (void *)&cmd_tests_set_app_http_server_T_imix,
        (void *)&cmd_tests_set_app_http_server_T_app_index_kw,
        (void *)&cmd_tests_set_app_http_server_T_app_index,
        (void *)&cmd_tests_set_app_http_server_T_server,
        (void *)&cmd_tests_set_app_http_server_T_http,
        (void *)&cmd_tests_set_app_http_server_T_resp_code,
        (void *)&cmd_tests_set_app_http_server_T_resp_size_kw,
        (void *)&cmd_tests_set_app_http_server_T_resp_size,
        NULL,
    },
};

/****************************************************************************
 * - "set tests server http port <port> test-case-id <tcid>
 *      http-field <http-field>"
 ****************************************************************************/
 struct cmd_tests_set_app_http_server_field_result {
    cmdline_fixed_string_t set;
    cmdline_fixed_string_t tests;
    cmdline_fixed_string_t imix;
    cmdline_fixed_string_t server;
    cmdline_fixed_string_t http;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
    cmdline_fixed_string_t tcid_kw;
    uint32_t               tcid;
    cmdline_fixed_string_t app_index_kw;
    uint32_t               app_index;

    cmdline_fixed_string_t http_field_kw;
    cmdline_fixed_string_t http_field;
};

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_set =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, set, "set");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_tests =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, tests, "tests");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_imix =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, imix, "imix");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_server =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, server,
                             TEST_CASE_SERVER_CLI_STR);
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_http =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, http, "http");

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_port_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_field_T_port =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, port, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_tcid_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, tcid_kw, "test-case-id");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_field_T_tcid =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, tcid, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_app_index_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, app_index_kw, "app-index");
static cmdline_parse_token_num_t cmd_tests_set_app_http_server_field_T_app_index =
    TOKEN_NUM_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, app_index, UINT32);

static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_field_kw =
    TOKEN_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, http_field_kw, "http-field");
static cmdline_parse_token_string_t cmd_tests_set_app_http_server_field_T_field =
    TOKEN_QUOTED_STRING_INITIALIZER(struct cmd_tests_set_app_http_server_field_result, http_field);

static void cmd_tests_set_app_http_server_field_parsed(void *parsed_result, struct cmdline *cl,
                                                       void *data __rte_unused)
{
    printer_arg_t      parg;
    tpg_app_t          app_cfg;
    tpg_http_server_t *http_cfg;
    char              *http_fields = NULL;
    int                err;
    http_flags_type_t  flags;

    struct cmd_tests_set_app_http_server_field_result *pr;

    parg = TPG_PRINTER_ARG(cli_printer, cl);
    pr = parsed_result;
    flags = (http_flags_type_t)(uintptr_t)data;

    if (!HTTP_IMIX_ISSET(flags)) {
        err = test_mgmt_get_test_case_app_cfg(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err) {
            cmdline_printf(cl, "ERROR: Failed fetching test case %"PRIu32
                        " config on port %"PRIu32"\n",
                        pr->tcid,
                        pr->port);
            return;
        }
    } else {
        err = imix_cli_get_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err) {
            cmdline_printf(cl,
                           "ERROR: Failed fetching IMIX app-index %"PRIu32" app config!\n",
                           pr->app_index);
            return;
        }
    }

    if (app_cfg.app_proto != APP_PROTO__HTTP_SERVER) {
        cmdline_printf(cl,
                       "ERROR: Trying to update NON HTTP-SERVER test case!\n");
        return;
    }

    http_cfg = &app_cfg.app_http_server;

    /* Append new field to old ones (if any). */
    if (http_cfg->hs_resp_fields) {
        if (asprintf(&http_fields, "%s\r\n%s", http_cfg->hs_resp_fields,
                     pr->http_field) == -1)
            http_fields = NULL;
    } else {
        http_fields = strdup(pr->http_field);
    }

    if (!http_fields) {
        cmdline_printf(cl, "ERROR: Failed allocating http fields!\n");
        return;
    }

    if (http_server_cfg_init(http_cfg, http_cfg->hs_resp_code,
                             http_fields,
                             http_cfg->hs_resp_size)) {
        cmdline_printf(cl, "ERROR: Failed allocating http server config!\n");

        /* Free the memory allocated by asprintf. */
        free(http_fields);
        return;
    }

    /* Free the memory allocated by asprintf. */
    free(http_fields);

    if (!HTTP_IMIX_ISSET(flags)) {
        err =  test_mgmt_update_test_case_app(pr->port, pr->tcid, &app_cfg,
                                              &parg);
        if (err == 0) {
            cmdline_printf(cl, "Port %"PRIu32", Test Case %"PRIu32" updated!\n",
                           pr->port,
                           pr->tcid);
        } else {
            cmdline_printf(cl, "ERROR: Failed updating test case %"PRIu32
                           " config on port %"PRIu32"\n",
                           pr->tcid,
                           pr->port);
        }
    } else {
        err = imix_cli_set_app_cfg(pr->app_index, &app_cfg, &parg);

        if (err == 0) {
            cmdline_printf(cl, "IMIX app-index %"PRIu32" updated!\n",
                           pr->app_index);
        } else {
            cmdline_printf(cl,
                           "ERROR: Failed updating IMIX app-index %"PRIu32"!\n",
                           pr->app_index);
        }
    }

    if (err != 0) {
        /* If an error happened, free the config memory here. */
        http_server_cfg_free(&app_cfg.app_http_server);
    }
}

cmdline_parse_inst_t cmd_tests_set_app_http_field_server = {
    .f = cmd_tests_set_app_http_server_field_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_NONE,
    .help_str = "set tests server http port <port> test-case-id <tcid> "
                "http-field <http-field>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_server_field_T_set,
        (void *)&cmd_tests_set_app_http_server_field_T_tests,
        (void *)&cmd_tests_set_app_http_server_field_T_server,
        (void *)&cmd_tests_set_app_http_server_field_T_http,
        (void *)&cmd_tests_set_app_http_server_field_T_port_kw,
        (void *)&cmd_tests_set_app_http_server_field_T_port,
        (void *)&cmd_tests_set_app_http_server_field_T_tcid_kw,
        (void *)&cmd_tests_set_app_http_server_field_T_tcid,
        (void *)&cmd_tests_set_app_http_server_field_T_field_kw,
        (void *)&cmd_tests_set_app_http_server_field_T_field,
        NULL,
    },
};

cmdline_parse_inst_t cmd_tests_set_app_http_field_server_imix = {
    .f = cmd_tests_set_app_http_server_field_parsed,
    .data = (void *)(uintptr_t)HTTP_FLAG_IMIX,
    .help_str = "set tests imix app-index <app-index> server http "
                "http-field <http-field>",
    .tokens = {
        (void *)&cmd_tests_set_app_http_server_field_T_set,
        (void *)&cmd_tests_set_app_http_server_field_T_tests,
        (void *)&cmd_tests_set_app_http_server_field_T_imix,
        (void *)&cmd_tests_set_app_http_server_field_T_app_index_kw,
        (void *)&cmd_tests_set_app_http_server_field_T_app_index,
        (void *)&cmd_tests_set_app_http_server_field_T_server,
        (void *)&cmd_tests_set_app_http_server_field_T_http,
        (void *)&cmd_tests_set_app_http_server_field_T_field_kw,
        (void *)&cmd_tests_set_app_http_server_field_T_field,
        NULL,
    },
};

/*****************************************************************************
 * - "show http statistics {details}"
 ****************************************************************************/
struct cmd_show_http_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t http;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
    cmdline_fixed_string_t port_kw;
    uint32_t               port;
};

static cmdline_parse_token_string_t cmd_show_http_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_http_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_http_statistics_T_http =
    TOKEN_STRING_INITIALIZER(struct cmd_show_http_statistics_result, http, "http");
static cmdline_parse_token_string_t cmd_show_http_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_http_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_http_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_http_statistics_result, details, "details");
static cmdline_parse_token_string_t cmd_show_http_statistics_T_port_kw =
        TOKEN_STRING_INITIALIZER(struct cmd_show_http_statistics_result, port_kw, "port");
static cmdline_parse_token_num_t cmd_show_http_statistics_T_port =
        TOKEN_NUM_INITIALIZER(struct cmd_show_http_statistics_result, port, UINT32);

static void cmd_show_http_statistics_parsed(void *parsed_result __rte_unused,
                                            struct cmdline *cl,
                                            void *data)
{
    uint32_t                               port;
    int                                    core;
    struct cmd_show_http_statistics_result *pr = parsed_result;
    int                                    option = (intptr_t) data;

    for (port = 0; port < rte_eth_dev_count_avail(); port++) {
        if ((option == 'p' || option == 'c') && port != pr->port)
            continue;

        /*
         * Calculate totals first
         */
        http_statistics_t  total_stats;
        http_statistics_t *http_stats;

        bzero(&total_stats, sizeof(total_stats));
        STATS_FOREACH_CORE(http_statistics_t, port, core, http_stats) {
            total_stats.hts_req_err += http_stats->hts_req_err;
            total_stats.hts_resp_err += http_stats->hts_resp_err;
        }

        /*
         * Display individual counters
         */
        cmdline_printf(cl, "Port %d HTTP statistics:\n", port);

        SHOW_32BIT_STATS("HTTP Req Build Err", http_statistics_t,
                         hts_req_err,
                         port,
                         option);

        SHOW_32BIT_STATS("HTTP Resp Build Err", http_statistics_t,
                         hts_resp_err,
                         port,
                         option);

        cmdline_printf(cl, "\n");
    }

}

cmdline_parse_inst_t cmd_show_http_statistics = {
    .f = cmd_show_http_statistics_parsed,
    .data = NULL,
    .help_str = "show http statistics",
    .tokens = {
        (void *)&cmd_show_http_statistics_T_show,
        (void *)&cmd_show_http_statistics_T_http,
        (void *)&cmd_show_http_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_http_statistics_details = {
    .f = cmd_show_http_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show http statistics details",
    .tokens = {
        (void *)&cmd_show_http_statistics_T_show,
        (void *)&cmd_show_http_statistics_T_http,
        (void *)&cmd_show_http_statistics_T_statistics,
        (void *)&cmd_show_http_statistics_T_details,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_http_statistics_port = {
    .f = cmd_show_http_statistics_parsed,
    .data = (void *) (intptr_t) 'p',
    .help_str = "show http statistics port <id>",
    .tokens = {
        (void *)&cmd_show_http_statistics_T_show,
        (void *)&cmd_show_http_statistics_T_http,
        (void *)&cmd_show_http_statistics_T_statistics,
        (void *)&cmd_show_http_statistics_T_port_kw,
        (void *)&cmd_show_http_statistics_T_port,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_http_statistics_port_details = {
    .f = cmd_show_http_statistics_parsed,
    .data = (void *) (intptr_t) 'c',
    .help_str = "show http statistics details port <id>",
    .tokens = {
        (void *)&cmd_show_http_statistics_T_show,
        (void *)&cmd_show_http_statistics_T_http,
        (void *)&cmd_show_http_statistics_T_statistics,
        (void *)&cmd_show_http_statistics_T_details,
        (void *)&cmd_show_http_statistics_T_port_kw,
        (void *)&cmd_show_http_statistics_T_port,
        NULL,
    },
};


static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_tests_set_app_http_client,
    &cmd_tests_set_app_http_client_imix,
    &cmd_tests_set_app_http_field_client,
    &cmd_tests_set_app_http_field_client_imix,
    &cmd_tests_set_app_http_server,
    &cmd_tests_set_app_http_server_imix,
    &cmd_tests_set_app_http_field_server,
    &cmd_tests_set_app_http_field_server_imix,
    &cmd_show_http_statistics,
    &cmd_show_http_statistics_details,
    &cmd_show_http_statistics_port,
    &cmd_show_http_statistics_port_details,
    NULL,
};

