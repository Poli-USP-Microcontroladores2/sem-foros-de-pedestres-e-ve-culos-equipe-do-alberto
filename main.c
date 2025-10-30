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
#define LED_VERMELHO_NODE DT_ALIAS(led1) // LED vermelho (led1)  
#define LED_AMARELO_NODE DT_ALIAS(led2)  // LED azul como amarelo (led2)
#define BUTTON_NODE DT_NODELABEL(user_button_0) // Botão de pedestres

// --- Definições de tempo (em milissegundos) ---
#define TEMPO_VERDE          3000    // 3 segundos
#define TEMPO_AMARELO        1000    // 1 segundo
#define TEMPO_VERMELHO       4000    // 4 segundos
#define TEMPO_TRAVESSIA      5000    // 5 segundos para travessia de pedestres
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

// --- Variáveis globais ---
static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);
static const struct gpio_dt_spec led_amarelo = GPIO_DT_SPEC_GET(LED_AMARELO_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;

// Mutex para controle de acesso aos LEDs
K_MUTEX_DEFINE(mutex_leds);

// Variáveis de estado do sistema
static volatile modo_operacao_t modo_atual = MODO_NORMAL;
static volatile estado_semaforo_t estado_atual = ESTADO_VERDE;
static volatile bool travessia_solicitada = false;
static volatile bool sistema_rodando = true;
static uint32_t timer_travessia = 0;
static uint32_t timer_modo = 0;

// Semaforo para sincronização entre threads
K_SEM_DEFINE(semaforo_controle, 0, 1);

// Stack das threads
#define STACK_SIZE 1024
K_THREAD_STACK_DEFINE(thread_verde_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_amarelo_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_vermelho_stack, STACK_SIZE);

// Estruturas das threads
static struct k_thread thread_verde_data;
static struct k_thread thread_amarelo_data;
static struct k_thread thread_vermelho_data;

// --- Protótipos das funções ---
void thread_verde_fn(void *arg1, void *arg2, void *arg3);
void thread_amarelo_fn(void *arg1, void *arg2, void *arg3);
void thread_vermelho_fn(void *arg1, void *arg2, void *arg3);
void controlar_led(const struct gpio_dt_spec *led, bool estado);
void atualizar_estado_semaforo(estado_semaforo_t novo_estado);
void processar_modo_normal(void);
void processar_modo_noturno(void);
void processar_modo_travessia(void);

/**
 * FUNÇÃO: controlar_led()
 * Controla um LED específico com exclusão mútua
 */
void controlar_led(const struct gpio_dt_spec *led, bool estado)
{
    k_mutex_lock(&mutex_leds, K_FOREVER);
    
    if (estado) {
        gpio_pin_set_dt(led, 1);
    } else {
        gpio_pin_set_dt(led, 0);
    }
    
    k_mutex_unlock(&mutex_leds);
}

/**
 * FUNÇÃO: atualizar_estado_semaforo()
 * Atualiza o estado atual do semáforo e controla os LEDs
 * Garante que apenas um estado esteja ativo por vez
 */
void atualizar_estado_semaforo(estado_semaforo_t novo_estado)
{
    k_mutex_lock(&mutex_leds, K_FOREVER);
    
    // Primeiro, apaga todos os LEDs
    gpio_pin_set_dt(&led_verde, 0);
    gpio_pin_set_dt(&led_amarelo, 0);
    gpio_pin_set_dt(&led_vermelho, 0);
    
    // Acende apenas o LED do estado atual
    switch (novo_estado) {
        case ESTADO_VERDE:
            gpio_pin_set_dt(&led_verde, 1);
            estado_atual = ESTADO_VERDE;
            LOG_INF("Estado: VERDE");
            break;
            
        case ESTADO_AMARELO:
            gpio_pin_set_dt(&led_amarelo, 1);
            estado_atual = ESTADO_AMARELO;
            LOG_INF("Estado: AMARELO");
            break;
            
        case ESTADO_VERMELHO:
            gpio_pin_set_dt(&led_vermelho, 1);
            estado_atual = ESTADO_VERMELHO;
            LOG_INF("Estado: VERMELHO");
            break;
    }
    
    k_mutex_unlock(&mutex_leds);
}

/**
 * FUNÇÃO: thread_verde_fn()
 * Thread independente para controle do LED verde
 * Responde aos diferentes modos de operação
 */
void thread_verde_fn(void *arg1, void *arg2, void *arg3)
{
    while (sistema_rodando) {
        switch (modo_atual) {
            case MODO_NORMAL:
                if (estado_atual == ESTADO_VERDE) {
                    // LED verde aceso por 3 segundos no modo normal
                    controlar_led(&led_verde, true);
                    k_msleep(TEMPO_VERDE);
                    // Transição para amarelo
                    k_sem_give(&semaforo_controle);
                } else {
                    // Mantém LED verde apagado em outros estados
                    controlar_led(&led_verde, false);
                    k_msleep(100);
                }
                break;
                
            case MODO_NOTURNO:
                // No modo noturno, verde sempre apagado
                controlar_led(&led_verde, false);
                k_msleep(100);
                break;
                
            case MODO_TRAVESSIA:
                // Na travessia, verde sempre apagado
                controlar_led(&led_verde, false);
                k_msleep(100);
                break;
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: thread_amarelo_fn()
 * Thread independente para controle do LED amarelo
 */
void thread_amarelo_fn(void *arg1, void *arg2, void *arg3)
{
    static bool amarelo_piscante = false;
    
    while (sistema_rodando) {
        switch (modo_atual) {
            case MODO_NORMAL:
                if (estado_atual == ESTADO_AMARELO) {
                    // LED amarelo aceso por 1 segundo no modo normal
                    controlar_led(&led_amarelo, true);
                    k_msleep(TEMPO_AMARELO);
                    // Transição para vermelho
                    k_sem_give(&semaforo_controle);
                } else {
                    // Mantém LED amarelo apagado em outros estados
                    controlar_led(&led_amarelo, false);
                    k_msleep(100);
                }
                break;
                
            case MODO_NOTURNO:
                // Modo noturno: amarelo piscante (1s ligado, 1s desligado)
                controlar_led(&led_amarelo, amarelo_piscante);
                amarelo_piscante = !amarelo_piscante;
                k_msleep(TEMPO_PISCANTE);
                break;
                
            case MODO_TRAVESSIA:
                // Na travessia, amarelo sempre apagado
                controlar_led(&led_amarelo, false);
                k_msleep(100);
                break;
        }
        k_yield();
    }
}

/**
 * FUNÇÃO: thread_vermelho_fn()
 * Thread independente para controle do LED vermelho
 */
void thread_vermelho_fn(void *arg1, void *arg2, void *arg3)
{
    while (sistema_rodando) {
        switch (modo_atual) {
            case MODO_NORMAL:
                if (estado_atual == ESTADO_VERMELHO) {
                    // LED vermelho aceso por 4 segundos no modo normal
                    controlar_led(&led_vermelho, true);
                    k_msleep(TEMPO_VERMELHO);
                    // Transição para verde
                    k_sem_give(&semaforo_controle);
                } else {
                    // Mantém LED vermelho apagado em outros estados
                    controlar_led(&led_vermelho, false);
                    k_msleep(100);
                }
                break;
                
            case MODO_NOTURNO:
                // No modo noturno, vermelho sempre apagado
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
 * FUNÇÃO: processar_modo_normal()
 * Controla a sequência do ciclo normal do semáforo
 * Verde → Amarelo → Vermelho → Verde...
 */
void processar_modo_normal(void)
{
    static estado_semaforo_t proximo_estado = ESTADO_VERDE;
    
    // Aguarda sinal de transição das threads
    if (k_sem_take(&semaforo_controle, K_MSEC(100)) == 0) {
        // Realiza a transição de estado
        switch (estado_atual) {
            case ESTADO_VERDE:
                proximo_estado = ESTADO_AMARELO;
                break;
            case ESTADO_AMARELO:
                proximo_estado = ESTADO_VERMELHO;
                break;
            case ESTADO_VERMELHO:
                proximo_estado = ESTADO_VERDE;
                break;
        }
        atualizar_estado_semaforo(proximo_estado);
    }
}

/**
 * FUNÇÃO: processar_modo_noturno()
 * Configura o semáforo para modo noturno
 * Apenas amarelo piscante
 */
void processar_modo_noturno(void)
{
    // No modo noturno, apenas a thread do amarelo está ativa (piscando)
    // Verde e vermelho são mantidos apagados pelas suas threads
    k_msleep(100);
}

/**
 * FUNÇÃO: processar_modo_travessia()
 * Configura o semáforo para modo travessia
 * Vermelho fixo para veículos
 */
void processar_modo_travessia(void)
{
    // Na travessia, apenas vermelho aceso para veículos
    // As threads mantêm verde e amarelo apagados, vermelho aceso
    k_msleep(100);
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

// --- Função principal ---
void main(void)
{
    int ret;
    
    LOG_INF("=== SEMÁFORO DE VEÍCULOS INICIALIZADO ===");
    LOG_INF("Placa: FRDM-KL25Z");
    LOG_INF("Threads: Verde, Amarelo (AZUL), Vermelho");
    LOG_INF("Aguardando sinal do semáforo de pedestres...");

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
    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);

    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&button_cb_data, receber_sinal_travessia, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    
    if (ret != 0) {
        LOG_ERR("Falha na configuração dos LEDs");
        return;
    }

    // Cria as 3 threads independentes
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

    LOG_INF("Threads criadas: Verde, Amarelo, Vermelho");
    LOG_INF("Modo inicial: NORMAL");

    // Inicia no estado verde
    atualizar_estado_semaforo(ESTADO_VERDE);

    // --- LOOP PRINCIPAL DE CONTROLE ---
    while (1) {
        // Processa o modo atual
        switch (modo_atual) {
            case MODO_NORMAL:
                processar_modo_normal();
                
                // Verifica se foi solicitada travessia
                if (travessia_solicitada) {
                    LOG_INF(">>> INICIANDO MODO TRAVESSIA <<<");
                    modo_atual = MODO_TRAVESSIA;
                    timer_travessia = 0;
                    // Garante que fique no vermelho durante a travessia
                    atualizar_estado_semaforo(ESTADO_VERMELHO);
                }
                break;
                
            case MODO_NOTURNO:
                processar_modo_noturno();
                break;
                
            case MODO_TRAVESSIA:
                processar_modo_travessia();
                
                // Timer para voltar ao modo normal após travessia
                timer_travessia += 100;
                
                if (timer_travessia >= TEMPO_TRAVESSIA) {
                    LOG_INF(">>> FINALIZANDO TRAVESSIA - VOLTANDO AO NORMAL <<<");
                    modo_atual = MODO_NORMAL;
                    travessia_solicitada = false;
                    timer_travessia = 0;
                    // Reinicia o ciclo no verde
                    atualizar_estado_semaforo(ESTADO_VERDE);
                }
                break;
        }
        
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
            }
        }
        
        k_msleep(100);
    }
}