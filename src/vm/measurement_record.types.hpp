#pragma once

#include <complex>
#include <string>
#include <vector>

struct MeasurementRecord {
    std::vector<int> targets;
    std::vector<int> bits;
};

struct ExecutionLog {
    int shot = 0;
    double logical_time = 0.0;
    std::string category;
    std::string message;
};
