#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

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

#define TEST_NUM 1000
int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    struct event_base *base = event_base_new();
	static redisAsyncContext *c[TEST_NUM];
	int i;

	for (i = 0; i < TEST_NUM; i++) {
		c[i] = redisAsyncConnect("127.0.0.1", 6379);
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
		
    event_base_dispatch(base);
    return 0;
}
