/*
 * GLOBALDATA.H
 */
/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/lib/libcaps/globaldata.h,v 1.2 2003/12/04 22:06:19 dillon Exp $
 */

#ifndef _LIBCAPS_GLOBALDATA_H_
#define _LIBCAPS_GLOBALDATA_H_

#ifndef _LIBCAPS_THREAD_H_
#include "thread.h"
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_UPCALL_H_
#include <sys/upcall.h>
#endif
#ifndef _LIBCAPS_SLABALLOC_H_
#include "slaballoc.h"
#endif
#ifndef _ASSERT_H_
#include <assert.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _MACHINE_LOCK_H_
#include <machine/lock.h>
#endif

struct globaldata;
typedef struct globaldata *globaldata_t;

#include "md_globaldata.h"

extern int smp_active;
extern int ncpus;
extern int hz;
extern u_int32_t stopped_cpus;
extern char *panicstr;

struct globaldata {
	struct globaldata *gd_self;		/* self pointer */
	struct upcall	gd_upcall;		/* upcall for this cpu */
	int		gd_upcid;		/* upcall id */
	struct thread	*gd_curthread;
	int		gd_tdfreecount;
        TAILQ_HEAD(,thread) gd_tdallq;          /* all threads */
        TAILQ_HEAD(,thread) gd_tdfreeq;         /* new thread cache */
        TAILQ_HEAD(,thread) gd_tdrunq[32];      /* runnable threads */
        __uint32_t      gd_runqmask;            /* which queues? */
        __uint32_t      gd_cpuid;
	int		gd_intr_nesting_level;
	struct thread   gd_idlethread;
	SLGlobalData    gd_slab;                /* slab allocator */
	int		gd_num_threads;		/* Number of threads */
	struct lwkt_ipiq *gd_ipiq;
};

#define gd_reqflags	gd_upcall.pending

#define KASSERT(exp, printargs)	\
	do { if (!(exp)) { panic printargs; } } while(0)
#define KKASSERT(exp)	assert(exp)

#define MAXVCPU		32

#define curthread	(mycpu->gd_curthread)

extern struct globaldata *globaldata_find(int cpu);

void globaldata_init(struct thread *td);
void splz(void);
int need_resched(void);
void cpu_send_ipiq(int dcpu);
void mi_gdinit(globaldata_t gd, int cpuid);
__dead2 void panic(const char *, ...);

#endif
