# Changelog

## Unreleased

- The in-game brightness setting works: D3DDevice_SetGammaRamp's corrected ramp (device +15408) goes to the renderer's
  output table at the next present (from ds2-nx 1.2.2).
- Vibration: `input_vibracion` defaults to true, so the game's own vibration setting decides (`ds3.toml` can still turn
  it off).
- RenderWare Audio: StreamPool::AcquireStream with no pool answers "no stream" instead of reading the pool's flags at
  +0x3C (the null-pool crash Dead Space 2 had after sleep and on some loads).

## 1.0.0

First release (see the GitHub release notes).
