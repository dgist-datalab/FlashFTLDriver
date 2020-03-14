#include<stdio.h>
#include<stdlib.h>
#include<limits.h>
#include<stdint.h>

#define for_each_header_start(idx,key,ppa_ptr,bitmap,body)\
	for(idx=0; bitmap[idx]!=UINT16_MAX && idx<2048; idx++){\
		ppa_ptr=(uint32_t*)&body[bitmap[idx]];\
		key.key=(char*)&body[bitmap[idx]+sizeof(uint32_t)];\
		key.len=bitmap[idx+1]-bitmap[idx]-sizeof(uint32_t);\

#define for_each_header_end }

typedef struct KEYT{
	char *key;
	uint16_t len;
}KEYT;

int main(){
	uint32_t idx;
	uint32_t *ppa_ptr;
	KEYT key;
	char body[8192]={0,};
	char *bitmap=&body[0];

	for(int j=0; bitmap[j]!=UINT16_MAX && j<2048; j++){
		
	}


	for_each_header_start(idx,key,ppa_ptr,bitmap,body)
		printf("[%u]%s:%d\n",idx,key.key,key.len);
	for_each_header_end

}
