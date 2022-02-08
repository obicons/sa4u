#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
    #include <unistd.h>
}

#define INTEGRAL_TYPE 0 
#define FLOATING_TYPE 1

// compute MSE
static double mse(double a, double b);

// returns a lock
std::mutex& get_lock() {
    static std::mutex lock;
    return lock;
}

// returns an array of physical measurements
// SIM_Aircraft.cpp fills this in directly
std::array<double, 11>& get_varinfo_measurements() {
    static std::array<double, 11> varinfo_measurements;
    return varinfo_measurements;
}

// returns a map relating variable IDs to the number of updates
static std::unordered_map<unsigned, unsigned long>& get_variables_to_updates() {
    static std::unordered_map<unsigned, unsigned long> variables_to_updates;
    static bool is_first = true;
    if (is_first) {
        variables_to_updates.reserve(50000);
        is_first = false;
    }
    return variables_to_updates;
}

// returns a map relating variable names to MSE w/ measurements
static std::unordered_map<unsigned, std::array<double, 11>>& get_variables_to_values() {
    static std::unordered_map<unsigned, std::array<double, 11>> variables_to_values;
    static bool is_first = true;
    if (is_first) {
        variables_to_values.reserve(50000);
        is_first = false;
    }
    return variables_to_values;
}

// Returns a map relating variable IDs to a sequence of (timestamp, value) pair.
static std::unordered_map<unsigned, std::vector<std::tuple<time_t, double>>>& get_variable_history() {
    static std::unordered_map<unsigned, std::vector<std::tuple<time_t, double>>> variable_readings;
    static bool first_use = true;
    if (first_use) {
        variable_readings.reserve(50000);
        first_use = false;
    }
    return variable_readings;
}

extern "C" void log_usage(int vartype, unsigned varid, void *data, unsigned long long size) {
    double r = static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
    if (r > 0.1) {
        // Only log 10% of the time.
        return;
    }

    std::unordered_map<unsigned, std::vector<std::tuple<time_t, double>>> &variable_readings = get_variable_history();
    std::mutex &lock = get_lock();
    if (!data) return;

    double val = 0.0;

    if (vartype == INTEGRAL_TYPE) {
        switch (size) {
            case sizeof(char):
                char ch;
                memcpy(&ch, data, 1);
                val = ch;
                break;
            case sizeof(short):
                short s;
                mempcpy(&s, data, sizeof(short));
                val = s;
                break;
            case sizeof(int):
                int i;
                memcpy(&i, data, sizeof(int));
                val = i;
                break;
            case sizeof(long):
                long l;
                memcpy(&l, data, sizeof(long));
                val = l;
                break;
            default:
                break;
        }
    } else if (vartype == FLOATING_TYPE) {
        switch (size) {
            case sizeof(float):
                float f;
                memcpy(&f, data, sizeof(float));
                val = f;
                break;
            case sizeof(double):
                memcpy(&val, data, sizeof(double));
                break;
            default:
                break;
        }
    }
    
    lock.lock();
    variable_readings[varid].push_back({time(nullptr), val});
    lock.unlock();
}

// compute MSE
static double mse(double a, double b) {
    return pow(a - b, 2);
}

static void print_log() {
    using namespace std;

    while (true) {
        ofstream out("/home/rewriter/log.csv", ofstream::out);
        out << "variable_id,timestamp,value" << endl;

        const unordered_map<unsigned, std::vector<std::tuple<time_t, double>>>& vars = get_variable_history();
        mutex &lock = get_lock();

        lock.lock();
        for (const auto &pair: vars) {
            for (const auto &reading: pair.second) {
                out << pair.first << "," << get<0>(reading) << "," << get<1>(reading) << endl;
            }
        }
        lock.unlock();
        sleep(300);
    }
}

std::thread th(print_log);
