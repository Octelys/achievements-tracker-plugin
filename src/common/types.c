#include "types.h"

#include <time.h>

bool is_token_expired(const token_t *token) {
    time_t current_time = time(NULL);
    return difftime(current_time, token->expires) >= 0;
}
