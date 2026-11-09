# OpenToonz Mobile v0.4.0 (pré-lançamento): pincéis MyPaint

## Novidades
* **Pincéis MyPaint** com a mesma biblioteca do OpenToonz desktop
  (libmypaint 1.6.0) e o mesmo adaptador (`MyPaintToonzBrush`):
  * **271 presets** do OpenToonz (classic, deevad, ramon, tanda, kaerhon,
    mojo, aotz, slos, experimental — domínio público/CC0), escolhidos numa
    grade com miniaturas (Pincel → botão "Pincel: …");
  * em camadas **Toonz Raster** o traço vira tinta do estilo atual (trocar
    a cor na paleta recolore); em camadas **Raster** pinta com a cor do
    estilo; presets de borracha apagam;
  * tamanho (−3..+3), pressão da caneta e estabilizador; desfazer/refazer.
* Inclui tudo da v0.3.0 (vetores, rig com hierarquia e pivô, efeitos por
  camada, várias trilhas de áudio, abrir `.tnz` do desktop, NDK oficial).

## Testes desta versão
* Motor nativo (mesmo C++ do APK, em x86_64): **233/233** verificações —
  inclui presets reais `classic/pen`, `classic/charcoal` (textura com tons
  parciais), `classic/ink_eraser`, tamanho, desfazer/refazer e camada Raster.
* App completo na JVM (Robolectric 3.8 / Android 8.1) com o motor real:
  **14/14** — inclui escolher o preset na grade, traço pela tela e voltar ao
  pincel OpenToonz.
* **Não testado em aparelho ARM64 real** (desempenho do MyPaint no celular,
  caneta, áudio e MP4 só um aparelho confirma).

## Limitações
* MyPaint não funciona em camadas vetoriais (no desktop também não); sem
  inclinação da caneta (tilt) e sem editor de parâmetros dos presets.
* Sem IK nem deformação *plastic*; `.tnz` importa parcialmente (ver v0.3.0).

## Arquivos
| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.4.0-release.apk` | versão otimizada (chave de release, certificado SHA-256 `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`) |
| `OpenToonzMobile-0.4.0-debug.apk` | com símbolos de depuração |

Android 8.0+ (API 26), arm64-v8a. Atualiza as versões anteriores (mesma chave
de release). Compilado com o NDK oficial r27c. Código de terceiros: libmypaint
(ISC), json-c (MIT).
