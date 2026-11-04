# LibrEQ

EQ paramétrico original, libre y comunitario para Linux, C++20 + JUCE + CMake: la música es de todos y para todos.

## Estado

Implementado:

- Hasta 24 bandas: Bell, Low Shelf, High Shelf, High Pass, Low Pass (12 a 48 dB/oct) y Notch.
- Gráfica interactiva con la curva real de los filtros y analizador FFT de entrada y salida.
- Automatización, cambios de parámetros suavizados, ganancia de salida, bypass y estado guardable. El callback de audio no asigna memoria.

Controles de la gráfica: doble clic agrega o elimina una banda, arrastrar mueve el nodo, la rueda cambia Q, Alt+clic activa o desactiva, clic derecho abre opciones y Supr elimina.

Pendiente: presets, deshacer, EQ dinámica, Mid/Side, modos Natural y Linear Phase y pruebas en más hosts.

## Compilar

Requiere CMake 3.22+, un compilador C++20 y Git. En Linux también hacen falta las [dependencias de JUCE](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md). JUCE 8.0.10 se descarga automáticamente:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target LibrEQ_VST3 LibrEQDspTests LibrEQUiCheck -j2
ctest --test-dir build -C Release --output-on-failure
```

El VST3 se genera en `build/LibrEQ_artefacts/Release/VST3/LibrEQ.vst3`. En REAPER: Preferences > Plug-ins > VST, añade esa carpeta y ejecuta rescan.

`LibrEQUiCheck` prueba el procesador y la interfaz sin DAW; en Linux necesita una pantalla, real o `xvfb-run`. Con una ruta como argumento, guarda una captura en PNG.
