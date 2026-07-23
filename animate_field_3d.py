#!/usr/bin/env python3
import argparse
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import pandas as pd


def args():
    p=argparse.ArgumentParser(description='Animate a 3D field.')
    p.add_argument('--file',default='simulation_output/field.csv')
    p.add_argument('--variable',default='T')
    p.add_argument('--skip',type=int,default=1)
    p.add_argument('--fps',type=int,default=15)
    p.add_argument('--state',type=int)
    p.add_argument('--marker-size',type=float,default=35)
    p.add_argument('--alpha',type=float,default=.85)
    p.add_argument('--fixed-scale',action='store_true')
    p.add_argument('--save')
    p.add_argument('--no-show',action='store_true')
    return p.parse_args()


def main():
    a=args(); d=pd.read_csv(Path(a.file))
    required={'step','time','x','y','z',a.variable}; missing=required-set(d.columns)
    if missing: raise ValueError(f'Missing columns: {sorted(missing)}')
    if a.state is not None:
        if 'state' not in d: raise ValueError('field.csv has no state column.')
        d=d[d.state==a.state]
    steps=sorted(d.step.unique())[::a.skip]
    if not steps: raise ValueError('No animation frames.')
    first=d[d.step==steps[0]]
    vmin=float(d[a.variable].min()); vmax=float(d[a.variable].max()); vmax=vmax if vmax!=vmin else vmin+1
    fig=plt.figure(figsize=(10,8)); ax=fig.add_subplot(111,projection='3d')
    sc=ax.scatter(first.x,first.y,first.z,c=first[a.variable],s=a.marker_size,alpha=a.alpha,
                  vmin=vmin if a.fixed_scale else None,vmax=vmax if a.fixed_scale else None)
    cb=fig.colorbar(sc,ax=ax,pad=.1); cb.set_label(a.variable)
    ax.set_xlabel('x [m]'); ax.set_ylabel('y [m]'); ax.set_zlabel('z [m]')
    for c,setter in [('x',ax.set_xlim),('y',ax.set_ylim),('z',ax.set_zlim)]: setter(d[c].min(),d[c].max())
    def update(n):
        f=d[d.step==steps[n]]; sc._offsets3d=(f.x.to_numpy(),f.y.to_numpy(),f.z.to_numpy()); sc.set_array(f[a.variable].to_numpy())
        if not a.fixed_scale:
            lo=float(f[a.variable].min()); hi=float(f[a.variable].max()); sc.set_clim(lo,hi if hi!=lo else lo+1); cb.update_normal(sc)
        ax.set_title(f'{a.variable} — step {int(steps[n])}, time {float(f.time.iloc[0]):g} s')
        return (sc,)
    anim=FuncAnimation(fig,update,frames=len(steps),interval=1000/a.fps,blit=False)
    fig.tight_layout()
    if a.save:
        writer='pillow' if a.save.lower().endswith('.gif') else 'ffmpeg'
        anim.save(a.save,writer=writer,fps=a.fps)
    if not a.no_show: plt.show()

if __name__=='__main__': main()
