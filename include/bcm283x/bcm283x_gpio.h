#ifndef __BCM283X_GPIO__
#define __BCM283X_GPIO__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/**
  @brief  Internal structures, types and constants
*/

#ifdef BCM283X_EXTENDED_GPIO
#define GPIO_PIN_COUNT     (54)
#else
#define GPIO_PIN_COUNT     (32)
#endif

#define GPIO_BASE_OFFSET (0x00200000)

typedef enum __attribute__((packed))
{
  GPIO_FUNCTION_INPUT   = 0,
  GPIO_FUNCTION_OUTPUT  = 1,
  GPIO_FUNCTION_AF0     = 4,
  GPIO_FUNCTION_AF1     = 5,
  GPIO_FUNCTION_AF2     = 6,
  GPIO_FUNCTION_AF3     = 7,
  GPIO_FUNCTION_AF4     = 3,
  GPIO_FUNCTION_AF5     = 2,
} GPIO_FUNCTION;

typedef enum __attribute__((packed))
{
  GPIO_PULL_OFF   = 0,
  GPIO_PULL_DOWN  = 1,
  GPIO_PULL_UP    = 2,
  // 3 - RESERVED
} GPIO_PULL;

typedef struct gpio_function_select_x_t
{
  uint32_t FSELx0 : 3;
  uint32_t FSELx1 : 3;
  uint32_t FSELx2 : 3;
  uint32_t FSELx3 : 3;
  uint32_t FSELx4 : 3;
  uint32_t FSELx5 : 3;
  uint32_t FSELx6 : 3;
  uint32_t FSELx7 : 3;
  uint32_t FSELx8 : 3;
  uint32_t FSELx9 : 3;
  uint32_t _reserved : 2;
} gpio_function_select_x_t;

typedef struct gpio_output_set_x_t
{
  uint32_t SET;
} gpio_output_set_x_t;

typedef struct gpio_output_clear_x_t
{
  uint32_t CLR;
} gpio_output_clear_x_t;

typedef struct gpio_level_x_t
{
  uint32_t LEV;
} gpio_level_x_t;

typedef struct gpio_event_status_x_t
{
  uint32_t EDS;
} gpio_event_status_x_t;

typedef struct gpio_rising_edge_enable_x_t
{
  uint32_t REN;
} gpio_rising_edge_enable_x_t;

typedef struct gpio_falling_edge_enable_x_t
{
  uint32_t FEN;
} gpio_falling_edge_enable_x_t;

typedef struct gpio_high_detect_enable_x_t
{
  uint32_t HEN;
} gpio_high_detect_enable_x_t;

typedef struct gpio_low_detect_enable_x_t
{
  uint32_t LEN;
} gpio_low_detect_enable_x_t;

typedef struct gpio_async_rising_edge_enable_x_t
{
  uint32_t AREN;
} gpio_async_rising_edge_enable_x_t;

typedef struct gpio_async_falling_edge_enable_x_t
{
  uint32_t AFEN;
} gpio_async_falling_edge_enable_x_t;

typedef struct gpio_pull_up_down_t
{
  uint32_t PUD : 2;
  uint32_t _reserved : 30;
} gpio_pull_up_down_t;

typedef struct gpio_pull_up_down_clock_x_t
{
  uint32_t PUDCLK;
} gpio_pull_up_down_clock_x_t;

typedef struct bcm283x_gpio_t
{
  volatile gpio_function_select_x_t           GPFSELx[6];
  volatile uint32_t _reserved;
  volatile gpio_output_set_x_t                GPSETx[2];
  volatile uint32_t _reserved2;
  volatile gpio_output_clear_x_t              GPCLRx[2];
  volatile uint32_t _reserved3;
  volatile gpio_level_x_t                     GPLEVx[2];
  volatile uint32_t _reserved4;
  volatile gpio_event_status_x_t              GPEDSx[2];
  volatile uint32_t _reserved5;
  volatile gpio_rising_edge_enable_x_t        GPRENx[2];
  volatile uint32_t _reserved6;
  volatile gpio_falling_edge_enable_x_t       GPFENx[2];
  volatile uint32_t _reserved7;
  volatile gpio_high_detect_enable_x_t        GPHENx[2];
  volatile uint32_t _reserved8;
  volatile gpio_low_detect_enable_x_t         GPLENx[2];
  volatile uint32_t _reserved9;
  volatile gpio_async_rising_edge_enable_x_t  GPARENx[2];
  volatile uint32_t _reserved10;
  volatile gpio_async_falling_edge_enable_x_t GPAFENx[2];
  volatile uint32_t _reserved11;
  volatile gpio_pull_up_down_t                GPPUD;
  volatile gpio_pull_up_down_clock_x_t        GPPUDCLKx[2];
  volatile uint32_t _reserved12[4];
  volatile uint32_t _test;
} bcm283x_gpio_t;

static_assert(sizeof(gpio_function_select_x_t) == sizeof(uint32_t), "gpio_function_select_x_t must be 4 bytes.");
static_assert(sizeof(gpio_output_set_x_t) == sizeof(uint32_t), "gpio_output_set_x_t must be 4 bytes.");
static_assert(sizeof(gpio_output_clear_x_t) == sizeof(uint32_t), "gpio_output_clear_x_t must be 4 bytes.");
static_assert(sizeof(gpio_level_x_t) == sizeof(uint32_t), "gpio_level_x_t must be 4 bytes.");
static_assert(sizeof(gpio_event_status_x_t) == sizeof(uint32_t), "gpio_event_status_x_t must be 4 bytes.");
static_assert(sizeof(gpio_rising_edge_enable_x_t) == sizeof(uint32_t), "gpio_rising_edge_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_falling_edge_enable_x_t) == sizeof(uint32_t), "gpio_falling_edge_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_high_detect_enable_x_t) == sizeof(uint32_t), "gpio_high_detect_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_low_detect_enable_x_t) == sizeof(uint32_t), "gpio_low_detect_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_async_rising_edge_enable_x_t) == sizeof(uint32_t), "gpio_async_rising_edge_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_async_falling_edge_enable_x_t) == sizeof(uint32_t), "gpio_async_falling_edge_enable_x_t must be 4 bytes.");
static_assert(sizeof(gpio_pull_up_down_t) == sizeof(uint32_t), "gpio_pull_up_down_t must be 4 bytes.");
static_assert(sizeof(gpio_pull_up_down_clock_x_t) == sizeof(uint32_t), "gpio_pull_up_down_clock_x_t must be 4 bytes.");

static_assert(sizeof(bcm283x_gpio_t) == 45 * sizeof(uint32_t), "bcm283x_gpio_t must be 180 bytes.");

/**
  @brief  External structures, types and interfaces.
*/
typedef uint32_t gpio_pin_t;

#ifdef BCM283X_EXTENDED_GPIO
typedef uint64_t gpio_pin_mask_t;
#else
typedef uint32_t gpio_pin_mask_t;
#endif

typedef enum gpio_pull_t
{
  gpio_pull_none  = GPIO_PULL_OFF,
  gpio_pull_down  = GPIO_PULL_DOWN,
  gpio_pull_up    = GPIO_PULL_UP,
  gpio_pull_no_change,
} gpio_pull_t;

typedef enum gpio_function_t
{
  gpio_function_input   = GPIO_FUNCTION_INPUT,
  gpio_function_output  = GPIO_FUNCTION_OUTPUT,
  gpio_function_af0     = GPIO_FUNCTION_AF0,
  gpio_function_af1     = GPIO_FUNCTION_AF1,
  gpio_function_af2     = GPIO_FUNCTION_AF2,
  gpio_function_af3     = GPIO_FUNCTION_AF3,
  gpio_function_af4     = GPIO_FUNCTION_AF4,
  gpio_function_af5     = GPIO_FUNCTION_AF5,
} gpio_function_t;

typedef enum gpio_event_detect_t
{
  gpio_event_detect_none,
  gpio_event_detect_rising_edge,
  gpio_event_detect_falling_edge,
  gpio_event_detect_any_edge,
  gpio_event_detect_high_level,
  gpio_event_detect_low_level,
  gpio_event_detect_rising_edge_async,
  gpio_event_detect_falling_edge_async,
  //gpio_event_detect_any_edge_async, // TODO Unknown if async edges can be combined.
} gpio_event_detect_t;

typedef struct gpio_configuration_t
{
  gpio_function_t     function;
  gpio_pull_t         pull;
  gpio_event_detect_t eventDetect;
} gpio_configuration_t;

void bcm283x_gpio_init(void* base);
void bcm283x_gpio_configure(gpio_pin_t pin, const gpio_configuration_t* config);
void bcm283x_gpio_configure_mask(gpio_pin_mask_t mask, const gpio_configuration_t* config);
void bcm283x_gpio_set(gpio_pin_t pin);
void bcm283x_gpio_set_mask(gpio_pin_mask_t mask);
void bcm283x_gpio_clear(gpio_pin_t pin);
void bcm283x_gpio_clear_mask(gpio_pin_mask_t mask);

#endif