`reredirect` - A tool to dynamically redirect outputs of a running program
==========================================================================

`reredirect` is a utility for taking an existing running program and
attaching its output (standard output and error output) to files or
to another process.

Using `reredirect`, you can log output of an already launched process, redirect
debug output of a background process to `/dev/null` or to a pager as if you
launched it with `>` or `|`.

Usage
-----

Simple usage is:

    reredirect -m FILE PID

It will redirect output of PID to FILE. It is also possible to redirect standard
output and error output in different files:

    reredirect -o FILE1 -e FILE2 PID

`-m` option is just a shortcut to `-o FILE -e FILE`.

After being launched, reredirect give you the ability to restore state of PID.
It will look something like this : 

    reredirect -N -O 5 -E 3 5453

`-O` and `-E` act as `-o` and `-e` but with already opened file descriptors in
PID. They only used to restore previous state of PID.

Without `-N`, `reredirect` keep previous output opened which allow you to
restore them. This will produce file descriptor leak if you call
`reredirect` multiple times. You should use `-N` to close and forget previous
output.

Redirect to your terminal or a command
--------------------------------------

This package also provide an utility called `relink` that allows to redirect
output to current terminal. When `relink` exits (with Ctrl+C for exemple)
original state is restored and command is detached.

For exemple:

    relink 5453
    relink 5453 | grep usefull_line

`relink` maintains stderr from original command to stderr. So you can do things
like:

    relink 5453 > /dev/null

Internally, `relink` is just a small shell script that creates necessary context
and call `reredirect` as necessary. It uses "named pipes". Using "named pipes",
you can redirect output of your target to another command (as a normal pipe):

First create a named pipe:

    mkfifo /tmp/myfifo

Run `reredirect` to redirect your target to /tmp/myfifo:

    reredirect -m /tmp/myfifo PID

Launch a command on this named pipe:

    less < /tmp/myfifo
    tee my_log < /tmp/myfifo
    cat -n < /tmp/myfifo

Note that `relink` only redirects output. The target process keep its original
terminal. So if you type Ctrl+Z or CTRL+C, they are not sent to target process.
If you want to do that, you should check the [`reptyr`](http://github.com/nelhage/reptyr) command from Nelson Elhage.

Trick with Makefile
---------------------

Sometime, I work with complex projects and I want to log subparts of compilation
output in different files. I use this trick:

    target:
    	@FIFO=$$(mktemp -u); mkfifo $$FIFO; tee my_file.log < $$FIFO & ./redirect -m $$FIFO $$PPID > ./restore_$$PPID.cmd
    	@echo Call sub makefile here
    	@sh ./restore_$$PPID.cmd
    	@echo No more in log file

Portability
-----------

reredirect is Linux-only. It uses ptrace to attach to the target and control it at
the syscall level, so it is highly dependent on Linux's particular syscall API,
syscalls, and terminal ioctl()s. A port to Solaris or BSD may be technically
feasible, but would probably require significant re-architecting to abstract out
the platform-specific bits.

reredirect works on i386, x86_64, and ARM. Ports to other architectures should be
straightforward, and should in most cases be as simple as adding an arch/ARCH.h
file and adding a clause to the ifdef ladder in ptrace.c.

ptrace_scope on Ubuntu Maverick and up
--------------------------------------

`redirect` depends on the `ptrace` system call to attach to the remote program. On
Ubuntu Maverick and higher, this ability is disabled by default for security
reasons. You can enable it temporarily by doing

    # echo 0 > /proc/sys/kernel/yama/ptrace_scope

as root, or permanently by editing the file /etc/sysctl.d/10-ptrace.conf, which
also contains more information about exactly what this setting accomplishes.

How does it work?
-----------------

Reredirect acts as a debugger to take control of a running process (it uses ptrace 
syscall). Once it takes control of a running process, it uses classical calls to 
`dup`, and `dup2` to change targets of file descriptors 1 and 2.

Basicly, to redirect to file, this pseudo code is executed:

    orig_fd = 1;
    save_fd = dup(1);
    new_fd = open(file, O_WRONLY | O_CREAT, 0666);
    dup2(new_fd, orig_fd);
    close(new_fd);

and to restore state:

    ret = dup2(save_fd, orig_fd);
    close(save_fd);


Credits
-------

reredirect was mainly written by Jérôme Pouiller <jezz@sysmic.org>. You can
contact him for any questions or bug reports.

reredirect (and especially all ptrace layer) is based on reptyr programm. reptyr 
was written by Nelson Elhage <nelhage@nelhage.com>.

URL
---
[http://github.com/jerome-pouiller/reredirect]()
