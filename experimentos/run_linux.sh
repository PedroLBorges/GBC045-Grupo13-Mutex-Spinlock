#!/bin/sh
# Roda os testes e o benchmark completo no Linux/FreeBSD.
# Uso: sh experimentos/run_linux.sh        (a partir da raiz do repositorio)
set -e
cd "$(dirname "$0")/.."

OS=$(uname -s | tr '[:upper:]' '[:lower:]')   # linux ou freebsd
make -C code
./code/test_platform

# registra o ambiente junto dos dados
{
  date
  uname -a
  if [ -r /proc/cpuinfo ]; then grep -m1 "model name" /proc/cpuinfo; fi
  if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
    echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
  fi
} > "dados/${OS}.ambiente.txt"

./code/bench --out "dados/${OS}.csv" "$@"
