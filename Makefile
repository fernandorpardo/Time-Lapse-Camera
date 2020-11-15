CFLAGS = -Wall -g -fmax-errors=2
CC= g++ -std=c++0x
LIBJPEG_LIB = -l:libjpeg.so.62
OLIBS= tlcam.o glib.o version.o HTTPpost.o

all: tlcam 
glib.o: glib.cpp glib.h 
	$(CC) $(CFLAGS) -c glib.cpp -o glib.o
HTTPpost.o: HTTPpost.cpp HTTPpost.h
	$(CC) $(CFLAGS) -c HTTPpost.cpp -o HTTPpost.o
tlcam.o: tlcam.cpp tlcam.h
	$(CC) $(CFLAGS) -c tlcam.cpp -o tlcam.o
version: 
	$(CC) $(CFLAGS) -c version.cpp -o version.o		
tlcam: tlcam.cpp tlcam.h tlcam.o glib.o glib.h HTTPpost.o version
	$(CC) -o tlcam  $(OLIBS) $(LIBJPEG_LIB) 
	mv tlcam ~/bin	
clean:
	rm -f *.o 
	@rm -f ~/bin/tlcam
