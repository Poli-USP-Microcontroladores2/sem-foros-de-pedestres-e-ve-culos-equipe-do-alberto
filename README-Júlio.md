# PSI-Microcontroladores2-Aula07
Atividade: Semáforos de Pedestres e Veículos

## Objetivo
- Desenvolver um sistema embarcado de controle de semáforos para pedestres e veículos, utilizando threads e mutex, e validar o funcionamento do código por meio de testes utilizando o modelo V (testes unitários, de integração e de sistema).
- Opcionalmente, alunos podem utilizar IA generativa para auxiliar na elaboração de trechos de código ou na geração de planos de teste, mas a avaliação deve se concentrar na qualidade dos testes e na correta validação do sistema.

---
# Relatório da Equipe

### Integrantes:
- Júlio Cesar Braga Parro - 16879560
- Arthur J. C. Brasil - 16855560

---

## Código Júlio - Semáforo de Pedestres:
### Testes Unitários
- Implementou-se um semáforo de pedestres com o controle de duas leds (verde e vermelho) por duas threads independentes distintas, thread_led_verde e thread_led_vermelho, com acionamento alternado controlado por mutex, para garantir os tempos de acionamento especificados na atividade (4 segundos para o verde e 4 segundos para o vermelho).
- No código, foram implementados dois modos de funcionamento distintos, noturno e diurno, controlados por uma variável global, em que o modo noturno faz com que apenas o led vermelho pisque, com 1 segundo ligado e 1 segundo desligado. Além disso, implementou-se um botão de travessia, no pino PTA16, para que, assim que for acionado, ligue o led verde por 4 segundos, enquanto desliga o led vermelho.
- Para correto funcionamento do botão de travessia, adicionou uma função que faz um sleep_time inteligente (smart_sleep), verificando se houve pedido de travessia a cada 50 ms, uma vez que o mutex só pode ser liberado por quem o ocupou.
- A partir do acionamento do botão de travessia, manda-se um sinal pelo pino PTE20, configurado como output, que será recebido pelo outro microcontrolador, para garantir sincronia no modo de travessia. A análise desse comportamento será feita posteriormente.
- Fez-se a implementação de um sinal de sincronização, por meio de um botão implementado no pino PTA17, que manda um sinal pelo pino PTE21, configurado como output, que será recebido pelo outro microcontrolador, de modo a garantir sincronia inicial das atividades.
- Com base na implementação desses requisitos, especificados pelo enunciado da atividade, realizou-se um planejamento e execução de diferentes testes unitários, para garantir o funcionamento do semáforo de pedestres isoladamente, conforme descrito abaixo:

- Caso de teste 1: Ciclo dos LEDs Verde e Vermelho
  - Pré-condição: modo_noturno = false e pedido_travessia = false. Mutex inicializado e threads ativas.
  - Etapas de teste: Compilar e executar o código original. Observar a sequência de acionamento dos LEDs.
  - Pós-condição observada: LED verde acende por cerca de 4 s e apaga; em seguida, LED vermelho acende por cerca de 4 s. Nenhum LED fica aceso simultaneamente.
  - Conclusão: A sequência e os tempos de cada LED estão corretos, indicando que as threads e o mutex funcionam conforme esperado.
- Caso de teste 2: Teste do modo noturno (comportamento isolado)
  - Pré-condição: modo_noturno = true. Nenhum pedido de travessia ativo (O botão não afeta esse modo).
  - Etapas de teste: Executar o código em modo noturno. Observar a piscagem dos LEDs.
  - Pós-condição observada: LED vermelho do pedestre pisca com ciclo de 2 segundos (1 s aceso, 1 s apagado). Nenhum outro LED interfere.
  - Conclusão: Comportamento do modo noturno é correto e o mutex garante exclusão mútua entre os LEDs.
- Caso de teste 3: Teste do botão de travessia (debounce e ação)
  - Pré-condição: modo_noturno = false. Sistema em operação normal.
  - Etapas de teste: Pressionar o botão de pedestre (fisicamente ou via simulação). Observar os logs e LEDs.
  - Pós-condição observada: LED verde acende imediatamente por cerca de 4 segundos, PTE20 é ativado durante esse tempo e pedido_travessia volta a false ao final. Pressões rápidas não geram múltiplos pedidos.
  - Conclusão: O botão de travessia é detectado corretamente, o debounce funciona e o sistema retorna ao estado normal após a travessia

#### Evidências
  - As evidências da execução de cada teste estão registradas abaixo, por meio de printscreens dos logs impressos no serial monitor.

Caso de teste 1: Ciclos dos LEDs

<img width="750" height="500" alt="Captura de tela 2025-11-02 164004" src="https://github.com/user-attachments/assets/79c3f29d-c7d1-4e5e-b018-b06709ebb8b4" />


Caso de teste 2: Modo noturno

<img width="750" height="500" alt="Captura de tela 2025-11-02 164829" src="https://github.com/user-attachments/assets/a2ba3a34-c94f-4cc9-80e2-20c27530b1b8" />


Caso de teste 3: Botão de travessia

<img width="750" height="500" alt="Captura de tela 2025-11-02 163553" src="https://github.com/user-attachments/assets/481714cb-b93e-4974-ac8a-b63c69731dfc" />


