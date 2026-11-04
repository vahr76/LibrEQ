#!/usr/bin/env bash
# Ejecuta la versión de REA fijada para LibrEQ. Uso: tools/rea.sh <comando> [args]
# Las rutas a binarios deben ser absolutas: REA se lanza desde fuera del repositorio.
set -euo pipefail

REA_VERSION="6.8.0" # Actualizar a propósito, revisando el CHANGELOG de REA.

if ! command -v npx >/dev/null 2>&1; then
  echo "rea.sh: se necesita Node.js con npx (REA ${REA_VERSION} pide ^22.23.4, ^24.22.1 o >=26.12.1)." >&2
  exit 127
fi

# REA recomienda obtener el paquete desde un directorio ajeno al proyecto analizado.
cd "${REA_WORKDIR:-$HOME}"
exec npx -y "rea-agents@${REA_VERSION}" "$@"
