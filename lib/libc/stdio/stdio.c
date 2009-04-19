/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)stdio.c	8.1 (Berkeley) 6/4/93
 * $FreeBSD: src/lib/libc/stdio/stdio.c,v 1.28 2008/05/05 16:14:02 jhb Exp $
 * $DragonFly: src/lib/libc/stdio/stdio.c,v 1.7 2005/11/20 11:07:30 swildner Exp $
 */

#include "namespace.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"
#include "local.h"
#include "priv_stdio.h"

/*
 * Small standard I/O/seek/close functions.
 */
int
__sread(void *cookie, char *buf, int n)
{
	FILE *fp = cookie;

	return(_read(fp->pub._fileno, buf, (size_t)n));
}

int
__swrite(void *cookie, const char *buf, int n)
{
	FILE *fp = cookie;

	return (_write(fp->pub._fileno, buf, (size_t)n));
}

fpos_t
__sseek(void *cookie, fpos_t offset, int whence)
{
	FILE *fp = cookie;

	return (lseek(fp->pub._fileno, (off_t)offset, whence));
}

int
__sclose(void *cookie)
{

	return (_close(((FILE *)cookie)->pub._fileno));
}

/*
 * Higher level wrappers.
 */
int
_sread(FILE *fp, char *buf, int n)
{
	int ret;

	ret = (*fp->_read)(fp->_cookie, buf, n);
	if (ret > 0) {
		if (fp->pub._flags & __SOFF) {
			if (fp->_offset <= OFF_MAX - ret)
				fp->_offset += ret;
			else
				fp->pub._flags &= ~__SOFF;
		}
	} else if (ret < 0)
		fp->pub._flags &= ~__SOFF;
	return (ret);
}

int
_swrite(FILE *fp, const char *buf, int n)
{
	int ret;
	int serrno;

	if (fp->pub._flags & __SAPP) {
		serrno = errno;
		if (_sseek(fp, (fpos_t)0, SEEK_END) == -1 &&
		    (fp->pub._flags & __SOPT))
			return (-1);
		errno = serrno;
	}
	ret = (*fp->_write)(fp->_cookie, buf, n);
	/* __SOFF removed even on success in case O_APPEND mode is set. */
	if (ret >= 0) {
		if ((fp->pub._flags & (__SAPP|__SOFF)) == (__SAPP|__SOFF) &&
		    fp->_offset <= OFF_MAX - ret)
			fp->_offset += ret;
		else
			fp->pub._flags &= ~__SOFF;

	} else if (ret < 0)
		fp->pub._flags &= ~__SOFF;
	return (ret);
}

fpos_t
_sseek(FILE *fp, fpos_t offset, int whence)
{
	fpos_t ret;
	int serrno, errret;

	serrno = errno;
	errno = 0;
	ret = (*fp->_seek)(fp->_cookie, offset, whence);
	errret = errno;
	if (errno == 0)
		errno = serrno;
	/*
	 * Disallow negative seeks per POSIX.
	 * It is needed here to help upper level caller
	 * in the cases it can't detect.
	 */
	if (ret < 0) {
		if (errret == 0) {
			if (offset != 0 || whence != SEEK_CUR) {
				if (HASUB(fp))
					FREEUB(fp);
				fp->pub._p = fp->_bf._base;
				fp->pub._r = 0;
				fp->pub._flags &= ~__SEOF;
			}
			fp->pub._flags |= __SERR;
			errno = EINVAL;
		} else if (errret == ESPIPE)
			fp->pub._flags &= ~__SAPP;
		fp->pub._flags &= ~__SOFF;
		ret = -1;
	} else if (fp->pub._flags & __SOPT) {
		fp->pub._flags |= __SOFF;
		fp->_offset = ret;
	}
	return (ret);
}
