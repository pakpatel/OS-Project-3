CC	= gcc
LDFLAGS	= -lm -lnsl
CFLAGS	= -g
TARGET	= media_transfer parser server client

default: $(TARGET)

server: server.o
	gcc $(CFLAGS) -o $@ $? media_transfer.o parser.o $(LDFLAGS) -lpthread

client: client.o
	gcc $(CFLAGS) -o $@ $? media_transfer.o parser.o $(LDFLAGS)

media_transfer: media_transfer.o
	gcc $(CFLAGS) -c media_transfer.c

parser: parser.o
	gcc $(CFLAGS) -c parser.c

clean:
	-rm -f *.o *~

cleanall: clean
	-rm -f $(TARGET)