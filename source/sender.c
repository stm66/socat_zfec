// программа которая напрямую кодирует
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/stat.h>

#include <erasurecode.h>

#define MAX_LINE 1024
#define MAX_PATH 1024
#define MAX_KEY  64

// Параметры по умолчанию
char WATCH_DIR[MAX_PATH]     = "./send";
char TMP_DIR[MAX_PATH]       = "./tmp";
char LOG_FILE[MAX_PATH]      = "./sender.log";

char REMOTE_HOST[MAX_PATH]   = "192.168.1.12";
int  REMOTE_PORT             = 5001;
int  REMOTE_PORT_BASE        = 5000;
int  TOTAL_PARTS             = 5;
int  REQUIRED_PARTS          = 3;

FILE *log_fp = NULL;

// === Чтение конфига ===
int read_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Не удалось открыть конфиг: %s\n", filename);
        return -1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        while (*key == ' ' || *key == '\t') key++;
        while (*val == ' ' || *val == '\t') val++;
        char *end = val + strlen(val) - 1;
        while (end >= val && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) *end-- = '\0';

        if (strcmp(key, "watch_dir") == 0) {
            strncpy(WATCH_DIR, val, MAX_PATH-1);
        } else if (strcmp(key, "tmp_dir") == 0) {
            strncpy(TMP_DIR, val, MAX_PATH-1);
        } else if (strcmp(key, "log_file") == 0) {
            strncpy(LOG_FILE, val, MAX_PATH-1);
        } else if (strcmp(key, "remote_host") == 0) {
            strncpy(REMOTE_HOST, val, MAX_PATH-1);
        } else if (strcmp(key, "remote_port") == 0) {
            REMOTE_PORT = atoi(val);
        } else if (strcmp(key, "remote_port_base") == 0) {
            REMOTE_PORT_BASE = atoi(val);
        } else if (strcmp(key, "total_parts") == 0) {
            TOTAL_PARTS = atoi(val);
        } else if (strcmp(key, "required_parts") == 0) {
            REQUIRED_PARTS = atoi(val);
        }
    }

    fclose(f);
    return 0;
}

// === Логирование с временем ===
void log_msg(const char *fmt, ...) {
    char timebuf[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[%s] ", timebuf);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);

    if (log_fp) {
        fprintf(log_fp, "[%s] ", timebuf);
        vfprintf(log_fp, fmt, args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
    }
}

// === Отправка метаданных по UDP ===
int send_udp_metadata(const char *basename, int total, const char *host, int port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_msg("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        log_msg("inet_pton() failed for %s", host);
        close(sock);
        return -1;
    }

    char meta[256];
    snprintf(meta, sizeof(meta), "%s|%d|%d", basename, total, REQUIRED_PARTS);

    ssize_t sent = sendto(sock, meta, strlen(meta), 0,
                          (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        log_msg("sendto() metadata failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    log_msg("Отправлены метаданные: %s", meta);
    close(sock);
    return 0;
}

// === Отправка части файла по UDP ===
int send_udp_file_part(const char *filepath, const char *host, int port) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        log_msg("Не удалось открыть файл для отправки: %s", filepath);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_msg("socket() failed: %s", strerror(errno));
        fclose(f);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        log_msg("inet_pton() failed for %s", host);
        close(sock);
        fclose(f);
        return -1;
    }

    unsigned char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        ssize_t sent = sendto(sock, buf, n, 0,
                              (struct sockaddr*)&addr, sizeof(addr));
        if (sent < 0) {
            log_msg("sendto() part failed: %s", strerror(errno));
            fclose(f);
            close(sock);
            return -1;
        }
    }

    fclose(f);
    close(sock);
    log_msg("Часть отправлена: %s", filepath);
    return 0;
}

// === Реальное кодирование Рида–Соломона через liberasurecode ===
int encode_with_liberasurecode(const char *src, const char *tmp_dir, int k, int m) {
    FILE *f = fopen(src, "rb");
    if (!f) {
        log_msg("Не удалось открыть исходный файл: %s", src);
        return -1;
    }

    // Читаем весь файл в память
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(file_size);
    if (!data) {
        fclose(f);
        log_msg("Не удалось выделить память для файла");
        return -1;
    }

    if (fread(data, 1, file_size, f) != (size_t)file_size) {
        free(data);
        fclose(f);
        log_msg("Ошибка чтения файла: %s", src);
        return -1;
    }

    fclose(f);

    // Инициализируем liberasurecode (RS)
    struct ec_args args = {
        .k = k,
        .m = m,
        .hd = 0,
//        .interleaved = 0,
    };

    int desc = liberasurecode_instance_create(EC_BACKEND_JERASURE_RS_VAND, &args);
    if (desc < 0) {
        free(data);
        log_msg("liberasurecode_instance_create() failed: %d", desc);
        return -1;
    }

    char **encoded_data = NULL;
    char **encoded_parity = NULL;
    uint64_t fragment_len = 0;

    int ret = liberasurecode_encode(desc,
                                    data, file_size,
                                    &encoded_data, &encoded_parity,
                                    &fragment_len);
    if (ret != 0) {
        liberasurecode_instance_destroy(desc);
        free(data);
        log_msg("liberasurecode_encode() failed: %d", ret);
        return -1;
    }

    // Сохраняем k данных + m паритетов как отдельные файлы
    char part_path[MAX_PATH];
    int idx = 0;

    for (int i = 0; i < k; i++) {
        snprintf(part_path, sizeof(part_path), "%s/%s.%d_%d", tmp_dir, src + strlen(WATCH_DIR) + 1, idx++, k+m);

        FILE *part = fopen(part_path, "wb");
        if (!part) {
            log_msg("Не удалось создать часть: %s", part_path);
            liberasurecode_encode_cleanup(desc, encoded_data, encoded_parity);
            liberasurecode_instance_destroy(desc);
            free(data);
            return -1;
        }

        fwrite(encoded_data[i], 1, fragment_len, part);
        fclose(part);
        log_msg("Создана часть данных: %s (размер %lu)", part_path, fragment_len);
    }

    for (int i = 0; i < m; i++) {
        snprintf(part_path, sizeof(part_path), "%s/%s.%d_%d", tmp_dir, src + strlen(WATCH_DIR) + 1, idx++, k+m);

        FILE *part = fopen(part_path, "wb");
        if (!part) {
            log_msg("Не удалось создать часть паритета: %s", part_path);
            liberasurecode_encode_cleanup(desc, encoded_data, encoded_parity);
            liberasurecode_instance_destroy(desc);
            free(data);
            return -1;
        }

        fwrite(encoded_parity[i], 1, fragment_len, part);
        fclose(part);
        log_msg("Создана часть паритета: %s (размер %lu)", part_path, fragment_len);
    }

    liberasurecode_encode_cleanup(desc, encoded_data, encoded_parity);
    liberasurecode_instance_destroy(desc);
    free(data);

    return 0;
}

// === Отправка всех частей ===
int send_all_parts(const char *tmp_dir, const char *basename, const char *host, int base_port, int total_parts) {
    for (int i = 0; i < total_parts; i++) {
        char part_path[MAX_PATH];
        snprintf(part_path, sizeof(part_path), "%s/%s.%d_%d",
                 tmp_dir, basename, i, total_parts);

        sleep(4);
        int port = base_port + i;
        if (send_udp_file_part(part_path, host, port) != 0) {
            return -1;
        }
    }

    log_msg("Отправка всех частей завершена");
    return 0;
}

// === Основной цикл inotify ===
int main() {
    // Читаем конфиг
    if (read_config("sender.conf") != 0) {
        fprintf(stderr, "Ошибка чтения конфига, используются значения по умолчанию\n");
    }

    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        fprintf(stderr, "Не удалось открыть лог-файл: %s\n", LOG_FILE);
        return 1;
    }

    log_msg("Старт программы");

    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        log_msg("inotify_init() failed: %s", strerror(errno));
        fclose(log_fp);
        return 1;
    }

    int watch_fd = inotify_add_watch(inotify_fd, WATCH_DIR, IN_CREATE);
    if (watch_fd < 0) {
        log_msg("inotify_add_watch() failed: %s", strerror(errno));
        close(inotify_fd);
        fclose(log_fp);
        return 1;
    }

    char buffer[1024 * (sizeof(struct inotify_event) + 16)];
    while (1) {
        ssize_t len = read(inotify_fd, buffer, sizeof(buffer));
        if (len < 0) {
            log_msg("read() inotify failed: %s", strerror(errno));
            break;
        }

        int i = 0;
        while (i < len) {
            struct inotify_event *event = (struct inotify_event*)&buffer[i];

            if (event->len > 0 && (event->mask & IN_CREATE)) {
                char fullpath[MAX_PATH];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", WATCH_DIR, event->name);

                struct stat st;
                if (stat(fullpath, &st) != 0 || !S_ISREG(st.st_mode)) {
                    i += sizeof(struct inotify_event) + event->len;
                    continue;
                }

                log_msg("Найден новый файл: %s", fullpath);

                // Очищаем временную директорию
                char clean_cmd[MAX_PATH];
                snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s/*", TMP_DIR);
                system(clean_cmd);

                // Кодируем файл через liberasurecode (RS)
                if (encode_with_liberasurecode(fullpath, TMP_DIR, REQUIRED_PARTS, TOTAL_PARTS - REQUIRED_PARTS) != 0) {
                    log_msg("Ошибка кодирования liberasurecode для файла: %s", fullpath);
                    break;
                }

                // Отправляем метаданные
                if (send_udp_metadata(event->name, TOTAL_PARTS, REMOTE_HOST, REMOTE_PORT) != 0) {
                    log_msg("Ошибка отправки метаданных");
                    break;
                }

                // Отправляем все части
                if (send_all_parts(TMP_DIR, event->name, REMOTE_HOST, REMOTE_PORT_BASE, TOTAL_PARTS) != 0) {
                    log_msg("Ошибка отправки частей");
                    break;
                }

                log_msg("Обработка файла завершена, программа завершается");
                close(inotify_fd);
                fclose(log_fp);
                return 0;
            }

            i += sizeof(struct inotify_event) + event->len;
        }
    }

    close(inotify_fd);
    fclose(log_fp);
    return 1;
}
