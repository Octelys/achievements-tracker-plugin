#include "uuid.h"

void uuid_get_random(char random_uuid[37])
{
	uuid_t uuid;
	uuid_generate_random(uuid);
	uuid_unparse_lower(uuid, random_uuid);
}
