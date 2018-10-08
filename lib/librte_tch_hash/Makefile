#   BSD LICENSE
#
#   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

include $(RTE_SDK)/mk/rte.vars.mk

# library name
LIB = librte_tch_hash.a

CFLAGS += -O3
CFLAGS += $(WERROR_FLAGS) $(TCH_FLAGS) -I$(SRCDIR)

EXPORT_MAP := rte_hash_version.map

LIBABIVER := 2

# all source are stored in SRCS-y

SYMLINK-y-include += rte_hash_horton.h
SRCS-y += rte_cuckoo_hash_horton.c

SYMLINK-y-include += rte_hash_bloom.h
SRCS-y += rte_cuckoo_hash_bloom.c

SYMLINK-y-include += rte_hash_lazy_bloom.h
SRCS-y += rte_cuckoo_hash_lazy_bloom.c

SYMLINK-y-include += rte_hash_cond.h
SRCS-y += rte_cuckoo_hash_cond.c

SYMLINK-y-include += rte_hash_lazy_cond.h
SRCS-y += rte_cuckoo_hash_lazy_cond.c

SYMLINK-y-include += rte_hash_lazy_uncond.h
SRCS-y += rte_cuckoo_hash_lazy_uncond.c

SYMLINK-y-include += rte_hash_uncond.h
SRCS-y += rte_cuckoo_hash_uncond.c

SYMLINK-y-include += rte_hash_lazy_no.h
SRCS-y += rte_cuckoo_hash_lazy_no.c

SYMLINK-y-include += rte_hash_v1604.h
SRCS-y += rte_cuckoo_hash_v1604.c

SYMLINK-y-include += rte_hash_v1702.h
SRCS-y += rte_cuckoo_hash_v1702.c


SYMLINK-y-include += rte_hash64.h
SYMLINK-y-include += rte_tchh_structs.h
SYMLINK-y-include += rte_tch_utils.h
SYMLINK-y-include += rte_tch_hash.h
SYMLINK-y-include += rte_hash_template.h
SYMLINK-y-include += rte_hash_commons.h


# this lib needs eal and ring
DEPDIRS-y += lib/librte_eal lib/librte_ring lib/librte_hash

include $(RTE_SDK)/mk/rte.extlib.mk
