#!/bin/bash
mkdir -f ./result
rm ./result/*_driver

make clean

make TARGET_ALGO=DFTL driver -j
mv driver ./result/dftl_driver
make clean

make TARGET_ALGO=Page_ftl driver -j
mv driver ./result/page_driver
make clean

make TARGET_ALGO=layeredLSM driver -j
mv driver ./result/lsmftl_driver
make clean

make TARGET_ALGO=leaFTL driver -j
mv driver ./result/leaftl_driver
make clean

ls -al ./result
