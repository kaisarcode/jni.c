/**
 * kcjni.c - JNI bridge for kclib native libraries
 * Summary: Android-only JNI loader shim that dispatches calls to pure C kclibs.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include <jni.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kcjni.h"

#define KCJNI_LIB_NAME_MAX 63
#define KCJNI_LIBS_MAX 16
#define KCJNI_PATH_MAX 1024
#define KCJNI_SYM_MAX 96
#define KCJNI_ERR_MAX 1024

#define KCJNI_BRIDGE_CLASS "com/kaisarcode/kclib/KclibBridge"

typedef struct {
    char name[KCJNI_LIB_NAME_MAX + 1];
    void *handle;
} kcjni_lib_t;

static kcjni_lib_t g_libs[KCJNI_LIBS_MAX];
static int g_lib_count = 0;

JNIEXPORT jstring JNICALL kcjni_native_run(JNIEnv *env, jclass cls,
    jstring args_json, jstring stdin_json);

/**
 * Validates a kclib identifier used to build paths and symbols.
 * @param name Candidate library name.
 * @return 1 when valid, 0 otherwise.
 */
static int valid_lib_name(const char *name) {
    size_t i;
    size_t len = strlen(name);

    if (len == 0 || len > KCJNI_LIB_NAME_MAX) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_')) {
            return 0;
        }
    }
    return 1;
}

/**
 * Extracts the JSON string value of a top-level key.
 * @param json Input JSON text.
 * @param json_len Length of the input.
 * @param key Top-level key to find.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @return 0 on success, -1 when the key is missing or the input is malformed
 *     or does not fit in out.
 */
static int json_get_string(const char *json, size_t json_len, const char *key,
    char *out, size_t out_size) {
    size_t key_len = strlen(key);
    const char *p = json;
    const char *end = json + json_len;
    char *o = out;

    if (out_size == 0) {
        return -1;
    }
    while (p < end) {
        const char *q = memchr(p, '"', (size_t)(end - p));
        if (q == NULL) {
            return -1;
        }
        if ((size_t)(end - q) >= key_len + 2 &&
            memcmp(q + 1, key, key_len) == 0 && q[1 + key_len] == '"') {
            const char *r = q + 2 + key_len;
            while (r < end && (*r == ' ' || *r == '\t' ||
                *r == '\r' || *r == '\n')) {
                r++;
            }
            if (r >= end || *r != ':') {
                return -1;
            }
            r++;
            while (r < end && (*r == ' ' || *r == '\t' ||
                *r == '\r' || *r == '\n')) {
                r++;
            }
            if (r >= end || *r != '"') {
                return -1;
            }
            r++;
            while (r < end) {
                unsigned char c = (unsigned char)*r;
                if (c == '"') {
                    *o = '\0';
                    return 0;
                }
                if (c == '\\') {
                    r++;
                    if (r >= end) {
                        return -1;
                    }
                    switch (*r) {
                    case '"':
                    case '\\':
                    case '/':
                        c = (unsigned char)*r;
                        break;
                    case 'n':
                        c = '\n';
                        break;
                    case 'r':
                        c = '\r';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case 'b':
                        c = '\b';
                        break;
                    case 'f':
                        c = '\f';
                        break;
                    default:
                        return -1;
                    }
                    r++;
                } else {
                    if (c < 0x20) {
                        return -1;
                    }
                    r++;
                }
                if ((size_t)(o - out) + 1 >= out_size) {
                    return -1;
                }
                *o++ = (char)c;
            }
            return -1;
        }
        p = q + 1;
    }
    return -1;
}

/**
 * Resolves the path of lib<name>.so as a sibling of libjni.so by locating
 * libjni.so itself with dladdr().
 * @param name Validated library name.
 * @param path Output buffer for the resolved path.
 * @param path_size Output buffer size.
 * @return 0 on success, -1 otherwise.
 */
static int resolve_kclib_path(const char *name, char *path, size_t path_size) {
    Dl_info info;
    const char *slash;
    int n;

    if (dladdr((void *)(uintptr_t)kcjni_native_run, &info) == 0 ||
        info.dli_fname == NULL) {
        return -1;
    }
    slash = strrchr(info.dli_fname, '/');
    if (slash == NULL) {
        return -1;
    }
    n = snprintf(path, path_size, "%.*s/lib%s.so",
        (int)(slash - info.dli_fname), info.dli_fname, name);
    if (n < 0 || (size_t)n >= path_size) {
        return -1;
    }
    return 0;
}

/**
 * Returns the dlopen handle for a kclib, loading and caching it on first use.
 * @param name Validated library name.
 * @param err Error buffer.
 * @param err_size Error buffer size.
 * @return The handle on success, NULL on failure with err set.
 */
static void *load_kclib(const char *name, char *err, size_t err_size) {
    char path[KCJNI_PATH_MAX];
    int i;

    for (i = 0; i < g_lib_count; i++) {
        if (strcmp(g_libs[i].name, name) == 0) {
            return g_libs[i].handle;
        }
    }
    if (g_lib_count >= KCJNI_LIBS_MAX) {
        snprintf(err, err_size, "too many kclibs loaded");
        return NULL;
    }
    if (resolve_kclib_path(name, path, sizeof path) != 0) {
        snprintf(err, err_size, "cannot locate libjni.so directory");
        return NULL;
    }
    g_libs[g_lib_count].handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (g_libs[g_lib_count].handle == NULL) {
        snprintf(err, err_size, "dlopen %s: %s", path,
            dlerror() != NULL ? dlerror() : "unknown error");
        return NULL;
    }
    snprintf(g_libs[g_lib_count].name,
        sizeof g_libs[g_lib_count].name, "%s", name);
    g_lib_count++;
    return g_libs[g_lib_count - 1].handle;
}

/**
 * JNI entry point bound to KclibBridge.run(String, String).
 * @param env JNI environment.
 * @param cls The bridge class.
 * @param args_json JSON payload with "lib", "cmd", and "args".
 * @param stdin_json Reserved standard input.
 * @return The kclib output string, or "error: <detail>" on any failure.
 */
JNIEXPORT jstring JNICALL kcjni_native_run(JNIEnv *env, jclass cls,
    jstring args_json, jstring stdin_json) {
    const char *json = NULL;
    char name[KCJNI_LIB_NAME_MAX + 1];
    char err[KCJNI_ERR_MAX];
    char *result = NULL;
    char *lib_err = NULL;
    void *handle = NULL;
    kc_run_fn fn = NULL;
    jstring out = NULL;

    (void)cls;
    (void)stdin_json;

    err[0] = '\0';

    if (args_json == NULL) {
        snprintf(err, sizeof err, "missing argsJson");
        goto done;
    }
    json = (*env)->GetStringUTFChars(env, args_json, NULL);
    if (json == NULL) {
        snprintf(err, sizeof err, "out of memory reading argsJson");
        goto done;
    }
    if (json_get_string(json, strlen(json), "lib", name, sizeof name) != 0) {
        snprintf(err, sizeof err, "missing or malformed \"lib\" field");
        goto done;
    }
    if (!valid_lib_name(name)) {
        snprintf(err, sizeof err, "invalid lib name \"%s\"", name);
        goto done;
    }

    handle = load_kclib(name, err, sizeof err);
    if (handle == NULL) {
        goto done;
    }

    {
        char sym[KCJNI_SYM_MAX];
        snprintf(sym, sizeof sym, "kc_%s_run", name);
        *(void **)(&fn) = dlsym(handle, sym);
        if (fn == NULL) {
            snprintf(err, sizeof err, "kclib %s has no %s entry point",
                name, sym);
            goto done;
        }
    }

    result = fn(json, &lib_err);
    if (result == NULL) {
        snprintf(err, sizeof err, "%s: %s", name,
            lib_err != NULL ? lib_err : "unknown error");
        goto done;
    }

done:
    if (err[0] != '\0') {
        char final[KCJNI_ERR_MAX + 8];
        snprintf(final, sizeof final, "error: %s", err);
        out = (*env)->NewStringUTF(env, final);
    } else {
        out = (*env)->NewStringUTF(env, result);
    }
    if (json != NULL) {
        (*env)->ReleaseStringUTFChars(env, args_json, json);
    }
    free(result);
    free(lib_err);
    return out;
}

/**
 * Registers the native method when libjni.so is loaded.
 * @param vm Java virtual machine.
 * @param reserved Reserved.
 * @return JNI version on success, JNI_ERR otherwise.
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = NULL;
    jclass cls;
    JNINativeMethod methods[] = {
        { "run",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            (void *)&kcjni_native_run },
    };

    (void)reserved;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    cls = (*env)->FindClass(env, KCJNI_BRIDGE_CLASS);
    if (cls == NULL) {
        return JNI_ERR;
    }
    if ((*env)->RegisterNatives(env, cls, methods, 1) != JNI_OK) {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

/**
 * Releases cached kclib handles when the library is unloaded.
 * @param vm Java virtual machine.
 * @param reserved Reserved.
 * @return None.
 */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    int i;

    (void)vm;
    (void)reserved;
    for (i = 0; i < g_lib_count; i++) {
        dlclose(g_libs[i].handle);
        g_libs[i].handle = NULL;
    }
    g_lib_count = 0;
}
