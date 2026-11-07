# LibrEQ

EQ paramétrico original, libre y comunitario para Linux, C++20 + JUCE + CMake: la música es de todos y para todos.

## Estado

Implementado:

- Hasta 24 bandas: Bell, Low Shelf, High Shelf, High Pass, Low Pass (12 a 48 dB/oct) y Notch. Filtros ajustados a la respuesta analógica, para que los agudos no se compriman cerca de Nyquist.
- Por banda: estéreo, izquierda, derecha, Mid o Side, y solo de banda.
- Dos modos: latencia cero o fase lineal. A 44,1 o 48 kHz, la fase lineal añade 4096 muestras (unos 85 ms a 48 kHz) y la informa al host.
- EQ dinámica por banda: con `Dynamic` activo, la banda aplica su ganancia solo cuando el nivel en su rango supera el umbral. En fase lineal, las bandas dinámicas se procesan con latencia cero.
- Global: escala de la curva, ganancia automática, ganancia de salida y bypass sin clic, con compensación de latencia.
- Deshacer y rehacer, presets de fábrica y de usuario, y comparación A/B. El estado A/B y el modo del analizador se guardan con el proyecto.
- Gráfica interactiva con la curva real de los filtros, analizador FFT de entrada y salida, medidor de picos y lectura de nota bajo el cursor.
- Automatización y parámetros suavizados. El audio no asigna memoria ni usa bloqueos en el callback.

Edición: Mayús hace los arrastres más finos, arrastrar sobre una zona vacía selecciona varias bandas, Ctrl+clic (Cmd en macOS) suma o quita bandas y Ctrl+A selecciona todas. La escala vertical (±6, ±12 o ±30 dB) y el tamaño de la ventana (75 a 150 %) se eligen en la cabecera y se guardan con el proyecto. Los valores del panel se pueden escribir, por ejemplo `1.5k`.

Controles de la gráfica: doble clic agrega o elimina una banda, arrastrar mueve el nodo, la rueda o el anillo del nodo cambian Q, Alt+clic activa o desactiva, clic derecho abre opciones, Supr elimina, Ctrl+Z deshace y Ctrl+Mayús+Z rehace. Algunos hosts pueden reservar esos atajos; los botones `Undo` y `Redo` siempre funcionan.

Los presets de usuario se guardan como archivos `.librq` en `~/.config/LibrEQ/Presets` en Linux.

La interfaz incluye la tipografía [Inter](assets/fonts/LICENSE-Inter.txt), bajo licencia SIL OFL 1.1.

Pendiente: sidechain externo, firma de instaladores, *releases*, formatos CLAP y LV2, y pruebas en más hosts.

## Instalar

Consulta [INSTALL.md](INSTALL.md). GitHub Actions compila, instala y valida los paquetes para Windows, macOS y Linux en cada cambio.

## Compilar

Requiere CMake 3.22+, un compilador C++20 y Git. En macOS se genera además la versión AU. En Linux también hacen falta las [dependencias de JUCE](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md). JUCE 8.0.10 se descarga automáticamente:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target LibrEQ_VST3 LibrEQDspTests LibrEQUiCheck -j2
ctest --test-dir build -C Release --output-on-failure
```

El VST3 se genera en `build/LibrEQ_artefacts/Release/VST3/LibrEQ.vst3`. En REAPER: Preferences > Plug-ins > VST, añade esa carpeta y ejecuta rescan.

`LibrEQUiCheck` prueba el procesador y la interfaz sin DAW; en Linux necesita una pantalla, real o `xvfb-run`. Con una ruta como argumento, guarda una captura en PNG.
