#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct file_info {
    struct file *file;
    off_t ofs;
    uint32_t page_read_bytes;
    uint32_t page_zero_bytes;
};

struct MapElem {
	struct file *key;
	struct file *value;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack (char **argv, int argc, struct intr_frame *if_);
struct thread *get_child_process (int pid);

#endif /* userprog/process.h */
