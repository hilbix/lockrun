/* $Header$
 *
 * Copyright (C)2008 Valentin Hilbig <webmaster@scylla-charybdis.com>
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

int
main(int argc, char **argv)
{
  int	argn, no_wait, fd, ret, verbose, shared;
  int	create_unlink;
  const char	*name;
  char	*cause;
  pid_t	pid;
  long	timeout;

  argn	= tino_getopt(argc, argv, 2, 0,
		      TINO_GETOPT_VERSION(LOCKRUN_VERSION)
		      " lockfile cmd [args..]\n"
		      "	Returns false if it was unable to aquire lock,\n"
		      "	else returns the return value of cmd",

		      TINO_GETOPT_USAGE
		      "h	this help"
		      ,

		      TINO_GETOPT_FLAG
		      "n	nowait, terminate if you cannot get the lock"
		      , &no_wait,
		      
		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      "q	quiet, do not print exit status of child"
		      , &verbose,
		      -1,
		      
		      TINO_GETOPT_FLAG
		      "s	get a shared lock (default: exclusive)"
		      , &shared,

		      TINO_GETOPT_FLAG
		      "u	creates and unlinks lockfile\n"
		      "		Shared locks are supported if all lockruns use -u"
		      , &create_unlink,

		      TINO_GETOPT_LONGINT
		      TINO_GETOPT_TIMESPEC
		      "w time	maximum wait time, 0 is unlimited"
		      , &timeout,

		      TINO_GETOPT_FLAG
		      TINO_GETOPT_MIN
		      "v	verbose, print exit status on error"
		      , &verbose,
		      1,
		      
		      NULL
		      );
  if (argn<=0)
    return 1;

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

  for (;; tino_file_closeE(fd))
    {
      if ((fd=open(name, O_CREAT|O_RDWR|O_APPEND, 0700))==-1)
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

  if (verbose>0)
    fprintf(stderr, "%s: got lock\n", argv[argn]);

  pid	= tino_fork_exec(0, 1, 2, argv+argn, NULL, 0, NULL);
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
    }

  return ret;
}
