#pragma once

#include <cassert>
#include <ctime>
#include <iostream>
#include <memory>
#include <optional>
#include <set>

extern "C" {
    #include <unistd.h>
}

// RAII c socket
class CSocket {
public:
    CSocket(int fd) { 
        assert(fd != -1);
        counter = std::make_shared<int>(1);
        this->fd = fd;
    }

    CSocket(const CSocket &other) {
        this->counter = other.counter;
        this->fd = other.fd;
        (*this->counter)++;
    }

    CSocket(CSocket &&other) {
        this->counter = other.counter;
        this->fd = other.fd;
        (*this->counter)++;
    }

    ~CSocket() {
        (*counter)--;
        if ((*counter) == 0) {
            close(fd);
        }
    }

    CSocket& operator=(const CSocket &other) {
        (*counter)--;
        if (*counter == 0) {
            close(fd);
        }
        counter = other.counter;
        fd = other.fd;
        (*counter)++;
        return *this;
    }

    int getfd() {
        return fd;
    }

    operator int() const {
        return this->fd;
    }

    void dump() {
        std::cout << "FD " << fd << " has " << "references count: " << (*counter) << std::endl;
    }

private:
    std::shared_ptr<int> counter;
    int fd;
};

// returns the first socket from sockets with a ready event
std::optional<int> select(std::set<CSocket> &read_sockets, std::set<CSocket> &write_sockets, std::set<CSocket> &err_sockets, std::timespec &ts);