# REA para análisis de referencias

LibrEQ puede usar [REA](https://github.com/morluto/rea) como herramienta opcional de desarrollo para inspección estática y análisis documentado de aplicaciones o binarios. REA no forma parte del VST3, no se carga en tiempo real y no se incluye como dependencia del plugin.

## Uso

Con Node.js 22+ instalado:

```bash
npx -y rea-agents@latest --help
```

Para analizar un binario, seguir la guía oficial de REA y usar una copia del archivo fuera del árbol de código fuente. Con binarios propietarios, limitar el trabajo a análisis autorizado, comportamiento observable y documentación de ideas generales de UX o arquitectura. No copiar código, recursos, claves, licencias ni implementaciones propietarias en LibrEQ.

## Alcance en LibrEQ

REA puede ayudarnos a:

- Inventariar formato, arquitectura, imports, strings y certificados.
- Documentar evidencia de comportamiento de una referencia autorizada.
- Comparar conceptos de interacción y convertirlos en diseño original.

El resultado esperado es una especificación propia para LibrEQ, no una extracción de código de terceros.
