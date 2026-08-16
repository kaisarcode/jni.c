# jni.c - JNI Bridge for kclib Native Libraries

`jni.c` produces `libjni.so`, a small Android-only shared library that maps one
JNI method, `com.kaisarcode.kclib.KclibBridge.run(String, String)`, to the
kclib requested by name. The kclib is loaded at runtime with
`dlopen`/`dlsym` from the same directory that holds `libjni.so`; kclib
repositories stay pure C and never gain JNI.

---

## Contract

### Java

```java
package com.kaisarcode.kclib;

public final class KclibBridge {
    public static native String run(String argsJson, String stdin);
}
```

### Payload

`argsJson` is a JSON object:

```json
{"lib":"grd","cmd":"split","args":["-w","1920","-H","1080","-k","row","-W","1 2 1"]}
```

| Field | Description |
| :--- | :--- |
| `lib` | kclib identifier. Names the shared library `lib<name>.so` and the run symbol `kc_<name>_run`. Required. |
| `cmd` | Command name, forwarded to the kclib untouched. |
| `args` | Command arguments, forwarded to the kclib untouched. |

`stdin` is reserved and currently ignored by `libjni.so`.

### kclib side

A kclib that wants to be callable from Android exports exactly (declared in
`src/kcjni.h`):

```c
char *kc_<name>_run(const char *payload_json, char **out_err);
```

- On success returns a malloc'd output string and sets `*out_err` to NULL.
- On failure returns NULL and sets `*out_err` to a malloc'd message.

### Result

A successful run returns the kclib output string. Any failure returns a string
that starts with `error: ` followed by a short cause.

---

## Build

Requirements:

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- Android NDK version `27.2.12479018` at `$ANDROID_HOME/ndk/`

Artifacts are generated under `bin/{arch}/android/`:

```bash
make
```

Produces:

- `bin/aarch64/android/libjni.so`
- `bin/armv7/android/libjni.so`

Individual targets are also available:

```bash
make aarch64/android
make armv7/android
```

---

## Development Requirements

- `ANDROID_HOME` defaults to `$HOME/.local/share/android-sdk`.
- `NDK_VERSION` defaults to `27.2.12479018`.
- Android platform level `android-21` is used for both ABIs.
- No host, desktop, iOS, or CLI build target exists; `libjni.so` is Android
    only by design.

---

## Beta Notice

This is a beta project. It was created out of a personal need for these
libraries, but no guarantees are provided regarding its stability or future
support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid
long-term dependency on platforms like GitHub, and I do not maintain fixed
infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3
(GPLv3)**.
