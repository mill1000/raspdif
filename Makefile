TARGET_NAME ?= raspdif

BUILD_DIR ?= build
SRC_BASE ?= source
INC_BASE ?= include

MKDIR_P ?= mkdir -p

SRCS := $(shell find $(SRC_BASE) -name "*.cpp" -or -name "*.c")
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(INC_BASE) -type d) /opt/vc/include
INC_FLAGS := $(addprefix -I ,$(INC_DIRS))

LDFLAGS := -L /opt/vc/lib -lbcm_host -lm -lvcos -lpthread -lstdc++
CPPFLAGS ?= $(INC_FLAGS) -MMD 
CFLAGS ?= -Wall -Wno-missing-braces
CXXFALGS ?= -std=c++11 $(CFLAGS)
CC = clang
CXX = clang++

all: $(BUILD_DIR)/$(TARGET_NAME)

$(BUILD_DIR)/$(TARGET_NAME): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ 

$(BUILD_DIR)/%.c.o: %.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@	

$(BUILD_DIR)/%.cpp.o: %.cpp
	@$(MKDIR_P) $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFALGS) -c $< -o $@	

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)
