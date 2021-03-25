target=sa4u
objects=main.o deduce.o mav.o util.o cfg.o
machine=$(shell uname -s)

ifeq "$(machine)" "Linux"
	CXXFLAGS=-I/usr/lib/llvm-13/include -Ipugixml/src -Irapidjson/include/ -Icxxopts/include/  -Wall -Wextra -Wpedantic -std=c++2a -g -lpthread
	LIBRARY_PATH=-L/usr/lib/llvm-13/lib/
	PUGIXML_PATH=pugixml/build/make-g++-debug-standard-c++11/src/pugixml.cpp.o
endif

ifeq "$(machine)" "Darwin"
	CXXFLAGS=-I/usr/local/Cellar/llvm/11.1.0/include/ -Ipugixml/src -Irapidjson/include/ -Icxxopts/include/  -Wall -Wextra -Wpedantic -std=c++2a -g
	LIBRARY_PATH=-L/usr/local/Cellar/llvm/11.1.0/lib
	PUGIXML_PATH=pugixml/build/make-c++-debug-standard-c++11/src/pugixml.cpp.o
endif

LDLIBS=-lclang

$(target): $(objects) pugixml.cpp.o
	$(CXX) -o $(target) $(objects) pugixml.cpp.o $(LIBRARY_PATH) $(LDLIBS) $(CXXFLAGS)

pugixml.cpp.o: pugixml/src/pugixml.cpp pugixml/src/pugixml.hpp
	$(MAKE) -C pugixml
	cp $(PUGIXML_PATH) pugixml.cpp.o

.PHONY: clean
clean:
	rm -f $(objects) $(target)
