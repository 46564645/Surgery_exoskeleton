import gzip,json,numpy as np,trimesh,matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pathlib import Path
P=Path(__file__).resolve().parents[1]
s=json.load(gzip.open(P/'metadata/fusion_snapshot.json.gz','rt'))
fig,axs=plt.subplots(1,2,figsize=(12,6))
for ax,name,y in zip(axs,['03_CUFF_A','08_A'],[-15.134,9.5]):
 p=next(p for p in s['parts'] if p['name']==name)
 v=np.array(p['vertices_cm']).reshape(-1,3);T=np.array(p['transform_cm']).reshape(4,4);v=v@T[:3,:3].T+T[:3,3]
 m=trimesh.Trimesh(vertices=v,faces=np.array(p['triangles']).reshape(-1,3),process=False)
 seg=trimesh.intersections.mesh_plane(m,[0,1,0],[0,y,0])
 for line in seg:ax.plot(line[:,0]*10,line[:,2]*10,color='black',lw=.6)
 ax.set_title(name);ax.set_xlabel('Fusion X (mm)');ax.set_ylabel('Fusion Z (mm)');ax.set_aspect('equal');ax.grid()
fig.tight_layout();fig.savefig(P/'docs/cuff_sections.png',dpi=150)
