# Isolated preparation only; shares read/once/put/changes from parent script.
# Keep existing lighting assertions and cover both layer branch states.
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

# The original capture fixture assumed scaled model images were constant and
# used neural[0] for every capture pixel. The new checkerboard is not constant.
# Compute an independent double-precision bilinear expectation for that case;
# source/composite/alpha/ROI/tail and capture-on/off remain bit-exact checks.
path = 'tests/codec_gpu_harness.cpp.in'
h = changes[path]
h = once(h, '        Float4 expected_neural = neural[model_index];', '''        Float4 expected_neural = neural[model_index];
        const bool patterned_scaled = v2 && variant == 6 &&
            (model_width != width || model_height != height);
        if (patterned_scaled) {
            Require((flags & 1u) != 0u,
                "Patterned capture oracle currently requires the explicit model-value domain");
            const double mx = (static_cast<double>(source_x)+0.5)*model_width/width-0.5;
            const double my = (static_cast<double>(source_y)+0.5)*model_height/height-0.5;
            const int ix = static_cast<int>(std::floor(mx));
            const int iy = static_cast<int>(std::floor(my));
            const double fx = mx-ix, fy = my-iy;
            const auto sample = [&](const std::vector<Float4> &pixels) {
                double r=0.0,g=0.0,b=0.0;
                for (int oy=0;oy<2;++oy) for (int ox=0;ox<2;++ox) {
                    const int x0=std::clamp(ix+ox,0,static_cast<int>(model_width)-1);
                    const int y0=std::clamp(iy+oy,0,static_cast<int>(model_height)-1);
                    const double weight=(ox?fx:1.0-fx)*(oy?fy:1.0-fy);
                    const auto &p=pixels[static_cast<UINT>(y0)*model_width+static_cast<UINT>(x0)];
                    r+=weight*p.x;g+=weight*p.y;b+=weight*p.z;
                }
                return Float4{static_cast<float>(r),static_cast<float>(g),static_cast<float>(b),1.0f};
            };
            expected_proxy=sample(proxy);expected_neural=sample(neural);
        }''')
h = once(h, '                    if (v2 && (flags & 1u) == 0 && (stage == 1 || stage == 2)) {', '''                    if (patterned_scaled && (stage == 1 || stage == 2)) {
                        Float4 actual{}; std::memcpy(&actual, words, sizeof(actual));
                        // Double analytic interpolation versus SM5 float coordinates/FMA.
                        // Only these two reconstructed model planes need this tolerance.
                        Require(std::abs(actual.x-expected[stage]->x)<0.000002f &&
                                std::abs(actual.y-expected[stage]->y)<0.000002f &&
                                std::abs(actual.z-expected[stage]->z)<0.000002f && actual.w==1.0f,
                                "Patterned scaled model capture differs from independent bilinear oracle");
                    } else if (v2 && (flags & 1u) == 0 && (stage == 1 || stage == 2)) {''')
put(path,h)
path='src/nr_settings_ui.inl'
ui=changes[path]
ui=once(ui, '0-1x and the full-resolution after-SR reference are unchanged.',
    'With layers off or neutral, 0-1x is unchanged; the full-resolution after-SR reference is always unchanged.')
ui=once(ui, '0-1 倍和完整后置参考不变。',
    '分层关闭或为中性时，0-1 倍不变；完整后置参考始终不变。')
put(path,ui)
subprocess.run(['python3','scripts/validate-bug-sweep.py','--compiler','g++'],check=True)
