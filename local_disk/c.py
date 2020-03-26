import torch
import numpy as np

#a = [[1,2,3],[4,5,6]]
#b = torch.zeros([3,2], dtype=torch.float32)
#print(b)
#for i, x in enumerate(a):
#	for j, y in enumerate(x):
#		k = y+0.1
#		print(k)
#		b[j][i] = k
#print(a)
#print(b.shape)
##c = torch.tensor(b)
##print(c)
#
#twod_list = []
#for i in range (0, 10):
#    new = []
#    for j in range (0, 10):
#        new.append(0)
#    twod_list.append(new)
#
#print(twod_list)

a = torch.zeros([2,2,4], dtype=torch.float32)
aaa = [1,2,3,4]
#aaa = torch.zeros([2], dtype=torch.float32)
#aaa[0] = 1
#aaa[1] = 2
a[0][0][:] = torch.FloatTensor(aaa)
print(a)
