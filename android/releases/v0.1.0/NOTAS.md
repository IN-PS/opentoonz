# OpenToonz Mobile v0.1.0 (pré-lançamento, não testado em aparelho)

> **Substituída pela v0.1.1** (correção no arraste da seleção).

| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.1.0-release.apk` | versão otimizada, assinada com a chave de release do projeto |
| `OpenToonzMobile-0.1.0-debug.apk` | símbolos de depuração, chave de debug |

* Android 8.0+ (API 26), somente **arm64-v8a**.
* Instalação: `adb install -r OpenToonzMobile-0.1.0-release.apk` (ou abrir o
  arquivo no aparelho permitindo "fontes desconhecidas"). As duas variantes
  usam o mesmo pacote com chaves diferentes: desinstale uma antes de instalar
  a outra.
* Certificado de release (SHA-256):
  `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`
* Conteúdo nativo: `libotmengine.so` (OpenToonz core + motor), `libQt5Core`
  5.15.13, `libc++_shared.so`, auxiliares `liblzocompress/liblzodecompress`.
* **Atenção:** compilado com o *sysroot* de um NDK r27c de espelho não
  oficial (o NDK do Google estava inacessível no ambiente de build). Para
  distribuição pública, recompile com o NDK oficial.
* Funcionalidades, testes e limitações: `android/docs/RELATORIO.md`.
