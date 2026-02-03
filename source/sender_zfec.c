// программа которая использует zfec

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <errno.h>

#define WATCH_DIR      "/home/user/work/par2/sender/send"
#define TMP_DIR        "/home/user/work/par2/sender/tmp"
#define LOG_FILE       "/home/user/work/par2/sender/log/sender.log"

#define REMOTE_HOST    "192.168.1.12"
#define REMOTE_PORT    5001
#define REMOTE_PORT_BASE 5000

#define TOTAL          5
#define REQUIRED       3

#define EVENT_SIZE     (sizeof(struct inotify_event))
#define BUF_LEN        (1024 * (EVENT_SIZE + 16))

FILE *log_fp = NULL;

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
    snprintf(meta, sizeof(meta), "%s|%d", basename, total);

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

int encode_with_zfec(const char *basename, const char *tmp_dir) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "zfec -m %d -k %d -d \"%s\" \"%s\"",
             TOTAL, REQUIRED, tmp_dir, basename);

    log_msg("Кодирование: %s", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        log_msg("zfec вернул ошибку: %d", ret);
        return -1;
    }
    return 0;
}

int send_all_parts(const char *tmp_dir, const char *host, int base_port) {
    DIR *d = opendir(tmp_dir);
    if (!d) {
        log_msg("Не удалось открыть директорию частей: %s", tmp_dir);
        return -1;
    }

    struct dirent *de;
    int part_num = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        char part_path[1024];
        snprintf(part_path, sizeof(part_path), "%s/%s", tmp_dir, de->d_name);

        sleep(4);  // задержка между частями
        int port = base_port + part_num;
        if (send_udp_file_part(part_path, host, port) != 0) {
            closedir(d);
            return -1;
        }
        part_num++;
    }

    closedir(d);
    log_msg("Отправка всех частей завершена");
    return 0;
}

int main() {
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        fprintf(stderr, "Не удалось открыть лог‑файл: %s\n", LOG_FILE);
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

    char buffer[BUF_LEN];
    while (1) {
        ssize_t len = read(inotify_fd, buffer, BUF_LEN);
        if (len < 0) {
            log_msg("read() inotify failed: %s", strerror(errno));
            break;
        }

        int i = 0;
        while (i < len) {
            struct inotify_event *event = (struct inotify_event*)&buffer[i];

            if (event->len > 0 && (event->mask & IN_CREATE)) {
                char fullpath[1024];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", WATCH_DIR, event->name);

                struct stat st;
                if (stat(fullpath, &st) != 0 || !S_ISREG(st.st_mode)) {
                    i += EVENT_SIZE + event->len;
                    continue;
                }

                log_msg("Найден новый файл: %s", fullpath);

                // Переходим в директорию WATCH_DIR
                if (chdir(WATCH_DIR) != 0) {
                    log_msg("chdir() failed: %s", strerror(errno));
                    break;
                }

                // Очищаем временную директорию
                char clean_cmd[1024];
                snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s/*", TMP_DIR);
                system(clean_cmd);

                // Кодируем файл
                if (encode_with_zfec(event->name, TMP_DIR) != 0) {
                    log_msg("Ошибка кодирования файла: %s", event->name);
                    break;
                }

                // Отправляем метаданные
                if (send_udp_metadata(event->name, TOTAL, REMOTE_HOST, REMOTE_PORT) != 0) {
                    log_msg("Ошибка отправки метаданных");
                    break;
                }

                // Отправляем все части
                if (send_all_parts(TMP_DIR, REMOTE_HOST, REMOTE_PORT_BASE) != 0) {
                    log_msg("Ошибка отправки частей");
                    break;
                }

                log_msg("Обработка файла завершена, программа завершается");
                close(inotify_fd);
                fclose(log_fp);
                return 0;
            }

            i += EVENT_SIZE + event->len;
        }
    }

    close(inotify_fd);
    fclose(log_fp);
    return 1;
}
