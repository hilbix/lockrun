/* $Header$
 *
 * Copyright (C)2008-2009 Valentin Hilbig <webmaster@scylla-charybdis.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * $Log$
 * Revision 1.9  2009-07-27 00:03:49  tino
 * Option -f
 *
 * Revision 1.8  2009-02-27 11:47:51  tino
 * Options -c -d -l
 *
 * Revision 1.7  2008-10-17 19:02:47  tino
 * Options -a and -e
 *
 * Revision 1.6  2008-10-15 23:55:36  tino
 * Bugfix for large files
 *
 * Revision 1.5  2008-09-20 23:11:21  tino
 * Option -w
 *
 * Revision 1.4  2008-09-20 21:32:54  tino
 * Option -u added
 *
 * Revision 1.3  2008-08-19 01:19:52  tino
 * Print lockfile on lockfile error
 *
 * Revision 1.2  2008-08-19 00:58:13  tino
 * Option -s (shared/read lock)
 */

#include "tino/alarm.h"
#include "tino/fileerr.h"
#include "tino/proc.h"
#include "tino/getopt.h"

#include <fcntl.h>
#include <errno.h>

#include "lockrun_version.h"

static const char lockrun_signature[]=
"THIS IS A LOCKFILE CREATED BY THE COMMAND lockrun.\n"
"IT VANISHES USUALLY WHEN IT IS NO MORE NEEDED.\n"
"YOU CAN REMOVE IT SAFELY IF THE FILE IS LEFT OVER\n"
"AND NO MORE lockrun PROCESS KEEPS THE LOCK.\n"
"THIS FILE SHALL ONLY CONTAIN THIS MESSAGE,\n"
"INCLUDING THE LAST LINEFEED, BUT NOTHING ELSE.\n";

static int
lock_timeout(void *user, long delta, time_t now, long run)
{
  char **argv=user;

  fprintf(stderr, "%s: timeout waiting for lock %s\n", argv[1], argv[1]);
  exit(1);
}

static int
signature(int fd, const char *name)
{
  char	cmp[sizeof lockrun_signature];
  int	len;

  tino_file_seek_uA(fd, 0, name);
  len	= tino_file_readA(fd, cmp, sizeof cmp, name);
  if (!len)
    return 0;

  if (len!=(sizeof lockrun_signature)-1 || memcmp(cmp, lockrun_signature, (sizeof lockrun_signature)-1))
    tino_exit("%s: lockfile signature mismatch", name);

  return 1;
}

static void
sp(FILE *fd, const char *ptr, char c)
{
  while (*ptr++)
    putc(c, fd);
}

static void
bs(FILE *fd, const char *ptr)
{
  sp(fd, ptr, '\b');
}

int
main(int argc, char **argv)
{
  int	argn, no_wait, fd, ret, verbose, shared, had_display;
  int	create_unlink, fail_missing;
  int	log_fd;
  const char	*name, *env_name, *env_append, *display_wait, *display_clean;
  char	*env[2];
  char	*cause;
  pid_t	pid;
  long	timeout;
  FILE	*display;

  argn	= tino_getopt(argc, argv, 2, 0,
		      TINO_GETOPT_VERSION(LOCKRUN_VERSION)
		      " lockfile cmd [args..]\n"
		      "	Returns false if it was unable to aquire lock,\n"
		      "	else returns the return value of cmd",

		      TINO_GETOPT_USAGE
		      "h	this help"
		      ,

		      TINO_GETOPT_STRING
		      "a str	Append PID to the given string for LOCKRUNPID\n"
		      "		This sets strPID instead of PID (PID is numeric).\n"
		      "		Example to make a shell script single run only:\n"
		      "		:	[ \"MAGIC$PPID\" = \"$LOCKRUNPID\" ] ||\n"
		      "		:	lockrun -qna MAGIC /tmp/lock.MAGIC \"$0\" \"$@\" ||\n"
		      "		:	exit 1"
		      , &env_append,

		      TINO_GETOPT_STRING
		      "c str	cleanup string to print after waiting.  See also -d\n"
		      "		Default is to backspace-overwrite -d with spaces.\n"
		      "		If -d is missing, str is backspace-printed while waiting.\n"
		      "		With -c the cursor is on the start of str, with -d on the end"
		      , &display_clean,

		      TINO_GETOPT_STRING
		      "d str	display str while waiting.  See also -c\n"
		      "		Writes to stdout if -l is not present, else stderr."
		      , &display_wait,

		      TINO_GETOPT_STRING
		      TINO_GETOPT_DEFAULT
		      "e name	Environment variable to set.  This is set to the\n"
		      "		PID of the lockrun process (which is the parent PID\n"
		      "		in the cmd which is run.  Set empty to suppress var"
		      , &env_name,
		      "LOCKRUNPID",

		      TINO_GETOPT_FLAG
		      "f	fail if lockfile is missing (never creates the file).\n"
		      "		Only safe with -u if the file is empty and not needed."
		      , &fail_missing,

		      TINO_GETOPT_INT
		      TINO_GETOPT_DEFAULT
		      "l fd	Write logging to this fd, not stderr"
		      , &log_fd,
		      -1,

		      TINO_GETOPT_FLAG
		      "n	nowait, terminate if you cannot get the lock"
		      , &no_wait,
#if 0
		      TINO_GETOPT_FLAG
		      TINO_GETOPT_DEFAULT
		      "o fd	keep lock open with the given file descriptor.\n"
		      "		If the program closes FD, the lock is broken.\n"
		      "		Default is to take the next free FD.\n"
		      "		You should not use this with -u"
		      , &lock_fd,
		      -1,
#endif
#if 0
		      TINO_GETOPT_STRING
		      TINO_GETOPT_DEFAULT
		      "p env	Put the FD of the lock into environment variable\n"
		      "		This variable is only present for -o or -t"
		      , &lockfd_env,
		      "LOCKRUNFD",
#endif		      
		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      "q	quiet, do not print exit status of child"
		      , &verbose,
		      -1,
#if 0
		      TINO_GETOPT_FLAG
		      "r	run command non-forking.  Implies -o.\n"
		      "		Incompatible with -u"
		      TINO_GETOPT_FLAG
		      , &direct_run,
#endif		      
		      TINO_GETOPT_FLAG
		      "s	get a shared lock (default: exclusive)"
		      , &shared,
#if 0
		      TINO_GETOPT_FLAG
		      "t	test if lock exists (run with /bin/true)"
		      , &only_test,
#endif
		      TINO_GETOPT_FLAG
		      "u	creates (if no -f present) and unlinks lockfile\n"
		      "		Shared locks are supported if all lockruns use -u"
		      , &create_unlink,

		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      "v	verbose, print exit status on error"
		      , &verbose,
		      1,
		      
		      TINO_GETOPT_LONGINT
		      TINO_GETOPT_TIMESPEC
		      "w time	maximum wait time, 0 is unlimited"
		      , &timeout,
#if 0
		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      "x	exclusive lock (default)"
		      , &shared,
		      0,
#endif
		      NULL
		      );
  if (argn<=0)
    return 1;

  display	= stdout;
  if (log_fd>=0)
    {
      display	= stderr;
      if (log_fd!=2 && dup2(log_fd, 2)!=2)
	tino_exit("cannot dup %d to stderr", log_fd);
    }

  if ((long)(int)timeout!=timeout)
    {
      if (verbose>=0)
	fprintf(stderr, "lockrun: timeout value too high, reduced to MAXINT\n");
      timeout	= -1;
    }

  if (timeout)
    tino_alarm_set((int)timeout, lock_timeout, argv+argn);

  name	= argv[argn];
  argn++;

  had_display=0;
  for (;; tino_file_closeE(fd))
    {
      if ((fd=(fail_missing ? tino_file_openE(name, O_RDWR|O_APPEND) : tino_file_open_createE(name, O_RDWR|O_APPEND, 0700)))==-1)
	tino_exit("%s: cannot open %s", argv[argn], name);

      if (create_unlink && !tino_file_lock_exclusiveA(fd, 0, name))
	{
	  /* We have the exclusive lock, so no other process is using
	   * that file.  (If we cannot lock, the signature is written
	   * by another process.)
	   *
	   * Place the signature and unlock file.
	   */
	  if (!signature(fd, name))
	    tino_file_writeA(fd, lockrun_signature, (sizeof lockrun_signature)-1, name);
	  tino_file_unlockA(fd, name);
	}

      if (tino_file_lockA(fd, !shared, 0, name))
	{
	  if (no_wait)
	    {
	      if (verbose>0)
		fprintf(stderr, "%s: unable to aquire lock %s\n", argv[argn], name);
	      return 1;
	    }
	  if (verbose>0)
	    fprintf(stderr, "%s: waiting for lock\n", argv[argn]);
	  if (!had_display && (display_wait || display_clean))
	    {
	      if (display_wait)
		{
		  fputs(display_wait, display);
		  had_display	= 1;
		}
	      else
		{
		  display_wait	= display_clean;
		  display_clean	= 0;
		  fputs(display_wait, display);
		  bs(display, display_wait);
		  had_display	= -1;
		}
	      fflush(display);
	    }
	  tino_file_lockA(fd, !shared, 1, name);
	}

      /* We now have a lock.  If it is create_unlink, check the file
       * again, as it might be, that it was unlink()ed.
       *
       * This can be done with a stat() call and comparing the inode number.
       *
       * No signature() checking is done here, as we can lock
       * non-created files, too.
       */
#if 0
      /* it cannot hurt to always check the file
       */
      if (create_unlink)
#endif
	{
	  tino_file_stat_t st1, st2;

	  if (tino_file_statE(name, &st1) || tino_file_stat_fdE(fd, &st2))
	    continue;
	  if (STAT2CMP(st1,st2,dev) || STAT2CMP(st1,st2,ino))
	    continue;
	}
      break;
    }
  if (had_display)
    {
      if (display_clean)
	{
	  fputs(display_clean, display);
	  fflush(display);
	}
      else
	{
	  if (had_display>0)
	    bs(display, display_wait);
	  sp(display, display_wait, ' ');
	  bs(display, display_wait);
	  fflush(display);
	}
    }

  if (verbose>0)
    fprintf(stderr, "%s: got lock\n", argv[argn]);

  env[0]	= *env_name ? tino_str_printf("%s=%s%lu", env_name, (env_append ? env_append : ""), (unsigned long)getpid()) : 0;
  env[1]	= 0;
  pid	= tino_fork_exec(0, 1, 2, argv+argn, env, 1, NULL);
  if (verbose>0)
    fprintf(stderr, "%s: running as PID %ld\n", argv[argn], (long)pid);

  ret	= tino_wait_child_exact(pid, &cause);
  if (cause && verbose>=0)
    fprintf(stderr, "%s: %s\n", argv[argn], cause);

  if (create_unlink)
    {
      /* If somebody else keeps a lock on the file,
       * it will be removed by this other process
       */
      if (!tino_file_lock_exclusiveA(fd, 0, name) && signature(fd, name))
	tino_file_unlinkE(name);
      /* I would like to be able to unlink an fd, that is:
       * Enumerate all directories which contain references (hardlinks) to the file
       * and safely (without race conditions) remove one of these entries.
       *
       * Well, Unix FS is no database, so we probably will never see this.
       *
       * One can emulate that:
       * a) create an empty temp directory
       * b) move (rename) the file there, now it is safe
       * c0) check if the FD still refers to the same file (by comparing inodes)
       * c1) If c0 fails then hardlink(!) the file back
       * c2) If c1 fails than bail out, leave the temp directory in place with file (or repeat c1 with a backup filename until it succeeds)
       * d) unlink in the directory
       * e) rmdir the directory
       * However that probably is plain overkill here.
       */
    }

  return ret;
}
