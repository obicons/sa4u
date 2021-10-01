#include <algorithm>
#include "socket.hpp"

extern "C" {
    #include <sys/select.h>
}

using namespace std;

#define NANO_TO_MICRO(x) ((x) / 1000)

/**
    A thin wrapper around the system's select. Relevant part from the man page:
    select() examines the I/O descriptor sets whose addresses are passed in readfds, writefds, and
    errorfds to see if some of their descriptors are ready for reading, are ready for writing, or have an
    exceptional condition pending, respectively.
    On return, select() replaces the given descriptor sets with subsets consisting of those descriptors
    that are ready for the requested operation.  select() returns the total number of ready descriptors
    in all the sets.
 */
optional<int> select(std::set<CSocket> &read_sockets, std::set<CSocket> &write_sockets, std::set<CSocket> &err_sockets, std::timespec &ts) {
    fd_set rset, wset, eset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&eset);

    int nfds = 0;
    for (auto &s: read_sockets) {
        FD_SET(s, &rset);
        nfds = max<int>(s, nfds);
    }
    
    for (auto &s: write_sockets) {
        FD_SET(s, &wset);
        nfds = max<int>(s, nfds);
    }
    
    for (auto &s: err_sockets) {
        FD_SET(s, &eset);
        nfds = max<int>(s, nfds);
    }

    nfds += 1;

    struct timeval tv = {
        .tv_sec = ts.tv_sec,
        .tv_usec = NANO_TO_MICRO(ts.tv_nsec),
    };

    int num_ready;
    if ((num_ready = select(nfds, &rset, &wset, &eset, &tv)) == -1)
        return {};

    set<CSocket> nread_sockets, nwrite_sockets, nerr_sockets;
    swap(read_sockets, nread_sockets);
    swap(write_sockets, nwrite_sockets);
    swap(err_sockets, nerr_sockets);
    for (auto &sock: nread_sockets)
        if (!FD_ISSET(sock, &rset))
            read_sockets.insert(sock);
    for (auto &sock: nwrite_sockets)
        if (!FD_ISSET(sock, &wset))
            write_sockets.insert(sock);
    for (auto &sock: nerr_sockets)
        if (!FD_ISSET(sock, &eset))
            err_sockets.insert(sock);

    return num_ready;
}