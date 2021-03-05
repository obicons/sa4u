#include "util.hpp"
#include "clang-c/CXString.h"
#include "clang-c/Index.h"

using namespace std;

// returns a string representing the type spelling
string get_type_spelling(CXType t) {
        CXString spelling = clang_getTypeSpelling(t);
        string result(clang_getCString(spelling));
        return result;
}

// returns the cursor's spelling as a string
string get_cursor_spelling(CXCursor c) {
        CXString spelling = clang_getCursorSpelling(c);
        string result(clang_getCString(spelling));
        clang_disposeString(spelling);
        return result;
}

// returns the unified symbol reference for the cursor
string get_USR(CXCursor c) {
        CXString usr = clang_getCursorUSR(c);
        string result(clang_getCString(usr));
        clang_disposeString(usr);
        return result;
}
