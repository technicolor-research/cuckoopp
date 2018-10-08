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


enum rte_tch_hash_variants var[] = {H_BLOOM, H_HORTON/*, H_V1604*/};
int sizes[] = {/*524288,  4194304, 16777216,*/ 33554432}; 
double filling_ratios[] = {0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.75};/*,0.8,0.85,0.875,0.9,0.91,0.92,0.93,0.94,0.95,0.96,0.97,0.975,0.98,0.985,0.99,0.995}; */
unsigned count=1;
struct rte_tch_rand_state rand_s;

static void insert(enum rte_tch_hash_variants hvar, struct rte_tch_hash* h, int nops){
	int i;
	hash_data_t data;
	data.mm = _mm_set1_epi16(16);
	hash_key_t key;
	int err_count=0;
	for(i=0;i<nops;i++){
			key.a = random_max(1 << 31,&rand_s);
			key.b = random_max(1 << 31,&rand_s);
			int r = rte_tch_hash_add_key_data(hvar,h, key, data, 16, 0);
			if(r != RHL_FOUND_UPDATED) err_count++;
	}
	if(err_count != 0) printf("%d failed insertions (%f %%)\n", err_count, ((double)err_count)*100.0/(double)nops);
}

static double get_stats(enum rte_tch_hash_variants hvar, int hsize, double hratio){
	double s;
	struct rte_tch_hash * h;
	int insertions = hsize*hratio;
	int lcore_id = rte_get_master_lcore();
	struct rte_tch_hash_parameters nat_hash_params = {
			.entries = hsize,
			.socket_id = rte_lcore_to_socket_id(lcore_id)
	};

	h = rte_tch_hash_create(hvar,&nat_hash_params);
	if (h == NULL){
		printf("Failed to init hashtable for lcore_id %d on socket_id %d - Check that CPU and memory are on the same socket\n", lcore_id, rte_lcore_to_socket_id(lcore_id));
	}

	insert(hvar,h,insertions);

	//rte_tch_hash_check_integrity(hvar,h,0);

	s=rte_tch_hash_stats_secondary(hvar,h,0);

	rte_tch_hash_free(hvar,h);

	return s;
}

int
main(int argc, char **argv)
{
	int ret;
	unsigned i,j,k,l;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	rte_tch_rand_init(&rand_s);

	printf("name\tslots_per_bucket\thash_table_capacity\tfilling_ratio\tstats_in_secondary\n");
	for(i=0;i<sizeof(var)/sizeof(enum rte_tch_hash_variants);i++){
		for(j=0;j<sizeof(sizes)/sizeof(int);j++){
			for(k=0;k<sizeof(filling_ratios)/sizeof(double);k++){

				enum rte_tch_hash_variants hvar=var[i];
				int hsize=sizes[j];
				double hratio=filling_ratios[k];

				if(rte_tch_hash_slots_per_bucket(hvar) == 4 && hratio > 0.92) break;

				double mean_secondary = 0;
				for(l=0;l<count;l++){
					mean_secondary += get_stats(hvar, hsize, hratio);
				}
				mean_secondary = mean_secondary / (double)count;
				printf("%s\t%d\t%d\t%f\t%f\n",rte_tch_hash_str(hvar),rte_tch_hash_slots_per_bucket(hvar),hsize,hratio,mean_secondary);
			}
		}
	}
	return 0;
}

