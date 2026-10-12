# 📄 TiffDocumentSegmenter

Сегментация TIFF-страницы на текст, изображения и фон без обученной нейросети.

![C++](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus&logoColor=white)
![OpenCV](https://img.shields.io/badge/OpenCV-4-5C3EE8?logo=opencv&logoColor=white)

## Как работает

1. Собственный TIFF-reader читает изображение и переводит RGB в оттенки серого.
2. Порог Оцу и градиент Собеля дают признаки для анализа страницы.
3. Блоки 32 × 32 классифицируются как текст, изображение или фон.
4. Последующая обработка сглаживает карту и объединяет области.
5. Программа сохраняет маску, карту классов и прямоугольники найденных областей.

Это эвристическая сегментация макета страницы, а не распознавание текста (OCR).

## Сборка и запуск

Нужны C++17, CMake ≥ 3.16, OpenCV 4 с C++-заголовками и Python 3 для скрипта запуска.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
python3 scripts/run_demo.py
```

Скрипт берёт `examples/document.tif`, создаёт рабочую папку `runs/demo/` и каталог результатов. Для своего изображения:

```bash
python3 scripts/run_demo.py --input /path/to/page.tif
```

В `runs/demo/results/` появятся:

| Файл | Содержание |
|---|---|
| `00_original_read.png` | Прочитанное изображение |
| `01_otsu_mask.png` | Бинаризация |
| `02_classification_map.png` | Текст — синий, изображения — зелёный, фон — серый |
| `03_background_marked.png` | Выделение фона |
| `04_final_rectangles.png` | Итоговые области TEXT и PHOTO |

## Ограничения

TIFF-reader ориентирован на несжатые 8-битные grayscale/RGB TIFF со strip-разметкой. Сжатие, tiled TIFF и другие глубины цвета не поддерживаются. Не все варианты TIFF и повреждённые файлы корректно диагностируются. Классификация использует фиксированные пороги и может ошибаться на другом масштабе или сложной вёрстке; численная оценка качества в исходном проекте отсутствует.

В исходнике заданы имя `17.tif` и каталог `results/`. Скрипт запуска готовит именно эту структуру, не меняя алгоритм.

`examples/document.tif` — искусственная тестовая страница, созданная скриптом `scripts/generate_examples.py`. Для её пересоздания нужны зависимости из `requirements-demo.txt`.

## Установка зависимостей C++

Для Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libopencv-dev
```

Для macOS с Homebrew:

```bash
brew install cmake opencv
```

На компьютере, где подготовлена эта копия, OpenCV C++ отсутствовал: локальная сборка и запуск не подтверждены. Входные файлы и синтаксис Python-скриптов проверены. Workflow `build.yml` подготовлен для проверки сборки и демо на Ubuntu, но ещё не запускался.
