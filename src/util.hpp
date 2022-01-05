#pragma once

#include <string>
#include <iostream>
#include <map>

extern "C" {
#include <clang-c/Index.h>
}

using namespace std;

// returns a string representing the type spelling
string get_type_spelling(CXType);

// returns the cursor's spelling as a string
string get_cursor_spelling(CXCursor);

// returns the unified symbol reference for the cursor
string get_USR(CXCursor);

// changes the current working directory only for the calling thread
int change_thread_working_dir(const char *);

// Inverts the map by mapping each value to its key.
map<int, string> invert_map(map<string, int> &m);