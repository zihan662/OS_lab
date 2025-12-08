CROSS=riscv64-unknown-elf-
CC=$(CROSS)gcc
LD=$(CROSS)ld
OBJDUMP=$(CROSS)objdump
NM=$(CROSS)nm
QEMU=qemu-system-riscv64

CFLAGS=-Wall -Werror -O -fno-omit-frame-pointer -ggdb -Wno-format -Wno-format-overflow
CFLAGS+=-mcmodel=medany -mno-relax
CFLAGS+=-I./kernel
CFLAGS+=-I./user

LDFLAGS=-z max-page-size=4096
          
USER_ELF = user/test_syscalls.elf
USER_BIN = user/test_syscalls.bin

kernel.elf: kernel/entry.S kernel/start.c kernel/uart.c kernel/console.c kernel/printf.c kernel/pmm.c kernel/spinlock.c kernel/string.c kernel/pagetable.c kernel/vm.c kernel/interrupts.c kernel/trap.S kernel/timer.c kernel/proc.c kernel/swtch.S kernel/cpu.c kernel/bcache.c kernel/log.c kernel/dir.c kernel/fs.c kernel/sysproc.c kernel/syscall.c kernel/initcode.S kernel/usermode.S $(USER_BIN)
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
	$(CC) $(CFLAGS) -c kernel/interrupts.c -o kernel/interrupts.o
	$(CC) $(CFLAGS) -c kernel/trap.S -o kernel/trap.o
	$(CC) $(CFLAGS) -c kernel/timer.c -o kernel/timer.o
	$(CC) $(CFLAGS) -c kernel/proc.c -o kernel/proc.o
	$(CC) $(CFLAGS) -c kernel/swtch.S -o kernel/swtch.o
	$(CC) $(CFLAGS) -c kernel/cpu.c -o kernel/cpu.o
	$(CC) $(CFLAGS) -c kernel/bcache.c -o kernel/bcache.o
	$(CC) $(CFLAGS) -c kernel/log.c -o kernel/log.o
	$(CC) $(CFLAGS) -c kernel/dir.c -o kernel/dir.o
	$(CC) $(CFLAGS) -c kernel/fs.c -o kernel/fs.o
	$(CC) $(CFLAGS) -c kernel/sysproc.c -o kernel/sysproc.o
	$(CC) $(CFLAGS) -c kernel/syscall.c -o kernel/syscall.o
	$(CC) $(CFLAGS) -c kernel/initcode.S -o kernel/initcode.o
	$(CC) $(CFLAGS) -c kernel/usermode.S -o kernel/usermode.o
	$(LD) $(LDFLAGS) -T kernel/kernel.ld kernel/entry.o kernel/start.o kernel/uart.o kernel/console.o kernel/printf.o kernel/pmm.o kernel/spinlock.o kernel/string.o kernel/pagetable.o kernel/vm.o kernel/interrupts.o kernel/trap.o kernel/timer.o kernel/proc.o kernel/swtch.o kernel/cpu.o kernel/bcache.o kernel/log.o kernel/dir.o kernel/fs.o kernel/sysproc.o kernel/syscall.o kernel/initcode.o kernel/usermode.o -o kernel.elf

# Build user test_syscalls to ELF and binary
user/start_user.o: user/start_user.S
	$(CC) -nostdlib -static -ffreestanding -fno-pic -I./user -c user/start_user.S -o user/start_user.o

user/test_syscalls.o: user/test_syscalls.c user/user.h
	$(CC) -nostdlib -static -ffreestanding -fno-pic -I./user -c user/test_syscalls.c -o user/test_syscalls.o

$(USER_ELF): user/start_user.o user/test_syscalls.o user/user.ld
	$(CC) -nostdlib -static -ffreestanding -fno-pic -I./user -T user/user.ld -o $(USER_ELF) user/start_user.o user/test_syscalls.o

$(USER_BIN): $(USER_ELF)
	$(CROSS)objcopy -O binary --only-section=.text --only-section=.rodata $(USER_ELF) $(USER_BIN)


#Run QEMU with kernel.elf
qemu: kernel.elf
	$(QEMU) -nographic -machine virt -bios default -kernel kernel.elf
# Debug targets for GDB
qemu-gdb: kernel.elf
	$(QEMU) -nographic -machine virt -bios default -kernel kernel.elf -s -S

# GDB connection helper
gdb: kernel.elf
	$(CROSS)gdb -ex "target remote localhost:1234" -ex "symbol-file kernel.elf" kernel.elf
# Fallback: use gdb-multiarch if cross gdb fails
gdb-multi: kernel.elf
	gdb-multiarch -ex "target remote localhost:1234" -ex "symbol-file kernel.elf" kernel.elf

clean:
	rm -f kernel/*.o kernel.elf
	rm -f $(USER_ELF) $(USER_BIN)
