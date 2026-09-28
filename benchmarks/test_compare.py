"""Independent checks of the reporting math and reference mismatch detection."""
import copy
import unittest

from compare import aggregate, compare


def report(value):
    return {
        'workload': {'fixture': 1}, 'semantics': {'height': [0, 10]},
        'build': {'type': 'Release'}, 'environment': {'cpu': 'test'},
        'measurements': {'sampleHeight': {
            'unit': 'ns/operation', 'median': value, 'p95': value,
            'batches': 1, 'iterations_per_batch': 2,
            'operations_per_iteration': 10, 'warmup_batches': 1,
            'raw_ns_per_operation': [value],
        }},
    }


class ComparisonTest(unittest.TestCase):
    def test_median_of_run_medians_and_nearest_rank_p95(self):
        pairs = [compare(report(a), report(b)) for a, b in [(10, 8), (20, 16), (100, 80)]]
        result = aggregate(pairs)
        sample = result['comparisons']['sampleHeight']
        self.assertEqual(result['paired_run_count'], 3)
        self.assertEqual(sample['before_median_ns'], 20)
        self.assertEqual(sample['after_median_ns'], 16)
        self.assertEqual(sample['time_reduction_percent'], 20)
        self.assertEqual(sample['before_p95_ns'], 100)
        self.assertEqual(sample['after_p95_ns'], 80)

    def test_changed_numerics_and_workload_are_rejected(self):
        original = report(10)
        for key in ('semantics', 'workload'):
            changed = copy.deepcopy(original)
            changed[key] = {'changed': True}
            with self.assertRaises(ValueError):
                compare(original, changed)

    def test_changed_measurement_set_and_configuration_are_rejected(self):
        original = report(10)
        changed = copy.deepcopy(original)
        changed['measurements']['sampleHeight']['batches'] = 2
        with self.assertRaises(ValueError):
            compare(original, changed)
        changed = copy.deepcopy(original)
        changed['measurements'].clear()
        with self.assertRaises(ValueError):
            compare(original, changed)

    def test_repeated_runs_must_have_the_same_environment_and_inputs(self):
        pair = compare(report(10), report(8))
        for section in ('environment', 'build'):
            changed = copy.deepcopy(pair)
            changed['after'][section] = {'changed': True}
            with self.assertRaises(ValueError):
                aggregate([pair, changed])
        changed = copy.deepcopy(pair)
        changed['semantic_snapshot_sha256'] = 'different'
        with self.assertRaises(ValueError):
            aggregate([pair, changed])


if __name__ == '__main__':
    unittest.main()
