"""Export logged OFXR menu measurements as grouped TSV and JSON evidence.

Uses historical platform templates; automatic suites write their own metric-row
CSV. No spreadsheet edits or third-party dependencies. Missing trials stay n.d.
"""
import argparse
import csv
import hashlib
import json
import re
import statistics
from pathlib import Path

FIELDS = re.compile(r'(\w+)=([^\s]+)')
MEGANEX = ['Disabled', 'Fidelity:00', 'Fidelity:10', 'Fidelity:01', 'Fidelity:11',
           'Reference:00', 'Reference:10', 'Reference:01', 'Reference:11',
           'Djules:00', 'Djules:00:prefer', 'Djules:10', 'Djules:01', 'Djules:11',
           'Djules3X:00', 'Djules3X:10', 'Djules3X:01', 'Djules3X:11',
           'New:00', 'New:10', 'New:01', 'New:11',
           'Hybrid:00', 'Hybrid:10', 'Hybrid:01', 'Hybrid:11',
           'FSR:00', 'FSR:10', 'FSR:01', 'FSR:11',
           'DLSS-G-A:00', 'DLSS-G-A:10', 'DLSS-G-A:01', 'DLSS-G-A:11',
           'DLSS-G-C:00', 'DLSS-G-C:10', 'DLSS-G-C:01', 'DLSS-G-C:11']
QUEST = ['Disabled', 'Reference', 'Djules', 'Djules3X', 'New', 'Hybrid', 'FSR', 'DLSS-G']

def decode(bits, quality):
    generator = (bits >> 3) & 3
    backend = (bits >> 1) & 3
    enabled = bool(bits & 1)
    triple = bool(bits & (1 << 10))
    dlssg_preset = (bits >> 17) & 3  # process-start per-game profile; not measured active NGX model
    route = ('Disabled' if not enabled else 'FSR' if generator == 1 else 'DLSS-G' if generator == 2
             else 'Hybrid' if bits & (1 << 7) else ['Fidelity', 'Reference', 'New', 'Djules'][backend])
    if route == 'Djules' and triple:
        route = 'Djules3X'
    elif route == 'DLSS-G' and triple:
        route = 'DLSS-G3X'  # No owner-sheet column: retain details, never replace the 2X cell.
    flags = str(int(bool(bits & 32))) + str(int(bool(bits & 64)))
    cell = route if route == 'Disabled' else route + ':' + flags
    if route == 'DLSS-G':
        cell = ('DLSS-G-A' if dlssg_preset == 1 else 'DLSS-G-C' if dlssg_preset == 3 else 'DLSS-G-unverified') + ':' + flags
    if route == 'Djules' and flags == '00' and bits & (1 << 9):
        cell += ':prefer'
    return dict(route=route, cell=cell, flags=flags, dlssg_preset_request=dlssg_preset,
                automatic=bool(bits & (1 << 24)), suite_step=(bits >> 25) & 127,
                bridge_buffer=(3 if bits & (1<<44) else 2) if bits & (1<<45) else None,
                pacing=(bits >> 42) & 3,
                prefer_fps=bool(bits & (1 << 9)), triple=triple,
                nr=bool(bits & (1 << 14)), separate_queue=bool(bits & (1 << 15)),
                luke=bool(bits & (1 << 16)), afw_mode=(bits >> 12) & 3, afw_shading_rate=(bits >> 20) & 15,
                hybrid_effective=bool(bits & (1 << 8)), motion_vectors=bool(bits & (1 << 11)),
                preset=quality & 255, scale=(quality >> 8) & 255, native_scale=(quality >> 16) & 65535)

def read_segments(path):
    segments, active, configurations = [], None, {}
    version = None
    for line in path.open(encoding='utf-8-sig'):
        f = dict(FIELDS.findall(line))
        if 'op' not in f:
            continue
        op = f['op']
        if op == 'logger' and int(f['result']) > 0:
            version = int(f['result'])
        if f['phase'] == 'M':
            continue  # Startup replay is not a continuous measurement interval.
        ms, a, b, c = float(f['ms']), int(f['a']), int(f['b']), int(f['c'])
        if op == 'benchmark_command':
            if active is not None and a != active['signature'][0]:
                active['end'] = ms
                active = None
        elif op == 'benchmark_configuration':
            signature = (a, b, c)
            configurations[a] = decode(b, c)
            if active is None or active['signature'] != signature or ms - active['last'] > 1000:
                if active is not None:
                    active['end'] = ms
                active = dict(signature=signature, config=configurations[a], start=ms, last=ms,
                              rates=[], cycles=[], gaps=0, file=str(path), version=version)
                segments.append(active)
            active['last'] = max(active['last'], ms)
        elif op in ('benchmark_rates', 'benchmark_cycles'):
            if active is None or c != active['signature'][0]:
                continue
            if ms - active['last'] > 1000:
                active['gaps'] += 1
            active['last'] = max(active['last'], ms)
            result = int(f['result'])
            if op == 'benchmark_rates' and result & 1:
                active['rates'].append(dict(ms=ms, output=(a >> 32) / 1000, real=(a & 0xffffffff) / 1000,
                                            generated=(b >> 32) / 1000, hz=(b & 0xffffffff) / 1000, source=result >> 8))
            elif op == 'benchmark_cycles':
                active['cycles'].append(dict(ms=ms, length=(a & 0xffffffff) / 1000 if result & 2 else None,
                                             estimated=b / 1000 if result & 4 else None,
                                             ofxr=(a >> 32) / 1000 if result & 1 else None))
    return segments

def span_text(values):
    low, high = round(min(values)), round(max(values))
    return str(low) if low == high else f'{low}-{high}'

def summarize(segment, settle, minimum, metric):
    modern = (segment.get('version') or 0) >= 558
    tail = 0 if modern else 3000
    lo, hi = segment['start'] + settle * 1000, segment.get('end', segment['last']) - tail
    if hi - lo < minimum * 1000 or segment['gaps']:
        return None
    rates = [x for x in segment['rates'] if lo <= x['ms'] <= hi]
    cycles = [x[metric] for x in segment['cycles'] if lo <= x['ms'] <= hi and x[metric] is not None]
    if len(rates) < minimum * 2:
        return None
    config = segment['config']
    fps = [x['output'] for x in rates]
    real = [x['real'] for x in rates]
    generated = [x['generated'] for x in rates]
    text = span_text(fps)
    if config['route'] != 'Disabled':
        text += '(' + (span_text(cycles) if len(cycles) >= minimum * 2 else 'n.d.') + ')'
    return dict(config=config, value=text, fps_min=min(fps), fps_median=statistics.median(fps), fps_max=max(fps),
                real_min=min(real), real_max=max(real), generated_min=min(generated), generated_max=max(generated),
                cycle_min=min(cycles) if cycles else None, cycle_median=statistics.median(cycles) if cycles else None, cycle_max=max(cycles) if cycles else None,
                metric=metric, metric_definition=('producer-inclusive cycle length' if metric=='length' else
                    'minimum confirmed-output refresh sequence (656+)' if (segment.get('version') or 0)>=656 else
                    'observed native OpenXR End return (655)' if (segment.get('version') or 0)==655 else
                    'predicted display estimate (pre-655)'),
                sample_count=len(rates), start_s=lo / 1000, end_s=hi / 1000,
                output_sources=sorted(set(x['source'] for x in rates)), file=segment['file'], version=segment['version'],
                warning='Hybrid selection lacked effective guide activation' if config['route'] == 'Hybrid' and not config['hybrid_effective']
                else 'No confirmed generated output' if config['route'] != 'Disabled' and max(generated) == 0
                else 'DLSS-G preset is a startup driver-profile request; active NVIDIA model is not independently verified' if config['route'].startswith('DLSS-G') else None)

def export(paths, platform, metric, settle=None, minimum=None):
    groups = {}
    measurements = {}
    attempted = {}
    segments = [segment for path in paths for segment in read_segments(path)]
    if any(segment['config'].get('automatic') for segment in segments):
        segments = [segment for segment in segments if segment['config'].get('automatic')]
    for segment in segments:
        cfg = segment['config']
        group = (cfg['nr'], cfg['separate_queue'], cfg['luke'], cfg['afw_mode'] if cfg['luke'] else 0,
                 0 if platform == 'meganex' else cfg['dlssg_preset_request'],
                 cfg['afw_shading_rate'] if cfg['luke'] and cfg['afw_mode'] else 0,
                 cfg['pacing'], cfg['bridge_buffer'] or 0)
        key = cfg['cell'] if platform == 'meganex' else cfg['route']
        attempted.setdefault(group, set()).add(key)
        useful_minimum = minimum if minimum is not None else (5 if (segment.get('version') or 0) >= 558 else 10)
        # Preserve historical trimming; only the V660+ automatic suite changed.
        settling = settle if settle is not None else (2.5 if cfg.get('automatic') and (segment.get('version') or 0)>=660 else 3)
        result = summarize(segment, settling, useful_minimum, metric)
        if result is None:
            continue
        cfg = result['config']
        # Different queues, pacing or buffer budgets must never merge.
        group = (cfg['nr'], cfg['separate_queue'], cfg['luke'], cfg['afw_mode'] if cfg['luke'] else 0,
                 0 if platform == 'meganex' else cfg['dlssg_preset_request'],
                 cfg['afw_shading_rate'] if cfg['luke'] and cfg['afw_mode'] else 0,
                 cfg['pacing'], cfg['bridge_buffer'] or 0)
        cells = groups.setdefault(group, {})
        measurements.setdefault(group, []).append(result)
        key = cfg['cell'] if platform == 'meganex' else cfg['route']
        cells[key] = result  # Last complete repeat wins; evidence preserves exact file/window.
    results = []
    for key, cells in sorted(groups.items()):
        slots = MEGANEX if platform == 'meganex' else QUEST
        row = [cells[x]['value'] if x in cells else 'n.d.' if x in attempted[key] else 'n.a.' for x in slots]
        versions = sorted(set(x['version'] for x in cells.values()))
        if len(versions) != 1:
            raise ValueError('Multiple builds in one row; export each build separately.')
        if platform in ('meganex', 'pimax'):
            row += ['', str(versions[0])]  # Meganex BE/BF, Pimax BQ/BR.
        headers = list(slots) + (['Log', 'Tested Build'] if platform in ('meganex', 'pimax') else [])
        results.append(dict(nr=key[0], separate_queue=key[1], luke=key[2], afw_mode=key[3], headers=headers,
                            dlssg_preset_request=key[4], afw_shading_rate=key[5],
                            pacing=key[6], bridge_buffer=key[7] or None,
                            start_column='S' if platform == 'meganex' else 'BI' if platform == 'pimax' else 'G',
                            missing=[x for x in slots if x not in cells], cells=cells,
                            untried=[x for x in slots if x not in attempted[key]],
                            measurements=measurements[key], tsv='\t'.join(row)))
    return results

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('logs', nargs='+', type=Path)
    p.add_argument('--platform', choices=['meganex', 'quest', 'pimax'], default='meganex')
    p.add_argument('--metric', choices=['estimated', 'length'], default='estimated')
    p.add_argument('--game', required=True)
    p.add_argument('--sync-mode', required=True, choices=['early', 'late', 'very-late', 'unknown'])
    p.add_argument('--hags', required=True, choices=['on', 'off', 'unknown'])
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    paths = []
    for path in a.logs:
        if path.is_dir():
            candidates = sorted(path.glob('*.benchmark.log'), key=lambda x: x.stat().st_mtime)
            if candidates:
                pid = re.search(r'pid\d+', candidates[-1].name)
                paths.extend([x for x in candidates if pid and pid[0] in x.name] if pid else [candidates[-1]])
        else:
            paths.append(path)
    paths = sorted(set(paths), key=lambda x: x.stat().st_mtime)
    rows = export(paths, a.platform, a.metric)
    if not rows:
        raise SystemExit('No complete benchmark intervals. V529+ compact logs are required; no row fabricated.')
    a.out.mkdir(parents=True, exist_ok=True)
    report = dict(game=a.game, platform=a.platform, metric=a.metric, sync_mode=a.sync_mode, hags=a.hags,
                  method='V558+: exclude first 3s only, retain samples until next command, >=5s useful; legacy builds retain first/last3s and >=10s. The legacy --metric estimated log slot means minimum refresh-sequence cycle from656, observed End return in655, predicted display before655; each measurement records metric_definition. Ranges rounded for sheet. S for Meganex; R remains a separate run without OFXR loaded.',
                  files=[dict(path=str(x), sha256=hashlib.sha256(x.read_bytes()).hexdigest()) for x in paths], rows=rows)
    (a.out / 'benchmark-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    # Portable named measurements, independent of destination cell addresses.
    with (a.out / 'measurements.tsv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t')
        writer.writerow(['Game', 'Build', 'Route', 'DLSS-G preset request', 'Private synthesis A',
                         'Reduce XR B', 'Prefer FPS', 'Multiplier', 'Neural', 'Separate queue',
                         'Luke AFW', 'AFW shading rate index', 'Output FPS range', 'Cycle metric', 'Cycle range ms', 'Warning'])
        for row in rows:
            for result in row['measurements']:
                cfg = result['config']
                writer.writerow([a.game, result['version'], cfg['route'],
                                 {1:'A',3:'C'}.get(cfg['dlssg_preset_request'], 'unverified'),
                                 cfg['flags'][0], cfg['flags'][1], int(cfg['prefer_fps']),
                                 '3X' if cfg['triple'] else '2X', int(row['nr']), int(row['separate_queue']),
                                 row['afw_mode'] if row['luke'] else 'off', row['afw_shading_rate'],
                                 span_text([result['fps_min'], result['fps_max']]), a.metric,
                                 span_text([result['cycle_min'], result['cycle_max']]) if result['cycle_min'] is not None else 'n.d.',
                                 result['warning'] or ''])
    for i, row in enumerate(rows):
        target = a.out / f'row-{i + 1}.tsv'
        target.write_text(row['tsv'] + '\n', encoding='utf-8')
        (a.out / f'row-{i + 1}-with-headers.tsv').write_text('\t'.join(row['headers']) + '\n' + row['tsv'] + '\n', encoding='utf-8')
        print(f'{target}: paste at {row["start_column"]}; NR={row["nr"]}, separate queue={row["separate_queue"]}, missing={len(row["missing"])}')
        print(row['tsv'])

if __name__ == '__main__':
    main()
