CXX ?= g++
UNAME_S ?= $(shell uname -s)
ARCH ?= $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null || echo /opt/homebrew/opt/openssl@3)
OPENSSL_INCLUDE_DIR ?= $(OPENSSL_PREFIX)/include
OPENSSL_LIB_DIR ?= $(OPENSSL_PREFIX)/lib
SYSTEM_INCLUDE_DIRS ?= /opt/homebrew/include
else
OPENSSL_PREFIX ?= /usr
OPENSSL_INCLUDE_DIR ?= $(firstword $(wildcard $(OPENSSL_PREFIX)/include /usr/local/include /usr/include))
OPENSSL_LIB_DIR ?= $(firstword $(wildcard $(OPENSSL_PREFIX)/lib/x86_64-linux-gnu $(OPENSSL_PREFIX)/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib))
SYSTEM_INCLUDE_DIRS ?= /usr/local/include /usr/include
endif

SEAL_ROOT ?= $(CURDIR)/SEAL
SEAL_BUILD ?= $(SEAL_ROOT)/build
SEAL_MSGSL_INCLUDE_DIR ?= $(SEAL_BUILD)/thirdparty/msgsl-src/include
SEAL_INCLUDE_DIRS ?= $(SEAL_ROOT)/native/src $(SEAL_BUILD)/native/src $(SEAL_MSGSL_INCLUDE_DIR)
SEAL_LIB_FILE ?= $(firstword $(wildcard \
	$(SEAL_BUILD)/lib/libseal*.a \
	$(SEAL_BUILD)/lib/libseal*.dylib \
	$(SEAL_BUILD)/lib/libseal*.so))
SEAL_LDLIBS ?= $(SEAL_LIB_FILE)
SEAL_CXXFLAGS ?=
USE_BATCHPIR ?= 1
SIMD_FLAGS ?=
ifeq ($(ARCH),x86_64)
SIMD_FLAGS += -msse2 -msse -msse4.1 -maes
endif
SHARED_OBJS = Utils/utils.o Utils/AES.o
PIR_CHECKLIST_OBJS = pir/checklist/psetggm/pset_ggm.o \
	pir/checklist/psetggm/xor.o \
	pir/checklist/psetggm/answer.o
OPRF_OBJS = oprf/droidcrypto_adapter.o
TEST_FUZZYPSI_PARTS = $(wildcard test_fuzzypsi_parts/*.inc.cpp) \
	$(wildcard oprf/*.inc.cpp) \
	$(wildcard pir/*.inc.cpp) \
	$(wildcard pir/checklist/*.inc.cpp) \
	$(wildcard pir/batchpir/*.inc.cpp)
BATCHPIR_DIR = pir/batchpir
VECTORIZED_BATCHPIR_DIR = $(BATCHPIR_DIR)/vectorized_batchpir
BATCHPIR_ADAPTER_DEPS = pir/pir_row_reader.h \
	$(BATCHPIR_DIR)/batchpir_row_reader.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/batchpirclient.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/batchpirserver.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/batchpirparams.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/client.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/server.h \
	$(VECTORIZED_BATCHPIR_DIR)/header/pirparams.h \
	$(VECTORIZED_BATCHPIR_DIR)/src/batchpirclient.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/batchpirserver.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/batchpirparams.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/client.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/server.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/pirparams.cpp \
	$(VECTORIZED_BATCHPIR_DIR)/src/utils.h
THREAD_FLAGS = -pthread
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra $(THREAD_FLAGS) $(SIMD_FLAGS) \
	-I. \
	-Iinclude \
	$(addprefix -I,$(SYSTEM_INCLUDE_DIRS)) \
	-IUtils \
	$(if $(OPENSSL_INCLUDE_DIR),-I$(OPENSSL_INCLUDE_DIR),)
LDFLAGS = $(THREAD_FLAGS) $(if $(OPENSSL_LIB_DIR),-L$(OPENSSL_LIB_DIR),)
LDLIBS = -lssl -lcrypto
ifneq ($(UNAME_S),Darwin)
LDLIBS += -ldl
endif
ifeq ($(USE_BATCHPIR),1)
CXXFLAGS := -DFUZZY_PETS_ENABLE_BATCHPIR=1 \
	-I$(VECTORIZED_BATCHPIR_DIR) \
	-I$(VECTORIZED_BATCHPIR_DIR)/header \
	$(addprefix -I,$(SEAL_INCLUDE_DIRS)) \
	$(SEAL_CXXFLAGS) \
	$(CXXFLAGS)
LDLIBS += $(SEAL_LDLIBS)
endif

.PHONY: all clean help smoke check-seal checklist-pir-selftest demo demo-test

all: test

help:
	@printf '%s\n' \
		'Targets:' \
		'  make test                 Build the main ./test experiment binary.' \
		'  make smoke                Build and run a tiny direct Cuckoo sanity check.' \
		'  make demo                 Build the staged E2LSH/Cuckoo/OPRF/BatchPIR demo.' \
		'  make demo-test            Run every demo stage with a local OPRF server.' \
		'  make checklist-pir-selftest  Build and test the Checklist two-server PIR port.' \
		'  make clean                Remove local build outputs owned by this makefile.' \
		'' \
		'Common experiment:' \
		'  ./test --filter=Cuckoo --L=5 --k=5 --w=0.5 --server_size=1000 --client_size=1000 --filter_size=3152' \
		'  ./run_cuckoo_server_sweep.sh --help'

# -------- TEST --------
test: test_fuzzypsi.o $(BATCHPIR_DIR)/batchpir_row_reader.o $(SHARED_OBJS) $(PIR_CHECKLIST_OBJS) $(OPRF_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(filter-out $(SEAL_LIB_FILE),$^) $(LDLIBS)

fuzzy_pets_demo: demo_fuzzypsi.o $(BATCHPIR_DIR)/batchpir_row_reader.o \
		$(SHARED_OBJS) $(PIR_CHECKLIST_OBJS) $(OPRF_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(filter-out $(SEAL_LIB_FILE),$^) $(LDLIBS)

demo: fuzzy_pets_demo

demo-test: fuzzy_pets_demo
	./scripts/test_demo.sh

checklist_pir_selftest: pir/checklist/checklist_pir_selftest.cpp \
		Utils/AES.o $(PIR_CHECKLIST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

checklist-pir-selftest: checklist_pir_selftest
	./checklist_pir_selftest

ifeq ($(USE_BATCHPIR),1)
test: $(SEAL_LIB_FILE)
test fuzzy_pets_demo $(BATCHPIR_DIR)/batchpir_row_reader.o: | check-seal

check-seal:
	@test -f "$(SEAL_ROOT)/native/src/seal/seal.h" || { \
		echo "error: SEAL headers not found under $(SEAL_ROOT)" >&2; \
		echo "clone Microsoft SEAL into ./SEAL and build it as documented in README.md" >&2; \
		exit 1; \
	}
	@test -f "$(SEAL_BUILD)/native/src/seal/util/config.h" || { \
		echo "error: generated SEAL headers not found under $(SEAL_BUILD)" >&2; \
		echo "run: cmake -S SEAL -B SEAL/build && cmake --build SEAL/build" >&2; \
		exit 1; \
	}
	@if grep -q '^#define SEAL_USE_MSGSL' "$(SEAL_BUILD)/native/src/seal/util/config.h" && \
		! test -f "$(SEAL_MSGSL_INCLUDE_DIR)/gsl/span"; then \
		echo "error: SEAL requires Microsoft GSL, but gsl/span was not found under $(SEAL_MSGSL_INCLUDE_DIR)" >&2; \
		echo "reconfigure SEAL with dependencies enabled: cmake -S SEAL -B SEAL/build -DSEAL_BUILD_DEPS=ON" >&2; \
		exit 1; \
	fi
	@test -n "$(SEAL_LIB_FILE)" -a -f "$(SEAL_LIB_FILE)" || { \
		echo "error: built SEAL library not found under $(SEAL_BUILD)/lib" >&2; \
		echo "run: cmake -S SEAL -B SEAL/build && cmake --build SEAL/build" >&2; \
		exit 1; \
	}
endif

smoke: test
	./test --filter=Cuckoo --L=3 --k=3 --w=0.5 --server_size=100 --client_size=20 --filter_size=512 --num_runs=1

# -------- compile rule --------
$(BATCHPIR_DIR)/batchpir_row_reader.o: $(BATCHPIR_DIR)/batchpir_row_reader.cpp $(BATCHPIR_ADAPTER_DEPS) makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_fuzzypsi.o: test_fuzzypsi.cpp $(TEST_FUZZYPSI_PARTS) \
		pir/pir_row_reader.h \
		$(BATCHPIR_DIR)/batchpir_row_reader.h \
		pir/checklist/pir_punc_psetggm_port.cpp \
		pir/checklist/psetggm/pset_ggm.h \
		pir/checklist/psetggm/answer.h makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

demo_fuzzypsi.o: demo_fuzzypsi.cpp cuckoo_filter.h \
		test_fuzzypsi_parts/globals_and_utils.inc.cpp \
		oprf/oprf_eval.inc.cpp \
		test_fuzzypsi_parts/cuckoo_prf_keys.inc.cpp \
		pir/batchpir/batchpir_readers.inc.cpp \
		pir/pir_membership.inc.cpp makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cpp makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f test fuzzy_pets_demo checklist_pir_selftest gowalla_stream_calibrate \
		test_fuzzypsi.o demo_fuzzypsi.o $(BATCHPIR_DIR)/batchpir_row_reader.o \
		gowalla_stream_calibrate.o $(SHARED_OBJS) $(PIR_CHECKLIST_OBJS) $(OPRF_OBJS)
