S6_HOME = $(abspath ../)

APP_OBJ_DIR = $(S6_HOME)/user_source/prads/objects
APP_KEY_DIR = $(S6_HOME)/user_source/keys
APP_GEN_SRC_DIR = $(S6_HOME)/gen_source/prads

APP_DIR = $(S6_HOME)/user_source/prads/src
APP_BIN_DIR = $(S6_HOME)/bin
APP_LIBS = -lpcre -l:libssl.a -l:libcrypto.a

APP_SRCS = $(wildcard $(APP_DIR)/*.cpp)
APPS = $(APP_BIN_DIR)/prads

APP_EXTRA_OBJ = $(patsubst %.cpp, %.o, $(APP_SRCS)) $(S6_HOME)/s6_worker/main.o

DEPS = .make.prads.dep

include common.mk
