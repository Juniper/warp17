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
#     benchmark_plot.gp
#
# Description:
#     Gnuplot script to plot the data generated with test2_perf_benchmark.py
#
# Author:
#     Dumitru Ceara, Eelco Chaudron
#
# Initial Created:
#     05/04/2016
#
# Notes:
#     You can run this script with the following command:
#     gnuplot -e "filename='<benchmark>.csv'" -e "tcp=12" -e "udp=12" \
#       -e "http=15" -e "out_dir='<dir>'" benchmark_plot.gp
#

set datafile separator ","
set datafile missing "0"
set terminal png size 800,480

if (!exists("out_dir")) out_dir = './'
out_file(name) = sprintf("%s/%s", out_dir, name)

set key right bottom

set format x '%.0fb'
set xlabel 'Response Size (bytes)'
set x2label 'Request Size (bytes)'
set xtics rotate by 45 right
set x2tics rotate by 45 right
set x2tics offset 1.5,1

set format y '%.2f'

# Plot TCP RAW
start = 1
end = start + tcp
set yrange [0:4]
set ylabel 'Session Setup Rate (Million sess/s)'
set ytics 1
set output out_file('tcp_raw_setup.png')
plot filename every ::start::end u (log(($3))+log(($2))):($4/1000000):x2tic(2):xtic(3) \
              w lp title 'TCP + RAW data: Client Setup Rate'

set yrange [4:15]
set ylabel 'Million Packets/s'
set ytics 1
set output out_file('tcp_raw_pps.png')
plot filename every ::start::end u (log(($3))+log(($2))):($5/1000000):x2tic(2):xtic(3) \
              w lp title 'TCP + RAW data: Client TX pps', \
     filename every ::start::end u (log(($3))+log(($2))):($6/1000000):x2tic(2):xtic(3) \
              w lp title 'TCP + RAW data: Client RX pps'

set yrange [0:110]
set ylabel 'Link Usage (%)'
set ytics 10
set output out_file('tcp_raw_link_usage.png')
plot filename every ::start::end u (log(($3))+log(($2))):($7):x2tic(2):xtic(3) \
              w lp title 'TCP + RAW data: Client TX link usage', \
     filename every ::start::end u (log(($3))+log(($2))):($8):x2tic(2):xtic(3) \
              w lp title 'TCP + RAW data: Client RX link usage'

# Plot HTTP
start = start + tcp
end = start + http
set yrange [0:4]
set ylabel 'Session Setup Rate (Million sess/s)'
set ytics 1
set output out_file('tcp_http_setup.png')
plot filename every ::start::end u (log(($3))+log(($2))):($4/1000000):x2tic(2):xtic(3) \
              w lp title 'HTTP: Client Setup Rate'

set yrange [4:15]
set ylabel 'Million Packets/s'
set ytics 1
set output out_file('tcp_http_pps.png')
plot filename every ::start::end u (log(($3))+log(($2))):($5/1000000):x2tic(2):xtic(3) \
              w lp title 'HTTP: Client TX pps', \
     filename every ::start::end u (log(($3))+log(($2))):($6/1000000):x2tic(2):xtic(3) \
              w lp title 'HTTP: Client RX pps'

set yrange [0:110]
set ylabel 'Link Usage (%)'
set ytics 10
set output out_file('tcp_http_link_usage.png')
plot filename every ::start::end u (log(($3))+log(($2))):($7):x2tic(2):xtic(3) \
              w lp title 'HTTP: Client TX link usage', \
     filename every ::start::end u (log(($3))+log(($2))):($8):x2tic(2):xtic(3) \
              w lp title 'HTTP: Client RX link usage'

# Plot UDP RAW
start = start + http
end = start + udp
set yrange [4:13]
set ylabel 'Session Setup Rate (Million sess/s)'
set ytics 1
set output out_file('udp_raw_setup.png')
plot filename every ::start::end u (log(($3))+log(($2))):($4/1000000):x2tic(2):xtic(3) \
              w lp title 'UDP + RAW data: Client Setup Rate'

set yrange [4:28]
set ylabel 'Million Packets/s'
set ytics 2
set output out_file('udp_raw_pps.png')
plot filename every ::start::end u (log(($3))+log(($2))):($5/1000000):x2tic(2):xtic(3) \
              w lp title 'UDP + RAW data: Client TX pps', \
     filename every ::start::end u (log(($3))+log(($2))):($6/1000000):x2tic(2):xtic(3) \
              w lp title 'UDP + RAW data: Client RX pps'

set yrange [0:110]
set ylabel 'Link Usage (%)'
set ytics 10
set output out_file('udp_raw_link_usage.png')
plot filename every ::start::end u (log(($3))+log(($2))):($7):x2tic(2):xtic(3) \
              w lp title 'UDP + RAW data: Client TX link usage', \
     filename every ::start::end u (log(($3))+log(($2))):($8):x2tic(2):xtic(3) \
              w lp title 'UDP + RAW data: Client RX link usage'

