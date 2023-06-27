#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "vm/vm.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	lock_init (&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	// thread_exit ();
	thread_current ()->user_rsp = f->rsp;

	switch (f->R.rax) {
		case SYS_HALT:
			halt ();
			break;
		case SYS_EXIT:
			exit (f->R.rdi);
			break;
		case SYS_FORK:
			memcpy (&thread_current ()->ptf, f, sizeof (struct intr_frame));
			f->R.rax = fork (f->R.rdi);
			break;
		case SYS_EXEC:
			exec (f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait (f->R.rdi);
			break; 
		case SYS_CREATE:
			f->R.rax = create (f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove (f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open (f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize (f->R.rdi);
			break;
		case SYS_READ:
			check_valid_buffer (f->R.rsi, f->R.rdx, f->rsp, 1);
			f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
			break;  
		case SYS_WRITE:      
			check_valid_buffer (f->R.rsi, f->R.rdx, f->rsp, 0);
			f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek (f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell (f->R.rdi);
			break;
		case SYS_CLOSE:
			close (f->R.rdi);
			break;
		case SYS_DUP2:
			f->R.rax = dup2 (f->R.rdi, f->R.rsi);
			break;
		case SYS_MMAP:
			f->R.rax = mmap (f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		case SYS_MUNMAP:
			munmap (f->R.rdi);
			break;
		default:
			exit (-1);
			break;
	}
}

void halt (void) {
	power_off ();
}

void exit (int status) {
	struct thread *cur = thread_current ();

	cur->exit_status = status;
	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit ();
}

int fork (const char *thread_name) {
	check_address (thread_name);
	return process_fork (thread_name, &thread_current ()->ptf);
}

int exec (const char *file_name) {
	check_address (file_name);

	int file_size = strlen (file_name) + 1;
	char *fn_copy = palloc_get_page (PAL_ZERO);

	if (!fn_copy) {
		exit (-1);
		return -1;
	}

	strlcpy (fn_copy, file_name, file_size);

	if (process_exec (fn_copy) == -1) {
		exit (-1);
		return -1;
	}
}

int wait (tid_t pid) {
  	return process_wait (pid);
}

bool create (const char *file, unsigned initial_size) {
	check_address (file);
	lock_acquire (&filesys_lock);
	bool result = filesys_create (file, initial_size);
	lock_release (&filesys_lock);

	return result;
}

bool remove (const char *file) {
	check_address (file);
	return filesys_remove (file);
}

int open (const char *file) {
	check_address (file);

	struct thread *cur = thread_current ();
	lock_acquire (&filesys_lock);
	struct file *fd = filesys_open (file);
	lock_release (&filesys_lock);

	if (fd) {
		for (int i = 2; i < 128; i++) {
			if (!cur->fdt[i]) {
				cur->fdt[i] = fd;
				cur->next_fd = i + 1;
				return i;
			}
		}
		file_close(fd);
	}

	return -1;
}

int filesize (int fd) {
	struct file *file = thread_current ()->fdt[fd];

	if (file)
		return file_length (file);
	return -1;
}

int read (int fd, void *buffer, unsigned size) {
	if (fd == 1)
		return -1;

	if (fd == 0) {
		lock_acquire (&filesys_lock);
		int byte = input_getc ();
		lock_release (&filesys_lock);
		return byte;
	}

	struct file *file = thread_current ()->fdt[fd];

	if (file) {
		lock_acquire (&filesys_lock);
		int read_byte = file_read (file, buffer, size);
		lock_release (&filesys_lock);
		return read_byte;
	}

	return -1;
}

int write (int fd UNUSED, const void *buffer, unsigned size) {
	if (fd == 0)
		return -1;

	if (fd == 1) {
		lock_acquire (&filesys_lock);
		putbuf (buffer, size);
		lock_release (&filesys_lock);
		return size;
	}

	struct file *file = thread_current ()->fdt[fd];

	if (file) {
		lock_acquire (&filesys_lock);
		int write_byte = file_write (file, buffer, size);
		lock_release (&filesys_lock);
		return write_byte;
	}
}

void seek (int fd, unsigned position) {
	struct file *curfile = thread_current ()->fdt[fd];

	if (curfile)
		file_seek (curfile, position);
}

unsigned tell (int fd) {
	struct file *curfile = thread_current ()->fdt[fd];

	if (curfile)
		return file_tell (curfile);
}

void close (int fd) {
	struct thread *curr = thread_current ();
	struct file *file = curr->fdt[fd];
	if (file == NULL)
		return;

	if (file == 1)
		curr->stdin_cnt--;
	else if (file == 2)
		curr->stdout_cnt--;
	else {
		if (file->dup_cnt == 0) {
			curr->next_fd = fd;
			file_close (file);
		} else
			file->dup_cnt--;
	}

	thread_current ()->fdt[fd] = NULL;
}

struct page *check_address (uint64_t addr) {
	if (is_kernel_vaddr (addr) || addr == NULL)
		exit (-1);

	return spt_find_page (&thread_current ()->spt, addr);
}

void
check_valid_buffer (void *buffer, unsigned size, void *rsp, bool to_write) {
	uintptr_t start_page = pg_round_down (buffer);
	uintptr_t end_page = pg_round_down (buffer + size - 1);

	if (buffer <= USER_STACK && buffer >= rsp)
		return;

	for (; start_page <= end_page; start_page += PGSIZE) {
		struct page *page = check_address (start_page);
		if (page == NULL)
			exit (-1);

		if (to_write == true && page->writable == false)
			exit (-1);
	}
}

int dup2 (int oldfd, int newfd) {
	if (oldfd == newfd)
		return newfd;

	struct thread *cur = thread_current ();
	struct file *f = cur->fdt[oldfd];

	if (f == NULL)
		return -1;

	if (newfd < 0 || newfd >= 128)
		return -1;

	if (f == 1)
		cur->stdin_cnt++;
	else if (f == 2)
		cur->stdout_cnt++;
	else
		f->dup_cnt++;

	close (newfd);
	cur->fdt[newfd] = f;
	return newfd;
}

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	struct thread *cur = thread_current ();
	struct file *file = cur->fdt[fd];
	if (file == NULL || file == 1 || file == 2)
		return NULL;

	if (length == 0 || filesize (fd) == 0)
		return NULL;

	if (is_kernel_vaddr (addr) || is_kernel_vaddr (addr + length) || pg_ofs (addr))
		return NULL;

	if (addr == NULL || addr + length == NULL)
		return false;

	if (spt_find_page (&cur->spt, addr) || offset % PGSIZE)
		return false;

	return do_mmap (addr, length, writable, file, offset);
}

void munmap (void *addr) {
	check_address (addr);
	do_munmap (addr);
}
