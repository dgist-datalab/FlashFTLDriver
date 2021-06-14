#!/bin/bash
make TARGET_ALGO=DFTL -j
mv driver ./result/dftl_driver
make clean

make TARGET_ALGO=lsmftl -j
mv driver ./result/lsmftl_driver
make clean

make TARGET_ALGO=Page_ftl -j
mv driver ./result/page_driver
make clean
