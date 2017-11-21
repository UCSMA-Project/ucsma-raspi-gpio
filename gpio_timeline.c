#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

/* marcos for fast reading GPIO value */
#define BCM2708_PERI_BASE   0x3F000000
#define GPIO_BASE           (BCM2708_PERI_BASE + 0x200000)
#define GPLEV0				(gpio_reg + 0x34)
#define GPLEV1				(gpio_reg + 0x38)

#define GET_GPIO(g) \
		((__raw_readl(GPLEV0 + (g / 32) * 4) >> (g % 32)) & 1)

/* define the pins used to receive signal */
#define PIN1 17
#define PIN2 27
#define PIN3 22
#define UNLOCKING_PIN 18

#define MAX_LOG_COUNT 50000


/* struct and variable for logging */
struct log {
  struct timespec time;
  unsigned int gpio;
};
struct log logs[MAX_LOG_COUNT];

u32 cur_log_idx = 0;
u32 max_log_count = 0;
module_param(max_log_count, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

u32 human_readable_output = 1;
module_param(human_readable_output, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

/* variable that points to the mapped mem address*/
static void __iomem *gpio_reg;

struct gpio txinfo_gpios[] = {
  {PIN1, GPIOF_IN, "TX INFO 1"},
  {PIN2, GPIOF_IN, "TX INFO 2"},
  {PIN3, GPIOF_IN, "TX INFO 3"},
  {UNLOCKING_PIN, GPIOF_IN, "UNLOCKING INFO"},
};

/* FD that we use to keep track of incoming TX info signals */
short int txinfo_irqs[4] = {0, 0, 0, 0};

/*
 *  * Holds onto any pending interrupts while we handle the
 *   * the incoming TX info interrupt
 *    */
unsigned long flags;

/*
 *  * Spin Lock used to guarantee that a single gpio
 *   * interrupt is handled at a time
 *    */
DEFINE_SPINLOCK(driver_lock);

/* Interrupt handler called on falling edfe of UNLOCK_IN GPIO */
static irqreturn_t txinfo_r_irq_handler(int irq, void *dev_id) {
  struct gpio *dev;
  struct timespec diff;

  dev = (struct gpio *) dev_id;

  spin_lock_irqsave(&driver_lock, flags);

  if (cur_log_idx < max_log_count) {
    logs[cur_log_idx].gpio = dev->gpio;
    getnstimeofday(&(logs[cur_log_idx].time));
    cur_log_idx++;
  }

  if (cur_log_idx && cur_log_idx == max_log_count) {
    printk(KERN_INFO "\n");
    printk("[%lu.%09lu] GPIO: %2d falling\n", logs[0].time.tv_sec,
             logs[0].time.tv_nsec, logs[0].gpio);
    for (cur_log_idx = 1; cur_log_idx < max_log_count; cur_log_idx++) {
      diff = timespec_sub(logs[cur_log_idx].time, logs[cur_log_idx - 1].time);
      if (human_readable_output)
        printk("[%4lu us] GPIO: %2d falling\n",
                diff.tv_sec * 1000000 + diff.tv_nsec / 1000,
                logs[cur_log_idx].gpio);
      else
        printk("%lu:%2df\n",
                diff.tv_sec * 1000000 + diff.tv_nsec / 1000,
                logs[cur_log_idx].gpio);
    }

    printk(KERN_INFO "clearing max_log_count %d -> 0\n", max_log_count);
    max_log_count = 0;
    cur_log_idx = 0;
  }

  spin_unlock_irqrestore(&driver_lock, flags);

  return IRQ_HANDLED;
}

/* Entry point of driver */
static int __init txinfo_init(void)
{
  int ret;

  /* map mem address of GPIO registers */
  gpio_reg = ioremap(GPIO_BASE, 1024);

  printk(KERN_INFO "[TX timeline] Init\n");

  /* Get access to GPIOS for recieving TX info signals */
  if ((ret = gpio_request_array(txinfo_gpios, ARRAY_SIZE(txinfo_gpios)))) {
    printk(KERN_ERR "[TX timeline] unable to requests gpios\n");
    goto fail_request_gpio;
  }
  /* Get interrupt number for TX info GPIOs */
  else if ((ret = txinfo_irqs[0] = gpio_to_irq(txinfo_gpios[0].gpio)) < 0) {
    printk(KERN_ERR "[TX timeline] IRQ mapping failure 1\n");
    goto fail_req_irq1;
  }
  else if ((ret = txinfo_irqs[1] = gpio_to_irq(txinfo_gpios[1].gpio)) < 0) {
    printk(KERN_ERR "[TX timeline] IRQ mapping failure 2\n");
    goto fail_req_irq1;
  }
  else if ((ret = txinfo_irqs[2] = gpio_to_irq(txinfo_gpios[2].gpio)) < 0) {
    printk(KERN_ERR "[TX timeline] IRQ mapping failure 3\n");
    goto fail_req_irq1;
  }
  else if ((ret = txinfo_irqs[3] = gpio_to_irq(txinfo_gpios[3].gpio)) < 0) {
    printk(KERN_ERR "[TX timeline] IRQ mapping failure UNLOCKING PIN\n");
    goto fail_req_irq1;
  }
  /* Initialize interrupt on UNLOCK_IN GPIO to call txinfo_r_irq_handler */
  else if ((ret = request_any_context_irq(txinfo_irqs[0],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING,
                "GPIO IRQ 1",
                (void *) &(txinfo_gpios[0])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 1\n");
    goto fail_req_irq1;
  }
  else if ((ret = request_any_context_irq(txinfo_irqs[1],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING,
                "GPIO IRQ 2",
                (void *) &(txinfo_gpios[1])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 2\n");
    goto fail_req_irq2;
  }
  else if ((ret = request_any_context_irq(txinfo_irqs[2],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING,
                "GPIO IRQ 3",
                (void *) &(txinfo_gpios[2])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 3\n");
    goto fail_req_irq3;
  }
  else if ((ret = request_any_context_irq(txinfo_irqs[3],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING,
                "GPIO IRQ UNLOCKING",
                (void *) &(txinfo_gpios[3])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ UNLOCKING\n");
    goto fail_req_irq_unlocking;
  }

  return 0;

fail_req_irq_unlocking:
  free_irq(txinfo_irqs[2], (void *) &(txinfo_gpios[2]));
fail_req_irq3:
  free_irq(txinfo_irqs[1], (void *) &(txinfo_gpios[1]));
fail_req_irq2:
  free_irq(txinfo_irqs[0], (void *) &(txinfo_gpios[0]));
fail_req_irq1:
  gpio_free_array(txinfo_gpios, ARRAY_SIZE(txinfo_gpios));
fail_request_gpio:
  iounmap(gpio_reg);
  return ret;
}

/* Exit Point of driver */
static void __exit txinfo_exit(void)
{
  free_irq(txinfo_irqs[0], (void *) &(txinfo_gpios[0]));
  free_irq(txinfo_irqs[1], (void *) &(txinfo_gpios[1]));
  free_irq(txinfo_irqs[2], (void *) &(txinfo_gpios[2]));
  free_irq(txinfo_irqs[3], (void *) &(txinfo_gpios[3]));

  gpio_free_array(txinfo_gpios, ARRAY_SIZE(txinfo_gpios));
  iounmap(gpio_reg);

  printk(KERN_INFO "[TX timeline] Uninit\n");
  return;
}

module_init(txinfo_init);
module_exit(txinfo_exit);
MODULE_LICENSE("GPL");
