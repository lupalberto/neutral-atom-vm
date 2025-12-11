# Ticket: Validator registry

- **Priority:** Medium
+ **Status:** Done

## Summary
Generalize the geometry/blockade guard into a pluggable validator model so each device or profile can register pre-flight checks (blockade compliance, connectivity, timing, etc.) without hard-wiring them into `Device`. This keeps the NoiseEngine and core VM agnostic while making constraint enforcement extensible.

## Notes
- Define a `Validator` interface that inspects a lowered program and hardware config before submission.
- Allow profiles to register validators when they are registered/resolved (e.g., via `_PROFILE_TABLE`).
- Surface validator failures consistently in the CLI/SDK and document how to extend the registry.

## Next steps
1. Design the validator API and hooking mechanism (registry, ordering, metadata).
2. Update profile definitions to associate validators (e.g., geometry guard, timing guard). 
3. Wire the CLI/SDK to surface validator messages, reusing the `ValueError` path we just added.
4. Implement the registry once the design is settled; delay implementation until resourcing/priority is confirmed.

## Implementation notes
- Added a `ValidatorRegistry` plus `LambdaValidator` that can compose any validator functor so validators can be combined with classes or lambdas.
- Refactored the blocked/transport/active-qubit checks into validator classes and registered them before `JobRunner` executes a job.
- Created validator registry tests that prove exceptions propagate and that lambda-backed validators can be ordered.
- Added `make_validator_registry_for(...)` so each job builds its own validator list (active/transport for basic configs, blockade when requested via hardware or metadata) and exposed validator names for diagnostics/tests.
- Validator selection now inspects the job’s metadata, blockade radii, and transport/move-limit data so only the relevant checks run per job.
