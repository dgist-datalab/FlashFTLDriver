import os
import sys
import numpy as np
from matplotlib import pyplot as plt


data_path = sys.argv[1]
f = open(data_path, "r")

data = f.readline()

loop_num = 0
flag = 0
while data:
    if loop_num % 15 == 0:
        print("\n",32*(flag+1), "GB result")
        flag += 1
    split_data = data.split(" ")
    group_num = (split_data[1].split("\n"))[0]
    group_id = int(split_data[0])
    #print(group_id,":", group_num)
    print(group_num, end=" ")
    data = f.readline()
    loop_num += 1
print("\n")
