#pragma once

#include <array>
#include <string>
#include <variant>
#include <vector>

// Core instruction set and hardware configuration for the Neutral Atom VM.
// This header intentionally contains no simulation state or engine-specific
// logic; it is the "ISA" view shared by compilers, services, and backends.

struct ISAVersion {
    int major = 1;
    int minor = 0;
};

inline constexpr ISAVersion kCurrentISAVersion{1, 0};

inline bool operator==(const ISAVersion& lhs, const ISAVersion& rhs) {
    return lhs.major == rhs.major && lhs.minor == rhs.minor;
}

inline bool operator!=(const ISAVersion& lhs, const ISAVersion& rhs) {
    return !(lhs == rhs);
}

inline std::string to_string(const ISAVersion& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor);
}

inline constexpr std::array<ISAVersion, 1> kSupportedISAVersions{{
    ISAVersion{1, 0},
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

struct HardwareConfig {
    std::vector<double> positions;  // 1D positions for atoms
    double blockade_radius = 0.0;
};
