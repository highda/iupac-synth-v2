#!/usr/bin/env python3
"""IUPAC V4 distance v1. NumPy analyzes production WAV; it never synthesizes product audio."""
import argparse, json, struct
from pathlib import Path
import numpy as np

METRIC_VERSION = 1

def read_wav(path):
    raw = Path(path).read_bytes()
    if len(raw) > 64 * 1024 * 1024 or len(raw) < 44 or raw[:4] != b"RIFF" or raw[8:12] != b"WAVE": raise ValueError("invalid or oversized WAV")
    offset, fmt, payload = 12, None, None
    while offset + 8 <= len(raw):
        kind, size = raw[offset:offset+4], struct.unpack_from("<I", raw, offset+4)[0]; start, end = offset+8, offset+8+size
        if end > len(raw): raise ValueError("truncated WAV chunk")
        if kind == b"fmt ": fmt = raw[start:end]
        elif kind == b"data": payload = raw[start:end]
        offset = end + (size & 1)
    if fmt is None or payload is None or len(fmt) < 16: raise ValueError("missing WAV format or data")
    code, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt)
    if code != 3 or channels != 2 or bits != 32 or len(payload) % 8: raise ValueError("requires stereo IEEE float32 WAV")
    return rate, np.frombuffer(payload, dtype="<f4").reshape(-1, 2)

def features(active, rate):
    if len(active) != 2 * rate: raise ValueError("active signal must contain exactly two seconds")
    rms = np.sqrt(np.mean(active * active))
    if not np.isfinite(rms) or rms < 1e-6: raise ValueError("silent active signal")
    normalized = active * (0.1 / rms); size, hop, window = 2048, 512, np.hanning(2048)
    frames = np.stack([normalized[i:i+size] for i in range(0, len(normalized)-size+1, hop)])
    powers = np.abs(np.fft.rfft(frames[:, :, 0] * window, axis=1))**2 + np.abs(np.fft.rfft(frames[:, :, 1] * window, axis=1))**2
    frequencies, edges = np.fft.rfftfreq(size, 1/rate), np.geomspace(40, 16000, 25)
    bands = np.array([np.mean(np.sum(powers[:, (frequencies >= low) & (frequencies < high)], axis=1)) for low, high in zip(edges[:-1], edges[1:])])
    band_db = 10*np.log10(np.maximum(bands, 1e-12)); band_db -= np.mean(band_db)
    envelope = np.array([np.sqrt(np.mean(part*part)) for part in np.array_split(active, 20)]); envelope /= np.max(envelope)
    return band_db, envelope

def distance(a, b, rate):
    ad, ae = features(a[:2*rate], rate); bd, be = features(b[:2*rate], rate)
    spectral = np.clip(np.mean(np.abs(ad-bd))/24, 0, 1); envelope = np.mean(np.abs(ae-be))
    return {"metricVersion": METRIC_VERSION, "distance": float(.75*spectral+.25*envelope), "spectral": float(spectral), "envelope": float(envelope)}

def main():
    parser = argparse.ArgumentParser(); parser.add_argument("a"); parser.add_argument("b"); parser.add_argument("--latency", type=int, default=0); args = parser.parse_args()
    first_rate, first = read_wav(args.a); second_rate, second = read_wav(args.b)
    if first_rate != second_rate or args.latency < 0: raise ValueError("sample rates differ or latency is invalid")
    print(json.dumps(distance(first[args.latency:], second[args.latency:], first_rate), sort_keys=True))

if __name__ == "__main__": main()
