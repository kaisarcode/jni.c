# jni.c Design

## Purpose

`jni.c` is the JNI loader shim of the kclib collection for Android. It lets
Android apps call kclib native functions through one small, fixed Java entry
point without adding JNI to the kclibs themselves and without static-linking
kclib objects into the shim.

The kclib ecosystem stays pure portable C. A kclib that wants to be reachable
from Android exports one extra C function, `kc_<name>_run()`, and ships its
shared library. `jni.c` is the only Android-aware component in the call path.

## Operating Model

An Android app embeds `libjni.so` and one or more `lib<name>.so` kclibs in the
same native library directory. The app declares a fixed class and calls it:

```java
package com.kaisarcode.kclib;

public final class KclibBridge {
    static { System.loadLibrary("jni"); }
    public static native String run(String payloadJson);
}
```

No account, subscription, hosted API, package registry, or remote service is
involved. The bridge is local to the process.

## Bridge Architecture

The bridge follows the same canonical pattern as `wvw.c`:

1. JavaScript calls `NativeBridge.invoke("runKclib", payload)`.
2. The injected JavaScript calls `KclibBridge.run(JSON.stringify(message))`.
3. `kcjni_native_run` extracts and validates the `"lib"` field.
4. The whitelist is checked against the manifest metadata.
5. `dladdr()` locates `libjni.so` itself; the kclib directory is derived.
6. The bridge opens `<dir>/lib<name>.so` with `dlopen(RTLD_NOW | RTLD_LOCAL)`.
7. It resolves `kc_<name>_run` with `dlsym()`.
8. It calls the function with the exact `argsJson` string.
9. It returns a structured JSON response.

## Dispatch Flow

1. The app calls `KclibBridge.run(payloadJson)`.
2. `kcjni_native_run` parses the JSON payload with Parson.
3. The `"lib"` field is extracted and validated (`[A-Za-z0-9_]+`, max 63 bytes).
4. The name is checked against the whitelist loaded from manifest metadata.
5. `dladdr()` on `kcjni_native_run` yields the absolute path; the directory is
    taken as the kclib directory.
6. The bridge opens `<dir>/lib<name>.so` with `dlopen(RTLD_NOW | RTLD_LOCAL)`,
    caching the handle per name.
7. It resolves `kc_<name>_run` with `dlsym()`.
8. It calls the function with the exact `argsJson` string.
9. It returns `{"ok":true,"result":...}` or `{"ok":false,"error":{...}}`.

## Payload Contract

`payloadJson` is a JSON object with the shape:

```json
{"lib":"<name>","cmd":"<command>","args":{...},"handle":0}
```

| Field | Description |
| :--- | :--- |
| `lib` | kclib identifier. Names `lib<name>.so` and symbol `kc_<name>_run`. Required. |
| `cmd` | Command name, dispatched by the kclib. |
| `args` | JSON object with command arguments (kclib-defined schema). |
| `handle` | Reserved for future stateful calls; `0` for stateless kclibs. |

The kclib receives the exact `payloadJson` string and defines its own `args`
schema.

## Response Contract

Success:

```json
{"ok":true,"result":<kclib output>}
```

Error:

```json
{"ok":false,"error":{"code":"...","message":"..."}}
```

Error codes:

| Code | Meaning |
| :--- | :--- |
| `INVALID_ARGUMENT` | Missing or invalid `lib` field, or malformed JSON. |
| `KCLIB_NOT_ALLOWED` | The kclib name is not in the application whitelist. |
| `KCLIB_FAILED` | The kclib runner returned an error or the library could not be loaded. |

## Whitelist

The allowed kclib names are declared in `AndroidManifest.xml` as
`<meta-data android:name="com.kaisarcode.kclib.allowed_kclibs"
android:value="..."/>`. The bridge reads this at load time via JNI using
`ActivityThread.currentActivityThread().getApplication()` ->
`PackageManager.getApplicationInfo()` -> `Bundle.getString()`.

When the whitelist is empty or absent, all kclibs are allowed. Names are
validated as `[A-Za-z0-9_]+` and at most 63 bytes before any path or `dlsym`
is attempted.

## Why dladdr Instead of a Passed Directory

The kclib must sit next to the bridge. `dladdr()` on `kcjni_native_run`
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

Handles are cached in a dynamically grown array, keyed by the validated kclib
name. A cached handle is reused on later calls. All handles are closed in
`JNI_OnUnload`. There is no reference counting.

## Lib Name Validation

The `"lib"` field must match `[A-Za-z0-9_]+` and be at most 63 bytes. It is
used to build both a filesystem path and a symbol name, so the check prevents
path traversal and symbol injection before any path or `dlsym` is attempted.

## Security

- Strict lib-name validation prevents path and symbol injection.
- JSON extraction fails on malformed or oversized input.
- `dlopen` uses `RTLD_NOW | RTLD_LOCAL`.
- The whitelist is read once from the manifest and enforced on every call.
- No hidden state, background work, or network access.

## Resource Model

- Bounded buffers for names, paths, symbols, and error messages.
- JSON parsing uses Parson (vendored, MIT licensed).
- malloc'd strings returned by kclib run functions are always freed.
- No threads, no background work, no global state beyond the handle cache
    and the whitelist.

## Inspectability

The complete path is short and visible:

1. The app passes one JSON payload.
2. The bridge parses it and extracts `"lib"`.
3. The whitelist is checked.
4. The kclib path is derived from the bridge's own loaded path.
5. The kclib is opened, its run symbol resolved, and called.
6. A structured JSON response is returned.

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
- Loaded handles are cached in a dynamic array and released on unload.
- Errors are returned as structured JSON objects, not thrown exceptions.
- The whitelist is read from manifest metadata, not passed as parameters.

These constraints keep `libjni.so` a sharp, single-purpose adapter between the
Java boundary and the pure C kclibs.
