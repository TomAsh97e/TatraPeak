#!/usr/bin/env python3
"""Compare benchmark reports, including their complete untimed numerical snapshots."""
import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path
import statistics


def read(path):
    return json.loads(path.read_text())


def compare(before, after):
    if before['workload'] != after['workload']:
        raise ValueError('workload differs: use the same dataset, scene and point lists')
    if before['semantics'] != after['semantics']:
        raise ValueError('numerical results differ (exact JSON value comparison)')
    if before['measurements'].keys() != after['measurements'].keys():
        raise ValueError('measurement sets differ')
    comparisons = {}
    for name, old in before['measurements'].items():
        new = after['measurements'][name]
        for key in ('unit', 'batches', 'iterations_per_batch', 'operations_per_iteration', 'warmup_batches'):
            if old[key] != new[key]:
                raise ValueError(f'{name}: different measurement configuration: {key}')
        comparisons[name] = {
            'before_median_ns': old['median'], 'after_median_ns': new['median'],
            'time_reduction_percent': 100 * (old['median'] - new['median']) / old['median'],
            'before_p95_ns': old['p95'], 'after_p95_ns': new['p95'],
        }
    semantics_hash = hashlib.sha256(json.dumps(before['semantics'], sort_keys=True,
                                              separators=(',', ':')).encode()).hexdigest()
    def summary(report):
        return {k: v for k, v in report.items() if k != 'semantics'}
    return {'schema_version': 1, 'numerical_results_exactly_equal': True,
            'semantic_snapshot_sha256': semantics_hash, 'workload': before['workload'],
            'comparisons': comparisons, 'before': summary(before), 'after': summary(after)}


def aggregate(pairs):
    first = pairs[0]
    for pair in pairs[1:]:
        for key in ('workload', 'semantic_snapshot_sha256'):
            if pair[key] != first[key]:
                raise ValueError(f'repeated runs differ: {key}')
        for side in ('before', 'after'):
            for key in ('build', 'environment'):
                if pair[side][key] != first[side][key]:
                    raise ValueError(f'repeated runs differ: {side} {key}')
            if pair[side]['measurements'].keys() != first[side]['measurements'].keys():
                raise ValueError('repeated measurement sets differ')
            for name, item in pair[side]['measurements'].items():
                for key in ('unit', 'batches', 'iterations_per_batch', 'operations_per_iteration', 'warmup_batches'):
                    if item[key] != first[side]['measurements'][name][key]:
                        raise ValueError(f'repeated measurement configuration differs: {name} {key}')
    comparisons = {}
    for name in first['comparisons']:
        item = {}
        for side in ('before', 'after'):
            medians = [p[side]['measurements'][name]['median'] for p in pairs]
            samples = sorted(v for p in pairs for v in p[side]['measurements'][name]['raw_ns_per_operation'])
            item[f'{side}_median_ns'] = statistics.median(medians)
            item[f'{side}_run_medians_ns'] = medians
            item[f'{side}_p95_ns'] = samples[math.ceil(.95 * len(samples)) - 1]
        item['time_reduction_percent'] = 100 * (item['before_median_ns'] - item['after_median_ns']) / item['before_median_ns']
        comparisons[name] = item
    return {
        'schema_version': 1, 'numerical_results_exactly_equal': True,
        'semantic_snapshot_sha256': first['semantic_snapshot_sha256'],
        'workload': first['workload'], 'comparisons': comparisons,
        'aggregation': 'median of per-run medians; p95 over all normalized batch averages (not per-call latency)',
        'paired_run_count': len(pairs), 'paired_runs': pairs,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--extra-pair', nargs=2, type=Path, action='append', default=[],
                        metavar=('BEFORE', 'AFTER'), help='add another alternating before/after run')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    pairs = [(args.before, args.after)] + args.extra_pair
    result = aggregate([compare(read(before), read(after)) for before, after in pairs])
    result['created_at_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    for name, item in result['comparisons'].items():
        print(f"{name}: {item['before_median_ns']:.2f} -> {item['after_median_ns']:.2f} ns; "
              f"reduction {item['time_reduction_percent']:.2f}%")
    print('Exact numerical equivalence: PASS')


if __name__ == '__main__':
    main()
