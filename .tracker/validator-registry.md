# Ticket: Validator registry

- **Priority:** Medium
- **Status:** Proposed

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
