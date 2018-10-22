
# Performance benchmarks

## <a name="hw"></a> Reference platform HW configuration
The configuration of the server on which the WARP17 benchmarks were run is:

* [Super X10DRX](http://www.supermicro.com/products/motherboard/Xeon/C600/X10DRX.cfm)
  dual socket motherboard
* Two [Intel&reg; Xeon&reg; Processor E5-2660 v3](http://ark.intel.com/products/81706/Intel-Xeon-Processor-E5-2660-v3-25M-Cache-2_60-GHz)
* 128GB RAM, using 16x 8G DDR4 2133Mhz to fill all the memory slots
* 2 40G [Intel&reg; Ethernet Converged Network Adapter XL710-QDA1](http://ark.intel.com/products/83966/Intel-Ethernet-Converged-Network-Adapter-XL710-QDA1)

__NOTE__: In order for the best performance to be achieved when running only
one instance of WARP17, NICs were installed on different CPU sockets. In
our case the two XL710 40G adapters were installed on __socket 0__ and
__socket 1__.

For all tests we used the following WARP17 configuration (detailed descriptions
of the command line arguments can be found in the [WARP17 command-line arguments](#warp17-command-line-arguments) section):

* The 40G adapters were connected back to back
* 34 lcores (hardware threads): `-c 0xFF3FCFF3FF`
	- 16 lcores were reserved for generating/receiving traffic on port 0
	- 16 lcores were reserved for generating/receiving traffic on port 1
	- 2 lcores are used for management of the test
* 32GB RAM memory allocated from hugepages: `-m 32768`

Three types of session setup benchmarks were run, __while emulating both
the servers and the clients__ when using 16 lcores for each ethernet port:

* TCP sessions with raw (random) application payload
* TCP sessions with __real__ HTTP payload
* UDP traffic with raw (random) payload

For each type of traffic 3 charts are presented with results collected
when running the test with different request/response message sizes. These
charts show the client port:

* Session setup rate
* Packets per second transmitted and received
* Ethernet link utilization (percentage of 40Gbps)

It is interesting to see that when emulating real HTTP traffic on top of
a few million TCP sessions, WARP17 can easily exhaust the 40Gbps throughput of
the link.

NOTE: the script used to obtain the benchmark results is available in the
codebase at `examples/python/test_2_perf_benchmark.py`. The script spawns WARP17
for each of the test configurations we were interested in.

## TCP setup and data rates for RAW application traffic

__NOTE__: In the case when we only want to test the TCP control implementation
(i.e., the TCP 3-way handshake and TCP CLOSE sequence), WARP17 achieved the
maximum setup rate of 8.5M clients/s and 8.5M servers/s, __so a total of
17M TCP sessions are handled every second__.

The tests set up 20 million TCP sessions (i.e., 10 million TCP clients and 10
million TCP servers) on which clients continuously send fixed size requests
(with random payload) and wait for fixed size responses from the servers.

* TCP raw traffic link utilization reaches line rate (40Gbps) as we increase
  the size of the requests and responses. When line rate is achieved the number
  of packets that actually make it on the wire decreases (due to the link
  bandwidth):

<div align="center">
  <img src="benchmarks/tcp_raw_link_usage.png" width="49%" alt="TCP raw link usage">
  <img src="benchmarks/tcp_raw_pps.png" width="49%" alt="TCP raw pps">
</div>

* TCP raw traffic setup rate is stable at approximately
  __7M sessions per second__ (3.5M TCP clients and 3.5M TCP servers per second)

<div align="center">
  <img src="benchmarks/tcp_raw_setup.png" width="49%" alt="TCP raw setup rate">
</div>

## TCP setup and data rates for HTTP application traffic

The tests set up 20 million TCP sessions (i.e., 10 million TCP clients and 10
million TCP servers) on which the clients continuously send _HTTP GET_
requests and wait for the _HTTP_ responses from the servers.

* HTTP traffic link utilization reaches line rate (40Gbps) as we increase the
  size of the requests and responses. When line rate is achieved the number
  of packets that actually make it on the wire decreases (due to the link
  bandwidth):

<div align="center">
  <img src="benchmarks/tcp_http_link_usage.png" width="49%" alt="HTTP link usage">
  <img src="benchmarks/tcp_http_pps.png" width="49%" alt="HTTP pps">
</div>

* HTTP traffic setup rate is stable at approximately
  __7M sessions per second__ (3.5M HTTP clients and 3.5M HTTP servers per
  second)

<div align="center">
  <img src="benchmarks/tcp_http_setup.png" width="49%" alt="HTTP setup rate">
</div>

## UDP setup and data rates for RAW application traffic

The tests continuously send UDP fixed size packets (with random
payload) from 10 million clients which are processed on the receing side by
10 million UDP listeners.

* UDP packets are generated at approximately __22 Mpps__ (for small packets) and
  as we reach the link bandwidth the rate decreases.

<div align="center">
  <img src="benchmarks/udp_raw_link_usage.png" width="49%" alt="UDP raw link usage">
  <img src="benchmarks/udp_raw_pps.png" width="49%" alt="UDP raw pps">
</div>

## Memory consumption

Warp17 memory consumption depends mostly on which testcase are you configuring
and which machine configuration you have:

type of nics, number of sockets, number of cpu and their architecture can
influence how much memory is needed by warp17, moreover when you configure
a server you can't really predict how much memory is gonna use.

Those two tests, which have been run on our [setup](#hw), listed below will give a better idea.

### 1 session

In order to run 1 TCP session, we can allocate just 1 hugepage of 1 Giga
per each socket


This is the commandline needed to run this test:
```
./warp17-private/build/warp17 -c 0xF -n 1 --socket-mem 1,1 -- --qmap-default max-q --tcb-pool-sz 1 --ucb-pool-sz 0 --mbuf-pool-sz 18 --mbuf-hdr-pool-sz 1
```

### 10 Million sessions

In order to run 10 million TCP sessions test on our setup we need a tcb-pool-sz of 20000 at least
and 18432 MB of memory.


This is the commandline needed to run this test:
```
./warp17-private/build/warp17 -c 0xFF3FCFF3FF -n 4 -m 18432 -- --qmap-default max-q --tcb-pool-sz 20000 --ucb-pool-sz 0
```
