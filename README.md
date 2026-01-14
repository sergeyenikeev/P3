# Mail.ru Cloud Uploader (WebDAV)

## Назначение
Утилита синхронизирует локальную директорию с Mail.ru Cloud по WebDAV. Серверные объекты **никогда не удаляются**. Правила синхронизации соответствуют требованиям из задания.

## Требования
- Windows 11
- Visual Studio 2026 (MSVC)
- CMake 3.20+

## Сборка
```bat
cmake -S . -B build
cmake --build build --config Release
```

## Использование
Запуск без параметров (реальная синхронизация при наличии учётных данных в конфиге/переменных окружения/скомпилированных значениях):
```bat
build\Release\uploader.exe
```

### Конфиг‑файл для учётных данных и параметров
Файл `uploader.conf` должен лежать рядом с исполняемым файлом (`<exe_dir>\uploader.conf`) и **не должен попадать в git** (он добавлен в `.gitignore`).

Формат:
```
email=user@mail.ru
app_password=****
source=C:\Data\ToUpload
remote=/PublicUploadRoot
base_url=https://webdav.cloud.mail.ru
threads=2
compare=size-mtime
dry_run=false
exclude=.git
exclude=*.tmp
```

Правила конфигурации:
- `source` может быть относительным (будет вычислен относительно папки exe).
- `exclude` можно указывать несколько раз.
- Приоритет: CLI‑параметры → `uploader.conf` → переменные окружения → значения, зашитые при компиляции.

### Переменные окружения (альтернатива)
- `MAILRU_EMAIL`
- `MAILRU_APP_PASSWORD`

Пример для PowerShell:
```powershell
$env:MAILRU_EMAIL = "user@mail.ru"
$env:MAILRU_APP_PASSWORD = "****"
build\Release\uploader.exe
```

### Значения по умолчанию, зашитые при компиляции
Можно собрать бинарник с нужными значениями по умолчанию, чтобы запускать без `uploader.conf` и без переменных окружения.

Пример (значения‑заглушки `TEST`, замените на свои локально и **не коммитьте**):
```bat
cmake -S . -B build ^
  -DDEFAULT_EMAIL=TEST ^
  -DDEFAULT_APP_PASSWORD=TEST ^
  -DDEFAULT_SOURCE="C:\Data\ToUpload" ^
  -DDEFAULT_REMOTE="/PublicUploadRoot" ^
  -DDEFAULT_BASE_URL="https://webdav.cloud.mail.ru" ^
  -DDEFAULT_THREADS=2 ^
  -DDEFAULT_COMPARE="size-mtime" ^
  -DDEFAULT_DRY_RUN=0 ^
  -DDEFAULT_EXCLUDES=".git;*.tmp"
cmake --build build --config Release
```

Поддерживаемые параметры компиляции:
- `DEFAULT_EMAIL`
- `DEFAULT_APP_PASSWORD`
- `DEFAULT_SOURCE`
- `DEFAULT_REMOTE`
- `DEFAULT_BASE_URL`
- `DEFAULT_THREADS` (целое > 0)
- `DEFAULT_COMPARE` (`size-mtime` или `size-only`)
- `DEFAULT_DRY_RUN` (0 или 1)
- `DEFAULT_EXCLUDES` (разделитель `;` или `,`)

Пример с явным источником и параметрами:
```bat
build\Release\uploader.exe --source "C:\Data\ToUpload" --remote "/PublicUploadRoot" --email "user@mail.ru" --app-password "****"
```

Обязательные параметры для синхронизации (если не заданы через конфиг/переменные окружения/компиляцию):
- `--email` email для WebDAV
- `--app-password` пароль приложения

Параметры с умолчанием:
- `--source` если не задан, используется подпапка `p` рядом с исполняемым файлом (`<exe_dir>\p`). Каталог создаётся автоматически.

Рекомендуемые параметры:
- `--remote` удалённый корень назначения (по умолчанию `/PublicUploadRoot`)
- `--dry-run` только показать действия, без загрузки и удаления
- `--threads N` число потоков (по умолчанию 1)
- `--exclude PATTERN` исключить путь по маске (`*` и `?`), можно указывать многократно
- `--compare size-mtime|size-only` стратегия сравнения (по умолчанию `size-mtime`)
- `--base-url URL` альтернативный WebDAV URL (нужен для тестов)

Если `--dry-run` используется без `--app-password`, удалённые проверки отключаются и все действия считаются «как если бы» объекта на сервере не было.

## Правила синхронизации
### Папки
- Если локальная папка существует, а на сервере нет — создаётся (MKCOL).
- Ничего на сервере не удаляется.

### Файлы `.jpg`
- Всегда загружаются (PUT).
- После успешной загрузки локальный файл удаляется.

### Файлы (не `.jpg`)
- Если файл на сервере отсутствует — загружается.
- Если файл на сервере есть и отличается — загружается.
- Если файл **старше 24 часов** на момент запуска и был успешно загружен — локальный файл удаляется.

## Сравнение файлов
По умолчанию используется стратегия `size-mtime`: размер + дата изменения на сервере. Если серверная дата недоступна или не распознана — файл считается отличающимся (будет загружен). Вариант `size-only` сравнивает только размер.

## Логи
Логи пишутся в `logs\YYYY-MM-DD.log` (папка создаётся автоматически). Пароль в логах не выводится.

## Как получить app-password в Mail.ru
1. Зайдите в аккаунт Mail.ru.
2. Откройте настройки безопасности.
3. Создайте пароль приложения для «WebDAV».
4. Используйте полученный пароль в `--app-password`.

## Как выбрать `--remote` для публичной ссылки
WebDAV работает с путями внутри облака, а публичная ссылка не всегда однозначно отображается на путь.

Рекомендуемый способ:
1. В веб-интерфейсе Mail.ru Cloud создайте папку (например `PublicUploadRoot`).
2. Откройте эту папку и опубликуйте её (создайте публичную ссылку).
3. Используйте путь `/PublicUploadRoot` как значение `--remote`.

Если у вас уже есть публичная ссылка, откройте её в браузере, перейдите в режим «Открыть в облаке» и посмотрите путь в хлебных крошках — его и используйте для `--remote`.

## Тесты
```bat
ctest --test-dir build -C Release --output-on-failure
```

## CI
GitHub Actions собирает проект и запускает unit/integration/e2e тесты.