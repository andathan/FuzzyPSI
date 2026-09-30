
SRC_DIR := ..
REPO_ROOT := ../../../..
LIB := libpsetggm.a
HEADERS := $(addprefix $(SRC_DIR)/, pset_ggm.h)
SRC := $(addprefix $(SRC_DIR)/, pset_ggm.cpp xor.cpp) $(REPO_ROOT)/Utils/AES.cpp
TESTS := test_pset test_distinct test_xor

OBJ := $(addsuffix .o,$(basename $(SRC)))

CCFLAGS := -Ofast -I.. -I$(REPO_ROOT) -g

SIMD_FLAGS.x86_64 := -msse2 -msse -march=native -maes
SIMD_FLAGS.aarch64 := -march=armv8-a+fp+simd+crypto+crc


uname_p := $(strip $(shell uname -p))
FLAGS := $(CCFLAGS) $(SIMD_FLAGS.${uname_p})

all: $(LIB) $(TESTS)

test_pset: test_pset.cpp $(LIB)
	g++ $(FLAGS) -o $@ $^

test_distinct: test_distinct.cpp $(LIB)
	g++ $(FLAGS) -o $@ $^

test_xor: test_xor.cpp $(LIB)
	g++ $(FLAGS) -o $@ $^


%.o: %.cpp $(HEADERS)
	g++ $(FLAGS) -c $< -o $@

$(LIB): $(OBJ)
	ar rcs $@ $(OBJ)

clean:
	rm -rf $(LIB) $(TESTS) $(OBJ) $(addsuffix .dSYM,$(TESTS))
