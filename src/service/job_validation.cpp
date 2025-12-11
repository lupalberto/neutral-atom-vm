#include "service/job_validation.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace service {
namespace {

std::size_t configuration_limit(const HardwareConfig& hardware) {
    if (!hardware.site_ids.empty()) {
        return hardware.site_ids.size();
    }
    if (!hardware.positions.empty()) {
        return hardware.positions.size();
    }
    if (!hardware.coordinates.empty()) {
        return hardware.coordinates.size();
    }
    if (!hardware.sites.empty()) {
        return hardware.sites.size();
    }
    return 0;
}

std::string format_interaction_message(int site_a, int site_b) {
    std::ostringstream oss;
    if (site_a >= 0 && site_b >= 0) {
        oss << site_a << "/" << site_b;
    } else {
        oss << "unknown sites";
    }
    return oss.str();
}

}  // namespace

void validate_blockade_constraints(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
) {
    const SiteIndexMap index = build_site_index(hardware);
    const std::size_t limit = configuration_limit(hardware);
    if (limit == 0) {
        return;
    }
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
        const InteractionGraph* graph = find_interaction_graph(hardware, gate.name);
        for (std::size_t i = 0; i < gate.targets.size(); ++i) {
            for (std::size_t j = i + 1; j < gate.targets.size(); ++j) {
                const int q0 = gate.targets[i];
                const int q1 = gate.targets[j];
                if (graph) {
                    const int site0 = site_id_for_slot(hardware, index, q0);
                    const int site1 = site_id_for_slot(hardware, index, q1);
                    if (site0 < 0 || site1 < 0 ||
                        !interaction_pair_allowed(*graph, site0, site1)) {
                        throw std::invalid_argument(
                            "Gate " + gate.name + " on " + format_interaction_message(site0, site1) +
                            " violates interaction graph constraints");
                    }
                }
                if (auto reason = blockade_violation_reason(hardware, index, q0, q1)) {
                    throw std::invalid_argument(
                        "Gate " + gate.name + " on qubits " + std::to_string(q0) + "/" +
                        std::to_string(q1) + " violates " + *reason);
                }
            }
        }
    }
}

}  // namespace service
