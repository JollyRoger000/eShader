import ftplib
import os
import sys
import configparser

def upload_file(ftp, filepath, remote_folder):
    filesize = os.path.getsize(filepath)
    uploaded = 0

    def upload_callback(data):
        nonlocal uploaded
        uploaded += len(data)
        percent_uploaded = (uploaded / filesize) * 100
        sys.stdout.write(f"\rЗагрузка: {percent_uploaded:.2f}%")
        sys.stdout.flush()

    try:
        ftp.cwd(remote_folder)  # Переход в удаленную папку
    except ftplib.error_perm as e:
        print(f"Ошибка при переходе в папку {remote_folder}: {e}")
        return

    with open(filepath, 'rb') as file:
        ftp.storbinary(f'STOR {os.path.basename(filepath)}', file, 1024, upload_callback)
    
    print("\nЗагрузка завершена.")

def main():
    # Чтение параметров подключения из конфигурационного файла
    config = configparser.ConfigParser()
    config.read('ftp_config.ini')

    ftp_host = config['ftp']['host']
    ftp_user = config['ftp']['user']
    ftp_pass = config['ftp']['password']
    remote_folder = config['ftp']['remote_folder']

    filepath = 'build\\eShader.bin'  

    # Проверка существования локального файла
    if not os.path.isfile(filepath):
        print(f"Ошибка: файл {filepath} не найден.")
        return

    ftp = None

    # Подключение к FTP-серверу
    try:
        ftp = ftplib.FTP(ftp_host)
        ftp.login(ftp_user, ftp_pass)
        print("Соединение успешно установлено.")

        upload_file(ftp, filepath, remote_folder)

    except ftplib.all_errors as e:
        print(f"Ошибка FTP: {e}")
    finally:
        if ftp:
            ftp.quit()

if __name__ == "__main__":
    main()