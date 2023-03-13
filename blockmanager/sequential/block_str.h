
typedef struct block_set{
        uint32_t total_invalid_number;
        uint32_t total_valid_number;
        uint32_t used_page_num;
        uint8_t type;
        __block *blocks[BPS];
        void *hptr;
}block_set;
