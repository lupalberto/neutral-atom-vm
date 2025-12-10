#pragma once

#include <string>

namespace service {

struct TimelineEntry {
    double start_time = 0.0;
    double duration = 0.0;
    std::string op;
    std::string detail;
};

inline bool operator==(const TimelineEntry& lhs, const TimelineEntry& rhs) {
    return lhs.start_time == rhs.start_time &&
           lhs.duration == rhs.duration &&
           lhs.op == rhs.op &&
           lhs.detail == rhs.detail;
}

}  // namespace service
