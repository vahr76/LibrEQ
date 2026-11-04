# REA para análisis de referencias

LibrEQ puede usar [REA](https://github.com/morluto/rea) como herramienta opcional de desarrollo para inspección estática y análisis documentado de aplicaciones o binarios. REA no forma parte del VST3, no se carga en tiempo real y no se incluye como dependencia del plugin.

## Uso

`tools/rea.sh` ejecuta la versión de REA fijada para LibrEQ. Requiere Node.js `^22.23.4`, `^24.22.1` o `>=26.12.1`:

```bash
tools/rea.sh --help
tools/rea.sh inspect-artifact /ruta/absoluta/al/binario --json
```

Usar rutas absolutas: el script lanza REA desde `$HOME` (o `REA_WORKDIR`), fuera del repositorio. La versión solo se cambia editando `REA_VERSION` en el script.

## Conectar REA con un agente

Este paso es personal y modifica la configuración del agente de cada desarrollador. Revisar los cambios propuestos antes de aprobarlos y reiniciar el agente al terminar:

```bash
tools/rea.sh setup
```

No se versiona un `.mcp.json` en el repositorio, como recomienda la guía de instalación de REA.

## Análisis nativo profundo

Las inspecciones básicas, como `inspect-artifact` e `inspect-elf-notes`, funcionan sin instalar nada más. Para descompilar hace falta un motor, por ejemplo Ghidra 12.1.x con `GHIDRA_INSTALL_DIR` apuntando a su carpeta. Para comprobar el estado:

```bash
tools/rea.sh doctor --provider ghidra --json
```

## Normas de análisis

Para analizar un binario, seguir la guía oficial de REA y usar una copia del archivo fuera del árbol de código fuente. Con binarios propietarios, limitar el trabajo a análisis autorizado, comportamiento observable y documentación de ideas generales de UX o arquitectura. No copiar código, recursos, claves, licencias ni implementaciones propietarias en LibrEQ.

## Alcance en LibrEQ

REA puede ayudarnos a:

- Inventariar formato, arquitectura, imports, strings y certificados.
- Documentar evidencia de comportamiento de una referencia autorizada.
- Comparar conceptos de interacción y convertirlos en diseño original.

El resultado esperado es una especificación propia para LibrEQ, no una extracción de código de terceros.
