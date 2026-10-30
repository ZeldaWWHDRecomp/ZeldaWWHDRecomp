# Windwake: arquitetura e desenvolvimento

Fork Android baseado em ZeldaWWHDRecomp v0.5.0, commit `264f53ee0eca9d52d39f6ccf1a890eecec018e6e`, sob MPL-2.0 e licenças dos componentes. O Dusklight serviu como referência da experiência móvel; seu código específico de Twilight Princess não foi transplantado.

## Arquitetura

O jogo suportado é Wind Waker **HD de Wii U**. Sua arquitetura GX2 / Cafe OS exige o runtime HD, em vez da camada GX / GameCube do Dusklight. Esta entrega utiliza recompilação estática: o setup traduz e compila o código a partir do dump no próprio aparelho. A decompilação C++ experimental continua no submódulo `decomp`, mas não é o modo do SDK binário utilizado.

- `WindwakeActivity`: launcher nativo em português, perfis, controles e acesso às transferências de saves.
- `WindwakeSettings`: leitura, migração única e publicação atômica das configurações reais do runtime, preservando opções desconhecidas.
- `WindwakeTouchProfile`, `TouchControls` e `ContextPack`: layout móvel, roteamento dos gestos e desenho dos comandos disponíveis. A preparação dos itens por toque usa a máquina de estados nativa upstream.
- `startup_touch.h` e `WindwakeStartupTouch`: o runtime publica a fase inicial no campo reservado `[3]` do cabeçalho de `ContextPack`, mantendo o formato v1 e seu tamanho. A fase vem do estado nativo e do nome do estágio: `sea_T` para título, `Name` para seleção/nome e estado de jogo para gameplay. Não usa reconhecimento de imagem ou tempo de carregamento estimado.
- No título, um toque confirmado enfileira A por dois passos de lógica nativa, usando `TitlePress`. Nas telas de seleção, nome e confirmação, `TouchControls` deixa o toque chegar à superfície SDL e à interface nativa, incluindo seu teclado e IME. O pad de gameplay fica oculto durante a inicialização; os menus abertos durante o jogo mantêm seu comportamento próprio.
- `WindwakeTouchRedraw`: agrupa pedidos de redesenho da camada Android em prazos de 16 ms fora do editor, buscando cerca de 60 atualizações visuais por segundo para analógicos e botões. O próximo prazo parte da invalidação agendada, incorporando a espera de VSync e o tempo até `onDraw` ao período, em vez de acrescentá-los a cada ciclo. O estado mais recente é desenhado por um único callback, cancelado ao remover a view ou liberar entradas. `nativeSetPad`, gestos e botões continuam sendo enviados imediatamente; a consulta de contexto permanece em 30 Hz. O alvo matemático de 62,5 atualizações/s é quantizado pela tela: até 60 em 60 Hz e aproximadamente 60–62,5 em 90/120 Hz com carga leve; não há garantia dessa cadência sob carga. Esse agendamento visual não controla a apresentação Vulkan do jogo, e o editor continua com atualização livre.
- `WindwakeScopeState`: mantém os controles da luneta durante a transição nativa observada do item para uma mensagem vazia `A:00`, somente após confirmação da luneta. Diálogos e mudanças de contexto encerram esse estado.
- `WindwakeInputPolicy`: exceção restrita para o dispositivo interno `uinput-xiaomi` do Poco, VID/PID `0666:0888`, sem joystick, que anunciava GAMEPAD e ocultava os controles touch. A mesma política é usada na descoberta de controles do SDL.
- `WwhdActivity`: configura o diretório XDG antes de carregar a biblioteca do jogo. Consultas JNI iniciais do touch passam a usar o mesmo diretório que o runtime, evitando cache antecipado no local errado.
- `SessionLease`: trava do sistema operacional entre launcher, jogo, setup e ferramentas. O processo `:game` mantém exclusividade sobre os arquivos durante a sessão.
- `SafeSaveArchive` e `TransactionalSaveStore`: validação de caminhos, duplicatas, CRC e limites reais de descompactação; preparação fora da pasta ativa; publicação da pasta completa por renomeação; backup e recuperação após interrupção.

A identidade Android é `dev.windwake.android`, independente do aplicativo upstream. O namespace Java original é preservado para manter a compatibilidade JNI.

## Perfis e controles

O perfil Poco usa lógica de 30 FPS, resolução dinâmica de 720p até 540p, VSync, AO sem alta resolução e proteção térmica normal. Economia permite descer a 360p; Qualidade mantém 720p. A v0.3 habilita `vkRecordWorker` e desabilita compilação de shaders em segundo plano na migração de desempenho somente quando essas opções ainda não foram definidas pelo usuário. São configurações iniciais, sem garantia de desempenho em todo o jogo.

A câmera usa gesto à direita. Ao mirar, as áreas livres dos dois lados aceitam o gesto. Câmera e mira têm sensibilidades separadas de 25% a 300%; o movimento à esquerda pode ser flutuante ou fixo. Dedos em lados diferentes não iniciam a pinça de zoom.

O layout móvel tem armazenamento próprio, preservando as edições dos estilos antigos. A migração para esse perfil ocorre uma única vez, sem substituir gráficos ou sensibilidades já escolhidas. O editor de posição, tamanho e opacidade abre ao segurar o ícone de controle.

## Compilar no Windows

Requer Python 3.11+ e JDK 17. O bootstrap baixa o SDK Android e o runtime upstream com checksums fixos, preserva a base e aplica o patch nativo versionado. Não requer NDK nem alteração do PATH global para reconstruir o APK usando esse patch pronto.

```powershell
python tools/windwake/bootstrap_android.py
python tools/windwake/test_safety.py
python tools/windwake/test_touch.py
python tools/windwake/test_runtime_patch.py
python tools/windwake/test_build_runtime_patch.py
python tools/windwake/test_performance_analysis.py
cd android
.\gradlew.bat :app:assembleRelease -PwwhdQuestTheater=false --console=plain --no-daemon --max-workers=2 '-Dorg.gradle.jvmargs=-Xmx1024m'
cd ..
python tools/windwake/verify_apk.py android/app/build/outputs/apk/release/app-release.apk
```

O APK é gerado em `android/app/build/outputs/apk/release/app-release.apk`. O heap e o número de workers limitados permitem compilar também em PCs de 8 GB.

O SDK é extraído do APK oficial v0.5.0, SHA-256 `6af660aac1c67e5d99bbc653b1c2b9f0e429a8513e0e9ca0c497b0c37177f791`, em `build/vendor/runtime-v0.5.0`. O bootstrap aplica `tools/windwake/runtime-patches/startup-touch-v1.zip`, com cerca de 30 KB, para gerar separadamente `build/vendor/runtime-v0.5.0-windwake`, usado pelo Gradle. A v0.2.0 mantinha os objetos upstream; a v0.3 substitui somente `sdk/obj/74-context_touch.cpp.o` por um objeto do código-fonte deste fork.

No APK, essa substituição altera exatamente dois assets: `assets/python-source.zip`, que contém o objeto e o manifesto do SDK atualizado, e `assets/runtime-sdk.json`, que registra sua identidade e checksums. `runtime_patch.py` valida a base fixada, os hashes das fontes, o ELF relocável ARM64 e os símbolos JNI esperados; então regenera os dois assets. `verify_apk.py` compara o APK com esses bytes regenerados e exige que os demais arquivos de runtime sejam iguais ao upstream, com normalização CRLF para LF somente nos textos de licença. Também verifica assinatura, identidade Android, launcher, arquitetura e ausência de material do jogo. Java, recursos e manifesto Android são recompilados neste fork.

Alterar C/C++ no checkout não modifica o objeto pré-compilado automaticamente. É necessário reconstruir e empacotar esse objeto, atualizar o patch e gerar novamente o runtime derivado. A identidade do SDK muda quando o objeto muda: na próxima abertura, o setup recompila uma vez os 80 blocos do jogo no aparelho usando os dados já preparados. Não é necessário selecionar a ROM outra vez. Atualizações somente de Java, mantendo a identidade nativa, não exigem essa recompilação do jogo.

Sem configuração de assinatura própria, o Gradle usa a chave de desenvolvimento local, conforme o comportamento upstream. Atualizações instaláveis sobre a release publicada precisam manter a mesma chave privada. Não coloque keystores ou senhas no Git. Os artefatos de CI são builds de verificação e podem ter assinatura diferente.

O workflow ativo `Windwake Android` repete os testes e produz um APK verificado. Os workflows multiplataforma upstream estão preservados em `.github/upstream-workflows` como referência, fora da pasta de execução automática.

## Patch nativo e verificações offline

Depois de obter a base com o bootstrap, a aplicação e a conferência do patch usam somente arquivos locais:

```powershell
python tools/windwake/runtime_patch.py apply --base build/vendor/runtime-v0.5.0 --patch tools/windwake/runtime-patches/startup-touch-v1.zip --out build/vendor/runtime-v0.5.0-windwake
python tools/windwake/runtime_patch.py check --base build/vendor/runtime-v0.5.0 --patch tools/windwake/runtime-patches/startup-touch-v1.zip --runtime build/vendor/runtime-v0.5.0-windwake
python tools/windwake/test_touch.py
python tools/windwake/test_runtime_patch.py
python tools/windwake/test_build_runtime_patch.py
python tools/windwake/test_performance_analysis.py
```

Uma saída já existente é conferida, nunca sobrescrita com conteúdo diferente. Ao desenvolver outro patch, escolha uma nova pasta de saída para `apply` e um novo arquivo para `pack`.

Para reconstruir o único objeto, conecte um Android ARM64 com depuração USB autorizada e Windwake instalado, contendo o compilador fixado. Com a base local, este processo também funciona sem internet:

```powershell
python tools/windwake/build_runtime_patch.py --serial SERIAL --out build/native-patch/context_touch.cpp.o
python tools/windwake/runtime_patch.py pack --base build/vendor/runtime-v0.5.0 --object build/native-patch/context_touch.cpp.o --out build/native-patch/rebuilt-startup-touch-v1.zip
python tools/windwake/runtime_patch.py apply --base build/vendor/runtime-v0.5.0 --patch build/native-patch/rebuilt-startup-touch-v1.zip --out build/vendor/runtime-v0.5.0-windwake-rebuilt
python tools/windwake/runtime_patch.py check --base build/vendor/runtime-v0.5.0 --patch build/native-patch/rebuilt-startup-touch-v1.zip --runtime build/vendor/runtime-v0.5.0-windwake-rebuilt
```

`build_runtime_patch.py` usa o clang ARM 20.1.8 empacotado no APK, validando seu SHA-256 antes de executar. Transfere `context_touch.cpp`, os cabeçalhos do runtime e somente os cabeçalhos de sysroot/clang de `toolchain-data.zip`; não transfere o jogo. Compila para `aarch64-linux-android33`, com C++20, `-O3`, `-fsigned-char` e mapas de prefixo para retirar o caminho temporário do resultado. Não exige o download de um NDK de Windows.

Ao lado do objeto são gerados `context_touch.cpp.inputs.tar.gz` e `context_touch.cpp.recipe.json`. A receita registra os hashes do arquivo canônico de entradas, de cada fonte/cabeçalho, do compilador, dos dados da toolchain e do objeto, além das flags e diagnósticos. O ZIP de patch inclui o objeto e seus metadados de procedência, hashes das fontes e identidades base/resultante; a verificação não aceita um patch produzido para fontes diferentes das presentes no checkout. Reconstruir esse objeto de runtime e recompilar os 80 blocos do jogo no setup são operações distintas.

Para analisar um trace já coletado, sem acessar o aparelho:

```powershell
python tools/windwake/analyze_performance.py build/qa/trace.csv --start-frame 100 --end-frame 1000 --label caminhada --output build/qa/caminhada.json
```

Substitua os limites por frames reais do trecho de gameplay, excluindo boot e menus; o limite inicial é exclusivo e o final inclusivo. `--report CAMINHO` pode ser repetido para anexar relatórios nativos. O resultado informa intervalos entre frames produzidos, percentis, quedas e eventos de compilação; a frequência da tela não é usada como FPS do jogo. Eventos de threads diferentes podem se sobrepor, e seus tempos não devem ser somados como duração de um frame.

A cadência dos controles deve ser medida separadamente com `adb shell dumpsys gfxinfo dev.windwake.android:game`, identificando a janela e duração da coleta. Os frames e deadlines da pipeline Skia/OpenGL pertencem à camada Android; não representam os frames Vulkan do jogo. Nas sondagens de cerca de 12 segundos, o contador passou de 2.540 sem agrupamento para 586 com o primeiro agendamento de 33 ms e 732 com o prazo ancorado de 33 ms. A possível duplicação desses contadores impede tratá-los diretamente como atualizações/s. A variante final de 16 ms registrou 1.488 no mesmo gesto (~12,25 s), com dois deadlines perdidos e p99 de 24 ms, comparados com 2.540, dois e 32 ms sem agrupamento. A redução de contagem foi de cerca de 41%; esse é um resultado da UI. Esses dados não demonstram que reduzir o redesenho causou alteração no FPS nativo.

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

## Validação histórica até v0.2.0 — 30/10/2026

- Onze contratos Java de segurança: transação, limites de ZIP, caminhos, CRC, falhas de I/O, recuperação, migração de configurações e exclusão de sessão.
- Três suítes upstream de layout, presets e analógicos; regressão do dispositivo interno do Poco; perfil móvel com sensibilidades independentes, roteamento, pinça e contexto da luneta. Testes C++ upstream de `touch_play` e `camera_swipe` no host.
- No Poco M8 Pro / SM7635 / Android 16: transação e `fsync` no armazenamento real, execução do clang ARM empacotado, perfis e sensibilidades pela interface, preparação de WUA sem seleção de chaves, abertura e recarga de save.
- Com a v0.2.0: movimento flutuante, câmera por gesto e pausa; arco com preparação por toque, mira nos dois lados, disparo e B para guardar; luneta com gesto, zoom de ×1 para ×3 e redução para ×2, seguida de retorno por B.
- Arco e luneta foram testados com save isolado. A pasta original foi restaurada e seu hash conferido antes de reabrir o save e encerrar pelo menu.

A validação foi curta e em Outset. Não cobre campanha completa, todos os itens, controle físico, multitouch real simultâneo ou estabilidade térmica prolongada. As amostras de 29,9–30,0 FPS em 720p pertencem ao teste inicial v0.1 na torre de Outset, e não constituem um benchmark da v0.2.0. A abertura a frio apresentou travadas iniciais.

## Verificações de desenvolvimento da v0.3

- Contratos Java puros do fluxo inicial: decodificação compatível da fase, toque de título, cancelamento por arrasto, troca de cena e múltiplos dedos. Contrato C++ puro de `TitlePress`, com A por dois passos de lógica e liberação ao sair do título.
- Contratos Java puros do redesenho: rajadas de eventos, prazos ancorados de 16 ms, callback antecipado, desenho solicitado pelo framework e cancelamento na liberação/remoção da view. A simulação de VSync em 60/90/120 Hz, com fases de 0,5/2/4 ms até o desenho, preserva aproximadamente 60 atualizações/s em 60 Hz e 62,5 em 90/120 Hz. O caso de carga com fase de 7 ms e a recuperação de callbacks atrasados não enfileiram redesenhos de compensação. A suíte de touch existente também passou.
- Cinco testes Python do patch: validação e rejeição de alterações fora do objeto permitido, metadados e fontes. Quatro testes do builder: entradas canônicas, seleção de fontes/cabeçalhos, flags e localização do compilador empacotado.
- Três testes Python da análise de desempenho: recorte de frames, percentis e separação de eventos/relatórios.

No Poco, o fluxo título → seleção → nome pelo teclado Android → confirmação → novo jogo foi percorrido por toque, incluindo o avanço dos diálogos até controlar Link na torre. O APK com patch foi preparado localmente sem selecionar novamente a ROM. Doze contratos de segurança passaram, incluindo preservação da escolha explícita de worker entre migrações e perfis.

As rotas de exploração e os experimentos não demonstraram aumento consistente de FPS. As amostras finais ocorreram após sessões longas e em condições térmicas diferentes. Consulte o [relatório de medições](docs/performance-v0.3.0.md); contagem de redesenhos do Android e FPS do jogo são métricas distintas.

A subida real da torre também foi confirmada por capturas e pelo procedimento de escalada com altura crescente. Com input direcional automatizado e fixture privado, a janela de 287 intervalos marcou 29,91 FPS e p99 de 41,62 ms. Sem baseline equivalente, esse resultado valida o trecho e não comprova ganho causado pela atualização. A pasta original do save foi restaurada e seu hash conferido; as opções temporárias de teste foram removidas.
