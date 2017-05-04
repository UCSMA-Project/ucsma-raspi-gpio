#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#define PIN1 17
#define PIN2 27
#define PIN3 22

struct gpio txinfo_gpios[] = {
  {PIN1, GPIOF_IN, "TX INFO 1"},
  {PIN2, GPIOF_IN, "TX INFO 2"},
  {PIN3, GPIOF_IN, "TX INFO 3"},
};

/* FD that we use to keep track of incoming TX info signals */
short int txinfo_irqs[3] = {0, 0, 0};

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
  unsigned char rising;

  spin_lock_irqsave(&driver_lock, flags);

  dev = (struct gpio *) dev_id;
  rising = gpio_get_value(dev->gpio);

  if (rising) {
    //rising edge
    printk(KERN_INFO "[TX]rising edge from %d\n", dev->gpio);
  }
  else {
    //falling edge
    printk(KERN_INFO "[TX]falling edge from %d\n", dev->gpio);
  }

  spin_unlock_irqrestore(&driver_lock, flags);

  return IRQ_HANDLED;
}

/* Entry point of driver */
static int __init txinfo_init(void)
{
  int ret;

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
  /* Initialize interrupt on UNLOCK_IN GPIO to call txinfo_r_irq_handler */
  else if ((ret = request_any_context_irq(txinfo_irqs[0],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                "GPIO IRQ 1",
                (void *) &(txinfo_gpios[0])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 1\n");
    goto fail_req_irq1;
  }
  else if ((ret = request_any_context_irq(txinfo_irqs[1],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                "GPIO IRQ 2",
                (void *) &(txinfo_gpios[1])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 2\n");
    goto fail_req_irq2;
  }
  else if ((ret = request_any_context_irq(txinfo_irqs[2],
                (irq_handler_t) txinfo_r_irq_handler,
                IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                "GPIO IRQ 3",
                (void *) &(txinfo_gpios[2])))) {
    printk(KERN_ERR "[TX timeline] unable to get GPIO IRQ 3\n");
    goto fail_req_irq3;
  }

  return 0;

fail_req_irq3:
  free_irq(txinfo_irqs[1], (void *) &(txinfo_gpios[1]));
fail_req_irq2:
  free_irq(txinfo_irqs[0], (void *) &(txinfo_gpios[0]));
fail_req_irq1:
  gpio_free_array(txinfo_gpios, ARRAY_SIZE(txinfo_gpios));
fail_request_gpio:
  return ret;
}

/* Exit Point of driver */
static void __exit txinfo_exit(void)
{
  free_irq(txinfo_irqs[0], (void *) &(txinfo_gpios[0]));
  free_irq(txinfo_irqs[1], (void *) &(txinfo_gpios[1]));
  free_irq(txinfo_irqs[2], (void *) &(txinfo_gpios[2]));

  gpio_free_array(txinfo_gpios, ARRAY_SIZE(txinfo_gpios));

  printk(KERN_INFO "[TX timeline] Uninit\n");
  return;
}

module_init(txinfo_init);
module_exit(txinfo_exit);
MODULE_LICENSE("GPL");
