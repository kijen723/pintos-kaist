#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "threads/thread.h"

struct lock filesys_lock;

void syscall_init (void);
void halt (void);
void exit (int status);
int fork (const char *thread_name);
int exec (const char *file_name);
int wait (tid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
struct page *check_address (uint64_t addr);
void check_valid_buffer (void *buffer, unsigned size, void *rsp, bool to_write);
int dup2 (int oldfd, int newfd);
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

#endif /* userprog/syscall.h */
