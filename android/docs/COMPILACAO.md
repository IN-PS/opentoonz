# OpenToonz Mobile — Documentação de compilação

O build é feito por um script (`android/build-apk.sh`) que chama diretamente
CMake/NDK, aapt2, javac, dx/d8, zipalign e apksigner. Não usa Gradle (o app
não tem dependências AndroidX), o que o torna reproduzível e fácil de rodar
em CI.

## 1. Pré-requisitos

| Item | Versão usada/testada | Observação |
|---|---|---|
| Android NDK | r27c (27.2.12479018) | qualquer NDK com clang 18 deve servir |
| Android SDK | `platforms/android-23`+ e `build-tools` 29+ | o pacote Ubuntu `android-sdk-platform-23` + `android-sdk-build-tools` (29.0.3) funciona |
| JDK | 17 ou 21 | `javac --release 8` |
| CMake / Ninja | 3.28 / 1.11 | |
| Qt 5.15 **para Android arm64** | 5.15.13 | só o módulo QtCore (ver §2) |
| moc do host | mesma versão do Qt (5.15.x) | ex.: `/usr/lib/qt5/bin/moc` do pacote `qtbase5-dev` |
| Boost (headers) | 1.83 | `libboost-dev`, usado só como headers |

No Ubuntu 24.04:
```bash
sudo apt-get install cmake ninja-build openjdk-21-jdk-headless qtbase5-dev \
     libboost-dev android-sdk-platform-23 android-sdk-build-tools apksigner zipalign
```

## 2. Qt5Core para Android (uma vez)

```bash
# fontes do qtbase 5.15.x (download.qt.io ou tarball orig do Debian/Ubuntu)
export ANDROID_NDK_ROOT=/caminho/android-ndk-r27c
export ANDROID_SDK_ROOT=/usr/lib/android-sdk
android/tools/build-qt5-android.sh qtbase-everywhere-src-5.15.13 $HOME/qt5-android
```
O `configure` usado (sem GUI, sem widgets, sem rede) foi validado; os passos
`make` são os padrões do qmake (ver nota no script).

Alternativa: usar o Qt 5.15 para Android do instalador oficial da Qt
(diretório `android/` ou `android_arm64_v8a/`), passando esse prefixo em
`QT5_ANDROID`.

## 3. APK

```bash
export ANDROID_NDK_ROOT=/caminho/android-ndk-r27c
export ANDROID_SDK_ROOT=/caminho/sdk          # padrão: /usr/lib/android-sdk
export QT5_ANDROID=$HOME/qt5-android
android/build-apk.sh debug                    # -> android/out/debug/OpenToonzMobile-0.1.0-debug.apk

# release (assinatura própria)
KEYSTORE=minha.jks KEYSTORE_PASS=... KEY_ALIAS=release android/build-apk.sh release
```
O script:
1. configura `android/engine` com `android.toolchain.cmake` (arm64-v8a,
   API 26, `c++_shared`) e compila `libotmengine.so`, `lzocompress`,
   `lzodecompress`;
2. copia `libQt5Core*.so` e `libc++_shared.so`; verifica que toda dependência
   `DT_NEEDED` é do sistema ou está no APK;
3. compila recursos (aapt2), Java (javac) e gera `classes.dex`;
4. empacota, alinha (zipalign) e assina (apksigner; chave de debug gerada
   automaticamente em `android/out/debug.keystore`).

Instalação: `adb install -r android/out/debug/OpenToonzMobile-0.1.0-debug.apk`.

`SKIP_NATIVE=1 android/build-apk.sh debug` gera um APK só com Java/recursos
(teste de empacotamento; **não executa**).

## 4. Testes do motor no host (Linux)

O mesmo código C++ do APK compila para Linux x86_64 com o Qt do sistema:
```bash
cmake -S android/engine -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host
./build-host/otm_tests /tmp/otm-test-out      # 126 verificações
```
Saídas visuais (PNG/GIF) ficam no diretório informado.

## 5. Verificação de portabilidade arm64 sem Qt para Android

Compila todo o código do motor + OpenToonz para arm64/bionic usando apenas os
headers do Qt (sem linkar):
```bash
cmake -S android/engine -B build-arm64-check -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared \
  -DOTM_BUILD_TESTS=OFF -DOTM_QT_INCLUDE_DIR=/usr/include/x86_64-linux-gnu/qt5 \
  -DOTM_QT_VERSION=5.15.13 -DOTM_HOST_MOC=/usr/lib/qt5/bin/moc -DOTM_BOOST_INCLUDE=/usr/include
cmake --build build-arm64-check --target otm_core lzocompress lzodecompress
```

## 6. Ambiente sem acesso ao dl.google.com

Se o NDK precisar vir de um espelho não oficial, use apenas o *sysroot* dele
e os compiladores LLVM 18 da distribuição:
`android/tools/make-hybrid-ndk.sh <ndk>` troca os executáveis do NDK por
wrappers do `clang-18`/`lld` do sistema. **Para distribuir o APK, prefira o
NDK oficial do Google** (o `libc++_shared.so` vai dentro do APK).

## 7. Estrutura

```
android/
  build-apk.sh              build completo do APK
  engine/                   CMake do motor nativo (OpenToonz + motor móvel + JNI)
    src/                    document, compositor, tools, projectio, gifencoder, engine, jni_bridge
    compat/                 stubs de GL (gerados) e TOfflineGL nulo, compat do LZO
    tests/otm_tests.cpp     testes do motor
  app/src/main/             AndroidManifest, Java (UI touch), recursos
  tools/                    make-hybrid-ndk.sh, build-qt5-android.sh, gen_glstub.py
  docs/                     esta documentação
```
