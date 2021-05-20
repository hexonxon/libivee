ROOTDIR := $(abspath ../)
BINDIR := $(ROOTDIR)/build-x86/tests

CC := clang
MAKE := make
NASM := nasm
CFLAGS := -Wall -Werror -std=gnu11 -I$(ROOTDIR)/include -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -ggdb3

SRCS := $(sort $(wildcard *.c))
OBJS := $(patsubst %.c,$(BINDIR)/%.o,$(SRCS))
TESTS := $(patsubst %.c,$(BINDIR)/%,$(SRCS))

all: $(TESTS)
	cd $(BINDIR); for t in $(TESTS); do $$t || exit 1; done

$(TESTS): $(BINDIR) $(BINS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/%.o: %.nasm
	$(NASM) -f elf64 -o $@ $<

$(BINDIR)/%: $(BINDIR)/%.o
	$(CC) $(LDFLAGS) $< -lcunit -livee -L$(ROOTDIR)/build-x86 -Wl,-rpath,$(ROOTDIR)/build-x86 -o $@

$(BINDIR)/%.bin: %.nasm
	$(NASM) -f bin -o $@ $<
	chmod +x $@

$(BINDIR)/%.elf64: $(BINDIR)/%.o
	$(LD) --gc-sections -nostdlib -e entry -o $@ $<
	chmod +x $@

$(BINDIR)/smoke_test: $(BINDIR)/smoke_test_payload.bin $(BINDIR)/smoke_test_payload.elf64

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
