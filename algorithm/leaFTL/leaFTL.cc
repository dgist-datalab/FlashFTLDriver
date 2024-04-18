#include "leaFTL.h"
#include "./write_buffer.h"
#include "./page_manager.h"
#include "./lea_container.h"
#include "./issue_io.h"
#include "../../include/debug_utils.h"
#include "group.h"
#include "../../include/data_struct/lru_list.h"
#include <vector>

#ifdef DEBUG
uint32_t *exact_map;
#endif

lea_write_buffer *wb;
page_manager *pm;
blockmanager *lea_bm;
lower_info *lower;
temp_map *user_temp_map;
temp_map *gc_temp_map;
temp_map *gc_translate_map;
temp_map *compaction_temp_map;

page_manager *translate_pm;
LRU *gp_lru;
int64_t lru_max_byte;
int64_t lru_now_byte;
int64_t lru_reserve_byte;
uint32_t max_cached_trans_map;
uint32_t now_segment_num;
page_read_buffer rb;
uint32_t read_buffer_hit_cnt;
extern group_monitor gm;

uint32_t lea_mapping_update_cnt;
uint32_t find_exact_piece_ppa_cnt;

typedef struct group_update_param{
   group_read_param grp;
   temp_map map;
   group *gp;
}group_update_param;

group_update_param gup_set[TRANSMAPNUM];
group main_gp[TRANSMAPNUM];

extern uint32_t test_key;
extern uint32_t lea_test_piece_ppa;
#define WRITE_BUF_SIZE (1*1024*1024)
#define INITIAL_STATE_PADDR (UINT32_MAX)
uint32_t lea_print_log();

struct algorithm lea_FTL={
    .argument_set=lea_argument,
    .create=lea_create,
    .destroy=lea_destroy,
    .read=lea_read,
    .write=lea_write,
    .flush=NULL,
    .remove=lea_remove,
    .test=NULL,
    .print_log=lea_print_log,
    .empty_cache=NULL,
    .dump_prepare=NULL,
    .dump=NULL,
    .load=NULL,
};

typedef struct user_io_param{
    uint32_t type;
    uint32_t piece_ppa;
    value_set *value;
}user_io_param;


uint32_t lea_print_log(){
    printf("===========LEA LOG===============\n");
    printf("target cache_memory:%u/%u\n", lru_now_byte, lru_max_byte);
    printf("segment_num: %u, average:%.2lf\n", now_segment_num, (double)now_segment_num/TRANSMAPNUM);

	uint64_t total_index_size=0;
	for(uint32_t i=0; i<TRANSMAPNUM; i++){
		total_index_size+=main_gp[i].size;
	}
	printf("total index size:%lu, %lf\n", total_index_size, (double)total_index_size/(RANGE*32));
    return 1;
}

user_io_param* get_user_io_param(uint32_t type, uint32_t piece_ppa, value_set *value){
    user_io_param *res=(user_io_param*)malloc(sizeof(user_io_param));
    res->type=type;
    res->piece_ppa=piece_ppa;
    res->value=value;
    return res;
}

uint32_t lea_create(lower_info *li, blockmanager *bm, algorithm *algo){
    algo->li=li;
    algo->bm=bm;
    lower=li;
    lea_bm=bm;

    exact_map=(uint32_t*)malloc(sizeof(uint32_t) * _NOP * L2PGAP);
    memset(exact_map, -1, sizeof(uint32_t)*_NOP*L2PGAP);

    wb=lea_write_buffer_init(WRITE_BUF_SIZE);
    uint32_t sector_num=WRITE_BUF_SIZE/L2PGAP;
    user_temp_map=temp_map_assign(sector_num);
    temp_map_clear(user_temp_map);

    sector_num=_PPS*L2PGAP;
    gc_temp_map=temp_map_assign(sector_num);
    temp_map_clear(gc_temp_map);
    gc_translate_map=temp_map_assign(sector_num);
    temp_map_clear(gc_translate_map);

    sector_num=MAPINTRANS;
    compaction_temp_map=temp_map_assign(sector_num);

    pm=pm_init(li, bm, true);
    translate_pm=pm_init(li, bm, false);
    lru_init(&gp_lru, NULL, NULL);
    //max_cached_trans_map=TRANSMAPNUM*30/100;
    printf("cache size:%lf\n", (double)max_cached_trans_map/TRANSMAPNUM);
    lru_max_byte=max_cached_trans_map*PAGESIZE;
    lru_now_byte=0;
    lru_reserve_byte=0;

    printf("fast search: %s\n", gm.fast_search?"on":"off");
    if(gm.fast_search){
        gm.seg_list=std::vector<segment*>(RANGE, NULL);
    }
    else{
        
    }

    for(uint32_t i=0; i<TRANSMAPNUM; i++){
        group_init(&main_gp[i], i);
    }

    rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;


    return 1;
}

void lea_destroy(lower_info *, algorithm *){
	lea_print_log();
    lea_write_buffer_free(wb);
    pm_free(pm);
    pm_free(translate_pm);
    free(exact_map);
    temp_map_free(user_temp_map);
    temp_map_free(gc_temp_map);
    temp_map_free(gc_translate_map);
    temp_map_free(compaction_temp_map);
    lru_free(gp_lru);

    for(int i=0; i<TRANSMAPNUM; i++){
        group_free(&main_gp[i]);
    }

    group_clen_temporal_storage();
	delete rb.pending_req;
	delete rb.issue_req;
}

inline uint32_t xx_to_byte(char *a){
	switch(a[0]){
		case 'K':
			return 1024;
		case 'M':
			return 1024*1024;
		case 'G':
			return 1024*1024*1024;
		default:
			break;
	}
	return 1;
}
uint32_t lea_argument(int argc, char **argv){
    bool cache_size=false;
	bool cache_type_set=false;
	uint32_t len;
	int c;
	char temp;
	uint32_t gran=1;
	uint64_t base;
	uint32_t physical_page_num;
	double cache_percentage;
    gm.fast_search=false;
	while((c=getopt(argc,argv,"cpf"))!=-1){
		switch(c){
			case 'c':
				cache_size=true;
				len=strlen(argv[optind]);
				temp=argv[optind][len-1];
				if(temp < '0' || temp >'9'){
					argv[optind][len-1]=0;
					gran=xx_to_byte(&temp);
				}
				base=atoi(argv[optind]);
				physical_page_num=base*gran/PAGESIZE;
				break;
            case 'f':
                gm.fast_search=true;
                break;
			case 'p':
				cache_size=true;
				cache_percentage=atof(argv[optind])/100;
				physical_page_num=(SHOWINGSIZE/K) * cache_percentage;
				physical_page_num/=PAGESIZE;
				break;
			default:
				printf("not invalid parameter!!\n");
				abort();
				break;
		}
	}

    if(!cache_size){
        max_cached_trans_map=TRANSMAPNUM*30/100;
    }
    else{
        max_cached_trans_map=physical_page_num;
    }

    return 1;
}

void processing_pending_req(algo_req *req, value_set *value){
    group_read_param *grp=(group_read_param*)req->param;
    grp->oob=(uint32_t*)lea_bm->get_oob(lea_bm, grp->piece_ppa/L2PGAP);
    bool data_found=false;
    bool isretry=false;
    bool data_retry=false;

    switch (grp->retry_flag){
    case RETRY_FLAG::NORMAL_RETRY:
    case RETRY_FLAG::NOT_RETRY:
        grp->set_idx = group_oob_check(grp);
        if (grp->set_idx == NOT_FOUND){
            isretry=true;
            data_retry=true;
        }
        else{
            data_found = true;
        }
        break;
    case RETRY_FLAG::DATA_FOUND:
        data_found = true;
        break;
    default:
        printf("not allowed type! %s:%u\n",__FUNCTION__, __LINE__);
        GDB_MAKE_BREAKPOINT;
        break;
    }
    grp->read_done=true;
    if (data_found){
        memmove(grp->value->value, &value->value[(grp->set_idx) * LPAGESIZE], LPAGESIZE);
        req->parents->end_req(req->parents);
        free(grp);
    }
    if(isretry){
        if(data_retry){
            req->parents->type_ftl+=10;
        }
        inf_assign_try(req->parents);
    }
    free(req);
}

void *lea_read_end_req(algo_req* const req){
    group_read_param *grp=(group_read_param*)req->param;
	algo_req *pending_req;
    if(req->type==MAPPINGR){
        grp->read_done=true;
        inf_assign_try(req->parents);
        free(req);
    }
    else{   
	    rb_r_iter target_r_iter;
        fdriver_lock(&rb.pending_lock);
        target_r_iter = rb.pending_req->find(grp->piece_ppa/ L2PGAP);
        for (; target_r_iter->first == grp->piece_ppa / L2PGAP &&
               target_r_iter != rb.pending_req->end();){
            pending_req = target_r_iter->second;
            pending_req->type_lower = req->type_lower;
            processing_pending_req(pending_req, grp->value);
            rb.pending_req->erase(target_r_iter++);
        }
        rb.issue_req->erase(grp->piece_ppa / L2PGAP);
        fdriver_unlock(&rb.pending_lock);

        fdriver_lock(&rb.read_buffer_lock);
        rb.buffer_ppa = grp->piece_ppa / L2PGAP;
        memcpy(rb.buffer_value, grp->value->value, PAGESIZE);
        fdriver_unlock(&rb.read_buffer_lock);

        processing_pending_req(req, grp->value);
    }

    return NULL;
}

uint32_t lea_read(request *const req){
    group_read_param *grp;
    group *gp;
    char *data;
    if(req->key==test_key){
        //GDB_MAKE_BREAKPOINT;
    }
    //printf("req->key:%u\n", req->key);

    /*first, check write buffer*/
    if(req->param==NULL && (data=lea_write_buffer_get(wb, req->key))){
        memcpy(req->value->value, data, LPAGESIZE);
        req->end_req(req);
        return 1;
    }

    if(req->param==NULL){
        /*cache miss check*/
        gp=&main_gp[req->key/MAPINTRANS];
        if(gp->cache_flag!=CACHE_FLAG::FLYING){
            if (lea_cache_evict(gp) == false){
                goto retry;
            }
        }

        grp=group_get_empty_grp();
        grp->lba=req->key;
        grp->user_req=req;
        grp->value=req->value;
        req->param=(void*)grp;

        if(group_get_map_read_grp(gp, true, grp, false, req->value, lea_cache_insert)){
            //issue IO
            req->type_ftl++;

            send_IO_user_req(MAPPINGR, lower, gp->ppa, req->value, (void*) grp, req, lea_read_end_req);
            goto end;
        }
        else if(grp->gp->cache_flag==CACHE_FLAG::FLYING){
            req->type_ftl++;
            goto end;
        }
    }
    else{
        grp = (group_read_param *)req->param;
        gp=grp->gp;
    }


    if (grp->r_type == GRP_READ_TYPE::MAPREAD){
        bool need_issue_for_map = group_get_map_read_grp(gp, false, grp, false, req->value, lea_cache_insert);
        if (gp->cache_flag == CACHE_FLAG::UNCACHED || need_issue_for_map){
            printf("not allowed type...may be evicted nother requests\n");
            GDB_MAKE_BREAKPOINT;
        }
        else if(grp->gp->cache_flag==CACHE_FLAG::FLYING){
            printf("this cannot be, since the header request is processed already!\n");
            GDB_MAKE_BREAKPOINT;
        }
    }

    if (grp->r_type != GRP_READ_TYPE::DATAREAD && grp->r_type!=GRP_READ_TYPE::NOTDATAREADSTART){
        printf("not allowed type for user read\n");
        GDB_MAKE_BREAKPOINT;
    }

    /*cache hit*/
    lea_cache_promote(gp);
    group_get(grp->gp, grp->lba, grp, true, GRP_TYPE::GP_READ);

    if(grp->piece_ppa!=INITIAL_STATE_PADDR){
        algo_req *hit_check=send_IO_user_req(DATAR, lower, grp->piece_ppa / L2PGAP, req->value, (void *)grp, req, lea_read_end_req);
        if(hit_check){
            //buffer hit
            processing_pending_req(hit_check, req->value);
        }
    }
    else{
        //not found data
        req->type = FS_NOTFOUND_T;
        free(grp);
        req->end_req(req);
    }

end:
    return 1;
retry:
    inf_assign_try(req);
    return 1;
}

#define ASSIGN_FLUSH_MAP(_TMAP, _SIZE, _LBA, _PIECEPPA)\
    do{\
        (_TMAP)->size=(_SIZE);\
        (_TMAP)->lba=(_LBA);\
        (_TMAP)->piece_ppa=(_PIECEPPA);\
    }while(0)

static inline void wrapper_group_insert(group *gp, uint32_t size, uint32_t *lba, uint32_t *piece_ppa, bool isacc, int32_t interval){
    temp_map flush_map;
    ASSIGN_FLUSH_MAP(&flush_map, size, lba, piece_ppa);
    if(size <=2){
        group_insert(gp, &flush_map, SEGMENT_TYPE::ACCURATE, size==1?0:lba[1]-lba[0], lea_cache_size_update);
    }
    else{
        if(isacc){
            if(interval==-1){
                printf("error %s:%u, interval must be available!\n", __FUNCTION__, __LINE__);
                abort();
            }
            group_insert(gp, &flush_map, SEGMENT_TYPE::ACCURATE, interval, lea_cache_size_update);
        }
        else{
            group_insert(gp, &flush_map, SEGMENT_TYPE::APPROXIMATE, -1, lea_cache_size_update);
        }
    }
}

void lea_making_segment_per_gp(group *gp, uint32_t *lba, uint32_t *piece_ppa, int32_t *interval, uint32_t size){
#ifdef DEBUG
    //cutting mappint into segment set
    for (uint32_t k = 0; k < size; k++){
        exact_map[lba[k]]=piece_ppa[k];
    }
#endif
    if(size==1){
        wrapper_group_insert(gp, size, lba, piece_ppa, false, 0);
        return;
    }
    uint32_t app_start_idx=UINT32_MAX;
    uint32_t app_length=0;

    uint32_t acc_start_idx=UINT32_MAX;
    uint32_t acc_length;
    uint32_t acc_interval;
    for(uint32_t i=1; i<size; i++){
        if(acc_start_idx==UINT32_MAX){
            acc_start_idx=i-1;
            acc_length=2;
            acc_interval=interval[i-1];
        }
        else{
            if(acc_interval==interval[i-1]){
                acc_length++;
                continue;
            }
            else{
                if(acc_length < 3){
                    if(app_start_idx==UINT32_MAX){
                        app_start_idx=acc_start_idx;
                    }
                    app_length+=acc_length;
                }
                else{
                    //flush previous acc & app segment 
                    if(app_start_idx!=UINT32_MAX){
                        wrapper_group_insert(gp, app_length, &lba[app_start_idx], &piece_ppa[app_start_idx], false, UINT32_MAX);
                        app_start_idx=UINT32_MAX;
                        app_length=0;
                    }
                    //clear information
                    wrapper_group_insert(gp, acc_length, &lba[acc_start_idx], &piece_ppa[acc_start_idx], true, acc_interval);
                }

                acc_start_idx=i;
                acc_length=1;
                acc_interval=UINT32_MAX;
            }
        }
    }

    if(acc_length < 3){
        if(app_start_idx==UINT32_MAX){
            app_start_idx=acc_start_idx;
        }
        app_length+=acc_length;
        acc_start_idx=UINT32_MAX;
    }

    if(app_start_idx!=UINT32_MAX){
        wrapper_group_insert(gp, app_length, &lba[app_start_idx], &piece_ppa[app_start_idx], false, UINT32_MAX);
    }

    if(acc_start_idx!=UINT32_MAX){
        wrapper_group_insert(gp, acc_length, &lba[acc_start_idx], &piece_ppa[acc_start_idx], true, acc_interval);
    }
}

void lea_mapping_find_exact_piece_ppa(temp_map *map, bool invalidate, blockmanager *bm, bool evict_path){
    group_read_param *grp=(group_read_param*)malloc(sizeof(group_read_param)*map->size);
    std::list<group_read_param*> cached_grp_list;
    std::list<group_read_param*> uncached_grp_list;
    
    //static int debug_cnt=0;
    //debug_cnt++;
    //bool debug_flag=false;
    find_exact_piece_ppa_cnt++;

    for(uint32_t i=0; i<map->size; i++){
        group *gp=&main_gp[map->lba[i]/MAPINTRANS];
        grp[i].gp=gp;
        grp[i].lba=map->lba[i];

        if(evict_path && !group_cached(gp)){
            printf("wtf??\n");
            GDB_MAKE_BREAKPOINT;
        }

        if(evict_path==false && group_cached(gp)){
            cached_grp_list.push_back(&grp[i]);
            lea_cache_promote(gp);
        }
        else{
            uncached_grp_list.push_back(&grp[i]);
        }

    }

    /*do round*/
    for(uint32_t i=0; i<2; i++){
        std::list<group_read_param*> &target_grp_list=i==0?cached_grp_list:uncached_grp_list;
        group_read_param *t_grp;
        std::list<group_read_param*>::iterator master_iter=target_grp_list.begin();
        while(master_iter!=target_grp_list.end()){
            std::list<group_read_param*> grp_list;
            for(;master_iter!=target_grp_list.end(); master_iter++){
                t_grp=*master_iter;

                if(lea_cache_evict(t_grp->gp)==false){
                    break;
                }
                group_get_exact_piece_ppa(t_grp->gp, t_grp->lba,t_grp->gp->map_idx, t_grp, true, lower, lea_cache_insert);
                grp_list.push_back(t_grp);
            }

            while(grp_list.size()){
                std::list<group_read_param *>::iterator list_iter = grp_list.begin();
                for (; list_iter != grp_list.end();){
                    group_read_param *t_grp=*list_iter;
                    if(t_grp->read_done==false){
                        list_iter++;
                        continue;
                    }

                    if(t_grp->r_type==DATAREAD && t_grp->retry_flag==RETRY_FLAG::DATA_FOUND){
                        goto found_end;
                    }
                    group_get_exact_piece_ppa(t_grp->gp, t_grp->lba, t_grp->set_idx, t_grp, false, lower, lea_cache_insert);

                    if(t_grp->r_type!=NOTDATAREADSTART && t_grp->r_type!=DATAREAD){
                        printf("not allowed type! %s:%u\n", __FUNCTION__, __LINE__);
                        abort();
                    }

                    if(t_grp->retry_flag==RETRY_FLAG::DATA_FOUND){
                    found_end:
                        #ifdef DEBUG
                        if(t_grp->piece_ppa!=exact_map[t_grp->lba]){
                            printf("address error find_ppa:%u lea_mapping_update:%u!\n", find_exact_piece_ppa_cnt, lea_mapping_find_exact_piece_ppa);
                            GDB_MAKE_BREAKPOINT;
                        }
                        #endif

                        if(invalidate && t_grp->piece_ppa!=INITIAL_STATE_PADDR){
                            invalidate_piece_ppa(bm, t_grp->piece_ppa, 1, false);
                        }
                        else if(invalidate==false){
                            map->piece_ppa[t_grp->lba%MAPINTRANS]=t_grp->piece_ppa;
                        }
                        grp_list.erase(list_iter++);
                    }
                    else{
                        list_iter++;
                    }
                }
            }
        }
    }
    free(grp);
}

uint32_t *lea_gp_to_mapping(group *gp){
#ifdef FAST_LOAD_STORE
    return &exact_map[gp->map_idx*MAPINTRANS];
#else
    for(uint32_t i=0; i<MAPINTRANS; i++){
        compaction_temp_map->lba[i]=gp->map_idx*MAPINTRANS+i;
    }
    lea_mapping_find_exact_piece_ppa(compaction_temp_map, false, NULL, true);
    group_from_translation_map(gp, compaction_temp_map->lba, compaction_temp_map->piece_ppa, gp->map_idx);
    return compaction_temp_map->piece_ppa;
#endif
}

void lea_compaction(){
    int64_t new_lru_now_byte=0;
	printf("compaction start!\n");
    for(uint32_t idx=0; idx<TRANSMAPNUM ;idx++){
        group *gp=&main_gp[idx];
        if(gp->cache_flag==CACHE_FLAG::UNCACHED){
            continue;
        }
        if(gp->level_list==NULL || gp->level_list->size()<=1){
            continue;
        }
	
		//printf("\tcompaction:%u!\n",idx);
        for(uint32_t i=0; i<MAPINTRANS; i++){
            compaction_temp_map->lba[i]=gp->map_idx*MAPINTRANS+i;
        }
        lea_mapping_find_exact_piece_ppa(compaction_temp_map, false, NULL, false);

        group_from_translation_map(gp, compaction_temp_map->lba, compaction_temp_map->piece_ppa, gp->map_idx);
        new_lru_now_byte+=gp->size;
    }
    lru_now_byte=new_lru_now_byte;
    lea_cache_evict_force();
	printf("compaction end!\n");
}

void *lea_update_end_req(algo_req *al_req){
    group_update_param *gup=(group_update_param*)al_req->param;
    gup->grp.read_done=true;
    free(al_req);
    return NULL;
}

group_update_param * lea_gup_setup(group_update_param *t_gup, group *gp, temp_map *map, uint32_t start_ptr, uint32_t num){
    t_gup->gp = gp;
    t_gup->map.lba = &map->lba[start_ptr];
    t_gup->map.piece_ppa = &map->piece_ppa[start_ptr];
    t_gup->map.interval= &map->interval[start_ptr];
    t_gup->map.size=num;
    t_gup->grp.value=NULL;
    return t_gup;
}

void lea_mapping_update(temp_map *map, blockmanager *bm, bool isgc){
    /*udpate mapping*/
    //figure out old mapping and invalidation itnnn

    static uint32_t cnt=0;
    if(++cnt%1==0){
        //printf("%s log: %u\n", __FUNCTION__, cnt);
    }

    lea_mapping_update_cnt++;
    if(map->size==0){
        printf("sorted buffer size is 0\n");
        abort();
    }

    /*do round*/
    uint32_t gup_idx=0;
    uint32_t start_ptr=UINT32_MAX;
    uint32_t transpage_idx=UINT32_MAX;
    uint32_t now_transpage_idx;
    group_update_param *t_gup;
    std::list<group_update_param*> cached_gup_list;
    std::list<group_update_param*> uncached_gup_list;
    /*divide map to gup*/
    for(uint32_t i=0; i<map->size; i++){
        if(start_ptr==UINT32_MAX){
            start_ptr=i;
            transpage_idx=map->lba[i]/MAPINTRANS;
        }
        now_transpage_idx=map->lba[i]/MAPINTRANS;

        if(now_transpage_idx!=transpage_idx){
            t_gup=lea_gup_setup(&gup_set[gup_idx++], &main_gp[transpage_idx], map, start_ptr, i-start_ptr);
            if(group_cached(t_gup->gp)){
                lea_cache_promote(t_gup->gp);
                cached_gup_list.push_back(t_gup);
            }
            else{
                uncached_gup_list.push_back(t_gup);
            }
            start_ptr=i;
            transpage_idx=now_transpage_idx;
        }
    }

    t_gup = lea_gup_setup(&gup_set[gup_idx++], &main_gp[transpage_idx], map, start_ptr, map->size - start_ptr);
    if (group_cached(t_gup->gp)){
        lea_cache_promote(t_gup->gp);
        cached_gup_list.push_back(t_gup);
    }
    else{
        uncached_gup_list.push_back(t_gup);
    }

    for(uint32_t i=0; i<2; i++){
        std::list<group_update_param*> &target_gup_list=i==0?cached_gup_list:uncached_gup_list;
        std::list<group_update_param*>::iterator master_iter=target_gup_list.begin();
        while(master_iter!=target_gup_list.end()){
            std::list<group_update_param *> gup_list;
            for(; master_iter!=target_gup_list.end(); master_iter++){
                t_gup=*master_iter;

                if(lea_cache_evict(t_gup->gp)==false){
                    break;
                }
                if(group_get_map_read_grp(t_gup->gp, true, &t_gup->grp, true, false, lea_cache_insert)){
                    t_gup->grp.value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);            
                    send_IO_back_req(MAPPINGR, lower, t_gup->gp->ppa, t_gup->grp.value, (void*)t_gup, lea_update_end_req);
                }
                gup_list.push_back(t_gup);
            }
    
            std::list<group_update_param*>::iterator gup_iter;
            while(gup_list.size()){
                for(gup_iter=gup_list.begin(); gup_iter!=gup_list.end();){
                    t_gup=*gup_iter;
                    if(t_gup->grp.read_done==false){
                        gup_iter++; //wait until read
                        continue;
                    }

                    if(group_get_map_read_grp(t_gup->gp, false, &t_gup->grp, true, false, lea_cache_insert)){
                        printf("mapping read must be done in this step %s:%u\n", __FUNCTION__, __LINE__);
                        GDB_MAKE_BREAKPOINT;
                    }

                    if(t_gup->grp.value){
                        inf_free_valueset(t_gup->grp.value, FS_MALLOC_R);
                    }

                    if(!isgc){
                        lea_mapping_find_exact_piece_ppa(&t_gup->map, true, bm, false);
                    }
                    lea_making_segment_per_gp(t_gup->gp, t_gup->map.lba, t_gup->map.piece_ppa, t_gup->map.interval, t_gup->map.size);

                    gup_list.erase(gup_iter++);
                }
            }
        }
    }

    lea_cache_evict_force();
    /*temp_map clear*/
    temp_map_clear(map);
}

bool write_buffer_check_ignore(uint32_t lba){
    return wb->L2P_map.find(lba)!=wb->L2P_map.end();
}

uint32_t lea_write(request *const req){
    lea_write_buffer_insert(wb, req->key, req->value->value);

    static int write_cnt=0;
    write_cnt++;
    if(write_cnt% COMPACTION_RESOLUTION ==0){
        lea_compaction();
        //group_monitor_print();
    }
    if(lea_write_buffer_isfull(wb)){
        uint32_t remain_space;
retry:
        remain_space=pm_remain_space(pm, true);

        if(lea_write_buffer_flush(wb, pm, user_temp_map, remain_space) == false){
            if(user_temp_map->size){
                lea_mapping_update(user_temp_map, pm->bm, false);
            }

            if(pm_assign_new_seg(pm, true) == false){
                /*gc_needed*/
                pm_gc(pm, gc_temp_map, true);
                if(gc_temp_map->size){
                    lea_mapping_update(gc_temp_map, pm->bm, true);
                }
            }
            goto retry;
        }
        else{
            lea_mapping_update(user_temp_map, pm->bm, false);
        }
        lea_write_buffer_clear(wb);
    }

    #ifdef WRITE_STOP_READ
    //    req->end_req(req);
    
        if(write_cnt%L2PGAP==0){
            send_IO_temp_write(lower, req);
        }
        else{
            req->end_req(req);
        }
    
    #else
        req->end_req(req);
    #endif
    if(wb->ps_ptr+1==wb->max_ps_ptr){
        return UINT32_MAX;
    }
    else{
        return 1;
    }
}

uint32_t lea_remove(request *const req){
    req->end_req(req);
    return 1;
}
