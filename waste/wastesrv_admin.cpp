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

#include <stdint.h>
#include <getopt.h>

#include "platform.hpp"

#include "blowfish.hpp"
#include "config.hpp"
#include "configparams.hpp"
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
// Also I have used quite a few of the rarer apis like getopt_long & bzero
//
#if !_WIN32

#define MAX_LC 30

static char g_waste_dir[MAXPATHLEN];
static const char *g_cmdname;
static char g_curProfile[MAXPATHLEN];
static int g_verbose_level;
static uint8_t g_passhash[SHA_OUTSIZE];
static C_Config *g_config;
static bool g_passhash_valid = false;

/* static */ R_RANDOM_STRUCT g_random;

static const char g_profile_default[] = "default";
static const char g_nickname_default[] = "unknown";
static const unsigned g_keylen_default = 1536;
static const unsigned g_listenport_default = CONFIG_port_DEFAULT;

static const int g_keysizes[] = { 1024, 1536, 2048, 3072, 4096 };
#define NUM_KEY_SIZES ((int) (sizeof(g_keysizes)/sizeof(g_keysizes[0])))

enum LevelsOfVerbosity {
	VNONE = 0,
	VLEAST,
	VMOST	// Make sure this is always last
};

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

#if __gnu_linux__
#define sized_bzero(buf)	memset(buf, 0, sizeof(buf))
#define struct_bzero(strct)	memset(strct, 0, sizeof(strct))
#define string_bzero(str)	memset(str, 0, strlen(str))
#else // probably bsd based
#define sized_bzero(buf)	bzero(buf, sizeof(buf))
#define struct_bzero(strct)	bzero(strct, sizeof(*strct))
#define string_bzero(str)	bzero(str, strlen(str))
#endif


// process argument list from getopt_long, updates optarg & optind
class decodedArgs {
	enum argumentValue {
		arg_no_advertise_listen	=  1,
		arg_no_export_ext		=  2,
		arg_no_forceip_dynip	=  3,
		arg_no_listen			=  4,
		arg_no_route_traffic	=  5,
		arg_no_store_passphrase	=  6,
		arg_advertise_listen	= 'A',
		arg_connections			= 'c',
		arg_data_path			= 'd',
		arg_display_public_key	= 'k',
		arg_download_dir		= 'D',
		arg_export_ext			= 'e',
		arg_forceip_dynip		= 'F',
		arg_generate_key		= 'g',
		arg_help				= 'h',
		arg_interactive			= 'i',
		arg_licence				= 'L',
		arg_listen				= 'l',
		arg_nickname			= 'n',
		arg_passphrase			= 'f',
		arg_port				= 'P',
		arg_profile				= 'p',
		arg_public_key			= 'a',
		arg_quit				= 'q',
		arg_read_configuration	= 'R',
		arg_route_traffic		= 'r',
		arg_speed				= 's',
		arg_store_passphrase	= 'S',
		arg_verbose				= 'v',
	};

	static struct option LongOpts[];
	static const char ShortOpts[];

	void appendArgs(const char **&list, int argc, char * const argv[]);

public:
	char *fPassphrase;	// May be overwritten with NULs
	const char **fConnections;
	const char **fDataDirs;
	const char **fExtensions;
	const char **fPublicKeys;
	const char *fDownDir;
	const char *fForceDynip;
	const char *fNickname;
	const char *fProfile;
	unsigned fConSpeed;
	unsigned fKeyLen;
	unsigned fListenPort;
	bool fAdvertiseListen, fAdvertiseListenChanged;
	bool fReadConfiguration;
	bool fDumpOurPublic;
	bool fForceDynipChanged;
	bool fInteractive;
	bool fListen, fListenChanged;
	bool fNoExtens, fNoExtensChanged;
	bool fPassphraseChanged;
	bool fQuit;
	bool fRouteTraffic, fRouteTrafficChanged;
	bool fShowLicence;
	bool fStorePassphrase, fStorePassChanged;

	decodedArgs() { memset(this, 0, sizeof(decodedArgs)); };
	~decodedArgs()
	{
		if (fConnections) free(fConnections);
		if (fDataDirs)    free(fDataDirs);
		if (fExtensions)  free(fExtensions);
		if (fPublicKeys)  free(fPublicKeys);
	};

	bool decode(int argc, char * const argv[]);
	static void help();
};

void decodedArgs::help()
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n", g_cmdname);
	fputs(
"NB: none of the functionality to remove an element is implemented as yet\n"
"-a  --public-key [<keys>]           (Not implemented)\n"
"                                    append public keys from the file in key\n"
"                                    ^ prefix means to remove the keys listed\n"
"                                    in the file <key>, however if <key> is \n"
"                                    not a file then remove the key with the\n"
"                                    nickname <key>\n"
"                                    prefix file with ^ to remove keys\n"
"-A  --advertise-listen              advertise ourselves as a listener\n"
"    --no-advertise-listen           don't advertise ourselves as a listener\n"
"-c  --connections [host:port ...]   add list of connections,\n"
"                                    '^' prefix means to remove connection\n"
"-d  --data-path <dir> [<dir>...]    list of directories to publish\n"
"                                    prefix directory with '^' to remove\n"
"-D  --download-dir <dir>            set the download-directory to <dir>\n"
"-e  --export-ext ext [ext...]       enable extension restrction and add list\n"
"                                    prefix with ^ to remove extension\n"
"    --no-export-ext                 disable extension restrction\n"
"-f  --passphrase <passphrase>       the past phrase for the profile's secret key\n"
"-F  --force-ip <this host>          Enable force ip and set ip host\n"
"    --no-force-ip                   Disable force ip mode\n"
"-g  --generate-key [length]         generate a private/public key pair (1536)\n"
"                                    1024 bits - weak, not recommended\n"
"                                    1536 bits - recommended\n"
"                                    2048 bits - slower, recommended\n"
"                                    3072 bits - slow, not recommended\n"
"                                    4096 bits - very slow, not recommended\n"
"-h  --help                          print this help\n"
"-i  --interactive                   (Not implemented) enter interactive mode\n"
"                                    interactive mode uses these commands\n"
"-k  --display-public-key            display the profile's public key\n"
"-L  --licence                       display the licence terms of this software\n"
"-l  --listen                        listen for route traffic\n"
"    --no-listen                     don't listen for route traffic\n"
"-n  --nickname                      nickname of profile's user\n"
"-p  --profile <profile_name>        name of profile to use (default)\n"
"-P  --port <port>                   network port to listen on (1337)\n"
"                                    profiles are stored in " WASTE_CONFIG_DIR "\n"
"-q  --quit                          Quit from interactive mode\n"
"-r  --route-traffic                 route traffic on network\n"
"    --no-route-traffic              don't route traffic on network\n"
"-R  --read-configuration            (not implemented) display the current configuration\n"
"-s  --speed <speed>                 speed of connection in kilo-bits\n"
"-S  --store-passphrase              enable use of stored passphrase and save it\n"
"    --no-store-passphrase           disables and deletes the store passphrase\n"
"-v  --verbose [level]               increase the level of verbosity\n"
"                                    more v's means => higher level\n"
	, stderr);

	char *szCR2;
	szCR2= (char*) malloc(sK0[1]);
	safe_strncpy(szCR2, (char *) sK1[1], sK0[1]);
	dpi(szCR2, 2);
	fprintf(stderr, "\n%s\n", szCR2);
	memset(szCR2, 0, sK0[1]);
	free(szCR2);
}

// NB: order is important, the enum above and this array must be
// kept in synch if the argument processing is going to work
struct option decodedArgs::LongOpts[] = {
	// arg_no_advertise_listen,		--no-advertise-listen
	{ "no-advertise-listen",no_argument,		0, arg_no_advertise_listen },
	// arg_no_export_ext,			--no-export-ext
	{ "no-export-ext",		no_argument,		0, arg_no_export_ext },
	// arg_no_forceip_dynip,		--no-force-ip
	{ "no-force-ip",		no_argument,		0, arg_no_forceip_dynip },
	// arg_no_listen,				--no-listen
	{ "no-listen",			no_argument,		0, arg_no_listen },
	// arg_no_route_traffic,		--no-route-traffic
	{ "no-route-traffic",	no_argument,		0, arg_no_route_traffic },
	// arg_no_store_passphrase,		--no-store-passphrase
	{ "no-store-passphrase",no_argument,		0, arg_no_store_passphrase },
	// arg_advertise_listen,	-A  --advertise-listen
	{ "advertise-listen",	no_argument,		0, arg_advertise_listen },
	// arg_connections,			-c  --connections [host:port ...]
	{ "connections",		required_argument,	0, arg_connections },
	// arg_data_path,			-d  --data-path <dir> [<dir>...]
	{ "data-path",			required_argument,	0, arg_data_path },
	// arg_display_public_key,	-k  --display-public-key
	{ "display-public-key",	no_argument,		0, arg_display_public_key },
	// arg_download_dir,		-D  --download-dir <dir>
	{ "download-dir",		required_argument,	0, arg_download_dir },
	// arg_export_ext			-e  --export-ext ext [ext...]
	{ "export-ext",			required_argument,	0, arg_export_ext },
	// arg_forceip_dynip,		-F	--force-ip <this host>
	{ "force-ip",			required_argument,	0, arg_forceip_dynip },
	// arg_generate_key			-s  --generate-key [length]
	{ "generate-key",		optional_argument,	0, arg_generate_key },
	// arg_help					-h  --help
	{ "help",				no_argument,		0, arg_help },
	// arg_interactive			-i  --interactive
	{ "interactive",		no_argument,		0, arg_interactive },
	// arg_licence				-L  --licence
	{ "licence",			no_argument,		0, arg_licence },
	// arg_listen				-l  --listen
	{ "listen",				no_argument,		0, arg_listen },
	// arg_nickname				-n  --nickname
	{ "nickname",			required_argument,	0, arg_nickname },
	// arg_passphrase			-f  --passphrase <passphrase>
	{ "passphrase", 		optional_argument,	0, arg_passphrase },
	// arg_port,				-P  --port <port>
	{ "port",				required_argument,	0, arg_port },
	// arg_profile				-p  --profile <profile_name>
	{ "profile",			required_argument,	0, arg_profile },
	// arg_public_key			-a  --public-key [<keys>]
	{ "public-key",			optional_argument,	0, arg_public_key },
	// arg_quit					-q  --quit
	{ "quit",				no_argument,		0, arg_quit },
	// arg_read_configuration	-R  --read-configuration
	{ "read-configuration",	no_argument,		0, arg_read_configuration },
	// arg_route_traffic		-r  --route-traffic
	{ "route-traffic",		no_argument,		0, arg_route_traffic },
	// arg_speed,				-C  --speed <speed>
	{ "speed",				required_argument,	0, arg_speed },
	// arg_store_passphrase		-S  --store-passphrase
	{ "store-passphrase",	no_argument,		0, arg_store_passphrase },
	// arg_verbose				-v  --verbose [level]
	{ "verbose",			optional_argument,	0, arg_verbose },
	{ 0,        			0,                	0,  0  },
};

const char decodedArgs::
ShortOpts[] = "Aa::c:D:d:e:F:f::g::hikLln:P:p:qRrSs:v::";

void decodedArgs::appendArgs(const char **&list, int argc, char * const argv[])
{
	if (!list) {
		// Maximum size of list is when the every argument except the 
		// initial 'flag' is an argument, obviously can't happen 4 times but
		// at this poing memory really is cheap as we will be returning very
		// soon.
		int maxElements = argc - optind;
		list = (const char **) calloc(maxElements, sizeof(const char *));
	}

	if (optarg) {
		// find end of list
		int i;
		for (i = 0; list[i]; i++)
			;

		do {
			list[i++] = optarg;
			optarg = argv[optind++];
		} while (optarg && '-' != *optarg);
		optind--;	// Undo the one passed argument walk
	}
}

bool decodedArgs::decode(int argc, char * const argv[])
{
	char c;
	int argInd = 0;

	bzero(this, sizeof(*this));
    while ((c = getopt_long(argc, argv, ShortOpts, LongOpts, &argInd)) != -1) {
		if (c) {
			// shortOpt, translate to argInd
			for (argInd = 0; LongOpts[argInd].name; argInd++)
				if (LongOpts[argInd].val == c)
					break;	// Found the character
		}
		else { 	// !c
			assert(argInd);
			c = LongOpts[argInd].val;
		}

		// optional argument processing
		if (LongOpts[argInd].has_arg == optional_argument && !optarg
		&&  optind < argc && *argv[optind] != '-')
			optarg = argv[optind++];

        switch (c) {

		// arg_no_advertise_listen,		--no-advertise-listen
		case arg_no_advertise_listen:
			fAdvertiseListenChanged = true; fAdvertiseListen = false; break;

		// arg_no_export_ext,			--no-export-ext
		case arg_no_export_ext:
			if (fExtensions) {
				reportf(stderr, 0,
					"Warning: disabling previously defined extensions");
				free(fExtensions); fExtensions = 0;
			}
			fNoExtensChanged = true;
			fNoExtens = true;
			break;

		// arg_no_forceip_dynip,		--no-force-ip
		case arg_no_forceip_dynip:
			if (fForceDynip)
				reportf(stderr, 0,
					"Warning: disabling previously defined dynip address");
			fForceDynipChanged = true;
			fForceDynip = 0;
			break;

		// arg_no_listen,				--no-listen
		case arg_no_listen:
			fListenChanged = true; fListen = false; break;

		// arg_no_route_traffic,		--no-route-traffic
		case arg_no_route_traffic:
			fRouteTrafficChanged = true; fRouteTraffic = false; break;

		// arg_no_store_passphrase,		--no-store-passphrase
		case arg_no_store_passphrase:
			fStorePassChanged = true; fStorePassphrase = false; break;

		// arg_advertise_listen,	-A  --advertise-listen
		case arg_advertise_listen:
			fAdvertiseListenChanged = true; fAdvertiseListen = true; break;

		// arg_connections,			-c  --connections [host:port ...]
		case arg_connections: appendArgs(fConnections, argc, argv); break;

		// arg_speed,				-C  --speed <speed>
		case arg_speed: fConSpeed = strtol(optarg, 0, 10); break;

		// arg_data_path,			-d  --data-path <dir> [<dir>...]
		case arg_data_path:  appendArgs(fDataDirs, argc, argv); break;

		// arg_display_public_key,	-k  --display-public-key
		case arg_display_public_key: fDumpOurPublic = true; break;

		// arg_download_dir,		-D  --download-dir <dir>
		case arg_download_dir:
			if (fDownDir) 
				reportf(stderr, 0,
					"Warning: replacing previous download dir with %s", optarg);
			fDownDir = optarg;
			break;

		// arg_export_ext			-e  --export-ext ext [ext...]
		case arg_export_ext:
			if (fNoExtens) {
				reportf(stderr, 0,
					"Warning: enabling extensions after disable");
				fNoExtens = false;
			}
			fNoExtensChanged = true;
			appendArgs(fExtensions, argc, argv);
			break;

		// arg_forceip_dynip,		-F  --force-ip <this host>
		case arg_forceip_dynip:
			if (fForceDynipChanged && !fForceDynip)
				reportf(stderr, 0,
					"Warning: enabling force dynamic ip after disable");
			fForceDynipChanged = true;
			fForceDynip = optarg;
			break;

		// arg_generate_key			-s  --generate-key [length]
		case arg_generate_key: {
			unsigned num = (optarg)? strtol(optarg, 0, 10) : g_keylen_default;
			if (fKeyLen && num != fKeyLen)
				reportf(stderr, 0,
					"Warning: replacing previous keylen with %s", optarg);
			fKeyLen = num;
			break;
		}

		// arg_help					-h  --help
		// See default: below

		// arg_interactive			-i  --interactive
		case arg_interactive:		// fInteractive = true; break;
			reportf(stderr, 0, "Warning: --interactive not implemented");
			break;

		// arg_licence				-L  --licence
		case arg_licence: fShowLicence = true; break;

		// arg_listen				-l  --listen
		case arg_listen:
			fListenChanged = true; fListen = true; break;

		// arg_port					-P  --listen-to-port <port>
		case arg_port: fListenPort = strtol(optarg, 0, 10); break;

		// arg_nickname				-n  --nickname
		case arg_nickname:
			if (fNickname) 
				reportf(stderr, 0,
					"Warning: replacing previous nickname with %s", optarg);
			fNickname = optarg;
			break;

		// arg_passphrase			-f  --passphrase [<passphrase>]
		case arg_passphrase:
			if (fPassphrase) 
				reportf(stderr, 0,
					"Warning: replacing previous passphrase", optarg);
			fPassphrase = optarg;
			fPassphraseChanged = true;
			break;

		// arg_profile				-p  --profile <profile_name>
		case arg_profile:
			if (fProfile) 
				reportf(stderr, 0,
					"Warning: replacing previous profile with %s", optarg);
			fProfile = optarg;
			break;

		// arg_public_key			-a  --public-key [<keys>]
		case arg_public_key: appendArgs(fPublicKeys, argc, argv); // break;
			reportf(stderr, 0, "Warning: --public-key not implemented"); break;

		// arg_quit					-q  --quit
		case arg_quit: fQuit = true; break;

		// arg_read_configuration	-R  --read-configuration
		case arg_read_configuration: // fReadConfiguration = true; break;
			reportf(stderr, 0, "Warning: --read-configuration not implemented"); break;

		// arg_route_traffic		-r  --route-traffic
		case arg_route_traffic:
			fRouteTrafficChanged = true; fRouteTraffic = true; break;

		// arg_store_passphrase		-S  --store-passphrase
		case arg_store_passphrase:
			fStorePassChanged = true; fStorePassphrase = true; break;

		// arg_verbose				-v  --verbose [level]
		case arg_verbose:
			if (optarg)
				g_verbose_level = strtol(optarg, 0, 10);
			else
				g_verbose_level++;
			break;

		case arg_help:
		default:
			help();
			return false;
        }
		argInd = 0;
    }
	return true;
}

static void licence(void)
{
	char *szLI2;

	szLI2 = (char*) malloc(sK0[3]);
	safe_strncpy(szLI2, (char*) sK1[3], sK0[3]);
	dpi(szLI2, 4);
	fprintf(stderr, "\n\n%s\n", szLI2);
	memset(szLI2, 0, sK0[3]);
	free(szLI2);
}

static void checkEntropy()
{
	static bool g_random_loaded;

	if (!g_random_loaded) {
		// Grab some entropy from the system and use it to initalise RSA

		int randfd;
		unsigned int need;

		verbosef(VLEAST, "Loading up the random generator with entropy\n");

		R_RandomInit(&g_random);
		sysc_test((randfd = open("/dev/random", O_RDONLY)),
			"failed to open entropy source %s", "/dev/random");
		
		R_GetRandomBytesNeeded(&need, &g_random);
		do {
			unsigned char buffer[1024];
			int cc;

			need = min(need, sizeof(buffer));
			sysc_test((cc = read(randfd, buffer, need)),
					"can't read entropy data");
			assert(cc);

			R_RandomUpdate(&g_random, buffer, cc);
			verbosef(VMOST, "Loaded %d bytes\n", cc);
			R_GetRandomBytesNeeded(&need, &g_random);
		} while(need > 0);

		close(randfd);
		verbosef(VLEAST, "Done loading\n");
		g_random_loaded = true;
	}
}

static void
loadpasshash(char *passwd, bool verify)
{
	char buf[PASS_MAX];

	if (g_passhash_valid)
		return;

	if (passwd) {
		safe_strncpy(buf, passwd, sizeof(buf));
		string_bzero(passwd);
	}
	else {
		for (;;) {
			libc_test(readpassphrase("Passphrase: ", buf, sizeof(buf), 0),
					"Error reading passphrase");

			if (!verify)
				break;

			char *verbuf = getpass("Verify:");
			if (!strncmp(buf, verbuf, sizeof(buf))) {
				string_bzero(verbuf);
				break;
			}
			reportf(stderr, false, "Passphrases don't match. Try again");
		}
	}

	SHAify c;
	c.add((uint8_t *) buf, strlen(buf));
	sized_bzero(buf);
	c.final((uint8_t *) g_passhash);
	g_passhash_valid = true;
}

// Actually generate a private key.  By the way the guesstimates are
// still fairly accurate.  I took 15 minutes to generate a 4096 size
// key on a 1.25 G4 powerbook.  Boy was I surprised.
static void
genthekeys(R_RSA_PRIVATE_KEY &privKey, int keyLen)
{
	R_RSA_PROTO_KEY protkey;
	R_RSA_PUBLIC_KEY pubkey;

	protkey.bits = keyLen;
	protkey.useFermat4 = true;
	verbosef(VLEAST, "Generating keys this may take as long as %d minutes...",
		keyLen <= 1024? 2 : keyLen <= 2048? 3 : keyLen <= 3072? 10: 20);
	fflush(stderr);

	checkEntropy();
	time_t delta = time(0);
	if (R_GeneratePEMKeys(&pubkey, &privKey, &protkey, &g_random)) {
		reportf(stderr, false, "Error generating keys");
		exit(EX_SOFTWARE);
	}
	delta = time(0) - delta;

	verbosef(VLEAST, " Done %dm %2ds\n", delta / 60, delta % 60);
}

static void
bin2hex(char *dst, uint8_t *in, size_t size)
{
	static const char nibble[] = "0123456789ABCDEF";

	for (unsigned i = 0; i < size; i++) {
		uint8_t c = *in++;
		*dst++ = nibble[(c >>  4)];
		*dst++ = nibble[(c & 0xf)];
	}
	*dst = 0;
}

// Write the data out to the given file in uppercase (blech) hex
static void
writehexdata(FILE *outf, const void *data, unsigned len, int &lc)
{
	const uint8_t *cp = (const uint8_t *) data;
	char outbuf[3];

	outbuf[2] = 0;
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
static void writeprivatekey(R_RSA_PRIVATE_KEY &privKey)
{
	char path[MAXPATHLEN];
	FILE *outf;

	sized_snprintf(path, "%s.pr4", g_curProfile);
	verbosef(VMOST, "Private key file %s\n", path);
	
	int fd;
	sysc_test(fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR),
			"Can't create private key file %s", path);
	libc_test((outf = fdopen(fd, "w")),
			"Can't open private key file %s", path);

	fprintf(outf, "WASTE_PRIVATE_KEY 10 %d\n", privKey.bits);

	int lc = 0;
	unsigned long tl[2];

	CBlowfish bl(g_passhash, SHA_OUTSIZE);

	checkEntropy();
	R_GenerateBytes((unsigned char *) tl, sizeof(tl), &g_random);
	writehexdata(outf, (unsigned char *) tl, sizeof(tl), lc);
	bl.SetIV(CBlowfish::IV_BOTH, tl);
	
#define WPK(field) writeBFdata(outf, bl, field, sizeof(field), lc)
	writeBFdata(outf, bl, "PASSWORD", 8, lc);
	WPK(privKey.modulus);
	WPK(privKey.publicExponent);
	WPK(privKey.exponent);
	WPK(privKey.prime);
	WPK(privKey.primeExponent);
	WPK(privKey.coefficient);
#undef WPK

	// Add a new line if necessary
	if (lc % MAX_LC)
		fputc('\n', outf);

	fprintf(outf, "WASTE_PRIVATE_KEY_END\n");
	fclose(outf);

	bl.Final();
}

static inline unsigned hex2bin_nibble(char c)
{
	unsigned t = tolower(c) - '0';
	if (t > 9) {
		t += '0' - 'a';	// add the '0' back in and subtract an 'a'
		if (t <= 0xf - 0xa)
			t += 0xa;
		else
			return (unsigned) -1;
	}
	return t;
}

#if 0
static unsigned char *
hex2bin(unsigned char *dst, const char *src, size_t dstlen)
{
	unsigned char *cp = dst;

	for (unsigned i = 0; i < dstlen; i++) {
		 unsigned byte = hex2bin_nibble(*src++);
		 if ( !(byte + 1) )	// if byte == -1
			 return 0;
		 byte = (byte << 4) | hex2bin_nibble(*src++);
		 if ( !(byte + 1) )	// if byte == -1
			 return 0;
		 *cp++ = byte;
	}

	return dst;
}
#endif

// Read a single byte's worth of hex data.  Wrap across lines and ignore
// some input characters.
static inline int readhexchar(FILE *inf) //returns -1 on error
{
	static const char ignorelist[] = "\n\r\t -_<>:|";
    int c;

    do {
		if (EOF == (c = fgetc(inf)))
			return -1;
	} while (strchr(ignorelist, (char) c));

	// Get high nibble
	int  byte = (int) hex2bin_nibble(c);
	if (byte < 0 || (EOF == (c = fgetc(inf))))
		return -1;

	return (byte << 4) | hex2bin_nibble(c);
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

// Read and verify the private key using the g_passhash as a key
// to the blowfish encipherment.
static int readprivatekey(R_RSA_PRIVATE_KEY &privKey)
{
	int goagain = 0;
	char path[MAXPATHLEN];
	FILE *inf;
	char *err = NULL;
	char linebuf[1024];
	CBlowfish bl;

	sized_snprintf(path, "%s.pr4", g_curProfile);
	verbosef(VMOST, "Private key file %s\n", path);
	
    bl.Init(g_passhash, SHA_OUTSIZE);

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
		|| (privKey.bits = strtoul(cp, NULL, 10)) > MAX_RSA_MODULUS_BITS
		||  privKey.bits < MIN_RSA_MODULUS_BITS)
			goto abortError;
	}

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

#define WPK(x) readBFdata(inf, bl, privKey.x, sizeof(privKey.x))

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
	privKey.bits = 0;
	fclose(inf);
	return !goagain;
}

static void
genkeys(int keyLen, R_RSA_PRIVATE_KEY &privKey)
{
	int i = 0;

	for (; i < NUM_KEY_SIZES; i++)
		if (g_keysizes[i] == keyLen)
			break;
	if (i < NUM_KEY_SIZES)
		verbosef(VMOST, "keyLen = %d\n", keyLen);
	else // Invalid key len not found;
		reportf(stderr, false, "Invalid key length requested");

	// The given password is ALWAYS ignored when generating a key
	loadpasshash(NULL, /* verify */ true);

	genthekeys(privKey, keyLen);
	writeprivatekey(privKey);
}

// Dump the public key to stdout
static void writepublickey(R_RSA_PRIVATE_KEY &privKey)
{
	int lc = 0;
	int ksize = (privKey.bits+7)/8;
	const char *nickname =
		g_config->ReadString(CONFIG_nick, g_nickname_default);

	printf("WASTE_PUBLIC_KEY 20 %d %s\n", privKey.bits, nickname);

	int zeroes = MAX_RSA_MODULUS_LEN - ksize;
	uint8_t *data = &privKey.modulus[zeroes];
	writehexdata(stdout, data, ksize, lc);

	while (zeroes < MAX_RSA_MODULUS_LEN && !privKey.publicExponent[zeroes])
		zeroes++;

	ksize = MAX_RSA_MODULUS_LEN - zeroes;
	uint16_t ksizes = htons((uint16_t) ksize);
	writehexdata(stdout, &ksizes, sizeof(ksizes), lc);

	data = &privKey.publicExponent[MAX_RSA_MODULUS_LEN - ksize];
	writehexdata(stdout, data, ksize, lc);

	// Add a new line if necessary
	printf("%sWASTE_PUBLIC_KEY_END\n", (lc % MAX_LC)?"\n":"");
}

// load the configuration data base
static void checkProfile(const char *profile)
{
	if (profile || !g_curProfile[0]) {
		sized_snprintf(g_curProfile, "%s%s", g_waste_dir, 
				(profile)? profile : g_profile_default);
	}

	if (g_config) {
		// Shut down the previous profile if necessary
		int profLen = strlen(g_curProfile);
		if (memcmp(g_config->GetIniFile(), g_curProfile, profLen)) {
			sized_bzero(g_passhash);
			delete g_config;
			g_config = 0;
		}
	}

	// Create the waste config dir if necessary
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

	// Load the new confguration database
	char path[MAXPATHLEN];
	sized_snprintf(path, "%s.pr0", g_curProfile);
	libc_test(g_config = new C_Config(path),
			"Couldn't load configuration file %s", path);

	// Create the clientid GUID if necessary
	if ( !*(g_config->ReadString(CONFIG_clientid128, "")) ) {
		T_GUID id;
		char idbuf[sizeof(id) * 2 + 1];

		checkEntropy();
		R_GenerateBytes((unsigned char *) id.idc, sizeof(id.idc), &g_random);
		bin2hex(idbuf, id.idc, sizeof(id.idc));
		g_config->WriteString(CONFIG_clientid128, idbuf);
	}
}

static void storeList(const char *name, const char **argv, char sep)
{
	// XXX gvdl: Need to implement removal of list elements
	int i, len;
	for (i = 0, len = 0; argv[i]; i++)
		len += 1 + strlen(argv[i]);

	char buf[len + 1], *cp = buf;
	for (i = 0, cp = buf; argv[i]; i++) {
		int cc = sprintf(cp, "%s%c", argv[i], sep);
		cp += cc;
		assert(!*cp);
	}

	buf[len-1] = 0;	// Get rid of trailing seperator
	g_config->WriteString(name, buf);
}

extern "C" int main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    // Get the command name and strip the leading dirpath
    if ( (g_cmdname = strrchr(argv[0], '/')) )
        g_cmdname++;
    else
        g_cmdname = argv[0];

    if (argc == 1) {
		decodedArgs::help();
		exit(EX_USAGE);
	}

	// find the user's home directory
	const char *home = getenv("HOME");
	if (!home) {
		reportf(stderr, false, "Can't find users home directory");
		exit(EX_SOFTWARE);
	}
	sized_snprintf(g_waste_dir, "%s%s", home, &WASTE_CONFIG_DIR[1]);
	
	R_RSA_PRIVATE_KEY privKey;
	privKey.bits = 0;

	bool interactive = false;
	decodedArgs a;
	if (a.decode(argc, argv))
		interactive = a.fInteractive;
	else
		exit(EX_USAGE);

	do {
		if (a.fShowLicence)
			licence();

		checkProfile(a.fProfile);

		if (a.fKeyLen) {
			if (a.fPassphraseChanged && a.fPassphrase)
				reportf(stderr, false, "Generating key - ignoring password");
			genkeys(a.fKeyLen, privKey);
		}

		if (a.fPassphraseChanged) {
			if (g_passhash_valid)
				string_bzero(a.fPassphrase);
			else
				loadpasshash(a.fPassphrase, /* verify */ false);
			a.fPassphrase = 0;
		}
		assert(!a.fPassphrase);

		// fAdvertiseListen CONFIG_advertise_listen
		if (a.fAdvertiseListenChanged)
			g_config->WriteInt(CONFIG_advertise_listen, a.fAdvertiseListen);

		// fNoExtens CONFIG_use_extlist fExtensions CONFIG_extlist
		if (a.fNoExtensChanged) {
			g_config->WriteInt(CONFIG_use_extlist, !a.fNoExtens);
			if (a.fNoExtens || !a.fExtensions)
				g_config->WriteString(CONFIG_extlist, "");
			else if (a.fExtensions)
				storeList(CONFIG_extlist, a.fExtensions, ';');
		}

		// fForceDynip CONFIG_forceip_dynip_mode CONFIG_forceip_name
		if (a.fForceDynipChanged) {
			g_config->WriteInt(CONFIG_forceip_dynip_mode, a.fForceDynip != 0);
			g_config->WriteString(CONFIG_forceip_name, a.fForceDynip);
		}

		// fListen CONFIG_listen
		if (a.fListenChanged)
			g_config->WriteInt(CONFIG_listen, a.fListen);

		// fRouteTraffic CONFIG_route
		if (a.fRouteTrafficChanged)
			g_config->WriteInt(CONFIG_route, a.fRouteTraffic);

		// fConnections ==> .pr1
		if (a.fConnections) {
			char path[MAXPATHLEN];
			FILE *conf;
			int fd;

			sized_snprintf(path, "%s.pr1", g_curProfile);
			sysc_test(fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR),
					"Can't create connection file %s", path);
			libc_test((conf = fdopen(fd, "w")),
					"Can't open connection file", path);

			for (int i = 0; a.fConnections[i]; i++)
				fprintf(conf, "%s:0\n", a.fConnections[i]);
			fclose(conf);
		}

		// fConSpeed CONFIG_conspeed
		if (a.fConSpeed)
			g_config->WriteInt(CONFIG_conspeed, a.fConSpeed);

		// fDataDirs CONFIG_databasepath
		if (a.fDataDirs)
			storeList(CONFIG_databasepath, a.fDataDirs, ':');

		// fDownDir CONFIG_downloadpath
		if (a.fDownDir)
			g_config->WriteString(CONFIG_downloadpath, a.fDownDir);

		// fListen CONFIG_listen
		if (a.fListenChanged)
			g_config->WriteInt(CONFIG_listen, a.fListen);

		// fListenPort  CONFIG_port
		if (a.fListenPort)
			g_config->WriteInt(CONFIG_port, a.fListenPort);

		// fPublicKeys ==> .pr3

		// fReadConfiguration

		// fNickname CONFIG_nick
		if (a.fNickname)
			g_config->WriteString(CONFIG_nick, a.fNickname);

		if (a.fDumpOurPublic) {
			if (!privKey.bits) {
				for (;;) {
					loadpasshash(NULL, /* verify */ false);
					a.fPassphrase = 0;
					if (readprivatekey(privKey))
						break;
					else
						g_passhash_valid = false;	// reload the password
				}
			}
			if (privKey.bits)
				writepublickey(privKey);
		}

		// fStorePassChanged CONFIG_storepass CONFIG_keypass
		if (a.fStorePassChanged) {
			if (a.fStorePassphrase) {
				loadpasshash(NULL, /* verify */ false);

				char passhashhex[1 + SHA_OUTSIZE * 2];
				bin2hex(passhashhex, g_passhash, sizeof(g_passhash));

				g_config->WriteInt(CONFIG_storepass, 2);
				g_config->WriteString(CONFIG_keypass, passhashhex);
				sized_bzero(passhashhex);
			}
			else {
				g_config->WriteInt(CONFIG_storepass, false);
				g_config->WriteString(CONFIG_keypass, "");
			}
		}

		if (!interactive || a.fQuit)
			break;

		verbosef(VMOST, "argument processing\n");
		(void) a.decode(argc, argv);
	} while (interactive);

	// Zero out sensitive data
	R_RandomFinal(&g_random);
	struct_bzero(&g_random);
	struct_bzero(&privKey);
	sized_bzero(g_passhash);

	// Complete the configuration dump
	if (g_config)
		delete g_config;

	exit(EX_OK);
}
#endif // !_WIN32
