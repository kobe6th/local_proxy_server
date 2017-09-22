#LDFLAGS = -lhiredis -lpthread -lm -lstreamhtmlparser
LDFLAGS = -lpthread
LIB = ../lib/
OBJECTS = server.o tcp.o cb_method.o rbtree.o utils.o

all: proxy_server 

proxy_server : ${OBJECTS}
	cc -o proxy_server -g ${OBJECTS} ${LDFLAGS}


server.o:server.c
	cc -c -g server.c

tcp.o:tcp.c
	cc -c -g tcp.c

cb_method.o:cb_method.c
	cc -c -g cb_method.c

rbtree.o:rbtree.c
	cc -c -g rbtree.c

utils.o:utils.c
	cc -c -g utils.c

.PHONY:clean

clean:
	rm -f *.o proxy_server 
