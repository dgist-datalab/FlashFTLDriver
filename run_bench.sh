RSLT_DIR=./eval
#TRACE="VECTOREDSSET"
TRACE="realtpc_opt_1"
#TRACE="synths"
bsize="1 128 256 512 1024 2048 4096 8192 16384" 
#bsize="64"
MAX_QSIZE=16384
#MAX_QSIZE=1
ratio=10
const=1
MAX_TEST=10
#MAX_TEST=1


for trc in $TRACE; do
	
#	qsize=128
	qsize=1
	
#	while [[ $qsize -le $MAX_QSIZE ]]; do
	for iter in $bsize; do
#		echo $qsize
		echo $iter
		cat include/settings_orig.h | sed -e "s/WBUFF_SIZE (128)/WBUFF_SIZE ($iter)/" > include/settings.h
#		cat include/settings_orig.h | sed -e "s/WBUFF_SIZE (128)/WBUFF_SIZE (1)/" > include/settings.h
		make clean 
		make -j
		
		testnum=1	
		#testnum=10
		while [[ $testnum -le $MAX_TEST ]]; do
			rfname=$RSLT_DIR/result_"$trc"_"$iter"_"$ratio"_"$testnum".perf
			./driver > $rfname
				
			testnum=$((testnum + const))
		done

		#qsize=$((qsize + qsize))
	done 
done

'''
cat include/settings_orig.h | sed -e "s/WBUFF_SIZE (128)/WBUFF_SIZE (1)/" > include/settings.h
make clean 
make -j 

rfname=$RSLT_DIR/result_sample.perf
./driver > $rfname 

for trc in $TRACE; do 
	for iter in $bsize; do
		testnum=1
		while[[ $testnum -le $MAX_TEST ]]; do 
			cp $rfname $RSLT_DIR/dawid_"$trc"_"$qsize"_"$ratio"_"$testnum".perf
			
			testnum=$((testnum + const))
		done
	done 
done
#rm -rf $rfname
'''
