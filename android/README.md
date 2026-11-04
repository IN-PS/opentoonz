# OpenToonz Mobile (Android)

Port do OpenToonz para Android (arm64-v8a, Android 8.0+). O núcleo C++ do
OpenToonz (tnzcore, toonzlib, codec TLV) é compilado sem fork; a interface de
desktop Qt é substituída por uma interface touch nativa.

**Estado:** motor nativo testado no host (126/126 verificações); APKs
v0.1.0 debug e release gerados para arm64-v8a em
[`releases/v0.1.0/`](releases/v0.1.0/). **Ainda não testados num aparelho** —
ver `docs/RELATORIO.md` §4.

| Documento | Conteúdo |
|---|---|
| [docs/ARQUITETURA.md](docs/ARQUITETURA.md) | engenharia reversa do OpenToonz, componentes adaptados, arquitetura do port |
| [docs/COMPILACAO.md](docs/COMPILACAO.md) | como compilar (APK debug/release, testes do motor, checagem arm64) |
| [docs/RELATORIO.md](docs/RELATORIO.md) | funcionalidades, testes e resultados, incompatibilidades |
| [docs/evidencias/](docs/evidencias/) | log dos testes, imagens e GIF gerados pelo motor |

Build rápido (com NDK oficial e Qt5Core para Android):
```bash
ANDROID_NDK_ROOT=... QT5_ANDROID=... android/build-apk.sh debug
```
Testes do motor no Linux:
```bash
cmake -S android/engine -B build-host -G Ninja && cmake --build build-host && ./build-host/otm_tests /tmp/out
```
