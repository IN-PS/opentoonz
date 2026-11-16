# OpenToonz Mobile v0.5.0 (pré-lançamento): mais OpenToonz por dentro e por fora

## Por fora — a tela ficou parecida com o desktop
* **Salas (rooms)** no topo, como no OpenToonz: **Desenho** (visor + Paleta
  do Nível), **Animação** (visor + linha do tempo) e **Xsheet** (visor +
  planilha).
* **Xsheet** no formato do desktop: uma coluna por nível, quadros de cima
  para baixo, cabeçalho com nome / olho / cadeado, blocos de nível com o
  número do desenho, linha de *hold*, marca de fim de nível e de keyframe.
  Cores do tema escuro original do OpenToonz.
* Barra de ferramentas na ordem e com os nomes do desktop: Animar, Seleção,
  Pincel, Geométrica, Preenchimento, Borracha, Seletor de Estilo, Esqueleto.
* O app abre sempre deitado (paisagem).

## Por dentro — mais código original do OpenToonz
* **Curvas de animação** calculadas pelo `TDoubleParam` do desktop (o mesmo
  código das curvas de função). O "suave" do app agora é o *Ease In/Out %*
  do OpenToonz.
* **Ferramenta Esqueleto com IK** (cinemática inversa), usando o `IKEngine`
  do desktop: arraste a mão e o braço inteiro acompanha.
* **Exportar cena OpenToonz (.tnz)**: gera uma pasta de projeto do desktop
  (cena, níveis TLV/PLI/PNG, paletas, hierarquia, pivôs, keyframes de
  colunas e câmera) num `.zip`. Descompacte no computador e abra a cena.
* **Abrir `.tnz` do desktop com animação**: pegs, hierarquia e câmera, com
  todos os tipos de curva (linear, constante, ease, ease %, exponencial,
  speed in/out).

## Como foi verificado
* A cena exportada pelo app foi **carregada pela biblioteca do OpenToonz
  desktop** (compilada deste repositório): câmera, fps, colunas, níveis e
  hierarquia corretos, e a posição de cada coluna em cada quadro idêntica
  à do app (39 de 39 matrizes).
* Uma cena animada **criada pelo desktop** (peg → coluna → coluna, câmera,
  todos os tipos de curva) foi aberta no motor do app e bate com os valores
  gravados pelo desktop (22 de 22 matrizes, erro < 0,000001).
* Motor: **267/267** verificações. App na JVM (Robolectric): **15/15**.

## O que NÃO foi verificado / falta
* **Não testado em aparelho ARM64 real.**
* A aparência da tela nova **não pôde ser vista** no ambiente de build (o
  simulador de testes não desenha pixels): layout, toques e lógica foram
  testados, o visual só num aparelho.
* Não exportado para o desktop: efeitos por camada, keys de opacidade, som.
* Ainda sem: FX do schematic, editor de curvas, expressões, deformação
  *plastic*, sub-xsheets.

## Arquivos
| Arquivo | Uso |
|---|---|
| `OpenToonzMobile-0.5.0-release.apk` | versão otimizada (certificado SHA-256 `b41d206de5c09a31f6f8ffcef1e528389439f866fe2390828630067a7041a7c3`) |
| `OpenToonzMobile-0.5.0-debug.apk` | com símbolos de depuração |

Android 8.0+ (API 26), arm64-v8a. Atualiza as versões anteriores. NDK oficial r27c.
