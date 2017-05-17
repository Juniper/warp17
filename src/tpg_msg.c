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
 *     tpg_msg.c
 *
 * Description:
 *     Simple/fast messaging module.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     05/28/2015
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Include files
 ***************************************************************************/
#include "tcp_generator.h"

/*****************************************************************************
 * Local definitions
 ***************************************************************************/
#define MSGID2MODULE(msgid) \
    ((msgid) >> MSG_PER_MODULE_MSGID_BITS)

#define MSGID2ID(msgid) \
    ((msgid) & ((1 << MSG_PER_MODULE_MSGID_BITS) - 1))

#define MSG_Q_NAME_MAXLEN 512

/*****************************************************************************
 * Global variables
 ***************************************************************************/
/* Message handler callback table. Index by module and message ID. */
static msg_handler_t msg_handler_map[MSG_MODULE_MAX][MSG_PER_MODULE_MAX_MSGIDS];

/* Per lcore_index message queues (actually rings). */
struct rte_ring *msg_queues[RTE_MAX_LCORE] __rte_cache_aligned;
struct rte_ring *msg_local_queues[RTE_MAX_LCORE] __rte_cache_aligned;

/* Define MSG global statistics. Each thread has its own set of locally
 * allocated stats which are accessible through STATS_GLOBAL(type, core, port).
 */
STATS_DEFINE(msg_statistics_t);

/*****************************************************************************
 * Forward declarations
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[];

/*****************************************************************************
 * msg_process()
 ***************************************************************************/
static int msg_process(msg_t *msg)
{
    uint16_t module;
    uint16_t id;
    int      error = 0;
    bool     to_free;

    if (unlikely(!msg))
        return -EINVAL;

    module = MSGID2MODULE(msg->msg_id);
    id = MSGID2ID(msg->msg_id);

    if (unlikely(module >= MSG_MODULE_MAX))
        return -EINVAL;

    if (unlikely(id >= MSG_PER_MODULE_MAX_MSGIDS))
        return -EINVAL;

    if (unlikely(msg_handler_map[module][id] == NULL))
        return -ENOENT;

    assert(rte_atomic32_read(&msg->msg_state) == MSG_STATE_QUEUED);

    to_free = MSG_FLAG_GET_TO_FREE(msg);
    error = msg_handler_map[module][id](msg->msg_id, msg->msg_dest_lcore,
                                        &msg->msg_buf[0]);

    /* -EAGAIN means that the message has to be reposted per request of
     * the application (which also needs to handle the freeing of
     * the message from now on).
     */
    if (error == -EAGAIN) {
        MSG_FLAG_RESET_TO_FREE(msg);
        error = msg_send_local(msg, MSG_SND_FLAG_NOWAIT);

        /* If the callback decided to repost the message for more processing
         * it's safe to return here.
         */
        if (error == 0)
            return 0;
    }

    if (!to_free) {
        /* WARNING! Assuming here that the function call above is a memory
         * barrier. Afaik this is always the case but need to check..
         * Otherwise we need to use:
         * rte_atomic32_cmpset((volatile uint32_t *)&msg->msg_state.cnt,
         *                     MSG_STATE_QUEUED,
         *                     MSG_STATE_DEQUEUED);
         */
        rte_atomic32_set(&msg->msg_state, MSG_STATE_DEQUEUED);
    } else {
        msg_free(msg);
    }
    return error;
}

/*****************************************************************************
 * msg_do_send()
 ***************************************************************************/
static int msg_do_send(msg_t *msg, struct rte_ring *queue, uint32_t snd_flags)
{
    bool noblock;
    bool nowait;
    bool to_free;
    int  error;

    noblock = (snd_flags & MSG_SND_FLAG_NOBLOCK);
    nowait = (snd_flags & MSG_SND_FLAG_NOWAIT);

    to_free = MSG_FLAG_GET_TO_FREE(msg);

    if (!nowait && likely(to_free))
        MSG_FLAG_RESET_TO_FREE(msg);

    INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_snd);

    rte_atomic32_set(&msg->msg_state, MSG_STATE_QUEUED);

    do {
        error = rte_ring_enqueue(queue, msg);
    } while (!noblock && error == -ENOBUFS);

    if (!error && !nowait) {
        /* Warning! Spinning until the destination has finished processing */
        while (rte_atomic32_read(&msg->msg_state) != MSG_STATE_DEQUEUED)
            ;
    }

    if (!error && !nowait) {
        if (likely(to_free))
            rte_free(msg);
        else
            rte_atomic32_set(&msg->msg_state, MSG_STATE_INIT);
    }

    if (unlikely(error != 0))
        INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_err);

    return error;
}

/*****************************************************************************
 * msg_sys_init()
 ***************************************************************************/
bool msg_sys_init(void)
{
    uint32_t         lcore;
    uint32_t         queue_size;
    global_config_t *cfg;

    /*
     * Add MSG module CLI commands
     */
    if (!cli_add_main_ctx(cli_ctx)) {
        RTE_LOG(ERR, USER1, "ERROR: Can't add MSG specific CLI commands!\n");
        return false;
    }

    /*
     * Allocate memory for MSG statistics, and clear all of them
     */
    if (STATS_GLOBAL_INIT(msg_statistics_t, "msg_stats") == NULL) {
        RTE_LOG(ERR, USER1,
                "ERROR: Failed allocating MSG statistics memory!\n");
        return false;
    }

    cfg = cfg_get_config();
    if (!cfg)
        return false;

    queue_size = cfg->gcfg_msgq_size;

    /* Queue size must be a power of 2! */
    assert(queue_size && !((queue_size - 1) & queue_size));

    RTE_LCORE_FOREACH(lcore) {
        char q_name[MSG_Q_NAME_MAXLEN];

        int lcore_idx = rte_lcore_index(lcore);

        /* Skip the CLI lcore. */
        if (lcore_idx == TPG_CORE_IDX_CLI)
            continue;

        snprintf(q_name, MSG_Q_NAME_MAXLEN, "%s%d", GCFG_MSGQ_NAME, lcore);
        msg_queues[lcore_idx] = rte_ring_create(q_name,
                                                queue_size,
                                                SOCKET_ID_ANY, RING_F_SC_DEQ);
        if (!msg_queues[lcore_idx]) {
            TPG_ERROR_ABORT("ERROR: %s!\n",
                            "Failed to allocate core msg queue");
            return false;
        }

        snprintf(q_name, MSG_Q_NAME_MAXLEN, "%s%d-local", GCFG_MSGQ_NAME,
                 lcore);
        msg_local_queues[lcore_idx] = rte_ring_create(q_name, queue_size,
                                                      SOCKET_ID_ANY,
                                                      RING_F_SC_DEQ | RING_F_SP_ENQ);
        if (!msg_local_queues[lcore_idx]) {
            TPG_ERROR_ABORT("ERROR: %s!\n",
                            "Failed to allocate core local msg queue");
            return false;
        }
    }

    /* Initialize current lcore too. */
    return msg_sys_lcore_init(rte_lcore_id());
}

/*****************************************************************************
 * msg_sys_lcore_init()
 ****************************************************************************/
bool msg_sys_lcore_init(uint32_t lcore_id)
{
    /* Init the local stats. */
    if (STATS_LOCAL_INIT(msg_statistics_t, "msg_stats", lcore_id) == NULL) {
        TPG_ERROR_ABORT("[%d:%s() Failed to allocate per lcore msg_stats!\n",
                        rte_lcore_index(lcore_id),
                        __func__);
        return false;
    }

    return true;
}

/*****************************************************************************
 * msg_register_handler()
 ****************************************************************************/
int msg_register_handler(uint16_t msgid,
                         msg_handler_t handler)
{
    uint16_t module;
    uint16_t id;

    module = MSGID2MODULE(msgid);
    id = MSGID2ID(msgid);

    if (module >= MSG_MODULE_MAX)
        return -EINVAL;

    if (id >= MSG_PER_MODULE_MAX_MSGIDS)
        return -EINVAL;

    if (msg_handler_map[module][id])
        return -EEXIST;

    msg_handler_map[module][id] = handler;
    return 0;
}

/*****************************************************************************
 * msg_alloc()
 ***************************************************************************/
msg_t *msg_alloc(uint16_t msgid, uint32_t msg_size, uint16_t dest_lcore)
{
    msg_t *msg;

    msg = rte_malloc("msg", sizeof(*msg) + msg_size, 0);
    if (!msg) {
        INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_alloc_err);
        return NULL;
    }

    msg_init(msg, msgid, dest_lcore, MSG_FLAG_TO_FREE);

    INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_alloc);
    return msg;
}

/*****************************************************************************
 * msg_free()
 ***************************************************************************/
void msg_free(msg_t *msg)
{
    INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_free);

    /* TODO: It would probably be better to use a mempool as the messages
     * *might* be freed sometimes by pkt cores.
     */
    rte_free(msg);
}

/*****************************************************************************
 * msg_init()
 ***************************************************************************/
void msg_init(msg_t *msg, uint16_t msgid, uint16_t dest_lcore, uint8_t flags)
{
    if (unlikely(!msg))
        return;

    msg->msg_id = msgid;
    msg->msg_flags = flags;
    msg->msg_dest_lcore = dest_lcore;
    rte_atomic32_init(&msg->msg_state);
    rte_atomic32_set(&msg->msg_state, MSG_STATE_INIT);
}

/*****************************************************************************
 * msg_send()
 ***************************************************************************/
int msg_send(msg_t *msg, uint32_t snd_flags)
{
    if (MSG_FLAG_GET_LOCAL(msg))
        return msg_send_local(msg, snd_flags);
    else
        return msg_send_remote(msg, snd_flags);
}

/*****************************************************************************
 * msg_send_local()
 ***************************************************************************/
int msg_send_local(msg_t *msg, uint32_t snd_flags)
{
    int core_idx;

    if (unlikely(!msg))
        return -EINVAL;

    if (unlikely(msg->msg_dest_lcore > RTE_MAX_LCORE))
        return -EINVAL;

    core_idx = rte_lcore_index(msg->msg_dest_lcore);
    if (unlikely(core_idx == -1))
        return -EINVAL;

    return msg_do_send(msg, msg_local_queues[core_idx], snd_flags);
}

/*****************************************************************************
 * msg_send_remote()
 ***************************************************************************/
int msg_send_remote(msg_t *msg, uint32_t snd_flags)
{
    int core_idx;

    if (unlikely(!msg))
        return -EINVAL;

    if (unlikely(msg->msg_dest_lcore > RTE_MAX_LCORE))
        return -EINVAL;

    core_idx = rte_lcore_index(msg->msg_dest_lcore);
    if (unlikely(core_idx == -1))
        return -EINVAL;

    if (unlikely(msg_queues[core_idx] == NULL))
        return -EADDRNOTAVAIL;

    return msg_do_send(msg, msg_queues[core_idx], snd_flags);
}

/*****************************************************************************
 * msg_poll_queue()
 ****************************************************************************/
static int msg_poll_queue(struct rte_ring *queue)
{
    void *msg;
    int   error;

    if (unlikely(!queue))
        return -EINVAL;

    INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_poll);

    error = rte_ring_dequeue(queue, &msg);

    /* Nothing found so return.. */
    if (likely(error == -ENOENT))
        return 0;

    INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_rcvd);
    error = msg_process(msg);
    if (unlikely(error)) {
        INC_STATS(STATS_LOCAL(msg_statistics_t, 0), ms_proc_err);
        return error;
    }
    return 0;
}

/*****************************************************************************
 * msg_poll()
 ****************************************************************************/
int msg_poll(void)
{
    uint32_t lcore_idx;
    int      err;

    lcore_idx = rte_lcore_index(rte_lcore_id());

    err = msg_poll_queue(msg_queues[lcore_idx]);
    if (err != 0)
        return err;

    return msg_poll_queue(msg_local_queues[lcore_idx]);
}

/*****************************************************************************
 * CLI commands
 *****************************************************************************
 * - "show msg statistics {details}"
 ****************************************************************************/
struct cmd_show_msg_statistics_result {
    cmdline_fixed_string_t show;
    cmdline_fixed_string_t msg;
    cmdline_fixed_string_t statistics;
    cmdline_fixed_string_t details;
};

static cmdline_parse_token_string_t cmd_show_msg_statistics_T_show =
    TOKEN_STRING_INITIALIZER(struct cmd_show_msg_statistics_result, show, "show");
static cmdline_parse_token_string_t cmd_show_msg_statistics_T_msg =
    TOKEN_STRING_INITIALIZER(struct cmd_show_msg_statistics_result, msg, "msg");
static cmdline_parse_token_string_t cmd_show_msg_statistics_T_statistics =
    TOKEN_STRING_INITIALIZER(struct cmd_show_msg_statistics_result, statistics, "statistics");
static cmdline_parse_token_string_t cmd_show_msg_statistics_T_details =
    TOKEN_STRING_INITIALIZER(struct cmd_show_msg_statistics_result, details, "details");

static void cmd_show_msg_statistics_parsed(void *parsed_result __rte_unused,
                                           struct cmdline *cl,
                                           void *data)
{
    int               core;
    int               option = (intptr_t)data;
    msg_statistics_t  total_stats;
    msg_statistics_t *msg_stats;

    /*
     * Calculate totals first
     */

    bzero(&total_stats, sizeof(total_stats));

    STATS_FOREACH_CORE(msg_statistics_t, 0, core, msg_stats) {
        total_stats.ms_rcvd += msg_stats->ms_rcvd;
        total_stats.ms_snd += msg_stats->ms_snd;
        total_stats.ms_poll += msg_stats->ms_poll;

        total_stats.ms_err += msg_stats->ms_err;
        total_stats.ms_proc_err += msg_stats->ms_proc_err;
    }

    /*
     * Display individual counters
     */
    cmdline_printf(cl, "MSG statistics:\n");

    SHOW_64BIT_STATS_ALL_CORES("Messages rcvd", msg_statistics_t, ms_rcvd, 0,
                               option);

    SHOW_64BIT_STATS_ALL_CORES("Messages sent", msg_statistics_t, ms_snd, 0,
                               option);

    SHOW_64BIT_STATS_ALL_CORES("Messages polled", msg_statistics_t, ms_poll, 0,
                               option);

    cmdline_printf(cl, "\n");

    SHOW_64BIT_STATS_ALL_CORES("Messages errors", msg_statistics_t, ms_err, 0,
                               option);

    SHOW_64BIT_STATS_ALL_CORES("Messages proc err", msg_statistics_t,
                               ms_proc_err,
                               0,
                               option);

    cmdline_printf(cl, "\n");

    SHOW_64BIT_STATS_ALL_CORES("Messages allocated", msg_statistics_t, ms_alloc,
                               0,
                               option);

    SHOW_64BIT_STATS_ALL_CORES("Messages alloc err", msg_statistics_t,
                               ms_alloc_err,
                               0,
                               option);

    SHOW_64BIT_STATS_ALL_CORES("Messages freed", msg_statistics_t, ms_free, 0,
                               option);

}

cmdline_parse_inst_t cmd_show_msg_statistics = {
    .f = cmd_show_msg_statistics_parsed,
    .data = NULL,
    .help_str = "show msg statistics",
    .tokens = {
        (void *)&cmd_show_msg_statistics_T_show,
        (void *)&cmd_show_msg_statistics_T_msg,
        (void *)&cmd_show_msg_statistics_T_statistics,
        NULL,
    },
};

cmdline_parse_inst_t cmd_show_msg_statistics_details = {
    .f = cmd_show_msg_statistics_parsed,
    .data = (void *) (intptr_t) 'd',
    .help_str = "show msg statistics details",
    .tokens = {
        (void *)&cmd_show_msg_statistics_T_show,
        (void *)&cmd_show_msg_statistics_T_msg,
        (void *)&cmd_show_msg_statistics_T_statistics,
        (void *)&cmd_show_msg_statistics_T_details,
        NULL,
    },
};

/*****************************************************************************
 * Main menu context
 ****************************************************************************/
static cmdline_parse_ctx_t cli_ctx[] = {
    &cmd_show_msg_statistics,
    &cmd_show_msg_statistics_details,
    NULL,
};

