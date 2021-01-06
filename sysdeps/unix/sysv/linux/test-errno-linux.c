/* Test that failing system calls do set errno to the correct value.
   Linux sycalls version.

   Copyright (C) 2017 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/fsuid.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/quota.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/swap.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

/* This is not an exhaustive test: only system calls that can be
   persuaded to fail with a consistent error code and no side effects
   are included.  Usually these are failures due to invalid arguments,
   with errno code EBADF or EINVAL.  The order of argument checks is
   unspecified, so we must take care to provide arguments that only
   allow _one_ failure mode.

   Note that all system calls that can fail with EFAULT are permitted
   to deliver a SIGSEGV signal instead, so we avoid supplying invalid
   pointers in general, and we do not attempt to test system calls
   that can only fail with EFAULT (e.g. gettimeofday, gethostname).

   Also note that root-only system calls (e.g. acct, reboot) may, when
   the test is run as an unprivileged user, fail due to insufficient
   privileges before bothering to do argument checks, so those are not
   tested either.

   Also, system calls that take enum or a set of flags as argument is
   not tested if POSIX doesn't specify exact binary values for all
   flags, and so any value passed to flags may become valid.

   Some tests assume "/bin/sh" names a file that exists and is not a
   directory.  */

#define test_wrp_rv(rtype, prtype, experr, syscall, ...)	\
  (__extension__ ({						\
    errno = 0xdead;						\
    rtype ret = syscall (__VA_ARGS__);				\
    int err = errno;						\
    int fail;							\
    if ((ret == (rtype) -1) && (err == experr))			\
      fail = 0;							\
    else							\
      {								\
        fail = 1;						\
        if (ret != (rtype) -1)					\
          printf ("FAIL: " #syscall ": didn't fail as expected"	\
		  " (return "prtype")\n", ret);			\
        else if (err == 0xdead)					\
          puts ("FAIL: " #syscall ": didn't update errno");	\
        else if (err != experr)					\
          printf ("FAIL: " #syscall				\
		  ": errno is: %d (%s) expected: %d (%s)\n",	\
		  err, strerror (err), experr, strerror (experr));\
      }								\
    fail;							\
  }))

#define test_wrp_rv2(rtype, prtype, experr1, experr2, syscall, ...) 	\
  (__extension__ ({							\
    errno = 0xdead;							\
    rtype ret = syscall (__VA_ARGS__);					\
    int err = errno;							\
    int fail;								\
    if ((ret == (rtype) -1) && ((err == experr1) || (err == experr2)))	\
      fail = 0;								\
    else								\
      {									\
        fail = 1;							\
        if (ret != (rtype) -1)						\
          printf ("FAIL: " #syscall ": didn't fail as expected"		\
		  " (return "prtype")\n", ret);				\
        else if (err == 0xdead)						\
          puts ("FAIL: " #syscall ": didn't update errno");		\
        else if (err != experr1 && err != experr2)			\
          printf ("FAIL: " #syscall					\
		  ": errno is: %d (%s) expected: %d (%s) or %d (%s)\n",	\
		  err, strerror (err), experr1, strerror (experr1),	\
		  experr2, strerror (experr2));				\
      }									\
    fail;								\
  }))

#define test_wrp(experr, syscall, ...)				\
  test_wrp_rv(int, "%d", experr, syscall, __VA_ARGS__)

#define test_wrp2(experr1, experr2, syscall, ...)		\
  test_wrp_rv2(int, "%d", experr1, experr2, syscall, __VA_ARGS__)

static int
do_test (void)
{
  fd_set rs, ws, es;
  int status;
  off_t off;
  stack_t ss;
  struct dqblk dqblk;
  struct epoll_event epoll_event;
  struct pollfd pollfd;
  struct sched_param sch_param;
  struct timespec ts;
  struct timeval tv;
  unsigned char vec[16];
  ss.ss_flags = ~SS_DISABLE;
  ts.tv_sec = -1;

  int fails = 0;
  fails |= test_wrp (EINVAL, epoll_create, -1);
  fails |= test_wrp (EINVAL, epoll_create1, EPOLL_CLOEXEC + 1);
  fails |= test_wrp (EBADF, epoll_ctl, -1, EPOLL_CTL_ADD, 0, &epoll_event);
  fails |= test_wrp (EBADF, epoll_wait, -1, &epoll_event, 1, 1);
  fails |= test_wrp (EBADF, fdatasync, -1);
  fails |= test_wrp (EBADF, flock, -1, LOCK_SH);
  fails |= test_wrp (ESRCH, getpgid, -1);
  /* Linux v3.8 (676a0675c) removed the test to check at least one valid
     bit in flags (to return EINVAL).  It was later added back in v3.9
     (04df32fa1).  */
  fails |= test_wrp2 (EINVAL, EBADF, inotify_add_watch, -1, "/", 0);
  fails |= test_wrp (EINVAL, mincore, (void *) -1, 0, vec);
  /* mlock fails if the result of the addition addr+len was less than addr
     (which indicates final address overflow), however on 32 bits binaries
     running on 64 bits kernels, internal syscall address check won't result
     in an invalid address and thus syscalls fails later in vma
     allocation.  */
  fails |= test_wrp2 (EINVAL, ENOMEM, mlock, (void *) -1, 1);
  fails |= test_wrp (EINVAL, nanosleep, &ts, &ts);
  fails |= test_wrp (EINVAL, poll, &pollfd, -1, 0);
  /* quotactl returns ENOSYS for kernels not configured with CONFIG_QUOTA.  */
  fails |= test_wrp2 (ENODEV, ENOSYS, quotactl, Q_GETINFO, NULL, -1,
                     (caddr_t) &dqblk);
  fails |= test_wrp (EINVAL, sched_getparam, -1, &sch_param);
  fails |= test_wrp (EINVAL, sched_getscheduler, -1);
  fails |= test_wrp (EINVAL, sched_get_priority_max, -1);
  fails |= test_wrp (EINVAL, sched_get_priority_min, -1);
  fails |= test_wrp (EINVAL, sched_rr_get_interval, -1, &ts);
  fails |= test_wrp (EINVAL, sched_setparam, -1, &sch_param);
  fails |= test_wrp (EINVAL, sched_setscheduler, -1, 0, &sch_param);
  fails |= test_wrp (EINVAL, select, -1, &rs, &ws, &es, &tv);
  fails |= test_wrp (EBADF, sendfile, -1, -1, &off, 0);
  fails |= test_wrp (EINVAL, sigaltstack, &ss, NULL);
  fails |= test_wrp (ECHILD, wait4, -1, &status, 0, NULL);

  return fails;
}

#include "support/test-driver.c"
