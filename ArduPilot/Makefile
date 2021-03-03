target=apmsa
objects=main.o deduce.o
CXXFLAGS=-I/usr/lib/llvm-13/include -Ipugixml/src -Irapidjson/include/  -Wall -Wextra -Wpedantic -std=c++17 -g
LDLIBS=-lclang
LIBRARY_PATH=-L/usr/lib/llvm-13/lib/

$(target): $(objects) pugixml.cpp.o
	$(CXX) -o $(target) $(objects) pugixml.cpp.o $(LIBRARY_PATH) $(LDLIBS) $(CXXFLAGS)

pugixml.cpp.o: pugixml/src/pugixml.cpp pugixml/src/pugixml.hpp
	$(MAKE) -C pugixml
	cp pugixml/build/make-g++-debug-standard-c++11/src/pugixml.cpp.o pugixml.cpp.o

.PHONY: clean
clean:
	rm -f $(objects) $(target)
