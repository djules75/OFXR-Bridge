from pathlib import Path
import importlib.util
import tempfile


spec = importlib.util.spec_from_file_location('exporter', Path(__file__).parents[1] / 'scripts/export-benchmark-row.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

assert m.decode((1<<24)|(64<<25)|(1<<45),0)['suite_step']==64
def event(op, ms, a, b, c, result=0):
    return f'seq=1 ms={ms} tid=1 phase=I op={op} result={result} dur_us=0 a={a} b={b} c={c}\n'
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'fixture.benchmark.log'
    rows = [event('logger', 0, 0, 0, 0, 529)]
    for index, key in enumerate(m.MEGANEX):
        parts = key.split(':'); route = parts[0]; flags = parts[1] if len(parts)>1 else '00'
        backend = {'Reference':1, 'New':2, 'Djules':3, 'Djules3X':3}.get(route, 0)
        gen = {'FSR':1, 'DLSS-G':2, 'Hybrid':3}.get(route, 0)
        bits = int(route!='Disabled') | backend<<1 | gen<<3 | int(flags[0])<<5 | int(flags[1])<<6 | 1<<15
        if route=='Hybrid': bits |= (1<<7)|(1<<8)
        if route=='Djules3X': bits |= 1<<10
        if route=='DLSS-G-A': bits = (bits & ~(3<<3)) | (2<<3) | (1<<17)
        if route=='DLSS-G-C': bits = (bits & ~(3<<3)) | (2<<3) | (3<<17)
        if key.endswith(':prefer'): bits |= 1<<9
        start=index*25000; revision=index+1
        rows.append(event('benchmark_configuration', start, revision, bits, 100<<16))
        for point in range(81):
            ms=start+point*250
            fps=40 if route=='Disabled' else 80
            rows.append(event('benchmark_rates', ms, (fps*1000<<32)|40000, (40000<<32)|90000, revision, 2049))
            rows.append(event('benchmark_cycles', ms, (7000<<32)|55000, 44000, revision, 7))
    path.write_text(''.join(rows))
    result=m.export([path], 'meganex', 'estimated')
    assert len(result)==1 and not result[0]['missing']
    values=result[0]['tsv'].split('\t')
    assert len(values)==40 and values[0]=='40' and values[-2:]==['','529']
    assert len(result[0]['headers'])==40 and result[0]['headers'][-2:]==['Log','Tested Build']
    assert result[0]['headers'][16]=='Djules3X:01'
    assert all(x=='80(44)' for x in values[1:-2])
    length=m.export([path], 'meganex', 'length')[0]
    assert length['cells']['FSR:10']['value']=='80(55)'
    assert m.decode((1<<15)|(1<<14), 0)['nr']
    # Unrecorded configurations and short samples cannot become a complete row.
    partial=Path(directory)/'partial.benchmark.log'
    partial.write_text(''.join(rows[:30]))
    assert not m.export([partial], 'meganex', 'estimated')
    assert m.decode((3<<3)|1|(1<<7), 0)['route']=='Hybrid'
    assert m.decode((2<<3)|1|(1<<10), 0)['route']=='DLSS-G3X'
    assert m.decode((2<<3)|1, 0)['cell']=='DLSS-G-unverified:00'
    assert result[0]['cells']['DLSS-G-A:00']['config']['dlssg_preset_request']==1
    assert result[0]['cells']['DLSS-G-C:00']['config']['dlssg_preset_request']==3
    pimax=m.export([path], 'pimax', 'estimated')
    assert len(pimax)==3 and all(r['start_column']=='BI' and len(r['tsv'].split('\t'))==10 for r in pimax)
    assert sorted(r['dlssg_preset_request'] for r in pimax)==[0,1,3]
    assert sum(len(r['measurements']) for r in pimax)==38  # compact cells cannot discard portable A/B evidence
    # Menu-transition outliers belong to neither measured end of an interval.
    edge=dict(signature=(1,0,0),config=m.decode(1,0),start=0,last=25000,gaps=0,file='edges',version=530,
              rates=[dict(ms=i*250,output=999 if i*250<3000 or i*250>22000 else 80,real=40,generated=40,hz=90,source=8) for i in range(101)],
              cycles=[dict(ms=i*250,estimated=999 if i*250<3000 or i*250>22000 else 44,length=55,ofxr=7) for i in range(101)])
    checked=m.summarize(edge,3,10,'estimated')
    assert checked['value']=='80(44)' and checked['start_s']==3 and checked['end_s']==22
    single=Path(directory)/'single.benchmark.log'
    single.write_text(''.join(rows[:164]))  # complete Disabled, all other routes untried
    one=m.export([single],'meganex','estimated')[0]
    assert one['tsv'].split('\t')[1]=='n.a.' and 'Reference:00' in one['untried']
    short=[event('benchmark_configuration',25000,2,1|(1<<1)|(1<<15),0)]
    short += [event('benchmark_rates',25000+i*250,(80000<<32)|40000,(40000<<32)|90000,2,2049) for i in range(41)]
    single.write_text(''.join(rows[:164]+short))
    attempted=m.export([single],'meganex','estimated')[0]
    assert attempted['tsv'].split('\t')[5]=='n.d.' and 'Reference:00' not in attempted['untried']
    print('38 exact spreadsheet measurements/40 cells, Djules3X B-only, A/C process profiles, Pimax rows, cycle selection, missing/short guards PASS')

with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'shading.benchmark.log'
    events=[event('logger',0,0,0,0,536)]
    for revision,rate in enumerate((0,4,8),1):
        start=revision*25000
        bits=1|(1<<16)|(3<<12)|(rate<<20)
        events.append(event('benchmark_configuration',start,revision,bits,100<<16))
        for point in range(81):
            ms=start+point*250
            events.append(event('benchmark_rates',ms,(80000<<32)|40000,(40000<<32)|90000,revision,2049))
            events.append(event('benchmark_cycles',ms,(7000<<32)|55000,44000,revision,7))
    path.write_text(''.join(events))
    rows=m.export([path],'meganex','estimated')
    assert [r['afw_shading_rate'] for r in rows]==[0,4,8]
    assert all(r['cells']['Fidelity:00']['config']['afw_shading_rate']==r['afw_shading_rate'] for r in rows)
    print('Active AFW shading rates remain separate portable benchmark rows PASS')

# Ten-second automatic intervals use the full tail and exclude restored/manual
# settings outside the suite. Known median and a late extreme catch tail loss.
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'automatic.benchmark.log'
    events=[event('logger',0,0,0,0,558),event('benchmark_command',0,1,1,99,558),
            event('benchmark_configuration',0,1,1|(1<<1)|(1<<24),0)]
    for i in range(40):
        ms=i*250
        fps=999 if ms<3000 else 42 if ms<9500 else 84
        latency=999 if ms<3000 else 44 if ms<9500 else 66
        events += [event('benchmark_rates',ms,(fps*1000<<32)|21000,(21000<<32)|90000,1,2049),
                   event('benchmark_cycles',ms,(1000<<32)|55000,latency*1000,1,7)]
    events += [event('benchmark_command',10000,2,0,99,558),event('benchmark_configuration',10200,2,1|(1<<1),0)]
    for i in range(80):
        events.append(event('benchmark_rates',10200+i*250,(999000<<32)|21000,90000,2,2049))
    path.write_text(''.join(events))
    rows=m.export([path],'meganex','estimated')
    result=rows[0]['cells']['Reference:00']
    assert result['value']=='42-84(44-66)' and result['fps_median']==42 and result['cycle_median']==44
    assert result['start_s']==3 and result['end_s']==10 and len(rows[0]['measurements'])==1
    print('V558 10-second suite, first3s only, medians, final samples and command boundary PASS')

# The current suite must not collapse different pacing or buffer budgets.
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'buffer.benchmark.log'
    rows=[event('logger',0,0,0,0,658)]
    for i,(pacing,capacity) in enumerate((p,c) for p in (0,1) for c in (2,3)):
        bits=1|(2<<1)|(1<<15)|(1<<45)|(1<<24)|(64<<25)|(pacing<<42)
        if capacity==3:bits|=1<<44
        start=i*12000;revision=i+1
        rows.append(event('benchmark_configuration',start,revision,bits,100<<16))
        for point in range(41):
            ms=start+point*250
            rows.append(event('benchmark_rates',ms,(90000<<32)|45000,(45000<<32)|90000,revision,2049))
            rows.append(event('benchmark_cycles',ms,(7000<<32)|55000,44000,revision,7))
    path.write_text(''.join(rows),encoding='utf-8')
    result=m.export([path],'meganex','estimated')
    assert len(result)==4 and {(g['pacing'],g['bridge_buffer']) for g in result}=={(p,c) for p in (0,1) for c in (2,3)}
    assert all(g['measurements'][0]['config']['suite_step']==64 for g in result)
print('V658 index64 preserved; Framework/Director and two/three-credit measurements never merge PASS')
