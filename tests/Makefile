ROOTDIR := $(abspath ../)
BINDIR := $(ROOTDIR)/build-x86/tests

CC := clang
MAKE := make
NASM := nasm
CFLAGS := -Wall -Werror -std=gnu11 -I$(ROOTDIR)/include -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -ggdb3

SRCS := $(sort $(wildcard *.c))
OBJS := $(patsubst %.c,$(BINDIR)/%.o,$(SRCS))
BINS := $(patsubst %.nasm,$(BINDIR)/%.bin,$(wildcard *.nasm))
TESTS := $(patsubst %.c,$(BINDIR)/%,$(SRCS))

all: $(TESTS)
	cd $(BINDIR); for t in $(TESTS); do $$t || exit 1; done

$(TESTS): $(BINDIR) $(BINS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/%: $(BINDIR)/%.o
	$(CC) $(LDFLAGS) $< -lcunit -livee -L$(ROOTDIR)/build-x86 -Wl,-rpath,$(ROOTDIR)/build-x86 -o $@

$(BINDIR)/%.bin: %.nasm
	$(NASM) -f bin $< -o $@
	chmod +x $@

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
