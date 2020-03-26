from socket import *
import numpy as np
import struct
import torch
import example as KV_Cache
 
LEN = 256
class SocketInfo():
    HOST=""
    PORT=9001
    BUFSIZE=4*LEN
    ADDR=(HOST, PORT)
 
class socketInfo(SocketInfo):
    HOST= "10.150.21.55"

PORT=9001
HOST="10.150.21.56"
 
csock= socket(AF_INET, SOCK_STREAM)
csock.connect((HOST,PORT))
print("conenct is success")

#data1, data2 = struct.unpack('f', commend[:4], commend[4:])
#print(data1)
#data= commend.decode("utf-8")
 
#print("type : {}, data len : {}, data : {}, Contents : {}".format(type(commend),len(commend), commend, data))


#----------------- send data -------------------
# | batch_size	- table_num 
#[batch, tablenum]
Cache = []
for i in range(26) :
	Cache.append(KV_Cache.LRUCache(5))

#res = torch.zeros([3,4,LEN], dtype=torch.float32)
result = []
a = [[1,43,4,1],[9999,2323,9999,232],[9999,2323,9999,232]]
for i, x in enumerate(a):
	res = torch.zeros([4,LEN], dtype=torch.float32)

	for j, y in enumerate(x):

		key = str(i)+"_"+str(y)	
		find_val = []

		if Cache[i].exist(key) :
			print("hit")
			find_val = Cache[i].get(key)	
		
		else :
			print("miss")
			req = [i,y]
			to_server = np.array(req).tobytes()
			csock.send(to_server)
		
			
			recv_val = csock.recv(socketInfo.BUFSIZE, MSG_WAITALL)
			start = 0
			end = 4
			while True:
				if end > 4*LEN :
					break
				find_val += list(struct.unpack('f', recv_val[start:end]))	
				start +=4
				end +=4

#			recv_decoded = struct.unpack('f', recv_val)
#			find_val = recv_decoded[0]
		#	print(find_val)
			Cache[i].put(key, find_val)

		res[j][:] = torch.FloatTensor(find_val)
#		res[i][j][:] = torch.FloatTensor(find_val)
	result.append(res)
print(result)
print(result[0])
print(result[0][0])

#res = res.tolist()
#print(result)

#b = [25,a[0][0]]
#to_server = np.array(b).tobytes()
#print(b)
#right_method= to_server.to_bytes(4, byteorder='little')
#print("Send Data : {}, bytes len : {}, bytes : {}".format(b,len(to_server), to_server))
#sent= csock.send(to_server)

#to_server= np.array(a[1]).tobytes()
#right_method= to_server.to_bytes(4, byteorder='little')
#print("Send Data : {}, bytes len : {}, bytes : {}".format(a[1],len(to_server), to_server))

#--------------recevie data-------------------
#req = [0,1]
#to_server = np.array(req).tobytes()
#csock.send(to_server)
#
#commend= csock.recv(socketInfo.BUFSIZE, MSG_WAITALL)
#i=0
#j=4
#tmp_list = []
#while True:
#	if j > 16 :
#		break
#	tmp_list += list(struct.unpack('f', commend[i:j]))	
#	i +=4
#	j +=4
	
#tmp_tuple = struct.unpack('f', commend)
#tmp_list = list(tmp_tuple)
#print(tmp_list)

#commend= csock.recv(socketInfo.BUFSIZE, MSG_WAITALL)
#tmp_tuple = struct.unpack('f', commend[:4])
#tmp_list = list(tmp_tuple)
#tmp_tuple = struct.unpack('f', commend[4:])
#tmp_list += list(tmp_tuple)



#to_server= int(12345)
#right_method= to_server.to_bytes(4, byteorder='little')
#print("Send Data : {}, bytes len : {}, bytes : {}".format(to_server,len(right_method), right_method))
#sent= csock.send(right_method)
 
csock.close()
exit()

