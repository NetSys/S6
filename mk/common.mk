ifndef S6_HOME
        $(error S6_HOME is not defined)
endif

ifndef RTE_SDK
        RTE_SDK = $(S6_HOME)/deps/dpdk-17.05
endif

ifndef RTE_TARGET
        RTE_TARGET = x86_64-native-linuxapp-gcc
endif

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
        DPDK_INCL = -isystem $(RTE_SDK)/$(RTE_TARGET)/include
        DPDK_LIB_DIR = $(RTE_SDK)/$(RTE_TARGET)/lib
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
        # if the user didn't do "make install" for DPDK
        DPDK_INCL = -isystem $(RTE_SDK)/build/include
        DPDK_LIB_DIR = $(RTE_SDK)/build/lib
else
        $(error DPDK is not available. \
                Make sure $(abspath $(RTE_SDK)) is available and built)
endif
DPDK_LIBS = -L$(DPDK_LIB_DIR) -Wl,--whole-archive -ldpdk

BOOST_INCL = -isystem $(S6_HOME)/deps/boost/build/include/
BOOST_LIB_DIR = $(S6_HOME)/deps/boost/build/lib/

S6_LIB_DIR = $(S6_HOME)/core/lib
S6_CORE_LIB = $(S6_LIB_DIR)/libdistref.a
S6_LIBS = -L$(S6_LIB_DIR) -ldistref \
	  -L$(BOOST_LIB_DIR) -lcoroutine -pthread \
	  $(DPDK_LIBS) \
	  -Wl,--no-whole-archive -lz -lm -ldl
S6_INCL = $(DPDK_INCL) $(BOOST_INCL) \
	  -I$(S6_HOME)/core/include

S6_INCL += -I$(APP_OBJ_DIR) \
	   -I$(APP_KEY_DIR) \
	   -I$(APP_GEN_SRC_DIR)
S6_LIBS += $(APP_LIBS)

CORES ?= $(shell nproc || echo 1)
MAKEFLAGS += -j $(CORES)

print-%  : ; @echo $* = $($*)

CC = g++
CFLAGS = -g
CFLAGS += -O2 -Wstrict-aliasing=2 -pthread
CFLAGS += -m64 -std=c++11 -march=native\
	  -Wall -Wno-attributes -Wno-unused-variable -Wno-unused-function -Wno-write-strings

.PHONY: all clean
.PRECIOUS: %.o

.DEFAULT_GOAL := all
all: $(DEPS) $(APP_BIN_DIR) $(APPS)

$(DEPS):
	@$(CC) $(CFLAGS) $(S6_INCL) -MM $(APP_SRCS) $(S6_HOME)/s6_worker/main.cpp $(APP_GEN_SRC_DIR)/adt_info.cpp | perl -0777 -pe 's/([-_[:alnum:]]*)\.o: (\\\n )?(.*)\.cpp/\3.o: \3.cpp/g' > $(DEPS);

%.o: %.cpp
	$(CC) -o $@ $(CFLAGS) $(S6_INCL) -c $<

$(APP_BIN_DIR):
	@mkdir -p $@

$(APPS): $(S6_CORE_LIB) $(APP_EXTRA_OBJ) $(APP_GEN_SRC_DIR)/adt_info.o
	$(CC) -o $@ $^ $(S6_LIBS)

clean:
	rm -f $(DEPS) $(APP_DIR)/*.o $(APP_GEN_SRC_DIR)/adt_info.o $(APPS)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
