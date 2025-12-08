# Neutral Atom VM Astro Site

This Astro site curates the architectural, UX, and implementation material that already lives in
`neutral-atom-vm`. It highlights:

- The hardware VM ISA and roadmap (`docs/vm-architecture.md`, `src/vm/isa.hpp`).
- User-level surfaces (SDK/CLI/service flows from `docs/ux.md` and `python/src/neutral_atom_vm`).
- Noise modeling plus Stim integration (`docs/noise.md`, `docs/stim-integration.md`, `src/noise.*`).
- Concrete code references: `service::JobRunner`, `HardwareVM`, `StatevectorEngine`, and SDK helpers.

## Getting Started

```bash
cd neutral-atom-vm/astro-site
npm install   # requires network access to download Astro
npm run dev   # start the local dev server on http://localhost:4321
npm run build # produce a static site in dist/
```

> **Note:** The CLI in this environment has restricted network access. If `npm install` fails, rerun the
> command once you are on a network that can reach the public npm registry.

## Content Structure

- `src/pages/index.astro` – landing page w/ doc map
- `src/pages/architecture.astro` – ISA & device profile deep dive
- `src/pages/ux.astro` – persona-driven journeys
- `src/pages/noise.astro` – noise pipeline & Stim split
- `src/pages/implementation.astro` – source-level walkthrough
- `src/layouts/BaseLayout.astro` – shared layout, navigation, and styling
- `src/components/Callout.astro` – minor highlighting component

Update the content sections as the VM evolves (e.g., once the CLI ships or new device presets are added).
