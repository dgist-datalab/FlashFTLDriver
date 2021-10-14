#!/bin/bash

OP=(70 75 80 85)

for i in ${OP[@]}; do
	cd $i
	echo "$i testing"
	cat ../test_set | parallel -j 4 bash -c "{}"
	cd ../
done
