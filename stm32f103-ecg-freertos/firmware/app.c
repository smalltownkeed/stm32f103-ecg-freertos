#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "protocol.h"
#include "history.h"
#include <string.h>

enum { ADC_PRIORITY = 4, COMM_PRIORITY = 2, MONITOR_PRIORITY = 1 };
static StaticTask_t adc_tcb, comm_tcb, monitor_tcb, idle_tcb;
static StackType_t adc_stack[256], comm_stack[448], monitor_stack[160], idle_stack[128];
static TaskHandle_t adc_handle, comm_handle, monitor_handle;
static history_t history;
static volatile uint32_t diagnostics[12];
static uint8_t transmit_buffer[FRAME_CAPACITY];
static parser_t command_parser;
static uint32_t session;
static uint32_t live_sequence;
static uint32_t repair_sequence;
static uint16_t repair_count;
static uint8_t status_due = 1;
static volatile uint8_t diagnostics_due;
volatile unsigned assertion_line;

void app_assert(const char *file, unsigned line)
{
    (void)file;
    assertion_line = line;
    taskDISABLE_INTERRUPTS();
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    app_assert(__FILE__, __LINE__);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *words)
{
    *tcb = &idle_tcb;
    *stack = idle_stack;
    *words = 128;
}

void app_adc_interrupt(void)
{
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(adc_handle, &wake);
    portYIELD_FROM_ISR(wake);
}

static void acquisition_task(void *argument)
{
    (void)argument;
    uint32_t expected = 0;
    unsigned wave_index = 0;
    board_start_sampling();
    for (;;) {
        uint32_t notices = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t generation = adc_generation;
        unsigned half = adc_ready_half;
        uint32_t start = board_cycles();
        uint32_t latency = (start - adc_event_cycles) / 72;
        if (latency > diagnostics[4]) diagnostics[4] = latency;
        if (notices != 1 || generation != expected + 1 || !board_adc_valid(half, generation)) {
            board_stop_sampling();
            vTaskSuspend(NULL);
        }
        uint16_t values[3];
        adc_average(board_adc_half(half), values);
        if (!board_adc_valid(half, generation) || history.next >= 0xfffffff0u) {
            board_stop_sampling();
            vTaskSuspend(NULL);
        }
        history_append(&history, values);
        expected = generation;
        wave_index = (wave_index + 1) % 200;
        board_set_wave(wave_index);
        diagnostics[1]++;
        uint32_t elapsed = (board_cycles() - start) / 72;
        if (elapsed > diagnostics[5]) diagnostics[5] = elapsed;
    }
}

static void send_packet(packet_t *packet)
{
    packet->session = session;
    size_t size = packet_encode(packet, transmit_buffer);
    configASSERT(size != 0);
    board_uart_send(transmit_buffer, size);
}

static void send_status(packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    taskENTER_CRITICAL();
    uint32_t next = history.next;
    uint32_t oldest = history_oldest(&history);
    taskEXIT_CRITICAL();
    packet->type = PKT_STATUS;
    packet->sequence = next;
    packet->size = 20;
    write_u16(packet->payload, ECG_SAMPLE_RATE);
    write_u16(packet->payload + 2, ECG_CHANNELS);
    write_u32(packet->payload + 4, oldest);
    write_u32(packet->payload + 8, next);
    write_u32(packet->payload + 12, adc_faults);
    write_u32(packet->payload + 16, uart_errors + command_parser.errors);
    send_packet(packet);
}

static int send_samples(packet_t *packet, uint32_t first, uint16_t count)
{
    memset(packet, 0, sizeof(*packet));
    packet->type = PKT_DATA;
    packet->sequence = first;
    for (unsigned i = 0; i < count; i++) {
        uint16_t values[3];
        /* Lock one sample, not a full packet. UART IRQ4 remains serviceable. */
        taskENTER_CRITICAL();
        int result = history_get(&history, first + i, values);
        uint32_t oldest = history_oldest(&history);
        taskEXIT_CRITICAL();
        if (result < 0) {
            packet->type = PKT_GAP;
            packet->count = 0;
            packet->size = 8;
            write_u32(packet->payload, first);
            write_u32(packet->payload + 4, oldest);
            send_packet(packet);
            return -1;
        }
        if (!result) break;
        for (unsigned c = 0; c < 3; c++) write_u16(packet->payload + i * 6 + c * 2, values[c]);
        packet->count++;
    }
    if (!packet->count) return 0;
    packet->size = packet->count * 6;
    send_packet(packet);
    return packet->count;
}

static void handle_command(const packet_t *packet)
{
    if (packet->type == CMD_HELLO) {
        status_due = 1;
    } else if (packet->type == CMD_BIND && packet->session) {
        session = packet->session;
        repair_count = 0;
        taskENTER_CRITICAL();
        live_sequence = history.next;
        taskEXIT_CRITICAL();
        status_due = 1;
    } else if (packet->type == CMD_READ && packet->session == session && session) {
        repair_sequence = packet->sequence;
        repair_count = packet->count;
    } else {
        command_parser.errors++;
    }
}

static void communication_task(void *argument)
{
    (void)argument;
    packet_t packet;
    TickType_t last_byte = 0, last_status = 0;
    for (;;) {
        uint8_t byte;
        TickType_t now = xTaskGetTickCount();
        if (command_parser.used && now - last_byte > 100) command_parser.used = 0;
        while (board_uart_read(&byte)) {
            last_byte = now;
            if (parser_push(&command_parser, byte, &packet)) handle_command(&packet);
        }
        diagnostics[2]++;
        if (!board_uart_busy()) {
            if (status_due || now - last_status >= 1000) {
                status_due = 0;
                last_status = now;
                send_status(&packet);
            } else if (diagnostics_due) {
                diagnostics_due = 0;
                memset(&packet, 0, sizeof(packet));
                packet.type = PKT_DIAG;
                packet.size = 48;
                for (unsigned i = 0; i < 12; i++) write_u32(packet.payload + i * 4, diagnostics[i]);
                send_packet(&packet);
            } else if (session) {
                taskENTER_CRITICAL();
                uint32_t next = history.next;
                uint32_t oldest = history_oldest(&history);
                taskEXIT_CRITICAL();
                if (live_sequence < oldest) {
                    send_samples(&packet, live_sequence, 25);
                    live_sequence = oldest;
                } else if (next - live_sequence >= 25 || (adc_faults && next > live_sequence)) {
                    int count = send_samples(&packet, live_sequence, 25);
                    if (count > 0) live_sequence += (uint32_t)count;
                } else if (repair_count) {
                    send_samples(&packet, repair_sequence, repair_count);
                    repair_count = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void monitor_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        diagnostics[0] = xTaskGetTickCount();
        diagnostics[3]++;
        diagnostics[6] = uxTaskGetStackHighWaterMark(adc_handle);
        diagnostics[7] = uxTaskGetStackHighWaterMark(comm_handle);
        diagnostics[8] = uxTaskGetStackHighWaterMark(monitor_handle);
        diagnostics[9] = uxTaskPriorityGet(adc_handle);
        diagnostics[10] = uxTaskPriorityGet(comm_handle);
        diagnostics[11] = uxTaskPriorityGet(monitor_handle);
        diagnostics_due = 1;
        if (!adc_faults) board_led_toggle();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    board_init();
    adc_handle = xTaskCreateStatic(acquisition_task, "adc", 256, NULL, ADC_PRIORITY, adc_stack, &adc_tcb);
    comm_handle = xTaskCreateStatic(communication_task, "comm", 448, NULL, COMM_PRIORITY, comm_stack, &comm_tcb);
    monitor_handle = xTaskCreateStatic(monitor_task, "monitor", 160, NULL, MONITOR_PRIORITY, monitor_stack, &monitor_tcb);
    configASSERT(adc_handle && comm_handle && monitor_handle);
    vTaskStartScheduler();
    app_assert(__FILE__, __LINE__);
}
