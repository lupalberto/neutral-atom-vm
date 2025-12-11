#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Core instruction set and hardware configuration for the Neutral Atom VM.
// This header intentionally contains no simulation state or engine-specific
// logic; it is the "ISA" view shared by compilers, services, and backends.

struct ISAVersion {
    int major = 1;
    int minor = 0;
};

inline constexpr ISAVersion kCurrentISAVersion{1, 1};

inline bool operator==(const ISAVersion& lhs, const ISAVersion& rhs) {
    return lhs.major == rhs.major && lhs.minor == rhs.minor;
}

inline bool operator!=(const ISAVersion& lhs, const ISAVersion& rhs) {
    return !(lhs == rhs);
}

inline std::string to_string(const ISAVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor);
}

inline constexpr std::array<ISAVersion, 2> kSupportedISAVersions{{
    ISAVersion{1, 0},
    ISAVersion{1, 1},
}};

inline bool is_supported_isa_version(const ISAVersion& version) {
    for (const auto& supported : kSupportedISAVersions) {
        if (supported == version) {
            return true;
        }
    }
    return false;
}

inline std::string supported_versions_to_string() {
    std::string out;
    for (std::size_t i = 0; i < kSupportedISAVersions.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += to_string(kSupportedISAVersions[i]);
    }
    return out;
}

struct MoveAtomInstruction {
    int atom = 0;
    double position = 0.0;
};

struct WaitInstruction {
    double duration = 0.0;
};

struct PulseInstruction {
    int target = 0;
    double detuning = 0.0;
    double duration = 0.0;
};

enum class Op {
    AllocArray,
    ApplyGate,
    Measure,
    MoveAtom,
    Wait,
    Pulse,
};

struct Gate {
    std::string name;          // "X", "H", "CX", "CZ", ...
    std::vector<int> targets;  // qubit indices
    double param = 0.0;        // angle or other parameter
};

struct Instruction {
    Op op;
    std::variant<
        int,
        Gate,
        std::vector<int>,
        MoveAtomInstruction,
        WaitInstruction,
        PulseInstruction> payload;
    // AllocArray: payload = int (n_qubits)
    // ApplyGate:  payload = Gate
    // Measure:    payload = std::vector<int> (targets)
    // MoveAtom:   payload = MoveAtomInstruction
    // Wait:       payload = WaitInstruction
    // Pulse:      payload = PulseInstruction
};

// ISA v1.1 hardware description extends the original v1.0 view
// with richer, hardware-oriented metadata. Existing fields remain
// valid and are treated as a legacy 1D geometry when the newer
// structures are left at their defaults.

struct SiteDescriptor {
    int id = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int zone_id = 0;
};

enum class ConnectivityKind {
    AllToAll,
    NearestNeighborChain,
    NearestNeighborGrid,
};

struct NativeGate {
    std::string name;
    int arity = 1;
    double duration_ns = 0.0;
    double angle_min = 0.0;
    double angle_max = 0.0;
    ConnectivityKind connectivity = ConnectivityKind::AllToAll;
};

struct InteractionPair {
    int site_a = 0;
    int site_b = 0;
};

struct InteractionGraph {
    std::string gate_name;
    std::vector<InteractionPair> allowed_pairs;
};

struct BlockadeZoneOverride {
    int zone_id = 0;
    double radius = 0.0;
};

struct BlockadeModel {
    double radius = 0.0;
    double radius_x = 0.0;
    double radius_y = 0.0;
    double radius_z = 0.0;
    std::vector<BlockadeZoneOverride> zone_overrides;
};

struct TimingLimits {
    double min_wait_ns = 0.0;
    double max_wait_ns = 0.0;
    int max_parallel_single_qubit = 0;  // 0 = unlimited
    int max_parallel_two_qubit = 0;     // 0 = unlimited
    int max_parallel_per_zone = 0;      // 0 = unlimited
    double measurement_cooldown_ns = 0.0;
    double measurement_duration_ns = 0.0;
};

struct PulseLimits {
    double detuning_min = 0.0;
    double detuning_max = 0.0;
    double duration_min_ns = 0.0;
    double duration_max_ns = 0.0;
    int max_overlapping_pulses = 0;  // 0 = unlimited
};

struct TransportEdge {
    int src_site_id = 0;
    int dst_site_id = 0;
    double distance = 0.0;
    double duration_ns = 0.0;
};

struct MoveLimits {
    double max_total_displacement_per_atom = 0.0;
    int max_moves_per_atom = 0;
    int max_moves_per_shot = 0;
    int max_moves_per_configuration_change = 0;
    double rearrangement_window_ns = 0.0;
};

struct HardwareConfig {
    // Legacy v1.0 fields.
    std::vector<double> positions;  // 1D positions for atoms (chain view)
    std::vector<std::vector<double>> coordinates;  // Optional multidimensional coordinates.
    double blockade_radius = 0.0;  // Effective global blockade radius (kept for compatibility).

    // v1.1 geometry/configuration extensions.
    std::vector<int> site_ids;  // Mapping from logical slots into the lattice described by `sites`.
    std::vector<InteractionGraph> interaction_graphs;  // Optional per-gate interaction graphs.
    BlockadeModel blockade_model;  // Optional anisotropic/zone-aware blockade model.

    // v1.1 lattice & timing extensions.
    std::vector<SiteDescriptor> sites;
    std::vector<NativeGate> native_gates;
    TimingLimits timing_limits;
    PulseLimits pulse_limits;
    std::vector<TransportEdge> transport_edges;
    MoveLimits move_limits;
};

using SiteIndexMap = std::unordered_map<int, std::size_t>;

inline SiteIndexMap build_site_index(const HardwareConfig& hw) {
    SiteIndexMap index;
    index.reserve(hw.sites.size());
    for (std::size_t idx = 0; idx < hw.sites.size(); ++idx) {
        index[hw.sites[idx].id] = idx;
    }
    return index;
}

inline const SiteDescriptor* site_descriptor_for_slot(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int slot
) {
    if (slot < 0) {
        return nullptr;
    }
    const std::size_t slot_index = static_cast<std::size_t>(slot);
    if (slot_index >= hw.site_ids.size()) {
        return nullptr;
    }
    const int site_id = hw.site_ids[slot_index];
    auto it = index.find(site_id);
    if (it == index.end()) {
        return nullptr;
    }
    const std::size_t site_index = it->second;
    if (site_index >= hw.sites.size()) {
        return nullptr;
    }
    return &hw.sites[site_index];
}

inline int zone_for_slot(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int slot
) {
    if (const SiteDescriptor* site = site_descriptor_for_slot(hw, index, slot)) {
        return site->zone_id;
    }
    return 0;
}

inline int site_id_for_slot(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int slot
) {
    if (const SiteDescriptor* site = site_descriptor_for_slot(hw, index, slot)) {
        return site->id;
    }
    return -1;
}

inline const SiteDescriptor* site_descriptor_by_id(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int site_id
) {
    const auto it = index.find(site_id);
    if (it == index.end()) {
        return nullptr;
    }
    const std::size_t site_index = it->second;
    if (site_index >= hw.sites.size()) {
        return nullptr;
    }
    return &hw.sites[site_index];
}

inline double distance_between_sites(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int site_a,
    int site_b
) {
    const SiteDescriptor* sa = site_descriptor_by_id(hw, index, site_a);
    const SiteDescriptor* sb = site_descriptor_by_id(hw, index, site_b);
    if (!sa || !sb) {
        return std::numeric_limits<double>::infinity();
    }
    const double dx = sa->x - sb->x;
    const double dy = sa->y - sb->y;
    const double dz = sa->z - sb->z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool interaction_pair_allowed(const InteractionGraph& graph, int a, int b) {
    for (const auto& pair : graph.allowed_pairs) {
        if ((pair.site_a == a && pair.site_b == b) ||
            (pair.site_a == b && pair.site_b == a)) {
            return true;
        }
    }
    return false;
}

inline const InteractionGraph* find_interaction_graph(
    const HardwareConfig& hw,
    const std::string& gate_name
) {
    for (const auto& graph : hw.interaction_graphs) {
        if (graph.gate_name == gate_name) {
            return &graph;
        }
    }
    return nullptr;
}

struct SpatialDelta {
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    double distance = 0.0;
};

inline SpatialDelta compute_spatial_delta(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int q0,
    int q1
) {
    SpatialDelta delta;
    if (q0 < 0 || q1 < 0) {
        delta.distance = std::numeric_limits<double>::infinity();
        return delta;
    }
    const std::size_t idx0 = static_cast<std::size_t>(q0);
    const std::size_t idx1 = static_cast<std::size_t>(q1);
    if (idx0 < hw.coordinates.size() && idx1 < hw.coordinates.size()) {
        const auto& lhs = hw.coordinates[idx0];
        const auto& rhs = hw.coordinates[idx1];
        const auto coord = [](const std::vector<double>& row, std::size_t offset) {
            return row.size() > offset ? row[offset] : 0.0;
        };
        delta.dx = coord(lhs, 0) - coord(rhs, 0);
        delta.dy = coord(lhs, 1) - coord(rhs, 1);
        delta.dz = coord(lhs, 2) - coord(rhs, 2);
        delta.distance = std::sqrt(delta.dx * delta.dx + delta.dy * delta.dy + delta.dz * delta.dz);
        return delta;
    }
    const SiteDescriptor* sa = site_descriptor_for_slot(hw, index, q0);
    const SiteDescriptor* sb = site_descriptor_for_slot(hw, index, q1);
    if (sa && sb) {
        delta.dx = sa->x - sb->x;
        delta.dy = sa->y - sb->y;
        delta.dz = sa->z - sb->z;
        delta.distance = std::sqrt(delta.dx * delta.dx + delta.dy * delta.dy + delta.dz * delta.dz);
        return delta;
    }
    if (idx0 < hw.positions.size() && idx1 < hw.positions.size()) {
        delta.dx = hw.positions[idx0] - hw.positions[idx1];
        delta.dy = 0.0;
        delta.dz = 0.0;
        delta.distance = std::abs(delta.dx);
        return delta;
    }
    delta.distance = std::numeric_limits<double>::infinity();
    return delta;
}

inline double zone_override_radius(const BlockadeModel& model, int zone) {
    for (const auto& entry : model.zone_overrides) {
        if (entry.zone_id == zone && entry.radius > 0.0) {
            return entry.radius;
        }
    }
    return 0.0;
}

inline std::optional<std::string> blockade_violation_reason(
    const HardwareConfig& hw,
    const SiteIndexMap& index,
    int q0,
    int q1
) {
    SpatialDelta delta = compute_spatial_delta(hw, index, q0, q1);
    if (!std::isfinite(delta.distance)) {
        return std::string("insufficient geometry for blockade check");
    }
    const BlockadeModel& model = hw.blockade_model;
    const auto axis_limit = [&](double value, const char* axis) -> std::optional<std::string> {
        if (value > 0.0) {
            double delta_axis = 0.0;
            if (axis[0] == 'x') {
                delta_axis = std::abs(delta.dx);
            } else if (axis[0] == 'y') {
                delta_axis = std::abs(delta.dy);
            } else if (axis[0] == 'z') {
                delta_axis = std::abs(delta.dz);
            }
            if (delta_axis > value) {
                std::ostringstream oss;
                oss << "anisotropic blockade (" << axis << "-axis limit " << value << ")";
                return oss.str();
            }
        }
        return std::nullopt;
    };
    if (auto reason = axis_limit(model.radius_x, "x")) {
        return reason;
    }
    if (auto reason = axis_limit(model.radius_y, "y")) {
        return reason;
    }
    if (auto reason = axis_limit(model.radius_z, "z")) {
        return reason;
    }
    double effective_radius = model.radius > 0.0 ? model.radius : hw.blockade_radius;
    const int zone = zone_for_slot(hw, index, q0);
    const double zone_radius = zone_override_radius(model, zone);
    if (zone_radius > 0.0) {
        effective_radius = zone_radius;
    }
    if (effective_radius <= 0.0) {
        return std::nullopt;
    }
    if (delta.distance > effective_radius) {
        std::ostringstream oss;
        if (zone_radius > 0.0) {
            oss << "zone " << zone << " blockade radius " << zone_radius;
        } else {
            oss << "blockade radius " << effective_radius;
        }
        return oss.str();
    }
    return std::nullopt;
}
