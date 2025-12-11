#pragma once

#include <array>
#include <cstddef>
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

struct HardwareConfig {
    // Legacy v1.0 fields.
    std::vector<double> positions;  // 1D positions for atoms
    std::vector<std::vector<double>> coordinates;  // Optional multidimensional coordinates.
    std::vector<int> site_ids;  // Mapping from logical slots into the lattice described by `sites`.
    double blockade_radius = 0.0;

    // v1.1 extensions.
    std::vector<SiteDescriptor> sites;
    std::vector<NativeGate> native_gates;
    TimingLimits timing_limits;
    PulseLimits pulse_limits;
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
