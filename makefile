CC = gcc
CFLAGS = -Wall -g
TARGET = myftpclient.c myftpserver.c
all: $(TARGET)
	$(CC) $(CFLAGS) -o myftpclient myftpclient.c
	$(CC) $(CFLAGS) -o myftpserver myftpserver.c

clean:
	$(RM) myftpclient myftpserver

