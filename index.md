---
layout: default
---

### Who's A Rapid Packet generator?

Lightweight solution for generating high volumes of session-based traffic with
high setup rates. The project emerged from the need
of having an easy to configure and use, open stateful traffic generator
that would run on commodity hardware.

### Performance benchmarks

A peek at _WARP17_'s performances shows that it easily reaches line rate of
40Gbps with:

* TCP setup rates of __17M sessions/sec__
* HTTP setup rates of __7M sessions/sec__ with continuous bidirectional traffic
* UDP rates between __22M pkts/sec__
* 40Gbps line rate traffic with RAW TCP requests of size 1K
* 40Gbps line rate traffic with HTTP requests of size 1K
* 40Gbps line rate traffic with UDP packets of size 256 bytes UDP

For details see the [Benchmarks Section](https://github.com/Juniper/warp17/blob/master/README.md#performance-benchmarks) in
the documentation.

### Want to know more?

Visit the GitHub [project page](https://github.com/Juniper/warp17).

