#!/usr/bin/env bash
# Empaqueta TechnoVisuals en un zip portable para Windows.
# Uso (desde MSYS2 UCRT64 o Git Bash con msys64 en PATH): ./package_release.sh v0.1.0
set -euo pipefail
VER="${1:-v0.1.0}"
BIN_DIR="${MINGW_BIN:-/c/msys64/ucrt64/bin}"
OUT="dist/TechnoVisuals-$VER-win64"
rm -rf "$OUT"; mkdir -p "$OUT"

cp build/app.exe "$OUT/TechnoVisuals.exe"
cp -r shaders presets post_effects "$OUT/"
[ -d procedural ] && cp -r procedural "$OUT/"
cp visual_controls.json "$OUT/" 2>/dev/null || true

# Copia recursiva de las DLL no-sistema (las de Windows y vulkan-1 se omiten)
declare -A seen
resolve() {
  for dll in $(objdump -p "$1" | awk '/DLL Name/{print $3}'); do
    low="${dll,,}"
    [[ -n "${seen[$low]:-}" ]] && continue
    seen[$low]=1
    if [ -f "$BIN_DIR/$dll" ]; then
      cp "$BIN_DIR/$dll" "$OUT/"
      resolve "$BIN_DIR/$dll"
    elif [ -f "build/$dll" ]; then
      cp "build/$dll" "$OUT/"; resolve "build/$dll"
    fi
  done
}
resolve build/app.exe

cat > "$OUT/LEEME.txt" <<TXT
TechnoVisuals $VER
PRIMER PASO: al abrir, selecciona la carpeta raiz de tus videos (control Folder). Sin eso las fuentes quedan en negro.
Ejecuta TechnoVisuals.exe desde su propia carpeta. Requiere GPU con soporte Vulkan y drivers actualizados.
Si falta algo, instala el Visual C++ Redistributable o actualiza el driver de video.
TXT

python - "$OUT" <<'PY'
import sys, os, zipfile
out = sys.argv[1]
z = zipfile.ZipFile(out + ".zip", "w", zipfile.ZIP_DEFLATED)
base = os.path.dirname(out)
for root, _, files in os.walk(out):
    for f in files:
        full = os.path.join(root, f)
        z.write(full, os.path.relpath(full, base).replace(os.sep, "/"))
z.close()
PY
echo "Listo: dist/TechnoVisuals-$VER-win64.zip"
ls "$OUT" | grep -ci dll | xargs echo "DLLs incluidas:"
