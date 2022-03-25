import os
import sys
import numpy as np
from matplotlib import pyplot as plt


data_path = sys.argv[1]
arg = int(sys.argv[2])

f1 = open("rand_"+data_path, "r")
f2 = open("08_"+data_path, "r")
f3 = open("11_"+data_path, "r")

def WAF_analysis(f):
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
    return progress_list, WAF_list
#print("\n")



p1, w1 = WAF_analysis(f1)
p2, w2 = WAF_analysis(f2)
p3, w3 = WAF_analysis(f3)



fig = plt.figure()
a = plt.plot(p1, w1, c='b', label="random")
plt.scatter(p1, w1, c='b')
b = plt.plot(p2, w2, c='g', label="zipfian 0.8")
plt.scatter(p2, w2, c='g')
c = plt.plot(p3, w3, c='r', label="zipfian 1.1")
plt.scatter(p3, w3, c='r')
plt.legend()
plt.xlabel('Request Size (GB)')
plt.ylabel('WAF')
#plt.legend([a, b, c], ["random", "zipfian 0.8", "zipfian 1.1"])
plt.savefig("all_WAF.png")
plt.show()
