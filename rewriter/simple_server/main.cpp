#include <cassert>
#include <cstring>
#include <ctime>
#include <iostream>
#include <optional>
#include <set>

extern "C" {
    #include <unistd.h>
    #include <signal.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <sys/un.h>
}

#include "socket.hpp"

using namespace std;

// stores if this program has been interrupted
static bool interrupted;

// returns the home directory path
static string gethome();

// returns a file descriptor for the server at listen path
static optional<CSocket> setup_server(const string &listen_path);

// returns if the file exists
static bool file_exists(const string &file);

// cleaner unlink
static int unlink(const string &path);

int main(int argc, char **argv) {
    string listen_path;
    if (argc != 2) {
        listen_path = gethome() + "/.simple_server.sock";
    } else {
        listen_path = argv[1];
    }

    optional<CSocket> socket = setup_server(listen_path);
    if (!socket.has_value()) {
        cerr << "Unable to create socket: " << strerror(errno) << endl;
        return 1;
    }

    int num_cons = 0;

    signal(SIGINT, [](int) {interrupted = true;});
    while (!interrupted) {
         set<CSocket> read_sockets = {*socket};
         set<CSocket> write_sockets = {}, err_sockets = {};
         timespec timeout = {.tv_sec = 0, .tv_nsec = 500000000UL};
         optional<int> maybe_num_ready = select(read_sockets, write_sockets, err_sockets, timeout);
         if (!maybe_num_ready || maybe_num_ready.value() == 0) {
             continue;
        }
        num_cons++;
    }

    cout << "Handled " << num_cons << " writes" << endl;
}

// returns the home directory path
static string gethome() {
    return getenv("HOME");
}

// returns a file descriptor for the server at listen path
static optional<CSocket> setup_server(const string &listen_path) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd == -1)
        return {};

    if (file_exists(listen_path))
        unlink(listen_path);

    CSocket socket(fd);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, listen_path.c_str(), sizeof(addr.sun_path));
    if (bind(socket, (const sockaddr *) &addr, sizeof(addr)) == -1)
        return {};

    return socket;
}

// returns if the file exists
static bool file_exists(const string &file) {
    return !access(file.c_str(), F_OK);
}

static int unlink(const string &path) {
    return unlink(path.c_str());
}