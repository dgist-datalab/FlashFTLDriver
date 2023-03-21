#pragma once
#include "../../include/container.h"
algo_req *send_IO_back_req(uint32_t type, lower_info *li, uint32_t ppa, value_set *value, void *parameter, void *(*end_req)(algo_req * const));
algo_req *send_IO_user_req(uint32_t type, lower_info *li, uint32_t ppa, value_set *value, void *parameter, request *req, void *(*end_req)(algo_req * const));
