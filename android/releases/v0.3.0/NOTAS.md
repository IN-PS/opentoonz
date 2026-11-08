# OpenToonz Mobile v0.3.0 (pré-lançamento): vetores, rig, efeitos e cenas do desktop

## Novidades
**Desenho**
* **Camadas vetoriais** com o motor de vetores do OpenToonz (`TVectorImage`,
  salvas em `.pli`): pincel, formas, preenchimento de regiões, seleção e
  transformação; linhas nítidas em qualquer zoom.
* **Pincel texturizado**: grão de papel e dispersão ajustáveis.

**Animação**
* **Hierarquia de camadas** (pegs pai/filho) e **pivô** por camada: monte
  rigs de recorte (braço → antebraço → mão) e anime com cinemática direta.
* **Efeitos por camada**: desfoque, brilho (glow), sombra projetada,
  brilho/contraste/saturação/matiz.

**Som**
* **Várias trilhas de áudio** na timeline, com início e volume por trilha,
  mixadas no MP4.

**Arquivos**
* **Abrir cena `.tnz` do OpenToonz desktop** (escolha a pasta do projeto):
  câmera, fps, colunas TLV / PLI / PNG, exposições, visibilidade, bloqueio,
  opacidade e paletas. Mostra o que não pôde ser importado.

**Build:** compilado com o **NDK oficial r27c** (Qt5Core também).

## Testes desta versão
* Motor nativo (mesmo C++ do APK, em x86_64): **220/220** verificações.
* App completo na JVM (Robolectric 3.8 / Android 8.1) ligado ao motor real:
  **13/13** — inclui camada vetorial pela interface, efeitos, hierarquia,
  pincel texturizado, duas trilhas de áudio mixadas e abrir uma cena `.tnz`.
* A cena `.tnz` dos testes foi **gravada pelas bibliotecas do OpenToonz
  desktop** (compiladas deste repositório). Esse teste encontrou e corrigiu
  um erro: níveis PNG RGBA de 8 bits não eram importados.
* **Não testado em aparelho ARM64 real.** Não verificado: desempenho no
  celular, caneta na interface, decodificação/reprodução de áudio real, MP4.

## Limitações conhecidas
* Sem **IK** (cinemática inversa) e sem deformação *plastic*: o esqueleto é
  feito com hierarquia + pivô (FK).
* Pincel **MyPaint** não incluído (o pincel texturizado é a alternativa).
* `.tnz`: FX do schematic, pegs/curvas do desktop, sub-xsheets e som não são
  importados; o app não grava `.tnz`.

## Arquivos
| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.3.0-release.apk` | versão otimizada (chave de release, certificado SHA-256 `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`) |
| `OpenToonzMobile-0.3.0-debug.apk` | com símbolos de depuração |

Android 8.0+ (API 26), arm64-v8a. Atualiza a v0.1.x/v0.2.0 (mesma chave de release).
