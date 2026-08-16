# jni.c Design

## Purpose

`jni.c` is the JNI loader shim of the kclib collection. It lets Android apps
call kclib native functions through one small, fixed Java entry point without
adding JNI to the kclibs themselves and without static-linking kclib objects
into the shim.

The kclib ecosystem stays pure portable C. A kclib that wants to be reachable
from Android exports one extra C function, `kc_<name>_run()`, and ships its
shared library. `jni.c` is the only Android-aware component in the call path.

## Operating Model

An Android app embeds `libjni.so` and one or more `lib<name>.so` kclibs in the
same private directory. The app declares a fixed class and calls it:

```java
package com.kaisarcode.kclib;

public final class KclibBridge {
    public static native String run(String argsJson, String stdin);
}
```

No account, subscription, hosted API, package registry, or remote service is
involved. The bridge is local to the process.

## Bridge

`libjni.so` registers `run` in `JNI_OnLoad` through `RegisterNatives` against
`com/kaisarcode/kclib/KclibBridge`, binding it to the internal function
`kcjni_native_run`. `JNI_OnUnload` releases every cached kclib handle.

## Dispatch Flow

1. The app calls `KclibBridge.run(argsJson, stdin)`.
2. `kcjni_native_run` extracts and validates the `"lib"` field of `argsJson`.
3. `dladdr()` on a function of `libjni.so` yields the absolute path used to
    load the bridge; its directory is taken as the kclib directory.
4. The bridge opens `<dir>/lib<name>.so` with `dlopen(RTLD_NOW | RTLD_LOCAL)`,
    caching the handle per name.
5. It resolves `kc_<name>_run` with `dlsym()`.
6. It calls the function with the exact `argsJson` string.
7. It returns the output string, or `error: <detail>` on any failure.

## Why dladdr Instead of a Passed Directory

The kclib must sit next to the bridge. `dladdr()` on a symbol of `libjni.so`
returns the absolute path used to load it, so the directory is discovered
instead of passed. This keeps the Java contract minimal and the bridge
position-independent: no directory argument, no environment variable, no
hard-coded filesystem convention.

## Why the Payload Is Forwarded Verbatim

The kclib run contract receives the exact string the app sent. The bridge
interprets only `"lib"`; `"cmd"` and `"args"` are the kclib's concern. Passing
the original bytes avoids any re-serialization, escaping, or key-order
subtleties, and the same JSON the app sees is the JSON the kclib receives.

## Why Not Static Linking

Statically linking every kclib `.a` into `libjni.so` would bind the bridge to
one fixed set of libraries, force a rebuild per kclib, and couple every kclib
to the bridge's build. Dynamic loading keeps `libjni.so` generic: it can
dispatch to any kclib dropped into its directory, today or later, without
rebuilding.

## Handle Cache

Handles are cached in a fixed-size array (`KCJNI_LIBS_MAX`), keyed by the
validated kclib name. A cached handle is reused on later calls. All handles
are closed in `JNI_OnUnload`. There is no dynamic registry and no reference
counting.

## Lib Name Validation

The `"lib"` field must match `[A-Za-z0-9_]+` and be at most 63 bytes. It is
used to build both a filesystem path and a symbol name, so the check prevents
path traversal and symbol injection before any path or `dlsym` is attempted.

## Payload and Error Contract

- `lib` is required and validated.
- `cmd` and `args` are forwarded untouched.
- `stdin` is accepted by the JNI method and ignored; the kclib run contract
    has no stdin yet.
- Success: the kclib output string is returned as a `jstring`.
- Failure: a string beginning with `error: ` is returned. The bridge does not
    throw Java exceptions for business errors; JNI and VM-level failures are
    left to the runtime.

## Resource Model

- Bounded buffers for names, paths, symbols, and error messages.
- JSON extraction fails on malformed, missing, or oversized input.
- malloc'd strings returned by kclib run functions are always freed by the
    bridge.
- No threads, no background work, no global state beyond the handle cache.

## Android Linker Namespace Note

`dlopen()` of a kclib from a private app directory may be subject to Android
linker namespace restrictions depending on API level and how the app's native
libraries were loaded. This must be validated during integration with the
embedding app; if it fails, the app must ensure the kclib directory is visible
to the namespace the bridge runs in. The bridge itself does not depend on a
specific namespace policy beyond standard `dlopen` behavior.

## Inspectability

The complete path is short and visible:

1. The app passes one JSON payload.
2. The bridge extracts `"lib"` and validates it.
3. The kclib path is derived from the bridge's own loaded path.
4. The kclib is opened, its run symbol resolved, and called.
5. The output or an `error: ` string is returned.

## Non-Goals

`jni.c` is not intended to provide:

- a CLI executable or host/desktop port;
- iOS, Windows, or other non-Android targets;
- a common kclib ABI or runtime;
- a plugin ecosystem or extension mechanism;
- an RPC or network transport;
- service discovery or daemon supervision;
- persistent state or background work;
- telemetry, analytics, tracing, or metrics;
- hosted infrastructure or cloud abstractions;
- automatic updates;
- enterprise administration;
- dynamic reconfiguration.

These omissions keep the bridge small and inspectable. They are not an
unfinished product roadmap.

## Core Invariants

- Android only; one shared artifact per ABI.
- The bridge never links a kclib; discovery and dispatch happen at runtime.
- The lib name is validated before any path or symbol is built.
- The payload reaches the kclib verbatim.
- Loaded handles are cached in a fixed array and released on unload.
- Errors are returned as `error: ` strings, not thrown exceptions.
- No hidden state, background work, or network access.

These constraints keep `libjni.so` a sharp, single-purpose adapter between the
Java boundary and the pure C kclibs.
