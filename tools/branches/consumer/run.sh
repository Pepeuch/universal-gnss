#!/usr/bin/env bash
# Runs only inside the disposable consumer container, never on the host.
set -eo pipefail
test "$(id -u)" != 0
mkdir -p "$HOME"
for path in /tmp/ug-work/source /tmp/ug-work/build /tmp/ug-work/install /workspaces/universal-gnss; do
  test ! -e "$path"
done
source /opt/ros/lyrical/setup.bash
python3 - <<'PY'
from ament_index_python.packages import get_packages_with_prefixes
assert not any(p.startswith('universal_gnss') for p in get_packages_with_prefixes()), 'preinstalled UG'
PY
prefix=/tmp/ug-consumer/prefix
mkdir -p "$prefix"
tar -C "$prefix" -xf install.tar
if ! test -f "$prefix/local_setup.bash"; then
  echo "missing relocated install prefix" >&2
  exit 1
fi
source "$prefix/local_setup.bash"
python3 test/check.py --prefix "$prefix" --input basic_fix.nmea --contracts-only
cmake -S test -B /tmp/ug-consumer/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ug-consumer/build --parallel 2
/tmp/ug-consumer/build/installed_consumer "$prefix/universal_gnss_mavros"
python3 test/check.py --prefix "$prefix" --input basic_fix.nmea
