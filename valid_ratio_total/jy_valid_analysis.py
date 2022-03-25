import os
import sys
import numpy as np
from matplotlib import pyplot as plt

valid_path = sys.argv[1]
group_num = int(sys.argv[2])
fr = open(valid_path, "r")

valid_rate_list = []
group_info_list = []
group_rate_sum = np.array([0. for i in range(group_num)])
group_trim_num = np.array([0. for i in range(group_num)])
x_val = [[] for i in range(group_num)]
y_val = [[] for i in range(group_num)]
TRIM_number = 0
trim = fr.readline()
while trim:
    trim = trim.split(" ")
    #print(trim)
    valid_rate = (trim[1].split("\n"))[0]
    valid_rate = float(valid_rate)
    #print(valid_rate)
    group = int(trim[0])
    #print(trim)
    x_val[group-1].append(TRIM_number+1)
    y_val[group-1].append(valid_rate)
    group_trim_num[group-1] += 1
    group_rate_sum[group-1] += valid_rate
    valid_rate_list.append(valid_rate)
    group_info_list.append(group)
    trim = fr.readline()
    TRIM_number += 1
    if TRIM_number % 50000 == 0:
        print("TRIM_number:", TRIM_number)

group_rate_avg = group_rate_sum/group_trim_num
#print(group_rate_sum)
#print(group_rate_sum.sum(), TRIM_number)
total_rate_avg = group_rate_sum.sum()/TRIM_number

x_tmp = int(TRIM_number/10)
for j in range(10):
    if j==9:
        x_total = [i+1 for i in range(x_tmp*j, TRIM_number)]
    else: 
        x_total = [i+1 for i in range(x_tmp*j, x_tmp*(j+1))]
    color_map = ['black','green', 'gray', 'purple', 'cyan', 'saddlebrown', 'red', 'blue', 'silver', 'darkgreen', 'coral', 'olive', 'pink', 'rosybrown','moccasin', 'springgreen', 'skyblue', 'tomato', 'slategray', 'indigo', 'teal']
    fig = plt.figure()
    if j==9:
        plt.scatter(x_total,valid_rate_list[x_tmp*j: TRIM_NUMBER], marker="o", s=0.3, color='black')
    else:
        plt.scatter(x_total, valid_rate_list[x_tmp*j: x_tmp*(j+1)], marker="o", s=0.3, color='black')
    plt.ylim([0.1, 0.9])
    plt.axhline(total_rate_avg,color='r')
    plt.savefig("bps/"+valid_path+"_total_"+str(j)+".png", format='png', dpi=1000)
    plt.show()
    fig.clear()
print("png 1 complete!")
"""
for i in range(group_num):
    x_val = []
    y_val = []
    for j in range(TRIM_number):
        if valid_rate_list
"""
"""
for i in range(TRIM_number):
    plt.scatter(i+1, valid_rate_list[i], marker="o", s=0.3, color=color_map[group_info_list[i]-1], label="group "+str(group_info_list[i]-1))
    print(i)
"""
tmp = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
ex_tmp = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

for j in range(10):
    for i in range(group_num):
        while (1):
            if j==9: break
            if (x_val[tmp[i]] > x_tmp*(j+1)):
                break
            else:
                tmp[i] += 1
        if (j == 0): 
            plt.scatter(x_val[i][0:tmp[i]], y_val[i][0:tmp[i]], marker="o", s=0.1, color=color_map[i], label="group "+str(i+1))
        elif (j == 9): 
            plt.scatter(x_val[i][ex_tmp[i]:], y_val[i][ex_tmp[i]:], marker="o", s=0.1, color=color_map[i], label="group "+str(i+1))
        else: 
            plt.scatter(x_val[i][ex_tmp[i]:tmp[i]], y_val[i][ex_tmp[i]:tmp[i]], marker='o', color=color_map[i], label="group "+str(i+1))
        ex_tmp[i] = tmp[i]

        #print(i)
    plt.ylim([0.1, 0.9])
    plt.legend(loc='upper right')
    plt.savefig("bps/"+valid_path+"_group_"+str(j)+".png")
    plt.show()
    print("png 2 complete!\n\n")


print("===========================================")
print("              MiDA FTL Result")
print("===========================================")

print("workload:", valid_path)
print("group num:", group_num)
for i in range(group_num):
    print("group "+str(i+1)+" TRIM number:", group_trim_num[i])
    print("group "+str(i+1)+" rate avg: ", str(round(group_rate_avg[i],3))+"%")
print(" ")
print("total rate avg:", str(round(total_rate_avg,3))+"%")
print("total TRIM number:", TRIM_number)

print("\n===========================================")
    
