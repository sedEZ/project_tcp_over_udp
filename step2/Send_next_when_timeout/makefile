CC = g++
FILE_TYPE = cpp
PROG1 = server
PROG2 = client

all:
	$(CC) -Wall -o $(PROG1) $(PROG1).$(FILE_TYPE) -lpthread
	$(CC) -Wall -o $(PROG2) $(PROG2).$(FILE_TYPE)
clean:
	rm -f $(PROG1) $(PROG2) *_result.mp4
