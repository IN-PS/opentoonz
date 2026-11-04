# OpenToonz Mobile — Relatório de funcionalidades, testes e pendências

**Estado (honesto):** APKs debug e release **gerados** (v0.1.0, arm64-v8a,
`android/releases/v0.1.0/`), assinados e verificados (apksigner v2/v3), com
todas as dependências nativas resolvidas. **Ainda não foram instalados nem
executados num aparelho ARM64** — o port só pode ser considerado concluído
após esse teste. Ver §4.

Legenda de verificação:
* **T-host** — testado automaticamente (`otm_tests`, 126 verificações) com o
  mesmo código C++ do APK, compilado para Linux x86_64;
* **C-arm64** — o código compila para arm64-v8a / bionic / API 26 com o NDK;
* **C-java** — o código Java compila, gera dex e é empacotado num APK assinado
  (`SKIP_NATIVE=1`), mas não foi executado;
* **Não testado em aparelho** — vale para *todas* as linhas: nenhum APK com
  bibliotecas nativas foi instalado/executado ainda.

## 1. Funcionalidades obrigatórias

| Requisito | Implementação | Verificação |
|---|---|---|
| Editor de animação 2D | `EditorActivity` + motor (`Engine`) | C-java, motor T-host |
| Desenho quadro a quadro | desenhar numa célula vazia cria um desenho novo; botões novo/duplicar/hold/limpar/excluir quadro | T-host (6 quadros, holds, duplicar, excluir) |
| Timeline com frames | `TimelineView`: camadas × quadros, número do desenho, barra de *hold*, cabeçalho, playhead | C-java (dados vêm de `getCell`, T-host) |
| Camadas de animação | Toonz Raster e Raster; ordem, visibilidade, bloqueio, opacidade, renomear, excluir (com undo) | T-host |
| Onion Skin | N quadros para trás/frente, opacidade decrescente, vermelho/verde (`TRop::applyColorScale`), ignora *holds* | T-host (cores medidas: anterior (255,175,175), seguinte (170,226,179)) |
| Pincel | `RasterStrokeGenerator` (motor do Brush do OpenToonz), pressão (curva do desktop), antisserrilhado/pencil, tamanho mín/máx | T-host |
| Borracha | `RasterStrokeGenerator` ERASE: linhas, áreas, ambos | T-host |
| Preenchimento | `fill()`/`inkFill()` do toonzlib: áreas, linhas, ambos, profundidade, só vazias; RGBA em camadas raster | T-host (área fechada pintada, exterior intacto) |
| Seleção e transformação | seleção retangular flutuante; mover (1 dedo), escalar+girar (2 dedos), aplicar/cancelar/excluir, undo | T-host (mover 200,100; escala 2× + 90°; excluir; cancelar) |
| Importar PNG, JPG, sequências | Android `BitmapFactory` (PNG/JPG) → camada raster ajustada à tela; sequência ordenada por nome → quadros consecutivos; no motor também `tiio_png` | PNG e sequência T-host; JPG depende do decodificador do Android (não testado) |
| Exportar GIF | codificador GIF89a próprio (median-cut + LZW, loop) | T-host; decodificado com Pillow: 8 quadros, 100 ms, diferença máx. 7/255 vs PNG |
| Exportar MP4 | `MediaCodec` H.264 + `MediaMuxer`, quadros renderizados direto nos planos YUV | **C-java apenas** (a API de codec não existe fora do Android) |
| Gerenciamento de projetos | lista com miniaturas, criar (tamanho/fps), abrir, renomear, excluir | C-java; salvar/abrir T-host |
| Salvamento automático | a cada 30 s se modificado, ao pausar/sair; incremental; em thread de fundo | T-host (salvar/abrir em thread de trabalho; incremental 35 ms vs 3,2 s completo) |

Extras: undo/redo (TUndoManager), paleta com estilos (editar cor recolore os
desenhos Toonz Raster), conta-gotas de estilo, reprodução, atalhos de teclado
físico, exportação de sequência PNG, interface em português e inglês.

## 2. Interface móvel

| Requisito | Implementação |
|---|---|
| Barra de ferramentas lateral | coluna à esquerda: pincel, borracha, preenchimento, seleção, conta-gotas, onion (toque longo = ajustes) |
| Canvas central | `CanvasView`: bitmap do tamanho do documento, atualização por retângulo sujo |
| Timeline inferior | barra de quadros (anterior/play/próximo/novo/duplicar/hold/limpar/excluir) + `TimelineView` |
| Painel de camadas | gaveta à direita (botão no topo) |
| Zoom e movimentação | pinça com 2 dedos (zoom 0,05×–40×) e arraste; com caneta, 1 dedo também move |
| Caneta digital | pressão (`getPressure`), `TOOL_TYPE_STYLUS/ERASER`, cursor em *hover*, rejeição de palma (após detectar caneta, dedos só navegam) |
| Horizontal e vertical | `configChanges` tratados; layout em blocos que se adapta às duas orientações |

## 3. Testes executados

`android/docs/evidencias/otm_tests.log` (126/126) — resumo:

| Teste obrigatório | Resultado |
|---|---|
| Abrir e salvar projetos | OK (T-host): TLV relido pelo leitor do OpenToonz (4 quadros), render idêntico após recarregar (tolerância 2/255 só em camada PNG com alfa), projeto corrompido → erro tratado |
| Animações com múltiplos quadros | OK (T-host) |
| Onion Skin | OK (T-host) |
| Camadas | OK (T-host) |
| Renderização | OK (T-host): frame 1280×720 com onion 4,6 ms; retângulo 64×64 0,02 ms (CPU x86 do servidor) |
| Importação e exportação | PNG/sequência/GIF/PNG-seq OK (T-host); JPG e MP4 não testados |
| Consumo de RAM | 120 quadros 1280×720 CM32: +31 MB de RSS (sem compressão seriam 422 MB só de desenhos) |
| Estabilidade | após a correção de coerência do `TImageCache`: 0 falhas em 11 execuções completas (antes: 2 falhas intermitentes em 22 execuções) |
| Instalação/execução em aparelho ARM64 | **NÃO REALIZADO** (ver §4) |

Os números de tempo são do servidor de build (x86_64); num celular ARM de
entrada espera-se algo de 5 a 10× mais lento — ainda não medido.

## 4. Build do APK e o que falta

* **Qt5Core 5.15.13 para Android** compilado a partir do tarball do Ubuntu
  (com autorização do usuário), com uma correção: `backtrace()` desativado em
  Android (só existe a partir da API 33). `memfd_create` e
  `__pthread_cond_timedwait_relative` aparecem como referências *weak* em
  `libQt5Core` — seguras no Android 8.
* Verificação de símbolos: todo símbolo indefinido de `libotmengine.so`,
  `liblzo*.so` e `libc++_shared.so` resolve nas dependências empacotadas ou
  nas bibliotecas do sistema da API 26.
* **Não executado ainda**: nenhum aparelho/emulador disponível neste ambiente.
  Pontos de risco que só o aparelho confirma: criação do `QCoreApplication`
  dentro de um app não-Qt, execução dos auxiliares LZO a partir de
  `nativeLibraryDir`, MediaCodec (MP4), decodificação de JPG, desempenho.

Problemas de ambiente encontrados:

1. **NDK oficial indisponível** (`dl.google.com` bloqueado). Usei o *sysroot*
   de um NDK r27c de um espelho não oficial no GitHub, com o clang 18 do
   Ubuntu. Serve para checar compilação, mas o `libc++_shared.so` desse NDK
   **não deve ir num APK distribuído** — use o NDK oficial.
2. **Sem aparelho nem emulador** (imagens de sistema também vêm de
   `dl.google.com`).

Os APKs desta versão foram compilados com esse *sysroot* não oficial (inclui
`libc++_shared.so` dele). Para uma distribuição pública, recompile com o NDK
oficial (`android/docs/COMPILACAO.md`).

## 5. Recursos do OpenToonz ainda incompatíveis / não portados

* Cena `.tnz` completa e projetos do desktop (o app grava `.tlv`/`.tpl`/`.png`
  compatíveis, mas não a cena; abrir `.tnz` do desktop não é suportado).
* Níveis e ferramentas **vetoriais** (`.pli`, Geometric tool, *tape*, *inbetween*
  vetorial) — o código de vetor do tnzcore está compilado, falta a
  rasterização em CPU (no desktop é OpenGL).
* Pincel MyPaint / pincel "blurred" (hardness < 100) e pincéis personalizados.
* Sistema de FX (`tnzbase`, `stdfx`, `colorfx`), schematic, render farm.
* Pegs/câmera/animação de transformação (`TStageObject`), curvas de função,
  esqueleto/IK, deformação *plastic* (`tnzext`, SuperLU/BLAS).
* Áudio (colunas de som, lip-sync), stop motion, cleanup/scanner.
* Outros formatos: TIFF, PSD, TGA, BMP, SVG, WebM/AVI/APNG (ffmpeg).
* Seleção laço/poligonal (só retangular), régua, grade/guia de câmera.
* Interface Qt do desktop (`toonz`, `toonzqt`) — substituída pela UI móvel.

## 6. Arquivos de origem alterados no OpenToonz

6 arquivos, 52 linhas, todas sob macros `OTM_*`/`__ANDROID__` (sem efeito no
build desktop): `include/tgl.h`, `include/toonz/fill.h`,
`common/tsystem/tsystem.cpp`, `common/tsystem/tsystempd.cpp`,
`common/tiio/tiio.cpp`, `common/trasterimage/tcodec.cpp`.
