# OpenToonz Mobile (Android)

Port do OpenToonz para Android (arm64-v8a, Android 8.0+). O núcleo C++ do
OpenToonz (tnzcore, toonzlib, codec TLV) é compilado sem fork; a interface de
desktop Qt é substituída por uma interface touch nativa.

**Estado:** motor nativo testado no host (233/233 verificações); app
testado na JVM com Robolectric (14/14, `hosttest/run-tests.sh`); APKs v0.4.0
debug e release para arm64-v8a (NDK oficial r27c, pincéis MyPaint) em
[`releases/v0.4.0/`](releases/v0.4.0/) e
nos GitHub Releases. **Ainda não testados num aparelho** —
ver `docs/RELATORIO.md` §4.

| Documento | Conteúdo |
|---|---|
| [docs/ARQUITETURA.md](docs/ARQUITETURA.md) | engenharia reversa do OpenToonz, componentes adaptados, arquitetura do port |
| [docs/COMPILACAO.md](docs/COMPILACAO.md) | como compilar (APK debug/release, testes do motor, checagem arm64) |
| [docs/RELATORIO.md](docs/RELATORIO.md) | funcionalidades, testes e resultados, incompatibilidades |
| [docs/evidencias/](docs/evidencias/) | log dos testes, imagens e GIF gerados pelo motor |

Build rápido (com NDK oficial e Qt5Core para Android):
```bash
android/tools/fetch-mypaint.sh /opt/mypaint   # libmypaint + json-c (opcional)
ANDROID_NDK_ROOT=... QT5_ANDROID=... MYPAINT_SRC=/opt/mypaint android/build-apk.sh debug
```
Testes do motor no Linux:
```bash
cmake -S android/engine -B build-host -G Ninja \
  -DOTM_MYPAINT_DIR=/opt/mypaint/libmypaint-1.6.0 -DOTM_JSONC_DIR=/opt/mypaint/json-c-json-c-0.17-20230812 \
  && cmake --build build-host && ./build-host/otm_tests /tmp/out
```
