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
 * Revision 1.2  2008-08-19 00:58:13  tino
 * Option -s (shared/read lock)
 *
 * Revision 1.1  2008-08-18 23:42:58  tino
 * First commit
 */

#include "tino/proc.h"
#include "tino/getopt.h"

#include <fcntl.h>
#include <errno.h>

#include "lockrun_version.h"

int
main(int argc, char **argv)
{
  int	argn, no_wait, fd, ret, verbose, shared;
  char	*cause;
  pid_t	pid;
  struct flock	lk;

  argn	= tino_getopt(argc, argv, 2, 0,
		      TINO_GETOPT_VERSION(LOCKRUN_VERSION)
		      " file cmd [args..]",

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
		      TINO_GETOPT_MIN
		      "v	verbose, print exit status on error"
		      , &verbose,
		      1,
		      
		      NULL
		      );
  if (argn<=0)
    return 1;

  if ((fd=open(argv[argn], O_CREAT|O_RDWR, 0700))==-1)
    tino_exit("%s: cannot open %s", argv[argn+1], argv[argn]);
  argn++;

  lk.l_type	= shared ? F_RDLCK : F_WRLCK;
  lk.l_whence	= 0;
  lk.l_start	= 0;
  lk.l_len	= 0;
  if (fcntl(fd, F_SETLK, &lk))
    {
      if (no_wait)
	{
	  if (verbose>0)
	    fprintf(stderr, "%s: unable to aquire lock\n", argv[argn]);
	  return 1;
	}
      if (verbose>0)
	fprintf(stderr, "%s: waiting for lock\n", argv[argn]);
      while (fcntl(fd, F_SETLKW, &lk))
	if (errno!=EINTR)
	  tino_exit("%s: fcnt(SETLKW)", argv[argn]);
    }
  if (verbose>0)
    fprintf(stderr, "%s: got lock\n", argv[argn]);

  pid	= tino_fork_exec(0, 1, 2, argv+argn, NULL, 0, NULL);
  if (verbose>0)
    fprintf(stderr, "%s: running as PID %ld\n", argv[argn], (long)pid);

  ret	= tino_wait_child_exact(pid, &cause);
  if (cause && verbose>=0)
    fprintf(stderr, "%s: %s\n", argv[argn], cause);

  /* unlink is a bad idea, this may break another lock!
   */

  return ret;
}
