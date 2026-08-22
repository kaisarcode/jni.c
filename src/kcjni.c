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
#include "parson.h"

#define KCJNI_LIB_NAME_MAX 63
#define KCJNI_PATH_MAX 1024
#define KCJNI_SYM_MAX 96
#define KCJNI_ERR_MAX 1024
#define KCJNI_KCLIBS_MAX 32
#define KCJNI_KCLIB_NAME_LEN 64

#define KCJNI_BRIDGE_CLASS "com/kaisarcode/kclib/KclibBridge"
#define KCJNI_MANIFEST_KEY "com.kaisarcode.kclib.allowed_kclibs"

typedef struct {
    char name[KCJNI_LIB_NAME_MAX + 1];
    void *handle;
} kcjni_lib_t;

static kcjni_lib_t *g_libs = NULL;
static int g_lib_count = 0;
static int g_lib_cap = 0;

static char g_allowed_kclibs[KCJNI_KCLIBS_MAX][KCJNI_KCLIB_NAME_LEN];
static int g_allowed_kclib_count = 0;
static int g_whitelist_loaded = 0;


JNIEXPORT jstring JNICALL kcjni_native_run(JNIEnv *env, jclass cls,
    jstring args_json);

/**
 * Validates a kclib identifier used to build paths and symbols.
 * @param name Candidate library name.
 * @return 1 when valid, 0 otherwise.
 */
static int valid_lib_name(const char *name) {
    size_t i;
    size_t len;

    if (!name || !name[0]) {
        return 0;
    }

    len = strlen(name);
    if (len > KCJNI_LIB_NAME_MAX) {
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
 * Checks if a kclib name is in the whitelist.
 * @param name Validated library name.
 * @return 1 when allowed, 0 otherwise.
 */
static int is_kclib_allowed(const char *name) {
    int i;

    if (g_allowed_kclib_count == 0) {
        return 1;
    }

    for (i = 0; i < g_allowed_kclib_count; i++) {
        if (strcmp(g_allowed_kclibs[i], name) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * Parses a comma-separated whitelist string into the global array.
 * @param raw Comma-separated kclib names.
 * @return None.
 */
static void parse_allowed_kclibs(const char *raw) {
    const char *p;
    const char *start;
    size_t len;

    if (!raw) {
        return;
    }

    g_allowed_kclib_count = 0;
    p = raw;

    while (*p && g_allowed_kclib_count < KCJNI_KCLIBS_MAX) {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') {
            p++;
        }
        len = (size_t)(p - start);
        if (len > 0 && len < KCJNI_KCLIB_NAME_LEN) {
            memcpy(g_allowed_kclibs[g_allowed_kclib_count], start, len);
            g_allowed_kclibs[g_allowed_kclib_count][len] = '\0';
            g_allowed_kclib_count++;
        }
    }

    g_whitelist_loaded = 1;
}

/**
 * Sets the kclib whitelist from a comma-separated string.
 * Must be called before any runner dispatch. When not called, all kclibs are allowed.
 * @param whitelist Comma-separated kclib names, or NULL/empty for all allowed.
 * @return None.
 */
static void set_whitelist(const char *whitelist) {
    if (g_whitelist_loaded) {
        return;
    }
    g_whitelist_loaded = 1;
    if (whitelist && whitelist[0]) {
        parse_allowed_kclibs(whitelist);
    }
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

    if (g_lib_count >= g_lib_cap) {
        int new_cap = g_lib_cap ? g_lib_cap * 2 : 8;
        kcjni_lib_t *new_libs = realloc(g_libs,
            (size_t)new_cap * sizeof(kcjni_lib_t));
        if (!new_libs) {
            snprintf(err, err_size, "kclib cache allocation failed");
            return NULL;
        }
        g_libs = new_libs;
        g_lib_cap = new_cap;
    }

    if (resolve_kclib_path(name, path, sizeof path) != 0) {
        snprintf(err, err_size, "cannot locate libjni.so directory");
        return NULL;
    }

    g_libs[g_lib_count].handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (g_libs[g_lib_count].handle == NULL) {
        const char *derr = dlerror();
        snprintf(err, err_size, "dlopen %s: %s", path,
            derr ? derr : "unknown error");
        return NULL;
    }

    snprintf(g_libs[g_lib_count].name,
        sizeof g_libs[g_lib_count].name, "%s", name);
    g_lib_count++;
    return g_libs[g_lib_count - 1].handle;
}

/**
 * Builds a JSON error object.
 * @param code Error code string.
 * @param message Error message string.
 * @return Newly allocated JSON string or NULL.
 */
static char *kcjni_error_object(const char *code, const char *message) {
    JSON_Value *root;
    char *out;

    root = json_value_init_object();
    if (!root) {
        return NULL;
    }

    json_object_set_string(json_value_get_object(root), "code", code);
    json_object_set_string(json_value_get_object(root), "message", message);

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * Builds a structured bridge response.
 * @param ok Success flag.
 * @param body Serialized result or error object string.
 * @return Newly allocated JSON response string or NULL.
 */
static char *kcjni_wrap_response(int ok, const char *body) {
    JSON_Value *root;
    JSON_Value *body_val;
    char *out;

    if (!body) {
        return NULL;
    }

    root = json_value_init_object();
    if (!root) {
        return NULL;
    }

    json_object_set_boolean(json_value_get_object(root), "ok", ok ? 1 : 0);

    body_val = json_parse_string(body);
    if (body_val) {
        json_object_set_value(json_value_get_object(root),
            ok ? "result" : "error", body_val);
    } else {
        json_object_set_string(json_value_get_object(root),
            ok ? "result" : "error", body);
    }

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * JNI entry point bound to KclibBridge.run(String).
 * @param env JNI environment.
 * @param cls The bridge class.
 * @param args_json JSON payload with "lib", "cmd", and "args".
 * @return Structured JSON response string.
 */

JNIEXPORT void JNICALL kcjni_native_set_whitelist(JNIEnv *env, jclass cls,
    jstring whitelist) {
    const char *raw;
    (void)cls;
    if (whitelist == NULL) {
        set_whitelist(NULL);
        return;
    }
    raw = (*env)->GetStringUTFChars(env, whitelist, NULL);
    if (raw) {
        set_whitelist(raw);
        (*env)->ReleaseStringUTFChars(env, whitelist, raw);
    }
}

JNIEXPORT jstring JNICALL kcjni_native_run(JNIEnv *env, jclass cls,
    jstring args_json) {
    const char *json = NULL;
    JSON_Value *root = NULL;
    JSON_Object *obj = NULL;
    const char *lib_name = NULL;
    char name[KCJNI_LIB_NAME_MAX + 1];
    char err[KCJNI_ERR_MAX];
    char *result = NULL;
    char *lib_err = NULL;
    void *handle = NULL;
    kc_run_fn fn = NULL;
    jstring out = NULL;
    char *response = NULL;
    char *error_obj = NULL;

    (void)cls;

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

    root = json_parse_string(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        snprintf(err, sizeof err, "invalid JSON payload");
        goto done;
    }

    obj = json_value_get_object(root);
    lib_name = json_object_get_string(obj, "lib");
    if (!lib_name || !valid_lib_name(lib_name)) {
        snprintf(err, sizeof err, "missing or invalid \"lib\" field");
        goto done;
    }

    snprintf(name, sizeof name, "%s", lib_name);

    if (!is_kclib_allowed(name)) {
        snprintf(err, sizeof err, "kclib \"%s\" is not allowed", name);
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
        if (strstr(err, "not allowed") != NULL) {
            error_obj = kcjni_error_object("KCLIB_NOT_ALLOWED", err);
        } else if (strstr(err, "missing or invalid") != NULL ||
            strstr(err, "invalid JSON") != NULL ||
            strstr(err, "missing argsJson") != NULL) {
            error_obj = kcjni_error_object("INVALID_ARGUMENT", err);
        } else {
            error_obj = kcjni_error_object("KCLIB_FAILED", err);
        }
        response = error_obj ? kcjni_wrap_response(0, error_obj) : NULL;
    } else {
        response = result ? kcjni_wrap_response(1, result) : NULL;
    }

    if (response) {
        out = (*env)->NewStringUTF(env, response);
    } else {
        out = (*env)->NewStringUTF(env, "{\"ok\":false,\"error\":{\"code\":\"KCLIB_FAILED\",\"message\":\"internal error\"}}");
    }

    if (json != NULL) {
        (*env)->ReleaseStringUTFChars(env, args_json, json);
    }
    if (root) {
        json_value_free(root);
    }
    free(result);
    free(lib_err);
    free(error_obj);
    free(response);
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
            "(Ljava/lang/String;)Ljava/lang/String;",
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

    if ((*env)->RegisterNatives(env, cls, methods, 2) != JNI_OK) {
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

    free(g_libs);
    g_libs = NULL;
    g_lib_count = 0;
    g_lib_cap = 0;
}
