#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

extern "C" {
    #include <unistd.h>
}

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

// returns a mp relating variable names to MSE w/ measurements
static std::unordered_map<unsigned, std::array<double, 11>>& get_variables_to_values() {
    static std::unordered_map<unsigned, std::array<double, 11>> variables_to_values;
    static bool is_first = true;
    if (is_first) {
        variables_to_values.reserve(50000);
        is_first = false;
    }
    return variables_to_values;
}

extern "C" void log_usage(unsigned varid, void *data, unsigned long long size) {
    std::unordered_map<unsigned, unsigned long> &variables_to_updates = get_variables_to_updates();
    std::array<double, 11> &varinfo_measurements = get_varinfo_measurements();
    std::unordered_map<unsigned, std::array<double, 11>> &variables_to_values = get_variables_to_values();
    std::mutex &lock = get_lock();

    double val = 0;
    if (size == sizeof(double)) {
        memcpy(&val, data, sizeof(double));
    } else if (size == sizeof(float)) {
        float f;
        memcpy(&f, data, sizeof(float));
        val = f;
    } else {
        return;
    }

    lock.lock();
    variables_to_updates[varid]++;
    for (int i = 0; i < 11; i++) {
        // computing online avg. MSE
        double a = 1.0 / variables_to_updates[varid];
        double b = (1.0 - a);
        variables_to_values[varid][i] = a * mse(val, varinfo_measurements[i]) +
                                        b * variables_to_values[varid][i];
    }
    lock.unlock();
}

// compute MSE
static double mse(double a, double b) {
    return pow(a - b, 2);
}

static void print_log() {
    using namespace std;

    while (true) {
        ofstream out;
        out.open("/home/rewriter/mse_log.csv", ofstream::out);

        //unordered_map<string, map<string, double>> &variables_to_values = get_variables_to_values();
        unordered_map<unsigned, std::array<double, 11>> &variables_to_values = get_variables_to_values();
        mutex &lock = get_lock();

        out << "variable name, measurement type, MSE" << endl;
        lock.lock();
        for (const auto &pair: variables_to_values) {
            for (int i = 0; i < 11; i++) {
                out << pair.first << "," << i << "," << pair.second[i] << endl;
            }
        }
        lock.unlock();
        out.close();
        sleep(60);
    }
}

//std::thread th(print_log);
