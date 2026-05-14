# Build & Run Instructions

1. Instala las dependencias necesarias en Arch:

```bash
sudo pacman -Syu vulkan-icd-loader vulkan-validation-layers sdl2 portaudio
```

2. Crea el directorio de compilación y configura con CMake (el script también lo hace por ti):

```bash
mkdir -p build
cd build
cmake ..
make
./app
```

Para que cada vez sea más rápido ejecuta el script `./build_and_run.sh`, que reconfigura y lanza el binario.

Si quieres forzar las validation layers en tiempo de ejecución, ejecuta:

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/app
```
