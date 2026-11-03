"""Offline paired quality gate; windows (not pixels or K8 frames) are replicates."""
import math
from statistics import mean, stdev

ALPHA = 0.05
REGIONS = ('hair_over_dof', 'bright_contour')
DOMAINS = ('rendered', 'presented')
METRICS = ('residual_x1000', 'residual_even', 'residual_odd')
FAMILY = len(REGIONS) * len(DOMAINS) * len(METRICS)
MIN_PAIRS = math.ceil(math.log2(FAMILY / ALPHA))


def paired_sign(off, on):
    """One-sided exact sign test H0: P(ON-OFF < 0) <= 1/2.

    A tie counts against improvement (conservative). No amplitude cutoff is
    tuned to the candidate: zero is the null; uncertainty comes from paired
    repeat signs. Pairs must be adjacent and fixed before seeing outcomes.
    """
    if len(off) != len(on) or not all(math.isfinite(v) for v in [*off, *on]):
        return dict(verdict='INVALID', reason='Missing or nonfinite paired values')
    delta = [b-a for a,b in zip(off,on)]
    n = len(delta)
    wins = sum(d < 0 for d in delta)
    p = sum(math.comb(n,k) for k in range(wins,n+1)) / 2**n
    enough = n >= MIN_PAIRS
    return dict(verdict=('PASS' if p <= ALPHA/FAMILY else 'FAIL') if enough else 'INSUFFICIENT',
                reason=None if enough else 'insufficient alternations',
                pairs=n, minimum_pairs=MIN_PAIRS, improvements=wins,
                differences_on_minus_off=delta, mean_difference=mean(delta) if n else None,
                paired_sd=stdev(delta) if n > 1 else None,
                p_one_sided=p, alpha_bonferroni=ALPHA/FAMILY,
                off_repeat_spread=max(off)-min(off) if n else None)


def pairing(labels):
    """Legacy two-pair receipt or new counterbalanced adjacent-pair labels."""
    if set(labels) == {'off-a','on-a','off-b','on-b'}:
        return [('off-a','on-a'),('off-b','on-b')]
    ids = sorted({l.split('-',1)[1] for l in labels})
    expected = {f'{state}-{i}' for i in ids for state in ('off','on')}
    if set(labels) != expected:
        raise ValueError('Incomplete OFF/ON pairs; never discard a failed window')
    order = [f'{state}-{i}' for j,i in enumerate(ids)
             for state in (('off','on') if j % 2 == 0 else ('on','off'))]
    if list(labels) != order or ids != [f'{i:02d}' for i in range(len(ids))]:
        raise ValueError('Pairs must be adjacent with predeclared counterbalanced order')
    return [(f'off-{i}', f'on-{i}') for i in ids]


def evaluate(report):
    rows = report['windows']
    pairs = pairing(rows)
    result = dict(method='exact paired sign test, ON minus OFF', family_size=FAMILY,
                  family_alpha=ALPHA, minimum_pairs=MIN_PAIRS, pairs=pairs, observables={})
    for domain in DOMAINS:
        for region in REGIONS:
            key = domain+'/'+region
            tests = {}
            for metric in METRICS:
                values = {l:r[domain][region] for l,r in rows.items()}
                if any('error' in v or metric not in v for v in values.values()):
                    tests[metric] = dict(verdict='INVALID', reason='Unavailable frozen OFF support or metric')
                else:
                    scale = 1 if metric == 'residual_x1000' else 1000
                    tests[metric] = paired_sign([values[a][metric]*scale for a,b in pairs],
                                               [values[b][metric]*scale for a,b in pairs])
            registered = all(not any(k in r['registration'][domain] for k in ('error','cross_window_error')) for r in rows.values())
            verdicts = [t['verdict'] for t in tests.values()]
            verdict = ('INSUFFICIENT' if 'INSUFFICIENT' in verdicts else
                       'PASS' if registered and all(v == 'PASS' for v in verdicts) else 'FAIL')
            result['observables'][key] = dict(verdict=verdict, registration_valid=registered, comparisons=tests)
    result['verdict'] = ('INSUFFICIENT' if any(o['verdict']=='INSUFFICIENT' for o in result['observables'].values()) else
                         'PASS' if all(o['verdict']=='PASS' for o in result['observables'].values()) else 'FAIL')
    result['reason'] = 'insufficient alternations' if result['verdict']=='INSUFFICIENT' else None
    result['quality_pass'] = False
    result['qualified_for_ultra'] = False
    result['qualification_note'] = 'Residual gate only. Face control, registered float energy, transport, cost and S8 remain mandatory; no promotion.'
    return result
