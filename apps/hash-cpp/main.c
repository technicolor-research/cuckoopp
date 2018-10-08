/**
 * Copyright (c) 2018 - Present – Thomson Licensing, SAS
 * All rights reserved.
 *
 * This source code is licensed under the Clear BSD license found in the
 * LICENSE.md file in the root directory of this source tree.
 */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <sys/types.h>
#include <getopt.h>

#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_hash64.h>

extern "C" {
	#include <rte_tch_hash.h>
}

/* Pattern sizes */
const int batch_size = 32;
const int pattern_prime = 6131;
const unsigned pattern_size = batch_size * pattern_prime;

/* Keys generation and management */
struct key_list {

	key_list() :
			keys(nullptr, rte_free), key_count(0) {
	}
	;

	key_list(unsigned key_count_, int socked_id) :
			keys(
					reinterpret_cast<hash_key_t*>(rte_malloc_socket(nullptr,
							key_count_ * sizeof(hash_key_t),
							RTE_CACHE_LINE_SIZE, socked_id)), rte_free), key_count(
					0) {
		if(keys.get() == nullptr) {
			fprintf(stderr, "Could not allocate %lu bytes on socket %d for key list\n",
					key_count_ * sizeof(hash_key_t), socked_id);
			exit(1);
		}
	}
	;

	std::unique_ptr<hash_key_t[], decltype(&rte_free)> keys;
	unsigned key_count;
};

void keys_random_fill(hash_key_t keys[], unsigned key_count,
		std::mt19937_64& rnd, float non_zero_percent = 1.0f) {
	unsigned key_i;
	unsigned non_zero_count = key_count * non_zero_percent;
	for (key_i = 0; key_i < non_zero_count; ++key_i) {
		keys[key_i].a = rnd();
		keys[key_i].b = rnd();
	}
	for (; key_i < key_count; ++key_i) {
		keys[key_i].a = 0;
		keys[key_i].b = 0;
	}
	if(non_zero_percent != 1.0f) {
		std::shuffle(keys, keys + key_count, rnd);
	}
}

/* Hash table operations */
template<rte_tch_hash_variants Variant>
void hashtable_insert(rte_tch_hash& hash,
		hash_key_t keys[], unsigned key_count,
		__rte_unused hash_key_t patterns[], __rte_unused unsigned op_count, float& success_rate) {

	hash_data_t data;
	data.mm = _mm_set1_epi16(16);

	for (unsigned key_i = 0; key_i < key_count; ++key_i) {
		rte_tch_hash_add_key_data(Variant, &hash, keys[key_i], data, 16, 0);
	}
	success_rate = 1.0f;
}

typedef decltype(&hashtable_insert<H_V1604>) hashtable_operation;

volatile uint64_t global_hash = 0;

template<rte_tch_hash_variants Variant>
void hashtable_hash(__rte_unused rte_tch_hash& hash,
		hash_key_t keys[], unsigned key_count,
		__rte_unused hash_key_t patterns[],
		unsigned op_count, float& success_rate) {

	uint64_t hash_value = 0;
	unsigned key_i = 0;
	for(unsigned op_i = 0; op_i < op_count; ++op_i) {
		key_i++;
		if (key_i >= key_count) {
			key_i = 0;
		}
		hash_value += dcrc_hash_m128(keys[key_i]);
	}
	global_hash = hash_value;
	success_rate = 1.0f;
}

template<rte_tch_hash_variants Variant>
void hashtable_lookup(rte_tch_hash& hash, hash_key_t keys[], unsigned key_count,
__rte_unused hash_key_t patterns[], unsigned op_count, float& success_rate) {

	hash_data_t data;

	unsigned key_i = 0;
	unsigned patt_i = 0;
	unsigned success_op = 0;
	hash_key_t xored_key;

	for (unsigned op_i = 0; op_i < op_count; ++op_i) {
		key_i++;
		if (key_i >= key_count) {
			key_i = 0;
		}

		patt_i++;
		if (patt_i >= pattern_size) {
			patt_i = 0;
		}

		xored_key.mm = _mm_xor_si128(keys[key_i].mm, patterns[patt_i].mm);

		success_op += (rte_tch_hash_lookup_data(Variant, &hash, xored_key,
				&data, 0) >= 0);
	}
	success_rate = static_cast<float>(success_op) / op_count;
}

template<rte_tch_hash_variants Variant, int BatchSize>
void hashtable_lookup_batch(rte_tch_hash& hash, hash_key_t keys[],
		unsigned key_count, hash_key_t patterns[], unsigned op_count,
		float& success_rate) {

	hash_data_t data[BatchSize];
	hash_key_t xored_keys[BatchSize];

	unsigned batch_i = 0;
	unsigned patt_i = 0;
	unsigned success_op = 0;
	for (unsigned op_i = 0; op_i < op_count; op_i += BatchSize) {

		batch_i += BatchSize;
		if (batch_i + BatchSize > key_count) {
			batch_i = 0;
		}

		patt_i += BatchSize;
		if (patt_i + BatchSize > pattern_size) {
			patt_i = 0;
		}

		const hash_key_t* keys_batch = keys + batch_i;
		const hash_key_t* patt_batch = patterns + patt_i;

		for (int i = 0; i < BatchSize; ++i) {
			xored_keys[i].mm = _mm_xor_si128(keys_batch[i].mm, patt_batch[i].mm);
		}

		uint64_t hits;
		rte_tch_hash_lookup_bulk_data(Variant, &hash, xored_keys, BatchSize,
				&hits, data, 0);
		success_op += __builtin_popcount(hits);
	}
	success_rate = static_cast<float>(success_op) / op_count;
}


/* Benchmark functions */

struct core_loop_args {
	float op_cycles;
	float op_rate;
	float success_rate;
	hashtable_operation op_func;
	const key_list* list;
	const key_list* patterns;
	rte_tch_hash* hash;
	int key_count;
	int op_count;
};

int core_loop(void* args) {
	core_loop_args* core_args = static_cast<core_loop_args*>(args);
	uint64_t startq = rte_get_tsc_cycles();
	core_args->op_func(*(core_args->hash), core_args->list->keys.get(),
			core_args->key_count, core_args->patterns->keys.get(),
			core_args->op_count, core_args->success_rate);
	uint64_t endq = rte_get_tsc_cycles();
	float diff_tsc = endq - startq;
	core_args->op_cycles = diff_tsc / static_cast<float>(core_args->op_count);
	core_args->op_rate = static_cast<float>(core_args->op_count)
			/ (diff_tsc / rte_get_tsc_hz());
	return 0;
}

struct bench_desc {
	const char* implem_name;
	const char* op_name;
	float load_factor;
	unsigned capacity;
};

static void run_bench_multicore(hashtable_operation op_func,
		const key_list list[], const key_list patterns[], rte_tch_hash* hash[],
		const bench_desc& desc, int core_count,
		unsigned key_count, unsigned op_count, std::ostream& outstream) {

	std::unique_ptr<core_loop_args[]> core_args(new core_loop_args[core_count]);

	// Prepare arguments
	for (int core_i = 0; core_i < core_count; ++core_i) {
		core_args[core_i].op_cycles = 0;
		core_args[core_i].op_rate = 0;
		core_args[core_i].success_rate = 0;
		core_args[core_i].op_func = op_func;
		core_args[core_i].list = list + core_i;
		core_args[core_i].patterns = patterns + core_i;
		core_args[core_i].hash = hash[core_i];
		core_args[core_i].key_count = key_count;
		core_args[core_i].op_count = op_count;
	}

	// Launch on every slave core
	int core_i = 0;
	const int skip_master = 1;
	const int wrap = 0;
	int lcore_id = rte_get_next_lcore(-1, skip_master, wrap);
	for (; core_i < core_count && lcore_id != RTE_MAX_LCORE; ++core_i) {
		rte_eal_remote_launch(core_loop,
				static_cast<void*>(core_args.get() + core_i), lcore_id);
		lcore_id = rte_get_next_lcore(lcore_id, skip_master, wrap);
	}


	// If needed, lauch on master core
	if(core_i == core_count - 2) {
		core_loop(static_cast<void*>(core_args.get() + core_count - 1));
	}

	// Wait for slaves
	core_i = 0;
	lcore_id = rte_get_next_lcore(-1, skip_master, wrap);
	for(; core_i < core_count && lcore_id != RTE_MAX_LCORE; ++core_i) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			rte_exit(EXIT_FAILURE, "One thread failed\n");
		}
		lcore_id = rte_get_next_lcore(lcore_id, skip_master, wrap);
	}

	// Compute statistics
	double rate_avg = 0;
	double cycles_avg = 0;
	double sucess_avg = 0;

	for(core_i = 0; core_i < core_count; ++core_i) {
		cycles_avg += core_args[core_i].op_cycles;
		rate_avg += core_args[core_i].op_rate;
		sucess_avg += core_args[core_i].success_rate;
	}
	cycles_avg /= core_count;
	rate_avg /= core_count;
	sucess_avg /= core_count;

	// Display statistics
	outstream << desc.implem_name << "," << desc.op_name << "," << desc.capacity
			<< "," << desc.load_factor << "," << core_count << "," << sucess_avg
			<< "," << cycles_avg << "," << rate_avg << "\n";
/*	printf("%s,%s,%d,%f,%f,%f\n", implem_name, op_name, core_count, sucess_avg,
			cycles_avg, rate_avg);*/
}

struct cmdargs {
	rte_tch_hash_variants implementation;
	std::vector<int> capacities;
	std::vector<float> load_factors;
	std::vector<int> core_counts;
	std::vector<float> unsucessful_rate;
	float max_unsucessful_rate;
	std::string outfile;
};

const unsigned hash_op_count = 10000000;
const unsigned lookup_op_count = 10000000;
const unsigned lookup_batch_op_count = 100000000;

template<rte_tch_hash_variants Variant>
static void make_hash(rte_tch_hash* hash[], unsigned capacity, int core_i, int lcore_id) {
	rte_tch_hash_parameters nat_hash_params = {
			.entries = capacity,
			.socket_id = static_cast<int>(rte_lcore_to_socket_id(lcore_id))
	};
	hash[core_i] = rte_tch_hash_create(Variant, &nat_hash_params);
	if (hash == nullptr) {
		fprintf(stderr,
				"Failed to init hashtable for lcore_id %d on socket_id %d - Check that CPU and memory are on the same socket\n",
				lcore_id, rte_lcore_to_socket_id(lcore_id));
	}
}

template<rte_tch_hash_variants Variant>
void benchmark_for_size(const cmdargs& args,
		key_list list[], key_list patterns[],
		int max_cores,
		std::mt19937_64& rnd,
		unsigned capacity, float load_factor,
		std::ostream& outstream) {

	unsigned size = capacity * load_factor;


	fprintf(stderr, "-- Benchmark: capacity=%d load=%f size=%d\n", capacity, load_factor, size);


	for(int core_count: args.core_counts) {

		key_list* shifted_patterns = patterns;

		// Init. hash tables
		std::unique_ptr<rte_tch_hash*[]> hash(new rte_tch_hash*[core_count]);

		int core_i = 0;
		const int skip_master = 1;
		const int wrap = 0;
		int lcore_id = rte_get_next_lcore(-1, skip_master, wrap);
		for(; core_i < core_count && lcore_id != RTE_MAX_LCORE; ++core_i) {
			make_hash<Variant>(hash.get(), capacity, core_i, lcore_id);
			lcore_id = rte_get_next_lcore(lcore_id, skip_master, 0);
		}

		if (core_i < core_count - 2) {
			fprintf(stderr,
					"Not enough available cores to run benchmarks on %d cores\n",
					core_count);
			exit(1);
		} else if (core_i == core_count - 2) {
			++core_i;
			lcore_id = rte_get_master_lcore();
			make_hash<Variant>(hash.get(), capacity, core_i, lcore_id);
		}

		bench_desc desc;
		desc.implem_name = rte_tch_hash_str(args.implementation);
		desc.load_factor = load_factor;
		desc.capacity = capacity;

		// Do benchmarks
		desc.op_name = "hash";
		run_bench_multicore(hashtable_hash<Variant>, list, shifted_patterns,
				hash.get(), desc, core_count, size, hash_op_count, outstream);
		desc.op_name = "insert";
		run_bench_multicore(hashtable_insert<Variant>, list, shifted_patterns,
				hash.get(), desc, core_count, size, size, outstream);

		// Shuffle key lists for lookups
		for(core_i = 0; core_i < core_count; ++core_i) {
			//fprintf(stderr, "Shuffle list core %d/%d %p\n", core_i, core_count, list[core_i].keys.get());
			std::shuffle(list[core_i].keys.get(),
					list[core_i].keys.get() + size, rnd);
		}

		for (float rate : args.unsucessful_rate) {
			fprintf(stderr, "Invalid rate: %f %d\n", rate, size);
			desc.op_name = "lookup";
			run_bench_multicore(hashtable_lookup<Variant>, list,
					shifted_patterns, hash.get(), desc, core_count, size,
					lookup_op_count, outstream);
			desc.op_name = "lookup_batch_32";
			run_bench_multicore(hashtable_lookup_batch<Variant, 32>, list,
					shifted_patterns, hash.get(), desc, core_count, size,
					lookup_batch_op_count, outstream);
			shifted_patterns += max_cores;
		}

		// Check integrity
		for(core_i = 0; core_i < core_count; ++core_i) {
			fprintf(stderr, "Checking for lcore_id %d\n", core_i);
			//rte_tch_hash_print_stats(Variant, hash[core_i], 0);
			rte_tch_hash_check_integrity(Variant, hash[core_i], 0);
		}

		// Reset hash
		for(core_i = 0; core_i < core_count; ++core_i) {
			rte_tch_hash_reset(Variant,hash[core_i]);
		}

		// Free structures
		for(core_i = 0; core_i < core_count; ++core_i) {
			rte_tch_hash_free(Variant,hash[core_i]);
		}
	}
}

template<rte_tch_hash_variants Variant>
void benchmark(const cmdargs& args, std::ostream& outstream) {

	const int max_capacity = *std::max_element(args.capacities.begin(),
			args.capacities.end());
	const float max_load_factor = *std::max_element(args.load_factors.begin(),
			args.load_factors.end());
	const int max_size = max_capacity * max_load_factor;
	const int max_cores = *std::max_element(args.core_counts.begin(),
			args.core_counts.end());

	fprintf(stderr, "max_capacity=%d, max_load_factor=%f, max_size=%d\n",
			max_capacity, max_load_factor, max_size);

	std::mt19937_64 rnd;

	// Generate key lists
	const int skip_master = 1;
	const int wrap = 0;
	int lcore_id = rte_get_next_lcore(-1, skip_master, wrap);
	std::unique_ptr<key_list[]> lists(new key_list[max_cores]);
	for(int core_i = 0; core_i < max_cores && lcore_id != RTE_MAX_LCORE; ++core_i) {
		lists[core_i] = key_list(max_size, rte_lcore_to_socket_id(lcore_id));
		//fprintf(stderr, "List %d %p\n", max_size, lists[core_i].keys.get());
		keys_random_fill(lists[core_i].keys.get(), max_size, rnd);
		lcore_id = rte_get_next_lcore(lcore_id, skip_master, wrap);
	}

	// Generate patterns lists
	int un_count = args.unsucessful_rate.size();
	std::unique_ptr<key_list[]> patterns(new key_list[max_cores * un_count]);
	for (int un_i = 0; un_i < un_count; ++un_i) {
		lcore_id = rte_get_next_lcore(-1, skip_master, wrap);
		for(int core_i = 0; core_i < max_cores && lcore_id != RTE_MAX_LCORE; ++core_i) {
			patterns[un_i * max_cores + core_i] = key_list(pattern_size,
					rte_lcore_to_socket_id(lcore_id));
			hash_key_t* patts = patterns[un_i * max_cores + core_i].keys.get();
			keys_random_fill(patts, pattern_size, rnd,
					args.unsucessful_rate[un_i]);
			lcore_id = rte_get_next_lcore(lcore_id, skip_master, wrap);
		}
	}

	for (int cap : args.capacities) {
		for (float load_f : args.load_factors) {
			benchmark_for_size<Variant>(args, lists.get(), patterns.get(),
					max_cores, rnd, cap, load_f, outstream);
		}
	}
}

void parse_float_list(char* str, std::vector<float>& list, const float min,
		const float max, const char* param_name, const char* sep = ",") {
	char* tok = strtok(str, sep);
	list.push_back(std::stof(tok));
	while ((tok = strtok(nullptr, sep)) != nullptr) {
		const float val = std::stof(tok);
		if (val > max || val < min) {
			fprintf(stderr, "Invalid %s: %f (%f-%f)\n", param_name, val, min,
					max);
			exit(1);
		}
		list.push_back(val);
	}
}

void parse_int_list(char* str, std::vector<int>& list, const int min,
		const int max, const char* param_name, const char* sep = ",") {
	char* tok = strtok(str, sep);
	list.push_back(std::stoi(tok));
	while ((tok = strtok(nullptr, sep)) != nullptr) {
		const int val = std::stoi(tok);
		if (val > max || val < min) {
			fprintf(stderr, "Invalid %s: %d (%d-%d)\n", param_name, val, min,
					max);
			exit(1);
		}
		list.push_back(val);
	}
}

rte_tch_hash_variants parse_implementation(const char* implem) {
	unsigned variants_count = sizeof(variants_names)
			/ sizeof(variants_names[0]);
	for (unsigned i = 0; i < variants_count; ++i) {
		if (!strcmp(variants_names[i], implem)) {
			return static_cast<rte_tch_hash_variants>(i);
		}
	}
	fprintf(stderr, "Invalid implementation: %s\n", implem);
	exit(1);
	return static_cast<rte_tch_hash_variants>(0);
}

void usage() {
	fprintf(stderr,
			"Usage: hash-cpp [-c CAPACITY_LIST] [-l LOAD_FACTOR_LIST] "
			"[-t CORE_COUNT_LIST] [-i INVALID_LOOKUP_RATE_LIST] IMPLEMENTATION [OUT_FILE]\n");
}

void parse_args(cmdargs& args, int argc, char* argv[]) {

	// Iterate over CLI arguments
	int opt;
	optind = 1;
	while ((opt = getopt(argc, argv, "c:l:t:i:")) != -1) {
		switch (opt) {
		case 'c':
			parse_int_list(optarg, args.capacities, 1, 1000000000, "capacity");
			break;
		case 'l':
			parse_float_list(optarg, args.load_factors, 0.01, 1.0,
					"load factor");
			break;
		case 't':
			parse_int_list(optarg, args.core_counts, 1, 1000, "core count");
			break;
		case 'i':
			parse_float_list(optarg, args.unsucessful_rate, 0.0, 1.0,
					"lookup rate");
			break;
		default:
			usage();
			exit(1);
		}
	}

	// Default value for arguments
	if (args.capacities.empty()) {
		args.capacities.push_back(1000000);
	}

	if (args.load_factors.empty()) {
		args.load_factors.push_back(0.5f);
	}

	if (args.core_counts.empty()) {
		args.core_counts.push_back(1);
	}

	if (args.unsucessful_rate.empty()) {
		args.unsucessful_rate.push_back(0.0f);
	}

	if (argc - optind < 1 || argc - optind > 2) {
		usage();
		exit(1);
	}

	args.implementation = parse_implementation(argv[optind]);

	// Open output stream
	if(argc - optind == 2) {
		args.outfile = argv[optind + 1];
	} else {
		args.outfile = "-";
	}
	fprintf(stderr, "Output file: %s\n", args.outfile.c_str());
}

const char* header_str = "implementation,operation,capacity,load_factor,core_count,success_rate,op_cycles,op_rate";

int main(int argc, char* argv[]) {

	// Check if root
	if(getuid() != 0) {
		fprintf(stderr, "Please run this program as root\n");
		exit(1);
	}

	// Init EAL
	int ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	}
	argc -= ret;
	argv += ret;

	cmdargs args;
	parse_args(args, argc, argv);

	std::ostream* outstream;
	std::ofstream outfile;

	if(args.outfile == "-") {
		outstream = &(std::cout);
		fprintf(stderr, "Opened stdout\n");
	} else {
		outfile.open(args.outfile, std::ofstream::out);
		fprintf(stderr, "Opened %s\n", args.outfile.c_str());
		outstream = &(outfile);
	}

	switch(args.implementation) {
	case H_V1604:
		(*outstream) << header_str << "\n";
		benchmark<H_V1604>(args, *outstream);
		break;
	case H_V1702:
		(*outstream) << header_str << "\n";
		benchmark<H_V1702>(args, *outstream);
		break;
	case H_LAZY_BLOOM:
		(*outstream) << header_str << "\n";
		benchmark<H_LAZY_BLOOM>(args, *outstream);
		break;
	case H_LAZY_COND:
		(*outstream) << header_str << "\n";
		benchmark<H_LAZY_COND>(args, *outstream);
		break;
	case H_LAZY_UNCOND:
		(*outstream) << header_str << "\n";
		benchmark<H_LAZY_UNCOND>(args, *outstream);
		break;
	case H_LAZY_NO:
		(*outstream) << header_str << "\n";
		benchmark<H_LAZY_NO>(args, *outstream);
		break;
	case H_HORTON:
		(*outstream) << header_str << "\n";
		benchmark<H_HORTON>(args, *outstream);
		break;
	case H_BLOOM:
		(*outstream) << header_str << "\n";
		benchmark<H_BLOOM>(args, *outstream);
		break;
	case H_COND:
		(*outstream) << header_str << "\n";
		benchmark<H_COND>(args, *outstream);
		break;
	case H_UNCOND:
		(*outstream) << header_str << "\n";
		benchmark<H_UNCOND>(args, *outstream);
		break;
	default:
		fprintf(stderr, "Unsupported implementation: %d\n", args.implementation);
		exit(1);
	}
	return 0;
}
