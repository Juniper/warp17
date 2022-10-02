#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
#
# Copyright (c) 2016, Juniper Networks, Inc. All rights reserved.
#
#
# The contents of this file are subject to the terms of the BSD 3 clause
# License (the "License"). You may not use this file except in compliance
# with the License.
#
# You can obtain a copy of the license at
# https://github.com/Juniper/warp17/blob/master/LICENSE.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# File name:
#     Makefile
#
# Description:
#     Main WARP17 makefile.
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     01/07/2016
#
# Notes:
#
#

.PHONY: all clean pyclean unittest

APIDIR = api
UTDIR  = ut
MKAPI  = Makefile.api
MKDPDK = Makefile.dpdk
MKUT   = Makefile.ut

Q ?= @

PYOBJS     = python/*.pyc
UNIQ_STAMP = $(shell python python/uniq.py)
UT_ARGS    = WARP17_UNIQ_STAMP=$(UNIQ_STAMP)

all:
	$(Q)$(MAKE) -C $(APIDIR) -f $(MKAPI) -j1 all Q=$(Q)
	$(Q)+$(MAKE) -f $(MKDPDK) M=$(MKDPDK)

all-ring-if:
	$(Q)$(MAKE) -C $(APIDIR) -f $(MKAPI) -j1 all Q=$(Q)
	$(Q)+$(MAKE) -f $(MKDPDK) M=$(MKDPDK) WARP17_RING_IF=1

all-kni-if:
	$(Q)$(MAKE) -C $(APIDIR) -f $(MKAPI) -j1 all Q=$(Q)
	$(Q)+$(MAKE) -f $(MKDPDK) M=$(MKDPDK) WARP17_KNI_IF=1

all-ring-kni-if:
	$(Q)$(MAKE) -C $(APIDIR) -f $(MKAPI) -j1 all Q=$(Q)
	$(Q)+$(MAKE) -f $(MKDPDK) M=$(MKDPDK) WARP17_RING_IF=1 WARP17_KNI_IF=1

clean: pyclean
	$(Q)+$(MAKE) -C $(APIDIR) -f $(MKAPI) clean Q=$(Q)
	$(Q)+$(MAKE) -C $(UTDIR) -f $(MKUT) clean Q=$(Q)
	$(Q)+$(MAKE) -f $(MKDPDK) clean M=$(MKDPDK) Q=$(Q)

pyclean:
	$(Q)$(RM) -rf $(PYOBJS)

unittest:
	$(Q)$(MAKE) -C $(UTDIR) -f $(MKUT) -i -j1 all Q=$(Q) $(UT_ARGS)

