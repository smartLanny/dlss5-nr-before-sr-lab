extern "C" __declspec(dllimport) int RuntimeLoadFixtureDependency();

extern "C" __declspec(dllexport) int RuntimeLoadFixtureTarget() {
    return RuntimeLoadFixtureDependency();
}
