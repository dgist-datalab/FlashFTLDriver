#include "./page.h"
#include "./demand_mapping.h"
#include "./gc.h"
#include "../../include/data_struct/list.h"

extern align_buffer a_buffer;
extern algorithm demand_ftl;
extern demand_map_manager dmm;
extern dmi DMI;

extern void page_create_body(lower_info *li, blockmanager *bm, algorithm *algo);
extern void demand_map_create_body(uint32_t total_caching_physical_pages, lower_info *li, blockmanager *bm, bool data_load);

void* dump_end_req(algo_req *req){
	gc_value *gv=(gc_value*)req->param;
	switch(req->type){
		case DUMPR:
			gv->isdone=true;
			break;
		case DUMPW:
			gv->isdone=true;
			break;
	}

	free(req);
	return NULL;
}

static gc_value * send_dump_req(uint32_t ppa, uint8_t type, value_set *value, 
		gc_value* gv){

	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=NULL;
	my_req->end_req=dump_end_req;
	my_req->type=type;

	gc_value *res=NULL;
	switch(type){
		case DUMPR:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->isdone=false;
			res->ppa=ppa;
			my_req->param=(void *)res;
			my_req->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			res->value=my_req->value;
			demand_ftl.li->read(ppa, PAGESIZE, my_req->value,  my_req);
			break;
		case DUMPW:
			gv->isdone=false;
			my_req->value=value;
			my_req->param=(void*)gv;
			demand_ftl.li->write(ppa, PAGESIZE, my_req->value,  my_req);
			break;
	}
	return res;
}

void update_cache_mapping(){
	uint32_t total_mapping_num=(SHOWINGSIZE/LPAGESIZE);
	uint32_t total_translation_page_num=total_mapping_num/(PAGESIZE/sizeof(DMF));

	list *temp_list=list_init();
	gc_value *gv;
	char init_map_data[PAGESIZE];
	memset(init_map_data, -1, PAGESIZE);
	for(uint32_t i=0; i<total_translation_page_num; i++){
		if(dmm.GTD[i].physical_address==UINT32_MAX){
			gv=(gc_value*)calloc(1,sizeof(gc_value));
			gv->ppa=UINT32_MAX;
			gv->value=inf_get_valueset(init_map_data, FS_MALLOC_R, PAGESIZE);
			gv->isdone=true;
		}
		else{
			gv=send_dump_req(dmm.GTD[i].physical_address/L2PGAP, DUMPR, NULL, NULL);
		}
		gv->gtd_idx=i;
		list_insert(temp_list, (void*)gv);
	}

	li_node *now, *nxt;
	list *temp_list2=list_init();
	uint32_t old_ppa, new_ppa;
	gc_value *gv2;
	while(temp_list->size){
		for_each_list_node_safe(temp_list, now, nxt){
			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
/*
			if(gv->gtd_idx==4095){
				uint32_t *ppa_list=(uint32_t*)gv->value->value;
				for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
					printf("prev %u %u\n", i, ppa_list[i]);
				}
				printf("old ppa:%u\n", dmm.GTD[gv->gtd_idx].physical_address);
			}
			*/
			dmm.cache->dump_cache_update(dmm.cache, &dmm.GTD[gv->gtd_idx], gv->value->value);
			if(gv->ppa!=UINT32_MAX){
				old_ppa=dmm.GTD[gv->gtd_idx].physical_address;
				invalidate_map_ppa(old_ppa);
			}
			
			new_ppa=get_map_ppa(gv->gtd_idx, NULL);
			dmm.GTD[gv->gtd_idx].physical_address=new_ppa*L2PGAP;
			/*
			if(gv->gtd_idx==4095){
				uint32_t *ppa_list=(uint32_t*)gv->value->value;
				for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
					printf("%u %u\n", i, ppa_list[i]);
				}
				printf("write to %u\n", new_ppa*L2PGAP);
			}*/

			list_insert(temp_list2, gv);
			send_dump_req(new_ppa, DUMPW, gv->value, gv);
			list_delete_node(temp_list, now);
		}
	}

	while(temp_list2->size){
		for_each_list_node_safe(temp_list2, now, nxt){
			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			list_delete_node(temp_list2, now);
		}
	}

	list_free(temp_list);
	list_free(temp_list2);
}

uint32_t demand_dump(FILE *fp){
	fwrite(&a_buffer.idx, sizeof(uint8_t), 1, fp);
	fwrite(a_buffer.value, sizeof(char), PAGESIZE, fp);
	fwrite(a_buffer.key, sizeof(uint32_t), a_buffer.idx, fp);

	uint32_t temp_seg_idx=UINT32_MAX;

	/*write active and reserve segment*/
	pm_body *p=(pm_body*)demand_ftl.algo_body;
	if(p->active){
		fwrite(&p->active->seg_idx, sizeof(uint32_t),1,fp);
	}
	else{
		fwrite(&temp_seg_idx, sizeof(uint32_t),1,fp);
	}
	if(p->reserve){
		fwrite(&p->reserve->seg_idx, sizeof(uint32_t),1,fp);
	}
	else{
		fwrite(&temp_seg_idx, sizeof(uint32_t),1,fp);
	}

	/*write active and reserve map segment*/
	if(p->map_active){
		fwrite(&p->map_active->seg_idx, sizeof(uint32_t),1,fp);
	}
	else{
		fwrite(&temp_seg_idx, sizeof(uint32_t),1,fp);
	}
	if(p->map_reserve){
		fwrite(&p->map_reserve->seg_idx, sizeof(uint32_t),1,fp);
	}
	else{
		fwrite(&temp_seg_idx, sizeof(uint32_t),1,fp);
	}

	/*write seg_type_checker*/
	fwrite(p->seg_type_checker, sizeof(uint8_t), _NOS, fp);

	/*write GTD*/
	uint32_t total_mapping_num=(SHOWINGSIZE/LPAGESIZE);
	uint32_t total_translation_page_num=total_mapping_num/(PAGESIZE/sizeof(DMF));
	/*
	for(uint32_t i=0; i<total_translation_page_num; i++){
		printf("gtd:%u -> %u\n", i, dmm.GTD[i].physical_address);
	}*/

	printf("dumping data\n");
/*
	gc_value *temp_gv=send_dump_req(dmm.GTD[0].physical_address/L2PGAP, DUMPR, NULL, NULL);
	uint32_t *map_data=(uint32_t*)temp_gv->value->value;
	while(!temp_gv->isdone){}
	for(uint32_t i=0; i<PAGESIZE/sizeof(DMF); i++){
		printf("%u : %u\n", i, map_data[i]);
	}
*/
	fwrite(dmm.GTD, sizeof(GTD_entry), total_translation_page_num, fp);

	fwrite(&DMI, sizeof(dmi), 1, fp);
	return 1;
}

uint32_t demand_load(lower_info *li, blockmanager *bm, struct algorithm *algo, FILE *fp){
	page_create_body(li, bm, algo);
	fread(&a_buffer.idx, sizeof(uint8_t), 1, fp);
	fread(a_buffer.value, sizeof(char), PAGESIZE, fp);
	fread(a_buffer.key, sizeof(uint32_t), a_buffer.idx, fp);
	
	pm_body *p=(pm_body*)calloc(1, sizeof(pm_body)); 
	algo->algo_body=(void*)p;
	uint32_t active, reserve; 
	//for data segment
	fread(&active, sizeof(uint32_t), 1, fp);
	if(active!=UINT32_MAX){
		p->active=bm->pick_segment(bm, active, BLOCK_LOAD);
	}
	else{p->active=NULL;}

	fread(&reserve, sizeof(uint32_t), 1, fp);
	if(reserve!=UINT32_MAX){
		p->reserve=bm->pick_segment(bm, reserve, BLOCK_RESERVE);
	}
	else{p->reserve=NULL;}
	//for mapping segment
	fread(&active, sizeof(uint32_t), 1, fp);
	if(active!=UINT32_MAX){
		p->map_active=bm->pick_segment(bm, active, BLOCK_LOAD);
	}
	else{p->map_active=NULL;}

	fread(&reserve, sizeof(uint32_t), 1, fp);
	if(reserve!=UINT32_MAX){
		p->map_reserve=bm->pick_segment(bm, reserve, BLOCK_RESERVE);
	}
	else{p->map_reserve=NULL;}
	/*read seg_type_checker*/
	fread(p->seg_type_checker, sizeof(uint8_t), _NOS, fp);
	
	demand_map_create_body(UINT32_MAX, li, bm, true);

	uint32_t total_logical_page_num;
	uint32_t total_translation_page_num;
	total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));
	dmm.GTD=(GTD_entry*)calloc(total_translation_page_num,sizeof(GTD_entry));
	
	fread(dmm.GTD, sizeof(GTD_entry), total_translation_page_num, fp);
	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_mutex_init(&dmm.GTD[i].lock);
		dmm.GTD[i].idx=i;
		dmm.GTD[i].pending_req=list_init();
		dmm.GTD[i].private_data=NULL;
		dmm.GTD[i].status=POPULATE;
	}

	printf("loading data\n");

	if(dmm.cache->load_specialized_meta){
		list *temp_list=list_init();
		gc_value *temp_gv;
		for(uint32_t i=0; i<total_translation_page_num; i++){
			temp_gv=send_dump_req(dmm.GTD[i].physical_address/L2PGAP, DUMPR, NULL, NULL);
			temp_gv->gtd_idx=i;
			list_insert(temp_list, (void *)temp_gv);
		}
		li_node *now, *nxt;
		for_each_list_node_safe(temp_list, now, nxt){
			temp_gv=(gc_value*)now->data;
			if(!temp_gv->isdone) continue;
			dmm.cache->load_specialized_meta(dmm.cache, &dmm.GTD[temp_gv->gtd_idx], temp_gv->value->value);

			inf_free_valueset(temp_gv->value,FS_MALLOC_R);
			free(temp_gv);
			list_delete_node(temp_list, now);
		}
		list_free(temp_list);
	}
	/*
	gc_value *temp_gv=send_dump_req(dmm.GTD[0].physical_address/L2PGAP, DUMPR, NULL, NULL);
	uint32_t *map_data=(uint32_t*)temp_gv->value->value;
	while(!temp_gv->isdone){}
	for(uint32_t i=0; i<PAGESIZE/sizeof(DMF); i++){
		printf("%u : %u\n", i, map_data[i]);
	}

	for(uint32_t i=0; i<total_translation_page_num; i++){
		printf("gtd:%u -> %u\n", i, dmm.GTD[i].physical_address);
	}*/
	fread(&DMI, sizeof(dmi), 1, fp);

	DMI.read_working_set=bitmap_init(RANGE);
	DMI.read_working_set_num=0;
	DMI.write_working_set=bitmap_init(RANGE);
	DMI.write_working_set_num=0;
	return 1;
}
