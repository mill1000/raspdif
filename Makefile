TARGET_NAME ?= raspdif
PREFIX ?= /usr/local

BUILD_DIR ?= build
SRC_BASE ?= source
INC_BASE ?= include

MKDIR_P ?= mkdir -p

SRCS := $(shell find $(SRC_BASE) -name "*.cpp" -or -name "*.c")
INCS := $(shell find $(INC_BASE) -name "*.h")
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
TARGET = $(BUILD_DIR)/$(TARGET_NAME)

INC_DIRS := $(shell find $(INC_BASE) -type d) /opt/vc/include
INC_FLAGS := $(addprefix -I ,$(INC_DIRS))

LDFLAGS := -L /opt/vc/lib -lbcm_host -lm -lvcos -lpthread -lstdc++
CPPFLAGS ?= $(INC_FLAGS) -MMD 
CFLAGS ?= -Wall -Wno-missing-braces
CC = clang

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ 

$(BUILD_DIR)/%.c.o: %.c $(INC_BASE)/git_version.h
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@	

$(INC_BASE)/git_version.h: .FORCE
	echo "#define GIT_VERSION \"Commit: $(shell git describe --dirty --always --tags)\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new

.FORCE:

.PHONY: clean install uninstall
clean:
	$(RM) -r $(BUILD_DIR)

install: $(TARGET)
	@$(MKDIR_P) $(DESTDIR)$(PREFIX)/bin
	cp $< $(DESTDIR)$(PREFIX)/bin/${TARGET_NAME}

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/${TARGET_NAME}

check-format:
	clang-format-16 --Werror -n $(SRCS) $(INCS)

format:
	clang-format-16 -i $(SRCS) $(INCS)

-include $(DEPS)
