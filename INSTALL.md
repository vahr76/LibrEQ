# Instalar LibrEQ

| Sistema | Archivo | Formatos | Requisitos |
|---|---|---|---|
| Windows | `LibrEQ-*-Windows-x64-Setup.exe` | VST3 | Windows de 64 bits |
| macOS | `LibrEQ-*-macOS.pkg` | AU y VST3 | macOS 11 o superior, Apple Silicon o Intel |
| Linux | `LibrEQ-*-Linux-x86_64.tar.gz` | VST3 | x86_64, con glibc 2.35 o superior |

Logic Pro y GarageBand usan AU. Pro Tools no es compatible.

Los instaladores todavía no están firmados, así que Windows y macOS muestran una advertencia.

## Windows

1. Abre el instalador. Si aparece "Windows protegió su PC", elige `Más información` y `Ejecutar de todas formas`.
2. Confirma los permisos de administrador.
3. Vuelve a escanear los plugins en tu DAW. El plugin queda en `C:\Program Files\Common Files\VST3`.

Para desinstalarlo, usa `Configuración > Aplicaciones`.

## macOS

1. Abre el `.pkg`. Si macOS lo bloquea, ve a `Ajustes del Sistema > Privacidad y seguridad` y elige `Abrir igualmente`.
2. Sigue el instalador e ingresa la contraseña del Mac.
3. Abre Logic. LibrEQ aparece en `Audio Units`, dentro del menú de efectos de una pista. Si no aparece, revísalo en el `Gestor de plugins`.

Para desinstalarlo, borra `LibrEQ.component` de `/Library/Audio/Plug-Ins/Components` y `LibrEQ.vst3` de `/Library/Audio/Plug-Ins/VST3`.

## Linux

```bash
tar -xzf LibrEQ-*-Linux-x86_64.tar.gz
./LibrEQ-*-Linux-x86_64/install.sh
```

No uses `sudo`. El plugin queda en `~/.vst3`; luego vuelve a escanear los plugins en tu DAW. Para desinstalarlo, borra `~/.vst3/LibrEQ.vst3`.
