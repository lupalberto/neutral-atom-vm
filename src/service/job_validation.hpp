#pragma once

#include "vm/isa.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace service {

struct JobRequest;

class Validator {
public:
    virtual ~Validator() = default;
    virtual void validate(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const = 0;
    virtual std::string name() const;
};

class LambdaValidator final : public Validator {
public:
    using ValidateFn = std::function<void(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    )>;

    LambdaValidator(std::string name, ValidateFn fn);
    void validate(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const override;
    std::string name() const override;

private:
    std::string name_;
    ValidateFn fn_;
};

class ValidatorRegistry final {
public:
    void register_validator(std::unique_ptr<Validator> validator);
    void run_all_validators(
        const HardwareConfig& hardware,
        const std::vector<Instruction>& program
    ) const;
    std::vector<std::string> validator_names() const;

private:
    std::vector<std::unique_ptr<Validator>> validators_;
};

std::unique_ptr<Validator> make_active_qubits_validator();
std::unique_ptr<Validator> make_blockade_validator();
std::unique_ptr<Validator> make_transport_validator();

ValidatorRegistry make_validator_registry_for(
    const JobRequest& job,
    const HardwareConfig& hw
);

}  // namespace service
