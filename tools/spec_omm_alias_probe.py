"""S4 decision gate: can DXC represent RO/coherent RW aliases without changing either program?"""
import sys
import subprocess
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.shader_identity import SourceTree, Toolchain, Cache, compile_requests, lower_priority
from tools.spec_cascade_check import canonical, opt, differ


def main():
    lower_priority()
    tool = Toolchain()
    source = '''
[[vk::binding(0,0)]] RWByteAddressBuffer output;
#ifdef SPEC
[[vk::constant_id(1012)]] const uint omm = 0;
[[vk::binding(9,1)]] ByteAddressBuffer stamp;
[[vk::binding(9,1)]] globallycoherent RWByteAddressBuffer stampRW;
#elif ON
[[vk::binding(9,1)]] globallycoherent RWByteAddressBuffer stampRW;
#else
[[vk::binding(9,1)]] ByteAddressBuffer stamp;
#endif
[numthreads(1,1,1)] void main() {
#ifdef SPEC
 if (omm != 0) { uint old; stampRW.InterlockedOr(144,1,old); output.Store(0,stampRW.Load(0)); }
 else output.Store(0,stamp.Load(0));
#elif ON
 uint old; stampRW.InterlockedOr(144,1,old); output.Store(0,stampRW.Load(0));
#else
 output.Store(0,stamp.Load(0));
#endif
}
'''
    tree = SourceTree.memory({'shaders/probe.hlsl': source})
    requests = ['probe.hlsl', 'probe.hlsl?ON=1', 'probe.hlsl?SPEC=1']
    results = compile_requests(tree, requests, tool=tool, cache=Cache(), label='S4 alias probe')
    for request, result in results.items():
        if not result.ok:
            print(f'S4 alias REJECTED: {request}\n{result.log}', flush=True)
            return 1
    with tempfile.TemporaryDirectory(prefix='b3alias-') as tmp:
        paths = []
        for i, request in enumerate(requests):
            p = Path(tmp) / f'{i}.spv'
            p.write_bytes(results[request].blob('main'))
            validation = subprocess.run([str(tool.dxc.with_name('spirv-val.exe')), '--target-env',
                                         'vulkan1.3', str(p)], capture_output=True, text=True)
            if validation.returncode:
                print(f'S4 alias REJECTED by spirv-val: {validation.stdout}{validation.stderr}', flush=True)
                return 1
            paths.append(p)
        print('S4 alias probe: 3/3 Vulkan 1.3 SPIR-V modules valid', flush=True)
        control = differ(canonical(tool, opt(tool, paths[0], Path(tmp) / 'c0.spv', '-O')),
                         canonical(tool, opt(tool, paths[1], Path(tmp) / 'c1.spv', '-O')))
        print(f'S4 alias control: {control} differing lines (must be > 0)', flush=True)
        if not control:
            return 1
        for v in (0, 1):
            new = opt(tool, paths[2], Path(tmp) / f'new{v}.spv',
                      f'--set-spec-const-default-value=1012:{v}', '--freeze-spec-const',
                      '--fold-spec-const-op-composite', '-O')
            old = opt(tool, paths[v], Path(tmp) / f'old{v}.spv', '-O')
            delta = differ(canonical(tool, new), canonical(tool, old))
            print(f'S4 alias value {v}: {delta} differing lines', flush=True)
            if delta:
                return 1
    print('S4 alias probe: both values equivalent', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
