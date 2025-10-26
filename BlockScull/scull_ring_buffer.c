#include <linux/module.h>    // Основные определения для модулей ядра
#include <linux/init.h>      // Макросы __init и __exit
#include <linux/kernel.h>    // Функции печати в ядро (printk, pr_info)
#include <linux/fs.h>        // Файловые операции, регистрация устройств
#include <linux/cdev.h>      // Структура cdev для символьных устройств
#include <linux/slab.h>      // Функции выделения памяти в ядре (kmalloc, kfree)
#include <linux/uaccess.h>   // Функции копирования между ядром и пользователем
#include <linux/wait.h>      // Очереди ожидания для синхронизации
#include <linux/sched.h>     // Определения структур процессов
#include <linux/mutex.h>     // Мьютексы для взаимного исключения
#include <linux/device/class.h> //for class_create/class_destroy
#include <linux/device.h> // for device_create/device_destroy
#include <linux/version.h> // for kenel version
#include <linux/moduleparam.h>

#define DEVICE_NAME "scull_ring_buffer"

#define DEFAULT_BUFFER_SIZE 1024
#define DEFAULT_NUM_DEVICES 2

// Module params - can be set in insmod
static int num_devices = DEFAULT_NUM_DEVICES;
static int buffer_size = DEFAULT_BUFFER_SIZE;

// Declare module params
module_param(num_devices, int, S_IRUGO);
MODULE_PARM_DESC(num_devices, "Number of scull devices to create (default: 2)");

module_param(buffer_size, int, S_IRUGO);
MODULE_PARM_DESC(buffer_size
            , "Size of each circular buffer in bytes (default: 1024)");

// Структура устройства
struct scull_ring_buffer {
    struct cdev cdev;               // Структура символьного устройства
    dev_t devno;                    // Номер устройства (major + minor)
    char *buffer;                   // Указатель на кольцевой буфер в памяти ядра
    int read_index;             
    int write_index;            
    int data_size;                  // Текущее количество данных в буфере
    int size;                   
    struct mutex lock;              // Мьютекс для защиты от гонок данных
    wait_queue_head_t read_queue;   // Очередь ожидания для процессов чтения
    wait_queue_head_t write_queue;  // Очередь ожидания для процессов записи
};

// Динамический массив структур устройств
static struct scull_ring_buffer *devices = NULL;
static int major_num = 0;
static struct class *scull_class = NULL;

// Объявления функций файловых операций
static int scull_open(struct inode *inode, struct file *filp);
static int scull_release(struct inode *inode, struct file *filp);
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

// Структура файловых операций - связывает системные вызовы с нашими функциями
static struct file_operations scull_fops = {
    .owner = THIS_MODULE,    
    .open = scull_open,      
    .release = scull_release, 
    .read = scull_read,      
    .write = scull_write,    
    .unlocked_ioctl = scull_ioctl
};

// Функция открытия устройства
static int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_ring_buffer *dev; 
    int minor = iminor(inode); 

    // Проверяем, что minor номер в допустимом диапазоне
    if (minor >= num_devices) {
        return -ENODEV; 
    }

    // Получаем указатель на структуру устройства по minor номеру
    dev = &devices[minor];
    filp->private_data = dev;

    // Выводим информационное сообщение в журнал ядра
    printk(KERN_ALERT "scull_ring_buffer: Device %d opened\n", minor);
    return 0; // Успешное завершение
}

// Функция закрытия устройства
static int scull_release(struct inode *inode, struct file *filp)
{
    printk(KERN_ALERT "scull_ring_buffer: Device %d closed\n", iminor(inode));
    return 0; // Успешное завершение
}

// Функция чтения из устройства
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_ring_buffer *dev = filp->private_data; // Получаем наше устройство
    ssize_t retval = 0;                            // Возвращаемое значение (количество прочитанных байт)
    int bytes_to_read;                             // Сколько байт будем читать в этой операции
    int bytes_read_first_part;                     // Сколько байт прочитаем из первой части буфера

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; 

    // Ждем, пока в буфере появятся данные для чтения
    while (dev->data_size == 0) {

        mutex_unlock(&dev->lock);
        // Проверяем, открыто ли устройство в неблокирующем режиме
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN; 

        // Сообщаем, что процесс идет спать из-за пустого буфера
        printk(KERN_ALERT "scull_ring_buffer: Buffer empty, process %d (%s) going to sleep\n",
                current->pid, current->comm);

        // Усыпляем процесс в очереди чтения. Проснется когда data_size > 0
        // wait_event_interruptible проверяет условие после пробуждения
        if (wait_event_interruptible(dev->read_queue, (dev->data_size > 0)))
            return -ERESTARTSYS;

        // Проснулись, снова пытаемся захватить мьютекс
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    // Определяем, сколько байт можем прочитать (минимум из запрошенного и доступного)
    bytes_to_read = min(count, (size_t)dev->data_size);

    // Первая часть чтения - до конца буфера
    bytes_read_first_part = min(bytes_to_read, dev->size - dev->read_index);

    if (copy_to_user(buf, dev->buffer + dev->read_index, bytes_read_first_part)) {
        retval = -EFAULT; 
        goto out; 
    }

    // Вторая часть чтения
    if (bytes_to_read > bytes_read_first_part) {
        if (copy_to_user(buf + bytes_read_first_part, dev->buffer, 
                        bytes_to_read - bytes_read_first_part)) {
            retval = -EFAULT; // Ошибка копирования
            goto out; // Переходим к метке выхода
        }
    }
    
    dev->read_index = (dev->read_index + bytes_to_read) % dev->size;
    dev->data_size -= bytes_to_read;
    retval = bytes_to_read;

    // Информационное сообщение о успешном чтении
    printk(KERN_ALERT "scull_ring_buffer: Read %zu bytes from device %d. Data size: %d/%d\n",
            retval, iminor(filp->f_path.dentry->d_inode), dev->data_size, dev->size);

    // Будим все процессы, ждущие в очереди записи
    wake_up_interruptible(&dev->write_queue);

// Метка выхода из функции
out:
    mutex_unlock(&dev->lock);
    return retval; 
}

// Функция записи в устройство
static ssize_t scull_write(struct file *filp
    , const char __user *buf
    , size_t count
    , loff_t *f_pos)
{
    struct scull_ring_buffer *dev = filp->private_data; // Получаем наше устройство
    ssize_t retval = 0;          
    int space_available;         
    int bytes_to_write;          
    int bytes_write_first_part; 

    // Захватываем мьютекс
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; 

    // Вычисляем свободное место в буфере
    space_available = dev->size - dev->data_size;

    // Ждем, пока в буфере появится свободное место для записи
    while (space_available == 0) {
        mutex_unlock(&dev->lock);

        // Проверяем, открыто ли устройство в неблокирующем режиме
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN; 
        
        pr_info("scull_ring_buffer: Buffer full, process %d (%s) going to sleep\n",
                current->pid, current->comm);

        // Усыпляем процесс в очереди записи. Проснется когда появится место
        // Вычисляем space_available снова после пробуждения
        if (wait_event_interruptible(dev->write_queue, 
            (space_available = (dev->size - dev->data_size)) > 0))
            return -ERESTARTSYS; // Было прерывание

        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    // Определяем, сколько байт можем записать (минимум из запрошенного и доступного)
    bytes_to_write = min(count, (size_t)space_available);

    bytes_write_first_part = min(bytes_to_write, dev->size - dev->write_index);

    // Копируем данные из пользовательского пространства в ядро (первая часть)
    if (copy_from_user(dev->buffer + dev->write_index, buf, bytes_write_first_part)) {
        retval = -EFAULT;
        goto out;
    }

    // Копируем данные из пользовательского пространства в ядро (вторая часть, если есть)
    if (bytes_to_write > bytes_write_first_part) {
        if (copy_from_user(dev->buffer, buf + bytes_write_first_part, 
                          bytes_to_write - bytes_write_first_part)) {
            retval = -EFAULT; 
            goto out; 
        }
    }

    // Обновляем индекс записи с учетом кольцевой структуры
    dev->write_index = (dev->write_index + bytes_to_write) % dev->size;
    dev->data_size += bytes_to_write;
    retval = bytes_to_write;

    // Информационное сообщение о успешной записи
    pr_info("scull_ring_buffer: Wrote %zu bytes to device %d. Data size: %d/%d\n",
            retval, iminor(filp->f_path.dentry->d_inode), dev->data_size, dev->size);

    // После записи в буфере точно появились новые данные
    // Будим все процессы, ждущие в очереди чтения
    wake_up_interruptible(&dev->read_queue);

// Метка выхода из функции
out:
    mutex_unlock(&dev->lock);
    return retval; 
}

// Функция инициализации модуля (вызывается при загрузке)
static int __init scull_init(void)
{
    int i, err;           // Счетчик и переменная для ошибок
    dev_t dev_num = 0;    // Номер устройства

    // Проверка параметров
    if (num_devices <= 0) {
        pr_err("scull_ring_buffer: Invalid number of devices: %d\n", num_devices);
        return -EINVAL;
    }
    
    if (buffer_size <= 0) {
        pr_err("scull_ring_buffer: Invalid buffer size: %d\n", buffer_size);
        return -EINVAL;
    }

    pr_info("scull_ring_buffer: Initializing with %d devices, buffer size: %d bytes\n", 
            num_devices, buffer_size);

    // Запрашиваем динамическое выделение диапазона номеров устройств
    // dev_num будет содержать первый номер, num_devices - количество устройств
    err = alloc_chrdev_region(&dev_num, 0, num_devices, DEVICE_NAME);
    if (err < 0) {
        pr_err("scull_ring_buffer: Failed to allocate device numbers\n");
        return err; 
    }
    // Сохраняем старший номер из выделенного диапазона
    major_num = MAJOR(dev_num);

    // Выделяем память под массив устройств
    devices = kmalloc_array(num_devices, sizeof(struct scull_ring_buffer), GFP_KERNEL);
    if (!devices) {
        pr_err("scull_ring_buffer: Failed to allocate devices array\n");
        err = -ENOMEM;
        goto fail_alloc;
    }

    // Создаем класс устройств для автоматического создания узлов в /dev
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    scull_class = class_create(DEVICE_NAME);
#else
    scull_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif

    if (IS_ERR(scull_class)) {
        pr_err("scull_ring_buffer: Failed to create device class\n");
        err = PTR_ERR(scull_class);
        goto fail_class;
    }

    // Инициализируем каждое устройство в цикле
    for (i = 0; i < num_devices; i++) {
        struct scull_ring_buffer *dev = &devices[i]; 

        // Выделяем память под кольцевой буфер в пространстве ядра
        dev->buffer = kmalloc(buffer_size, GFP_KERNEL);
        if (!dev->buffer) {
            pr_err("scull_ring_buffer: Failed to allocate buffer for device %d\n", i);
            err = -ENOMEM; 
            goto fail_device; 
        }

        // Инициализируем мьютекс для синхронизации
        mutex_init(&dev->lock);
        // Инициализируем очереди ожидания для читателей и писателей
        init_waitqueue_head(&dev->read_queue);
        init_waitqueue_head(&dev->write_queue);

        // Инициализируем переменные буфера
        dev->read_index = 0;  
        dev->write_index = 0;  
        dev->data_size = 0;   
        dev->size = buffer_size;

        dev->devno = MKDEV(major_num, i);

        // Инициализируем структуру cdev и связываем с файловыми операциями
        cdev_init(&dev->cdev, &scull_fops);
        dev->cdev.owner = THIS_MODULE; // Устанавливаем владельца

        // Добавляем символьное устройство в систему
        err = cdev_add(&dev->cdev, dev->devno, 1);
        if (err) {
            pr_err("scull_ring_buffer: Error %d adding device %d\n", err, i);
            kfree(dev->buffer); 
            goto fail_device;
        }

        // Создаем устройство в /dev через sysfs
        device_create(scull_class, NULL, dev->devno, NULL, "scull_ring_buffer%d", i);

        // Сообщаем об успешном создании устройства
        pr_info("scull_ring_buffer: Device /dev/scull_ring_buffer%d created (buffer size: %d bytes)\n", 
                i, buffer_size);
    }

    pr_info("scull_ring_buffer: Module loaded successfully (major number = %d, devices = %d, buffer size = %d)\n", 
            major_num, num_devices, buffer_size);
    return 0;

// Метка обработки ошибок при создании устройств
fail_device:
    // Откат: удаляем все созданные устройства в обратном порядке
    while (--i >= 0) {
        // Удаляем устройство из /dev
        device_destroy(scull_class, devices[i].devno);
        // Удаляем символьное устройство из системы
        cdev_del(&devices[i].cdev);
        // Освобождаем память буфера
        kfree(devices[i].buffer);
    }
    // Удаляем класс устройств
    class_destroy(scull_class);

// Метка обработки ошибок при создании класса
fail_class:
    // Освобождаем массив устройств
    kfree(devices);
    devices = NULL;

// Метка обработки ошибок при выделении памяти
fail_alloc:
    // Освобождаем выделенные номера устройств
    unregister_chrdev_region(dev_num, num_devices);
    return err; // Возвращаем код ошибки
}

// Функция выгрузки модуля (вызывается при удалении)
static void __exit scull_exit(void)
{
    int i; 

    // Удаляем все устройства в цикле
    for (i = 0; i < num_devices; i++) {
        // Удаляем устройство из /dev
        device_destroy(scull_class, devices[i].devno);
        // Удаляем символьное устройство из системы
        cdev_del(&devices[i].cdev);
        // Освобождаем память буфера
        kfree(devices[i].buffer);
    }

    // Удаляем класс устройств
    class_destroy(scull_class);
    // Освобождаем массив устройств
    kfree(devices);
    devices = NULL;
    // Освобождаем номера устройств
    unregister_chrdev_region(MKDEV(major_num, 0), num_devices);

    // Сообщение о успешной выгрузке модуля
    pr_info("scull_ring_buffer: Module unloaded\n");
}

// Добавим ioctl для Process C, чтобы получать состояние буфера
static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct scull_ring_buffer *dev = filp->private_data; // Получаем наше устройство
    int retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    switch (cmd) {
    case 0: // Команда для получения размера данных в буфере
        if (copy_to_user((int __user *)arg, &dev->data_size, sizeof(dev->data_size))) {
            retval = -EFAULT;
        }
        break;
    case 1: // Команда для получения полной информации о буфере
        {
            struct buffer_info {
                int data_size;
                int buffer_size;
                int read_index;
                int write_index;
            } info;
            
            info.data_size = dev->data_size;
            info.buffer_size = dev->size;
            info.read_index = dev->read_index;
            info.write_index = dev->write_index;
            
            if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
                retval = -EFAULT;
            }
        }
        break;
    // Можно добавить другие команды, например, для чтения всего содержимого без извлечения
    default:
        retval = -ENOTTY;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

// Указываем функции инициализации и очистки
module_init(scull_init); // Функция scull_init будет вызвана при загрузке модуля
module_exit(scull_exit); // Функция scull_exit будет вызвана при выгрузке модуля

// Информация о модуле
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Mikhail Kalinin");
MODULE_DESCRIPTION("Scull Driver with Ring Buffer and Locking"); 