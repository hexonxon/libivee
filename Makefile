CC := clang
MAKE := make
CFLAGS := -Wall -Werror -std=gnu11 -Iinclude -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -fPIC
DEBUG_CFLAGS := -D_DEBUG -ggdb3 -O0
RELEASE_CFLAGS := -DNDEBUG -O2
LDFLAGS := -shared
DEBUG_LDFLAGS :=
RELEASE_LDFLAGS := -Wl,--version-script=libivee.version

BINDIR := build-x86
SRCDIR := src

HDRS := $(wildcard include/*.h)
HDRS += $(wildcard include/*/*.h)
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BINDIR)/%.o,$(SRCS))

TARGET_SO := $(BINDIR)/libivee.so
TARGET_ARCHIVE := $(BINDIR)/libivee.a

ifeq ($(CONFIG_DEBUG),y)
	CFLAGS += $(DEBUG_CFLAGS)
else
	CFLAGS += $(RELEASE_CFLAGS)
	LDFLAGS += $(RELEASE_LDFLAGS)
endif

all: $(TARGET_SO)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.o:$(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_SO): $(BINDIR) $(HDRS) $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

clean:
	$(MAKE) -C tests clean
	rm -rf $(BINDIR)

tests:
	$(MAKE) -C tests

.PHONY: all clean tests
