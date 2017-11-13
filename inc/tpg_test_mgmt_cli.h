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
 *     tpg_test_mgmt_cli.h
 *
 * Description:
 *     WARP17 mgmt CLI definitions.
 *
 * Author:
 *     Dumitru Ceara
 *
 * Initial Created:
 *     09/22/2017
 *
 * Notes:
 *
 */

/*****************************************************************************
 * Multiple include protection
 ****************************************************************************/
#ifndef _H_TPG_TEST_MGMT_CLI_
#define _H_TPG_TEST_MGMT_CLI_

/*****************************************************************************
 * CLI test type name definitions
 ****************************************************************************/
#define TEST_CASE_UCAST_SRC_STR "client"
#define TEST_CASE_MCAST_SRC_STR "multicast-src"
#define TEST_CASE_UCAST_DST_STR "server"

#define TEST_CASE_CLIENT_STR \
    TEST_CASE_UCAST_SRC_STR "/" TEST_CASE_MCAST_SRC_STR
#define TEST_CASE_SERVER_STR \
    TEST_CASE_UCAST_DST_STR

#define TEST_CASE_CLIENT_CLI_STR \
    TEST_CASE_UCAST_SRC_STR "#" TEST_CASE_MCAST_SRC_STR
#define TEST_CASE_SERVER_CLI_STR \
    TEST_CASE_UCAST_DST_STR

#endif /* _H_TPG_TEST_MGMT_CLI_ */

