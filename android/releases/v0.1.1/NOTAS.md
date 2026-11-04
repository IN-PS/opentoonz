# OpenToonz Mobile v0.1.1 (pré-lançamento)

**Correção:** ao arrastar uma seleção, a posição em que o dedo era levantado
era ignorada (a seleção parava antes do ponto final). Encontrado pelos novos
testes do app.

## Testes desta versão
* **Motor nativo** (mesmo código C++ do APK, compilado para Linux x86_64):
  126/126 verificações (`android/engine/tests`).
* **App completo na JVM com Robolectric 3.8 / Android 8.1 (API 27)**, ligado
  ao motor nativo real via JNI — 6/6 testes (`android/hosttest`):
  gerenciador de projetos; pincel por toque; quadro a quadro; onion skin;
  hold; camadas (adicionar, ocultar); preenchimento; desfazer/refazer;
  seleção (mover + aplicar); reprodução; salvar pelo menu (TLV pelos
  auxiliares LZO); reabrir com pixels idênticos; salvamento automático ao
  pausar; importação de imagem pelo seletor do sistema; exportação GIF e
  sequência PNG; mudança para retrato; pressão da caneta (1 px a 31 px).
* **Não testado em aparelho ARM64 real** (não há aparelho/emulador no
  ambiente de build). Só um aparelho confirma: inicialização do Qt no
  Android, execução dos auxiliares LZO a partir do APK, codificação MP4 real
  (MediaCodec), decodificação JPG, caneta na interface e desempenho.

## Arquivos
| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.1.1-release.apk` | versão otimizada, chave de release (SHA-256 do certificado `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`) |
| `OpenToonzMobile-0.1.1-debug.apk` | com símbolos de depuração, chave de debug |

* Android 8.0+ (API 26), somente **arm64-v8a**.
* `adb install -r OpenToonzMobile-0.1.1-release.apk`. Release e debug usam
  chaves diferentes: desinstale uma antes de instalar a outra.
* Compilado com o *sysroot* de um NDK r27c de espelho não oficial (o NDK do
  Google estava inacessível no ambiente). Para distribuição pública,
  recompile com o NDK oficial (`android/docs/COMPILACAO.md`).
