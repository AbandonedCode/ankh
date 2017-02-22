PROG=		ankh
SRCS=		ankh.c

BINDIR=		/usr/local/bin

CFLAGS+=	-Wall -Werror
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith
CFLAGS+=	-Wsign-compare
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-I/usr/local/include

LDADD=		-L/usr/local/lib -lsodium

NOMAN=		noman

secret=		secret.passphrase
testsize=	bs=$$(($$RANDOM % 1024 + 1)) count=$$(($$RANDOM % 8192 + 1024))
mode=		1

test: ${PROG}
	dd if=/dev/random of=foo.bin ${testsize}
	sha256 foo.bin | tee SHA256
	tr -cd [:graph:] < /dev/random | fold -bw 20 | head -1 | tee ${secret}
	${PROG} -m ${mode} -v foo.bin bar.bin < ${secret}
	${PROG} -v -d bar.bin foo.bin < ${secret}
	sha256 -c SHA256

.include <bsd.prog.mk>
