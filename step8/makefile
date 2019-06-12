CC = g++
FILE_TYPE = cpp
PROG1 = server
PROG1_1 = connect_thread
PROG2 = client

all: $(PROG1).o $(PROG1_1).o $(PROG2).o
	$(CC) -Wall -o $(PROG1) $(PROG1).o $(PROG1_1).o -lpthread
	$(CC) -Wall -o $(PROG2) $(PROG2).o

$(PROG1).o : $(PROG1).$(FILE_TYPE)
	$(CC) -c $(PROG1).$(FILE_TYPE)

$(PROG1_1).o : $(PROG1_1).$(FILE_TYPE)
	$(CC) -c $(PROG1_1).$(FILE_TYPE)

$(PROG2).o : $(PROG2).$(FILE_TYPE)
	$(CC) -c $(PROG2).$(FILE_TYPE)

clean:
	rm -f $(PROG1) $(PROG2) *.o *_result.mp4
