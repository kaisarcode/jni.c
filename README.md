# jni.c - JNI Bridge for kclib Native Libraries

`jni.c` produces `libjni.so`, an Android-only shared library that maps one
JNI method, `com.kaisarcode.kclib.KclibBridge.run(String)`, to the kclib
requested by name. The kclib is loaded at runtime with `dlopen`/`dlsym` from
the same directory that holds `libjni.so`; kclib repositories stay pure C and
never gain JNI.

The bridge follows the same canonical pattern as the `wvw.c` NativeBridge:
structured JSON responses, Parson for JSON handling, whitelist-gated kclib
dispatch, and the standard `kc_<name>_run` runner contract.

---

## Contract

### Java

```java
package com.kaisarcode.kclib;

public final class KclibBridge {
    static { System.loadLibrary("jni"); }
    public static native String run(String payloadJson);
}
```

### Payload

`payloadJson` is a JSON object forwarded verbatim to the kclib:

```json
{"lib":"grd","cmd":"split","args":{"w":1920,"h":1080,"k":"row","W":[1,2,1]}}
```

| Field | Description |
| :--- | :--- |
| `lib` | kclib identifier. Names the shared library `lib<name>.so` and the run symbol `kc_<name>_run`. Required. |
| `cmd` | Command name, dispatched by the kclib. |
| `args` | Command arguments as a JSON object. The kclib defines the schema. |
| `handle` | Reserved for future stateful calls. Must be `0` for stateless kclibs. |

### Response

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
| `INVALID_ARGUMENT` | Missing or invalid `lib` field, or malformed JSON payload. |
| `KCLIB_NOT_ALLOWED` | The kclib name is not in the application whitelist. |
| `KCLIB_FAILED` | The kclib runner returned an error or the shared library could not be loaded. |

### kclib side

A kclib that wants to be callable from Android exports exactly (declared in
`src/kcjni.h`):

```c
char *kc_<name>_run(const char *payload_json, char **out_err);
```

- On success returns a malloc'd output string and sets `*out_err` to NULL.
- On failure returns NULL and sets `*out_err` to a malloc'd message.

---

## Whitelist

The allowed kclib names are declared in `AndroidManifest.xml` as metadata:

```xml
<meta-data
    android:name="com.kaisarcode.kclib.allowed_kclibs"
    android:value="grd,redp2p" />
```

`libjni.so` reads this metadata at load time via JNI. When the whitelist is
empty or the metadata is absent, all kclibs are allowed. Names are validated
as `[A-Za-z0-9_]+` and at most 63 bytes before any path or symbol is built.

---

## JavaScript Interface

The embedding app injects `NativeBridge` JavaScript into the WebView. The
API matches `wvw.c`:

```js
NativeBridge.invoke("runKclib", {
    lib: "grd",
    cmd: "split",
    args: { w: 1920, h: 1080, k: "row", W: [1, 2, 1] }
})
.then(function (result) { /* result = {boxes:[...]} */ })
.catch(function (err) { /* err = {code:"...", message:"..."} */ });
```

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

Or equivalently:

```bash
make all
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

## Dependencies

- Parson (`lib/parson/`) is the vendored JSON library (same copy used by
    `wvw.c`, `redp2p.c`, and `grd.c`), with its MIT `LICENSE`.

---

## Beta Notice

This project is a beta project. It was created out of a personal need for these
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
