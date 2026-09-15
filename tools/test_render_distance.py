#!/usr/bin/env python3
import unittest
import numpy as np
from render_distance import METRIC_VERSION, distance

RATE = 48000
TIME = np.arange(3 * RATE) / RATE

class MetricTests(unittest.TestCase):
    def test_identical_and_gain_scaled_are_zero(self):
        signal = np.column_stack([np.sin(2*np.pi*220*TIME)]*2).astype(np.float32)
        self.assertEqual(distance(signal, signal, RATE)["metricVersion"], METRIC_VERSION)
        self.assertAlmostEqual(distance(signal, signal, RATE)["distance"], 0)
        self.assertLess(distance(signal, signal*.2, RATE)["distance"], .01)

    def test_time_envelope_changes_envelope_term(self):
        signal = np.column_stack([np.sin(2*np.pi*220*TIME)]*2).astype(np.float32)
        shaped = signal*np.linspace(1, .05, len(signal))[:, None]
        self.assertGreater(distance(signal, shaped, RATE)["envelope"], .1)

    def test_spectrum_changes_spectral_term(self):
        low = np.column_stack([np.sin(2*np.pi*220*TIME)]*2).astype(np.float32)
        high = np.column_stack([np.sin(2*np.pi*1760*TIME)]*2).astype(np.float32)
        self.assertGreater(distance(low, high, RATE)["spectral"], .1)

if __name__ == "__main__": unittest.main()
