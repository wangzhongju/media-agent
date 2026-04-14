# Algorithm Module

## Overview
`third_party/algorithm` contains standalone algorithm packages used by `media-agent`.
Each package exposes a stable shared-library target for upper-layer integration, while internal algorithm cores are built as private static libraries.

Current packages:
- `byteTrack`: multi-object tracking package.
- `eventEdge`: event-judging package.

## Directory Layout
```text
third_party/algorithm/
  CMakeLists.txt                # algorithm root entry
  cmake/                        # shared CMake helpers + Find modules
    generate.cmake
    FindEigen3.cmake
    Findjson.cmake
    Findspdlog.cmake
    Findyaml-cpp.cmake
  3rdparty/                     # vendored third-party headers/libs (optional)
  byteTrack/
    CMakeLists.txt
    cmake/
      modules.cmake
      srcs.cmake
    bytetrack/                  # internal ByteTrack core
    include/
    src/
  eventEdge/
    CMakeLists.txt
    cmake/
      modules.cmake
      srcs.cmake
    event/                      # internal EventEdge core
    include/
    src/
```

## Unified CMake Style
All algorithm packages follow the same pattern:
- `CMakeLists.txt`: module entry, includes `cmake/modules.cmake` and `cmake/srcs.cmake`.
- `cmake/modules.cmake`: package-level dependency discovery (`find_package(...)`).
- `cmake/srcs.cmake`: wrapper shared-library source collection and link rules.

Algorithm root behavior:
- top-level project only needs `add_subdirectory(third_party/algorithm)`.
- root `CMakeLists.txt` includes `cmake/generate.cmake`, which auto-discovers and adds algorithm packages.
- `register_algorithm_target(...)` unifies install targets and RPATH settings.

## Exported Targets
Current exported shared-library targets:
- `cdky_track` (from `byteTrack`)
- `cdky_event` (from `eventEdge`)

Internal static targets:
- `es_bytetrack`
- `es_eventedge`

## Shared Find Modules
`third_party/algorithm/cmake` provides Find modules used by algorithm packages:
- `FindEigen3.cmake`
- `Findjson.cmake`
- `Findspdlog.cmake`
- `Findyaml-cpp.cmake`

Search priority is aligned for portability:
1. explicit env/CMake root hints (`*_ROOT`)
2. `third_party/algorithm/3rdparty`
3. system default paths

## Integration Notes
- Keep upper-layer linkage to exported shared targets only (`cdky_track`, `cdky_event`).
- Do not link internal static targets directly from the main app.
- Keep algorithm wrappers C ABI stable when evolving internals.

## Maintenance Notes
- Preserve the unified CMake shape for all future algorithm packages.
- Add new third-party dependencies through shared `Find*.cmake` when possible.
- Update both English and Chinese READMEs when behavior, targets, or dependencies change.
