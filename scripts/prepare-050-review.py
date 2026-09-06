# Isolated preparation only; shares read/once/put/changes from parent script.
# Keep the existing random/edge lighting assertions intact; supply the new
# branch inputs and execute both layer-disabled and layer-enabled settings.
path = 'scripts/validate-bug-sweep.py'
text = read(path)
text = once(text, 'block = definition(source, "if (LightingLock > 0.000001)")',
                  'block = definition(source, "if (LightingLock > 0.000001")')
text = once(text, 'float taps[4];',
    'float taps[4];\nbool layeredActive = false;\nfloat LayerTone = 1.f, LayerStructure = 1.f, layerMean = 0.f;')
text = once(text, '        const float result = apply(residual, raw, unit(rng));',
    '        layeredActive = (i & 1) != 0;\n'
    '        LayerTone = (i % 3) == 0 ? .5f : 1.f;\n'
    '        LayerStructure = (i % 3) == 0 ? 1.5f : 1.f;\n'
    '        const float lock = unit(rng);\n'
    '        const float result = apply(residual, raw, lock);\n'
    '        if (layeredActive)\n'
    '            check(apply(residual, raw, 0.f) == residual, "layer-only neighborhood never applies lighting attenuation");')
put(path, text)
source = changes['src/nr_before_sr.cpp']
source = once(source, '    // keeps this branch completely free when LightingLock is zero.',
    '    // skips these reads when LightingLock is zero unless a non-neutral\n'
    '    // tone/structure split explicitly needs the same neighborhood.')
source = source.replace('LayeredEnhancement requested=%d mode=%d', 'LayeredEnhancement requested=%d mode=%u')
put('src/nr_before_sr.cpp', source)
subprocess.run(['python3','scripts/validate-bug-sweep.py','--compiler','g++'],check=True)
