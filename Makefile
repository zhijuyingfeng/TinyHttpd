CC=gcc
CFLAGS=-Wall -pthread -g

clean:
	rm httpd client

httpd: httpd.c
	$(CC)  httpd.c -o httpd $(CFLAGS)

client: client.c
	$(CC) $(CFLAGS) client.c -o client
