#set term png small color
set size 0.28, 0.25
set output 'VECTOREDSSET_IOPS.png' 
set key right top 

#set grid
#set ylab "IOPS" x,y
#set xlab "BUF_SIZE" x,y

plot "res.txt" using 1:2 title "VECTOREDSSET" with linespoints
