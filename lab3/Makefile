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

kernel.elf: kernel/entry.S kernel/start.c kernel/uart.c kernel/console.c kernel/printf.c kernel/pmm.c kernel/spinlock.c kernel/string.c
	$(CC) $(CFLAGS) -c kernel/entry.S -o kernel/entry.o
	$(CC) $(CFLAGS) -c kernel/start.c -o kernel/start.o
	$(CC) $(CFLAGS) -c kernel/uart.c -o kernel/uart.o
	$(CC) $(CFLAGS) -c kernel/console.c -o kernel/console.o
	$(CC) $(CFLAGS) -c kernel/printf.c -o kernel/printf.o
	$(CC) $(CFLAGS) -c kernel/pmm.c -o kernel/pmm.o
	$(CC) $(CFLAGS) -c kernel/spinlock.c -o kernel/spinlock.o
	$(CC) $(CFLAGS) -c kernel/string.c -o kernel/string.o
	$(CC) $(CFLAGS) -c kernel/pagetable.c -o kernel/pagetable.o
	$(CC) $(CFLAGS) -c kernel/vm.c -o kernel/vm.o
	$(LD) $(LDFLAGS) -T kernel/kernel.ld kernel/entry.o kernel/start.o kernel/uart.o kernel/console.o kernel/printf.o kernel/pmm.o kernel/spinlock.o kernel/string.o kernel/pagetable.o kernel/vm.o -o kernel.elf

#Run QEMU with kernel.elf
qemu: kernel.elf
	$(QEMU) -nographic -machine virt -bios default -kernel kernel.elf
clean:
	rm -f *.o kernel.elf
