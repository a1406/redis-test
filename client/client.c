#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

#define TEST_NUM 100000

//static struct timeval sg_timeout = {5, 0};
static int test_num = TEST_NUM;
static int connected_num;
static struct epoll_event all_events[TEST_NUM];

static int connect_tcp(const char *addr, int port);
static int get_next_timeout();
static void refresh_next_timer(int r);

int on_recv(int fd)
{
	static char buf[1024];
	int ret = recv(fd, buf, 1024, 0);
	if (ret <= 0) {
		printf("%s %d: connect[%d] down[%d][%d]\n", __FUNCTION__, __LINE__, fd, ret, errno);
		sleep(99999);
	}
	buf[ret] = '\0';
//	printf("%s %d: ret[%d] fd[%d] %s\n", __FUNCTION__, __LINE__, ret, fd, buf);
	return (0);
}

int on_timer()
{
	printf("%s\n", __FUNCTION__);
	int i;
	char buf[64];
	for (i = 0; i < test_num; i++) {
		int fd = all_events[i].data.fd;
		int len = sprintf(buf, "[on_timer send fd = %d]", fd);
		int ret = send(fd, buf, len, 0);
		if (ret != len) {
			printf("%s %d: connect[%d] down[%d][%d]\n", __FUNCTION__, __LINE__, fd, ret, errno);
			sleep(99999);
		}
	}
	return (0);
}

int main(int argc, char *argv[])
{
    int epoll_fd = epoll_create(1);
	signal(SIGPIPE, SIG_IGN);
	int i;
	int port = 6379;
	const char *ip = "127.0.0.1";
	if (argc > 1)
		ip = argv[1];
	if (argc > 2)
		port = atoi(argv[2]);
	if (argc > 3)
		test_num = atoi(argv[3]);

	for (i = 0; i < test_num; i++) {
		int fd = connect_tcp(ip, port);
		if (fd <= 0) {
			printf("conntect failed[%d]\n", i);
			sleep(99999);
		}
		all_events[i].events = EPOLLIN;
		all_events[i].data.fd = fd;
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &all_events[i]);
		++connected_num;
	}

	for (;;) {
		int ret = epoll_wait(epoll_fd, all_events, test_num, get_next_timeout());
		if (ret == 0) {
			on_timer();
			refresh_next_timer(0);			
		}
		else {
			for (i = 0; i < ret; i++) {
				on_recv(all_events[i].data.fd);				
			}

			refresh_next_timer(1);
		}
	}
    return 0;
}

static int connect_tcp(const char *addr, int port)
{
    int s, rv;
    char _port[6];  /* strlen("65535"); */
    struct addrinfo hints, *servinfo, *p;
	int ret = -1;

    snprintf(_port, 6, "%d", port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    /* Try with IPv6 if no IPv4 address was found. We do it in this order since
     * in a Redis client you can't afford to test if you have IPv6 connectivity
     * as this would add latency to every connect. Otherwise a more sensible
     * route could be: Use IPv6 if both addresses are available and there is IPv6
     * connectivity. */
    if ((rv = getaddrinfo(addr,_port,&hints,&servinfo)) != 0) {
         hints.ai_family = AF_INET6;
         if ((rv = getaddrinfo(addr,_port,&hints,&servinfo)) != 0) {
			 ret = -10;
			 goto done;
		 }
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (connect(s,p->ai_addr,p->ai_addrlen) == -1) {
			printf("connect failed, err = %d %s\n", errno, strerror(errno));
			close(s);
			continue;
        }
		ret = s;
		goto done;
    }
    if (p == NULL) {
		ret = -20;
		goto done;
    }

done:
    freeaddrinfo(servinfo);
    return ret;  // Need to return REDIS_OK if alright
}

#define SLEEP_SEC (1)
static time_t t1;
static int next_time;	
static int get_next_timeout()
{
	return (next_time);
}

static void refresh_next_timer(int r)
{
	time_t now;
	time(&now);
	if (now >= t1) {
		if (r)
			on_timer();
		t1 = now + SLEEP_SEC;
		next_time = SLEEP_SEC * 1000;
		return;
	}

	next_time = (t1 - now) * 1000;
	return;
}
