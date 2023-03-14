#pragma once
#include "../../include/settings.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"

uint32_t lea_create(lower_info *, blockmanager *, algorithm *);
void lea_destroy(lower_info *, algorithm *);
uint32_t lea_argument(int argc, char **argv);
uint32_t lea_read(request *const);
uint32_t lea_write(request *const);
uint32_t lea_remove(request *const);