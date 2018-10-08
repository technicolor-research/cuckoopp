Cuckoo++
========
Cuckoo++ is a high-performance hash table implementation targetted at network packet processing applications. The hash table implementation provides highly-optimized code for batched lookups, typical of packet processing applications. Indeed, when a batch of keys is looked up in a hash table, it allows for specific optimization (prefetching,...) that significantly improve performance. It further implements algorithmical optimization described in [Cuckoo++ Hash Tables: High-Performance Hash Tables for Networking Applications, Nicolas Le Scouarnec, IEEE/ACM ANCS, 2018](https://dl.acm.org/citation.cfm?doid=3230718.3232629). If you are interested in design decisions (e.g., using SSE vs AVX, adding a bloom filter, having 8 slots per bucket) or performance evaluation, you are invited to refer to the aforementionned paper.

If you use this work, please cite:  
> **Cuckoo++ Hash Tables: High-Performance Hash Tables for Networking Applications**  
> Nicolas Le Scouarnec  
> *ACM/IEEE Symposium on Architectures for Networking and Communications Systems (ANCS)*  
> July 2018  
> https://doi.org/10.1145/3230718.3232629

License
=======
Most of the code of Cuckoo++ is licensed under Clear BSD license (see LICENSE.md) and copyrighted by Thomson Licensing. It contains portions of code from DPDK, which are licensed under BSD license and copyrighted by others as stated in file headers.

Building and testing
====================
You need to have the tools and libraries listed as dependencies of DPDK (see [DPDK documentation](http://doc.dpdk.org/guides/linux_gsg/sys_reqs.html#compilation-of-the-dpdk)). Then, you must get a copy of DPDK (preferably version 17.08) and compile it. You can then compile the additional library and example programs. It has been tested on Ubuntu 16.04 and 18.04 but should work on other distributions and versions supported by DPDK.

```
# Export variables necessary to compilation
mkdir ~/dpdk-cuckoopp
cd ~/dpdk-cuckoopp
export RTE_SDK=~/dpdk-cuckoopp/dpdk
export RTE_TARGET=x86_64-native-linuxapp-gcc

# Get and compile DPDK
git clone --branch v17.08 git://dpdk.org/dpdk
make config T=x86_64-native-linuxapp-gcc
make

# Get and compile Cuckoo++ (depends on DPDK)
git clone https://github.com/technicolor-research/cuckoopp.git
make
```

Built applications are available in `build/apps/`. These are DPDK applications and thus require hugepages and usual DPDK arguments (see [DPDK documentation](http://doc.dpdk.org/guides/linux_gsg/sys_reqs.html#running-dpdk-applications)). As an example, one can reserve hugepages, make them available and run the hash_perf application with the following commands. Do not forget:  **(i) you need hugepages (check `/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages`)**  and **(ii) you must run the program as root**.

```
# All commands (including the app) must be run as root (super user)
# Reserve hugepages (can also be done at boot time or reserve 1GB hugepages)
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
mkdir /mnt/huge
mount -t hugetlbfs nodev /mnt/huge

# Measure the performance for a hash-table of capacity of 1000000 entries and load factor of 0.9
cd build/app
./hash-perf --lcores 0@0 --socket-mem 1024,0   -w99:0.0 -- 1000000 0.9
```

For optimal performance, additional tunning may be needed such as using 1G hugepages, isolating CPU cores, configuring Memory Snoop Mode. Settings for optimal performance are described in DPDK documentation.

Benckmarking
============
The benchmarking application used for the paper on Cuckoo++ is `hash-cpp`. An example usage is the following:
```
sudo build/hash_cpp -- -c 500000 -l 0.8  -i 0.5 BLOOM
```

DPDK arguments come before the --  
Application arguments come after the --  
 * _-c_ gives the hash table capacity
 * _-l_ gives the load factor  
 * _-i_ gives the fraction of negative lookups  
 * _-t_ can be used to test multiple threads  
 * _BLOOM_ is the implementation to benchmark

The available implementations are
 * **DPDK_1604**, almost vanilla DPDK 16.04 implementation changed to support 128 bit values.
 * **DPDK_1702**, almost vanilla DPDK 16.04 implementation changed to support 128 bit values.
 * **COND**, our optimized implementation with an optimistic prefetching strategy
 * **UNCOND**, our optimized implementation with a pessimistic prefetching strategy
 * **BLOOM**, our Cuckoo++ implementation (which should be used most of the time)
 * **HORTON**, our own optimized implementation of Horton tables for CPUs
 * **LAZY_BLOOM, LAZY_COND, LAZY_UNCOND**, same as previous but with builtin timers.

_Note: the program must be run as root and there must be sufficient available memory (hugepages) on all sockets (especially if you benchmark larger hash tables)._

Using it in your application
============================
Cuckoo++ is meant to be used, as is, in DPDK-based applications. Hence, it depends on DPDK (see building section). Yet, it could easily be ported to plain C.

The API is relatively close to the original hash-table from DPDK and it could be used as a drop-in replacement in numerous cases. The main limit is that Cuckoo++ was developped for an application that uses sharding to the core (see [Krononat - USENIX ATC 2018](https://www.usenix.org/conference/atc18/presentation/andre)) and thus does not implement concurrent access to the hash-table from multiple threads.

It however implements additional features such as built-in timers (described in [Cuckoo++ Hash Tables - arXiv 2017](https://arxiv.org/abs/1712.09624)) and iterators (described in [Krononat - USENIX ATC 2018](https://www.usenix.org/conference/atc18/presentation/andre)).

This library implements several highly-optimized variants (Vanilla "Pessimistic" Cuckoo Hash-Table, Vanilla "Optimistic" Cuckoo Hash-Table, **Cuckoo++ Hash Tables**, and our implementation of [Horton Hash Tables](https://www.usenix.org/conference/atc16/technical-sessions/presentation/breslow) for CPUs.). Furthermore, all implementations exists with or without built-in entry expiration (lazy variants). The performance and benefits of all variants are discussed in [Cuckoo++ - ANCS 2018]((https://dl.acm.org/citation.cfm?doid=3230718.3232629)).

Using this library requires to choose the implementation you want to use; we recommand Cuckoo++ as it achieves excellent performance for all workloads. This choice can be done, either at compile time by including the header of the desired implementation (e.g., `rte_cuckoo_hash_bloom.h` for a Cuckoo++ hash-table), or at runtime by including `rte_tch_hash.h` which allows selecting the implementation. Example usages of  `rte_tch_hash.h`  are provided in `apps/hash-perf` and `apps/hash-stats`.

The API is described in `lib/librte_tch_hash/rte_hash_template.h`.

The available implementations are
 * **COND**, our optimized implementation with an optimistic prefetching strategy
 * **UNCOND**, our optimized implementation with a pessimistic prefetching strategy
 * **BLOOM**, our Cuckoo++ implementation (which should be used most of the time)
 * **HORTON**, our own optimized implementation of Horton tables for CPUs
 * **LAZY_BLOOM, LAZY_COND, LAZY_UNCOND**, same as previous but with builtin timers.


References
==========
> **Cuckoo++ Hash Tables: High-Performance Hash Tables for Networking Applications**  
> Nicolas Le Scouarnec  
> *ACM/IEEE Symposium on Architectures for Networking and Communications Systems (ANCS)*  
> July 2018  
> https://doi.org/10.1145/3230718.3232629

> **Don't share, Don't lock: Large-scale Software Connection Tracking with Krononat**  
> Fabien André, Stéphane Gouache, Nicolas Le Scouarnec, and Antoine Monsifrot  
> *USENIX ATC'18 - USENIX Annual Technical Conference*  
> July 2018  
> https://doi.org/10.1145/3230718.3232629

> **Cuckoo++ Hash Tables: High-Performance Hash Tables for Networking Applications**  
> **_(Technical report that describes built-in timers)_**  
> Nicolas Le Scouarnec  
> *arXiv 1712.09624*  
> December 2017  
> https://doi.org/10.1145/3230718.3232629