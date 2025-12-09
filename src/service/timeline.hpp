#pragma once

#include <string>

namespace service {

struct TimelineEntry {
    double start_time = 0.0;
    double duration = 0.0;
    std::string op;
    std::string detail;
};

}  // namespace service
