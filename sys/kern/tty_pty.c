/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
 *	@(#)tty_pty.c	8.4 (Berkeley) 2/20/95
 * $FreeBSD: src/sys/kern/tty_pty.c,v 1.74.2.4 2002/02/20 19:58:13 dillon Exp $
 * $DragonFly: src/sys/kern/tty_pty.c,v 1.21 2008/08/13 10:29:38 swildner Exp $
 */

/*
 * Pseudo-teletype Driver
 * (Actually two drivers, requiring two dev_ops structures)
 */
#include "use_pty.h"		/* XXX */
#include "opt_compat.h"

#include <sys/param.h>
#include <sys/systm.h>
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
#include <sys/ioctl_compat.h>
#endif
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/thread2.h>
#include <sys/devfs.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#define	UNIX98_PTYS	1

MALLOC_DEFINE(M_PTY, "ptys", "pty data structures");

static void ptsstart (struct tty *tp);
static void ptsstop (struct tty *tp, int rw);
static void ptcwakeup (struct tty *tp, int flag);
static void ptyinit (int n);
static int  filt_ptcread (struct knote *kn, long hint);
static void filt_ptcrdetach (struct knote *kn);
static int  filt_ptcwrite (struct knote *kn, long hint);
static void filt_ptcwdetach (struct knote *kn);

static	d_open_t	ptsopen;
static	d_close_t	ptsclose;
static	d_read_t	ptsread;
static	d_write_t	ptswrite;
static	d_ioctl_t	ptyioctl;
static	d_open_t	ptcopen;
static	d_close_t	ptcclose;
static	d_read_t	ptcread;
static	d_write_t	ptcwrite;
static	d_kqfilter_t	ptckqfilter;

#ifdef UNIX98_PTYS
DEVFS_DECLARE_CLONE_BITMAP(pty);

static	d_clone_t 	ptyclone;

static int	pty_debug_level = 0;

static struct dev_ops pts98_ops = {
	{ "pts98", 0, D_TTY | D_KQFILTER },
	.d_open =	ptsopen,
	.d_close =	ptsclose,
	.d_read =	ptsread,
	.d_write =	ptswrite,
	.d_ioctl =	ptyioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

static struct dev_ops ptc98_ops = {
	{ "ptc98", 0, D_TTY | D_KQFILTER | D_MASTER },
	.d_open =	ptcopen,
	.d_close =	ptcclose,
	.d_read =	ptcread,
	.d_write =	ptcwrite,
	.d_ioctl =	ptyioctl,
	.d_kqfilter =	ptckqfilter,
	.d_revoke =	ttyrevoke
};
#endif

#define	CDEV_MAJOR_S	5
static struct dev_ops pts_ops = {
	{ "pts", CDEV_MAJOR_S, D_TTY | D_KQFILTER },
	.d_open =	ptsopen,
	.d_close =	ptsclose,
	.d_read =	ptsread,
	.d_write =	ptswrite,
	.d_ioctl =	ptyioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

#define	CDEV_MAJOR_C	6
static struct dev_ops ptc_ops = {
	{ "ptc", CDEV_MAJOR_C, D_TTY | D_KQFILTER | D_MASTER },
	.d_open =	ptcopen,
	.d_close =	ptcclose,
	.d_read =	ptcread,
	.d_write =	ptcwrite,
	.d_ioctl =	ptyioctl,
	.d_kqfilter =	ptckqfilter,
	.d_revoke =	ttyrevoke
};

#define BUFSIZ 100		/* Chunk size iomoved to/from user */

struct	pt_ioctl {
	int	pt_flags;
	int	pt_flags2;
	int	pt_refs;	/* Structural references interlock S/MOPEN */
	int	pt_uminor;
	struct	kqinfo pt_kqr, pt_kqw;
	u_char	pt_send;
	u_char	pt_ucntl;
	struct tty pt_tty;
	cdev_t	devs, devc;
	struct	prison *pt_prison;
};

#define	PF_PKT		0x08		/* packet mode */
#define	PF_STOPPED	0x10		/* user told stopped */
#define	PF_REMOTE	0x20		/* remote and flow controlled input */
#define	PF_NOSTOP	0x40
#define PF_UCNTL	0x80		/* user control mode */

#define	PF_UNIX98	0x01
#define	PF_SOPEN	0x02
#define	PF_MOPEN	0x04
#define PF_TERMINATED	0x08

static int
ptydebug(int level, char *fmt, ...)
{
	__va_list ap;

	__va_start(ap, fmt);
	if (level <= pty_debug_level)
		kvprintf(fmt, ap);
	__va_end(ap);

	return 0;
}

/*
 * This function creates and initializes a pts/ptc pair
 *
 * pts == /dev/tty[pqrsPQRS][0123456789abcdefghijklmnopqrstuv]
 * ptc == /dev/pty[pqrsPQRS][0123456789abcdefghijklmnopqrstuv]
 *
 * XXX: define and add mapping of upper minor bits to allow more 
 *      than 256 ptys.
 */
static void
ptyinit(int n)
{
	cdev_t devs, devc;
	char *names = "pqrsPQRS";
	struct pt_ioctl *pt;

	/* For now we only map the lower 8 bits of the minor */
	if (n & ~0xff)
		return;

	pt = kmalloc(sizeof(*pt), M_PTY, M_WAITOK | M_ZERO);
	pt->devs = devs = make_dev(&pts_ops, n,
	    0, 0, 0666, "tty%c%r", names[n / 32], n % 32);
	pt->devc = devc = make_dev(&ptc_ops, n,
	    0, 0, 0666, "pty%c%r", names[n / 32], n % 32);

	devs->si_drv1 = devc->si_drv1 = pt;
	devs->si_tty = devc->si_tty = &pt->pt_tty;
	devs->si_flags |= SI_OVERRIDE;	/* uid, gid, perms from dev */
	devc->si_flags |= SI_OVERRIDE;	/* uid, gid, perms from dev */
	pt->pt_tty.t_dev = devs;
	pt->pt_uminor = n;
	ttyregister(&pt->pt_tty);
}

#ifdef UNIX98_PTYS
static int
ptyclone(struct dev_clone_args *ap)
{
	int unit;
	struct pt_ioctl *pt;

	/*
	 * Limit the number of unix98 pty (slave) devices to 1000, as
	 * the utmp(5) format only allows for 8 bytes for the tty,
	 * "pts/XXX".
	 * If this limit is reached, we don't clone and return error
	 * to devfs.
	 */
	unit = devfs_clone_bitmap_get(&DEVFS_CLONE_BITMAP(pty), 1000);

	if (unit < 0) {
		ap->a_dev = NULL;
		return 1;
	}

	pt = kmalloc(sizeof(*pt), M_PTY, M_WAITOK | M_ZERO);

	pt->devc = make_only_dev(&ptc98_ops, unit,
				 ap->a_cred->cr_ruid,
				 0, 0600, "ptm/%d", unit);
	pt->devs = make_dev(&pts98_ops, unit,
			    ap->a_cred->cr_ruid,
			    GID_TTY, 0620, "pts/%d", unit);
	ap->a_dev = pt->devc;

	pt->devs->si_flags |= SI_OVERRIDE;	/* uid, gid, perms from dev */
	pt->devc->si_flags |= SI_OVERRIDE;	/* uid, gid, perms from dev */

	pt->devs->si_drv1 = pt->devc->si_drv1 = pt;
	pt->devs->si_tty = pt->devc->si_tty = &pt->pt_tty;
	pt->pt_tty.t_dev = pt->devs;
	pt->pt_flags2 |= PF_UNIX98;
	pt->pt_uminor = unit;

	ttyregister(&pt->pt_tty);

	return 0;
}
#endif

/*
 * pti_hold() prevents the pti from being destroyed due to a termination
 * while a pt*open() is blocked.
 *
 * This function returns non-zero if we cannot hold due to a termination
 * interlock.
 */
static int
pti_hold(struct pt_ioctl *pti)
{
	if (pti->pt_flags2 & PF_TERMINATED)
		return(ENXIO);
	++pti->pt_refs;
	return(0);
}

/*
 * pti_done() releases the reference and checks to see if both sides have
 * been closed on a unix98 pty, allowing us to destroy the device and
 * release resources.
 *
 * We do not release resources on non-unix98 ptys.  Those are left
 * statically allocated.
 */
static void
pti_done(struct pt_ioctl *pti)
{
	if (--pti->pt_refs == 0) {
#ifdef UNIX98_PTYS
		cdev_t dev;
		int uminor_no;

		/*
		 * Only unix09 ptys are freed up
		 */
		if ((pti->pt_flags2 & PF_UNIX98) == 0)
			return;

		/*
		 * Interlock open attempts against termination by setting
		 * PF_TERMINATED.  This allows us to block while cleaning
		 * out the device infrastructure.
		 */
		if ((pti->pt_flags2 & (PF_SOPEN|PF_MOPEN)) == 0) {
			pti->pt_flags2 |= PF_TERMINATED;
			uminor_no = pti->pt_uminor;

			if ((dev = pti->devs) != NULL) {
				dev->si_drv1 = NULL;
				pti->devs = NULL;
				destroy_dev(dev);
			}
			if ((dev = pti->devc) != NULL) {
				dev->si_drv1 = NULL;
				pti->devc = NULL;
				destroy_dev(dev);
			}
			ttyunregister(&pti->pt_tty);
			devfs_clone_bitmap_put(&DEVFS_CLONE_BITMAP(pty),
					       uminor_no);
			kfree(pti, M_PTY);
		}
#endif
	}
}

/*ARGSUSED*/
static	int
ptsopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	int error;
	struct pt_ioctl *pti;

	/*
	 * The pti will already be assigned by the clone code or
	 * pre-created if a non-unix 98 pty.  If si_drv1 is NULL
	 * we are somehow racing a unix98 termination.
	 */
	if (dev->si_drv1 == NULL)
		return(ENXIO);
	pti = dev->si_drv1;
	if (pti_hold(pti))
		return(ENXIO);
	tp = dev->si_tty;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);		/* Set up default chars */
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
	} else if ((tp->t_state & TS_XCLUDE) && priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
		pti_done(pti);
		return (EBUSY);
	} else if (pti->pt_prison != ap->a_cred->cr_prison) {
		pti_done(pti);
		return (EBUSY);
	}
	if (tp->t_oproc)			/* Ctrlr still around. */
		(void)(*linesw[tp->t_line].l_modem)(tp, 1);
	while ((tp->t_state & TS_CARR_ON) == 0) {
		if (ap->a_oflags & FNONBLOCK)
			break;
		error = ttysleep(tp, TSA_CARR_ON(tp), PCATCH, "ptsopn", 0);
		if (error) {
			pti_done(pti);
			return (error);
		}
	}
	tp->t_state &= ~TS_ZOMBIE;
	error = (*linesw[tp->t_line].l_open)(dev, tp);
	if (error == 0)
		ptcwakeup(tp, FREAD|FWRITE);

#ifdef UNIX98_PTYS
	/*
	 * Unix98 pty stuff.
	 * On open of the slave, we set the corresponding flag in the common
	 * struct.
	 */
	ptydebug(1, "ptsopen=%s | unix98? %s\n", dev->si_name,
	    (pti->pt_flags2 & PF_UNIX98)?"yes":"no");

	if (error == 0 && (pti->pt_flags2 & PF_UNIX98)) {
		pti->pt_flags2 |= PF_SOPEN;
	}
#endif
	pti_done(pti);

	return (error);
}

static	int
ptsclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	struct pt_ioctl *pti = dev->si_drv1;
	int err;

	if (pti_hold(pti))
		panic("ptsclose on terminated pti");
	tp = dev->si_tty;
	err = (*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	ptsstop(tp, FREAD|FWRITE);
	(void) ttyclose(tp); /* clears t_state */
	tp->t_state |= TS_ZOMBIE;

#ifdef UNIX98_PTYS
	/*
	 * Unix98 pty stuff.
	 * On close of the slave, we unset the corresponding flag, and if the master
	 * isn't open anymore, we destroy the slave and unset the unit.
	 */
	ptydebug(1, "ptsclose=%s | unix98? %s\n", dev->si_name,
	    (pti->pt_flags2 & PF_UNIX98)?"yes":"no");

	if (pti->pt_flags2 & PF_UNIX98) {
		pti->pt_flags2 &= ~PF_SOPEN;
		KKASSERT((pti->pt_flags2 & PF_SOPEN) == 0);
		ptydebug(1, "master open? %s\n",
		    (pti->pt_flags2 & PF_MOPEN)?"yes":"no");
	}
#endif
	pti_done(pti);
	return (err);
}

static	int
ptsread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct proc *p = curproc;
	struct tty *tp = dev->si_tty;
	struct pt_ioctl *pti = dev->si_drv1;
	struct lwp *lp;

	int error = 0;

	lp = curthread->td_lwp;

again:
	if (pti->pt_flags & PF_REMOTE) {
		while (isbackground(p, tp)) {
			if (SIGISMEMBER(p->p_sigignore, SIGTTIN) ||
			    SIGISMEMBER(lp->lwp_sigmask, SIGTTIN) ||
			    p->p_pgrp->pg_jobc == 0 || p->p_flag & P_PPWAIT)
				return (EIO);
			pgsignal(p->p_pgrp, SIGTTIN, 1);
			error = ttysleep(tp, &lbolt, PCATCH, "ptsbg", 0);
			if (error)
				return (error);
		}
		if (tp->t_canq.c_cc == 0) {
			if (ap->a_ioflag & IO_NDELAY)
				return (EWOULDBLOCK);
			error = ttysleep(tp, TSA_PTS_READ(tp), PCATCH,
					 "ptsin", 0);
			if (error)
				return (error);
			goto again;
		}
		while (tp->t_canq.c_cc > 1 && ap->a_uio->uio_resid > 0)
			if (ureadc(clist_getc(&tp->t_canq), ap->a_uio) < 0) {
				error = EFAULT;
				break;
			}
		if (tp->t_canq.c_cc == 1)
			clist_getc(&tp->t_canq);
		if (tp->t_canq.c_cc)
			return (error);
	} else
		if (tp->t_oproc)
			error = (*linesw[tp->t_line].l_read)(tp, ap->a_uio, ap->a_ioflag);
	ptcwakeup(tp, FWRITE);
	return (error);
}

/*
 * Write to pseudo-tty.
 * Wakeups of controlling tty will happen
 * indirectly, when tty driver calls ptsstart.
 */
static	int
ptswrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;

	tp = dev->si_tty;
	if (tp->t_oproc == 0)
		return (EIO);
	return ((*linesw[tp->t_line].l_write)(tp, ap->a_uio, ap->a_ioflag));
}

/*
 * Start output on pseudo-tty.
 * Wake up process selecting or sleeping for input from controlling tty.
 */
static void
ptsstart(struct tty *tp)
{
	struct pt_ioctl *pti = tp->t_dev->si_drv1;

	if (tp->t_state & TS_TTSTOP)
		return;
	if (pti) {
		if (pti->pt_flags & PF_STOPPED) {
			pti->pt_flags &= ~PF_STOPPED;
			pti->pt_send = TIOCPKT_START;
		}
	}
	ptcwakeup(tp, FREAD);
}

static void
ptcwakeup(struct tty *tp, int flag)
{
	if (flag & FREAD) {
		wakeup(TSA_PTC_READ(tp));
		KNOTE(&tp->t_rkq.ki_note, 0);
	}
	if (flag & FWRITE) {
		wakeup(TSA_PTC_WRITE(tp));
		KNOTE(&tp->t_wkq.ki_note, 0);
	}
}

static	int
ptcopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	struct pt_ioctl *pti;

	/*
	 * The pti will already be assigned by the clone code or
	 * pre-created if a non-unix 98 pty.  If si_drv1 is NULL
	 * we are somehow racing a unix98 termination.
	 */
	if (dev->si_drv1 == NULL)
		return(ENXIO);	
	pti = dev->si_drv1;
	if (pti_hold(pti))
		return(ENXIO);
	if (pti->pt_prison && pti->pt_prison != ap->a_cred->cr_prison) {
		pti_done(pti);
		return(EBUSY);
	}
	tp = dev->si_tty;
	if (tp->t_oproc) {
		pti_done(pti);
		return (EIO);
	}
	tp->t_oproc = ptsstart;
	tp->t_stop = ptsstop;
	(void)(*linesw[tp->t_line].l_modem)(tp, 1);
	tp->t_lflag &= ~EXTPROC;
	pti->pt_prison = ap->a_cred->cr_prison;
	pti->pt_flags = 0;
	pti->pt_send = 0;
	pti->pt_ucntl = 0;

	pti->devs->si_uid = ap->a_cred->cr_uid;
	pti->devs->si_gid = 0;
	pti->devs->si_perms = 0600;
	pti->devc->si_uid = ap->a_cred->cr_uid;
	pti->devc->si_gid = 0;
	pti->devc->si_perms = 0600;

#ifdef UNIX98_PTYS
	/*
	 * Unix98 pty stuff.
	 * On open of the master, we set the corresponding flag in the common
	 * struct.
	 */
	ptydebug(1, "ptcopen=%s (master) | unix98? %s\n", dev->si_name,
	    (pti->pt_flags2 & PF_UNIX98)?"yes":"no");

	if (pti->pt_flags2 & PF_UNIX98) {
		pti->pt_flags2 |= PF_MOPEN;
	}
#endif
	pti_done(pti);

	return (0);
}

static	int
ptcclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp;
	struct pt_ioctl *pti = dev->si_drv1;

	if (pti_hold(pti))
		panic("ptcclose on terminated pti");

	tp = dev->si_tty;
	(void)(*linesw[tp->t_line].l_modem)(tp, 0);

	/*
	 * XXX MDMBUF makes no sense for ptys but would inhibit the above
	 * l_modem().  CLOCAL makes sense but isn't supported.   Special
	 * l_modem()s that ignore carrier drop make no sense for ptys but
	 * may be in use because other parts of the line discipline make
	 * sense for ptys.  Recover by doing everything that a normal
	 * ttymodem() would have done except for sending a SIGHUP.
	 */
	if (tp->t_state & TS_ISOPEN) {
		tp->t_state &= ~(TS_CARR_ON | TS_CONNECTED);
		tp->t_state |= TS_ZOMBIE;
		ttyflush(tp, FREAD | FWRITE);
	}
	tp->t_oproc = 0;		/* mark closed */

#ifdef UNIX98_PTYS
	/*
	 * Unix98 pty stuff.
	 * On close of the master, we unset the corresponding flag in the common
	 * struct asap.
	 */
	pti->pt_flags2 &= ~PF_MOPEN;
#endif

	pti->pt_prison = NULL;
	pti->devs->si_uid = 0;
	pti->devs->si_gid = 0;
	pti->devs->si_perms = 0666;
	pti->devc->si_uid = 0;
	pti->devc->si_gid = 0;
	pti->devc->si_perms = 0666;

	pti_done(pti);
	return (0);
}

static	int
ptcread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp = dev->si_tty;
	struct pt_ioctl *pti = dev->si_drv1;
	char buf[BUFSIZ];
	int error = 0, cc;

	/*
	 * We want to block until the slave
	 * is open, and there's something to read;
	 * but if we lost the slave or we're NBIO,
	 * then return the appropriate error instead.
	 */
	for (;;) {
		if (tp->t_state&TS_ISOPEN) {
			if (pti->pt_flags&PF_PKT && pti->pt_send) {
				error = ureadc((int)pti->pt_send, ap->a_uio);
				if (error)
					return (error);
				if (pti->pt_send & TIOCPKT_IOCTL) {
					cc = (int)szmin(ap->a_uio->uio_resid,
							sizeof(tp->t_termios));
					uiomove((caddr_t)&tp->t_termios, cc,
						ap->a_uio);
				}
				pti->pt_send = 0;
				return (0);
			}
			if (pti->pt_flags&PF_UCNTL && pti->pt_ucntl) {
				error = ureadc((int)pti->pt_ucntl, ap->a_uio);
				if (error)
					return (error);
				pti->pt_ucntl = 0;
				return (0);
			}
			if (tp->t_outq.c_cc && (tp->t_state&TS_TTSTOP) == 0)
				break;
		}
		if ((tp->t_state & TS_CONNECTED) == 0)
			return (0);	/* EOF */
		if (ap->a_ioflag & IO_NDELAY)
			return (EWOULDBLOCK);
		error = tsleep(TSA_PTC_READ(tp), PCATCH, "ptcin", 0);
		if (error)
			return (error);
	}
	if (pti->pt_flags & (PF_PKT|PF_UCNTL))
		error = ureadc(0, ap->a_uio);
	while (ap->a_uio->uio_resid > 0 && error == 0) {
		cc = q_to_b(&tp->t_outq, buf,
			    (int)szmin(ap->a_uio->uio_resid, BUFSIZ));
		if (cc <= 0)
			break;
		error = uiomove(buf, (size_t)cc, ap->a_uio);
	}
	ttwwakeup(tp);
	return (error);
}

static	void
ptsstop(struct tty *tp, int flush)
{
	struct pt_ioctl *pti = tp->t_dev->si_drv1;
	int flag;

	/* note: FLUSHREAD and FLUSHWRITE already ok */
	if (pti) {
		if (flush == 0) {
			flush = TIOCPKT_STOP;
			pti->pt_flags |= PF_STOPPED;
		} else {
			pti->pt_flags &= ~PF_STOPPED;
		}
		pti->pt_send |= flush;
		/* change of perspective */
	}
	flag = 0;
	if (flush & FREAD)
		flag |= FWRITE;
	if (flush & FWRITE)
		flag |= FREAD;
	ptcwakeup(tp, flag);
}

/*
 * kqueue ops for pseudo-terminals.
 */
static struct filterops ptcread_filtops =
	{ FILTEROP_ISFD, NULL, filt_ptcrdetach, filt_ptcread };
static struct filterops ptcwrite_filtops =
	{ FILTEROP_ISFD, NULL, filt_ptcwdetach, filt_ptcwrite };

static	int
ptckqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct tty *tp = dev->si_tty;
	struct klist *klist;

	ap->a_result = 0;
	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &tp->t_rkq.ki_note;
		kn->kn_fop = &ptcread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &tp->t_wkq.ki_note;
		kn->kn_fop = &ptcwrite_filtops;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	kn->kn_hook = (caddr_t)dev;
	knote_insert(klist, kn);
	return (0);
}

static int
filt_ptcread (struct knote *kn, long hint)
{
	struct tty *tp = ((cdev_t)kn->kn_hook)->si_tty;
	struct pt_ioctl *pti = ((cdev_t)kn->kn_hook)->si_drv1;

	if (tp->t_state & TS_ZOMBIE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}

	if ((tp->t_state & TS_ISOPEN) &&
	    ((tp->t_outq.c_cc && (tp->t_state & TS_TTSTOP) == 0) ||
	     ((pti->pt_flags & PF_PKT) && pti->pt_send) ||
	     ((pti->pt_flags & PF_UCNTL) && pti->pt_ucntl))) {
		kn->kn_data = tp->t_outq.c_cc;
		return(1);
	} else {
		return(0);
	}
}

static int
filt_ptcwrite (struct knote *kn, long hint)
{
	struct tty *tp = ((cdev_t)kn->kn_hook)->si_tty;
	struct pt_ioctl *pti = ((cdev_t)kn->kn_hook)->si_drv1;

	if (tp->t_state & TS_ZOMBIE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}

	if (tp->t_state & TS_ISOPEN &&
	    ((pti->pt_flags & PF_REMOTE) ?
	     (tp->t_canq.c_cc == 0) :
	     ((tp->t_rawq.c_cc + tp->t_canq.c_cc < TTYHOG - 2) ||
	      (tp->t_canq.c_cc == 0 && (tp->t_lflag & ICANON))))) {
		kn->kn_data = tp->t_canq.c_cc + tp->t_rawq.c_cc;
		return(1);
	} else {
		return(0);
	}
}

static void
filt_ptcrdetach (struct knote *kn)
{
	struct tty *tp = ((cdev_t)kn->kn_hook)->si_tty;

	knote_remove(&tp->t_rkq.ki_note, kn);
}

static void
filt_ptcwdetach (struct knote *kn)
{
	struct tty *tp = ((cdev_t)kn->kn_hook)->si_tty;

	knote_remove(&tp->t_wkq.ki_note, kn);
}

/*
 * I/O ops
 */
static	int
ptcwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp = dev->si_tty;
	u_char *cp = 0;
	int cc = 0;
	u_char locbuf[BUFSIZ];
	int cnt = 0;
	struct pt_ioctl *pti = dev->si_drv1;
	int error = 0;

again:
	if ((tp->t_state&TS_ISOPEN) == 0)
		goto block;
	if (pti->pt_flags & PF_REMOTE) {
		if (tp->t_canq.c_cc)
			goto block;
		while ((ap->a_uio->uio_resid > 0 || cc > 0) &&
		       tp->t_canq.c_cc < TTYHOG - 1) {
			if (cc == 0) {
				cc = (int)szmin(ap->a_uio->uio_resid, BUFSIZ);
				cc = imin(cc, TTYHOG - 1 - tp->t_canq.c_cc);
				cp = locbuf;
				error = uiomove(cp, (size_t)cc, ap->a_uio);
				if (error)
					return (error);
				/* check again for safety */
				if ((tp->t_state & TS_ISOPEN) == 0) {
					/* adjust as usual */
					ap->a_uio->uio_resid += cc;
					return (EIO);
				}
			}
			if (cc > 0) {
				cc = b_to_q((char *)cp, cc, &tp->t_canq);
				/*
				 * XXX we don't guarantee that the canq size
				 * is >= TTYHOG, so the above b_to_q() may
				 * leave some bytes uncopied.  However, space
				 * is guaranteed for the null terminator if
				 * we don't fail here since (TTYHOG - 1) is
				 * not a multiple of CBSIZE.
				 */
				if (cc > 0)
					break;
			}
		}
		/* adjust for data copied in but not written */
		ap->a_uio->uio_resid += cc;
		clist_putc(0, &tp->t_canq);
		ttwakeup(tp);
		wakeup(TSA_PTS_READ(tp));
		return (0);
	}
	while (ap->a_uio->uio_resid > 0 || cc > 0) {
		if (cc == 0) {
			cc = (int)szmin(ap->a_uio->uio_resid, BUFSIZ);
			cp = locbuf;
			error = uiomove(cp, (size_t)cc, ap->a_uio);
			if (error)
				return (error);
			/* check again for safety */
			if ((tp->t_state & TS_ISOPEN) == 0) {
				/* adjust for data copied in but not written */
				ap->a_uio->uio_resid += cc;
				return (EIO);
			}
		}
		while (cc > 0) {
			if ((tp->t_rawq.c_cc + tp->t_canq.c_cc) >= TTYHOG - 2 &&
			   (tp->t_canq.c_cc > 0 || !(tp->t_lflag&ICANON))) {
				wakeup(TSA_HUP_OR_INPUT(tp));
				goto block;
			}
			(*linesw[tp->t_line].l_rint)(*cp++, tp);
			cnt++;
			cc--;
		}
		cc = 0;
	}
	return (0);
block:
	/*
	 * Come here to wait for slave to open, for space
	 * in outq, or space in rawq, or an empty canq.
	 */
	if ((tp->t_state & TS_CONNECTED) == 0) {
		/* adjust for data copied in but not written */
		ap->a_uio->uio_resid += cc;
		return (EIO);
	}
	if (ap->a_ioflag & IO_NDELAY) {
		/* adjust for data copied in but not written */
		ap->a_uio->uio_resid += cc;
		if (cnt == 0)
			return (EWOULDBLOCK);
		return (0);
	}
	error = tsleep(TSA_PTC_WRITE(tp), PCATCH, "ptcout", 0);
	if (error) {
		/* adjust for data copied in but not written */
		ap->a_uio->uio_resid += cc;
		return (error);
	}
	goto again;
}

/*ARGSUSED*/
static	int
ptyioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct tty *tp = dev->si_tty;
	struct pt_ioctl *pti = dev->si_drv1;
	u_char *cc = tp->t_cc;
	int stop, error;

	if (dev_dflags(dev) & D_MASTER) {
		switch (ap->a_cmd) {

		case TIOCGPGRP:
			/*
			 * We avoid calling ttioctl on the controller since,
			 * in that case, tp must be the controlling terminal.
			 */
			*(int *)ap->a_data = tp->t_pgrp ? tp->t_pgrp->pg_id : 0;
			return (0);

		case TIOCPKT:
			if (*(int *)ap->a_data) {
				if (pti->pt_flags & PF_UCNTL)
					return (EINVAL);
				pti->pt_flags |= PF_PKT;
			} else
				pti->pt_flags &= ~PF_PKT;
			return (0);

		case TIOCUCNTL:
			if (*(int *)ap->a_data) {
				if (pti->pt_flags & PF_PKT)
					return (EINVAL);
				pti->pt_flags |= PF_UCNTL;
			} else
				pti->pt_flags &= ~PF_UCNTL;
			return (0);

		case TIOCREMOTE:
			if (*(int *)ap->a_data)
				pti->pt_flags |= PF_REMOTE;
			else
				pti->pt_flags &= ~PF_REMOTE;
			ttyflush(tp, FREAD|FWRITE);
			return (0);

		case TIOCISPTMASTER:
			if ((pti->pt_flags2 & PF_UNIX98) && (pti->devc == dev))
				return (0);
			else
				return (EINVAL);
		}

		/*
		 * The rest of the ioctls shouldn't be called until 
		 * the slave is open.
		 */
		if ((tp->t_state & TS_ISOPEN) == 0)
			return (EAGAIN);

		switch (ap->a_cmd) {
#ifdef COMPAT_43
		case TIOCSETP:
		case TIOCSETN:
#endif
		case TIOCSETD:
		case TIOCSETA:
		case TIOCSETAW:
		case TIOCSETAF:
			/*
			 * IF CONTROLLER STTY THEN MUST FLUSH TO PREVENT A HANG.
			 * ttywflush(tp) will hang if there are characters in
			 * the outq.
			 */
			ndflush(&tp->t_outq, tp->t_outq.c_cc);
			break;

		case TIOCSIG:
			if (*(unsigned int *)ap->a_data >= NSIG ||
			    *(unsigned int *)ap->a_data == 0)
				return(EINVAL);
			if ((tp->t_lflag&NOFLSH) == 0)
				ttyflush(tp, FREAD|FWRITE);
			pgsignal(tp->t_pgrp, *(unsigned int *)ap->a_data, 1);
			if ((*(unsigned int *)ap->a_data == SIGINFO) &&
			    ((tp->t_lflag&NOKERNINFO) == 0))
				ttyinfo(tp);
			return(0);
		}
	}
	if (ap->a_cmd == TIOCEXT) {
		/*
		 * When the EXTPROC bit is being toggled, we need
		 * to send an TIOCPKT_IOCTL if the packet driver
		 * is turned on.
		 */
		if (*(int *)ap->a_data) {
			if (pti->pt_flags & PF_PKT) {
				pti->pt_send |= TIOCPKT_IOCTL;
				ptcwakeup(tp, FREAD);
			}
			tp->t_lflag |= EXTPROC;
		} else {
			if ((tp->t_lflag & EXTPROC) &&
			    (pti->pt_flags & PF_PKT)) {
				pti->pt_send |= TIOCPKT_IOCTL;
				ptcwakeup(tp, FREAD);
			}
			tp->t_lflag &= ~EXTPROC;
		}
		return(0);
	}
	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, ap->a_data,
					      ap->a_fflag, ap->a_cred);
	if (error == ENOIOCTL)
		 error = ttioctl(tp, ap->a_cmd, ap->a_data, ap->a_fflag);
	if (error == ENOIOCTL) {
		if (pti->pt_flags & PF_UCNTL &&
		    (ap->a_cmd & ~0xff) == UIOCCMD(0)) {
			if (ap->a_cmd & 0xff) {
				pti->pt_ucntl = (u_char)ap->a_cmd;
				ptcwakeup(tp, FREAD);
			}
			return (0);
		}
		error = ENOTTY;
	}
	/*
	 * If external processing and packet mode send ioctl packet.
	 */
	if ((tp->t_lflag&EXTPROC) && (pti->pt_flags & PF_PKT)) {
		switch(ap->a_cmd) {
		case TIOCSETA:
		case TIOCSETAW:
		case TIOCSETAF:
#ifdef COMPAT_43
		case TIOCSETP:
		case TIOCSETN:
#endif
#if defined(COMPAT_43) || defined(COMPAT_SUNOS)
		case TIOCSETC:
		case TIOCSLTC:
		case TIOCLBIS:
		case TIOCLBIC:
		case TIOCLSET:
#endif
			pti->pt_send |= TIOCPKT_IOCTL;
			ptcwakeup(tp, FREAD);
		default:
			break;
		}
	}
	stop = (tp->t_iflag & IXON) && CCEQ(cc[VSTOP], CTRL('s'))
		&& CCEQ(cc[VSTART], CTRL('q'));
	if (pti->pt_flags & PF_NOSTOP) {
		if (stop) {
			pti->pt_send &= ~TIOCPKT_NOSTOP;
			pti->pt_send |= TIOCPKT_DOSTOP;
			pti->pt_flags &= ~PF_NOSTOP;
			ptcwakeup(tp, FREAD);
		}
	} else {
		if (!stop) {
			pti->pt_send &= ~TIOCPKT_DOSTOP;
			pti->pt_send |= TIOCPKT_NOSTOP;
			pti->pt_flags |= PF_NOSTOP;
			ptcwakeup(tp, FREAD);
		}
	}
	return (error);
}


static void ptc_drvinit (void *unused);

#ifdef UNIX98_PTYS
SYSCTL_INT(_kern, OID_AUTO, pty_debug, CTLFLAG_RW, &pty_debug_level,
		0, "Change pty debug level");
#endif

static void
ptc_drvinit(void *unused)
{
	int i;

#ifdef UNIX98_PTYS
	/*
	 * Unix98 pty stuff.
	 * Create the clonable base device.
	 */
	make_autoclone_dev(&ptc_ops, &DEVFS_CLONE_BITMAP(pty), ptyclone,
	    0, 0, 0666, "ptmx");
#endif

	for (i = 0; i < 256; i++) {
		ptyinit(i);
	}
}

SYSINIT(ptcdev,SI_SUB_DRIVERS,SI_ORDER_MIDDLE+CDEV_MAJOR_C,ptc_drvinit,NULL)
