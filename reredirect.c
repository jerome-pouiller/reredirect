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
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/limits.h>
#include "reredirect.h"

static int verbose = 0;

static void usage() {
    char *me = program_invocation_short_name;
    fprintf(stderr, "Usage: %s [-m FILE|-o FILE|-e FILE|-O FD|-E FD] [-N] [-d] PID\n", me);
    fprintf(stderr, "%s redirect outputs of a running process to a file.\n", me);
    fprintf(stderr, "  PID      Process to reattach\n");
    fprintf(stderr, "  -o FILE  File to redirect stdout. \n");
    fprintf(stderr, "  -e FILE  File to redirect stderr.\n");
    fprintf(stderr, "  -m FILE  Same than -o FILE -e FILE.\n");
    fprintf(stderr, "  -O FD    Redirect stdout to this file descriptor. Mainly used to restore\n");
    fprintf(stderr, "           process outputs.\n");
    fprintf(stderr, "  -E FD    Redirect stderr to this file descriptor. Mainly used to restore\n");
    fprintf(stderr, "           process outputs.\n");
    fprintf(stderr, "  -N       Do not save previous stream.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Notice you can redirect to another program using name pipe. For example:\n");
    fprintf(stderr, "   mkfifo /tmp/fifo\n");
    fprintf(stderr, "   tee /tmp/log < /tmp/fifo\n");
    fprintf(stderr, "   %s PID -m /tmp/fifo\n", me);
}

static void _debug(const char *pfx, const char *msg, va_list ap) {
    if (pfx)
        fprintf(stderr, "%s", pfx);
    vfprintf(stderr, msg, ap);
    fprintf(stderr, "\n");
}

void debug(const char *msg, ...) {
    va_list ap;

    if (!verbose)
        return;

    va_start(ap, msg);
    _debug("[+] ", msg, ap);
    va_end(ap);
}

void error(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    _debug("[-] ", msg, ap);
    va_end(ap);
}

void die(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    _debug("[!] ", msg, ap);
    va_end(ap);

    exit(1);
}

void usage_die(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    _debug("[!] ", msg, ap);
    va_end(ap);
    usage();

    exit(1);
}

static void check_yama_ptrace_scope(void) {
    int fd = open("/proc/sys/kernel/yama/ptrace_scope", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        int n;
        n = read(fd, buf, sizeof buf);
        close(fd);
        if (n > 0) {
            if (!atoi(buf)) {
                return;
            }
        }
    } else if (errno == ENOENT)
        return;
    fprintf(stderr, "The kernel denied permission while attaching. If your uid matches\n");
    fprintf(stderr, "the target's, check the value of /proc/sys/kernel/yama/ptrace_scope.\n");
    fprintf(stderr, "For more information, see /etc/sysctl.d/10-ptrace.conf\n");
}

int main(int argc, char **argv) {
    int no_restore = 0;
    int fde = -1;
    int fdo = -1;
    int fde_orig = -1;
    int fdo_orig = -1;
    const char *fileo = NULL;
    const char *filee = NULL;
    pid_t pid;
    int opt;
    int err;
    unsigned long scratch_page = (unsigned long) -1;
    struct ptrace_child child;

    while ((opt = getopt(argc, argv, "m:o:e:O:E:s:dNvh")) != -1) {
        switch (opt) {
            case 'O':
                if (fileo || fdo >= 0)
                    usage_die("-m, -o and -O are exclusive\n");
                fdo = atoi(optarg);
                break;
            case 'E':
                if (filee || fde >= 0)
                    usage_die("-m, -e and -E are exclusive\n");
                fde = atoi(optarg);
                break;
            case 'o':
                if (fileo || fdo >= 0)
                    usage_die("-m, -o and -O are exclusive\n");
                fileo = optarg;
                break;
            case 'e':
                if (filee || fde >= 0)
                    usage_die("-m, -e and -E are exclusive\n");
                filee = optarg;
                break;
            case 'm':
                if (filee || fde >= 0 || fileo || fdo >= 0)
                    usage_die("-m is exclusive with  -o, -e, -O and -E\n");
                fileo = filee = optarg;
                break;
            case 'N':
                no_restore = 1;
                break;
            case 'h':
                usage(argv[0]);
                exit(0);
                break;
            case 'v':
                verbose = 1;
                break;
            case 'V':
                printf("This is reredirect version %s.\n", REREDIRECT_VERSION);
                printf("http://github.com/jerome-pouiller/reredirect/\n");
                exit(0);
            default: /* '?' */
                usage_die("Unknown option\n");
                break;
        }
    }

    if (optind >= argc)
        usage_die("No pid specified to attach\n");

    pid = atoi(argv[optind]);
    err = child_attach(pid, &child, &scratch_page);
    if (err) {
        fprintf(stderr, "Unable to attach to pid %d: %s\n", pid, strerror(err));
        if (err == EPERM)
            check_yama_ptrace_scope();
        exit(1);
    }
    if (fileo)
        fdo = child_open(&child, scratch_page, fileo);
    if (filee)
        fde = child_open(&child, scratch_page, filee);
    if (fdo >= 0)
        fdo_orig = child_dup(&child, fdo, 1, !no_restore);
    if (fde >= 0)
        fde_orig = child_dup(&child, fde, 2, !no_restore);
    child_detach(&child, scratch_page);

    if (!no_restore) {
        printf("# Previous state saved. To restore, use:\n");
        printf("%s -N -O %d -E %d %d\n", program_invocation_name, fdo_orig, fde_orig, pid);
    }

    return 0;
}
