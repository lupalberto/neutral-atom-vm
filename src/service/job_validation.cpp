#include "service/job_validation.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace service {
namespace {

constexpr double kBlockadeTolerance = 1e-12;

bool try_coordinate_distance(
    const HardwareConfig& hardware,
    std::size_t idx0,
    std::size_t idx1,
    double* out_distance
) {
    if (!out_distance) {
        return false;
    }
    if (idx0 >= hardware.coordinates.size() || idx1 >= hardware.coordinates.size()) {
        return false;
    }
    const auto& lhs = hardware.coordinates[idx0];
    const auto& rhs = hardware.coordinates[idx1];
    const std::size_t dim = std::min(lhs.size(), rhs.size());
    if (dim == 0) {
        return false;
    }
    double sum = 0.0;
    for (std::size_t offset = 0; offset < dim; ++offset) {
        const double diff = lhs[offset] - rhs[offset];
        sum += diff * diff;
    }
    *out_distance = std::sqrt(sum);
    return true;
}

double distance_between(const HardwareConfig& hardware, std::size_t idx0, std::size_t idx1) {
    double coordinate_distance = 0.0;
    if (try_coordinate_distance(hardware, idx0, idx1, &coordinate_distance)) {
        return coordinate_distance;
    }
    if (idx0 < hardware.positions.size() && idx1 < hardware.positions.size()) {
        return std::abs(hardware.positions[idx0] - hardware.positions[idx1]);
    }
    return std::numeric_limits<double>::infinity();
}

std::string format_blockade_radius(double radius) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << radius;
    return oss.str();
}

}  // namespace

void validate_blockade_constraints(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
) {
    if (hardware.positions.empty() || hardware.blockade_radius <= 0.0) {
        return;
    }
    const std::size_t limit = hardware.positions.size();
    const double threshold = hardware.blockade_radius + kBlockadeTolerance;
    for (const auto& instr : program) {
        if (instr.op != Op::ApplyGate) {
            continue;
        }
        const auto& gate = std::get<Gate>(instr.payload);
        if (gate.targets.size() < 2) {
            continue;
        }
        for (int target : gate.targets) {
            if (target < 0 || static_cast<std::size_t>(target) >= limit) {
                throw std::invalid_argument(
                    "Gate " + gate.name + " references qubit " + std::to_string(target) +
                    " but configuration only allocates qubits 0.." + std::to_string(limit - 1)
                );
            }
        }
        for (std::size_t i = 0; i < gate.targets.size(); ++i) {
            for (std::size_t j = i + 1; j < gate.targets.size(); ++j) {
                const int q0 = gate.targets[i];
                const int q1 = gate.targets[j];
                const std::size_t idx0 = static_cast<std::size_t>(q0);
                const std::size_t idx1 = static_cast<std::size_t>(q1);
                const double distance = distance_between(hardware, idx0, idx1);
                if (distance > threshold) {
                    throw std::invalid_argument(
                        "Gate " + gate.name + " on qubits " + std::to_string(q0) + "/" +
                        std::to_string(q1) + " violates blockade radius " +
                        format_blockade_radius(hardware.blockade_radius)
                    );
                }
            }
        }
    }
}

}  // namespace service
