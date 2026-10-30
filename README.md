# Windwake para Android

Launcher e adaptações de controles para jogar **The Wind Waker HD** no Android. Fork de [ZeldaWWHDRecomp v0.5.0](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp), com experiência móvel inspirada no [Dusklight](https://github.com/TwilitRealm/dusklight).

[Baixar o APK v0.2.0](https://github.com/GenissonEmilio/Windwake/releases/tag/v0.2.0) · [Documentação técnica](WINDWAKE.md) · [Notas da versão](docs/releases/v0.2.0.md)

## Recursos

- Launcher em português com instalação, perfis de desempenho, controles e transferência de saves.
- Câmera por gesto na área livre à direita, sem analógico direito na tela. Sensibilidades de câmera e mira independentes, de 25% a 300%.
- Movimento flutuante à esquerda, com opção de analógico fixo. Botões coloridos, gatilhos em cápsula e layout editável.
- Toque curto no item prepara a mira; arrastar ajusta a direção; outro toque dispara e **B** guarda. Toque longo abre a roda de itens. A mira aceita gestos nas áreas livres dos dois lados.
- Luneta com gesto para olhar e botões **+ / −** para zoom. Barra inferior para bolsa, primeira pessoa, mapa e configurações, conforme o contexto.
- Perfil inicial para Poco M8 Pro / Snapdragon 7s Gen 4: lógica de 30 FPS, resolução dinâmica de 720p até 540p e proteção térmica normal. Há perfis de economia e qualidade.
- Importação de saves com validação, backup e troca transacional da pasta completa. Configurações e transferências ficam bloqueadas durante a sessão do jogo.

## Instalar e jogar

Requer **Android 13 ou superior, ARM64 e Vulkan 1.3**.

1. Baixe `Windwake-0.2.0-android-arm64.apk` na [release](https://github.com/GenissonEmilio/Windwake/releases/tag/v0.2.0) e instale no Android.
2. Abra o launcher e selecione seu dump compatível de **Wind Waker HD USA ou Europa, versão 0**.
3. Para preparar o jogo sem selecionar chaves, use **`.wua`** ou uma pasta extraída com `code`, `content` e `meta`. Imagens `.wux` / `.wud` criptografadas ainda precisam das chaves correspondentes; o instalador não consegue deduzi-las da imagem.
4. Aguarde a extração e a compilação local. Depois, abra pelo botão **JOGAR**.
5. Na aba **TOUCH**, ajuste câmera, mira e movimento. Segure o ícone de controle durante o jogo para editar posição, tamanho e transparência dos botões.

Atualizações do APK de mesma assinatura mantêm a instalação e os saves. O identificador deste aplicativo é `dev.windwake.android`.

## Estado da versão

A v0.2.0 é uma versão inicial para teste. No Poco M8 Pro foram verificados preparação a partir de WUA, abertura e recarga de save, movimento, câmera por gesto, arco por toque, luneta, zoom, pausa e persistência das configurações. Os testes de arco e luneta usaram um save isolado; o progresso original foi restaurado e conferido.

A validação se limita a esses fluxos em Outset. Ainda não há validação de campanha completa, todos os itens, controle físico ou desempenho térmico prolongado. Os perfis são pontos de partida, sem garantia de FPS. O APK desta release usa assinatura local de desenvolvimento; artefatos de CI servem para verificar o build e podem ter outra assinatura.

## Código e compilação

Consulte [WINDWAKE.md](WINDWAKE.md) para arquitetura, dependências fixadas por checksum, testes e build no Windows. A documentação original está em [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md); os caminhos relativos desse documento seguem a raiz do projeto upstream.

O runtime usa recompilação estática do jogo de Wii U, Vulkan e SDL3. Não é um port do código de Twilight Princess nem um build da decompilação C++ experimental. O código específico do jogo é produzido localmente a partir do dump do jogador.

## Créditos e licença

Base: [ZeldaWWHDRecomp e colaboradores](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp). Referência de experiência móvel: [Dusklight / TwilitRealm](https://github.com/TwilitRealm/dusklight). Os avisos e licenças de terceiros foram preservados; veja [LICENSE](LICENSE) e [licenças empacotadas](android/app/src/main/assets/licenses).

Projeto não oficial, sem vínculo com a Nintendo. Este repositório e o APK não distribuem ROM, WUA, chaves, assets nem código traduzido do jogo. O uso requer seu próprio dump compatível.
