/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_vect.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_string_fns.h>
#include <assert.h>

#include <x86intrin.h>

#include <rte_hash64.h>
#include <rte_tch_hash.h>
#include <rte_tch_utils.h>


//const enum rte_tch_hash_variants v = H_LAZY_BLOOM;
//const enum rte_tch_hash_variants v = H_V1702;
//const enum rte_tch_hash_variants v = H_TCH2016;
enum rte_tch_hash_variants v;

#define RTE_LOGTYPE_L3NAT RTE_LOGTYPE_USER1

#define MAX_PKT_BURST 32u

int N_capacity = 1024*1024;
int N_insertions = 1024*850;
int N_lookups = 1024*1024*100;
int inv_Failure = 8; // must be power of 2.

struct hh {
	volatile uint64_t v;
} __rte_cache_aligned;

volatile struct hh hv[32];


// Up to 32 threads
struct rte_tch_hash * h[32];
#pragma message "Random state is shared between state, need to refactor application"
struct rte_tch_rand_state rand_s;

//double diff_tsc[32];
volatile double cycles[32];
volatile double rate[32];
volatile int nops;
volatile int hashmap_size;
int total_inserts=0;

//enum rte_tch_hash_variants var[] = {H_HORTON, H_COND, H_BLOOM, H_LAZY_BLOOM,H_UNCOND, H_V1702, H_LAZY_UNCOND, H_LAZY_COND, H_LAZY_NO };
enum rte_tch_hash_variants var[] = {H_BLOOM, H_HORTON};


static void insert(struct rte_tch_hash* h, int nops){
	int i;
	hash_data_t data;
	data.mm = _mm_set1_epi16(16);
	hash_key_t key;
	int err_count=0;

	for(i=0;i<nops;i++){
			key.a = i;
			key.b = 0;
			int r = rte_tch_hash_add_key_data(v,h, key, data, 16, 0);
			if(r != RHL_FOUND_UPDATED) err_count++;
	}
	total_inserts=nops;
	if(err_count != 0) printf("%d failed insertions\n", err_count);
}

static void hash(__rte_unused struct rte_tch_hash* h, int nops){
	int i;
	hash_key_t key;

    for(i=0;i<nops;i+=1){
		key.a = i;
		key.b = 0;
    	// Use a volatile structure to avoid gcc optimizing out call.
    	hv[rte_lcore_id()].v=dcrc_hash_m128(key);
    }
}

static void lookup_ind(struct rte_tch_hash* h, int nops){
	// Perform N_lookups
	int j,i=0;
	int negative=0;
	hash_data_t data;
	data.mm = _mm_set1_epi16(16);
	hash_key_t key;


	int successes = 0;
	for(j=0;j<nops;j++){
		key.a = random_max(total_inserts,&rand_s);
		key.b = ((negative & (inv_Failure - 1)) == (inv_Failure - 1));
		i++;
		if(i >= hashmap_size){
			i=0;
		}
		successes += (rte_tch_hash_lookup_data(v,h, key, &data, 0) == RHL_FOUND_NOTUPDATED );
	}
	if(successes < nops)  printf("%.2f successful lookup rate (ind) - %d/%d\n", ((double)successes)/(double)nops,successes,nops);

}

static inline void lookup_batch(struct rte_tch_hash * h, int nops, int batch, int neg){
	// Perform N_lookups
	int j,k,i=0;
	int successes=0;
	hash_data_t data[batch];
	hash_key_t key[batch];

	for(j=0;j<nops;j+=batch){
		for(k=0;k<batch;k++){
			key[k].a = random_max(total_inserts,&rand_s);
			key[k].b = neg;
			i++;
			if(i >= hashmap_size){
				i=0;
			}
		}
		uint64_t hits=0;
		rte_tch_hash_lookup_bulk_data(v,h, key, batch, &hits, data, 0);
		successes += __builtin_popcount(hits);
	}
	if((!neg) && successes < nops)  printf("%.2f successful lookup rate (batch) - %d/%d\n", ((double)successes)/(double)nops,successes,nops);
	if((neg) && successes > 0)  printf("%.2f successful lookup rate (batch) - %d/%d\n", ((double)successes)/(double)nops,successes,nops);

}


//static void lookup_batch1(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,1, 0); }
//static void lookup_batch2(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,2, 0); }
//static void lookup_batch4(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,4, 0); }
//static void lookup_batch8(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,8, 0); }
//static void lookup_batch12(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,12, 0); }
//static void lookup_batch16(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,16, 0); }
//static void lookup_batch24(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,24, 0); }
__rte_unused static void lookup_batch32(struct rte_tch_hash* h, int nops){ lookup_batch(h, nops,32, 0); }
//static void lookup_batch48(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,48, 0); }
//static void lookup_batch64(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,64, 0); }


//static void lookup_batch1_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,1, 1); }
//static void lookup_batch2_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,2, 1); }
//static void lookup_batch4_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,4, 1); }
//static void lookup_batch8_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,8, 1); }
//static void lookup_batch12_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,12, 1); }
//static void lookup_batch16_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,16, 1); }
//static void lookup_batch24_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,24, 1); }
__rte_unused static void lookup_batch32_neg(struct rte_tch_hash* h, int nops){ lookup_batch(h, nops,32, 1); }
//static void lookup_batch48_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,48, 1); }
//static void lookup_batch64_neg(struct rte_hash_lazy* h, int nops){ lookup_batch(h, nops,64, 1); }






typedef void (*tBenchFunc)(struct rte_tch_hash * h , int nops);

static int main_loop(void * args){
	unsigned lcore_id= rte_lcore_id();
	uint64_t startq = rte_get_tsc_cycles();
	((tBenchFunc)args)(h[lcore_id],nops);
	uint64_t endq = rte_get_tsc_cycles();
	double diff_tsc = endq-startq;

	cycles[lcore_id] = diff_tsc/(double)nops;
	rate[lcore_id] = ((double)nops)/(double)(diff_tsc/rte_get_tsc_hz());
	return 0;
}

static void call_bench(tBenchFunc f, const char* title, int ncores, int n){
	nops=n;
	int launched_cores = 0;
	int lcore_id;
	/* Reset stats to 0 */
	RTE_LCORE_FOREACH(lcore_id) {
		cycles[lcore_id] = 0;
		rate[lcore_id] = 0;
	}

	/* launch on every slave lcore */
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if(launched_cores++ >= ncores) break;
		rte_eal_remote_launch(main_loop, f, lcore_id);

	}

	/* If needed launch on master */
	if(launched_cores < ncores) main_loop(f);

	/* Wait for slaves */
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			rte_exit(EXIT_FAILURE, "One thread failed\n");
		}
	}

	double rate_avg=0;
	double cycles_avg=0;
	RTE_LCORE_FOREACH(lcore_id) {
		cycles_avg += cycles[lcore_id];
		rate_avg += rate[lcore_id];
	}
	cycles_avg /= ncores;
	rate_avg /= ncores;

	printf("%s %d cores  - %.2f cycles/operation , %.2f M operation/second\n", title, ncores, (double)cycles_avg, (double)rate_avg/1000000.0);
}

int
main(int argc, char **argv)
{
	int ret;
	unsigned i;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	unsigned lcore_id;

	int capacity=0;
	float filling_ratio=0;

	if(argc != 3){
		rte_exit(EXIT_FAILURE, "Wrong number of arguments: hash_perf EAL_ARGS -- capacity filling_ratio \n");
	}
	sscanf(argv[1],"%d",&capacity);
	sscanf(argv[2],"%f",&filling_ratio);

	if(capacity == 0 || filling_ratio == 0){
		rte_exit(EXIT_FAILURE, "Wrong arguments: hash_perf EAL_ARGS -- capacity filling_ratio \n");
	}

	rte_tch_rand_init(&rand_s);

	for(i=0;i<sizeof(var)/sizeof(enum rte_tch_hash_variants);i++){
		v=var[i];
		hashmap_size = ((double)capacity)*filling_ratio;

		// Do init
		RTE_LCORE_FOREACH(lcore_id) {
			char name[50];
			snprintf(name,50,"HASH_%u",lcore_id);
			struct rte_tch_hash_parameters nat_hash_params = {
					.entries = capacity,
					.socket_id = rte_lcore_to_socket_id(lcore_id)
			};

			h[lcore_id] = rte_tch_hash_create(v,&nat_hash_params);
			if (h[lcore_id] == NULL){
				printf("Failed to init hashtable for lcore_id %d on socket_id %d - Check that CPU and memory are on the same socket\n", lcore_id, rte_lcore_to_socket_id(lcore_id));
			}
		}

		printf("Testing for %s\n", rte_tch_hash_str(v));
		unsigned cores;
		for(cores=1;cores<=rte_lcore_count();cores++){
			// Perform tests...
			call_bench(hash, "hashing", cores, 10000000);
			call_bench(insert, "insert", cores, hashmap_size);
			call_bench(lookup_ind, "lookup", cores, 10000000);
			//call_bench(lookup_batch1, "lookup_batch1", cores, 100000000);
			//call_bench(lookup_batch2, "lookup_batch2", cores, 100000000);
			//call_bench(lookup_batch4, "lookup_batch4", cores, 100000000);
			//call_bench(lookup_batch8, "lookup_batch8", cores, 100000000);
			//call_bench(lookup_batch12, "lookup_batch12", cores, 100000000);
			//call_bench(lookup_batch16, "lookup_batch16", cores, 1000000000);
			//call_bench(lookup_batch24, "lookup_batch24", cores, 1000000000);
			call_bench(lookup_batch32, "lookup_batch32", cores, 500000000);
			//call_bench(lookup_batch48, "lookup_batch48", cores, 1000000000);
			//call_bench(lookup_batch64, "lookup_batch64", cores, 1000000000);
			//call_bench(lookup_batch1_neg, "lookup_batch1_neg", cores, 100000000);
			//call_bench(lookup_batch2_neg, "lookup_batch2_neg", cores, 100000000);
			//call_bench(lookup_batch4, "lookup_batch4", cores, 100000000);
			//call_bench(lookup_batch8_neg, "lookup_batch8_neg", cores, 100000000);
			//call_bench(lookup_batch12, "lookup_batch12", cores, 100000000);
			//call_bench(lookup_batch16_neg, "lookup_batch16_neg", cores, 1000000000);
			//call_bench(lookup_batch24, "lookup_batch24", cores, 1000000000);
			call_bench(lookup_batch32_neg, "lookup_batch32_neg", cores, 500000000);
			//call_bench(lookup_batch48_neg, "lookup_batch48_neg", cores, 1000000000);
			//call_bench(lookup_batch64_neg, "lookup_batch64_neg", cores, 1000000000);
		}

		// Do multiple (identical) round of insertion
		call_bench(insert,"insert-redo",cores, hashmap_size);
		call_bench(insert,"insert-redo",cores, hashmap_size);
		call_bench(insert,"insert-redo",cores, hashmap_size);

		// Check integrity
		RTE_LCORE_FOREACH(lcore_id) {
			printf("Checking for lcore_id %d\n",lcore_id);
			rte_tch_hash_print_stats(v,h[lcore_id], 0);
			rte_tch_hash_check_integrity(v,h[lcore_id], 0);
		}

		// Reset hash
		RTE_LCORE_FOREACH(lcore_id) {
			rte_tch_hash_reset(v,h[lcore_id]);
		}


		// Reset hash
		RTE_LCORE_FOREACH(lcore_id) {
			rte_tch_hash_free(v,h[lcore_id]);
		}


	}
	return 0;
}

