#include "engine_statevector.hpp"

#include <iostream>

int main() {
    HardwareConfig cfg;
    cfg.positions = {0.0, 1.0};
    cfg.blockade_radius = 1.5;

    StatevectorEngine engine(cfg);

    std::vector<Instruction> program;
    program.push_back(Instruction{Op::AllocArray, 2});
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"H", {0}, 0.0},
    });
    program.push_back(Instruction{
        Op::ApplyGate,
        Gate{"CX", {0, 1}, 0.0},
    });

    engine.run(program);

    const auto& state = engine.state_vector();
    std::cout << "Final state amplitudes:\n";
    for (std::size_t i = 0; i < state.size(); ++i) {
        std::cout << i << ": " << state[i] << '\n';
    }

    return 0;
}
