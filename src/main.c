#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestres, LOG_LEVEL_INF);

// === LEDs ===
#define LED_VERDE_NODE DT_ALIAS(led0)
#define LED_VERMELHO_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);

// === Mutex para exclusão mútua ===
K_MUTEX_DEFINE(led_mutex);

// === Configuração das threads ===
#define STACK_SIZE 512
#define PRIO 5

// === Variável de modo ===
volatile bool modo_noturno = true;

// === Thread: LED Verde ===
void thread_led_verde(void *p1, void *p2, void *p3)
{
    while (1) {
        if (!modo_noturno){
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_verde, 1);
            LOG_INF("Pedestre pode atravessar! (VERDE ligado)");
            k_msleep(4000);  // 4 segundos aceso

            gpio_pin_set_dt(&led_verde, 0);
            LOG_INF("Sinal verde desligado.");

            k_mutex_unlock(&led_mutex);
            k_msleep(10);  // pequeno intervalo para alternância
        }
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
        else {
            k_mutex_lock(&led_mutex, K_FOREVER);

            gpio_pin_set_dt(&led_vermelho, 1);
            LOG_INF("Pedestre deve esperar! (VERMELHO ligado)");
            k_msleep(2000);  // 2 segundos aceso

            gpio_pin_set_dt(&led_vermelho, 0);
            LOG_INF("Sinal vermelho desligado.");

            k_mutex_unlock(&led_mutex);
            k_msleep(10);  // pequeno intervalo para alternância
        }
    }
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

    LOG_INF("=== Semáforo de Pedestres Iniciado ===");
    LOG_INF("Controle de LEDs com exclusão mútua via mutex.");

    while (1) {
        k_sleep(K_FOREVER);
    }
}
