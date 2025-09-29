CROSS=riscv64-unknown-elf-
CC=$(CROSS)gcc
LD=$(CROSS)ld
OBJDUMP=$(CROSS)objdump
NM=$(CROSS)nm
QEMU=qemu-system-riscv64

CFLAGS=-Wall -Werror -O -fno-omit-frame-pointer -ggdb -Wno-format -Wno-format-overflow
CFLAGS+=-mcmodel=medany -mno-relax
CFLAGS+=-I./kernel

LDFLAGS=-z max-page-size=4096

kernel.elf: kernel/entry.S kernel/start.c kernel/uart.c kernel/console.c kernel/printf.c
	$(CC) $(CFLAGS) -c kernel/entry.S -o kernel/entry.o
	$(CC) $(CFLAGS) -c kernel/start.c -o kernel/start.o
	$(CC) $(CFLAGS) -c kernel/uart.c -o kernel/uart.o
	$(CC) $(CFLAGS) -c kernel/console.c -o kernel/console.o
	$(CC) $(CFLAGS) -c kernel/printf.c -o kernel/printf.o
	$(LD) $(LDFLAGS) -T kernel/kernel.ld kernel/entry.o kernel/start.o kernel/uart.o kernel/console.o kernel/printf.o -o kernel.elf

#Run QEMU with kernel.elf
qemu: kernel.elf
	$(QEMU) -nographic -machine virt -bios default -kernel kernel.elf
clean:
	rm -f *.o kernel.elf
