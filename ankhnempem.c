/*
 * Copyright (c) 2017 Steven Roberts <sroberts@fenderq.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readpassphrase.h>
#include <unistd.h>
#include <util.h>

#include <sodium.h>

#define BUFSIZE 1024 * 1024
#define MAX_PASSWD 1024
#define VERSION "1.0.1"

struct cipher_info {
	FILE *fin;
	FILE *fout;
	int enc;
	unsigned char key[crypto_secretbox_KEYBYTES];
};

extern char *__progname;
extern char *optarg;

int verbose;

__dead void usage(void);

static int	 ankhnempem(char *, char *, int);
static int	 decrypt(struct cipher_info *);
static int	 encrypt(struct cipher_info *);
static void	 kdf(uint8_t *, int, int, uint8_t *);

int
main(int argc, char *argv[])
{
	char ch;
	int dflag;

	dflag = 0;

	if (pledge("cpath rpath stdio tty wpath", NULL) == -1)
		err(1, "pledge");

	if (sodium_init() == -1)
		errx(1, "libsodium init failure");

	while ((ch = getopt(argc, argv, "dv")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	ankhnempem(argv[0], argv[1], dflag ? 0 : 1);

	exit(EXIT_SUCCESS);
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-dv] infile outfile\n", __progname);
	exit(EXIT_FAILURE);
}

static int
encrypt(struct cipher_info *ci)
{
	int clen;
	int mlen;
	size_t r;
	unsigned char *c;
	unsigned char *m;
	unsigned char mac[crypto_secretbox_MACBYTES];
	unsigned char n[crypto_secretbox_NONCEBYTES];

	clen = BUFSIZE;
	if ((c = malloc(clen)) == NULL)
		err(1, NULL);
	mlen = BUFSIZE;
	if ((m = malloc(mlen)) == NULL)
		err(1, NULL);

	do {
		if ((r = fread(m, 1, mlen, ci->fin)) == 0) {
			if (ferror(ci->fin))
				errx(1, "failure reading from input stream");
			break;
		}
		randombytes_buf(n, sizeof(n));
		crypto_secretbox_detached(c, mac, m, r, n, ci->key);
		if (fwrite(n, sizeof(n), 1, ci->fout) == 0)
			errx(1, "error writing nonce");
		if (fwrite(mac, sizeof(mac), 1, ci->fout) == 0)
			errx(1, "error writing mac");
		if (fwrite(c, r, 1, ci->fout) == 0)
			errx(1, "failure writing to output stream");
	} while (1);

	free(c);
	free(m);

	return 0;
}

static int
decrypt(struct cipher_info *ci)
{
	int clen;
	int mlen;
	size_t r;
	unsigned char *c;
	unsigned char *m;
	unsigned char mac[crypto_secretbox_MACBYTES];
	unsigned char n[crypto_secretbox_NONCEBYTES];

	clen = BUFSIZE;
	if ((c = malloc(clen)) == NULL)
		err(1, NULL);
	mlen = BUFSIZE;
	if ((m = malloc(mlen)) == NULL)
		err(1, NULL);

	do {
		if (fread(n, sizeof(n), 1, ci->fin) == 0)
			errx(1, "error reading nonce");
		if (fread(mac, sizeof(mac), 1, ci->fin) == 0)
			errx(1, "error reading mac");
		if ((r = fread(c, 1, clen, ci->fin)) == 0) {
			if (ferror(ci->fin))
				errx(1, "error reading from input stream");
			break;
		}
		if (crypto_secretbox_open_detached(m,
		    c, mac, r, n, ci->key) != 0)
			errx(1, "invalid message data");
		if (fwrite(m, r, 1, ci->fout) == 0)
			errx(1, "failure writing to output stream");
	} while (!feof(ci->fin));

	free(c);
	free(m);

	return 0;
}

static int
ankhnempem(char *infile, char *outfile, int enc)
{
	struct cipher_info *c;
	unsigned char salt[crypto_pwhash_SALTBYTES];

	if ((c = calloc(1, sizeof(struct cipher_info))) == NULL)
		err(1, NULL);
	c->enc = enc;

	if ((c->fin = fopen(infile, "r")) == NULL)
		err(1, "%s", infile);
	if ((c->fout = fopen(outfile, "w")) == NULL)
		err(1, "%s", outfile);

	if (c->enc) {
		randombytes_buf(salt, sizeof(salt));
		if (fwrite(salt, sizeof(salt), 1, c->fout) != 1)
			errx(1, "error writing salt to %s", infile);
	} else {
		if (fread(salt, sizeof(salt), 1, c->fin) != 1)
			errx(1, "error reading salt from %s", infile);
	}

	kdf(salt, 1, c->enc ? 1 : 0, c->key);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (verbose)
		printf("%scrypting ...\n", enc ? "en" : "de");
	enc ? encrypt(c) : decrypt(c);

	fclose(c->fin);
	fclose(c->fout);
	explicit_bzero(c, sizeof(struct cipher_info));
	free(c);

	return 0;
}

static void
kdf(uint8_t *salt, int allowstdin, int confirm, uint8_t *key)
{
	char pass[MAX_PASSWD];
	int rppflags = RPP_ECHO_OFF;

	if (allowstdin && !isatty(STDIN_FILENO))
		rppflags |= RPP_STDIN;
	if (!readpassphrase("passphrase: ", pass, sizeof(pass), rppflags))
		errx(1, "unable to read passphrase");
	if (strlen(pass) == 0)
		errx(1, "please provide a password");
	if (confirm && !(rppflags & RPP_STDIN)) {
		char pass2[MAX_PASSWD];
		if (!readpassphrase("confirm passphrase: ", pass2,
		    sizeof(pass2), rppflags))
			errx(1, "unable to read passphrase");
		if (strcmp(pass, pass2) != 0)
			errx(1, "passwords don't match");
		explicit_bzero(pass2, sizeof(pass2));
	}
	if (verbose)
		printf("generating key ...\n");
	if (crypto_pwhash(key, crypto_secretbox_KEYBYTES,
	    pass, strlen(pass), salt,
	    crypto_pwhash_OPSLIMIT_INTERACTIVE,
	    crypto_pwhash_MEMLIMIT_INTERACTIVE,
	    crypto_pwhash_ALG_DEFAULT) == -1)
		errx(1, "crypto_pwhash failure");
	explicit_bzero(pass, sizeof(pass));
}
