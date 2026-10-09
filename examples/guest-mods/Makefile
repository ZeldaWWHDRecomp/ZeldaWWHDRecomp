# Builds the example guest mods: mod.c -> mod.elf (PowerPC relocatable ELF, platform independent).
# Needs a clang with the PowerPC target and ld.lld, e.g. Homebrew `llvm` + `lld` on macOS, the LLVM
# MSYS2 CLANG64 build on Windows or distro LLVM on Linux. Apple's clang has no PowerPC target.
CLANG ?= clang
LLD ?= ld.lld
SDK ?= ../../runtime/guest/include
CFLAGS = --target=powerpc-unknown-eabi -mcpu=750 -O2 -ffreestanding -fno-builtin -nostdlib \
         -fno-jump-tables -ffunction-sections -fdata-sections -I$(SDK)
MODS = heart-ticker addcalc-replace

all: $(MODS:%=%/mod.elf)
%/mod.o: %/mod.c $(SDK)/wwhd_guest.h $(wildcard $(SDK)/game/*.h)
	$(CLANG) $(CFLAGS) -c $< -o $@
%/mod.elf: %/mod.o
	$(LLD) -m elf32ppc -r $< -o $@
clean:
	rm -f $(MODS:%=%/mod.o) $(MODS:%=%/mod.elf)
