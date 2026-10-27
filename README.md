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

Путь `--input` может быть относительным к текущему каталогу. Для исполняемого файла в другом каталоге сборки используйте `--binary`:

```bash
python3 scripts/run_demo.py --binary build/Release/segmenter.exe \
  --input /path/to/page.tif
```

Повторный запуск использует ту же папку `runs/demo/` и перезаписывает одноимённые результаты.

В `runs/demo/results/` появятся:

| Файл | Содержание |
|---|---|
| `00_original_read.png` | Прочитанное изображение |
| `01_otsu_mask.png` | Бинаризация |
| `02_classification_map.png` | Текст — синий, изображения — зелёный, фон — серый |
| `03_background_marked.png` | Выделение фона |
| `04_final_rectangles.png` | Итоговые области TEXT и PHOTO |

## Ограничения

TIFF-reader обрабатывает первый IFD изображения: несжатые 8-битные grayscale TIFF и RGB с чередующимися цветовыми каналами (chunky/interleaved), со strip-разметкой. Сжатие, tiled TIFF, planar RGB, BigTIFF, другие глубины цвета и обход всех страниц многостраничного документа не поддерживаются. Не все варианты TIFF и повреждённые файлы корректно диагностируются. Классификация использует фиксированные пороги и может ошибаться на другом масштабе или сложной вёрстке; численная оценка качества в исходном проекте отсутствует.

В исходнике заданы имя `17.tif` и каталог `results/`. Скрипт запуска готовит именно эту структуру, не меняя алгоритм.

`examples/document.tif` — искусственная тестовая страница, созданная скриптом `scripts/generate_examples.py`. Pillow нужен только для её пересоздания; скрипт запуска использует стандартную библиотеку Python.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-demo.txt
.venv/bin/python scripts/generate_examples.py
```

Генератор перезаписывает `examples/document.tif`. Он создаёт несжатое grayscale-изображение, соответствующее формату демонстрационного входа.

## Установка зависимостей C++

Для Ubuntu/Debian:

```bash
sudo apt install build-essential cmake libopencv-dev
```

Для macOS с Homebrew:

```bash
brew install cmake opencv
```

## Структура и автоматическая проверка

- `src/main.cpp` — чтение TIFF, вычисление признаков и сегментация.
- `scripts/run_demo.py` — подготовка фиксированных путей и запуск программы.
- `scripts/generate_examples.py` — создание синтетической страницы.

Workflow [Build and demo](.github/workflows/build.yml) устанавливает OpenCV на Ubuntu, собирает программу через CMake и запускает синтетическое демо. Проверка останавливается при ошибке команды, но не сравнивает сегментацию с размеченным эталоном.
