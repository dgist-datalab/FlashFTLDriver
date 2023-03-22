#!/bin/bash
mkdir -f ./result
rm ./result/*_driver

make TARGET_ALGO=DFTL cheeze_block_driver -j
mv cheeze_block_driver ./result/cheeze_dftl_driver
make clean

make TARGET_ALGO=Page_ftl cheeze_block_driver -j
mv cheeze_block_driver ./result/cheeze_page_driver
make clean

make TARGET_ALGO=layeredLSM cheeze_block_driver -j
mv cheeze_block_driver ./result/cheeze_lsmftl_driver
make clean

make TARGET_ALGO=leaFTL cheeze_block_driver -j
mv cheeze_block_driver ./result/cheeze_leaftl_driver
make clean

ls -al ./result
