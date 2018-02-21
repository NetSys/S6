ifndef RTE_SDK
	RTE_SDK = $(abspath ../deps/dpdk-17.05)
endif

ifndef RTE_TARGET
	RTE_TARGET = x86_64-native-linuxapp-gcc
endif

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
	DPDK_INCL = -isystem $(RTE_SDK)/$(RTE_TARGET)/include
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
	# if the user didn't do "make install" for DPDK
	DPDK_INCL = -isystem $(RTE_SDK)/build/include
else
	$(error DPDK is not available. \
		Make sure $(abspath $(RTE_SDK)) is available and built)
endif

BOOST_INCL = -isystem $(abspath ../deps/boost/build/include/)

RAPIDJSON_PATH = $(abspath ../deps/rapidjson/include/)
RAPIDJSON_INCL = -isystem $(RAPIDJSON_PATH)

INCLUDES = -Iinclude \
	   -I../user_source/prads/objects/ \
	   -I../gen_source/prads/ \
	   -I../gen_source/samples_sample/ \
	   -I../user_source/samples/objects/ \
	   $(BOOST_INCL) $(DPDK_INCL) $(RAPIDJSON_INCL)
HEADERS = $(wildcard **/*.hh)
SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:.cpp=.o)

LIB = ./lib
LIBDISTREF = $(LIB)/libdistref.a

DEPS = .make.dep

CORES ?= $(shell nproc || echo 1)
MAKEFLAGS += -j $(CORES)

CC = g++
CFLAGS = -g
CFLAGS += -O2 -Wstrict-aliasing=2
CFLAGS += -m64 -std=c++11 -march=native\
	  -Wall -Wno-attributes -Wno-unused-variable -Wno-unused-function\
	  -fPIC

.PHONY: all clean

all: $(DEPS) $(LIB) $(LIBDISTREF)

$(DEPS): $(SRCS) $(HEADERS)
	@$(CC) $(CFLAGS) $(INCLUDES) -MM $(SRCS) | perl -0777 -pe 's/([-_[:alnum:]]*)\.o: (\\\n )?(.*)\.cpp/\3.o: \3.cpp/g' > $(DEPS);

.cpp.o:
	$(CC) -o $@ $(CFLAGS) $(INCLUDES) -c $<

$(LIB):
	mkdir -p $(LIB)

$(LIBDISTREF): $(OBJS)
	ar rcs $@ $^

clean:
	rm -f **/*.o $(DEPS) $(LIBDISTREF)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
