# Fable Localization Studio

**Version 0.3.1 · C++20 / Qt 6 · Linux / Windows · MIT**

Готові збірки: [Linux x86_64](https://github.com/SmailWolf/fable-localization-studio/releases/tag/v0.3.0) · [Windows x86_64](https://github.com/SmailWolf/fable-localization-studio/releases/tag/v0.3.1).
Windows-пакет перевірено на Windows runner: запуск інтерфейсу й CPU-рушія,
українські шляхи, SQLite та ручне збереження. Linux-пакет також перевірено
з локальною TranslateGemma. Роботу експортованого патча всередині гри на Windows
ще потрібно перевіряти.

Локальна утиліта для створення й редагування перекладів **Fable II** та **Fable III**.
Основна мова інтерфейсу й типовий переклад — українська. Білоруська та інші мови
доступні на вибір. Автоматичний переклад виконується локально через TranslateGemma 4B.

**Цей репозиторій містить лише утиліту. Готових локалізацій, перекладів, файлів гри,
бази прогресу, моделі або стороннього рушія тут немає.**

## Можливості

- Вибір Fable II / Fable III та окремі запам'ятовані шляхи до `book.babel`.
- Читання, перевірка та вибіркове складання BABEL; незмінені блоки зберігаються.
- TranslateGemma 4B через локальний `llama-server`, CPU або NVIDIA CUDA.
- Профілі швидкості та якості, налаштування потоків, контексту й довжини відповіді.
- SQLite: збереження прогресу, продовження роботи, повторення помилок,
  пам'ять перекладів і повторне використання однакових текстів.
- Захист і перевірка тегів, підстановок та переносів рядків; словник термінів JSON.
- Ручний редактор оригіналу й перекладу, копіювання, вставлення, Ctrl+S.
- Фільтр «Лише помилки» та інші стани; пошук, сторінки й сортування всіх записів
  натисканням на заголовки ID / Оригінал / Переклад / Стан.
- Перевірка символів і доповнення BFT зі свого TTF/OTF: зокрема І, Ї, Є, Ґ,
  апостроф, білоруські Ў, Ё, Ы, Э та інша кирилиця.
- Xbox 360 BFT v1 (BC4) та PC BFT v1/v3 (RGBA/SDF).
- Експорт в окрему папку з маніфестом; оригінали гри не перезаписуються.

Машинні результати позначаються як чернетки: якість перекладу потрібно перевіряти.
Підтримка перевірена на наявних зразках форматів, але всі видання гри не перевірені.
Експорт замінює тексти вибраного мовного пакета, а не додає пункт мови в самій грі.

## Збірка в Linux

Потрібні компілятор C++20, `pkg-config`, Qt 6.4+ (Widgets, Network, Sql з плагіном
SQLite) та zlib. Наприклад, у Debian / Ubuntu:

```sh
sudo apt install build-essential pkg-config qt6-base-dev libqt6sql6-sqlite zlib1g-dev
./build.sh
./start.sh
```

Альтернатива через CMake 3.20+:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./start.sh
```

Програма збирається в `bin/fable-localization-studio`.

## Windows

У Windows-релізі вибирайте файл із суфіксом `-portable.zip`: він містить потрібні Qt та Microsoft Visual C++ DLL і не потребує окремого встановлення VC++ Redistributable.
Розпакуйте весь архів і запустіть `start.bat`. Підтримка пакета: Windows 10/11
x64, процесор AVX2/FMA/F16C. Модель завантажується окремо. Архів містить CPU-рушій;
для CUDA потрібен окремий Windows `llama-server.exe` у `engine/cuda/`.

Для власної збірки потрібні Visual Studio 2022 C++, CMake, Qt 6.4+ MSVC x64 і zlib.
Наприклад, з Qt 6.8.3 та zlib, встановленою через vcpkg:

```powershell
cmake -S . -B build-win -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-win --config Release
```

Для пакування потрібні `windeployqt`, SQLite-плагін, zlib DLL та Windows-збірка
llama.cpp. Повна відтворювана послідовність — у `.github/workflows/windows-release.yml`.
Linux-інсталяція й локальні проєкти при цих змінах не оновлюються автоматично.

## Підготовка локального перекладу

1. Отримайте сумісну **TranslateGemma 4B GGUF** окремо. Встановіть модель як
   `models/translategemma-4b-Q4_K_M.gguf` або виберіть її у вікні програми.
   Офіційна модель: [Google TranslateGemma](https://huggingface.co/google/translategemma-4b-it).
2. Зберіть або встановіть [llama.cpp](https://github.com/ggml-org/llama.cpp).
   Розмістіть `llama-server` разом із потрібними йому бібліотеками в
   `engine/cpu/` або `engine/cuda/`. Шляхи до обох варіантів фіксовані.
   Сервер повинен підтримувати Jinja-шаблон TranslateGemma, `source_lang_code`
   та `target_lang_code`. Відомий перевірений snapshot наведено в [THIRD_PARTY.md](THIRD_PARTY.md).
3. Відкрийте утиліту, виберіть частину гри та свій оригінальний `book.babel`.
   Мови й файл визначають окремий проєкт у базі. Файл для кожної гри запам'ятовується.
4. Спершу перекладіть невелику пробу. Значення кількості текстів **0** означає
   обробку без обмеження. Нові записи обробляються перед попередніми помилками;
   кнопка «Повторити помилки» запускає лише проблемні записи.
5. Для ручної редактури зупиніть автоматичний переклад, виберіть рядок і збережіть
   правку кнопкою або Ctrl+S. Пошук можна поєднувати з фільтром стану.
6. Для шрифтів виберіть власний TTF/OTF, додайте потрібні BFT й перевірте символи.
   Шлях `fonts/NotoSans-Regular.ttf` є лише типовою пропозицією — файл не включено.
7. Експортуйте в окрему папку, збережіть резервну копію оригіналів і перевірте патч у грі.

Теги на кшталт `<a_img>` і підстановки на кшталт `%1` / `%d` потрібно зберігати
також при ручній редактурі. Невдалий текст отримує стан «Помилка» й не блокує
інші записи. Збій самого рушія зупиняє виконання зі збереженням прогресу.

Налаштування й база створюються локально в `state/`. Не додавайте їх до Git.

## Перевірки

Перевірки підстановок, відсотків, тегів і переносів не потребують файлів гри:

```sh
QT_QPA_PLATFORM=offscreen ./bin/fable-localization-studio --self-test
```

Додаткові перевірки BABEL/BFT можна запустити на власних файлах через змінні
`FABLE_TEST_FABLE2_BOOK`, `FABLE_TEST_FABLE3_BOOK`, `FABLE_TEST_FONTS`
(шляхи BFT через `;`) та `FABLE_TEST_TTF`. Вони не постачаються з репозиторієм.
Результати записуються локально в `state/tests/`.

## Командний рядок

```sh
./start.sh --init --game fable2 --book /path/to/book.babel --project state/project.sqlite
./start.sh --run --game fable3 --book /path/to/book.babel --to uk --quality balanced --limit 10
./start.sh --run --game fable2 --book /path/to/book.babel --retry-errors --limit 0
```

`--game` приймає `fable2` або `fable3`, `--to be` обирає білоруську,
`--cuda` вмикає варіант рушія CUDA. Інтерфейс дозволяє працювати з текстами
вручну без завантаженої моделі.

## Ліцензія

Код утиліти — [MIT](LICENSE), © 2026 SmailWolf.
Ліцензії зовнішніх залежностей, моделі та шрифтів описано в [THIRD_PARTY.md](THIRD_PARTY.md).
Це неофіційний інструмент; Fable належить відповідним правовласникам.

---

**English:** A native C++20 / Qt 6 localization editor for Fable II and Fable III,
with local TranslateGemma translation, manual editing, SQLite resume, protected
placeholders, global sorting, error filtering, and BFT font augmentation. Ukrainian
is the default; Belarusian and other languages are available. This repository ships
only the utility source, without game assets, generated localizations, translation
projects, models, or external engine binaries. Build with `./build.sh`, then run
`./start.sh`. See the setup steps above and `THIRD_PARTY.md` for external dependencies.
