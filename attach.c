/*
 * Copyright (C) 2011 by Nelson Elhage
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <sys/types.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "ptrace.h"
#include "reptyr.h"

#define PAGE_SZ sysconf(_SC_PAGE_SIZE)

#define TASK_COMM_LENGTH 16
struct proc_stat {
    pid_t pid;
    char comm[TASK_COMM_LENGTH+1];
    char state;
    pid_t ppid, sid, pgid;
    dev_t ctty;
};

#define do_syscall(child, name, a0, a1, a2, a3, a4, a5) \
    ptrace_remote_syscall((child), ptrace_syscall_numbers((child))->nr_##name, \
                          a0, a1, a2, a3, a4, a5)

#define assert_nonzero(expr) ({                         \
            typeof(expr) __val = expr;                  \
            if (__val == 0)                             \
                die("Unexpected: %s == 0!\n", #expr);   \
            __val;                                      \
        })

int parse_proc_stat(int statfd, struct proc_stat *out) {
    char buf[1024];
    int n;
    unsigned dev;
    lseek(statfd, 0, SEEK_SET);
    if (read(statfd, buf, sizeof buf) < 0)
        return assert_nonzero(errno);
    n = sscanf(buf, "%d (%16[^)]) %c %d %d %d %u",
               &out->pid, out->comm,
               &out->state, &out->ppid, &out->sid,
               &out->pgid, &dev);
    if (n == EOF)
        return assert_nonzero(errno);
    if (n != 7) {
        return EINVAL;
    }
    out->ctty = dev;
    return 0;
}

int read_proc_stat(pid_t pid, struct proc_stat *out) {
    char stat_path[PATH_MAX];
    int statfd;
    int err;

    snprintf(stat_path, sizeof stat_path, "/proc/%d/stat", pid);
    statfd = open(stat_path, O_RDONLY);
    if (statfd < 0) {
        error("Unable to open %s: %s", stat_path, strerror(errno));
        return -statfd;
    }

    err = parse_proc_stat(statfd, out);
    close(statfd);
    return err;
}

static int do_mmap(struct ptrace_child *child, child_addr_t *arg_addr, unsigned long len) {
    int mmap_syscall = ptrace_syscall_numbers(child)->nr_mmap2;
    child_addr_t addr;
    if (mmap_syscall == -1)
        mmap_syscall = ptrace_syscall_numbers(child)->nr_mmap;
    addr = ptrace_remote_syscall(child, mmap_syscall, 0,
                                         PAGE_SZ, PROT_READ|PROT_WRITE,
                                         MAP_ANONYMOUS|MAP_PRIVATE, 0, 0);
    if (addr > (unsigned long) -1000)
        return -(signed long)addr;
    *arg_addr = addr;
    return 0;
}

static void do_unmap(struct ptrace_child *child, child_addr_t addr, unsigned long len) {
    if (addr == (unsigned long)-1)
        return;
    do_syscall(child, munmap, addr, len, 0, 0, 0, 0);
}

int check_pgroup(pid_t target) {
    pid_t pg;
    DIR *dir;
    struct dirent *d;
    pid_t pid;
    char *p;
    int err = 0;
    struct proc_stat pid_stat;

    debug("Checking for problematic process group members...");

    pg = getpgid(target);
    if (pg < 0) {
        error("Unable to get pgid (does process %d exist?)", (int)target);
        return pg;
    }

    if ((dir = opendir("/proc/")) == NULL)
        return assert_nonzero(errno);

    while ((d = readdir(dir)) != NULL) {
        if (d->d_name[0] == '.') continue;
        pid = strtol(d->d_name, &p, 10);
        if (*p) continue;
        if (pid == target) continue;
        if (getpgid(pid) == pg) {
            /*
             * We are actually being somewhat overly-conservative here
             * -- if pid is a child of target, and has not yet called
             * execve(), reptyr's setpgid() strategy may suffice. That
             * is a fairly rare case, and annoying to check for, so
             * for now let's just bail out.
             */
            if ((err = read_proc_stat(pid, &pid_stat))) {
                memcpy(pid_stat.comm, "???", 4);
            }
            error("Process %d (%.*s) shares %d's process group. Unable to attach.\n"
                  "(This most commonly means that %d has suprocesses).",
                  (int)pid, TASK_COMM_LENGTH, pid_stat.comm, (int)target, (int)target);
            err = EINVAL;
            goto out;
        }
    }
 out:
    closedir(dir);
    return err;
}

int child_attach(pid_t pid, struct ptrace_child *child, child_addr_t *scratch_page) {
    int err = 0;

    err = check_pgroup(pid);
    if (err)
        return err;

    if (ptrace_attach_child(child, pid))
        return child->error;

    if (ptrace_advance_to_state(child, ptrace_at_syscall))
        return child->error;

    if (ptrace_save_regs(child))
        return child->error;

    err = do_mmap(child, scratch_page, PAGE_SZ);
    if (err)
	return err;

    debug("Allocated scratch page: %lx", *scratch_page);
    return 0;
}

int child_detach(struct ptrace_child *child, unsigned long scratch_page) {
    do_unmap(child, scratch_page, PAGE_SZ);

    ptrace_restore_regs(child);
    ptrace_detach_child(child);
    return 0;
}

int child_open(struct ptrace_child *child, unsigned long scratch_page, const char *file) {
    int child_fd;

    if (ptrace_memcpy_to_child(child, scratch_page, file, strlen(file)+1)) {
	error("Unable to memcpy the pty path to child.");
	return child->error;
    }

    child_fd = do_syscall(child, open,
	    scratch_page, O_RDWR|O_CREAT, 0666,
	    0, 0, 0);
    if (child_fd < 0) {
	error("Unable to open the tty in the child.");
	return child_fd;
    }

    debug("Opened the new tty in the child: %d", child_fd);
    return child_fd;
}

int child_dup(struct ptrace_child *child, int file_fd, int orig_fd, int save_orig) {
    int save_fd = -1;
    if (save_orig)
	save_fd = do_syscall(child, dup,
                          orig_fd,
                          0, 0, 0, 0, 0);
    do_syscall(child, dup2, file_fd, orig_fd, 0, 0, 0, 0);
    do_syscall(child, close, file_fd, 0, 0, 0, 0, 0);
    return save_fd;
}

