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
 *     tpg_msg.h
 *
 * Description:
 *     Simple/fast messaging interface.
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
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_MSG_
#define _H_TPG_MSG_

/*****************************************************************************
 * MSG Module list. All modules that require messaging must be declared
 * here.
 ****************************************************************************/
enum msg_modules {

    MSG_MODULE_MIN = 1,
    MSG_TRACE_MODULE = MSG_MODULE_MIN,
    MSG_TRACE_FILTER_MODULE,
    MSG_ROUTE_MODULE,
    MSG_TESTS_MODULE,
    MSG_TEST_MGMT_MODULE,
    MSG_PKTLOOP_MODULE,
    MSG_MODULE_MAX

};

/*****************************************************************************
 * Message ID macros
 ****************************************************************************/
#define MSG_PER_MODULE_MSGID_BITS (8)
#define MSG_PER_MODULE_MAX_MSGIDS (1 << MSG_PER_MODULE_MSGID_BITS)

/* Leave room for 2^8 message types for each module. */
#define MSGID_INITIALIZER(module) \
    ((module) << MSG_PER_MODULE_MSGID_BITS)

#define MSG_TYPE_START_MARKER(module) \
    MSG_ ## module ## _START_MARKER

#define MSG_TYPE_END_MARKER(module) \
    MSG_ ## module ## _END_MARKER

#define MSG_TYPE_DEF_START_MARKER(module) \
    MSG_TYPE_START_MARKER(module) = MSGID_INITIALIZER(MSG_ ## module ## _MODULE)

#define MSG_TYPE_DEF_END_MARKER(module) \
    MSG_TYPE_END_MARKER(module)

/* Checks that we didn't define too many messge types. */
#define MSG_TYPE_MAX_CHECK(module)                           \
    static_assert(MSG_TYPE_END_MARKER(module) -              \
                  MSG_TYPE_START_MARKER(module) <=           \
                  MSG_PER_MODULE_MAX_MSGIDS,                 \
                  "Too many message types for this module!")


/*****************************************************************************
 * Message type definitions
 ****************************************************************************/
typedef struct msg_hdr_s {
    uint32_t        msg_id         :16;
    uint32_t        msg_flags      :8;
    uint32_t        msg_dest_lcore :8;
    rte_atomic32_t  msg_state;

    /* Align the msg field because this will probably hold a structure. */
    char            msg_buf[0] __rte_cache_aligned;
} msg_t;

#define MSG_INNER(type, msg) ((__typeof__(type) *)&((msg)->msg_buf[0]))

/* Flag to determine if the message should be freed after processing or not. */
#define MSG_FLAG_TO_FREE (1 << 0)
/* Flag to determine if the message is local to the core or not. */
#define MSG_FLAG_LOCAL   (1 << 1)

#define MSG_FLAG_GET_TO_FREE(msg) \
    ((msg)->msg_flags & MSG_FLAG_TO_FREE)
#define MSG_FLAG_SET_TO_FREE(msg) \
    ((msg)->msg_flags |= MSG_FLAG_TO_FREE)
#define MSG_FLAG_RESET_TO_FREE(msg) \
    ((msg)->msg_flags &= ~MSG_FLAG_TO_FREE)

#define MSG_FLAG_GET_LOCAL(msg) \
    ((msg)->msg_flags & MSG_FLAG_LOCAL)
#define MSG_FLAG_SET_LOCAL(msg) \
    ((msg)->msg_flags |= MSG_FLAG_LOCAL)
#define MSG_FLAG_RESET_LOCAL(msg) \
    ((msg)->msg_flags &= ~MSG_FLAG_LOCAL)

#define MSG_FLAG_GET_BCAST(msg) \
    ((msg)->msg_flags & MSG_FLAG_BCAST)
#define MSG_FLAG_SET_BCAST(msg) \
    ((msg)->msg_flags |= MSG_FLAG_BCAST)
#define MSG_FLAG_RESET_BCAST(msg) \
    ((msg)->msg_flags &= ~MSG_FLAG_BCAST)

typedef enum msg_states_s {
    MSG_STATE_INIT = 0,
    MSG_STATE_QUEUED,
    MSG_STATE_DEQUEUED,
} msg_states_t;

#define MSG_TYPEDEF(inner_type)              \
    union {                                  \
        char msg_buf[sizeof(msg_t) +         \
            sizeof(__typeof__(inner_type))]; \
        msg_t msg;                           \
    }

#define MSG_LOCAL_DEFINE(inner_type, name) \
    MSG_TYPEDEF(inner_type) (name)

#define MSG_LOCAL(name) (&((name).msg))

typedef int (*msg_handler_t)(uint16_t msgid, uint16_t lcore, void *msg);

#define MSG_SND_FLAG_NOBLOCK (1 << 0)
#define MSG_SND_FLAG_NOWAIT  (1 << 1)

#define __tpg_msg __attribute__((__may_alias__))

#define MSG_INVALID(msgid, inner_msg, expected_type) \
    (unlikely((msgid) != (expected_type) || (inner_msg) == NULL))

/*****************************************************************************
 * MSG statistics
 ****************************************************************************/
typedef struct msg_statistics_s {

    uint64_t ms_rcvd;
    uint64_t ms_snd;
    uint64_t ms_poll;

    uint64_t ms_err;
    uint64_t ms_proc_err;

    uint64_t ms_alloc;
    uint64_t ms_alloc_err;
    uint64_t ms_free;

} __rte_cache_aligned msg_statistics_t;

/*****************************************************************************
 * Externals for messaging module
 ****************************************************************************/
/* Initializes the messages subsystem. */
extern bool   msg_sys_init(void);
extern bool   msg_sys_lcore_init(uint32_t lcore_id);
extern int    msg_register_handler(uint16_t msgid, msg_handler_t handler);

/* Allocate and initialize a new message. */
extern msg_t *msg_alloc(uint16_t msgid, uint32_t msg_size, uint16_t dest_lcore);

/*
 * Initialize an existing message. Careful: make sure it's not in use anymore!
 */
extern void   msg_init(msg_t *msg, uint16_t msgid, uint16_t dest_lcore,
                       uint8_t flags);

/*
 * Will send a message either locally or remote based on the msg flags.
 */
extern int    msg_send(msg_t *msg, uint32_t snd_flags);

/*
 * Will send a message to a remote core. Should be used when in fast path
 * instead of msg_send.
 */
extern int    msg_send_remote(msg_t *msg, uint32_t snd_flags);

/*
 * Will send a message to the local core. Should be used when in fast path
 * instead of msg_send.
 */
extern int    msg_send_local(msg_t *msg, uint32_t snd_flags);

extern void   msg_free(msg_t *msg);
extern int    msg_poll(void);
extern void   msg_total_stats_clear(uint32_t lcore_id);

#endif /* _H_TPG_MSG_ */

