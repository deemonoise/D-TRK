#include "midi_uart.h"
#include "driver/uart.h"
#include "pins.h"

namespace hw {

void MidiUart::begin() {
  uart_config_t cfg = {};
  cfg.baud_rate = 31250;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 1024, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, pins::kMidiTx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
}

void MidiUart::send(const uint8_t* b, uint8_t len) {
  uart_write_bytes(UART_NUM_1, reinterpret_cast<const char*>(b), len);
}

}  // namespace hw
