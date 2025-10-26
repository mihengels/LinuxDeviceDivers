#include <stdio.h>      // Стандартный ввод-вывод (printf, perror)
#include <stdlib.h>     // Стандартная библиотека (exit, malloc)
#include <unistd.h>     // POSIX API (read, write, close, sleep)
#include <fcntl.h>      // Управление файлами (open, O_RDONLY, O_WRONLY)
#include <string.h>     // Работа со строками (strlen, strcpy)
#include <time.h>       // Работа со временем (не используется здесь, но может понадобиться)

// Размер буфера для операций чтения/записи
#define BUFFER_SIZE 256
// Путь к устройству для записи (первый драйвер)
#define DEV_WRITE "/dev/scull_ring_buffer0"
// Путь к устройству для чтения (второй драйвер)
#define DEV_READ  "/dev/scull_ring_buffer1"

int main() {
    int fd_write, fd_read;          // Файловые дескрипторы устройств
    char message[BUFFER_SIZE];      // Буфер для формируемых сообщений
    char read_buf[BUFFER_SIZE];     // Буфер для чтения данных
    int counter = 0;                // Счетчик сообщений
    ssize_t ret;                    // Для хранения возвращаемых значений read/write

    // Открываем устройство для записи в блокирующем режиме
    fd_write = open(DEV_WRITE, O_WRONLY);
    if (fd_write < 0) {
        perror("Failed to open write device"); // Выводим ошибку
        exit(EXIT_FAILURE); // Выходим с ошибкой
    }

    // Открываем устройство для чтения в блокирующем режиме
    fd_read = open(DEV_READ, O_RDONLY);
    if (fd_read < 0) {
        perror("Failed to open read device"); // Выводим ошибку
        close(fd_write); // Закрываем уже открытое устройство
        exit(EXIT_FAILURE); // Выходим с ошибкой
    }

    // Сообщаем о запуске процесса
    printf("Process A started (PID: %d). Writing to %s, Reading from %s\n", 
           getpid(), DEV_WRITE, DEV_READ);

    // Бесконечный цикл работы процесса
    while (1) {
        // Формируем сообщение для записи с номером
        snprintf(message, BUFFER_SIZE, "Message from Process A #%d", counter++);
        
        // Пишем сообщение в первый драйвер
        ret = write(fd_write, message, strlen(message));
        if (ret < 0) {
            perror("Write failed"); // Ошибка записи
        } else {
            printf("Process A: Wrote %zd bytes: '%s'\n", ret, message);
        }

        // Читаем данные из второго драйвера
        ret = read(fd_read, read_buf, BUFFER_SIZE - 1); // -1 для места под '\0'
        if (ret < 0) {
            perror("Read failed"); // Ошибка чтения
        } else if (ret > 0) {
            read_buf[ret] = '\0'; // Добавляем нуль-терминатор для вывода как строки
            printf("Process A: Read %zd bytes: '%s'\n", ret, read_buf);
        }

        sleep(1); // Задержка 1 секунда для наглядности работы
    }

    // Эти строки никогда не выполнятся в данном примере с бесконечным циклом
    close(fd_write); // Закрываем устройство записи
    close(fd_read);  // Закрываем устройство чтения
    return 0;        // Возвращаем успешный статус
}