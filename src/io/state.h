#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void io_load(void);

const char *state_get_device_uuid(void);

const char *get_xid(void);
const char *get_xsts_token(void);

void state_clear_tokens(void);
void state_set_tokens(const char *, const char *);

#ifdef __cplusplus
}
#endif
