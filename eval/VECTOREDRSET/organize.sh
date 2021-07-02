#MAX_QSIZE=16384
#qsize=128
MAX_QSIZE=1
qsize=1
ratio=10

while [[ $qsize -le $MAX_QSIZE ]]; do
	rfname=iops_"$qsize".perf
        grep -rn "IOPS" ./"$qsize"/result_VECTOREDRSET_"$qsize"_* | awk '{print $2}' | sort -n > "$rfname" 
	#echo ./"$qsize"/result_VECTOREDRSET_"$qsize"_*
	qsize=$(( qsize + qsize ))
done
