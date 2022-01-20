#include "util.hpp"

extern "C" {
#include "clang-c/CXString.h"
#include "clang-c/Index.h"
}

#if defined(__linux__)

extern "C" {
#include <linux/limits.h>
#include <linux/sched.h>
#include <unistd.h>
}

#elif defined(__APPLE__)

extern "C" {
#include <pthread.h>
extern int pthread_chdir_np(const char *);
}

#else
#error "Only Linux and MacOS are currently supported"
#endif

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

// changes the current working directory only for the calling thread
int change_thread_working_dir(const char *dirname) {
        int result;
        #if defined(__linux__)
        static thread_local bool did_unshare;
        if (!did_unshare) {
                did_unshare = true;
                unshare(CLONE_FS);
        }
        result = chdir(dirname);
        #elif defined(__APPLE__)
        result = pthread_chdir_np(dirname);
        #endif
        return result;
}

// Inverts the map by mapping each value to its key.
map<int, string> invert_map(map<string, int> &m) {
        map<int, string> result;
        for (const auto &p : m) {
                result[p.second] = p.first;
        }
        return result;
}

// Returns the GCD of the parameters.
int gcd(int a, int b) {
        if (a == 0 || b == 0) {
                return max(a, b);
        }

        int greatest = max(a, b);
        int smallest = min(a, b);
        int remainder = greatest % smallest;
        while (remainder) {
                greatest = smallest;
                smallest = remainder;
                remainder = greatest % smallest;                
        }
        return smallest;
}
