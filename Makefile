all: serverthread serverfork

serverthread.o: serverthread.cpp
	$(CXX) -Wall -c serverthread.cpp -I.

serverfork.o: serverfork.cpp
	$(CXX) -Wall -c serverfork.cpp -I.

serverfork: serverfork.o 
	$(CXX) -L./ -Wall -o serverfork serverfork.o

serverthread: serverthread.o 
	$(CXX) -L./ -Wall -o serverthread serverthread.o -lpthread

clean:
	rm -f *.o *.a perf_*.txt tmp.* serverfork serverthread
