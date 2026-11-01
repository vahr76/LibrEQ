# LibrEQ

EQ paramétrico original, libre y comunitario para Linux, C++20 + JUCE + CMake: la música es de todos y para todos.

Implementado: motor DSP desacoplado, 24 ranuras internas, 8 bandas parametrizadas en el estado, Bell/HP/LP/Notch, frecuencia/ganancia/Q, automatización y estado serializable. El procesamiento no asigna memoria en el callback.

Pendiente: shelves completos en el DSP, nodos interactivos, FFT real, presets, EQ dinámica, Mid/Side, Natural/Linear Phase y pruebas de host.

## Compilar

Requiere CMake 3.22+, compilador C++20 y Git. JUCE 8.0.10 se descarga automáticamente:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ArieloEQ ArieloEQSmokeTest -j2
./build/ArieloEQSmokeTest
```

El VST3 se genera en `build/ArieloEQ_artefacts/Release/VST3/Arielo EQ.vst3`. En REAPER: Preferences > Plug-ins > VST, añade esa carpeta y ejecuta rescan.
