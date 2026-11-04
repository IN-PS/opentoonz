# OpenToonz Mobile v0.2.0 (pré-lançamento): ferramentas para animação profissional

## Novidades
**Desenho**
* Pincel com **dureza** (macio ↔ duro) e **opacidade** uniforme por traço
  (sem acumular onde o traço se sobrepõe); a tinta continua sendo Toonz
  Raster, então trocar a cor na paleta recolore também os traços macios.
* **Borracha macia** (dureza ajustável).
* **Estabilizador** de traço (0–10) para linhas limpas.
* Ferramenta **Formas**: linha, retângulo e elipse com o pincel atual.
* **Seleção laço** (livre), além da retangular; mover/girar/escalar.

**Animação**
* **Keyframes de camada** (posição, rotação, escala, opacidade) e **câmera**
  (pan, rotação, zoom), com interpolação linear, suave (ease in/out) ou
  constante — semelhante aos pegs do OpenToonz. Arraste no canvas para
  criar/editar a chave do quadro atual; 2 dedos giram/escalam. Marcadores ◆
  na timeline (camadas) e no cabeçalho (câmera).
* Desenhar numa camada animada cai exatamente sob o dedo.

**Timing**
* **Reframe**: animar em 1s, 2s ou 3s com um toque.
* **Exposição** do desenho atual (quantos quadros ele dura).
* **Arrastar células** na timeline (toque longo + arrastar).

**Som**
* **Importar áudio** (MP3, M4A, WAV, OGG… o que o Android decodificar),
  **forma de onda** na timeline, reprodução sincronizada, **scrub** ao
  trocar de quadro (lip-sync), início e volume ajustáveis, **áudio no MP4**.

**Correção no motor OpenToonz:** um bug do `TImageCache` em Linux/macOS/
Android (ponteiros obsoletos após compressão) podia fazer um desenho recém-
criado "sumir". Era a causa de uma falha intermitente vista antes.

## Testes desta versão
* Motor nativo (mesmo C++ do APK, em x86_64): **166/166** verificações,
  estáveis em execuções repetidas.
* App completo na JVM (Robolectric 3.8 / Android 8.1) ligado ao motor real:
  **10/10** — inclui formas, laço, keyframes de camada e câmera (com
  desfazer e salvar/reabrir), reframe, mover células e trilha de áudio com
  forma de onda.
* **Não testado em aparelho ARM64 real.** Não verificado: decodificação de
  áudio real (MediaExtractor/MediaCodec), reprodução de som, MP4 com áudio,
  caneta na interface, desempenho no celular.

## Arquivos
| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.2.0-release.apk` | versão otimizada (chave de release, certificado SHA-256 `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`) |
| `OpenToonzMobile-0.2.0-debug.apk` | com símbolos de depuração |

Android 8.0+ (API 26), arm64-v8a. Atualiza a v0.1.x (mesma chave de release).
Compilado com o *sysroot* de um NDK r27c de espelho não oficial; para
distribuição pública, recompile com o NDK oficial.
