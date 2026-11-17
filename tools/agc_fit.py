"""The numbers behind the Modeled AGC of VHS Vibes 1.6 (ModeledAgc in src/VHSVibes.cpp).

Pure Python, no packages. The detector is the record level circuit of an early VHS deck
(C70, R103, R104, the germanium diode, C80, R107 + R108), simulated per sample with explicit
Euler exactly as the plugin runs it. The gain law is GR = clamp(S * (vK - Vth), 0, 40) dB.

Three things are fitted, each to one figure:
  - Vth: the gain reduction starts at the knee, 10 dB below the deck's nominal input [E];
  - S:   40 dB more input than the knee comes out 3.25 dB louder [D from the NTE1263 sheet:
         0.55 V out at 0.25 mV in, 0.8 V at 25 mV, typical];
  - the input scale: at the nominal input, once settled, the detector sees 1.1 V peak [R:
         2.2 Vp-p line out at the reference level].
vK(A), the voltage C80 settles to under a 1 kHz sine of peak A at the detector, is found by
simulation (open loop: in a steady state the gain is constant, so the loop does not matter).

    python tools/agc_fit.py fit          the three constants and the gain at nominal (a minute)
    python tools/agc_fit.py table        the steady-state table, closed loop, at 44.1 kHz
    python tools/agc_fit.py dynamics     attack on a +20 dB step from the knee, recovery after it
"""
import math
import sys

# the circuit [R: schematic and parts list; E: estimates, see the source comment]
C70, R_SRC, R103, R104, C80, R_DIS = 4.7e-6, 100.0, 1000.0, 22000.0, 47e-6, 1.76e6
V_D, R_D = 0.15, 100.0            # germanium diode, piecewise linear [E]
GR_MAX = 40.0                     # dB [D/E]
SWING = 4.5                       # the line output clips at +-4.5 V [E]; no steady tone here reaches it
KNEE_DB = -10.0                   # knee re the nominal input [E]
V_NOM = 1.1                       # detector peak at the nominal level, settled [R]
RISE_40 = 20 * math.log10(0.8 / 0.55)   # +3.25 dB out for +40 dB in [D]
REF_LEVEL = 0.4                   # the plugin's nominal peak (kRefLevel, -8 dBFS)


class Detector:
    def __init__(self, fs):
        dt = 1.0 / fs
        rs = R_SRC + R103
        self.a_off = R104 / (rs + R104)
        self.on_e = 1.0 / rs
        self.on_k = 1.0 / R_D
        self.on_n = 1.0 / (1.0 / rs + 1.0 / R104 + 1.0 / R_D)
        self.k_c80, self.k_dis, self.k_c70 = dt / C80, dt / (C80 * R_DIS), dt / (C70 * rs)
        self.vk = self.v70 = 0.0

    def step(self, e):
        d = min(max(e, -SWING), SWING) - self.v70
        va = d * self.a_off
        i = 0.0
        if va > self.vk + V_D:
            va = (d * self.on_e + (self.vk + V_D) * self.on_k) * self.on_n
            i = (va - self.vk - V_D) * self.on_k
        self.vk += self.k_c80 * i - self.k_dis * self.vk
        self.v70 += self.k_c70 * (d - va)


def settled_vk(a, fs=192000.0, hz=1000.0):
    """vK in a steady state under a sine of peak a volts (open loop)."""
    det = Detector(fs)
    det.vk = max(0.0, a * det.a_off - V_D - 0.05)    # start a little low: charging is fast
    n = int(round(fs / hz))
    tab = [a * math.sin(2 * math.pi * k / n) for k in range(n)]
    last = None
    for cyc in range(40000):
        for e in tab:
            det.step(e)
        if cyc % 200 == 199:                          # every 0.2 s
            if last is not None and abs(det.vk - last) < 2e-7:
                return det.vk
            last = det.vk
    return det.vk


def fit():
    f_nom = settled_vk(V_NOM)

    def err(ak):
        vth = settled_vk(ak)
        s = (40.0 - RISE_40) /(settled_vk(ak * 10 ** (RISE_40 / 20)) - vth)
        return 20 * math.log10(10 ** (-KNEE_DB / 20) * ak / V_NOM) - s * (f_nom - vth), vth, s

    a0, a1 = 0.95, 1.0
    e0, e1 = err(a0)[0], err(a1)[0]
    for _ in range(30):
        a2 = a1 - e1 * (a1 - a0) / (e1 - e0)
        a0, e0 = a1, e1
        a1 = a2
        e1, vth, s = err(a1)
        if abs(e1) < 1e-7:
            break
    knee_in = REF_LEVEL * 10 ** (KNEE_DB / 20)
    gr_nom = s * (f_nom - vth)
    return {'vth': vth, 's': s, 'volts': a1 / knee_in, 'knee_volts': a1, 'gr_nom': gr_nom}


CONSTS = None


def consts():
    global CONSTS
    if CONSTS is None:
        CONSTS = fit()
    return CONSTS


def run_closed(levels, fs=44100.0, hz=1000.0):
    """Closed loop, per sample, the gain from vK through C81's 3.8 ms lag (no control tick):
    for each (seconds, peak re nominal in dB) the gain reduction in dB, sample by sample."""
    c = consts()
    det = Detector(fs)
    lag = 1 - math.exp(-1 / (fs * 0.01e-6 * 1.2e6 * 560e3 / 1.76e6))
    vc = 0.0
    out, pos = [], 0
    for sec, db in levels:
        amp = REF_LEVEL * 10 ** (db / 20) * c['volts']
        trace = []
        for k in range(int(sec * fs)):
            gr = min(max(c['s'] * (vc - c['vth']), 0.0), GR_MAX)
            det.step(amp * math.sin(2 * math.pi * hz * (pos + k) / fs) * 10 ** (-gr / 20))
            vc += lag * (det.vk - vc)
            trace.append(gr)
        pos += int(sec * fs)
        out.append(trace)
    return out


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'fit'
    c = consts()
    if cmd == 'fit':
        print('S                = %.6f dB per volt of vK' % c['s'])
        print('Vth              = %.6f V' % c['vth'])
        print('volts per unit   = %.6f (detector volts for a sample of 1.0 at full gain)' % c['volts'])
        print('knee at detector = %.6f V peak' % c['knee_volts'])
        print('GR at nominal    = %.6f dB (the normalising boost)' % c['gr_nom'])
    elif cmd == 'table':
        for db in (-10.0, -5.0, 0.0, 10.0, 20.0, 30.0, 35.0):
            gr = run_closed([(20.0, db)])[0][-1]
            print('input %+5.1f dB re knee: GR %6.2f dB, output %+5.2f dB re knee' % (db - KNEE_DB, gr, db - KNEE_DB - gr))
    elif cmd == 'dynamics':
        fs = 44100.0
        tr = run_closed([(10.0, KNEE_DB), (2.0, KNEE_DB + 20.0), (20.0, KNEE_DB - 30.0)], fs)
        att = next(k for k, g in enumerate(tr[1]) if g >= 10.0)
        print('+20 dB step from the knee: 10 dB of GR after %.1f ms; GR %.1f dB after 2 s' % (1000 * att / fs, tr[1][-1]))
        rec = tr[2]
        for t in (1.0, 5.0, 10.0, 15.0):
            print('  %4.1f s after the drop: GR %.2f dB' % (t, rec[int(t * fs) - 1]))
        print('recovery 2..8 s after the drop: %.3f dB/s' % ((rec[int(2 * fs)] - rec[int(8 * fs)]) / 6.0))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
