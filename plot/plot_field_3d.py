#!/usr/bin/env python3
import argparse
from pathlib import Path
import matplotlib.pyplot as plt
import pandas as pd


def args():
    p=argparse.ArgumentParser(description='Plot one 3D field snapshot.')
    p.add_argument('--file',default='simulation_output/field.csv')
    p.add_argument('--variable',default='T')
    p.add_argument('--step',default='latest')
    p.add_argument('--time',type=float)
    p.add_argument('--state',type=int)
    p.add_argument('--marker-size',type=float,default=35)
    p.add_argument('--alpha',type=float,default=.85)
    p.add_argument('--save')
    p.add_argument('--no-show',action='store_true')
    return p.parse_args()


def equal_axes(ax,d):
    cols=['x','y','z']; centers=[(d[c].min()+d[c].max())/2 for c in cols]
    radius=max(d[c].max()-d[c].min() for c in cols)/2 or .5
    ax.set_xlim(centers[0]-radius,centers[0]+radius)
    ax.set_ylim(centers[1]-radius,centers[1]+radius)
    ax.set_zlim(centers[2]-radius,centers[2]+radius)


def main():
    a=args(); path=Path(a.file)
    if not path.exists(): raise FileNotFoundError(path)
    d=pd.read_csv(path)
    required={'step','time','x','y','z',a.variable}
    missing=required-set(d.columns)
    if missing: raise ValueError(f'Missing columns: {sorted(missing)}')
    if a.time is not None:
        times=d['time'].drop_duplicates(); t=float(times.iloc[(times-a.time).abs().argmin()]); frame=d[d.time==t]
    else:
        step=int(d.step.max()) if a.step=='latest' else int(a.step); frame=d[d.step==step]
    if frame.empty: raise ValueError('Selected snapshot has no rows.')
    if a.state is not None:
        if 'state' not in frame: raise ValueError('field.csv has no state column.')
        frame=frame[frame.state==a.state]
    frame=frame.dropna(subset=['x','y','z',a.variable])
    step=int(frame.step.iloc[0]); t=float(frame.time.iloc[0])
    fig=plt.figure(figsize=(10,8)); ax=fig.add_subplot(111,projection='3d')
    sc=ax.scatter(frame.x,frame.y,frame.z,c=frame[a.variable],s=a.marker_size,alpha=a.alpha)
    fig.colorbar(sc,ax=ax,pad=.1,label=a.variable)
    ax.set(xlabel='x [m]',ylabel='y [m]',zlabel='z [m]',title=f'{a.variable} — step {step}, time {t:g} s')
    equal_axes(ax,frame); fig.tight_layout()
    if a.save: fig.savefig(a.save,dpi=200,bbox_inches='tight')
    if not a.no_show: plt.show()

if __name__=='__main__': main()
