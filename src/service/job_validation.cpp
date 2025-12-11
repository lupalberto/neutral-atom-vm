#include "service/job.hpp"
#include "service/job_validation.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>

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

std::string format_coordinate_triplet(double x, double y, double z) {
    std::ostringstream oss;
    oss << "(" << x << "," << y << "," << z << ")";
    return oss.str();
}

std::string describe_slot_coordinates(const HardwareConfig& hardware, int slot) {
    if (slot < 0) {
        return {};
    }
    const std::size_t index = static_cast<std::size_t>(slot);
    if (index < hardware.coordinates.size()) {
        const auto& coords = hardware.coordinates[index];
        const double x = coords.size() > 0 ? coords[0] : 0.0;
        const double y = coords.size() > 1 ? coords[1] : 0.0;
        const double z = coords.size() > 2 ? coords[2] : 0.0;
        return format_coordinate_triplet(x, y, z);
    }
    if (index < hardware.positions.size()) {
        return format_coordinate_triplet(hardware.positions[index], 0.0, 0.0);
    }
    return {};
}

std::string describe_slot_location(
    const HardwareConfig& hardware,
    const SiteIndexMap& index_map,
    int slot
) {
    std::ostringstream oss;
    oss << "slot " << slot;
    const SiteDescriptor* descriptor = site_descriptor_for_slot(hardware, index_map, slot);
    if (descriptor) {
        oss << " (site " << descriptor->id
            << " coords=" << format_coordinate_triplet(descriptor->x, descriptor->y, descriptor->z)
            << " zone=" << descriptor->zone_id << ")";
        return oss.str();
    }
    const std::string coords = describe_slot_coordinates(hardware, slot);
    if (!coords.empty()) {
        oss << " coords=" << coords;
    }
    const int site_id = site_id_for_slot(hardware, index_map, slot);
    if (site_id >= 0) {
        oss << " site=" << site_id;
    }
    return oss.str();
}

std::string describe_slot_pair(
    const HardwareConfig& hardware,
    const SiteIndexMap& index_map,
    int slot_a,
    int slot_b
) {
    return describe_slot_location(hardware, index_map, slot_a) +
        " and " +
        describe_slot_location(hardware, index_map, slot_b);
}

bool move_limits_has_data(const MoveLimits& limits) {
    return limits.max_total_displacement_per_atom > 0.0 ||
           limits.max_moves_per_atom > 0 ||
           limits.max_moves_per_shot > 0 ||
           limits.max_moves_per_configuration_change > 0 ||
           limits.rearrangement_window_ns > 0.0;
}

struct TransportGraph {
    void add_edge(int src, int dst) {
        adjacency_[src].insert(dst);
        adjacency_[dst].insert(src);
    }

    bool allows(int src, int dst) const {
        const auto it = adjacency_.find(src);
        if (it == adjacency_.end()) {
            return false;
        }
        return it->second.find(dst) != it->second.end();
    }

    bool empty() const {
        return adjacency_.empty();
    }

private:
    std::unordered_map<int, std::unordered_set<int>> adjacency_;
};

std::optional<int> find_site_id_for_position(const HardwareConfig& hardware, double position) {
    constexpr double kPositionTolerance = 1e-6;
    for (std::size_t idx = 0; idx < hardware.positions.size(); ++idx) {
        if (std::fabs(hardware.positions[idx] - position) < kPositionTolerance) {
            if (idx < hardware.site_ids.size()) {
                return hardware.site_ids[idx];
            }
            return static_cast<int>(idx);
        }
    }
    for (const auto& site : hardware.sites) {
        if (std::fabs(site.x - position) < kPositionTolerance) {
            return site.id;
        }
    }
    return std::nullopt;
}

struct MoveStats {
    int moves = 0;
    double displacement = 0.0;
};

class ActiveQubitsValidator final : public Validator {
public:
    void validate(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const override {
        const std::size_t limit = hardware.site_ids.size();
        if (limit == 0) {
            throw std::runtime_error("Configuration must specify at least one occupied site.");
        }
        for (const auto& instr : program) {
            if (instr.op != Op::ApplyGate) {
                continue;
            }
            const auto& gate = std::get<Gate>(instr.payload);
            for (int target : gate.targets) {
                if (target < 0 || static_cast<std::size_t>(target) >= limit) {
                    throw std::runtime_error(
                        "Gate " + gate.name + " references qubit " + std::to_string(target) +
                        " but configuration only allocates qubits 0.." + std::to_string(limit - 1)
                    );
                }
            }
        }
    }

    std::string name() const override {
        return "active_qubits";
    }
};

class BlockadeValidator final : public Validator {
public:
    void validate(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const override {
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
                    const std::string slot_pair_description = describe_slot_pair(hardware, index, q0, q1);
                    if (graph) {
                        const int site0 = site_id_for_slot(hardware, index, q0);
                        const int site1 = site_id_for_slot(hardware, index, q1);
                        if (site0 < 0 || site1 < 0 ||
                            !interaction_pair_allowed(*graph, site0, site1)) {
                            throw std::invalid_argument(
                                "Gate " + gate.name + " between " + slot_pair_description +
                                " violates interaction graph constraints"
                            );
                        }
                    }
                    if (auto reason = blockade_violation_reason(hardware, index, q0, q1)) {
                        throw std::invalid_argument(
                            "Gate " + gate.name + " between " + slot_pair_description +
                            " violates " + *reason
                        );
                    }
                }
            }
        }
    }

    std::string name() const override {
        return "blockade";
    }
};

class TransportValidator final : public Validator {
public:
    void validate(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const override {
        if (hardware.transport_edges.empty() && !move_limits_has_data(hardware.move_limits)) {
            return;
        }
        const SiteIndexMap index = build_site_index(hardware);
        const std::size_t slot_count = configuration_limit(hardware);
        if (slot_count == 0) {
            return;
        }
        std::vector<int> slot_site_ids(slot_count, -1);
        std::vector<double> slot_positions(slot_count, 0.0);
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            if (slot < hardware.site_ids.size()) {
                slot_site_ids[slot] = hardware.site_ids[slot];
            } else {
                slot_site_ids[slot] = static_cast<int>(slot);
            }
            if (slot < hardware.positions.size()) {
                slot_positions[slot] = hardware.positions[slot];
            } else if (const SiteDescriptor* descriptor =
                           site_descriptor_for_slot(hardware, index, static_cast<int>(slot))) {
                slot_positions[slot] = descriptor->x;
            }
        }

        TransportGraph graph;
        for (const auto& edge : hardware.transport_edges) {
            graph.add_edge(edge.src_site_id, edge.dst_site_id);
        }

        const MoveLimits& limits = hardware.move_limits;
        bool seen_main_program = false;
        std::vector<MoveStats> stats(slot_count);
        int total_moves = 0;

        for (const auto& instr : program) {
            if (instr.op == Op::MoveAtom) {
                if (limits.rearrangement_window_ns > 0.0 && seen_main_program) {
                    throw std::invalid_argument("MoveAtom violates rearrangement window constraints");
                }
                const auto& move = std::get<MoveAtomInstruction>(instr.payload);
                if (move.atom < 0 || static_cast<std::size_t>(move.atom) >= slot_count) {
                    throw std::invalid_argument("MoveAtom references invalid atom index");
                }
                const std::size_t slot = static_cast<std::size_t>(move.atom);
                const int prev_site_id = slot_site_ids[slot];
                const double prev_position = slot_positions[slot];
                const double target_position = move.position;
                const std::optional<int> target_site_id =
                    find_site_id_for_position(hardware, target_position);
                if (!graph.empty() && prev_site_id >= 0 && !target_site_id) {
                    std::ostringstream oss;
                    oss << "MoveAtom target position " << target_position
                        << " has no transport edge";
                    throw std::invalid_argument(oss.str());
                }
                if (!graph.empty() && prev_site_id >= 0 && target_site_id &&
                    !graph.allows(prev_site_id, *target_site_id)) {
                    std::ostringstream oss;
                    oss << "MoveAtom from site " << prev_site_id << " to " << *target_site_id
                        << " is not allowed by transport edges";
                    throw std::invalid_argument(oss.str());
                }

                double displacement = std::abs(target_position - prev_position);
                if (prev_site_id >= 0 && target_site_id) {
                    const double site_distance =
                        distance_between_sites(hardware, index, prev_site_id, *target_site_id);
                    if (std::isfinite(site_distance)) {
                        displacement = site_distance;
                    }
                }

                stats[slot].moves += 1;
                stats[slot].displacement += displacement;
                total_moves += 1;

                if (limits.max_moves_per_atom > 0 && stats[slot].moves > limits.max_moves_per_atom) {
                    throw std::invalid_argument("MoveAtom exceeds per-atom move limit");
                }
                if (limits.max_moves_per_shot > 0 && total_moves > limits.max_moves_per_shot) {
                    throw std::invalid_argument("MoveAtom exceeds per-shot move limit");
                }
                if (limits.max_moves_per_configuration_change > 0 &&
                    total_moves > limits.max_moves_per_configuration_change) {
                    throw std::invalid_argument("MoveAtom exceeds per-configuration move limit");
                }
                if (limits.max_total_displacement_per_atom > 0.0 &&
                    stats[slot].displacement > limits.max_total_displacement_per_atom) {
                    std::ostringstream oss;
                    oss << "Atom " << slot << " exceeds displacement limit";
                    throw std::invalid_argument(oss.str());
                }

                slot_positions[slot] = target_position;
                slot_site_ids[slot] = target_site_id.value_or(-1);
                continue;
            }
            if (instr.op == Op::ApplyGate || instr.op == Op::Measure || instr.op == Op::Pulse) {
                seen_main_program = true;
            }
        }
    }

    std::string name() const override {
        return "transport";
    }
};

}  // namespace

std::string Validator::name() const {
    return {};
}

LambdaValidator::LambdaValidator(std::string name, ValidateFn fn)
    : name_(std::move(name)), fn_(std::move(fn)) {}

void LambdaValidator::validate(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
) const {
    if (fn_) {
        fn_(hardware, program);
    }
}

std::string LambdaValidator::name() const {
    return name_;
}

void ValidatorRegistry::register_validator(std::unique_ptr<Validator> validator) {
    if (validator) {
        validators_.push_back(std::move(validator));
    }
}

void ValidatorRegistry::run_all_validators(
    const HardwareConfig& hardware,
    const std::vector<Instruction>& program
) const {
    for (const auto& validator : validators_) {
        validator->validate(hardware, program);
    }
}

std::vector<std::string> ValidatorRegistry::validator_names() const {
    std::vector<std::string> names;
    names.reserve(validators_.size());
    for (const auto& validator : validators_) {
        names.push_back(validator->name());
    }
    return names;
}

ValidatorRegistry make_validator_registry_for(const JobRequest& job, const HardwareConfig& hw) {
    ValidatorRegistry registry;
    registry.register_validator(make_active_qubits_validator());
    const bool wants_blockade =
        job.metadata.find("blockade_validator") != job.metadata.end();
    const bool has_blockade =
        wants_blockade ||
        hw.blockade_radius > 0.0 ||
        hw.blockade_model.radius > 0.0 ||
        hw.blockade_model.radius_x > 0.0 ||
        hw.blockade_model.radius_y > 0.0 ||
        hw.blockade_model.radius_z > 0.0 ||
        !hw.blockade_model.zone_overrides.empty();
    if (has_blockade) {
        registry.register_validator(make_blockade_validator());
    }

    const bool wants_transport =
        job.metadata.find("transport_validator") != job.metadata.end();
    const bool has_transport =
        wants_transport ||
        !hw.transport_edges.empty() ||
        move_limits_has_data(hw.move_limits);
    if (has_transport) {
        registry.register_validator(make_transport_validator());
    }

    return registry;
}

std::unique_ptr<Validator> make_active_qubits_validator() {
    return std::make_unique<ActiveQubitsValidator>();
}

std::unique_ptr<Validator> make_blockade_validator() {
    return std::make_unique<BlockadeValidator>();
}

std::unique_ptr<Validator> make_transport_validator() {
    return std::make_unique<TransportValidator>();
}

}  // namespace service
