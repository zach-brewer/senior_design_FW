#include "cmd_ctrl.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_fsm.h"
#include "capture.h"
#include "dac.h"
#include "main.h"
#include "result.h"
#include "stm32g474xx.h"
#include "stm32g4xx_hal_cortex.h"
#include "stm32g4xx_hal_def.h"
#include "stm32g4xx_hal_uart.h"
#include "stm32g4xx_nucleo.h"

#if COM_TERMINAL
#define TERMINAL_RX_RING_SIZE 128U /* Must be a power of two. */
#define TERMINAL_LINE_SIZE 96U
#define MAX_BYTES 32
#define TERMINAL_RX_RING_MASK (TERMINAL_RX_RING_SIZE - 1U)

static uint8_t rx_byte;
static uint8_t rx_ring[TERMINAL_RX_RING_SIZE];
static volatile uint16_t rx_head = 0U;
static volatile uint16_t rx_tail = 0U;
static volatile uint8_t rx_overflow_flag = 0U;
static volatile uint8_t rx_restart_pending = 0U;
static char line_buffer[TERMINAL_LINE_SIZE];
static uint16_t line_length = 0U;
static uint8_t discard_line = 0U;
static uint8_t previous_was_cr = 0U;

static Result_t parse_uint32_decimal(const char *text, uint32_t max,
                                     uint32_t *value) {
  uint32_t parsed = 0U;

  if (text == NULL || value == NULL || *text == '\0')
    return R_InvalidArgument;

  for (const char *p = text; *p != '\0'; ++p) {
    /* Accept decimal digits only. Reject signs and other characters. */
    if (*p < '0' || *p > '9')
      return R_ValueError;

    uint32_t digit = (uint32_t)(*p - '0');

    /* Check the limit before multiplication/addition to avoid overflow. */
    if (parsed > max / 10U || (parsed == max / 10U && digit > max % 10U)) {
      return R_ValueError;
    }

    parsed = parsed * 10U + digit;
  }

  *value = parsed;
  return R_Ok;
}

static Result_t parse_capture_mask(const char *text, uint8_t *mask) {
  if (text == NULL || mask == NULL || *text == '\0')
    return R_InvalidArgument;

  uint32_t base = 10U;
  const char *p = text;

  /* Accept hexadecimal masks with a 0x or 0X prefix. */
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    base = 16U;
    p += 2;

    if (*p == '\0')
      return R_InvalidArgument;
  }

  uint32_t parsed = 0U;

  for (; *p != '\0'; ++p) {
    uint32_t digit;

    if (*p >= '0' && *p <= '9') {
      digit = (uint32_t)(*p - '0');
    } else if (base == 16U && *p >= 'a' && *p <= 'f') {
      digit = (uint32_t)(*p - 'a') + 10U;
    } else if (base == 16U && *p >= 'A' && *p <= 'F') {
      digit = (uint32_t)(*p - 'A') + 10U;
    } else {
      return R_ValueError;
    }

    if (digit >= base || digit > CAPTURE_MASK)
      return R_ValueError;

    /* Reject values above 0x0F before multiplication. */
    if (parsed > ((uint32_t)CAPTURE_MASK - digit) / base)
      return R_ValueError;

    parsed = parsed * base + digit;
  }

  /* An empty selection is not a valid acquisition request. */
  if (parsed == 0U)
    return R_InvalidArgument;

  *mask = (uint8_t)parsed;
  return R_Ok;
}

static uint8_t equals_cmd(const char *lhs, const char *rhs) {
  while (*lhs != '\0' && *rhs != '\0') {
    if ((unsigned char)*lhs != (unsigned char)*rhs)
      return 0;
    lhs++;
    rhs++;
  }
  return (*lhs == '\0' && *rhs == '\0');
}

static Result_t buffer_append_hex(uint8_t *buffer, size_t capacity,
                                  size_t *length, uint32_t value,
                                  size_t width_bytes) {
  static const char hex_digits[] = "0123456789ABCDEF";
  if (buffer == NULL || length == NULL || width_bytes == 0U ||
      width_bytes > sizeof(value) || *length > capacity) {
    return R_InvalidArgument;
  }
  size_t num_digits = width_bytes * 2U;
  if (num_digits > capacity - *length)
    return R_ValueError;
  for (size_t i = num_digits; i > 0; i--) {
    size_t shift = (i - 1) * 4;
    uint8_t b = (uint8_t)((value >> shift) & 0x0F);
    buffer[*length] = (uint8_t)hex_digits[b];
    (*length)++;
  }
  return R_Ok;
}

static Result_t buffer_append_char(uint8_t *buffer, size_t capacity,
                                   size_t *length, char c) {
  if (buffer == NULL || length == NULL || *length >= capacity)
    return R_InvalidArgument;
  buffer[*length] = (uint8_t)c;
  (*length)++;
  return R_Ok;
}

static void reply_err(Result_t res) { printf("ERR %02X\r\n", (uint8_t)res); }

/**
 * @brief builds the reply for status command
 * 0x## - capture error
 * 0x## - last fsm error
 * 0x## - state
 * 0x## - flags
 * timestamps
 * @param send_buff
 * @param capacity
 * @param send_len
 * @return Result_t
 */
static Result_t build_status_reply(uint8_t *send_buff, size_t capacity,
                                   size_t *send_len) {
  if (send_buff == NULL || send_len == NULL)
    return R_InvalidArgument;
  *send_len = 0;
  CaptureStatus_t capture = {0};
  Result_t res = Capture_GetStatus(&capture);
  if (res != R_Ok)
    return res;
  FSMState_t state = AppFSM_GetState();
  uint8_t flags =
      (uint8_t)((capture.captured & 0x0Fu) | (capture.capture_armed << 4) |
                (capture.capture_complete << 5));
  uint8_t capture_error = (uint8_t)capture.error;
  uint8_t fsm_error = (uint8_t)AppFSM_GetLastError();

  res = buffer_append_hex(send_buff, capacity, send_len, capture_error,
                          sizeof(capture_error));
  if (res != R_Ok)
    return res;
  res = buffer_append_char(send_buff, capacity, send_len, ' ');
  if (res != R_Ok)
    return res;

  res = buffer_append_hex(send_buff, capacity, send_len, fsm_error,
                          sizeof(fsm_error));
  if (res != R_Ok)
    return res;
  res = buffer_append_char(send_buff, capacity, send_len, ' ');
  if (res != R_Ok)
    return res;
  res = buffer_append_hex(send_buff, capacity, send_len, (uint32_t)state,
                          sizeof(uint8_t));
  if (res != R_Ok)
    return res;
  res = buffer_append_char(send_buff, capacity, send_len, ' ');
  if (res != R_Ok)
    return res;

  res = buffer_append_hex(send_buff, capacity, send_len, flags, sizeof(flags));
  if (res != R_Ok)
    return res;
  res = buffer_append_char(send_buff, capacity, send_len, ' ');
  if (res != R_Ok)
    return res;

  res = buffer_append_hex(send_buff, capacity, send_len, capture.active_mask,
                          sizeof(capture.active_mask));

  if (res != R_Ok)
    return res;

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    uint32_t timestamp = capture.timestamps[i];
    res = buffer_append_char(send_buff, capacity, send_len, ' ');
    if (res != R_Ok)
      return res;
    res = buffer_append_hex(send_buff, capacity, send_len, timestamp,
                            sizeof(timestamp));
    if (res != R_Ok)
      return res;
  }
  return R_Ok;
}

static Result_t build_timestamps(uint8_t *send_buff, size_t capacity,
                                 size_t *send_len) {
  if (send_buff == NULL || send_len == NULL)
    return R_InvalidArgument;
  *send_len = 0;
  CaptureStatus_t capture = {0};
  Result_t res = Capture_GetStatus(&capture);
  if (res != R_Ok)
    return res;
  if (capture.capture_complete == 0U || capture.active_mask == 0U ||
      (capture.captured & capture.active_mask) != capture.active_mask) {
    return R_InvalidState;
  }

  for (uint8_t i = 0U; i < NUM_CHANNELS; ++i) {
    if ((capture.active_mask & (1U << i)) == 0U)
      continue;

    if (*send_len > 0U) {
      res = buffer_append_char(send_buff, capacity, send_len, ' ');

      if (res != R_Ok)
        return res;
    }

    res =
        buffer_append_hex(send_buff, capacity, send_len, capture.timestamps[i],
                          sizeof(capture.timestamps[i]));

    if (res != R_Ok)
      return res;
  }
  return R_Ok;
}

static void reply_status(void) {
  uint8_t send_buff[64];
  size_t send_len = 0;
  Result_t res = build_status_reply(send_buff, sizeof(send_buff), &send_len);
  if (res != R_Ok) {
    reply_err(res);
    return;
  }
#if COM_TERMINAL
  printf("%.*s\r\n", (int)send_len, (char *)send_buff);
#endif
}

static void reply_times(void) {
  uint8_t send_buff[40];
  size_t send_len = 0;
  Result_t res = build_timestamps(send_buff, sizeof(send_buff), &send_len);
  if (res != R_Ok) {
    reply_err(res);
    return;
  }
#if COM_TERMINAL
  printf("%.*s\r\n", (int)send_len, (char *)send_buff);
#endif
}
/**
static void reply_ok(void)
{
    uint8_t send_buff[8];
    size_t send_len = 0;
    Result_t res = buffer_append_char(send_buff, sizeof(send_buff), &send_len,
'O'); if(res != R_Ok){reply_err(res); return;} res =
buffer_append_char(send_buff, sizeof(send_buff), &send_len, 'K'); if(res !=
R_Ok){reply_err(res); return;} res = buffer_append_char(send_buff,
sizeof(send_buff), &send_len, '\0'); if(res != R_Ok){reply_err(res); return;}
    #if COM_TERMINAL
    printf("%.*s\r\n", (int)send_len, (char *)send_buff);
    #endif
}
*/
static void reply_ok(void) { printf("OK\r\n"); }
static char *next_token(char **cursor) {
  char *p = *cursor;
  /* Skip leading separators. */
  while (*p == ' ' || *p == '\t')
    ++p;

  if (*p == '\0') {
    *cursor = p;
    return NULL;
  }

  char *token = p;

  /* Find the end of this token. */
  while (*p != '\0' && *p != ' ' && *p != '\t')
    ++p;

  if (*p != '\0') {
    *p = '\0';
    ++p;
  }

  *cursor = p;
  return token;
}

static Result_t parse_command_line(char *line, scpiCommand_t *command,
                                   uint32_t *argument) {
  if (line == NULL || command == NULL || argument == NULL)
    return R_InvalidArgument;

  *command = CMD_NONE;
  *argument = 0U;

  char *cursor = line;
  char *first = next_token(&cursor);

  if (first == NULL)
    return R_InvalidArgument;

  char *second = next_token(&cursor);

  if (equals_cmd(first, "status")) {
    if (second != NULL)
      return R_InvalidArgument;

    *command = CMD_STATUS;
    return R_Ok;
  }

  if (equals_cmd(first, "arm")) {
    uint8_t mask;

    /* No argument means arm all channels. */
    if (second == NULL || equals_cmd(second, "all")) {
      mask = CAPTURE_MASK;
    } else {
      Result_t res = parse_capture_mask(second, &mask);

      if (res != R_Ok)
        return res;
    }

    /* Reject additional arguments. */
    if (next_token(&cursor) != NULL)
      return R_InvalidArgument;

    *argument = (uint32_t)mask;
    *command = CMD_ARM_SYSTEM;

    return R_Ok;
  }

  if (equals_cmd(first, "times")) {
    if (second != NULL)
      return R_InvalidArgument;

    *command = CMD_SEND_TIME;
    return R_Ok;
  }

  if (equals_cmd(first, "reset")) {
    if (second != NULL)
      return R_InvalidArgument;

    *command = CMD_RECOVER;
    return R_Ok;
  }

  if (equals_cmd(first, "reboot")) {
    if (second != NULL)
      return R_InvalidArgument;

    *command = CMD_RESET;
    return R_Ok;
  }

  if (equals_cmd(first, "dac?")) {
    if (second != NULL)
      return R_InvalidArgument;

    *command = CMD_QUERY_DAC;
    return R_Ok;
  }

  if (equals_cmd(first, "dac")) {
    if (second == NULL)
      return R_InvalidArgument;

    if (!equals_cmd(second, "set"))
      return R_InvalidCMD;

    char *value_text = next_token(&cursor);

    if (value_text == NULL)
      return R_InvalidArgument;

    /* Reject extra arguments. */
    if (next_token(&cursor) != NULL)
      return R_InvalidArgument;

    Result_t res = parse_uint32_decimal(value_text, DAC_REFERENCE_MV, argument);

    if (res != R_Ok)
      return res;

    *command = CMD_SET_DAC;
    return R_Ok;
  }

  return R_InvalidCMD;
}

static void process_line(void) {
  char *start = line_buffer;
  scpiCommand_t command = CMD_NONE;
  uint32_t argument = 0U;

  Result_t res = parse_command_line(start, &command, &argument);

  if (res != R_Ok) {
    reply_err(res);
    line_length = 0U;
    return;
  }

  res = AppFSM_ProcessCommand(command, argument);

  if (res != R_Ok) {
    reply_err(res);
    line_length = 0U;
    return;
  }
  switch (command) {
  case CMD_STATUS:
    reply_status();
    break;
  case CMD_ARM_SYSTEM:
    printf("OK ARMED MASK=0x%02X\r\n", (unsigned int)(argument & CAPTURE_MASK));
    break;
  case CMD_SEND_TIME:
    reply_times();
    break;
  case CMD_RECOVER:
    reply_ok();
    break;
  case CMD_QUERY_DAC:
    printf("DAC=%lu mV CODE=0x%03lX\r\n", (unsigned long)DAC_GetThreshold_mV(),
           (unsigned long)DAC_GetValue());
    break;
  case CMD_SET_DAC:
    reply_ok();
    break;
  case CMD_RESET:
    // probably wont actually get to the
    reply_ok();
    break;
  default:
    reply_err((Result_t)R_InvalidCMD);
    break;
  }
  line_length = 0U;
}

Result_t Terminal_Init() {
  rx_head = 0U;
  rx_tail = 0U;
  rx_overflow_flag = 0U;
  rx_restart_pending = 0U;
  line_length = 0U;
  discard_line = 0U;
  previous_was_cr = 0;
  HAL_NVIC_SetPriority(LPUART1_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(LPUART1_IRQn);

  if (HAL_UART_Receive_IT(&hcom_uart[COM1], &rx_byte, 1U) != HAL_OK)
    return R_TerminalError;
  printf("\r\nSTM32 four-input capture firmware\r\n");
  printf("COM1 ready at 115200 8-N-1");
  return R_Ok;
}

void Terminal_Poll(void) {
  if (rx_restart_pending != 0U) {
    if (HAL_UART_Receive_IT(&hcom_uart[COM1], &rx_byte, 1U) == HAL_OK) {
      rx_restart_pending = 0U;
    }
  }

  if (rx_overflow_flag != 0U) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    rx_overflow_flag = 0U;
    if (primask == 0U)
      __enable_irq();
    line_length = 0U;
    discard_line = 1U;
  }
  while (rx_tail != rx_head) {
    uint8_t ch = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & TERMINAL_RX_RING_MASK);

    if (ch == '\r' || ch == '\n') {
      if (ch == '\n' && previous_was_cr != 0U) {
        previous_was_cr = 0U;
        continue;
      }

      previous_was_cr = (uint8_t)(ch == '\r');
      printf("\r\n");

      if (discard_line != 0U) {
        /* Discard everything through this line terminator. */
        discard_line = 0U;
        line_length = 0U;
        line_buffer[0] = '\0';

        reply_err(R_ValueError);
      } else if (line_length > 0U) {
        /* A C string must be terminated before parsing. */
        line_buffer[line_length] = '\0';
        process_line();
      } else {
        /* Ignore an empty line rather than reporting an error. */
        line_buffer[0] = '\0';
      }

      printf("> ");
      continue;
    }
    previous_was_cr = 0U;

    if (ch == 0x08U || ch == 0x7FU) {
      if (discard_line == 0U && line_length > 0U) {
        --line_length;
        printf("\b \b");
      }
      continue;
    }

    if (ch >= 32U && ch <= 126U) {
      if (discard_line == 0U && line_length < (TERMINAL_LINE_SIZE - 1U)) {
        line_buffer[line_length++] = (char)ch;
        printf("%c", (char)ch);
      } else {
        discard_line = 1U;
      }
    }
  }
}

/* The BSP owns and initialized hcom_uart[COM1]. We use that exact handle rather
 * than initializing a second handle for LPUART1. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart != &hcom_uart[COM1]) {
    return;
  }

  uint16_t next = (uint16_t)((rx_head + 1U) & TERMINAL_RX_RING_MASK);
  if (next == rx_tail) {
    rx_overflow_flag = 1U;
  } else {
    rx_ring[rx_head] = rx_byte;
    rx_head = next;
  }

  if (HAL_UART_Receive_IT(huart, &rx_byte, 1U) != HAL_OK) {
    rx_restart_pending = 1U;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart == &hcom_uart[COM1]) {
    rx_restart_pending = 1U;
  }
}
#endif