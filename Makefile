all: __apps

__apps : __hash_perf __hash_stats __hash_cpp

__libs:
	make -f lib/librte_tch_hash/Makefile S=lib/librte_tch_hash O=build
	
__hash_perf: __libs
	make -f apps/hash-perf/Makefile S=apps/hash-perf O=build EXTRA_CFLAGS="-I$(CURDIR)/build/include -L$(CURDIR)/build/lib" EXTRA_CPPFLAGS=-I$(CURDIR)/build/include EXTRA_LDFLAGS="-L$(CURDIR)/build/lib -lrte_tch_hash -lm" V=1 

__hash_stats: __libs
	make -f apps/hash-stats/Makefile S=apps/hash-stats O=build EXTRA_CFLAGS="-I$(CURDIR)/build/include -L$(CURDIR)/build/lib" EXTRA_CPPFLAGS=-I$(CURDIR)/build/include EXTRA_LDFLAGS="-L$(CURDIR)/build/lib -lrte_tch_hash -lm" V=1 

__hash_cpp: __libs
	make -f apps/hash-cpp/Makefile S=apps/hash-cpp O=build EXTRA_CXXFLAGS="-I$(CURDIR)/build/include -L$(CURDIR)/build/lib" EXTRA_CPPFLAGS=-I$(CURDIR)/build/include EXTRA_LDFLAGS="-L$(CURDIR)/build/lib -lrte_tch_hash -lm" V=1 

clean:
	rm -rf build
