RSLT_DIR=./eval
TRACE="VECTOREDSSET"

#MAX_QSIZE=16384
MAX_QSIZE=1
ratio=10
const=1
MAX_TEST=10
#MAX_TEST=1

for trc in $TRACE; do
	
#	qsize=128
	qsize=1
	
	while [[ $qsize -le $MAX_QSIZE ]]; do
		echo $qsize
		cat include/settings_orig.h | sed -e "s/WBUFF_SIZE (128)/WBUFF_SIZE ($qsize)/" > include/settings.h
		make clean 
		make -j
		
		testnum=1		
		while [[ $testnum -le $MAX_TEST ]]; do
			rfname=$RSLT_DIR/result_"$trc"_"$qsize"_"$ratio"_"$testnum".perf
			./driver > $rfname
				
			testnum=$((testnum + const))
		done

		qsize=$((qsize + qsize))
	done 
done
