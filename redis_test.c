#include "server.h"
#include "cluster.h"
#include "slowlog.h"
#include "bio.h"
#include "latency.h"
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <locale.h>

typedef struct redis_test_client {
    int fd;                 /* Client socket. */
    sds querybuf;           /* Buffer we use to accumulate client queries. */
//    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size. */
    list *reply;            /* List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* Tot bytes of objects in reply list. */
    size_t sentlen;         /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* Client creation time. */
    time_t lastinteraction; /* Time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    int flags;              /* Client flags: CLIENT_* macros. */

    /* Response buffer */
    int bufpos;
    char buf[PROTO_REPLY_CHUNK_BYTES];
} redis_test_client;

static void unlink_redis_test_client(redis_test_client *c)
{
    /* Certain operations must be done only if the client has an active socket.
     * If the client was already unlinked or if it's a "fake client" the
     * fd is already set to -1. */
    if (c->fd != -1) {
        /* Unregister async I/O handlers and close the socket. */
        aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
        close(c->fd);
        c->fd = -1;
    }
}

static void free_redis_test_client(redis_test_client *c)
{
	printf("%s %d: fd = %d\n", __FUNCTION__, __LINE__, c->fd);	
    /* Free the query buffer */
    sdsfree(c->querybuf);
    c->querybuf = NULL;
    /* Free data structures. */
    listRelease(c->reply);

    /* Unlink the client: this will close the socket, remove the I/O
     * handlers, and remove references of the client from different
     * places where active clients may be referenced. */
    unlink_redis_test_client(c);
    zfree(c);
}

static void redis_test_read_handler(aeEventLoop *el, int fd, void *privdata, int mask)
{
	redis_test_client *c = (redis_test_client*) privdata;
    int nread, readlen;
    size_t qblen;
    UNUSED(el);
    UNUSED(mask);

    readlen = PROTO_IOBUF_LEN;
    qblen = sdslen(c->querybuf);
//    if (c->querybuf_peak < qblen) c->querybuf_peak = qblen;
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);
    nread = read(fd, c->querybuf+qblen, readlen);
    if (nread == -1) {
        if (errno == EAGAIN) {
            return;
        } else {
            serverLog(LL_VERBOSE, "Reading from client: %s",strerror(errno));
            free_redis_test_client(c);
            return;
        }
    } else if (nread == 0) {
        serverLog(LL_VERBOSE, "Client closed connection");
		free_redis_test_client(c);
        return;
    }

    sdsIncrLen(c->querybuf,nread);
    c->lastinteraction = server.unixtime;

	printf("%s %d: len = %zd, nread = %d, buf = %s\n", __FUNCTION__, __LINE__,
		sdslen(c->querybuf), nread, c->querybuf);	
	
//    processInputBuffer(c);
}

static redis_test_client *redis_test_create_client(int fd)
{
	redis_test_client *c = zmalloc(sizeof(redis_test_client));

    /* passing -1 as fd it is possible to create a non connected client.
     * This is useful since all the commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */
    if (fd != -1) {
        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);
        if (server.tcpkeepalive)
            anetKeepAlive(NULL,fd,server.tcpkeepalive);
        if (aeCreateFileEvent(server.el,fd,AE_READABLE,
            redis_test_read_handler, c) == AE_ERR)
        {
            close(fd);
            zfree(c);
            return NULL;
        }
    }

    c->fd = fd;
    c->bufpos = 0;
    c->querybuf = sdsempty();
    c->sentlen = 0;
    c->flags = 0;
    c->ctime = c->lastinteraction = server.unixtime;
    c->reply = listCreate();
    c->reply_bytes = 0;
    c->obuf_soft_limit_reached_time = 0;

	printf("%s %d: fd = %d\n", __FUNCTION__, __LINE__, fd);	
	
    return c;
}

#define MAX_ACCEPTS_PER_CALL 1000
static void redis_accept_handler(int fd, int flags, char *ip)
{
	UNUSED(ip);
	redis_test_client *c;
	if ((c = redis_test_create_client(fd)) == NULL) {
        serverLog(LL_WARNING,
            "Error registering fd event for the new client: %s (fd=%d)",
            strerror(errno),fd);
        close(fd); /* May be already closed, just ignore errors */
        return;
    }
    c->flags |= flags;
}

static void redis_test_accept(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd, max = MAX_ACCEPTS_PER_CALL;
    char cip[NET_IP_STR_LEN];
    UNUSED(el);
    UNUSED(mask);
    UNUSED(privdata);

    while(max--) {
        cfd = anetTcpAccept(server.neterr, fd, cip, sizeof(cip), &cport);
        if (cfd == ANET_ERR) {
            if (errno != EWOULDBLOCK)
                serverLog(LL_WARNING,
                    "Accepting client connection: %s", server.neterr);
            return;
        }
        serverLog(LL_VERBOSE,"Accepted %s:%d", cip, cport);
        redis_accept_handler(cfd,0,cip);
    }
}

void redis_test_init(int port)
{
	int fd[16];
	int count = 0;
	
    if (listenToPort(port, &fd[0], &count) == C_ERR)
        exit(1);
	int i;
	for (i = 0; i < count; i++) {
		if (aeCreateFileEvent(server.el, fd[i], AE_READABLE,
				redis_test_accept,NULL) == AE_ERR)
		{
			serverPanic(
				"Unrecoverable error creating server.ipfd file event.");
		}
	}
	printf("%s %d: port[%d] count[%d]\n", __FUNCTION__, __LINE__, port, count);
}

