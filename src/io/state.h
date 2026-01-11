#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void io_load(void);

const char* state_get_device_uuid(void);

const char *get_xid(void);
const char *get_xsts_token(void);

void clear_xid_xsts_token(void);
void set_xid_xsts_token(const char *, const char *);

#ifdef __cplusplus
}
#endif
