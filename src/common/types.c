#include "types.h"

#include <time.h>

bool is_token_expired(const token_t *token) {
    time_t current_time = time(NULL);
    return difftime(current_time, token->expires) >= 0;
}

int count_achievements(const achievement_t *achievements) {
    int                  count   = 0;
    const achievement_t *current = achievements;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}
