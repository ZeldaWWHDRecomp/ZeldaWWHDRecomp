# Windwake: arquitetura e desenvolvimento

Fork Android baseado em ZeldaWWHDRecomp v0.5.0, commit `264f53ee0eca9d52d39f6ccf1a890eecec018e6e`, sob MPL-2.0 e licenças dos componentes. O Dusklight serviu como referência da experiência móvel; seu código específico de Twilight Princess não foi transplantado.

## Arquitetura

O jogo suportado é Wind Waker **HD de Wii U**. Sua arquitetura GX2 / Cafe OS exige o runtime HD, em vez da camada GX / GameCube do Dusklight. Esta entrega utiliza recompilação estática: o setup traduz e compila o código a partir do dump no próprio aparelho. A decompilação C++ experimental continua no submódulo `decomp`, mas não é o modo do SDK binário utilizado.

- `WindwakeActivity`: launcher nativo em português, perfis, controles e acesso às transferências de saves.
- `WindwakeSettings`: leitura, migração única e publicação atômica das configurações reais do runtime, preservando opções desconhecidas.
- `WindwakeTouchProfile`, `TouchControls` e `ContextPack`: layout móvel, roteamento dos gestos e desenho dos comandos disponíveis. A preparação dos itens por toque usa a máquina de estados nativa upstream.
- `WindwakeScopeState`: mantém os controles da luneta durante a transição nativa observada do item para uma mensagem vazia `A:00`, somente após confirmação da luneta. Diálogos e mudanças de contexto encerram esse estado.
- `WindwakeInputPolicy`: exceção restrita para o dispositivo interno `uinput-xiaomi` do Poco, VID/PID `0666:0888`, sem joystick, que anunciava GAMEPAD e ocultava os controles touch. A mesma política é usada na descoberta de controles do SDL.
- `WwhdActivity`: configura o diretório XDG antes de carregar a biblioteca do jogo. Consultas JNI iniciais do touch passam a usar o mesmo diretório que o runtime, evitando cache antecipado no local errado.
- `SessionLease`: trava do sistema operacional entre launcher, jogo, setup e ferramentas. O processo `:game` mantém exclusividade sobre os arquivos durante a sessão.
- `SafeSaveArchive` e `TransactionalSaveStore`: validação de caminhos, duplicatas, CRC e limites reais de descompactação; preparação fora da pasta ativa; publicação da pasta completa por renomeação; backup e recuperação após interrupção.

A identidade Android é `dev.windwake.android`, independente do aplicativo upstream. O namespace Java original é preservado para manter a compatibilidade JNI.

## Perfis e controles

O perfil Poco usa lógica de 30 FPS, resolução dinâmica de 720p até 540p, VSync, AO sem alta resolução e proteção térmica normal. Economia permite descer a 360p; Qualidade mantém 720p. São configurações iniciais, sem garantia de desempenho em todo o jogo.

A câmera usa gesto à direita. Ao mirar, as áreas livres dos dois lados aceitam o gesto. Câmera e mira têm sensibilidades separadas de 25% a 300%; o movimento à esquerda pode ser flutuante ou fixo. Dedos em lados diferentes não iniciam a pinça de zoom.

O layout móvel tem armazenamento próprio, preservando as edições dos estilos antigos. A migração para esse perfil ocorre uma única vez, sem substituir gráficos ou sensibilidades já escolhidas. O editor de posição, tamanho e opacidade abre ao segurar o ícone de controle.

## Compilar no Windows

Requer Python 3.11+ e JDK 17. O bootstrap baixa o SDK Android e o runtime upstream com checksums fixos. Não requer NDK nem alteração do PATH global para reconstruir a camada Java.

```powershell
python tools/windwake/bootstrap_android.py
python tools/windwake/test_safety.py
python tools/windwake/test_touch.py
cd android
.\gradlew.bat :app:assembleRelease -PwwhdQuestTheater=false --console=plain --no-daemon --max-workers=2 '-Dorg.gradle.jvmargs=-Xmx1024m'
cd ..
python tools/windwake/verify_apk.py android/app/build/outputs/apk/release/app-release.apk
```

O APK é gerado em `android/app/build/outputs/apk/release/app-release.apk`. O heap e o número de workers limitados permitem compilar também em PCs de 8 GB.

O SDK é extraído do APK oficial v0.5.0, SHA-256 `6af660aac1c67e5d99bbc653b1c2b9f0e429a8513e0e9ca0c497b0c37177f791`. Java, recursos e manifesto são recompilados neste fork. A verificação compara runtime, compilador e assets byte a byte, além de verificar assinatura, identidade, launcher, arquitetura e ausência de material do jogo.

Mudanças C/C++ exigem reconstruir o SDK pelos scripts nativos upstream; reconstruir somente o APK não altera os objetos nativos pré-compilados. A v0.2.0 não modifica esses objetos.

Sem configuração de assinatura própria, o Gradle usa a chave de desenvolvimento local, conforme o comportamento upstream. Atualizações instaláveis sobre a release publicada precisam manter a mesma chave privada. Não coloque keystores ou senhas no Git. Os artefatos de CI são builds de verificação e podem ter assinatura diferente.

O workflow ativo `Windwake Android` repete os testes e produz um APK verificado. Os workflows multiplataforma upstream estão preservados em `.github/upstream-workflows` como referência, fora da pasta de execução automática.

## Instrumentação no aparelho

Com Android ARM64 autorizado por ADB:

```powershell
cd android
.\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest -PwwhdQuestTheater=false
cd ..
build/android-sdk/platform-tools/adb.exe install -r android/app/build/outputs/apk/debug/app-debug.apk
build/android-sdk/platform-tools/adb.exe install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
build/android-sdk/platform-tools/adb.exe shell am instrument -w dev.windwake.android.test/org.wwhdrecomp.wwhd.WindwakeInstrumentation
```

A instrumentação usa uma pasta temporária independente e verifica renomeação, sincronização em disco, recuperação, configurações, exclusão da sessão e execução do compilador ARM empacotado.

`python tools/windwake/phone_smoke.py --serial SERIAL` exercita a interface real do launcher e confere o INI publicado. Altera temporariamente os perfis e controles e termina com o perfil Poco, câmera 100%, mira 70% e movimento flutuante. Execute com o jogo fechado.

## Formatos e conteúdo distribuído

O APK contém runtime e compilador ARM. Não contém jogo, código traduzido do jogo nem chaves. O fluxo sem seleção de chaves aceita `.wua` ou uma pasta já extraída com `code`, `content` e `meta`. WUX / WUD criptografados precisam das chaves correspondentes; elas não podem ser deduzidas apenas descompactando a imagem.

`tools/windwake/prepare_dump.py` extrai somente o WUX de um ZIP externo em streaming, valida cabeçalho e CRC e calcula SHA-256. Não descriptografa o disco, não interpreta documentos do arquivo e não altera o original. Use `build/private` para os arquivos locais de jogo; essa pasta é ignorada pelo Git.

## Validação até v0.2.0 — 30/10/2026

- Onze contratos Java de segurança: transação, limites de ZIP, caminhos, CRC, falhas de I/O, recuperação, migração de configurações e exclusão de sessão.
- Três suítes upstream de layout, presets e analógicos; regressão do dispositivo interno do Poco; perfil móvel com sensibilidades independentes, roteamento, pinça e contexto da luneta. Testes C++ upstream de `touch_play` e `camera_swipe` no host.
- No Poco M8 Pro / SM7635 / Android 16: transação e `fsync` no armazenamento real, execução do clang ARM empacotado, perfis e sensibilidades pela interface, preparação de WUA sem seleção de chaves, abertura e recarga de save.
- Com a v0.2.0: movimento flutuante, câmera por gesto e pausa; arco com preparação por toque, mira nos dois lados, disparo e B para guardar; luneta com gesto, zoom de ×1 para ×3 e redução para ×2, seguida de retorno por B.
- Arco e luneta foram testados com save isolado. A pasta original foi restaurada e seu hash conferido antes de reabrir o save e encerrar pelo menu.

A validação foi curta e em Outset. Não cobre campanha completa, todos os itens, controle físico, multitouch real simultâneo ou estabilidade térmica prolongada. As amostras de 29,9–30,0 FPS em 720p pertencem ao teste inicial v0.1 na torre de Outset, e não constituem um benchmark da v0.2.0. A abertura a frio apresentou travadas iniciais.
