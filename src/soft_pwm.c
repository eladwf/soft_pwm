/* Copyright (C) 2022 Elad Yifee
 *
 * May be copied or modified under the terms of the GNU General Public
 * License. See linux/COPYING for more information.
 *
 * Generic software-only driver for generating PWM signals via high
 * resolution timers and GPIO lib interface.
*/



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gpio.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Elad Yifee");
MODULE_DESCRIPTION("Driver for kernel-generated PWM signals");

#define FLAG_SOFTPWM 0


struct pwm_desc {
	unsigned int duty_cycle;	
	unsigned int period;	
	int value;		
  struct hrtimer hr_timer;
	unsigned long flags;	
  unsigned int gpio;
  unsigned int enable;
};

#define MAX_PWM_INSTANCES   (5)

static struct pwm_desc pwm_table[MAX_PWM_INSTANCES];

static int pwm_export(unsigned i);  
static int pwm_unexport(unsigned i); 


static ssize_t show_duty_cycle(struct device *dev, struct device_attribute *attr,
                         char *buf)
{
  const struct pwm_desc *desc = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n", desc->duty_cycle);
}

static ssize_t store_duty_cycle(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned long value;
  int ret = kstrtoul(buf, 0, &value);
  struct pwm_desc *desc = dev_get_drvdata(dev);
  if(ret == 0)
  {
    desc->duty_cycle = value;
    return count;
  }
	else return -EIO;
}

static ssize_t show_enable(struct device *dev, struct device_attribute *attr,
                         char *buf)
{
  const struct pwm_desc *desc = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n", desc->enable);
}

static ssize_t store_enable(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned long value;
  int ret = kstrtoul(buf, 0, &value);
  struct pwm_desc *desc = dev_get_drvdata(dev);
  if(ret == 0)
  {
    if(value && desc->period && (desc->duty_cycle < desc->period))
    {
      desc->enable = value;
      hrtimer_start(&desc->hr_timer, ktime_set(0,1), HRTIMER_MODE_REL);
    }
    else
    {
      desc->enable = 0;
	  __gpio_set_value(desc->gpio,0);
    }
    return count;
  }
  else return -EIO;
}

static ssize_t show_period(struct device *dev, struct device_attribute *attr,
                         char *buf)
{
    const struct pwm_desc *desc = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%s\n", desc->enable);
}

static ssize_t store_period(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
  unsigned long value;
  int ret = kstrtoul(buf, 0, &value);
  struct pwm_desc *desc = dev_get_drvdata(dev);
  if(ret == 0)
  {
    desc->period = value;
    return count;
  }
  else
	  return -EIO;
}


static DEVICE_ATTR(duty_cycle,   0644, show_duty_cycle, store_duty_cycle);
static DEVICE_ATTR(period,  0644, show_period , store_period);
static DEVICE_ATTR(enable,  0644, show_enable, store_enable);

static const struct attribute *soft_pwm_dev_attrs[] = 
{
  &dev_attr_duty_cycle.attr,
  &dev_attr_period.attr,
  &dev_attr_enable.attr,
  NULL,
};

static const struct attribute_group soft_pwm_dev_attr_group = {
  .attrs = (struct attribute **) soft_pwm_dev_attrs,
};


static ssize_t export_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len){
  unsigned long gpio;
  int  status;
  size_t i = 0;

  status = kstrtoul(buf, 0, &gpio);
  if(status<0){ goto done; }

  status = gpio_request(gpio, "soft_pwm");
  if(status<0){ goto done; }

  status = gpio_direction_output(gpio,0);
  if(status<0){ goto done; }
  

 status = -1;
  for ( i = 0; i < MAX_PWM_INSTANCES; i++)
  {
    if(!test_bit(FLAG_SOFTPWM, &pwm_table[i].flags))
      {
        pwm_table[i].gpio = gpio;
        set_bit(FLAG_SOFTPWM, &pwm_table[i].flags);
        status = 1;
        break;
      }
  }

  if(status<0){ goto done; }

  status = pwm_export(i);
  if(status<0){ goto done; }
done:
  if(status){
    gpio_free(gpio);
    pr_debug("%s: status %d\n", __func__, status);
  }
  return status ? : len;
}


static ssize_t unexport_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len){
  unsigned long gpio;
  int  status;

  status = kstrtoul(buf, 0, &gpio);
  if(status<0){ goto done; }

  status = -EINVAL;
  if(!gpio_is_valid(gpio)){ goto done; }

  for (size_t i = 0; i < MAX_PWM_INSTANCES; i++)
  {
    if(test_bit(FLAG_SOFTPWM, &pwm_table[i].flags) && pwm_table[i].gpio == gpio)
    {
      clear_bit(FLAG_SOFTPWM, &pwm_table[i].flags);
          status = pwm_unexport(i);
          if(status==0){ gpio_free(pwm_table[i].gpio); }
    }
  }
  

done:
  if(status){ pr_debug("%s: status %d\n", __func__, status); }
  return status ? : len;
}

static CLASS_ATTR_WO(export);
static CLASS_ATTR_WO(unexport);

static struct attribute *soft_pwm_class_attrs[] = {
	&class_attr_export.attr,
	&class_attr_unexport.attr,
	NULL,
 };

ATTRIBUTE_GROUPS(soft_pwm_class);



static struct class soft_pwm_class = {
  .name =        "soft_pwm",
  .owner =       THIS_MODULE,
  .class_groups = soft_pwm_class_groups,
};

int pwm_export(unsigned i)
{
  struct pwm_desc *desc;
  struct device   *dev;
  int             status;

  desc = &pwm_table[i];
  desc->value  = 0;

  dev = device_create(&soft_pwm_class, NULL, MKDEV(0, 0), desc, "pwm%d", desc->gpio);
  printk(KERN_INFO "dev kobj %s\n", dev->kobj.name);
  if(dev)
  {
    status = sysfs_create_group(&dev->kobj, &soft_pwm_dev_attr_group);
    if(status==0)
    {
    }
    else
    {
      device_unregister(dev);
    }
  }
  else
  {
    status = -ENODEV;
  }

  if(status){ pr_debug("%s: pwm%d status %d\n", __func__, desc->gpio, status); }
  return status;
}

static int match_export(struct device *dev, const void *data){
  return dev_get_drvdata(dev) == data;
}


int pwm_unexport(unsigned i){
  struct pwm_desc *desc;
  struct device   *dev;
  int             status;
      
  desc = &pwm_table[i];
  dev  = class_find_device(&soft_pwm_class, NULL, desc, match_export);

  if(dev){
    put_device(dev);
    device_unregister(dev);
    status = 0;
  }else{
    status = -ENODEV;
  }

  if(status){ pr_debug("%s: pwm%d status %d\n", __func__, desc->gpio, status); }
  return status;
}


enum hrtimer_restart soft_pwm_hrtimer_callback(struct hrtimer *timer)
{
  struct pwm_desc*  desc = container_of(timer, struct pwm_desc, hr_timer);
  ktime_t ns_next_tick;
  if(!desc->enable)
    return HRTIMER_NORESTART;
  desc->value = 1-desc->value;
  __gpio_set_value(desc->gpio,desc->value);
  ns_next_tick = (desc->value ? desc->duty_cycle : (desc->period - desc->duty_cycle))*1000;
  hrtimer_start(&desc->hr_timer, ktime_set(0, ns_next_tick), HRTIMER_MODE_REL);
  return HRTIMER_NORESTART;
}

static int __init soft_pwm_init(void){

  int status;


  for (size_t i = 0; i < MAX_PWM_INSTANCES; i++)
  {
    struct pwm_desc *desc;
    desc = &pwm_table[i];
    hrtimer_init(&desc->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    desc->hr_timer.function =  &soft_pwm_hrtimer_callback;
  }
  

  status = class_register(&soft_pwm_class);
  if(status<0){ goto fail_no_class; }

  return 0;

fail_no_class:
  printk(KERN_ERR "soft-pwm failed to initialize.\n");
  return status;
}


static void __exit soft_pwm_exit(void){
  unsigned i;
  int status;

  for(i=0;i<MAX_PWM_INSTANCES;i++){
    struct pwm_desc *desc;
    desc = &pwm_table[i];
    hrtimer_cancel(&desc->hr_timer);
    if(test_bit(FLAG_SOFTPWM,&desc->flags)){
      __gpio_set_value(desc->gpio,0);
      status = pwm_unexport(i);
      if(status==0){ gpio_free(desc->gpio); }
    }
  }
  class_unregister(&soft_pwm_class);
}

module_init(soft_pwm_init);
module_exit(soft_pwm_exit);
