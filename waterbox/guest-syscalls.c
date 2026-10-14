/* Sandbox-only replacements for C library calls whose system calls miniBox
 * does not have. One static link means defining these here takes the place
 * of the C library's versions. Not compiled into the native reference.
 *
 * PUAE asks whether a file is there before it opens it (a Kickstart, a
 * disk). In a sandbox there are files mounted by name and no folders: a path
 * is "there" when it can be opened, and what it is is what the open file
 * says; anything else is not there. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* The C library's fstat() is fstatat() of an empty path on an open file, so
 * this function is asked both things: about an open file (answered by the
 * system call the sandbox does have), and about a name (answered by opening
 * it). */
int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
	if (path && !*path && (flags & AT_EMPTY_PATH)) {
		if (syscall(SYS_fstat, dirfd, st) != 0) {
			errno = EBADF;
			return -1;
		}
		return 0;
	}
	if (!path || !*path) {
		errno = ENOENT;
		return -1;
	}
	const int fd = open(path, O_RDONLY);
	if (fd < 0) {
		errno = ENOENT;
		return -1;
	}
	const long r = syscall(SYS_fstat, fd, st);
	close(fd);
	if (r != 0) {
		errno = EIO;
		return -1;
	}
	return 0;
}

int stat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, 0); }
int lstat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, 0); }

int access(const char *path, int mode)
{
	struct stat st;
	(void)mode;
	return stat(path, &st);
}
