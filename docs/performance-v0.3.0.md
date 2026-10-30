# Performance v0.3.0 — medições e limites

Medições de 30/10/2026 no Poco usado no desenvolvimento. Com o limitador final de 16 ms, o contador de renderizações da interface Android caiu cerca de **41%** e o p99 da UI passou de 32 para 24 ms. Os testes de gameplay ainda mostram quedas e não demonstram aumento consistente de FPS do jogo.

## Interface touch: limite de redesenho

A comparação usa a mesma versão do SDK: antes, o APK v0.3.0 sem o limitador Java de redesenho; depois, com intervalo de **16 ms** entre redesenhos periódicos do overlay, voltado a uma cadência próxima de 60 atualizações por segundo da UI. O prazo é ancorado na invalidação para evitar somar a espera de VSync a cada período. A entrega dos eventos de toque continua imediata; estes testes não medem latência física de entrada.

Após zerar as estatísticas de `gfxinfo` do processo do jogo, foi repetido o mesmo gesto de câmera de 12 segundos. A duração efetiva foi 12,136 s antes e 12,251 s na versão final:

| Métrica da interface Android | Antes | Final, 16 ms |
| --- | ---: | ---: |
| `Total frames rendered` | 2.540 | 1.488 |
| `Janky frames` | 2 (0,08%) | 2 (0,13%) |
| p99 do tempo de quadro da UI | 32 ms | 24 ms |

O contador `Total frames rendered` caiu **41,4%**, enquanto o p99 da UI diminuiu **25%**. A quantidade absoluta de quadros classificados como janky permaneceu em dois; a proporção mudou porque o total mudou. Esse contador pertence à camada HWUI/Skia, não aos quadros Vulkan do jogo, e não deve ser convertido em FPS do jogo ou frequência física da tela. É evidência de menos atividade contabilizada na interface, sem comprovar eliminação das quedas durante a exploração.

Os ensaios preliminares com 33 ms foram descartados como configuração final. A primeira implementação retornou 586 renderizações, 174 quadros janky (29,69%) e p99 de 28 ms. Com o prazo ancorado na invalidação, retornou 732 renderizações, 192 quadros janky (26,23%) e p99 de 26 ms. A redução de 76,9% no contador do primeiro ensaio não representa a versão entregue; ambas as tentativas apresentaram regressão no contador de jank.

## Gameplay: oito gestos na mesma rota

Foi usado um save de teste isolado no píer inicial de Outset. A rota contém oito gestos, com aproximadamente 40 segundos de movimento e câmera, além dos intervalos de automação. **A rota não inclui torre.** Boot e menus foram excluídos pelas janelas de frames do trace nativo. Os percentis usam nearest rank; FPS refere-se aos intervalos de quadros atualizados do jogo, não ao overlay Android.

A execução final de 16 ms produziu os resultados abaixo:

| Gesto | FPS | p99, ms | Intervalos ≥50 ms |
| --- | ---: | ---: | ---: |
| Andar para frente | 25,07 | 56,84 | 4 |
| Câmera à direita | 24,99 | 59,68 | 2 |
| Andar para trás | 26,98 | 47,57 | 0 |
| Câmera à esquerda | 24,41 | 46,97 | 0 |
| Andar para frente, repetição | 23,42 | 59,86 | 11 |
| Andar para trás, repetição | 24,98 | 50,27 | 2 |
| Panorama da ilha | 21,88 | 77,51 | 7 |
| Andar para o lado | 24,23 | 54,51 | 5 |

A faixa foi **21,88–26,98 FPS**. Essa sessão começou a primeira janela no frame 4.630, com mais tempo de execução acumulado; a temperatura da bateria reportada foi 38,8 °C, contra 37,0 °C na sessão anterior. Aquecimento, duração da sessão e estado dos caches não ficaram equivalentes. Os resultados não sustentam ganho causal de FPS com o limitador, nem permitem atribuir toda a diferença à mudança da UI.

Para preservar o histórico, seguem também os oito pares da primeira tentativa de 33 ms. O aquecimento já era diferente: primeira janela no frame 1.358 antes e 961 depois. As diferenças abaixo também não podem ser atribuídas causalmente ao limitador:

| Gesto | FPS antes → 33 ms | p99, ms antes → 33 ms | Intervalos ≥50 ms antes → 33 ms |
| --- | ---: | ---: | ---: |
| Andar para frente | 28,01 → 24,58 | 49,37 → 73,04 | 1 → 26 |
| Câmera à direita | 27,96 → 28,95 | 51,77 → 51,21 | 1 → 1 |
| Andar para trás | 28,03 → 29,56 | 43,19 → 41,22 | 0 → 0 |
| Câmera à esquerda | 28,95 → 30,02 | 40,27 → 38,78 | 0 → 0 |
| Andar para frente, repetição | 29,34 → 29,81 | 38,79 → 44,09 | 0 → 1 |
| Andar para trás, repetição | 29,75 → 29,97 | 39,18 → 38,36 | 0 → 0 |
| Panorama da ilha | 25,66 → 25,95 | 80,98 → 82,55 | 7 → 7 |
| Andar para o lado | 26,16 → 27,15 | 67,06 → 81,43 | 5 → 7 |

A primeira janela piorou após a mudança de 33 ms. Outras aproximaram-se de 30 FPS, mas o panorama manteve picos acima de 80 ms e o último trecho apresentou p99 maior.

## Gameplay: subida real da torre

A escada da torre de Outset foi validada separadamente no APK final de 16 ms. O teste usou um fixture QA privado, com a posição inicial preparada a partir da geometria da escada, e automação do direcional pela entrada nativa de controle. Capturas no pé, durante a escalada e no topo confirmaram a subida. O log registrou o procedimento de escada `0x3C` com a altura aumentando, seguido da saída e de Link parado no topo.

A janela `870 < frame ≤ 1157` seleciona 287 intervalos durante a escalada, aproximadamente 9,60 segundos:

| Métrica do jogo durante a subida | Resultado |
| --- | ---: |
| FPS de quadros atualizados | 29,91 |
| p50 | 33,52 ms |
| p95 | 38,89 ms |
| p99 | 41,62 ms |
| Intervalos ≥50 ms | 1 |
| Intervalos ≥100 ms | 0 |

Esta execução comprova a subida real e registra seu comportamento de performance na versão final. Não houve uma execução anterior equivalente da mesma escalada; portanto, esses números não quantificam ganho sobre a versão anterior nem garantem o mesmo resultado em toda sessão. A rota touch do píer e esta automação de controle são testes distintos. Após as medições, o save original foi restaurado com integridade verificada e as opções de teste foram removidas.

## Experimentos no renderizador

As janelas automáticas abaixo usam os frames `900 < frame ≤ 1530`, com 630 intervalos por amostra. São ensaios distintos da rota touch; repetir a janela de frames não garante o mesmo estado térmico ou de caches.

| Configuração / repetição | FPS | p99, ms | Intervalos ≥50 ms |
| --- | ---: | ---: | ---: |
| Base 1 | 28,75 | 45,51 | 1 |
| Worker 1 | 29,54 | 42,67 | 2 |
| Base 2 | 29,55 | 41,63 | 0 |
| Worker 2 | 29,50 | 43,85 | 3 |
| Worker + batch 4096 | 26,73 | 47,75 | 2 |
| Render pass forçado | 27,37 | 100,62 | 14 |
| Janela experimental do renderizador | 26,18 | 76,34 | 50 |
| Base 3, após a janela experimental | 26,43 | 49,18 | 5 |

O worker não apresentou ganho consistente nas repetições e permanece **desligado por padrão**, disponível como opção experimental. Batch 4096, render pass forçado e janela experimental não foram adotados como padrão. A base 3 também ficou mais lenta que as bases anteriores: não se pode atribuir toda a diferença de FPS ao experimento imediatamente anterior. Caminhos novos de pipeline também podem exigir aquecimento próprio.

Nos trechos pesados perfilados houve cerca de 5.000 draws por quadro, alto custo de CPU no renderizador e pouca espera pela GPU. Isso aponta um gargalo de preparação dos comandos na CPU. Reduzir resolução pode aliviar a GPU, mas não elimina esse trabalho de CPU nem garante melhora nesses trechos.

## Fontes e próximos critérios

Os registros locais são `build/qa/v03-*-analysis.json`, `v03-{touch-before,touch-after,touch-16}-comparison.json`, as respectivas janelas `*-windows.json` e `*-gfx.txt`. `v03-touch-final-gfx.txt` registra a tentativa intermediária de 33 ms; `v03-touch-16-gfx.txt` registra a configuração final de 16 ms; `v03-tower-climb-analysis.json` registra a subida validada. O analisador está em `tools/windwake/analyze_performance.py`. Eventos de compilação de shaders/pipelines são contabilizados separadamente; suas durações podem se sobrepor e não devem ser somadas ao tempo de quadro.

Antes de afirmar ganho de fluidez no jogo, repetir comparações das mesmas rotas, incluindo a torre, com aquecimento e configurações equivalentes, observando p95/p99 e intervalos longos. A redução final de aproximadamente 41% no contador de renderizações da UI não é uma promessa de FPS estável.
