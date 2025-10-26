#include <stdio.h>      // Стандартный ввод-вывод
#include <stdlib.h>     // Стандартная библиотека
#include <unistd.h>     // POSIX API
#include <fcntl.h>      // Управление файлами
#include <string.h>     // Работа со строками
#include <time.h>       // Работа со временем

// Размер буфера для операций чтения/записи
#define BUFFER_SIZE 256
// Путь к устройству для чтения (первый драйвер)
#define DEV_READ  "/dev/scull_ring_buffer0"
// Путь к устройству для записи (второй драйвер)
#define DEV_WRITE "/dev/scull_ring_buffer1"

int main() {
    int fd_read, fd_write;         // Файловые дескрипторы устройств
    char message[BUFFER_SIZE];     // Буфер для формируемых сообщений
    char read_buf[BUFFER_SIZE];    // Буфер для чтения данных
    int counter = 0;               // Счетчик сообщений
    ssize_t ret;                   // Для хранения возвращаемых значений read/write

    // Открываем устройство для чтения в блокирующем режиме
    fd_read = open(DEV_READ, O_RDONLY);
    if (fd_read < 0) {
        perror("Failed to open read device"); // Выводим ошибку
        exit(EXIT_FAILURE); // Выходим с ошибкой
    }

    // Открываем устройство для записи в блокирующем режиме
    fd_write = open(DEV_WRITE, O_WRONLY);
    if (fd_write < 0) {
        perror("Failed to open write device"); // Выводим ошибку
        close(fd_read); // Закрываем уже открытое устройство
        exit(EXIT_FAILURE); // Выходим с ошибкой
    }

    // Сообщаем о запуске процесса
    printf("Process B started (PID: %d). Reading from %s, Writing to %s\n", 
           getpid(), DEV_READ, DEV_WRITE);

    // Бесконечный цикл работы процесса
    while (1) {
        // Читаем данные из первого драйвера
        ret = read(fd_read, read_buf, BUFFER_SIZE - 1); // -1 для места под '\0'
        if (ret < 0) {
            perror("Read failed"); // Ошибка чтения
        } else if (ret > 0) {
            read_buf[ret] = '\0'; // Добавляем нуль-терминатор
            printf("Process B: Read %zd bytes: '%s'\n", ret, read_buf);
        }

        // Формируем сообщение для записи с номером
        snprintf(message, BUFFER_SIZE, "Message from Process B #%d", counter++);
        
        // Пишем сообщение во второй драйвер
        ret = write(fd_write, message, strlen(message));
        if (ret < 0) {
            perror("Write failed"); // Ошибка записи
        } else {
            printf("Process B: Wrote %zd bytes: '%s'\n", ret, message);
        }

        sleep(1); // Задержка 1 секунда для наглядности работы
    }

    // Закрываем устройства (недостижимый код в данном примере)
    close(fd_read);  // Закрываем устройство чтения
    close(fd_write); // Закрываем устройство записи
    return 0;        // Возвращаем успешный статус
}