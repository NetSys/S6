S6_HOME = $(abspath ../)

APP_OBJ_DIR = $(S6_HOME)/user_source/samples/objects
APP_KEY_DIR = $(S6_HOME)/user_source/keys
APP_GEN_SRC_DIR = $(S6_HOME)/gen_source/samples_sample

APP_DIR = $(S6_HOME)/user_source/samples/apps/sample
APP_BIN_DIR = $(S6_HOME)/bin/apps
APP_LIBS = -lpcre

APP_SRCS = $(wildcard $(APP_DIR)/*_app.cpp)
APPS = $(patsubst $(APP_DIR)/%.cpp, $(APP_BIN_DIR)/%, \
       $(wildcard $(APP_DIR)/*_app.cpp))

# one object -> one executable
.SECONDEXPANSION:
$(APPS): $(APP_DIR)/$$(notdir $$@).o

APP_EXTRA_OBJ = $(S6_HOME)/s6_worker/main.o

DEPS = .make.apps.dep

include common.mk
