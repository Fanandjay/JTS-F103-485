/* Executes the actual transport, RTU parser, measurement and register adapter
 * against fake UART/TIM/GPIO/ADC devices. No target hardware is required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ModBus.h"
#include "app_modbus.h"
#include "app_config.h"

GPIO_TypeDef test_gpio_a, test_gpio_b;
UART_HandleTypeDef huart1, huart2;
TIM_HandleTypeDef htim2;
static RS485_Handle_t transport;
static ModBus_Handle_t protocol;
static uint8_t tx[256];
static uint16_t tx_length;
static unsigned int checks, groups, transmissions, vofa_frames;
static uint32_t primask, ipsr, tick;
static HAL_StatusTypeDef tx_result;
static int32_t adc_value;
static uint8_t adc_ready;
static float vofa[8];

#define CHECK(x) do { checks++; if (!(x)) { \
    printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define GROUP(name) do { groups++; printf("PASS %s\n", (name)); } while (0)

uint32_t __get_PRIMASK(void) { return primask; }
uint32_t __get_IPSR(void) { return ipsr; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t value) { primask = value; }
void __DMB(void) { }
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) port->output |= pin;
    else port->output &= ~pin;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte,
                                     uint16_t count)
{
    CHECK(count == 1U);
    if (uart->RxState != HAL_UART_STATE_READY) return HAL_BUSY;
    uart->rx_target = byte;
    uart->RxState = HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, const uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
    CHECK(uart == &huart2);
    CHECK((GPIOA->output & RS485_EN_Pin) != 0U);
    CHECK(primask == 0U && ipsr == 0U);
    CHECK(timeout == 50U);
    CHECK(length <= sizeof(tx));
    memcpy(tx, data, length);
    tx_length = length;
    transmissions++;
    return tx_result; /* HAL's blocking API returns after TC on success. */
}

HAL_StatusTypeDef ADS1220_ReadDataContinuous(ADS1220_Handle_t *adc, int32_t *raw)
{
    (void)adc;
    if (!adc_ready) return HAL_ERROR;
    *raw = adc_value;
    adc_ready = 0U;
    return HAL_OK;
}
uint8_t ADS1220_IsDataReady(ADS1220_Handle_t *adc)
{
    (void)adc; return adc_ready;
}
uint8_t ADS1220_GetGainValue(ADS1220_Handle_t *adc)
{
    return (uint8_t)(1U << adc->gain);
}
float ADS1220_RawToVoltage(int32_t raw, float reference, uint8_t gain)
{
    return (float)raw * reference / ((float)gain * 8388608.0f);
}
void App_Report_Printf(const char *format, ...) { (void)format; }
HAL_StatusTypeDef VOFA_JustFloat_Send(UART_HandleTypeDef *uart, const float *data,
                                      uint8_t channels)
{
    CHECK(uart == &huart1 && channels == 8U);
    memcpy(vofa, data, sizeof(vofa));
    vofa_frames++;
    return HAL_OK;
}

static uint8_t read_registers(void *context, uint8_t function, uint16_t start,
                               uint16_t count, uint16_t *values)
{
    uint16_t i;
    (void)context; (void)function;
    if ((uint32_t)start + count > 125U) return MODBUS_EXCEPTION_ADDRESS;
    for (i = 0U; i < count; i++) values[i] = (uint16_t)(0x1000U + start + i);
    return 0U;
}

static void reset_devices(uint32_t baud)
{
    memset(&huart1, 0, sizeof(huart1));
    memset(&huart2, 0, sizeof(huart2));
    memset(&htim2, 0, sizeof(htim2));
    huart2.Init.BaudRate = baud;
    huart2.Init.WordLength = UART_WORDLENGTH_9B;
    huart2.Init.Parity = UART_PARITY_EVEN;
    huart2.Init.StopBits = UART_STOPBITS_1;
    htim2.Init.Prescaler = 71U;
    htim2.Init.Period = 1749U;
    transmissions = 0U; tx_length = 0U; vofa_frames = 0U;
    tx_result = HAL_OK;
    primask = 0U; ipsr = 0U; tick = 100U;
    test_gpio_a.output = 0U;
}

static void timeout_generic(void)
{
    htim2.flags |= TIM_FLAG_UPDATE;
    htim2.counter = 0U;
    ModBus_TimerIRQ(&protocol);
}

static void fixture(uint32_t baud)
{
    reset_devices(baud);
    CHECK(RS485_Init(&transport, &huart2, GPIOA, RS485_EN_Pin,
                     GPIO_PIN_SET, 50U) == HAL_OK);
    CHECK(ModBus_Init(&protocol, &transport, &htim2, 1U,
                      read_registers, NULL) == HAL_OK);
    timeout_generic();
}

static uint16_t append_crc(uint8_t *frame, uint16_t length)
{
    uint16_t crc = ModBus_CRC16(frame, length);
    frame[length++] = (uint8_t)crc;
    frame[length++] = (uint8_t)(crc >> 8);
    return length;
}

static uint16_t request(uint8_t *frame, uint8_t address, uint8_t function,
                         uint16_t start, uint16_t count)
{
    frame[0] = address; frame[1] = function;
    frame[2] = (uint8_t)(start >> 8); frame[3] = (uint8_t)start;
    frame[4] = (uint8_t)(count >> 8); frame[5] = (uint8_t)count;
    return append_crc(frame, 6U);
}

static void rx_byte(uint8_t byte, uint32_t interval, uint8_t app)
{
    CHECK(huart2.RxState == HAL_UART_STATE_BUSY_RX);
    *huart2.rx_target = byte;
    huart2.RxState = HAL_UART_STATE_READY;
    htim2.counter = interval;
    if (app) App_ModBus_OnRxComplete(&huart2);
    else RS485_OnRxComplete(&transport, &huart2);
}

static void receive(const uint8_t *frame, uint16_t length, uint8_t app)
{
    uint16_t i;
    for (i = 0U; i < length; i++) rx_byte(frame[i], 100U, app);
    if (app)
    {
        htim2.flags |= TIM_FLAG_UPDATE;
        htim2.counter = 0U;
        App_ModBus_TimerIRQ();
        App_ModBus_Poll();
    }
    else
    {
        timeout_generic();
        ModBus_Poll(&protocol);
    }
}

static void check_crc(void)
{
    uint16_t crc = ModBus_CRC16(tx, (uint16_t)(tx_length - 2U));
    CHECK(tx[tx_length - 2U] == (uint8_t)crc);
    CHECK(tx[tx_length - 1U] == (uint8_t)(crc >> 8));
    CHECK((GPIOA->output & RS485_EN_Pin) == 0U);
}

static uint16_t response_register(uint16_t address)
{
    return (uint16_t)(((uint16_t)tx[3U + address * 2U] << 8) |
                       tx[4U + address * 2U]);
}

static void test_crc_and_packing(void)
{
    uint8_t frame[8];
    uint16_t pair[2];
    CHECK(ModBus_CRC16((const uint8_t *)"123456789", 9U) == 0x4B37U);
    request(frame, 1U, 3U, 0U, 4U);
    CHECK(frame[6] == 0x44U && frame[7] == 0x09U);
    ModBus_PackInt32(pair, -123456);
    CHECK(pair[0] == 0xFFFEU && pair[1] == 0x1DC0U);
    ModBus_PackFloat(pair, 1.0f);
    CHECK(pair[0] == 0x3F80U && pair[1] == 0U);
    ModBus_PackFloat(pair, -10.0f);
    CHECK(pair[0] == 0xC120U && pair[1] == 0U);
    GROUP("CRC reference vectors and ABCD signed/float packing");
}

static void test_reads(void)
{
    uint8_t frame[8];
    fixture(115200U);
    CHECK(protocol.frame_timeout_us == 1750U && htim2.autoreload == 1749U);
    request(frame, 1U, 3U, 0U, 4U);
    receive(frame, 8U, 0U);
    CHECK(transmissions == 1U && tx_length == 13U);
    CHECK(tx[0] == 1U && tx[1] == 3U && tx[2] == 8U);
    CHECK(response_register(0U) == 0x1000U && response_register(3U) == 0x1003U);
    check_crc();
    request(frame, 1U, 4U, 124U, 1U);
    receive(frame, 8U, 0U);
    CHECK(tx_length == 7U && tx[1] == 4U && response_register(0U) == 0x107CU);
    request(frame, 1U, 3U, 0U, 125U);
    receive(frame, 8U, 0U);
    CHECK(tx_length == 255U && tx[2] == 250U && response_register(124U) == 0x107CU);
    check_crc();
    GROUP("03/04 reads, nonzero start, maximum 125-register response");
}

static void test_silent_frames(void)
{
    uint8_t frame[8];
    fixture(115200U);
    request(frame, 0U, 3U, 0U, 4U); receive(frame, 8U, 0U);
    request(frame, 2U, 3U, 0U, 4U); receive(frame, 8U, 0U);
    request(frame, 1U, 3U, 0U, 4U); frame[6] ^= 1U; receive(frame, 8U, 0U);
    CHECK(transmissions == 0U && protocol.crc_errors == 1U);
    request(frame, 1U, 3U, 0U, 4U); receive(frame, 8U, 0U);
    CHECK(transmissions == 1U);
    GROUP("broadcast, other station and bad CRC stay silent; next frame recovers");
}

static void expect_exception(uint8_t function, uint16_t start,
                              uint16_t count, uint8_t exception)
{
    uint8_t frame[8];
    request(frame, 1U, function, start, count); receive(frame, 8U, 0U);
    CHECK(tx_length == 5U && tx[1] == (uint8_t)(function | 0x80U));
    CHECK(tx[2] == exception);
    check_crc();
}

static void test_exceptions(void)
{
    uint8_t frame[8] = {1U, 3U, 0U, 0U};
    fixture(115200U);
    expect_exception(6U, 0U, 1U, 1U);
    expect_exception(3U, 0U, 0U, 3U);
    expect_exception(3U, 0U, 126U, 3U);
    expect_exception(4U, 124U, 2U, 2U);
    expect_exception(3U, 65535U, 2U, 2U);
    receive(frame, append_crc(frame, 4U), 0U);
    CHECK(tx_length == 5U && tx[2] == 3U);
    GROUP("unsupported function, invalid length/quantity and address overflow exceptions");
}

static void test_framing(void)
{
    uint8_t frame[8];
    uint16_t i;
    fixture(115200U);
    request(frame, 1U, 3U, 0U, 4U);
    for (i = 0U; i < 8U; i++) rx_byte(frame[i], 100U, 0U);
    ModBus_Poll(&protocol);
    CHECK(transmissions == 0U);
    ModBus_TimerIRQ(&protocol); /* Spurious interrupt without UIF. */
    CHECK(protocol.frame_ready == 0U && protocol.receiving != 0U);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(transmissions == 1U);
    for (i = 0U; i < 8U; i++)
        rx_byte(frame[i], i == 3U ? protocol.byte_timeout_us + 1U : 100U, 0U);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(transmissions == 1U);
    receive(frame, 8U, 0U);
    CHECK(transmissions == 2U);
    GROUP("t3.5 required, spurious IRQ ignored, t1.5 violation discarded and recovery");
}

static void test_timer_race_and_queue(void)
{
    uint8_t first[8], second[8];
    uint16_t i;
    fixture(115200U);
    request(first, 1U, 3U, 0U, 1U);
    request(second, 1U, 4U, 1U, 1U);
    for (i = 0U; i < 8U; i++) rx_byte(first[i], 100U, 0U);
    htim2.flags |= TIM_FLAG_UPDATE;
    rx_byte(second[0], 0U, 0U); /* UART wins against pending TIM IRQ. */
    CHECK(protocol.frame_ready == 1U && protocol.rx_length == 1U);
    ModBus_TimerIRQ(&protocol); /* The stale IRQ must not end the new frame. */
    CHECK(protocol.rx_length == 1U && protocol.receiving == 1U);
    ModBus_Poll(&protocol);
    CHECK(tx[1] == 3U && response_register(0U) == 0x1000U);
    for (i = 1U; i < 8U; i++) rx_byte(second[i], 100U, 0U);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(tx[1] == 4U && response_register(0U) == 0x1001U);

    for (i = 0U; i < 8U; i++) rx_byte(first[i], 100U, 0U);
    timeout_generic();
    for (i = 0U; i < 8U; i++) rx_byte(second[i], 100U, 0U);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(tx[1] == 3U && response_register(0U) == 0x1000U);
    CHECK(protocol.frame_ready == 0U);
    GROUP("UART/TIM pending race and pending-frame ownership");
}

static void test_noise_and_overflow(void)
{
    uint8_t frame[8];
    uint16_t i;
    fixture(115200U);
    request(frame, 1U, 3U, 0U, 1U);
    for (i = 0U; i < 257U; i++) rx_byte(0x55U, 100U, 0U);
    for (i = 0U; i < 8U; i++) rx_byte(frame[i], 100U, 0U);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(transmissions == 0U);
    for (i = 0U; i < 8U; i++) rx_byte(frame[i], 100U, 0U);
    huart2.RxState = HAL_UART_STATE_READY; /* ORE ended the receive operation. */
    RS485_OnError(&transport, &huart2);
    CHECK(huart2.RxState == HAL_UART_STATE_BUSY_RX);
    timeout_generic(); ModBus_Poll(&protocol);
    CHECK(transmissions == 0U);
    receive(frame, 8U, 0U);
    CHECK(transmissions == 1U);
    GROUP("oversized frame and UART error discarded, receive rearmed");
}

static void test_transport_failures(void)
{
    uint8_t frame[8];
    fixture(115200U);
    request(frame, 1U, 3U, 0U, 1U);
    tx_result = HAL_TIMEOUT;
    receive(frame, 8U, 0U);
    CHECK(protocol.transmit_errors == 1U && transport.transmitting == 0U);
    CHECK((GPIOA->output & RS485_EN_Pin) == 0U);
    tx_result = HAL_OK;
    receive(frame, 8U, 0U);
    CHECK(transmissions == 2U);
    ipsr = 1U;
    CHECK(RS485_Send(&transport, frame, 8U) == HAL_ERROR);
    ipsr = 0U; primask = 1U;
    CHECK(RS485_Send(&transport, frame, 8U) == HAL_ERROR && primask == 1U);
    primask = 0U;
    GROUP("transmit timeout restores DE; ISR/interrupt-masked sends rejected");
}

static void test_init_and_low_baud(void)
{
    fixture(9600U);
    CHECK(protocol.frame_timeout_us == 4011U && htim2.autoreload == 4010U);
    CHECK(protocol.byte_timeout_us == 2865U);
    CHECK(ModBus_Init(&protocol, &transport, &htim2, 0U,
                      read_registers, NULL) == HAL_ERROR);
    CHECK(ModBus_Init(&protocol, &transport, &htim2, 248U,
                      read_registers, NULL) == HAL_ERROR);
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    CHECK(RS485_Init(&transport, &huart2, GPIOA, RS485_EN_Pin,
                     GPIO_PIN_SET, 50U) == HAL_ERROR);
    GROUP("invalid configuration rejected; 9600 baud timing scales with characters");
}

static void sample_adc(App_Measure_t *measure, int32_t raw)
{
    adc_value = raw; adc_ready = 1U; tick += 50U;
    HAL_GPIO_EXTI_Callback(ADS1220_DRDY_Pin);
    App_Measure_Poll(measure);
}

static void test_measurement_and_registers(void)
{
    App_Measure_t measure;
    ADS1220_Handle_t adc;
    uint8_t frame[8];
    uint16_t i;
    uint32_t bits;
    float voltage;
    reset_devices(115200U);
    memset(&adc, 0, sizeof(adc)); adc.gain = ADS1220_G128;
    App_Measure_Init(&measure, &adc, &huart1);
    CHECK(App_ModBus_Init(&measure) == HAL_OK);
    htim2.flags |= TIM_FLAG_UPDATE; App_ModBus_TimerIRQ();
    request(frame, 1U, 3U, 0U, 14U); receive(frame, 8U, 1U);
    CHECK(response_register(8U) == APP_MODBUS_STATUS_STALE);
    CHECK(response_register(12U) == 0xFFFFU && response_register(13U) == 0xFFFFU);

    sample_adc(&measure, 1073742);
    CHECK(measure.latest.valid == 1U && measure.latest.raw_code == 1073742);
    CHECK(vofa_frames == 0U && measure.output_enabled == 0U);
    request(frame, 1U, 4U, 0U, 14U); receive(frame, 8U, 1U);
    CHECK(tx_length == 33U && response_register(0U) == 0x10U &&
          response_register(1U) == 0x624EU);
    bits = ((uint32_t)response_register(2U) << 16) | response_register(3U);
    memcpy(&voltage, &bits, sizeof(voltage));
    CHECK(fabs(voltage - 5.0f) < 0.00001);
    CHECK(response_register(8U) == APP_MODBUS_STATUS_VALID);
    CHECK(response_register(9U) == 128U && response_register(11U) == 1U);
    CHECK(response_register(13U) == 0U);
    check_crc();

    App_Measure_SetOutput(&measure, 1U);
    sample_adc(&measure, -1073742);
    CHECK(vofa_frames == 1U && vofa[0] == -1073742.0f);
    CHECK(vofa[2] == (float)measure.latest.filtered_code &&
          vofa[3] == measure.latest.filtered_voltage_mv);
    App_Measure_SetOutput(&measure, 0U);
    sample_adc(&measure, -2000);
    CHECK(vofa_frames == 1U && measure.latest.raw_code == -2000);
    request(frame, 1U, 3U, 0U, 4U); receive(frame, 8U, 1U);
    CHECK(response_register(0U) == 0xFFFFU && response_register(1U) == 0xF830U);
    bits = ((uint32_t)response_register(2U) << 16) | response_register(3U);
    memcpy(&voltage, &bits, sizeof(voltage));
    CHECK(voltage < 0.0f);

    App_Measure_StartTare(&measure);
    for (i = 0U; i < APP_TARE_SAMPLES; i++) sample_adc(&measure, 1000);
    CHECK(measure.zero_offset == 1000 && measure.zero_valid == 1U &&
          measure.tare_running == 0U && measure.latest.filtered_code == 0);
    CHECK(measure.latest.raw_code == 1000 && measure.latest.raw_voltage_mv > 0.0f);
    CHECK(measure.latest.filtered_voltage_mv == 0.0f);
    sample_adc(&measure, -1); /* Existing disconnected/all-ones sample policy. */
    CHECK(measure.latest.raw_code == 1000 && measure.invalid_sample_count == 1U);
    tick += APP_MODBUS_STALE_MS;
    request(frame, 1U, 3U, 0U, 14U); receive(frame, 8U, 1U);
    CHECK((response_register(8U) & APP_MODBUS_STATUS_STALE) != 0U);
    CHECK(response_register(13U) >= APP_MODBUS_STALE_MS);
    request(frame, 1U, 3U, 13U, 2U); receive(frame, 8U, 1U);
    CHECK(tx_length == 5U && tx[2] == MODBUS_EXCEPTION_ADDRESS);
    GROUP("continuous ADC, signed/float register values, VOFA isolation, tare and stale status");
}

int main(void)
{
    test_crc_and_packing();
    test_reads();
    test_silent_frames();
    test_exceptions();
    test_framing();
    test_timer_race_and_queue();
    test_noise_and_overflow();
    test_transport_failures();
    test_init_and_low_baud();
    test_measurement_and_registers();
    printf("ALL PASS: %u groups, %u checks\n", groups, checks);
    return 0;
}
