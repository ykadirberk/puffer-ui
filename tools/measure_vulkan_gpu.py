"""Bounded per-process Windows GPU counters at matched 60 Hz native pacing.

Native VSync is requested, while a matched harness cap also bounds unfocused
windows. Focus/visibility, all samples, invalid statuses and full LUID/physical
engine instances are retained. This does not certify foreground presentation.
"""
from pathlib import Path
import argparse, csv, ctypes as c, datetime, hashlib, io, json, math, os
import re, statistics, subprocess, time


def counter_key(sample):
    # Never average distinct adapters or physical engines into one percentage.
    return sample['counter'], re.sub(r'^pid_\d+_', '', sample['instance'])


def summarize(samples):
    valid = [s for s in samples if s['status'] in [0, 1] and s['value'] is not None]
    result = {}
    for name in sorted({counter_key(s) for s in valid}):
        values = [s['value'] for s in valid if counter_key(s) == name]
        result['/'.join(name)] = dict(mean=statistics.mean(values), max=max(values), samples=len(values))
    return result


def workloads(integrated=False):
    if integrated:
        return {'idle': ['--idle'], 'moving': [], 'local': ['--local-change']}
    return {'idle-sdf': ['--idle', '--sdf', '--vector-text'],
            'idle-cache': ['--idle', '--compute', '--vector-text', '--cache'],
            'animated-sdf': ['--sdf', '--vector-text'],
            'animated-cache': ['--compute', '--vector-text', '--cache']}


def painter_flags(side, integrated=False, before_cache=False):
    if not integrated:
        return []
    if side == 'before':
        return (['--compute', '--cache'] if before_cache else ['--sdf']) + ['--vector-text', '--no-program-cache']
    return ['--integrated']

class ValueUnion(c.Union):
    _fields_=[('long',c.c_int32),('double',c.c_double),('large',c.c_int64),('string',c.c_wchar_p)]
class Value(c.Structure):
    _fields_=[('status',c.c_uint32),('value',ValueUnion)]
class Item(c.Structure):
    _fields_=[('name',c.c_wchar_p),('formatted',Value)]

class Query:
    def __init__(self):
        self.dll=c.WinDLL('pdh.dll')
        self.open=self.dll.PdhOpenQueryW
        self.open.argtypes=[c.c_wchar_p,c.c_size_t,c.POINTER(c.c_void_p)]
        self.add=self.dll.PdhAddEnglishCounterW
        self.add.argtypes=[c.c_void_p,c.c_wchar_p,c.c_size_t,c.POINTER(c.c_void_p)]
        self.collect=self.dll.PdhCollectQueryData
        self.collect.argtypes=[c.c_void_p]
        self.array=self.dll.PdhGetFormattedCounterArrayW
        self.array.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32),c.POINTER(c.c_uint32),c.c_void_p]
        self.close=self.dll.PdhCloseQuery
        self.close.argtypes=[c.c_void_p]
        for f in [self.open,self.add,self.collect,self.array,self.close]: f.restype=c.c_uint32
        self.handle=c.c_void_p()
        status=self.open(None,0,c.byref(self.handle))
        if status: raise OSError(f'PdhOpenQueryW {status:08x}')
        self.counters={}
        for name,path in {
            'engine':r'\GPU Engine(*)\Utilization Percentage',
            'dedicated':r'\GPU Process Memory(*)\Dedicated Usage',
            'shared':r'\GPU Process Memory(*)\Shared Usage'}.items():
            handle=c.c_void_p()
            status=self.add(self.handle,path,0,c.byref(handle))
            if status: raise OSError(f'PdhAddEnglishCounterW {path} {status:08x}')
            self.counters[name]=handle
    def sample(self,pid):
        result=[]
        status=self.collect(self.handle)
        if status: return [{'counter':'query','instance':'','status':status,'value':None}]
        needle=f'pid_{pid}_'
        for name,handle in self.counters.items():
            size=c.c_uint32(); count=c.c_uint32()
            status=self.array(handle,0x200,c.byref(size),c.byref(count),None)
            if status not in [0,0x800007D2]:
                result.append(dict(counter=name,instance='',status=status,value=None))
                continue
            data=c.create_string_buffer(size.value)
            status=self.array(handle,0x200,c.byref(size),c.byref(count),data)
            if status:
                result.append(dict(counter=name,instance='',status=status,value=None))
                continue
            items=c.cast(data,c.POINTER(Item))
            for i in range(count.value):
                item=items[i]
                if needle not in item.name: continue
                value=item.formatted.value.double
                result.append(dict(counter=name,instance=item.name,status=item.formatted.status,
                                   value=value if math.isfinite(value) else None))
        return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before',type=Path); parser.add_argument('after',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--frames',type=int,default=180)
    parser.add_argument('--runs',type=int,default=2)
    parser.add_argument('--integrated',action='store_true',
                        help='compare always-drawn native SDF against the integrated production policy')
    parser.add_argument('--before-cache',action='store_true',
                        help='use the old forced compute/cache path as the integrated control')
    args=parser.parse_args()
    if os.name!='nt': parser.error('Windows PDH counters required')
    if args.frames < 180 or args.frames > 3600 or args.runs < 1 or args.runs > 6:
        parser.error('bounded protocol requires 180..3600 frames and 1..6 repetitions')
    if args.before_cache and not args.integrated:
        parser.error('--before-cache requires --integrated')
    args.output.mkdir(exist_ok=False,parents=True)
    binaries={side:p.resolve() for side,p in [('before',args.before),('after',args.after)]}
    modes=workloads(args.integrated)
    receipts=[]
    for repeat in range(args.runs):
        for mode,flags in modes.items():
            for side in (['before','after'] if repeat%2==0 else ['after','before']):
                stem=args.output/f'{mode}-{side}-{repeat}'
                query=Query(); samples=[]
                command=[str(binaries[side]),*painter_flags(side,args.integrated,args.before_cache),*flags,
                         '--visible','--vsync','--hz','60','--frames',str(args.frames)]
                start=time.perf_counter()
                with stem.with_suffix('.csv').open('w') as output,stem.with_suffix('.log').open('w') as error:
                    process=subprocess.Popen(command,stdout=output,stderr=error,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
                    try:
                        query.sample(process.pid)
                        while process.poll() is None:
                            if time.perf_counter()-start>180:
                                process.kill(); raise TimeoutError('bounded native probe timed out')
                            time.sleep(0.5)
                            elapsed=time.perf_counter()-start
                            for sample in query.sample(process.pid):
                                samples.append(dict(elapsed=elapsed,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                                    pid=process.pid,**sample))
                    finally: query.close(query.handle)
                    code=process.wait()
                stem.with_suffix('.pdh.json').write_text(json.dumps(samples,indent=2))
                if code: raise RuntimeError(f'native probe failed {stem} code={code}')
                rows=list(csv.DictReader(stem.with_suffix('.csv').read_text().splitlines()))
                if len(rows)!=args.frames: raise ValueError('incomplete native frame stream')
                valid=[s for s in samples if s['status'] in [0,1] and s['value'] is not None]
                if not any(s['counter']=='engine' for s in valid): raise ValueError('no own-process engine counters')
                summary=summarize(samples)
                receipt=dict(mode=mode,side=side,repeat=repeat,pid=process.pid,command=command,
                             elapsed=time.perf_counter()-start,frames=len(rows),summary=summary,
                             focused_frames=sum(int(row['focused']) for row in rows),
                             visible_frames=sum(int(row['visible']) for row in rows),
                             program_hits=sum(int(row['program_hits']) for row in rows))
                receipts.append(receipt)
                (args.output/'receipt.json').write_text(json.dumps(dict(
                    protocol='per-PID PDH; visible native WSI; VSync requested; harness cap 60 Hz; all raw counters and frames retained; adapters/engines separate',
                    binaries={k:dict(path=str(v),sha256=hashlib.sha256(v.read_bytes()).hexdigest()) for k,v in binaries.items()},
                    runs=receipts),indent=2))
                print(mode,side,repeat,'focus',receipt['focused_frames'],'/',len(rows),'hits',receipt['program_hits'],summary,flush=True)

if __name__=='__main__': main()
