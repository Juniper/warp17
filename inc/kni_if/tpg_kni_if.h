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
 *     tpg_kni_if.h
 *
 * Description:
 *     Kernel Networking Interface interface support for running WARP17 with a
 *     interface into the underlying linux kernel.
 *
 * Author:
 *     Dumitru Ceara, Eelco Chaudron
 *
 * Initial Created:
 *     10/12/2016
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_KNI_IF_
#define _H_TPG_KNI_IF_

/*****************************************************************************
 * Definitions
 ****************************************************************************/
#define KNI_IF_CMDLINE_OPTIONS() \
    CMDLINE_OPT_ARG("kni-ifs", true)

#define KNI_IF_CMDLINE_PARSER() \
    CMDLINE_ARG_PARSER(kni_if_handle_cmdline_opt, NULL)

/*****************************************************************************
 * Externals for tpg_kni_if.c
 ****************************************************************************/
extern uint32_t kni_if_get_count(void);
extern void     kni_handle_kernel_status_requests(void);
extern bool     kni_if_handle_cmdline_opt(const char *opt_name,
                                          char *opt_arg);
extern bool     kni_if_init(void);
extern uint32_t kni_get_first_kni_interface(void);


/*****************************************************************************
 * Externals for tpg_kni_pdm.c
 ****************************************************************************/
extern bool kni_eth_from_kni(const char *kni_name, struct rte_kni *kni,
                             const unsigned int numa_node);

/*****************************************************************************
 * End of include file
 ****************************************************************************/
#endif /* _H_TPG_KNI_IF_ */
