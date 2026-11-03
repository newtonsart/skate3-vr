<picture>
  <source media="(prefers-color-scheme: dark)" srcset="banner.png">
  <source media="(prefers-color-scheme: light)" srcset="banner-light.png">
  <img alt="Skate 3 VR" src="banner-light.png">
</picture>

# Skate 3 VR

Juega a **Skate 3 en realidad virtual**, con profundidad 3D, seguimiento de
cabeza y tu mando habitual. Basado en [Skate3Recomp](https://github.com/mchughalex/skate3recomp).

**Versión experimental:** probado en Windows con Quest 3 por cable Link.
Otros visores OpenXR todavía no están comprobados. El juego y sus archivos
no están incluidos: necesitas tu propia copia de Xbox 360.

## Empezar a jugar

1. Conecta el visor al PC y entra en **Quest Link**. La aplicación Meta Link
   debe estar seleccionada como runtime OpenXR activo.
2. Conecta un **mando Xbox o PlayStation**.
3. Haz doble clic en **[Jugar-VR.cmd](Jugar-VR.cmd)**, en la carpeta del proyecto.
4. Elige un modo en la ventana que aparece:

| Botón | Qué verás |
| --- | --- |
| **VR inmersiva** | El mundo del juego a tu alrededor; puedes mirar con la cabeza. |
| **Ventana 3D** | Una pantalla flotante con el encuadre de escritorio y profundidad 3D. |
| **Ventana 3D + passthrough** | La pantalla 3D sobre la imagen de tu habitación, si Meta Link lo permite. |

**No necesitas escribir comandos para jugar.** El lanzador usa la compilación
de `out/build/relwithdebinfo` y el juego extraído en `game/`.
Si faltan, muestra un aviso: sigue la [guía de preparación y compilación VR](docs/PCVR_BUILD.md#build-the-openxr-game-on-windows).
Clonar el repositorio no instala ni compila el juego automáticamente.

## Controles y ajustes

El juego se controla con el mando original; no utiliza los mandos Touch.

| Control | Acción |
| --- | --- |
| **Start** | Pausar el juego. |
| **RB + Start** | Abrir los ajustes del mod, también visibles en el visor. |
| **F1 / Escape** | Abrir los ajustes con el teclado. |
| **Ctrl + F8** | Recentrar la vista o colocar la ventana delante de ti. |
| **Ctrl + F11** | Cambiar entre los tres modos sin cerrar el juego. |

En **RB + Start → Video** puedes cambiar el modo VR, la calidad de imagen,
la posición y el tamaño de la ventana, la cámara y el HUD. Los ajustes se guardan.
Si tu mando PlayStation no responde, selecciona **Controls → Controller Backend
→ SDL** y reinicia. [Más sobre ajustes y cámara](docs/PCVR_SETTINGS.md).

## Mejorar la imagen

En **Video → VR Render Resolution**, empieza por **HMD 100%**.

- **HMD 75%:** reduce la carga gráfica.
- **HMD 100%:** renderiza a la resolución recomendada por el visor.
- **HMD 125%:** añade supersampling y cuesta más rendimiento.
- **Desktop (compatibility):** usa la resolución de escritorio.

El lanzador mantiene tus ajustes de calidad y usa MSAA desactivado para seguir
la configuración VR probada. Las texturas originales no cambian.
[Detalles de calidad de imagen](docs/PCVR_IMAGE_QUALITY.md).

## Ver la habitación con passthrough

En Meta Link del PC, abre **Configuración → Developer** (o **Beta**):

1. Activa **Developer runtime features**.
2. Activa **Pass-through over Meta Quest/Horizon Link** y acepta el aviso de Meta.
3. Entra en Quest Link y vuelve a abrir el juego con **Ventana 3D + passthrough**.

Si Meta no ofrece esta función, la ventana aparece sobre fondo negro.
**La imagen de las cámaras sigue pendiente de validación en este proyecto.**
[Configuración y diagnóstico de passthrough](docs/PCVR_PASSTHROUGH.md).

## Partidas guardadas

El lanzador conserva la carpeta usada en las pruebas VR: **`out/runtime-test/`**.
Ahí se guardan tu perfil, partidas y ajustes; no la borres al actualizar.
Si antes usabas otra carpeta de usuario, el lanzador no migra esas partidas.
Abrir `skate3.exe` directamente puede usar una carpeta de usuario diferente.

## Si algo falla

- **No arranca o falta un archivo:** comprueba que existe la compilación VR y
  que has preparado el juego y su actualización siguiendo la [guía VR](docs/PCVR_BUILD.md).
- **El visor no muestra el juego:** entra en Quest Link antes de lanzarlo y
  comprueba el runtime OpenXR activo en Meta Link.
- **La ventana queda fuera de tu vista:** mira al frente y pulsa **Ctrl + F8**.
- **El menú de pausa se ve mal:** prueba **Video → VR Pause View → Flat panel**.

La ventana 3D acaba de recibir una corrección de encuadre y necesita otra prueba
en el visor. También faltan pruebas amplias de mapas, editores y rendimiento;
no se garantizan 72/90 FPS en todo el juego.

## Desarrollo y créditos

El desarrollo VR está en **`main`** de [skate3-vr](https://github.com/newtonsart/skate3-vr).

- [Compilar y diagnosticar OpenXR](docs/PCVR_BUILD.md)
- [Rendimiento y mediciones](docs/PCVR_PERFORMANCE.md)
- [Estado del proyecto y tareas pendientes](docs/PCVR_ROADMAP.md)
- [Documentación de escritorio, DLC y compilación del proyecto base](docs/DESKTOP.md)

Créditos a [Skate3Recomp y sus colaboradores](https://github.com/mchughalex/skate3recomp),
al [SDK ReXGlue](https://github.com/rexglue/rexglue-sdk) y a
[Xenia](https://github.com/xenia-project/xenia), cuya investigación y herramientas
han contribuido al ecosistema de recompilación.
