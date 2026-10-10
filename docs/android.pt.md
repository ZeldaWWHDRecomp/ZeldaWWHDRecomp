# Wind Waker HD no Android

[English](android.md) · **Português** · [Español](android.es.md)

The Wind Waker HD roda de forma nativa em celulares e tablets Android. Não é um emulador: o próprio
código do jogo é recompilado para processadores ARM e desenha com Vulkan. Você precisa da sua
própria cópia do jogo de Wii U; este projeto nunca fornece arquivos do jogo.

## O que a versão de Android faz

| | |
|---|---|
| **Controles na tela** | Os dois analógicos, o direcional, A B X Y, L R ZL ZR e + −. O botão 🎮 embaixo do botão de visão (canto superior esquerdo) mostra ou esconde os controles. Eles somem enquanto um controle está conectado e voltam no próximo toque. Um controle Bluetooth ou USB também funciona. |
| **A tela do GamePad ao pausar** | Aperte + e a imagem troca para a tela do GamePad (mapa e itens), como o menu de pausa da versão de GameCube. Aperte + de novo para voltar. |
| **60 fps sem deixar o jogo lento** | Nos celulares rápidos o jogo mostra 60 quadros por segundo. Quando o celular esquenta, ele passa sozinho para 30 estáveis, e o jogo nunca fica lento. |
| **Celulares antigos** | Se o driver da sua GPU Adreno for antigo demais, a tela de erro oferece instalar um driver como o Mesa Turnip. |
| **Saves** | Exporte e importe seus saves e guarde no backup do Android. Saves de um Wii U de verdade (americano ou europeu) também funcionam. |

## Celulares testados

Medido com o medidor do próprio jogo na parte mais pesada da Ilha de Outset (cerca de 3.900
desenhos por quadro). *Velocidade do jogo* são os passos de lógica do jogo por segundo: 30 é a
velocidade total.

| Aparelho | Processador / GPU | Quadros por segundo | Velocidade do jogo | Situação |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite, Adreno 830 | cerca de 59 fps | 29,7 / 30 | ótimo |
| Galaxy Z Fold 8 | SM8850, Adreno 840 | 60 fps, 30 quando quente | 26–30 / 30 | ótimo, nas duas telas |
| Lenovo Legion Tab | Snapdragon 8 Gen 3, Adreno 750 | 30 fps estáveis | 28,7–30 / 30 | bom |
| OnePlus 8 Pro | Snapdragon 865, Adreno 650 (Mesa Turnip) | 30 fps | 26–30 / 30 | roda; um travamento de GPU ainda em análise |

Você precisa de um celular de 64 bits com Android 13 ou mais novo e Vulkan 1.3 (ou um driver
personalizado na Adreno), e alguns GB livres.

## Como jogar

### Em breve: o app de instalação (sem PC)

O app de instalação está sendo adicionado ao projeto ([#98](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/pull/98)).
Ele não leva nenhum código do jogo: monta o jogo no seu celular a partir da sua cópia.

1. Instale o app.
2. Escolha o seu jogo: uma pasta do jogo já extraída, um arquivo `.wua` do Cemu, ou uma imagem de disco `.wud`/`.wux` com as chaves.
3. Espere o celular preparar o jogo. Isso acontece uma vez só e leva alguns minutos; a tela mostra
   quantas partes já foram feitas e quanto tempo falta, mais ou menos.
4. Jogue.

### Hoje: monte no seu PC

1. Instale o Android SDK e o NDK, o JDK 17 ou mais novo, CMake, Ninja e Python 3.
2. Extraia o seu jogo e gere o código dele (passos 1 e 2 de *Building* no [README](../README.md)).
3. Rode `android/build_native.sh` e depois `./gradlew assembleRelease` dentro de `android/`.
4. Instale o APK pela USB com `adb install -r app/build/outputs/apk/release/app-release.apk`.

Os passos completos estão no README, na seção *Android (build it yourself)*.

## Jogo limpo

- Use a sua própria cópia do jogo, tirada do seu disco ou console.
- Um APK montado no seu PC contém o seu jogo recompilado. Use só no seu celular e nunca compartilhe.
- Se alguém oferecer um APK pronto com o jogo dentro, ele não é deste projeto.

## Perguntas

**Preciso de um controle?**
Não. Os controles na tela cobrem o GamePad inteiro. Um controle também funciona e esconde os
botões de toque enquanto estiver conectado.

**Qual versão do jogo?**
A versão de Wii U do The Wind Waker HD, americana ou europeia. Saves de um Wii U de verdade também
funcionam.

**Por que ainda não tem APK para baixar?**
Um APK montado no PC hoje contém o jogo recompilado, que não pode ser compartilhado. O app de
instalação monta o jogo no seu celular, por isso o próprio app pode ser compartilhado. Ele está
em teste agora.

**Meu celular esquenta. É normal?**
O jogo é pesado. Quando o celular esquenta, o jogo cai sozinho de 60 para 30 fps estáveis e
continua na velocidade total.

**Alguma coisa não funciona.**
Abra uma [issue](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues) com o modelo do celular,
a versão do Android e o que aconteceu (pode escrever em inglês ou português). Depois de um
travamento, na próxima vez que você abrir o jogo ele oferece o relatório do travamento para
compartilhar.

---

O port de Android é de [rhemfur](https://github.com/rhemfur) e faz parte do ZeldaWWHDRecomp.
Projeto de fã, sem ligação com a Nintendo e sem aprovação dela. The Legend of Zelda e The Wind
Waker são marcas da Nintendo.
