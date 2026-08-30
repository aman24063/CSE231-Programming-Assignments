#include "loader.h"
#include <sys/stat.h>

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;

static char  *file_buf   = NULL;
static void  *mapped_seg = NULL;
static size_t mapped_len = 0;

/*
 * release memory and other cleanups
 */
void loader_cleanup() {
  if (mapped_seg != NULL && mapped_seg != MAP_FAILED) {
    munmap(mapped_seg, mapped_len);
    mapped_seg = NULL;
  }
  if (file_buf != NULL) {
    free(file_buf);
    file_buf = NULL;
  }
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

/*
 * Load and run the ELF executable file
 */
void load_and_run_elf(char** exe) {
  fd = open(exe[1], O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Error: could not open '%s'\n", exe[1]);
    exit(1);
  }

  // 1. Load entire binary content into the memory from the ELF file.
  struct stat st;
  if (fstat(fd, &st) < 0) {
    fprintf(stderr, "Error: fstat failed\n");
    close(fd);
    exit(1);
  }
  size_t file_size = (size_t)st.st_size;

  file_buf = (char *)malloc(file_size);
  if (file_buf == NULL) {
    fprintf(stderr, "Error: malloc failed\n");
    close(fd);
    exit(1);
  }

  ssize_t total_read = 0;
  while ((size_t)total_read < file_size) {
    ssize_t n = read(fd, file_buf + total_read, file_size - total_read);
    if (n <= 0) {
      fprintf(stderr, "Error: read failed\n");
      free(file_buf);
      close(fd);
      exit(1);
    }
    total_read += n;
  }

  ehdr = (Elf32_Ehdr *)file_buf;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
    fprintf(stderr, "Error: not an ELF file\n");
    loader_cleanup();
    exit(1);
  }
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
    fprintf(stderr, "Error: not a 32-bit ELF file\n");
    loader_cleanup();
    exit(1);
  }

  // 2. Iterate through the PHDR table and find the section of PT_LOAD 
  //    type that contains the address of the entrypoint method in factorial.c
  phdr = (Elf32_Phdr *)(file_buf + ehdr->e_phoff);
  Elf32_Phdr *entry_phdr = NULL;

  for (int i = 0; i < ehdr->e_phnum; i++) {
    Elf32_Phdr *cur = &phdr[i];
    if (cur->p_type == PT_LOAD &&
        ehdr->e_entry >= cur->p_vaddr &&
        ehdr->e_entry <  cur->p_vaddr + cur->p_memsz) {
      entry_phdr = cur;
      break;
    }
  }

  if (entry_phdr == NULL) {
    fprintf(stderr, "Error: no PT_LOAD segment contains the entry point\n");
    loader_cleanup();
    exit(1);
  }

  // 3. Allocate memory of the size "p_memsz" using mmap function 
  //    and then copy the segment content
  mapped_len = entry_phdr->p_memsz;
  mapped_seg = mmap(NULL, mapped_len,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  if (mapped_seg == MAP_FAILED) {
    fprintf(stderr, "Error: mmap failed\n");
    loader_cleanup();
    exit(1);
  }
  memcpy(mapped_seg, file_buf + entry_phdr->p_offset, entry_phdr->p_memsz);

  // 4. Navigate to the entrypoint address into the segment loaded in the memory in above step
  Elf32_Addr offset_in_segment = ehdr->e_entry - entry_phdr->p_vaddr;
  void *entry_point = (char *)mapped_seg + offset_in_segment;

  // 5. Typecast the address to that of function pointer matching "_start" method in factorial.c.
  int (*start_fn)(void) = (int (*)(void))entry_point;

  // 6. Call the "_start" method and print the value returned from the "_start"
  int result = start_fn();
  printf("User _start return value = %d\n",result);
}
