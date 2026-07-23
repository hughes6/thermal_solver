#!/usr/bin/env python3
import argparse, glob
from pathlib import Path
import matplotlib.pyplot as plt
import pandas as pd

EXCLUDE={'step','time','requested_x','requested_y','requested_z','i','j','k'}

def numeric_columns(d,requested):
    if requested:
        missing=[c for c in requested if c not in d]
        if missing: raise ValueError(f'Missing columns: {missing}')
        return requested
    return [c for c in d.select_dtypes('number').columns if c not in EXCLUDE]

def main():
    p=argparse.ArgumentParser(description='Plot summary or probe histories.')
    p.add_argument('--summary'); p.add_argument('--probe',nargs='*'); p.add_argument('--columns',nargs='*'); p.add_argument('--save'); p.add_argument('--no-show',action='store_true')
    a=p.parse_args()
    if bool(a.summary)==bool(a.probe): 
        a.summary = "simulation_output/summary.csv"
        print("no probes provided: plotting summary")
    fig,ax=plt.subplots(figsize=(10,6))
    if a.summary:
        d=pd.read_csv(a.summary)
        for c in numeric_columns(d,a.columns): ax.plot(d.time,d[c],label=c)
        ax.set_title('Simulation summary history')
    else:
        paths=[]
        for pattern in a.probe:
            matches=glob.glob(pattern); paths.extend(matches or [pattern])
        for path in paths:
            d=pd.read_csv(path); name=Path(path).stem.removeprefix('probe_')
            for c in numeric_columns(d,a.columns): ax.plot(d.time,d[c],label=f'{name}: {c}')
        ax.set_title('Probe history')
    ax.set_xlabel('Time [s]'); ax.set_ylabel('Value'); ax.grid(True); ax.legend(); fig.tight_layout()
    if a.save: fig.savefig(a.save,dpi=200,bbox_inches='tight')
    if not a.no_show: plt.show()

if __name__=='__main__': main()
