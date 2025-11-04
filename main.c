#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

// Registro do módulo de log
LOG_MODULE_REGISTER(SEMAFORO_VEICULOS, LOG_LEVEL_INF);

// --- Definições dos LEDs via DeviceTree ---
#define LED_VERDE_NODE DT_ALIAS(led0)    // LED verde (led0)
#define LED_VERMELHO_NODE DT_ALIAS(led2) // LED vermelho (led1)  
#define LED_AMARELO_NODE DT_ALIAS(led1)  // LED azul como amarelo (led2)
#define BUTTON_NODE DT_NODELABEL(user_button_0) // Botão de pedestres - PTA16
#define BUTTON_NODE_2 DT_NODELABEL(user_button_1) // Botão de inicialização - PTA17

// --- Definições de tempo (em milissegundos) ---
#define TEMPO_VERDE          3000    // 3 segundos
#define TEMPO_AMARELO        1000    // 1 segundo
#define TEMPO_VERMELHO       4000    // 4 segundos
#define TEMPO_TRAVESSIA      4000    // 4 segundos para travessia de pedestres
#define TEMPO_PISCANTE       1000    // 1 segundo para amarelo piscante

// --- Estados do sistema ---
typedef enum {
    MODO_NORMAL,
    MODO_NOTURNO,
    MODO_TRAVESSIA
} modo_operacao_t;

// --- Estados do semáforo ---
typedef enum {
    ESTADO_VERDE,
    ESTADO_AMARELO, 
    ESTADO_VERMELHO
} estado_semaforo_t;

// --- Sinais de transição ---
typedef enum {
    SINAL_NENHUM,
    SINAL_VERDE_PARA_AMARELO,
    SINAL_AMARELO_PARA_VERMELHO,
    SINAL_VERMELHO_PARA_VERDE
} sinal_transicao_t;

static const char* modos_str[] = {
    "MODO_NORMAL",
    "MODO_NOTURNO", 
    "MODO_TRAVESSIA"
};

// --- Variáveis globais ---
static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);
static const struct gpio_dt_spec led_amarelo = GPIO_DT_SPEC_GET(LED_AMARELO_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static const struct gpio_dt_spec button_2 = GPIO_DT_SPEC_GET(BUTTON_NODE_2, gpios);
static struct gpio_callback button_cb_data;
static struct gpio_callback button_cb_data_2;

// Mutex para controle de acesso ao estado
K_MUTEX_DEFINE(mutex_estado);

// Variáveis de estado do sistema
static volatile modo_operacao_t modo_atual = MODO_NORMAL;
static volatile estado_semaforo_t estado_atual = ESTADO_VERDE;
static volatile bool travessia_solicitada = false;
static volatile bool sistema_rodando = false; // Enquanto eu não tiver o Júlio, isso é true. Se eu estiver com ele, é false para testar integração.
static volatile bool transicao_controlada = false; // Para evitar transição abrupta para o modo travessia
static uint32_t timer_travessia = 0;
static uint32_t timer_modo = 0;
static volatile sinal_transicao_t sinal_transicao = SINAL_NENHUM;

// Semaforo para sincronização entre threads
K_SEM_DEFINE(semaforo_verde, 0, 1);
K_SEM_DEFINE(semaforo_amarelo, 0, 1);
K_SEM_DEFINE(semaforo_vermelho, 0, 1);
K_SEM_DEFINE(semaforo_transicao, 0, 1);


// Stack das threads
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(thread_controle_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_verde_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_amarelo_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_vermelho_stack, STACK_SIZE);

// Estruturas das threads
static struct k_thread thread_controle_data;
static struct k_thread thread_verde_data;
static struct k_thread thread_amarelo_data;
static struct k_thread thread_vermelho_data;

// --- Protótipos das funções ---
void thread_controle_fn(void *arg1, void *arg2, void *arg3);
void thread_verde_fn(void *arg1, void *arg2, void *arg3);
void thread_amarelo_fn(void *arg1, void *arg2, void *arg3);
void thread_vermelho_fn(void *arg1, void *arg2, void *arg3);
void controlar_led(const struct gpio_dt_spec *led, bool estado);
void atualizar_estado_semaforo(estado_semaforo_t novo_estado);
void processar_modo_normal(void);
void processar_modo_noturno(void);
void processar_modo_travessia(void);
void iniciar_transicao(sinal_transicao_t sinal);

/**
 * FUNÇÃO: controlar_led()
 * Controla um LED específico com exclusão mútua
 */
void controlar_led(const struct gpio_dt_spec *led, bool estado)
{
    if (estado) {
        gpio_pin_set_dt(led, 1);
    } else {
        gpio_pin_set_dt(led, 0);
    }
}

/**
 * FUNÇÃO: atualizar_estado_semaforo()
 * Atualiza o estado atual do semáforo e controla os LEDs
 * Garante que apenas um estado esteja ativo por vez
 */
void atualizar_estado_semaforo(estado_semaforo_t novo_estado)
{
    k_mutex_lock(&mutex_estado, K_FOREVER);
    
    // Primeiro, apaga todos os LEDs
    gpio_pin_set_dt(&led_verde, false);
    gpio_pin_set_dt(&led_amarelo, false);
    gpio_pin_set_dt(&led_vermelho, false);
    
    // Acende apenas o LED do estado atual
    switch (novo_estado) {
        case ESTADO_VERDE:
            gpio_pin_set_dt(&led_verde, true);
            estado_atual = ESTADO_VERDE;
            LOG_INF("Estado: VERDE");
            break;
            
        case ESTADO_AMARELO:
            gpio_pin_set_dt(&led_amarelo, true);
            estado_atual = ESTADO_AMARELO;
            LOG_INF("Estado: AMARELO");
            break;
            
        case ESTADO_VERMELHO:
            gpio_pin_set_dt(&led_vermelho, true);
            estado_atual = ESTADO_VERMELHO;
            LOG_INF("Estado: VERMELHO");
            break;
    }
    
    k_mutex_unlock(&mutex_estado);
}

/**
 * FUNÇÃO: iniciar_transicao()
 * Inicia uma transição de estado de forma sincronizada
 */
void iniciar_transicao(sinal_transicao_t sinal)
{
    k_mutex_lock(&mutex_estado, K_FOREVER);
    sinal_transicao = sinal;
    k_mutex_unlock(&mutex_estado);
    
    k_sem_give(&semaforo_transicao);
}

/**
 * FUNÇÃO: thread_controle_fn()
 * Thread principal que controla as transições de estado
 */
void thread_controle_fn(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("Thread controle iniciada");
    
    while (sistema_rodando) {
        // Aguarda sinal de transição
        if (k_sem_take(&semaforo_transicao, K_FOREVER) == 0) {
            k_mutex_lock(&mutex_estado, K_FOREVER);
            sinal_transicao_t sinal = sinal_transicao;
            sinal_transicao = SINAL_NENHUM;
            k_mutex_unlock(&mutex_estado);
            
            // Processa a transição
            switch (sinal) {
                case SINAL_VERDE_PARA_AMARELO:
                    if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_VERDE) {
                        atualizar_estado_semaforo(ESTADO_AMARELO);
                        k_sem_give(&semaforo_amarelo);
                    }
                    break;
                    
                case SINAL_AMARELO_PARA_VERMELHO:
                    if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_AMARELO) {
                        atualizar_estado_semaforo(ESTADO_VERMELHO);
                        k_sem_give(&semaforo_vermelho);
                    }
                    break;
                    
                case SINAL_VERMELHO_PARA_VERDE:
                    if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_VERMELHO) {
                        atualizar_estado_semaforo(ESTADO_VERDE);
                        k_sem_give(&semaforo_verde);
                    }
                    break;
                    
                default:
                    break;
            }
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: thread_verde_fn()
 * Thread para controle do estado verde
 */
void thread_verde_fn(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("Thread verde iniciada");
    
    while (sistema_rodando) {
        if (modo_atual == MODO_NORMAL) {
            // Aguarda ativação do estado verde
            if (k_sem_take(&semaforo_verde, K_MSEC(100)) == 0) {
                // Verifica se realmente está no estado verde
                if (estado_atual == ESTADO_VERDE && modo_atual == MODO_NORMAL) {
                    LOG_INF("VERDE: Aguardando %d segundos", TEMPO_VERDE / 1000);
                    k_msleep(TEMPO_VERDE);
                    
                    // Solicita transição para amarelo
                    if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_VERDE) {
                        iniciar_transicao(SINAL_VERDE_PARA_AMARELO);
                    }
                }
            }
        } else {
            // Em outros modos, mantém LED apagado
            controlar_led(&led_verde, false);
            k_msleep(100);
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: thread_amarelo_fn()
 * Thread para controle do estado amarelo
 */
void thread_amarelo_fn(void *arg1, void *arg2, void *arg3)
{
    static bool amarelo_piscante = false;
    LOG_INF("Thread amarelo iniciada");
    
    while (sistema_rodando) {
        switch (modo_atual) {
            case MODO_NORMAL:
                // Aguarda ativação do estado amarelo
                // Só processa se NÃO for uma transição controlada
                if (!transicao_controlada && k_sem_take(&semaforo_amarelo, K_MSEC(100)) == 0) {
                    // Verifica se realmente está no estado amarelo
                    if (estado_atual == ESTADO_AMARELO && modo_atual == MODO_NORMAL && !transicao_controlada) {
                        LOG_INF("AMARELO: Aguardando %d segundos", TEMPO_AMARELO / 1000);
                        k_msleep(TEMPO_AMARELO);
                        
                        // Solicita transição para vermelho
                        if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_AMARELO && !transicao_controlada) {
                            iniciar_transicao(SINAL_AMARELO_PARA_VERMELHO);
                        }
                    }
                }
                break;
                
            case MODO_NOTURNO:
                // Modo noturno: amarelo piscante
                controlar_led(&led_amarelo, amarelo_piscante);
                amarelo_piscante = !amarelo_piscante;
                LOG_INF("AMARELO: Piscando %d segundos", TEMPO_PISCANTE / 1000);
                k_msleep(TEMPO_PISCANTE);
                break;
                
            case MODO_TRAVESSIA:
                // Na travessia, amarelo apagado
                controlar_led(&led_amarelo, false);
                k_msleep(100);
                break;
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: thread_vermelho_fn()
 * Thread para controle do estado vermelho
 */
void thread_vermelho_fn(void *arg1, void *arg2, void *arg3)
{
    LOG_INF("Thread vermelho iniciada");
    
    while (sistema_rodando) {
        switch (modo_atual) {
            case MODO_NORMAL:
                // Aguarda ativação do estado vermelho
                if (k_sem_take(&semaforo_vermelho, K_MSEC(100)) == 0) {
                    // Verifica se realmente está no estado vermelho
                    if (estado_atual == ESTADO_VERMELHO && modo_atual == MODO_NORMAL) {
                        LOG_INF("VERMELHO: Aguardando %d segundos", TEMPO_VERMELHO / 1000);
                        k_msleep(TEMPO_VERMELHO);
                        
                        // Solicita transição para verde
                        if (modo_atual == MODO_NORMAL && estado_atual == ESTADO_VERMELHO) {
                            iniciar_transicao(SINAL_VERMELHO_PARA_VERDE);
                        }
                    }
                }
                break;
                
            case MODO_NOTURNO:
                // No modo noturno, vermelho apagado
                controlar_led(&led_vermelho, false);
                k_msleep(100);
                break;
                
            case MODO_TRAVESSIA:
                // Na travessia, vermelho sempre aceso
                controlar_led(&led_vermelho, true);
                k_msleep(100);
                break;
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: receber_sinal_travessia()
 * Função para ser chamada pelo código do seu amigo
 * Quando o botão de pedestres for pressionado
 */
void receber_sinal_travessia(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    
    if (modo_atual == MODO_NORMAL) {
        travessia_solicitada = true;
        LOG_INF(">>> SINAL RECEBIDO: Travessia solicitada <<<");
    }
}

/**
 * FUNÇÃO: receber_sinal-inicializacao()
 * Função para ser chamada pelo código do seu amigo
 * Quando o botão de inicialização for pressionado
 */
void receber_sinal_inicializacao(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    
    if (!sistema_rodando) {
        sistema_rodando = true;
        LOG_INF(">>> SINAL RECEBIDO: Sistema iniciado via botão PTA17 <<<");
        LOG_INF(">>> Threads serão criadas e sequência iniciada <<<");
    }
}

// --- Função principal ---
void main(void)
{
    int ret;
    
    LOG_INF("=== SEMÁFORO DE VEÍCULOS INICIALIZADO ===");
    LOG_INF("Placa: FRDM-KL25Z");
    LOG_INF("Threads: Controle, Verde, Amarelo (AZUL), Vermelho");

    // Inicialização dos LEDs
    if (!device_is_ready(led_verde.port) || 
        !device_is_ready(led_amarelo.port) || 
        !device_is_ready(led_vermelho.port)) {
        LOG_ERR("GPIOs não prontos!");
        return;
    }

    // Configura LEDs como saída
    ret = gpio_pin_configure_dt(&led_verde, GPIO_OUTPUT_INACTIVE);
    ret |= gpio_pin_configure_dt(&led_amarelo, GPIO_OUTPUT_INACTIVE);
    ret |= gpio_pin_configure_dt(&led_vermelho, GPIO_OUTPUT_INACTIVE);
    
    // Configura botão inicializacao
    gpio_pin_configure_dt(&button_2, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&button_cb_data_2, receber_sinal_inicializacao, BIT(button_2.pin));
    gpio_add_callback(button_2.port, &button_cb_data_2);

    // Configura botão travessia
    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&button_cb_data, receber_sinal_travessia, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    
    if (ret != 0) {
        LOG_ERR("Falha na configuração dos LEDs");
        return;
    }
    
    // --- AGUARDA BOTÃO DE INICIALIZAÇÃO ---
    LOG_INF("Aguardando botão de inicialização (PTA17)...");
    while (!sistema_rodando) {
    k_msleep(100);
    }
    LOG_INF("Sistema inicializado via botão PTA17");

        // Cria as threads com prioridades adequadas
    k_thread_create(&thread_controle_data, thread_controle_stack,
                   K_THREAD_STACK_SIZEOF(thread_controle_stack),
                   thread_controle_fn, NULL, NULL, NULL,
                   K_PRIO_PREEMPT(3), 0, K_NO_WAIT);

    k_thread_create(&thread_verde_data, thread_verde_stack,
                   K_THREAD_STACK_SIZEOF(thread_verde_stack),
                   thread_verde_fn, NULL, NULL, NULL,
                   K_PRIO_PREEMPT(4), 0, K_NO_WAIT);

    k_thread_create(&thread_amarelo_data, thread_amarelo_stack,
                   K_THREAD_STACK_SIZEOF(thread_amarelo_stack),
                   thread_amarelo_fn, NULL, NULL, NULL,
                   K_PRIO_PREEMPT(4), 0, K_NO_WAIT);

    k_thread_create(&thread_vermelho_data, thread_vermelho_stack,
                   K_THREAD_STACK_SIZEOF(thread_vermelho_stack),
                   thread_vermelho_fn, NULL, NULL, NULL,
                   K_PRIO_PREEMPT(4), 0, K_NO_WAIT);

    LOG_INF("Threads criadas: Controle, Verde, Amarelo, Vermelho");
    LOG_INF("Modo atual: %s", modos_str[modo_atual]);

    // Inicia no estado verde APÓS criar todas as threads
    k_msleep(50); // Pequeno delay para garantir que threads iniciaram
    atualizar_estado_semaforo(ESTADO_VERDE);
    k_sem_give(&semaforo_verde); // Inicia a thread verde

    // --- LOOP PRINCIPAL DE CONTROLE ---
    while (1) {
        // Processa o modo atual
        switch (modo_atual) {
            case MODO_NORMAL:
                // Verifica se foi solicitada travessia
                if (travessia_solicitada) {
                    LOG_INF(">>> INICIANDO MODO TRAVESSIA <<<");
                    // BLOQUEIA transições automáticas
                    transicao_controlada = true;

                    // Garante que fique no vermelho durante a travessia DE FORMA SEGURA (presença do amarelo antes)
                    atualizar_estado_semaforo(ESTADO_AMARELO);
                    k_msleep(TEMPO_AMARELO);
                    // LIBERA transições automáticas após completar a transição segura
                    transicao_controlada = false;

                    modo_atual = MODO_TRAVESSIA;
                    timer_travessia = 0;
                    atualizar_estado_semaforo(ESTADO_VERMELHO);

                }
                break;
                
            case MODO_NOTURNO:
                // Timer para voltar ao modo normal
                timer_modo += 100;
                if (timer_modo >= 30000) { // 30 segundos
                    timer_modo = 0;
                    LOG_INF(">>> RETORNANDO PARA MODO NORMAL <<<");
                    modo_atual = MODO_NORMAL;
                    atualizar_estado_semaforo(ESTADO_VERDE);
                    k_sem_give(&semaforo_verde);
                }
                break;
                
            case MODO_TRAVESSIA:
                // Timer para voltar ao modo normal após travessia
                timer_travessia += 100;
                
                if (timer_travessia >= TEMPO_TRAVESSIA) {
                    LOG_INF(">>> FINALIZANDO TRAVESSIA - VOLTANDO AO NORMAL <<<");
                    modo_atual = MODO_NORMAL;
                    travessia_solicitada = false;
                    timer_travessia = 0;
                    // Reinicia o ciclo no verde
                    atualizar_estado_semaforo(ESTADO_VERDE);
                    k_sem_give(&semaforo_verde);
                }
                break;
        }
        
        

        /*
        // Alternância automática entre normal/travessia para teste
        timer_modo += 100;
        
        if (timer_modo >= 15000) { // A cada 15 segundos
            timer_modo = 0;
            
            if (modo_atual == MODO_NORMAL) {
                LOG_INF(">>> ALTERNANDO PARA MODO TRAVESSIA <<<");
                travessia_solicitada = true; // Apenas para teste sem o Júlio
            }
        }
        */
        

        /*
        // Alternância automática entre normal/noturno para teste
        timer_modo += 100;
        
        if (timer_modo >= 30000) { // A cada 30 segundos
            timer_modo = 0;
            
            if (modo_atual == MODO_NORMAL) {
                LOG_INF(">>> ALTERNANDO PARA MODO NOTURNO <<<");
                modo_atual = MODO_NOTURNO;
                // No modo noturno, apenas amarelo pisca
                atualizar_estado_semaforo(ESTADO_AMARELO);
            } else if (modo_atual == MODO_NOTURNO) {
                LOG_INF(">>> RETORNANDO PARA MODO NORMAL <<<");
                modo_atual = MODO_NORMAL;
                atualizar_estado_semaforo(ESTADO_VERDE);
                k_sem_give(&semaforo_verde);
            }
        }
        */
        
        
        k_msleep(100);
    }
}

