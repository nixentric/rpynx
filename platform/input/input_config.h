#ifndef RSL_INPUT_CONFIG_H
#define RSL_INPUT_CONFIG_H

#include <stddef.h>

#include "input.h"

/* malloc'd, pretty-printed JSON. Caller frees. */
char *rsl_config_to_json(const rsl_input_config *cfg);

/* Strict parse: unknown actions/buttons, wrong types, newer versions and
 * configs missing an essential action are rejected (err says why). Actions
 * absent from the file keep their default binding. */
bool rsl_config_from_json(const char *text, rsl_input_config *out, char *err, size_t errlen);

typedef enum { RSL_LOAD_OK, RSL_LOAD_BACKUP, RSL_LOAD_DEFAULTS } rsl_load_result;

/* Load path, falling back to path.bak, then to defaults. err holds the
 * reason the primary file was not used (empty if it was). */
rsl_load_result rsl_config_load(const char *path, rsl_input_config *out, char *err, size_t errlen);

/* Refuses incomplete or conflicting configs; writes atomically. */
bool rsl_config_save(const char *path, const rsl_input_config *cfg, char *err, size_t errlen);

#endif
