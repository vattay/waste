/*
WASTE - wastesrc_admin.cpp (Standard Unix command line program)
Copyright (C) 2003 Nullsoft, Inc.
Copyright (C) 2004 WASTE Development Team

WASTE is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

WASTE  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with WASTE; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "platform.hpp"

#include <glob.h>
#include <stdint.h>

#include "blowfish.hpp"
#include "sha.hpp"
#include "util.hpp"

#include "rsa/global.hpp"
#include "rsa/rsaref.hpp"
#include "rsa/r_random.hpp"


//
// If an attempt is made to port this to windows some other way of
// gathering entropy is required, most modern Unices have /dev/random
// which is used here.
//
// Also I have used quite a few of the rarer apis like getopt & bzero
//
#if !_WIN32

#define MAX_LC 30

static char g_waste_dir[MAXPATHLEN];
static char *g_cmdname;
static char *g_publickeyfile;
static bool g_dumpourpublic;
static const char *g_profile;
static const char g_profile_default[] = "default";
static const char *g_nickname;
static const char g_nickname_default[] = "unknown";
static bool g_genkey;
static int g_keylen;
static const int g_keylen_default = 1536;
static char *g_passwd;
static int g_verbose_level;

enum LevelsOfVerbosity {
	VNONE = 0,
	VLEAST,
	VMOST	// Make sure this is always last
};

static const int g_keysizes[] = { 1024, 1536, 2048, 3072, 4096 };
#define NUM_KEY_SIZES ((int) (sizeof(g_keysizes)/sizeof(g_keysizes[0])))

static void reportf(FILE *out, bool appenderr, char *fmt, ...)
{
	FILE *fout = (out)?out : stdout;
	char buffer[1024];
	va_list ap;
	int err = errno;

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	fprintf(fout, "%s: %s%c", g_cmdname, buffer, appenderr?' ':'\n');
	if (appenderr)
		fprintf(fout, "- %s(%d)\n", strerror(err), err);
}

#define sysc_test(expr, msg...) do {							\
	if (-1 == (expr)) {                                			\
		reportf(stderr, true,  ## msg);							\
		exit(EX_OSERR);											\
	}                                                           \
} while(0)

#define libc_test(expr, msg...) do {							\
	if (!(expr)) {                                 				\
		reportf(stderr, true, ## msg);							\
		exit(EX_OSERR);											\
	}                                                           \
} while(0)

#define verbosef(level, args...) do {							\
	if (g_verbose_level >= level) fprintf(stderr, ## args);		\
} while (0)

#define sized_snprintf(buf, args...) snprintf(buf, sizeof(buf), ## args)

static void usage()
{
	char *szCR2;
	szCR2= (char*) malloc(sK0[1]);
	safe_strncpy(szCR2, (char *) sK1[1], sK0[1]);
	dpi(szCR2, 2);
	fprintf(stderr, "\n%s\n", szCR2);
	memset(szCR2, 0, sK0[1]);
	free(szCR2);

	fprintf(stderr, "%s: [-bhLv] [-a -|<public key file>] [-f <passphrase>] [-p profile] [-k <keysize>] [-n nickname]\n", g_cmdname);
	fputs("where	-a	Add a public key, ('-' from input)\n", stderr);
	fputs("	-b	dump public key for profile\n", stderr);
	fprintf(stderr,"	-p	Profile in %s (Default)\n", WASTE_CONFIG_DIR);
	fputs("	-h	Print this help\n", stderr);
	fputs("	-l	Show the licence terms\n", stderr);
	fputs("	-n	Nickname (unknown)\n", stderr);
	fputs("	-f	Passphrase to protect secret key (request it)\n", stderr);
	fputs("	-k	Generate a secret (private) key of the given length (0 ==> 1536)\n", stderr);
	fputs("	-v	Verbose - increase the verbosity can be used more than once\n", stderr);
	fputs("\nkeysize can be one of\n", stderr);
	fputs("    1024 bits (weak, not recommended)\n", stderr);
	fputs("    1536 bits (recommended)\n", stderr);
	fputs("    2048 bits (slower, recommended)\n", stderr);
	fputs("    3072 bits (slow, not recommended)\n", stderr);
	fputs("    4096 bits (very slow, not recommended)\n", stderr);

	exit(EX_USAGE);
}

// Argument processor
static void init(int argc, char * const argv[])
{
    char c;
	bool licence = false;

    if(argc == 1){
    	usage();
    }

    // Get the command name and strip the leading dirpath
    if ( (g_cmdname = strrchr(argv[0], '/')) )
        g_cmdname++;
    else
        g_cmdname = argv[0];

	// find the user's home directory
	const char *home = getenv("HOME");
	if (!home) {
		reportf(stderr, false, "Can't find users home directory");
		exit(EX_SOFTWARE);
	}
	sized_snprintf(g_waste_dir, "%s%s", home, &WASTE_CONFIG_DIR[1]);
	
    while ((c = getopt(argc, argv, "a:bf:k:Ln:p:v")) != -1) {
        switch(c) {
		case 'a': g_publickeyfile = optarg; break;
		case 'b': g_dumpourpublic = true; break;
		case 'f': g_profile = optarg; break;
		case 'k': g_genkey = true; g_keylen = strtol(optarg, NULL, 10); break;
		case 'L': licence = true; break;
		case 'n': g_nickname = optarg; break;
		case 'p': g_passwd = optarg; break;
		case 'v': g_verbose_level++; break;
        default:
            usage();
        }
    }

	verbosef(VMOST, "argument processing\n");
	if (g_genkey) {
		int i = 0;

		if (!g_keylen)
			g_keylen = g_keylen_default;
		else for (; i < NUM_KEY_SIZES; i++)
			if (g_keysizes[i] == g_keylen)
				break;
		if (i < NUM_KEY_SIZES)
			verbosef(VMOST, "g_keylen = %d\n", g_keylen);
		else {
			// Invalid key len not found;
			reportf(stderr, false, "Invalid key length requested");
			usage();
		}
		// The given password is ALWAYS ignored when generating a key
		if (g_passwd) {
			reportf(stderr, false, "Generating key - ignoring password");
			bzero(g_passwd, strlen(g_passwd));
			g_passwd = NULL;
		}
	}

	if (!g_profile)
		g_profile = g_profile_default;

	if (!g_nickname)
		g_nickname = g_nickname_default;

	if (g_publickeyfile)
		reportf(stderr, false, "adding publickeys is not yet implemented");

	if (licence) {
		char *szLI2;

		szLI2 = (char*) malloc(sK0[3]);
		safe_strncpy(szLI2, (char*) sK1[3], sK0[3]);
		dpi(szLI2, 4);
		fprintf(stderr, "\n\n%s\n", szLI2);
		memset(szLI2, 0, sK0[3]);
		free(szLI2);
	}

	verbosef(VMOST, "g_publickeyfile = %s\n", g_publickeyfile);
	verbosef(VMOST, "g_dumpourpublic = %d\n", g_dumpourpublic);
	verbosef(VMOST, "g_profile = %s\n", g_profile);
	verbosef(VMOST, "g_nickname = %s\n", g_nickname);
	verbosef(VMOST, "g_passwd = %s\n", g_passwd);
}

// Grab some entropy from the system and use it to initalise RSA
static void loadentropy(R_RANDOM_STRUCT &ent)
{
	int randfd;
	unsigned int need;

	verbosef(VMOST, "Loading up the random generator with entropy\n");

	sysc_test((randfd = open("/dev/random", O_RDONLY)),
		"failed to open entropy source %s", "/dev/random");
	
	R_GetRandomBytesNeeded(&need, &ent);
	do {
		unsigned char buffer[1024];
		int cc;

		need = min(need, sizeof(buffer));
		sysc_test((cc = read(randfd, buffer, need)), "can't read entropy data");
		assert(cc);

		R_RandomUpdate(&ent, buffer, cc);
		verbosef(VMOST, "Loaded %d bytes\n", cc);
		R_GetRandomBytesNeeded(&need, &ent);
	} while(need > 0);

	close(randfd);
	verbosef(VMOST, "Done loading\n");
}

// Actually generate a private key.  By the way the guesstimates are
// still fairly accurate.  I took 15 minutes to generate a 4096 size
// key on a 1.25 G4 powerbook.  Boy was I surprised.
static void
genthekeys(R_RSA_PRIVATE_KEY &privkey, int keylen, R_RANDOM_STRUCT &ent)
{
	R_RSA_PROTO_KEY protkey;
	R_RSA_PUBLIC_KEY pubkey;

	protkey.bits = keylen;
	protkey.useFermat4 = true;
	verbosef(VLEAST, "Generating keys this may take as long as %d minutes...",
		keylen <= 1024? 2 : keylen <= 2048? 3 : keylen <= 3072? 10: 20);
	fflush(stderr);

	time_t delta = time(0);
	if (R_GeneratePEMKeys(&pubkey, &privkey, &protkey, &ent)) {
		reportf(stderr, false, "Error generating keys");
		exit(EX_SOFTWARE);
	}
	delta = time(0) - delta;

	verbosef(VLEAST, " Done %dm %2ds\n", delta / 60, delta % 60);
}

// Create the waste config dir if necessary
static void createwastedir()
{
	struct stat statbuf;
	
	if (-1 == stat(g_waste_dir, &statbuf)) {
		if (ENOENT == errno) {
			verbosef(VLEAST, "Create waste directory %s\n", g_waste_dir);
			sysc_test(mkdir(g_waste_dir, S_IRWXU),
					"Can't create Waste directory %s", g_waste_dir);
		}
		else
			sysc_test(-1, "Can't 'stat' Waste direcotry %s", g_waste_dir);
	}
}

// Write the data out to the given file in uppercase (blech) hex
static void
writehexdata(FILE *outf, const void *data, unsigned len, int &lc)
{
	const uint8_t *cp = (const uint8_t *) data;

    for (unsigned i = 0; i < len; i++) {
		fprintf(outf, "%02X", *cp++);
		if (++lc % MAX_LC == 0)
			fputc('\n', outf);
	}
}

// Blowfish encipher some data and then write it out
static void
writeBFdata(FILE *outf, CBlowfish &bl,
			const void *data, unsigned len, int &lc)
{
	const unsigned long *p = (const unsigned long *) data;

	assert(!(len & 7));
	for (unsigned i = 0; i < len; i += 8) {
		unsigned long pp[2];

		pp[0] = *p++; pp[1] = *p++;
		bl.EncryptCBC(pp, sizeof(pp));
		writehexdata(outf, pp, sizeof(pp), lc);
	}
}

// Write that secret key out.  
// TODO: using "PASSWORD" is not a good idea, it opens us up to a 
// known plaintext attack on the private key.  Remember the first 8 bytes
// are the IV followed by "PASSWORD".  This gives too much information away.
static void writeprivatekey(R_RSA_PRIVATE_KEY &privkey,
							R_RANDOM_STRUCT &ent, 
							uint8_t passbuf[SHA_OUTSIZE])
{
	char path[MAXPATHLEN];
	FILE *outf;

	createwastedir();

	sized_snprintf(path, "%s/%s.pr4", g_waste_dir, g_profile);
	verbosef(VMOST, "Private key file %s\n", path);
	
	libc_test((outf = fopen(path, "w")),
			"Can't create private key file", path);

	fprintf(outf, "WASTE_PRIVATE_KEY 10 %d\n", privkey.bits);

	int lc = 0;
	unsigned long tl[2];

	CBlowfish bl(passbuf, SHA_OUTSIZE);

	R_GenerateBytes((unsigned char *) tl, sizeof(tl), &ent);
	writehexdata(outf, (unsigned char *) tl, sizeof(tl), lc);
	bl.SetIV(CBlowfish::IV_BOTH, tl);
	
#define WPK(field) writeBFdata(outf, bl, field, sizeof(field), lc)
	writeBFdata(outf, bl, "PASSWORD", 8, lc);
	WPK(privkey.modulus);
	WPK(privkey.publicExponent);
	WPK(privkey.exponent);
	WPK(privkey.prime);
	WPK(privkey.primeExponent);
	WPK(privkey.coefficient);
#undef WPK

	// Add a new line if necessary
	if (lc % MAX_LC)
		fputc('\n', outf);

	fprintf(outf, "WASTE_PRIVATE_KEY_END\n");
	fclose(outf);

	bl.Final();
}

// Read a single byte's worth of hex data.  Wrap across lines and ignore
// some input characters.
static const char ignorelist[] = "\n\r\t =_<>:|";
static inline int readhexchar(FILE *inf) //returns -1 on error
{
    int c;

    do {
		c = fgetc(inf);
		if (EOF == c)
			return -1;
	} while (strchr(ignorelist, (char) c));

	// Get high nibble
	unsigned t = tolower(c) - '0';
	if (t > 9) {
		t += '0' -'a';	// add the '0' back in and subtract an 'a'
		if (t <= 0xf - 10)
			t += 10;
		else
			return -1;
	}
	int ret = t << 4;

	c = fgetc(inf);
	if (EOF == c)
		return -1;

	t = tolower(c) - '0';
	if (t > 9) {
		t += '0' -'a';	// add the '0' back in and subtract an 'a'
		if (t > 0xf - 0xa)
			return -1;
		t += 0xa;
	}
	return ret | t;
}

// Read len bytes worth of hex data from the input file
static bool
readhexdata(FILE *inf, void *data, unsigned len)
{
	char  *cp = (char *) data;

	for (; len > 0; len--) {
		int c = readhexchar(inf);

		if (c < 0)
			return false;
		*cp++ = (char) c;
	}

	return true;
}

// Read len bytes of input data and decode it
static bool readBFdata(FILE *inf, CBlowfish &bl, void *data, unsigned int len)
{
	assert( !(len & 7) );

	if (!readhexdata(inf, data, len))
		return false;

	bl.DecryptCBC(data, len);
	return true;
}

#define conststrncmp(linebuf, conststr) \
	strncmp(linebuf, conststr, sizeof(conststr)-1)

// Read and verify the private key using the passbuf as a key
// to the blowfish encipherment.
static int readprivatekey(R_RSA_PRIVATE_KEY &privkey,
						   uint8_t passbuf[SHA_OUTSIZE])
{
	int goagain = 0;
	char path[MAXPATHLEN];
	FILE *inf;
	char *err = NULL;
	char linebuf[1024];
	CBlowfish bl;

	sized_snprintf(path, "%s/%s.pr4", g_waste_dir, g_profile);
	verbosef(VMOST, "Private key file %s\n", path);
	
	libc_test((inf = fopen(path, "r")),
			"Can't open private key file", path);

	err = "No private key found in file";
	for (;;) {
		fgets(linebuf, sizeof(linebuf), inf);
		if (feof(inf) || !linebuf[0])
			goto abortError;
		if (!conststrncmp(linebuf, "WASTE_PRIVATE_KEY "))
			break;
	}

	{
		char *cp = strchr(linebuf, ' ');
		long num;

		// Parse out the WASTE Version
		err = "Private key found but version incorrect";
		if (!cp || (num = strtol(cp, &cp, 10)) < 10 || num >= 20)
			goto abortError;

		// Parse out the key length
		err = "Private key found but size incorrect";
		if (!cp
		|| (privkey.bits = strtoul(cp, NULL, 10)) > MAX_RSA_MODULUS_BITS
		||  privkey.bits < MIN_RSA_MODULUS_BITS)
			goto abortError;
	}

    bl.Init(passbuf, SHA_OUTSIZE);

	{
		// Parse out the IV for the blowfish CBC
		unsigned long tl[2];
		err = "Private key corrupt";
		if (!readhexdata(inf, tl, sizeof(tl)))
			goto abortError;
		bl.SetIV(CBlowfish::IV_BOTH, tl);
	}

	{
		char buf[8];
		if (!readBFdata(inf, bl, buf, sizeof(buf)))
			goto abortError;

		if (memcmp(buf, "PASSWORD", 8)) {
			goagain++;
			err = "Invalid password for private key";
			goto abortError;
		}
	}

#define WPK(x) readBFdata(inf, bl, privkey.x, sizeof(privkey.x))

	if (!WPK(modulus) || !WPK(publicExponent) || !WPK(exponent)
	||  !WPK(prime)   || !WPK(primeExponent)  || !WPK(coefficient))
		goto abortError;

#undef WPK

	// Make sure the key is terminated properly
	err = "Private key corrupt";
	if (!fgets(linebuf, sizeof(linebuf), inf)	// Clear to end of line
	||  !fgets(linebuf, sizeof(linebuf), inf)	// read 'END' line
	||  conststrncmp(linebuf, "WASTE_PRIVATE_KEY_END"))	// Check it
		goto abortError;

	fclose(inf);
	return true;

abortError:
	reportf(stderr, 0, err);
	if (!goagain)
		exit(EX_DATAERR);
	fclose(inf);
	privkey.bits = 0;
	return false;
}

// passbuf must be at least SHA_OUTSIZE in size
static void
genkeys(int keylen, R_RSA_PRIVATE_KEY &privkey, uint8_t passbuf[SHA_OUTSIZE])
{
	R_RANDOM_STRUCT entropy;

	R_RandomInit(&entropy);

	loadentropy(entropy);
	genthekeys(privkey, keylen, entropy);

	writeprivatekey(privkey, entropy, passbuf);

	R_RandomFinal(&entropy);
	bzero(&entropy, sizeof(entropy));
}

#if __gnu_linux__
static char *
readpassphrase(const char *prompt, char *buf, size_t bufsz, int /* flags */)
{
    char *passwd = getpass(prompt);

	if (passwd) {
		strncpy(buf, passwd, bufsz);
		bzero(passwd, strlen(buf));
		return buf;
	}

	return NULL;
}

#define PASS_MAX 128

#else // Probably BSD based 
#include <readpassphrase.h>

#endif // __gnu_linux__

static void
getpassword(uint8_t passbuf[SHA_OUTSIZE], bool verify)
{
	char buf[PASS_MAX];

	if (g_passwd) {
		safe_strncpy(buf, g_passwd, sizeof(buf));
		bzero(g_passwd, strlen(g_passwd));
		g_passwd = NULL;
	}
	else {
		for (;;) {
			libc_test(readpassphrase("Passphrase: ", buf, sizeof(buf), 0),
					"Error reading passphrase");

			if (!verify)
				break;

			char *verbuf = getpass("Verify:");
			if (!strncmp(buf, verbuf, sizeof(buf))) {
				bzero(verbuf, strlen(verbuf));
				break;
			}
			reportf(stderr, false, "Passphrases don't match. Try again");
		}
	}

	SHAify c;
	c.add((uint8_t *) buf, strlen(buf));
	bzero(buf, sizeof(buf));
	c.final((uint8_t *) &passbuf[0]);
}

// Dump the public key to stdout
static void writepublickey(R_RSA_PRIVATE_KEY &privkey, const char *nickname)
{
	int lc = 0;
	int ksize = (privkey.bits+7)/8;

	printf("WASTE_PUBLIC_KEY 20 %d %s\n", privkey.bits, nickname);

	int zeroes = MAX_RSA_MODULUS_LEN - ksize;
	uint8_t *data = &privkey.modulus[zeroes];
	writehexdata(stdout, data, ksize, lc);

	while (zeroes < MAX_RSA_MODULUS_LEN && !privkey.publicExponent[zeroes])
		zeroes++;

	ksize = MAX_RSA_MODULUS_LEN - zeroes;
	uint16_t ksizes = htons((uint16_t) ksize);
	writehexdata(stdout, &ksizes, sizeof(ksizes), lc);

	data = &privkey.publicExponent[MAX_RSA_MODULUS_LEN - ksize];
	writehexdata(stdout, data, ksize, lc);

	// Add a new line if necessary
	printf("%sWASTE_PUBLIC_KEY_END\n", (lc % MAX_LC)?"\n":"");
}

extern "C" int main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	R_RSA_PRIVATE_KEY privkey;
	uint8_t passbuf[SHA_OUTSIZE];

	assert(sizeof(passbuf) >= SHA_OUTSIZE);

	init(argc, argv);

	if (g_genkey) {
		getpassword(passbuf, /* verify */ true);
		genkeys(g_keylen, privkey, passbuf);
		bzero(passbuf, sizeof(passbuf));  // Blast the passphrase
	}

	if (g_dumpourpublic) {
		while (!privkey.bits) {
			getpassword(passbuf, /* verify */ false);
			(void) readprivatekey(privkey, passbuf);
			bzero(passbuf, sizeof(passbuf));  // Blast the passphrase
		}
		writepublickey(privkey, g_nickname);
	}

	// Zero out that sucker
	bzero(&privkey, sizeof(privkey));

	exit(EX_OK);
}
#endif // !_WIN32
