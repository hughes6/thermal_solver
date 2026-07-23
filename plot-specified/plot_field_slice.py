#!/usr/bin/env python3
import argparse
from pathlib import Path
import matplotlib.pyplot as plt
import pandas as pd


def main():
    p=argparse.ArgumentParser(description='Plot a 2D field slice.')
    p.add_argument('--file',default='simulation_output/field.csv'); p.add_argument('--variable',default='T')
    p.add_argument('--axis',choices='xyz',default='z'); p.add_argument('--index',type=int); p.add_argument('--position',type=float)
    p.add_argument('--step',default='latest'); p.add_argument('--state',type=int); p.add_argument('--save'); p.add_argument('--no-show',action='store_true')
    a=p.parse_args(); d=pd.read_csv(Path(a.file)); step=int(d.step.max()) if a.step=='latest' else int(a.step); f=d[d.step==step]
    if a.state is not None: f=f[f.state==a.state]
    idx={'x':'i','y':'j','z':'k'}[a.axis]; axes={'x':('y','z'),'y':('x','z'),'z':('x','y')}[a.axis]
    if a.position is not None:
        vals=f[a.axis].drop_duplicates(); pos=float(vals.iloc[(vals-a.position).abs().argmin()]); f=f[f[a.axis]==pos]; plane=f'{a.axis}={pos:g} m'
    else:
        n=int(f[idx].max()//2) if a.index is None else a.index; f=f[f[idx]==n]; plane=f'{idx}={n}'
    if f.empty: raise ValueError('Selected slice has no cells.')
    fig,ax=plt.subplots(figsize=(9,7)); sc=ax.scatter(f[axes[0]],f[axes[1]],c=f[a.variable],s=80,marker='s')
    fig.colorbar(sc,ax=ax,label=a.variable); ax.set_xlabel(f'{axes[0]} [m]'); ax.set_ylabel(f'{axes[1]} [m]'); ax.set_aspect('equal')
    ax.set_title(f'{a.variable} slice at {plane}\nstep {step}, time {float(f.time.iloc[0]):g} s'); fig.tight_layout()
    if a.save: fig.savefig(a.save,dpi=200,bbox_inches='tight')
    if not a.no_show: plt.show()

if __name__=='__main__': main()
