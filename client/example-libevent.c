#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>
//mkfifo /tmp/f
//cat /tmp/f | nc -k -v -l 6379 > /tmp/f
void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    printf("%s[%p %p %p]\n", __FUNCTION__, c, r, privdata);
    redisReply *reply = r;
    if (reply == NULL) return;
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
//    redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected... %p\n", c);
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected... %p\n", c);
}

static struct timeval sg_timeout = {60, 0};
static struct event sg_event_timer;
static struct event_base *base;
int add_timer(struct timeval t, struct event *event_timer, void *arg);
static void cb_timer(evutil_socket_t fd, short events, void *arg)
{
	(void)(fd);
	(void)(events);	
	add_timer(sg_timeout, &sg_event_timer, arg);
//	printf("%s: fd = %d, events = %d, arg = %p", __FUNCTION__, fd, events, arg);
//	if (arg)
//		event_free((struct event *)arg);
}

int add_timer(struct timeval t, struct event *event_timer, void *arg)
{
	if (!event_timer) {
		event_timer = evtimer_new(base, cb_timer, arg);
		if (!event_timer) {
			printf("%s %d: evtimer_new failed[%d]", __FUNCTION__, __LINE__, errno);					
			return (-1);
		}
		event_timer->ev_arg = event_timer;
	} else if (!(event_timer->ev_flags & EVLIST_TIMEOUT)) {
		evtimer_assign(event_timer, base, event_timer->ev_callback, arg);
	}

	return evtimer_add(event_timer, &t);
}

#define TEST_NUM 10000
int main (int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    base = event_base_new();
	static redisAsyncContext *c[TEST_NUM];
	int i;
	int port = 6379;
	const char *ip = "127.0.0.1";
	int test_num = TEST_NUM;
	if (argc > 1)
		ip = argv[1];
	if (argc > 2)
		port = atoi(argv[2]);
	if (argc > 3)
		test_num = atoi(argv[3]);

	for (i = 0; i < test_num; i++) {
		c[i] = redisAsyncConnect(ip, port);
		if (c[i]->err) {
				/* Let *c leak for now... */
			printf("Error: %s\n", c[i]->errstr);
			return 1;
		}
		printf("c[%d] = %p\n", i, c[i]);
		redisLibeventAttach(c[i], base);
		redisAsyncSetConnectCallback(c[i], connectCallback);
		redisAsyncSetDisconnectCallback(c[i], disconnectCallback);
		redisAsyncCommand(c[i], NULL, NULL, "SET key %b", argv[argc-1], strlen(argv[argc-1]));
		redisAsyncCommand(c[i], getCallback, (char*)"end-1", "GET key");
    }

	sg_event_timer.ev_callback = cb_timer;
	add_timer(sg_timeout, &sg_event_timer, &sg_event_timer);	
		
    event_base_dispatch(base);
    return 0;
}
