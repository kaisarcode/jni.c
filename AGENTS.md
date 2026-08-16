# AGENTS.md

## Project Context

`jni.c` produces `libjni.so`, a small Android-only JNI bridge for the kclib
collection. It exposes one native entry point,
`com.kaisarcode.kclib.KclibBridge.run(String, String)`, and forwards each call
to the kclib requested by name. The kclib is loaded at runtime with
`dlopen`/`dlsym` from the same directory as `libjni.so`; kclib repositories
remain pure C and never gain JNI.

Read `README.md` for the effective contract and `DESIGN.md` for architectural
boundaries before modifying the project.

## Core Invariants

- Android only. No host, desktop, iOS, or CLI build target exists.
- One shared artifact per ABI: `bin/<arch>/android/libjni.so`.
- `libjni.so` never links a kclib; discovery and dispatch happen at runtime.
- The lib name comes from the `"lib"` field and is validated as
    `[A-Za-z0-9_]+` before any path or symbol is built.
- The payload is forwarded to the kclib verbatim; the bridge interprets only
    `"lib"`.
- Loaded handles are cached in a fixed-size array and released in
    `JNI_OnUnload`.
- Errors are returned as strings prefixed with `error: `; the bridge does not
    throw Java exceptions for business errors.

## Payload and Error Contract

`KclibBridge.run(String argsJson, String stdin)` receives:

```json
{"lib":"<name>","cmd":"<command>","args":[...]}
```

- `"lib"` selects the kclib and its run symbol. Required and validated.
- `"cmd"` and `"args"` are forwarded untouched; the kclib dispatches on them.
- `stdin` is reserved; kcjni accepts it and ignores it.

The kclib run contract is `char *kc_<name>_run(const char *payload_json,
char **out_err)` as declared in `src/kcjni.h`. Successful runs return a
malloc'd output string; failures return NULL and set `*out_err`. kcjni owns
the returned strings and frees them.

## Resource Model

- Fixed-size handle cache; no dynamic registries.
- Bounded buffers for names, paths, symbols, and error messages.
- malloc'd strings returned by kclib run functions are always freed.
- No threads, no background work, no global state beyond the handle cache.

## Security

- Strict lib-name validation prevents path and symbol injection.
- JSON extraction fails on malformed or oversized input.
- `dlopen` uses `RTLD_NOW | RTLD_LOCAL`.

## Source Layout

Preserve exactly:

- `src/kcjni.c` for the JNI bridge, loader, and JSON extraction;
- `src/kcjni.h` for the kclib native-run contract.

Do not create additional source or header files. There is no `src/test.c`: the
bridge has no host runtime and is exercised through the Android app that
embeds it. Cross-compilation proves compilation only.

## Forbidden Default Recommendations

Do not recommend or implement without explicit instruction:

- CLI executables, host or desktop ports;
- static linking of kclibs into the bridge;
- a generic JNI reflection/dispatch layer;
- network access, telemetry, or remote services;
- plugin ecosystems or extension mechanisms;
- RPC frameworks or service infrastructure;
- caches or registries beyond the fixed handle array.

## Build and Validation

Build with `make` (both ABIs) or `make aarch64/android` / `make armv7/android`.
Treat compiler warnings as failures. Do not run `make clean` or delete build
artifacts without authorization.

## Documentation

Keep documentation operational and truthful:

- `README.md` for the effective Java and kclib contracts, build, requirements,
    status, and license;
- `DESIGN.md` for the bridge architecture, dispatch flow, and non-goals;
- `AGENTS.md` for implementation constraints and agent behavior.

Do not describe the absence of enterprise facilities as incompleteness.

## Completion Standard

A change is complete when:

- the JNI contract, the payload contract, and the error contract agree;
- the loader resolves and validates names, paths, and symbols safely;
- both Android ABIs build without warnings;
- documentation matches actual behavior;
- no unrelated platform or enterprise machinery was introduced.

The goal is one small, deterministic JNI adapter in front of pure C kclibs.
