#include <unity.h>
#include <stdint.h>

// Native W1C/interrupt model. SDIO_DRIVER_EXTRACT optionally compiles the actual SDK function
// bodies emitted by the read-only extraction harness; the default is the ownership model.
constexpr uint32_t CC = 1, TC = 2, DINT = 8;
constexpr uint32_t CMD_ERRORS = 0x000F0000, DATA_ERRORS = 0x01700000, DMA_ERROR = 0x10000000;
constexpr uint32_t CMD_OWNER = CC | CMD_ERRORS;
constexpr uint32_t DATA_OWNER = TC | DINT | DATA_ERRORS | DMA_ERROR;
constexpr uint32_t TERMINAL = TC | DATA_ERRORS | DMA_ERROR;

static void sdIrs();
static bool inIrq = false, irqAtLaunch = false;
static int irqAfterRead = 0, statusReads = 0;
static bool irqBeforeAck = false, irqAfterAck = false;
static uint32_t hardwareBeforeAck = 0, hardwareIrqBeforeAck = 0, completion = 0;
static volatile uint32_t m_irqstat = 0, m_cmdstat = 0;
static volatile bool m_dmaBusy = true;
static uint32_t SDHC_IRQSIGEN = 0, SDHC_MIX_CTRL = 0, SDHC_CMDARG = 0;
static uint32_t SDHC_PROCTL = 0, SDHC_BLKATTR = 0;
static bool m_highCapacity = true;
static void runIrq() { if (inIrq) return; inIrq = true; sdIrs(); inIrq = false; }

struct W1C {
  uint32_t value = 0;
  operator uint32_t() {
    const uint32_t snapshot = value;
    if (!inIrq && ++statusReads == irqAfterRead) runIrq();
    return snapshot;
  }
  void operator=(uint32_t mask) {
    if (inIrq) {
      value |= hardwareIrqBeforeAck; hardwareIrqBeforeAck = 0;
    } else {
      value |= hardwareBeforeAck; hardwareBeforeAck = 0;
      if (irqBeforeAck) { irqBeforeAck = false; runIrq(); }
    }
    value &= ~mask;  // a write of one clears only those hardware flags
    if (!inIrq && irqAfterAck) { irqAfterAck = false; runIrq(); }
  }
} SDHC_IRQSTAT;
struct CommandLaunch {
  void operator=(uint32_t) {
    SDHC_IRQSTAT.value |= completion;
    if (irqAtLaunch) { irqAtLaunch = false; runIrq(); }
  }
} SDHC_XFERTYP;

#define DBG_IRQSTAT() do {} while (false)
#define SDHC_IRQSTAT_CC CC
#define SDHC_IRQSTAT_TC TC
#define SDHC_IRQSTAT_DINT DINT
#define SDHC_IRQSTAT_DMAE DMA_ERROR
#define SDHC_MIX_CTRL_AC23EN 0x80u
#define SDHC_MIX_CTRL_DMAEN 1u
#define SDHC_XFERTYP_DPSEL 0x00200000u
#define SDHC_PROCTL_SABGREQ 0x00010000u
#define SDHC_BLKATTR_BLKCNT(n) (uint32_t(n)<<16)
#define SDHC_BLKATTR_BLKSIZE(n) uint32_t(n)
#define sdError(code) false
constexpr uint32_t SDHC_MIX_CTRL_MASK = 0xFF;
constexpr uint32_t MAX_BLKCNT = 0xFFFF;
constexpr uint32_t CMD18_PGM_XFERTYP = SDHC_XFERTYP_DPSEL | (18u<<24);
constexpr uint32_t CMD25_PGM_XFERTYP = SDHC_XFERTYP_DPSEL | (25u<<24);
static bool isBusyCommandInhibit() { return false; }
static bool isBusyDMA() { return m_dmaBusy; }
static bool isBusyCMD13() { return false; }
static bool waitTimeout(bool (*busy)()) {
  for (int i=0;i<4;++i) if (!busy()) return false;
  return true;
}
static bool yieldTimeout(bool (*busy)()) { return waitTimeout(busy); }
struct SdioCard {
  uint32_t errorData() const;
  bool readStart(uint32_t sector);
  bool writeStart(uint32_t sector);
};

#ifdef SDIO_DRIVER_EXTRACT
#include SDIO_DRIVER_EXTRACT
#else
constexpr uint32_t SDHC_IRQSTAT_CMD_ERROR = CMD_ERRORS;
constexpr uint32_t SDHC_IRQSTAT_DATA_ERROR = DATA_ERRORS;
constexpr uint32_t SDHC_IRQSTAT_ERROR = CMD_ERRORS | DATA_ERRORS | DMA_ERROR;
constexpr uint32_t SDHC_IRQSTAT_CMD_MASK = CMD_OWNER;
constexpr uint32_t SDHC_IRQSTAT_DATA_MASK = DATA_OWNER;
constexpr uint32_t SDHC_IRQSIGEN_MASK = TERMINAL;
static void clearDataStatus() {
  SDHC_IRQSIGEN = 0;
  SDHC_IRQSTAT = DATA_OWNER;
  m_irqstat = 0;
}
static void enableDmaIrs() { clearDataStatus(); m_dmaBusy = true; }
static void sdIrs() {
  const uint32_t status = SDHC_IRQSTAT & SDHC_IRQSTAT_DATA_MASK;
  if (!status) return;
  m_irqstat |= status;
  SDHC_IRQSTAT = status;
  if (!(status & TERMINAL)) return;
  SDHC_IRQSIGEN = 0;
  SDHC_MIX_CTRL &= ~(SDHC_MIX_CTRL_AC23EN | SDHC_MIX_CTRL_DMAEN);
  m_dmaBusy = false;
}
static bool isBusyCommandComplete() { return !(SDHC_IRQSTAT & CMD_OWNER); }
static bool isBusyTransferComplete() { return !(SDHC_IRQSTAT & TERMINAL); }
static bool cardCommand(uint32_t type, uint32_t arg) {
  m_cmdstat = 0;
  if (waitTimeout(isBusyCommandInhibit)) return false;
  SDHC_CMDARG = arg;
  SDHC_XFERTYP = type;
  if (waitTimeout(isBusyCommandComplete)) return false;
  const uint32_t status = SDHC_IRQSTAT & CMD_OWNER;
  m_cmdstat = status;
  SDHC_IRQSTAT = status;
  return (status & CC) && !(status & CMD_ERRORS);
}
static bool waitDmaStatus() {
  if (yieldTimeout(isBusyDMA)) return false;
  return (m_irqstat & TC) && !(m_irqstat & SDHC_IRQSTAT_ERROR);
}
uint32_t SdioCard::errorData() const { return m_irqstat | m_cmdstat; }
bool SdioCard::readStart(uint32_t sector) {
  if (yieldTimeout(isBusyCMD13)) return false;
  clearDataStatus();
  return cardCommand(CMD18_PGM_XFERTYP,m_highCapacity?sector:512*sector);
}
bool SdioCard::writeStart(uint32_t sector) {
  if (yieldTimeout(isBusyCMD13)) return false;
  clearDataStatus();
  return cardCommand(CMD25_PGM_XFERTYP,m_highCapacity?sector:512*sector);
}
#endif

void setUp() {
  inIrq = irqAtLaunch = irqBeforeAck = irqAfterAck = false;
  irqAfterRead = statusReads = 0; hardwareBeforeAck = hardwareIrqBeforeAck = 0;
  SDHC_IRQSTAT.value = 0; SDHC_IRQSIGEN = SDHC_IRQSIGEN_MASK;
  m_irqstat = m_cmdstat = 0; m_dmaBusy = true;
  completion = CC | TC | DINT;
  SDHC_PROCTL = SDHC_BLKATTR = 0; m_highCapacity = true;
}
void tearDown() {}

void test_irq_before_command_poll_preserves_command_completion() {
  irqAtLaunch = true;
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_TRUE(waitDmaStatus());
  TEST_ASSERT_EQUAL_HEX32(CC,m_cmdstat);
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,m_irqstat);
}
void test_command_poll_before_irq_preserves_dma_completion() {
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,SDHC_IRQSTAT.value);
  runIrq();
  TEST_ASSERT_TRUE(waitDmaStatus());
}
void test_irq_at_every_command_read_and_ack_boundary_preserves_both_owners() {
  for (int point=1;point<=4;++point) {
    setUp();
    if(point<=2)irqAfterRead=point;
    if(point==3)irqBeforeAck=true;
    if(point==4)irqAfterAck=true;
    TEST_ASSERT_TRUE_MESSAGE(cardCommand(0,0),"command owner lost completion");
    runIrq();
    TEST_ASSERT_TRUE_MESSAGE(waitDmaStatus(),"data owner lost completion");
    TEST_ASSERT_EQUAL_HEX32(CC,m_cmdstat);
    TEST_ASSERT_EQUAL_HEX32(TC|DINT,m_irqstat);
  }
}
void test_command_crc_error_remains_visible_without_data_irq_storm() {
  completion = 1u<<17;
  TEST_ASSERT_EQUAL_HEX32(0,SDHC_IRQSIGEN_MASK & CMD_OWNER);
  TEST_ASSERT_FALSE(cardCommand(0,0));
  TEST_ASSERT_EQUAL_HEX32(1u<<17,m_cmdstat);
  TEST_ASSERT_EQUAL_HEX32(1u<<17,SdioCard().errorData());
}
void test_spurious_irq_does_not_claim_command_bits_or_end_dma() {
  SDHC_IRQSTAT.value = CC;
  runIrq();
  TEST_ASSERT_EQUAL_HEX32(CC,SDHC_IRQSTAT.value);
  TEST_ASSERT_TRUE(m_dmaBusy);
  TEST_ASSERT_EQUAL_HEX32(0,m_irqstat);
}
void test_dint_alone_is_not_terminal_and_is_accumulated_until_tc() {
  SDHC_IRQSTAT.value = DINT;
  runIrq();
  TEST_ASSERT_TRUE(m_dmaBusy);
  TEST_ASSERT_EQUAL_HEX32(DINT,m_irqstat);
  TEST_ASSERT_EQUAL_HEX32(SDHC_IRQSIGEN_MASK,SDHC_IRQSIGEN);
  SDHC_IRQSTAT.value |= TC;
  runIrq();
  TEST_ASSERT_FALSE(m_dmaBusy);
  TEST_ASSERT_TRUE(waitDmaStatus());
  TEST_ASSERT_EQUAL_HEX32(DINT|TC,m_irqstat);
}
void test_data_errors_are_not_cleared_by_command_poll_or_reported_as_success() {
  const uint32_t errors[] = {1u<<20,1u<<21,1u<<22,1u<<24,DMA_ERROR};
  for (uint32_t error:errors) {
    setUp(); completion = CC|error;
    TEST_ASSERT_TRUE(cardCommand(0,0));
    TEST_ASSERT_EQUAL_HEX32(error,SDHC_IRQSTAT.value);
    runIrq();
    TEST_ASSERT_FALSE(waitDmaStatus());
    TEST_ASSERT_FALSE(m_dmaBusy);
    TEST_ASSERT_EQUAL_HEX32(error,m_irqstat);
  }
}
void test_late_data_flags_between_command_read_and_w1c_are_not_lost() {
  completion = CC; hardwareBeforeAck = TC|DINT;
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,SDHC_IRQSTAT.value);
  runIrq(); TEST_ASSERT_TRUE(waitDmaStatus());
}
void test_late_command_flags_between_irq_read_and_w1c_are_not_lost() {
  SDHC_IRQSTAT.value = TC|DINT;
  hardwareIrqBeforeAck = CC;
  runIrq();
  TEST_ASSERT_EQUAL_HEX32(CC,SDHC_IRQSTAT.value);
  TEST_ASSERT_TRUE(waitDmaStatus());
}
void test_fifo_transfer_flags_survive_command_completion() {
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_FALSE(isBusyTransferComplete());
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,SDHC_IRQSTAT.value);
}
void test_dma_completion_remains_latched_through_later_status_commands() {
  irqAtLaunch=true;
  TEST_ASSERT_TRUE(cardCommand(0,0));
  completion=CC;
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_TRUE(waitDmaStatus());
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,m_irqstat);
}
void test_dma_arm_discards_old_abort_status_without_claiming_command_flags() {
  SDHC_IRQSTAT.value = CC|TC|DINT|DATA_ERRORS;
  m_irqstat = TC|DINT; m_dmaBusy = false;
  enableDmaIrs();
  TEST_ASSERT_EQUAL_HEX32(CC,SDHC_IRQSTAT.value);
  TEST_ASSERT_EQUAL_HEX32(0,m_irqstat);
  TEST_ASSERT_TRUE(m_dmaBusy);
  TEST_ASSERT_EQUAL_HEX32(0,SDHC_IRQSIGEN);
  // A stale NVIC pending bit sees no data status and cannot complete this new DMA.
  runIrq(); TEST_ASSERT_TRUE(m_dmaBusy);
}
void test_fifo_read_start_discards_the_previous_abort_tc_before_a_new_transfer() {
  SDHC_IRQSTAT.value = TC|DINT;
  m_irqstat = TC|DINT;
  completion = CC;
  TEST_ASSERT_TRUE(SdioCard().readStart(10));
  TEST_ASSERT_EQUAL_HEX32(0,SDHC_IRQSTAT.value);
  TEST_ASSERT_EQUAL_HEX32(0,m_irqstat);
  TEST_ASSERT_TRUE(isBusyTransferComplete());
  SDHC_IRQSTAT.value |= TC;
  TEST_ASSERT_FALSE(isBusyTransferComplete());
}
void test_fifo_write_start_discards_the_previous_abort_tc_before_a_new_transfer() {
  SDHC_IRQSTAT.value = TC|DINT;
  m_irqstat = TC|DINT;
  completion = CC;
  TEST_ASSERT_TRUE(SdioCard().writeStart(10));
  TEST_ASSERT_EQUAL_HEX32(0,SDHC_IRQSTAT.value);
  TEST_ASSERT_EQUAL_HEX32(0,m_irqstat);
  TEST_ASSERT_TRUE(isBusyTransferComplete());
  SDHC_IRQSTAT.value |= TC;
  TEST_ASSERT_FALSE(isBusyTransferComplete());
}
void test_tc_after_dint_snapshot_stays_pending_for_the_next_irq() {
  SDHC_IRQSTAT.value=DINT; hardwareIrqBeforeAck=TC;
  runIrq();
  TEST_ASSERT_TRUE(m_dmaBusy);
  TEST_ASSERT_EQUAL_HEX32(TC,SDHC_IRQSTAT.value);
  runIrq();
  TEST_ASSERT_TRUE(waitDmaStatus());
  TEST_ASSERT_EQUAL_HEX32(DINT|TC,m_irqstat);
}
void test_error_after_dint_snapshot_cannot_become_success() {
  const uint32_t errors[] = {1u<<20,1u<<21,1u<<22,1u<<24,DMA_ERROR};
  for (uint32_t error:errors) {
    setUp(); SDHC_IRQSTAT.value=DINT; hardwareIrqBeforeAck=error;
    runIrq();
    TEST_ASSERT_TRUE(m_dmaBusy);
    TEST_ASSERT_EQUAL_HEX32(error,SDHC_IRQSTAT.value);
    runIrq();
    TEST_ASSERT_FALSE(m_dmaBusy);
    TEST_ASSERT_FALSE(waitDmaStatus());
    TEST_ASSERT_EQUAL_HEX32(DINT|error,m_irqstat);
  }
}
void test_command_error_and_data_completion_remain_separate() {
  completion=(1u<<17)|TC|DINT; irqAtLaunch=true;
  TEST_ASSERT_FALSE(cardCommand(0,0));
  TEST_ASSERT_EQUAL_HEX32(1u<<17,m_cmdstat);
  TEST_ASSERT_EQUAL_HEX32(TC|DINT,m_irqstat);
  // The DMA did complete; rdWrSectors must still fail because its command failed.
  TEST_ASSERT_TRUE(waitDmaStatus());
  TEST_ASSERT_EQUAL_HEX32((1u<<17)|TC|DINT,SdioCard().errorData());
}
void test_dma_timeout_does_not_claim_the_transfer_stopped() {
  completion=CC;
  TEST_ASSERT_TRUE(cardCommand(0,0));
  TEST_ASSERT_FALSE(waitDmaStatus());
  TEST_ASSERT_TRUE(m_dmaBusy);  // caller must quiesce / latch a fault before buffer reuse
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_irq_before_command_poll_preserves_command_completion);
  RUN_TEST(test_command_poll_before_irq_preserves_dma_completion);
  RUN_TEST(test_irq_at_every_command_read_and_ack_boundary_preserves_both_owners);
  RUN_TEST(test_command_crc_error_remains_visible_without_data_irq_storm);
  RUN_TEST(test_spurious_irq_does_not_claim_command_bits_or_end_dma);
  RUN_TEST(test_dint_alone_is_not_terminal_and_is_accumulated_until_tc);
  RUN_TEST(test_data_errors_are_not_cleared_by_command_poll_or_reported_as_success);
  RUN_TEST(test_late_data_flags_between_command_read_and_w1c_are_not_lost);
  RUN_TEST(test_late_command_flags_between_irq_read_and_w1c_are_not_lost);
  RUN_TEST(test_fifo_transfer_flags_survive_command_completion);
  RUN_TEST(test_dma_completion_remains_latched_through_later_status_commands);
  RUN_TEST(test_dma_arm_discards_old_abort_status_without_claiming_command_flags);
  RUN_TEST(test_fifo_read_start_discards_the_previous_abort_tc_before_a_new_transfer);
  RUN_TEST(test_fifo_write_start_discards_the_previous_abort_tc_before_a_new_transfer);
  RUN_TEST(test_tc_after_dint_snapshot_stays_pending_for_the_next_irq);
  RUN_TEST(test_error_after_dint_snapshot_cannot_become_success);
  RUN_TEST(test_command_error_and_data_completion_remain_separate);
  RUN_TEST(test_dma_timeout_does_not_claim_the_transfer_stopped);
  return UNITY_END();
}
