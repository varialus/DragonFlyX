# $DragonFly: src/gnu/usr.bin/cc41/cc1/Makefile,v 1.2 2007/01/20 03:51:33 corecode Exp $

CFLAGS+=	-DTARGET_MACHINE=\"${target}\" -DHOST_MACHINE=\"${target}\"

PROG=		cc1
NOMAN=		1
NOSHARED?=	yes

SRCS=	c-lang.c stub-objc.c

SRCS+=	attribs.c c-errors.c c-lex.c c-pragma.c c-decl.c c-typeck.c \
	c-convert.c c-aux-info.c c-common.c c-opts.c c-format.c c-semantics.c \
	c-incpath.c cppdefault.c c-ppoutput.c c-cppbuiltin.c prefix.c \
	c-objc-common.c c-dump.c c-pch.c c-parser.c \
	c-gimplify.c tree-mudflap.c c-pretty-print.c

SRCS+=	main.c

SRCS+=	tm-preds.h tree-check.h genrtl.h insn-flags.h insn-constants.h
SRCS+=	gtype-desc.h insn-modes.h insn-config.h options.h insn-modes.c
SRCS+=	insn-emit.c insn-codes.h insn-attrtab.c insn-attr.h insn-extract.c
SRCS+=	insn-opinit.c genrtl.c

LDADD+=	${LIBBACKEND} ${LIBCPP}
DPADD+=	${LIBBACKEND} ${LIBCPP}

CFLAGS+= -I.

OBJS+=	cc1-checksum.o

.include <bsd.prog.mk>

# compiler checksum handling
CLEANFILES+=	cc1-checksum.c dummy-checksum.o cc1-dummy

cc1-dummy: ${OBJS:Ncc1-checksum.o} dummy-checksum.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} ${LDADD}

cc1-checksum.c: cc1-dummy
	${TOOLDIR}/genchecksum/genchecksum.nx ${.ALLSRC} > ${.TARGET}


.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"
