from pathlib import Path
import importlib.util
import tempfile

spec = importlib.util.spec_from_file_location('exporter', Path(__file__).parents[1] / 'scripts/export-benchmark-row.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
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
    assert len(values)==35 and values[0]=='40' and values[-2:]==['','529']
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
    print('33 exact spreadsheet columns, separate PreferFPS/3X/A/B, cycle selection, missing/short guards PASS')
