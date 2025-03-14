# Build

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh
export VCPKG_ROOT=/path/to/vcpkg
export PATH=$VCPKG_ROOT:$PATH
export VCPKG_TOOLCHAIN_PATH="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
make
```
