#!/usr/bin/env bash
#Скрипт следит за директорией, как только появляется новый файл — кодирует его в 5 кусков и отправляет по UDP на второй сервер
#После этого прекращает работу
#

# === НАСТРОЙКИ ===
WATCH_DIR="/home/user/work/par2/sender/send"      # Папка, которую нужно контролировать
TMP_DIR="/home/user/work/par2/sender/tmp"       # Временная папка для частей
REMOTE_HOST="192.168.1.12"         # IP второго сервера
REMOTE_PORT_BASE=5000           # Базовый порт, части пойдут на 5000..5004
#REMOTE_PORT=$((REMOTE_PORT_BASE+1))
REMOTE_PORT=5001

# Параметры zfec: всего 5 частей, достаточно любых 3 для восстановления
TOTAL=5
REQUIRED=3

cd $WATCH_DIR

mkdir -p "$TMP_DIR"

# Требуется утилита inotifywait (пакет inotify-tools)
inotifywait -m -e create --format '%w%f' "$WATCH_DIR" | while read NEWFILE; do
    # Проверяем, что это обычный файл (а не каталог, сокет и т.п.)
    if [ ! -f "$NEWFILE" ]; then
        continue
    fi

    echo "Найден новый файл: $NEWFILE"

    # Очищаем временную директорию
    rm -rf "$TMP_DIR"/*

    BASENAME=$(basename "$NEWFILE")
#  BASE=$(basename "$NEWFILE" .${NEWFILE##*.})
#    TEMP_DIR="/tmp/zfec_$(date +%s)_$BASE"
	# Отправляем метаданные (имя файла и количество частей)
    echo "Отправляем метаданные $BASENAME|$TOTAL"
    echo "$BASENAME|$TOTAL" | socat - UDP:$REMOTE_HOST:$REMOTE_PORT

    # Кодирование файла в 5 частей (zfec)
    # Создаст файлы вида BASENAME.0_5, BASENAME.1_5, ... BASENAME.4_5
    zfec -m "$TOTAL" -k "$REQUIRED" -d "$TMP_DIR" "$BASENAME"

    if [ $? -ne 0 ]; then
        echo "Ошибка кодирования zfec для файла $NEWFILE"
        continue
    fi

    echo "Файл закодирован, отправка частей..."
    

#    i=0
    for PART in "$TMP_DIR"/*; do
#        PORT=$((REMOTE_PORT_BASE + i))
#      задерживаем отправление каждой части на несколько секунд так как приемной стороне необходимо запустить программу
        sleep 4
        echo "Отправка части $PART на ${REMOTE_HOST}:${PORT}"
        # socat отправляет содержимое файла по UDP
        socat -u "FILE:$PART" "UDP:${REMOTE_HOST}:${REMOTE_PORT_BASE}" 
        
#        i=$((i+1))
    done

#    wait  # Ждем завершения всех фоновых socat
    echo "Отправка всех частей завершена для файла $BASENAME"
#    rm "$TMP_DIR"/* 
done

# установить пакеты zfec, socat, inotify-tools
# ubuntu 
# sudo apt update
# sudo apt install inotify-tools
# sudo apt install socat
#// sudo apt install zfec
# sudo apt -y install python3-zfec
# Проверка установки
# zfec --version
# Путь WATCH_DIR замените на свою директорию.
# REMOTE_HOST и REMOTE_PORT_BASE замените на адрес и базовый порт второго сервера
