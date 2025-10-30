#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestres, LOG_LEVEL_INF);

// === LEDs ===
#define LED_VERDE_NODE DT_ALIAS(led0)
#define LED_VERMELHO_NODE DT_ALIAS(led2)
#define BUTTON_NODE DT_NODELABEL(user_button_0)

static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);
static const struct gpio_dt_spec botao = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios); //PTA16
static struct gpio_callback botao_cb_data;

// === Mutex para exclusão mútua ===
K_MUTEX_DEFINE(led_mutex);

// === Configuração das threads ===
#define STACK_SIZE 512
#define PRIO 5

// === Estados de modo ===
volatile bool modo_noturno = false;
volatile bool pedido_travessia = false;  // sinalizado pela ISR do botão

// Debounce: mínimo entre pressões em ms
#define DEBOUNCE_MS 200
static atomic_t last_press_ts = ATOMIC_INIT(0);

// === Thread: LED Verde ===
void thread_led_verde(void *p1, void *p2, void *p3)
{
    while (1) {
        if (!modo_noturno && !pedido_travessia){
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_verde, 1);
            LOG_INF("Pedestre pode atravessar! (VERDE ligado)");
            k_msleep(4000);  // 4 segundos aceso

            gpio_pin_set_dt(&led_verde, 0);
            LOG_INF("Sinal verde desligado.");

            k_mutex_unlock(&led_mutex);
            k_msleep(10);  // pequeno intervalo para alternância
        }
        else if (!modo_noturno && pedido_travessia) {
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_verde, 1);
            LOG_INF("Pedido de travessia!");
            k_msleep(15000);  // 5 segundos aceso

            gpio_pin_set_dt(&led_verde, 0);
            LOG_INF("Fim do pedido de travessia.");
            pedido_travessia = false;
            k_mutex_unlock(&led_mutex);

            k_msleep(100); // evita reentrada imediata
        }
        else k_msleep(1000);
    }
}

// === Thread: LED Vermelho ===
void thread_led_vermelho(void *p1, void *p2, void *p3)
{
    while (1) {
        if (modo_noturno){
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_vermelho, 1);
            LOG_INF("Modo Noturno Ativado!");
            k_msleep(1000);  // 1 segundo aceso
            gpio_pin_set_dt(&led_vermelho, 0);
            k_msleep(1000);  // 1 segundo apagado

            k_mutex_unlock(&led_mutex);
            k_msleep(10);
        }
        else if (!modo_noturno && !pedido_travessia) {
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_vermelho, 1);
            LOG_INF("Pedestre deve esperar! (VERMELHO ligado)");
            k_msleep(2000);  // 2 segundos aceso

            gpio_pin_set_dt(&led_vermelho, 0);
            LOG_INF("Sinal vermelho desligado.");

            k_mutex_unlock(&led_mutex);
            k_msleep(10);  // pequeno intervalo para alternância
        }
        else k_msleep(1000);
    }
}

// === Interrupção do botão ===
void botao_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uint32_t now = (uint32_t)k_uptime_get_32();
    uint32_t last = atomic_get(&last_press_ts);

    if ((now - last) < DEBOUNCE_MS) {
        /* ignora bounce */
        return;
    }
    atomic_set(&last_press_ts, now);

    LOG_INF("Botão de travessia pressionado (ISR)");
    pedido_travessia = true;  // sinaliza o pedido de travessia
}

// === Definição das threads ===
K_THREAD_DEFINE(thread_verde_id, STACK_SIZE, thread_led_verde, NULL, NULL, NULL, PRIO, 0, 0);
K_THREAD_DEFINE(thread_vermelho_id, STACK_SIZE, thread_led_vermelho, NULL, NULL, NULL, PRIO, 0, 0);

// === Função principal ===
void main(void)
{
    if (!device_is_ready(led_verde.port) || !device_is_ready(led_vermelho.port)) {
        LOG_INF("Dispositivo de LED não está pronto");
        return;
    }

    gpio_pin_configure_dt(&led_verde, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_vermelho, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&botao, GPIO_INPUT | GPIO_PULL_UP);

    // configurar interrupção do botão (borda de descida = pressionado)
    gpio_pin_interrupt_configure_dt(&botao, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&botao_cb_data, botao_isr, BIT(botao.pin));
    gpio_add_callback(botao.port, &botao_cb_data);

    LOG_INF("=== Semáforo de Pedestres Iniciado ===");
    LOG_INF("Controle de LEDs com exclusão mútua via mutex.");

    while (1) {
        k_sleep(K_FOREVER);
    }
}
