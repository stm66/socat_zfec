#include stdio.h
#include stdlib.h
#include string.h
#include unistd.h
#include time.h
#include syssocket.h
#include netinetin.h
#include arpainet.h
#include fcntl.h
#include sysstat.h

#include erasurecode.h

#define MAX_LINE     1024
#define MAX_PATH     1024
#define MAX_BUF      1024

 Параметры по умолчанию
char RECV_DIR[MAX_PATH]      = homeuserworkpar2receiverdata;
char LOG_FILE[MAX_PATH]      = homeuserworkpar2receiverlogreceiver.log;

int  REMOTE_PORT_BASE        = 5000;
int  TOTAL_PARTS             = 5;
int  REQUIRED_PARTS          = 3;

FILE log_fp = NULL;

 === Чтение конфига ===
int read_config(const char filename) {
    FILE f = fopen(filename, r);
    if (!f) {
        fprintf(stderr, Не удалось открыть конфиг %sn, filename);
        return -1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char eq = strchr(line, '=');
        if (!eq) continue;

        eq = '0';
        char key = line;
        char val = eq + 1;

        while (key == ' '  key == 't') key++;
        while (val == ' '  val == 't') val++;
        char end = val + strlen(val) - 1;
        while (end = val && (end == 'n'  end == 'r'  end == ' '  end == 't')) end-- = '0';

        if (strcmp(key, recv_dir) == 0) {
            strncpy(RECV_DIR, val, MAX_PATH-1);
        } else if (strcmp(key, log_file) == 0) {
            strncpy(LOG_FILE, val, MAX_PATH-1);
        } else if (strcmp(key, remote_port_base) == 0) {
            REMOTE_PORT_BASE = atoi(val);
        } else if (strcmp(key, total_parts) == 0) {
            TOTAL_PARTS = atoi(val);
        } else if (strcmp(key, required_parts) == 0) {
            REQUIRED_PARTS = atoi(val);
        }
    }

    fclose(f);
    return 0;
}

 === Логирование с временем ===
void log_msg(const char fmt, ...) {
    char timebuf[64];
    time_t now = time(NULL);
    struct tm tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), %Y-%m-%d %H%M%S, tm_info);

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, [%s] , timebuf);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, n);
    va_end(args);

    if (log_fp) {
        fprintf(log_fp, [%s] , timebuf);
        vfprintf(log_fp, fmt, args);
        fprintf(log_fp, n);
        fflush(log_fp);
    }
}

 === Получение метаданных ===
int recv_metadata(int sock, char basename, int total, int required) {
    char buf[256];
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    ssize_t n = recvfrom(sock, buf, sizeof(buf)-1, 0,
                         (struct sockaddr)&addr, &addr_len);
    if (n = 0) return -1;

    buf[n] = '0';
    if (sscanf(buf, %[^]%d%d, basename, total, required) != 3) {
        log_msg(Неверный формат метаданных %s, buf);
        return -1;
    }

    log_msg(Получены метаданные %s  n=%d k=%d, basename, total, required);
    return 0;
}

 === Получение части по UDP ===
int recv_udp_part(int sock, const char filepath) {
    FILE f = fopen(filepath, ab);
    if (!f) {
        log_msg(Не удалось открыть файл для записи %s, filepath);
        return -1;
    }

    char buf[MAX_BUF];
    ssize_t n;
    while (1) {
        n = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) break;
        if (n  0) {
            if (errno == EAGAIN  errno == EWOULDBLOCK) break;
            log_msg(recv() failed %s, strerror(errno));
            fclose(f);
            return -1;
        }
        fwrite(buf, 1, n, f);
    }

    fclose(f);
    log_msg(Часть сохранена %s, filepath);
    return 0;
}

 === Восстановление файла через liberasurecode ===
int reconstruct_file(const char tmp_dir, const char basename,
                     int k, int m, const char out_dir) {
     Читаем все фрагменты в память
    char fragments = malloc((k + m)  sizeof(char));
    uint64_t frag_sizes = malloc((k + m)  sizeof(uint64_t));
    int is_valid = malloc((k + m)  sizeof(int));
    int num_valid = 0;

    for (int i = 0; i  k + m; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), %s%s.%d_%d, tmp_dir, basename, i, k+m);

        FILE f = fopen(path, rb);
        if (!f) {
            is_valid[i] = 0;
            continue;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char data = malloc(size);
        if (!data  fread(data, 1, size, f) != (size_t)size) {
            free(data);
            fclose(f);
            is_valid[i] = 0;
            continue;
        }

        fclose(f);

        fragments[num_valid] = data;
        frag_sizes[num_valid] = size;
        is_valid[i] = 1;
        num_valid++;
    }

    if (num_valid  k) {
        log_msg(Недостаточно частей для восстановления %d (нужно %d), num_valid, k);
        free(fragments);
        free(frag_sizes);
        free(is_valid);
        return -1;
    }

     Инициализируем liberasurecode (RS)
    struct ec_args args = {
        .k = k,
        .m = m,
        .hd = 0,
        .interleaved = 0,
    };

    int desc = liberasurecode_instance_create(EC_BACKEND_JERASURE_RS_VAND, &args);
    if (desc  0) {
        log_msg(liberasurecode_instance_create() failed %d, desc);
        free(fragments);
        free(frag_sizes);
        free(is_valid);
        return -1;
    }

     Все фрагменты одинакового размера
    uint64_t fragment_len = frag_sizes[0];
    char avail = malloc(num_valid  sizeof(char));
    for (int i = 0; i  num_valid; i++) {
        avail[i] = fragments[i];
    }

    char out_data = NULL;
    uint64_t out_data_len = 0;

    int ret = liberasurecode_decode(desc,
                                    avail, num_valid, fragment_len,
                                    0, &out_data, &out_data_len);
    if (ret != 0) {
        log_msg(liberasurecode_decode() failed %d, ret);
        liberasurecode_instance_destroy(desc);
        free(avail);
        free(fragments);
        free(frag_sizes);
        free(is_valid);
        return -1;
    }

     Записываем восстановленный файл
    char out_path[MAX_PATH];
    snprintf(out_path, sizeof(out_path), %s%s, out_dir, basename);

    FILE out = fopen(out_path, wb);
    if (!out) {
        log_msg(Не удалось создать выходной файл %s, out_path);
        liberasurecode_decode_cleanup(desc, out_data);
        liberasurecode_instance_destroy(desc);
        free(avail);
        free(fragments);
        free(frag_sizes);
        free(is_valid);
        return -1;
    }

    fwrite(out_data, 1, out_data_len, out);
    fclose(out);

    liberasurecode_decode_cleanup(desc, out_data);
    liberasurecode_instance_destroy(desc);
    free(avail);
    free(fragments);
    free(frag_sizes);
    free(is_valid);

    log_msg(Файл восстановлен %s, out_path);
    return 0;
}

 === Основной цикл приёмника (постоянный режим) ===
int main() {
    if (read_config(receiver.conf) != 0) {
        fprintf(stderr, Ошибка чтения конфига, используются значения по умолчаниюn);
    }

    log_fp = fopen(LOG_FILE, a);
    if (!log_fp) {
        fprintf(stderr, Не удалось открыть лог‑файл %sn, LOG_FILE);
        return 1;
    }

    log_msg(Старт постоянного приёмника);

    int k = REQUIRED_PARTS;
    int m = TOTAL_PARTS - REQUIRED_PARTS;

     Слушаем метаданные на базовом порту
    int meta_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (meta_sock  0) {
        log_msg(meta socket() failed %s, strerror(errno));
        fclose(log_fp);
        return 1;
    }

    struct sockaddr_in meta_addr;
    memset(&meta_addr, 0, sizeof(meta_addr));
    meta_addr.sin_family = AF_INET;
    meta_addr.sin_addr.s_addr = INADDR_ANY;
    meta_addr.sin_port = htons(REMOTE_PORT_BASE);

    if (bind(meta_sock, (struct sockaddr)&meta_addr, sizeof(meta_addr))  0) {
        log_msg(bind() meta failed %s, strerror(errno));
        close(meta_sock);
        fclose(log_fp);
        return 1;
    }

     === Бесконечный цикл обработки файлов ===
    while (1) {
        char basename[256] = {0};
        int total_parts = 0;
        int required_parts = 0;

         Ждём метаданные
        if (recv_metadata(meta_sock, basename, &total_parts, &required_parts) != 0) {
            log_msg(Не удалось получить метаданные, продолжаем ожидание...);
            continue;
        }

         Создаём временную директорию для этого файла
        char tmp_dir[MAX_PATH];
        snprintf(tmp_dir, sizeof(tmp_dir), %stmp_%s, RECV_DIR, basename);
        char cmd[MAX_PATH];
        snprintf(cmd, sizeof(cmd), mkdir -p %s, tmp_dir);
        system(cmd);

         Слушаем части на портах 5001..5004
        int num_received = 0;
        int max_ports = TOTAL_PARTS;

        for (int port_offset = 1; port_offset  max_ports; port_offset++) {
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock  0) continue;

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(REMOTE_PORT_BASE + port_offset);

            if (bind(sock, (struct sockaddr)&addr, sizeof(addr))  0) {
                close(sock);
                continue;
            }

            fcntl(sock, F_SETFL, O_NONBLOCK);

            char part_path[MAX_PATH];
            snprintf(part_path, sizeof(part_path), %s%s.%d_%d,
                     tmp_dir, basename, port_offset-1, TOTAL_PARTS);

            if (recv_udp_part(sock, part_path) == 0) {
                num_received++;
            }

            close(sock);

             Если уже получили достаточно частей — можно восстанавливать
            if (num_received = REQUIRED_PARTS) {
                log_msg(Получено достаточно частей (%d), начинаем восстановление, num_received);
                break;
            }
        }

         Восстановление файла
        if (reconstruct_file(tmp_dir, basename, k, m, RECV_DIR) != 0) {
            log_msg(Ошибка восстановления файла %s, basename);
        }

         Очистка временных файлов
        snprintf(cmd, sizeof(cmd), rm -rf %s, tmp_dir);
        system(cmd);
    }

    close(meta_sock);
    fclose(log_fp);
    return 0;
}