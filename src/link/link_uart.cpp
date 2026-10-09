#include "link_uart.h"
#include "driver/uart.h"
#include "hw/pins.h"

namespace slink {

namespace {
constexpr uart_port_t kPort = UART_NUM_2;  // UART_NUM_1 is MIDI OUT
constexpr int kRxBuf = 8192;
}  // namespace

bool uartBegin(uint32_t baud) {
  uart_config_t cfg = {};
  cfg.baud_rate = static_cast<int>(baud);
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(kPort, kRxBuf, 0, 0, nullptr, 0) != ESP_OK) return false;
  if (uart_param_config(kPort, &cfg) != ESP_OK) return false;
  return uart_set_pin(kPort, pins::kLinkTx, pins::kLinkRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK;
}

int uartRead(uint8_t* d, int n, TickType_t wait) {
  size_t have = 0;
  uart_get_buffered_data_len(kPort, &have);
  if (have == 0) {
    // uart_read_bytes waits for all n: take one byte with the wait, then whatever else arrived.
    if (uart_read_bytes(kPort, d, 1, wait) != 1) return 0;
    uart_get_buffered_data_len(kPort, &have);
    const int more = static_cast<int>(have) < n - 1 ? static_cast<int>(have) : n - 1;
    const int r = more > 0 ? uart_read_bytes(kPort, d + 1, more, 0) : 0;
    return 1 + (r > 0 ? r : 0);
  }
  const int r = uart_read_bytes(kPort, d, static_cast<int>(have) < n ? static_cast<int>(have) : n, 0);
  return r < 0 ? 0 : r;
}

int uartWrite(const uint8_t* d, int n) { return uart_write_bytes(kPort, reinterpret_cast<const char*>(d), n); }

}  // namespace slink
