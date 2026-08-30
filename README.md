# CSE231 – Programming Assignment 1
## ELF Loader and x86 Bootloader

This repository contains my implementation of **CSE231 – Operating Systems Programming Assignment 1**.

The assignment covers low-level program loading and execution through:

- **Part A:** Simple ELF Loader
- **Part A – Bonus:** Shared-library based ELF Loader with a separate launcher
- **Part B:** 16-bit x86 Bootloader

---

## Part A – Simple ELF Loader

### Objective

The objective of Part A is to implement a **Simple ELF Loader in C** capable of loading and executing a **32-bit ELF executable**.

The provided `factorial.c` program is compiled into a 32-bit ELF executable. The loader then reads the ELF file, interprets its headers, identifies the required loadable segment, maps it into memory, locates the entry point, and transfers execution to `_start()`.

### Execution Flow

```text
factorial.c
     |
     | GCC
     v
factorial
(32-bit ELF executable)
     |
     v
Simple ELF Loader
     |
     +--> open()
     |
     +--> read()
     |
     +--> Parse ELF Header
     |
     +--> Locate Program Header Table
     |
     +--> Find PT_LOAD segment
     |
     +--> mmap() memory
     |
     +--> Copy segment
     |
     +--> Locate e_entry
     |
     +--> Call _start()
     |
     v
factorial(5)
     |
     v
120
```

### Loader Implementation

The loader implemented in `loader.c` performs the following operations:

1. Opens the ELF executable using `open()`.
2. Obtains the file size using `fstat()`.
3. Allocates memory for the ELF binary using `malloc()`.
4. Reads the ELF binary using `read()`.
5. Interprets the ELF header using `Elf32_Ehdr`.
6. Validates that the input is a valid 32-bit ELF executable.
7. Locates the Program Header Table using `e_phoff`.
8. Iterates through the program headers using `e_phnum`.
9. Identifies the `PT_LOAD` segment containing the entry point.
10. Allocates memory for the segment using `mmap()`.
11. Copies the required segment contents into the mapped memory.
12. Calculates the entry-point location using `e_entry` and `p_vaddr`.
13. Converts the entry-point address into a function pointer.
14. Calls `_start()`.
15. Prints the value returned by `_start()`.
16. Releases allocated resources during cleanup.

### Test Program

The provided `factorial.c` contains a factorial implementation and an `_start()` function.

The `_start()` function executes:

```text
num = 5
    |
    v
factorial(5)
    |
    v
120
```

The returned value is received and printed by the loader.

Expected output:

```text
User _start return value = 120
```

---

## Part A – Without Bonus

The `without-bonus` directory contains the basic standalone implementation of the Simple ELF Loader.

### Directory Structure

```text
Simple_ELF_Loader/
└── without-bonus/
    ├── factorial.c
    ├── loader.c
    ├── loader.h
    └── Makefile
```

### Build

```bash
cd Simple_ELF_Loader/without-bonus
make
```

This builds the factorial test executable and the loader.

The factorial test case is compiled as a 32-bit executable using:

```text
-m32 -no-pie -nostdlib
```

The loader is compiled using:

```text
-m32
```

### Run

```bash
./loader ./factorial
```

Expected output:

```text
User _start return value = 120
```

---

## Part A – Bonus

The bonus implementation separates the loader into a **shared library** and a **launcher program**.

The loader is compiled as:

```text
lib_simpleloader.so
```

and the launcher is compiled as:

```text
launch
```

The generated launcher and shared library are placed inside the `bin` directory.

### Directory Structure

```text
Simple_ELF_Loader/
└── with-bonus/
    ├── bin/
    │   ├── launch
    │   └── lib_simpleloader.so
    │
    ├── launcher/
    │   ├── launch.c
    │   └── Makefile
    │
    ├── loader/
    │   ├── loader.c
    │   ├── loader.h
    │   └── Makefile
    │
    ├── test/
    │   ├── factorial.c
    │   └── Makefile
    │
    └── Makefile
```

### Execution Flow

```text
factorial.c
     |
     v
factorial
(32-bit ELF executable)
     |
     v
launch
     |
     v
lib_simpleloader.so
     |
     v
Simple ELF Loader
     |
     +--> Read ELF
     +--> Find PT_LOAD
     +--> mmap()
     +--> Load segment
     +--> Find entry point
     +--> Call _start()
     |
     v
120
```

The launcher accepts the ELF executable as a command-line argument, passes it to the loader, and invokes the loader cleanup routine after execution.

### Build

```bash
cd Simple_ELF_Loader/with-bonus
make
```

The build generates:

```text
bin/
├── launch
└── lib_simpleloader.so
```

and:

```text
test/
└── factorial
```

### Run

```bash
./bin/launch ./test/factorial
```

Expected output:

```text
User _start return value = 120
```

---

# Part B – x86 Bootloader

Part B focuses on the fundamentals of the **x86 boot process** by implementing a small 16-bit bootloader.

The bootloader source and generated binary are located in:

```text
Simple_ELF_Loader/Complete_bootloader/
```

### Files

```text
Complete_bootloader/
├── complete_bootloader.asm
└── boot.bin
```

### Bootloader Functionality

The bootloader:

- Executes in 16-bit x86 mode.
- Starts execution at `0x7C00`.
- Uses BIOS interrupt `0x10` for character output.
- Reads characters from the message string.
- Displays the message on the screen.
- Disables interrupts after displaying the message.
- Halts the CPU.
- Enters an infinite loop.
- Pads the boot sector to 512 bytes.
- Places the boot signature `0xAA55` at the end of the boot sector.

The message displayed by the bootloader is:

```text
Hello from my bootloader!
```

### Bootloader Execution Flow

```text
BIOS
  |
  | Loads boot sector
  v
0x7C00
  |
  v
Bootloader
  |
  +--> Initialize registers
  |
  +--> Read character
  |
  +--> BIOS interrupt 0x10
  |
  +--> Display character
  |
  +--> Repeat until end of string
  |
  +--> Disable interrupts
  |
  +--> Halt CPU
  |
  v
Infinite loop
```

### Build

Navigate to the bootloader directory:

```bash
cd Simple_ELF_Loader/Complete_bootloader
```

Assemble the bootloader using NASM:

```bash
nasm -f bin complete_bootloader.asm -o boot.bin
```

### Run Using QEMU

```bash
qemu-system-x86_64 -drive format=raw,file=boot.bin
```

The QEMU window should display:

```text
Hello from my bootloader!
```

---

# Project Structure

```text
CSE231-Programming-Assignments-2026/
│
├── Simple_ELF_Loader/
│   │
│   ├── without-bonus/
│   │   ├── factorial.c
│   │   ├── loader.c
│   │   ├── loader.h
│   │   └── Makefile
│   │
│   ├── with-bonus/
│   │   ├── bin/
│   │   │   ├── launch
│   │   │   └── lib_simpleloader.so
│   │   │
│   │   ├── launcher/
│   │   │   ├── launch.c
│   │   │   └── Makefile
│   │   │
│   │   ├── loader/
│   │   │   ├── loader.c
│   │   │   ├── loader.h
│   │   │   └── Makefile
│   │   │
│   │   ├── test/
│   │   │   ├── factorial.c
│   │   │   └── Makefile
│   │   │
│   │   └── Makefile
│   │
│   └── Complete_bootloader/
│       ├── complete_bootloader.asm
│       └── boot.bin
│
└── README.md
```

---

# Technologies Used

- C
- GCC
- GNU Make
- ELF
- Linux
- `open()`
- `read()`
- `fstat()`
- `mmap()`
- `munmap()`
- Function pointers
- Shared libraries
- x86 Assembly
- NASM
- QEMU
- Git

---

# Key Concepts

This project covers several operating-system and systems-programming concepts:

- ELF executable format
- ELF Header
- Program Header Table
- Program Headers
- `PT_LOAD` segments
- Virtual addresses
- Entry-point addresses
- File descriptors
- File I/O
- Memory mapping
- `mmap()` and `munmap()`
- Shared libraries
- Function pointers
- Low-level program loading
- Makefiles
- x86 boot sectors
- BIOS interrupts
- Boot-sector signatures
- QEMU emulation

---

# Overall Learning

The project demonstrates two different stages of low-level system operation.

### ELF Program Loading

```text
C Source Code
      |
      v
Compiler
      |
      v
Object File
      |
      v
Linker
      |
      v
ELF Executable
      |
      v
Simple ELF Loader
      |
      v
Memory Mapping
      |
      v
Entry Point
      |
      v
CPU Execution
```

### Boot Process

```text
BIOS
  |
  v
Boot Sector
  |
  v
0x7C00
  |
  v
x86 CPU
  |
  v
Bootloader Instructions
  |
  v
BIOS Services
  |
  v
Screen Output
```

---

