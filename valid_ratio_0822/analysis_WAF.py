import os
import sys
import numpy as np
from matplotlib import pyplot as plt


data_path = sys.argv[1]
arg = int(sys.argv[2])
f = open(data_path, "r")

data = f.readline()
WAF_list = []
progress_list = []
loop_num = 1
flag = 0
while data:
    #if loop_num % 15 == 0:
    #    print(32*(flag+1), "GB WAF result")
    #    flag += 1
    split_data = data.split(" ")
    WAF = float((split_data[0].split("\n"))[0])
    #group_id = int(split_data[0])
    #print(group_id,":", group_num)
    print(WAF)
    data = f.readline()
    if loop_num % arg == 0:
        progress_list.append(32*(loop_num))
        WAF_list.append(WAF)
    loop_num += 1
print(loop_num)
#print("\n")


print(WAF_list)
print(progress_list)
fig = plt.figure()
plt.plot(progress_list, WAF_list)
plt.scatter(progress_list,WAF_list)
plt.show()
plt.savefig(data_path+".png")
