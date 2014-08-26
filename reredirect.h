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
#include "ptrace.h"

#define REREDIRECT_VERSION "0.1"

int child_attach(pid_t pid, struct ptrace_child *child, child_addr_t *scratch_page);
int child_detach(struct ptrace_child *child, child_addr_t scratch_page);
int child_open(struct ptrace_child *child, child_addr_t scratch_page, const char *file);
int child_dup(struct ptrace_child *child, int file_fd, int orig_fd, int save_orig);

#define __printf __attribute__((format(printf, 1, 2)))
void __printf die(const char *msg, ...) __attribute__((noreturn));
void __printf debug(const char *msg, ...);
void __printf error(const char *msg, ...);
