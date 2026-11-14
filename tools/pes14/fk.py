import numpy as np
def qmul(a,b):
    x1,y1,z1,w1=a; x2,y2,z2,w2=b
    return np.array([w1*x2+x1*w2+y1*z2-z1*y2, w1*y2-x1*z2+y1*w2+z1*x2, w1*z2+x1*y2-y1*x2+z1*w2, w1*w2-x1*x2-y1*y2-z1*z2])
def qrot(q,v):
    u=np.array(q[:3]); w=q[3]
    return v+2*np.cross(u,np.cross(u,v)+w*v)
def fk(parents,local):  # local: (nb,10) t,q,s
    n=len(parents); W=[None]*n
    for i in range(n):
        t=local[i,:3]; q=local[i,3:7]
        if parents[i]<0: W[i]=(t.copy(),q.copy())
        else:
            pt,pq=W[parents[i]]
            W[i]=(pt+qrot(pq,t), qmul(pq,q))
    return W
