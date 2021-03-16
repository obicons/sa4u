target=apmsa
objects=main.o deduce.o mav.o util.o cfg.o
CXXFLAGS=-I/usr/lib/llvm-13/include -Ipugixml/src -Irapidjson/include/  -Wall -Wextra -Wpedantic -std=c++20 -g
#CXXFLAGS=-I/usr/local/Cellar/llvm/11.1.0/include/ -Ipugixml/src -Irapidjson/include/  -Wall -Wextra -Wpedantic -std=c++2a -g
LDLIBS=-lclang
LIBRARY_PATH=-L/usr/lib/llvm-13/lib/
#LIBRARY_PATH=-L/usr/local/Cellar/llvm/11.1.0/lib

$(target): $(objects) pugixml.cpp.o
	$(CXX) -o $(target) $(objects) pugixml.cpp.o $(LIBRARY_PATH) $(LDLIBS) $(CXXFLAGS)

pugixml.cpp.o: pugixml/src/pugixml.cpp pugixml/src/pugixml.hpp
	$(MAKE) -C pugixml
	cp pugixml/build/make-g++-debug-standard-c++11/src/pugixml.cpp.o pugixml.cpp.o
	# cp pugixml/build/make-c++-debug-standard-c++11/src/pugixml.cpp.o pugixml.cpp.o
.PHONY: clean
clean:
	rm -f $(objects) $(target)
