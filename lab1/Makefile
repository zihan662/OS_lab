CROSS=riscv64-unknown-elf-
CC=$(CROSS)gcc
LD=$(CROSS)ld
OBJDUMP=$(CROSS)objdump
NM=$(CROSS)nm
QEMU=qemu-system-riscv64

CFLAGS=-Wall -Werror -O -fno-omit-frame-pointer -ggdb
CFLAGS+=-mcmodel=medany -mno-relax
CFLAGS+=-I.

LDFLAGS=-z max-page-size=4096

# Build kernel.elf
kernel.elf: kernel/entry.S kernel/start.c kernel/uart.c
	$(CC) $(CFLAGS) -c kernel/entry.S -o kernel/entry.o
	$(CC) $(CFLAGS) -c kernel/start.c -o kernel/start.o
	$(CC) $(CFLAGS) -c kernel/uart.c -o kernel/uart.o
	$(LD) $(LDFLAGS) -T kernel/kernel.ld kernel/entry.o kernel/start.o kernel/uart.o -o kernel.elf

# Run QEMU with kernel.elf
qemu: kernel.elf
	$(QEMU) -nographic -machine virt -kernel kernel.elf

# Clean generated files
clean:
	rm -f *.o kernel/*.o kernel.elf