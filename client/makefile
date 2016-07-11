all: example-libevent client

example-libevent: example-libevent.c
	cc -o $@ -g -O0 -fPIC  -Wall -W -Wstrict-prototypes -Wwrite-strings  -I. $^ -levent -lhiredis

client: client.c
	cc -o $@ -g -O0 -fPIC  -Wall   -I. $^ 
