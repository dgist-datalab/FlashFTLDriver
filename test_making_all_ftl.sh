#!/bin/bash

OP=(70 75 80 85)
for i in ${OP[@]}; do
	mkdir ./OP_TESTING/$i/
	
	make USER_DEF="-DOP=$i" TARGET_ALGO=DFTL ./cheeze_trace_block_driver -j
	mv cheeze_trace_block_driver ./OP_TESTING/$i/dftl_driver
	make clean
	
	make USER_DEF="-DOP=$i" TARGET_ALGO=Page_ftl ./cheeze_trace_block_driver -j
	mv cheeze_trace_block_driver ./OP_TESTING/$i/page_driver
	make clean
done
