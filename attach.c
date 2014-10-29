/*
 * Copyright (C) 2011 by Nelson Elhage
 * Copyright (C) 2014 by Jérôme Pouiller <jezz@sysmic.org>
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
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <limits.h>

#include "ptrace.h"
#include "reredirect.h"

#define PAGE_SZ sysconf(_SC_PAGE_SIZE)

#define do_syscall(child, name, a0, a1, a2, a3, a4, a5) \
    ptrace_remote_syscall((child), ptrace_syscall_numbers((child))->nr_##name, \
                          a0, a1, a2, a3, a4, a5)

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

int child_attach(pid_t pid, struct ptrace_child *child, child_addr_t *scratch_page) {
    int err = 0;

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

int child_detach(struct ptrace_child *child, child_addr_t scratch_page) {
    do_unmap(child, scratch_page, PAGE_SZ);
    debug("Freed scratch page: %lx", scratch_page);

    ptrace_restore_regs(child);
    ptrace_detach_child(child);
    return 0;
}

int child_open(struct ptrace_child *child, child_addr_t scratch_page, const char *file) {
    int child_fd;
    char buf[PATH_MAX + 1];

    if (file[0] == '/') {
        strncpy(buf, file, sizeof(buf));
    } else {
        getcwd(buf, sizeof(buf));
        strncat(buf, "/", sizeof(buf));
        strncat(buf, file, sizeof(buf));
    }

    if (ptrace_memcpy_to_child(child, scratch_page, buf, strlen(buf) + 1)) {
        error("Unable to memcpy the pty path to child.");
        return child->error;
    }

    child_fd = do_syscall(child, open, scratch_page,
                          O_RDWR | O_CREAT, 0666, 0, 0, 0);
    if (child_fd < 0) {
        error("Unable to open the file in the child.");
        return child_fd;
    }

    debug("Opened the new fd in the child: %d (%s)", child_fd, file);
    return child_fd;
}

int child_dup(struct ptrace_child *child, int file_fd, int orig_fd, int save_orig) {
    int save_fd = -1;
    int err;

    if (save_orig)
        save_fd = do_syscall(child, dup, orig_fd, 0, 0, 0, 0, 0);
    debug("Saved fd %d to %d in the child", orig_fd, save_fd);

    err = do_syscall(child, dup2, file_fd, orig_fd, 0, 0, 0, 0);
    if (err < 0) {
        error("Unable to dup2 in the child.");
        return save_fd;
    }
    debug("Duplicated fd %d to %d", file_fd, orig_fd);

    err = do_syscall(child, close, file_fd, 0, 0, 0, 0, 0);
    if (err < 0) {
        error("Unable to close in the child.");
        return save_fd;
    }

    debug("Closed fd %d", file_fd);
    return save_fd;
}

