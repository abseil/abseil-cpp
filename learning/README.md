# Abseil Learning Journal

This folder is a lightweight learning space for reading Abseil code and writing tiny playground examples.

## Focus areas
- absl::Status and absl::StatusOr<T>
- absl::Time and absl::Duration
- absl::flat_hash_map

## Plan
1. Read the header files and follow the types and helpers.
2. Keep playground files small and focused.
3. Add new playgrounds incrementally as I learn more APIs.

## Sessions
### Session 1
- Created minimal playground sources for Status, Time, and flat_hash_map.
- Next: read implementation details around status macros/helpers and time formatting utilities.

### Session 2
- Practiced common Status patterns:
  - Early return on failure
  - StatusOr chaining across steps
  - Adding context to errors while preserving the original cause
- Next: read helper utilities around status handling and investigate how errors are commonly propagated across library boundaries.
