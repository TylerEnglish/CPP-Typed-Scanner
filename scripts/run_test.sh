set -euo pipefail
build_dir="${BUILD_DIR:-build}"

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j"$(nproc)"
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir "$build_dir"