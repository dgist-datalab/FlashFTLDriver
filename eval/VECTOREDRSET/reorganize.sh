#MAX_QSIZE=16384
#qsize=1

QSIZE="1 128 256 512 1024 2048 4096 8192 16384"

for qsize in $QSIZE; do 
	head -n 5 iops_"$qsize".perf | tail -n 1
done
