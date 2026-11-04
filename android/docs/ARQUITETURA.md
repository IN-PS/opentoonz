# OpenToonz Mobile — Engenharia reversa e arquitetura do port

Este documento registra a análise da arquitetura do OpenToonz (código-fonte
deste repositório, licença BSD modificada) e as decisões de adaptação para
Android. Números medidos sobre a árvore `toonz/sources` deste branch.

## 1. Arquitetura do OpenToonz desktop

| Módulo | Arquivos C/C++ | Linhas | Papel |
|---|---:|---:|---|
| `common/` → **tnzcore** | 184 | 114 576 | Tipos base: `TRaster*`, `TImage`, `TToonzImage` (CM32), `TVectorImage`, `TStroke`, `TPalette`/`TColorStyle`, `TRop` (operações raster), `TImageCache`, `TFilePath`/`TSystem`, persistência `TOStream`/`TIStream`, `TUndoManager`, codecs (`tcodec`) |
| `tnzbase` | 13 | 5 332 | Sistema de FX (`TRasterFx`), parâmetros animáveis, curvas |
| `tnzext` | 36 | 10 555 | Deformação *plastic*/mesh (usa SuperLU/BLAS) |
| **toonzlib** | 161 | 86 102 | Modelo de cena: `ToonzScene`, `TXsheet` (colunas × linhas de células), `TXshSimpleLevel`, `TLevelSet`, câmera, `TStageObject` (pegs/animação de transformações), renderização (`stage`, `movierenderer`), pincel raster (`RasterStrokeGenerator`, `rasterbrush`), preenchimento (`fill.cpp`), undo por tiles (`ttilesaver`) |
| `image` | 53 | 39 559 | Leitura/escrita de formatos (TLV/TZL, PLI, PNG, TIFF, PSD, ffmpeg → MP4/GIF/WebM) |
| `toonzqt` | 98 | 76 879 | Widgets Qt reutilizáveis (paleta, timeline, painéis) |
| `tnztools` | 91 | 77 793 | Ferramentas interativas (brush, eraser, fill, seleção...) acopladas a `TTool`, viewer OpenGL e eventos de mouse/tablet Qt |
| `toonz` | 207 | 192 083 | Aplicação desktop (janelas, menus, comandos, viewer, xsheet) |
| `stdfx`/`colorfx` | 212 | 80 779 | Efeitos |
| `sound`, `stopmotion`, `toonzfarm`, `tcomposer`, `tcleanupper` | — | — | Áudio, captura, render farm, renderizador CLI, cleanup |

Dependências externas do desktop: Qt 5 (Core, Gui, Widgets, OpenGL, Network,
Script, Svg, Multimedia, PrintSupport, SerialPort), OpenGL legado + GLEW +
GLUT, Boost (headers), libpng, libjpeg-turbo, libtiff, LZ4, LZO (executáveis
`lzocompress`/`lzodecompress` separados), SuperLU/OpenBLAS, libmypaint,
libusb, OpenCV (opcional), ffmpeg (executável externo).

### 1.1 Sistema de animação
* **Xsheet** (`TXsheet`): grade colunas × linhas. Cada célula `TXshCell`
  aponta para (nível, `TFrameId`). Repetir o mesmo `TFrameId` em linhas
  consecutivas = *hold* (exposição estendida).
* **Níveis** (`TXshSimpleLevel`): sequência de desenhos (frames) de um tipo:
  *Toonz Raster* (`TToonzImage`, pixels CM32 = tinta + pintura + tom,
  indexados numa `TPalette`), *Raster* (`TRasterImage` RGBA) ou *Vector*.
* **Paleta**: estilos com id; alterar a cor de um estilo recolore todos os
  desenhos Toonz Raster que o usam.
* **Onion skin** (`OnionSkinMask`): desenhos vizinhos sobrepostos com
  sombreamento de cor (`TRop::applyColorScale` no caminho raster).
* **Undo**: `TUndoManager` + `TTileSaverCM32/FullColor` (guarda tiles 64×64
  antes da alteração).

### 1.2 Renderização
* Viewer interativo: OpenGL **legado** (`glBegin/glVertex`, display lists,
  `tgl.cpp`, `TOfflineGL`) — 61 arquivos usam a API de modo imediato.
* Render final: `TRasterFx` sobre `TTile`s em CPU, mais rasterização de
  vetores via contexto OpenGL offscreen.

### 1.3 Gerenciamento de projetos
* `TProject` (pasta de projeto + *sandbox*), `ToonzScene` (`.tnz`, XML via
  `TOStream`), níveis em arquivos separados (`.tlv` + paleta `.tpl`, `.pli`,
  sequências `nome.0001.png`).

## 2. Componentes dependentes de desktop (bloqueadores no Android)

| Componente | Problema no Android | Decisão |
|---|---|---|
| `toonz`, `toonzqt` (QWidget, docking, menus, mouse) | UI de desktop; Qt Widgets em telas pequenas e sem mouse | **Substituída** por UI touch nativa Android (Java, `app/`) |
| OpenGL legado (`tgl.cpp`, `TOfflineGL`, viewer, `tglcurves`, `tglregions`) | Android só tem OpenGL ES (sem `glBegin`, display lists, GLU, GLUT) | Renderização **em CPU** com `TRop` (o mesmo código do render final). Chamadas GL restantes do núcleo resolvidas por *stubs* gerados (`engine/compat/otm_glstub.h`, `tgl_stub.cpp`) |
| `QtGui`/`QtNetwork` em `tsystem.cpp` (`QDesktopServices`, `QHostInfo`) | Não usados no app móvel | Guardas `OTM_NO_QTGUI` (equivalente em `QSysInfo`) |
| `Preferences` (toonzlib, QSettings + dezenas de dependências) em `fill.h` | Arrasta o módulo de preferências inteiro | Guarda `OTM_NO_PREFERENCES` com os **valores padrão** do desktop |
| `lzocompress`/`lzodecompress` em `applicationDirPath()` | Executáveis precisam ir no APK (`lib*.so`) | Guarda `OTM_LZO_ENV`: caminho via variável de ambiente; compilados para arm64 e empacotados como `liblzocompress.so`/`liblzodecompress.so` |
| `sys/dir.h` em `tsystempd.cpp` | Não existe no bionic | Guardado por `__ANDROID__` |
| `tiio_jpg.h` incluído (sem uso) em `tiio.cpp` | Exigiria libjpeg | Guarda `OTM_NO_JPEG`; JPEG decodificado pelo `BitmapFactory` do Android |
| ffmpeg externo (MP4/GIF/WebM) | Não há executável ffmpeg no Android | MP4 via **MediaCodec/MediaMuxer**; GIF via codificador próprio (`gifencoder.cpp`) |
| Qt Script, Multimedia, SerialPort, libusb, câmera (stop motion), farm | Fora do escopo móvel | Não portados |
| `tnzbase`/`stdfx` (FX), `tnzext` (plastic: SuperLU/BLAS) | Grandes, dependem de GL e BLAS | Não portados nesta versão |
| `ToonzScene`/`TXsheet` completos (toonzlib) | Puxam FX DAG, stage, câmera, sound, script… | Modelo xsheet **reduzido** no motor (`document.h`), mesmos conceitos |

Todas as mudanças nos fontes originais estão em 6 arquivos (52 linhas,
`git diff 572d59a -- toonz/sources`) e só têm efeito quando as macros
`OTM_*` são definidas — o build desktop não muda.

## 3. Arquitetura do OpenToonz Mobile

```
┌──────────────── APK (arm64-v8a, Android 8.0+ / API 26) ────────────────┐
│ Java (app/)  ProjectsActivity · EditorActivity · CanvasView ·            │
│              TimelineView · LayerPanel · Mp4Exporter (MediaCodec)        │
│                │ JNI (NativeEngine, 69 métodos)                           │
│ libotmengine.so ── engine/src: Document · Compositor · Tools · ProjectIO │
│                │                 · GifEncoder · Engine (fachada, mutex)   │
│                ├─ OpenToonz (101 arquivos, ~75 mil linhas, sem fork):     │
│                │   tnzcore (raster, CM32, paleta, TRop, TImageCache,      │
│                │   TUndo, TOStream, TFilePath, vetores…)                  │
│                │   toonzlib: RasterStrokeGenerator, rasterbrush, fill,    │
│                │   ttilesaver/ttileset   ·  image/tzl (TLV), image/png      │
│                ├─ thirdparty do repositório: LZ4, libpng 1.6.21          │
│ libQt5Core.so (Qt 5.15, só QtCore)   libc++_shared.so                    │
│ liblzocompress.so / liblzodecompress.so (LZO 2.03 do thirdparty)          │
└──────────────────────────────────────────────────────────────────────────┘
```

### 3.1 O que é reaproveitado do OpenToonz (código original, sem cópia)
* **Pincel** (Toonz Raster): `RasterStrokeGenerator` + `rasterBrush`, o mesmo
  motor do *Brush* (modo pencil/hardness 100) do desktop, com a mesma curva de
  pressão (`computeThickness`).
* **Borracha**: `RasterStrokeGenerator` modo `ERASE` (linhas, áreas, ambos),
  como o *Eraser* normal do desktop.
* **Preenchimento**: `fill()` / `inkFill()` de `toonzlib/fill.cpp` (áreas,
  linhas, profundidade de preenchimento, "somente vazias"); `fill()` RGBA para
  camadas raster.
* **Seleção/transformação**: `TRop::resample` (CM32→CM32) + `TRop::over`.
* **Composição e onion skin**: `TRop::convert` (CM32 + paleta → RGBA),
  `TRop::over`, `TRop::applyColorScale` (sombreamento do onion).
* **Undo/redo**: `TUndoManager` + `TTileSaverCM32/FullColor`.
* **Memória**: `TImageCache` (desenhos inativos comprimidos em LZ4 na RAM).
* **Arquivos**: `.tlv` (codec TZL/LZO do OpenToonz), paleta `.tpl`
  (`TPalette::saveData`), `.png` (`tiio_png`), descrição do projeto em XML
  com `TOStream`/`TIStream`.

### 3.2 Modelo de documento (xsheet reduzido)
* `Layer` = coluna do xsheet com exatamente um nível; `cells[row]` = número do
  desenho (`TFrameId`), 0 = vazio, repetição = *hold*.
* Tipos de camada: **Toonz Raster** (CM32 + paleta compartilhada) e
  **Raster** (RGBA, usado para imagens importadas; aceita pincel/borracha/fill).
* Coordenadas: a UI usa pixels com origem no canto superior esquerdo; o
  motor converte para o sistema do OpenToonz (y para cima).

### 3.3 Formato do projeto
```
<projeto>/project.otm          cena (XML TOStream): canvas, fps, camadas, células, onion
<projeto>/palette.tpl          paleta (formato .tpl do OpenToonz)
<projeto>/levels/<n>_<uid>.tlv nível Toonz Raster (abre no OpenToonz desktop) + .tpl
<projeto>/levels/<n>_<uid>.0001.png …  camadas raster (convenção de sequência do OpenToonz)
<projeto>/thumbnail.png
```
O `.tnz` completo do desktop **não** é gerado (ver lista de incompatibilidades).

### 3.4 Desempenho e memória (medidos no host, ver RELATORIO.md)
* Atualização por retângulo sujo durante o traço (64×64 px ≈ 0,02 ms).
* Desenhos fora da janela ±(onion+2) quadros são comprimidos no `TImageCache`.
* Salvamento incremental: níveis inalterados são reaproveitados por *hard
  link*; autosave a cada 30 s e ao pausar o app.
