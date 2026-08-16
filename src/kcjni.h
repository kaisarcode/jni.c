/**
 * kcjni.h - kclib native-run contract
 * Summary: Public contract a kclib exports for the libjni JNI bridge.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_JNI_H
#define KC_JNI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * kc_run_fn - kclib command entry point signature.
 *
 * A kclib that wants to be callable from Android exports exactly
 * `char *kc_<name>_run(const char *payload_json, char **out_err)`, where
 * <name> is the library identifier carried by the "lib" field of the payload
 * and by the shared library file name (lib<name>.so).
 *
 * payload_json is the exact JSON string passed to KclibBridge.run(). It has
 * the shape `{"lib":"<name>","cmd":"<command>","args":{...},"handle":0}`.
 * The kclib ignores the "lib" field and dispatches on "cmd" using "args".
 * @param payload_json Exact JSON payload with "lib", "cmd", "args", and "handle".
 * @param out_err On failure set to a malloc'd diagnostic message.
 * @return On success a malloc'd UTF-8 output string with *out_err set to NULL;
 *     NULL with *out_err set to a malloc'd diagnostic on failure. The caller
 *     owns and frees both strings.
 */
typedef char *(*kc_run_fn)(const char *payload_json, char **out_err);

#ifdef __cplusplus
}
#endif

#endif
