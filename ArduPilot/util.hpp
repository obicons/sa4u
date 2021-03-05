#pragma once

#include <string>

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
