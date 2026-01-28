#!/usr/bin/env bash
#Скрипт на втором сервере слушает 5 UDP‑портов, складывает приходящие части в каталог и при наличии хотя бы 3 корректных частей собирает исходный файл.
#Для простоты ниже — вариант:
#Для каждого файла используется префикс имени FILE_ID, который передается отдельно или задается вручную.
#Скрипт запускает 5 слушателей, каждый пишет в свой файл‑часть.
#После приема всех 5 частей выполняется восстановление.




# === НАСТРОЙКИ ===
PARTS_DIR="/home/user/work/par2/receiver/tmp_receive"      # Куда складывать полученные части
RECV_DIR="/home/user/work/par2/receiver/data_receive"   # Папка для восстановленных файлов
WATCH_DIR="/home/user/work/par2/receiver/meta"

LOCAL_PORT_BASE=5000            # Должен совпадать с REMOTE_PORT_BASE с первого сервера
#LOCAL_REMOTE=$((LOCAL_PORT_BASE+1))
LOCAL_REMOTE=5001
TOTAL=5
REQUIRED=3


#подготавливаем 
SAVE_DIR=$(pwd)
echo SAVE_DIR=$SAVE_DIR
mkdir -p "$PARTS_DIR"
mkdir -p "$RECV_DIR"
mkdir -p "$WATCH_DIR"
rm -rf "$WATCH_DIR"/*
rm -rf "$PARTS_DIR"/*


# Создаем FIFO для чтения UDP метаданные
#FIFO="/tmp/udp_fifo_$$"
#mkfifo "$FIFO"
#mkfifo "/tmp/udp_fifo_$$"
#rm -f "$WATCH_DIR"/*

# перехватываем системный сигнал
trap "kill $SOCAT_PID 2>/dev/null; rm -f $FIFO; exit" INT TERM EXIT


#echo "создан FIFO $FIFO"
#Слушаем UDP через socat получаем метаданные
#socat UDP-LISTEN:$LOCAL_REMOTE,reuseaddr,fork ${FIFO}
#socat UDP-LISTEN:$LOCAL_REMOTE,reuseaddr,fork PIPE:${FIFO} &
socat UDP-LISTEN:$LOCAL_REMOTE,reuseaddr,fork  "FILE:${WATCH_DIR}/metadata.txt,creat,truncate" &

inotifywait -m -e create --format '%w%f' "$WATCH_DIR" | while read NEWFILE; do
echo "Найден новый файл: $NEWFILE"
	while read LINE ; do
	if [[ -z "$LINE" ]]; then echo "continue"; continue; fi
		#Парсим данные
		if [[ "$LINE" =~ ^([^|]+)\|([0-9]+)$ ]]; then
			FILENAME="${BASH_REMATCH[1]}"
			PARTS="${BASH_REMATCH[2]}"
#			echo "Получены метаданные: $FILENAME ($PARTS частей)"
		fi
	done < "$NEWFILE"
	echo "Получены метаданные: $FILENAME ($PARTS частей)"
	rm "$NEWFILE"
	echo "Ожидание $TOTAL частей файла '$FILENAME'"
	# Запускаем прием 5 частей, каждая пишется в отдельный файл
	for i in $(seq 0 $((PARTS-1))); do
		PART_FILE="${PARTS_DIR}/${FILENAME}.${i}_5"

		echo "Слушаю UDP порт $PORT, пишу в $PART_FILE"

		# -u: UDP, -b: бинарный, stdio отключаем
		# необходимо ввести timeout 
		socat -u "UDP-LISTEN:${LOCAL_PORT_BASE},reuseaddr" "FILE:${PART_FILE},creat,truncate" 
	done
	# Вoсстанавливаю файл   
#        cd "$PARTS_DIR"
#        zunfec -o "$FILENAME" "${FILENAME}."*_5  
        
#        cd -
        
        echo $(pwd)
        
        # Очищаем временные файлы
#	rm -rf "$PARTS_DIR"/*
#	rm -rf "$WATCH_DIR"/*
done

#echo "socat запущен:$LOCAL_REMOTE"

#while read LINE < "$FIFO"; do
#echo "read Line: $LINE "
#done

#while read line < "$FIFO"; do
#    echo "got: $line" # > ${RECV_DIR}/metadata.txt
#done

#while true; do
#	echo "read LINE FIFO"
#	while read LINE < "$FIFO"; do
#	    	if [[ -z "$LINE" ]]; then echo "continue"; continue; fi
                
 #               echo "read Line: $LINE "
		# Парсим данные
#		if [[ "$LINE" =~ ^([^|]+)\|([0-9]+)$ ]]; then
#			FILENAME="${BASH_REMATCH[1]}"
#			PARTS="${BASH_REMATCH[2]}"
#			echo "Получены метаданные: $FILENAME ($PARTS частей)"
#		fi
#	done        
    
#	echo "Ожидание $TOTAL частей файла '$FILENAME'"

	# Запускаем прием 5 частей, каждая пишется в отдельный файл
#	for i in $(seq 0 $((PARTS-1))); do
#		PART_FILE="${PARTS_DIR}/${FILENAME}.${i}_5"

#		echo "Слушаю UDP порт $PORT, пишу в $PART_FILE"

		# -u: UDP, -b: бинарный, stdio отключаем
		# необходимо ввести timeout 
#		socat -u "UDP-LISTEN:${LOCAL_PORT_BASE},reuseaddr" "FILE:${PART_FILE},creat,truncate" 
#	done

# Вoсстанавливаю файл   
 #   cd "$PARTS_DIR"
  #  zunfec -o "$FILENAME" "${FILENAME}."*_5  
	
	# Очищаем временные файлы
#    rm -rf "$PARTS_DIR"
#    echo "Файл успешно восстановлен: ${PARTS_DIR}/${FILENAME}"
#done	
        

