# lust — фейковое меню-оверлей для Android

Визуальное меню «чита» поверх других приложений — для съёмки видео. **Это только интерфейс**: оно ничего не
читает и не меняет в играх, никаких функций, кроме самого меню, в нём нет.

Готовый APK: [`release/lust.apk`](release/lust.apk) (arm64-v8a + armeabi-v7a, Android 8.0+).

## Как пользоваться

1. Установить `lust.apk`, открыть **lust**.
2. Нажать **Start** → Android откроет настройку «Поверх других приложений» → включить для lust → вернуться назад.
3. Start открывает Standoff 2, через 3 секунды поверх игры появляется меню.
4. Внизу экрана — длинная полупрозрачная белая линия: тап по ней прячет / показывает меню.
   Пока меню скрыто, все касания идут в игру / приложение под ним, вотермарка остаётся.
5. Меню двигается за заголовок, размер — за правый нижний угол, панели листаются пальцем.
6. **Бинды**: зажми опцию пальцем (вокруг пальца заполняется кольцо) → откроется окно бинда → режим Hold / Toggle /
   Always on → Create. Бинд включается нажатием на его строку в списке Binds (работает и при скрытом меню):
   Hold — пока держишь палец, Toggle — тап включает / выключает.
7. Остановить — кнопка **Stop** в уведомлении «lust».

## Что внутри

- Меню — порт [Astral-Remake](https://github.com/kdt255/Astral-Remake) (Dear ImGui) на Android: GLES3 вместо D3D11,
  ввод с тач-скрина, прокрутка панелей пальцем, конфиги в папке приложения.
- В меню и вотермарке: **lust** и **t.me/lustriped**.
- **Rage → General → Rage aim**: выбор хитбоксов (Head, Neck, Chest, Stomach, Pelvis, Arms, Legs, Feet),
  **Multipoints** (тот же список) + Point scale, слайдер **Field of view** 0–360°.
- **Visuals → Enemy** (и Team / Local): 3D-модель агента из velocitypasq (`model3d.cpp`, программный рендер в
  отдельном потоке, idle-анимация из лобби; Enemy — CT с M4, Team — T с AKR), вращение пальцем.
  - Модель рендерится в фоне с суперсэмплингом (1.3x размера превью, минимум 720p) и оставляет одно ядро под интерфейс.
  - Чамсы — материалы velocitypasq: Flat, Glow, Glass, Wireframe, Galaxy Wireframe, Waterflow, Galaxy, Glitter,
    Lightning, Fire, Star Nest, Dissolve, Crystal, Liquid Metal, Aurora. Выбор — обычный выпадающий список
    (как Type у бокса). + Overlay поверх текстуры, Glow — обводка.
  - ESP-превью по логике velocitypasq: бокс по границам модели (Default / Corner / 3D), скелет и хитбоксы по костям
    анимации, head dot, backtrack-скелет, текст на модели (ник, оружие, дистанция, флаги) с обводкой, анимированная
    полоска HP, броня, патроны, иконка оружия (у каждого элемента свой цвет, у HP — цвет полной и низкой полоски), шрифты Default / Verdana / Pixel. Любой элемент можно перетащить
    пальцем на другую сторону бокса — он примагнитится к ближайшей стороне.

## HTML-превью

[`web/lust.html`](web/lust.html) — тот же самый код меню, скомпилированный в WebAssembly: открывается в любом
современном браузере (Chrome, Edge, Firefox, Safari 16.4+), один файл без интернета. Меню, модель, чамсы, ESP,
вотермарка и белая линия снизу работают как в APK; модель рендерится во Web Worker. Конфиги в браузере хранятся
только до перезагрузки страницы.

Сборка: `pip install ziglang`, затем `web/build_web.sh`.

## Сборка

Нужны Android SDK (`platforms;android-34`, `build-tools;36.0.0`, `ndk;26.3.11579264`) и JDK 17+.

```bash
./build.sh
```

Скрипт сам распакует нужные файлы velocitypasq из `../Anesthysia_SO2_v19_9_source.zip`, соберёт `liblust.so`
NDK-клангом, ресурсы `aapt2`, Java `javac` + `d8`, и подпишет `release/lust.apk` отладочным ключом
`android/debug.keystore`. Gradle не нужен.

Скриншоты того же кода на ПК (Xvfb): `tools/build_host.sh`, затем `build/host/host_linux --tab 2 --target 0 --out shot.ppm`.

## Структура

```
src/astral/          меню (Astral-Remake, портировано): ui, страницы, превью ESP (preview.cpp — модель velocitypasq)
src/android_main.cpp JNI-хост: GL-поверхность оверлея, тач-ввод, масштаб под экран
android/             манифест, MainActivity (кнопка Start + запрос разрешения), OverlayService (оверлей + линия)
third_party/imgui/   Dear ImGui 1.92.9b + OpenGL3/ES3 бэкенд
tools/               распаковка velocitypasq, генерация иконки, ПК-хост для скриншотов
```

## Лицензии

Интерфейс основан на Astral-Remake (MIT + Commons Clause) — его лицензия лежит в
[`LICENSE-Astral-Remake.txt`](LICENSE-Astral-Remake.txt) и внутри APK (`assets/`), заголовки исходников сохранены.
Commons Clause запрещает продавать проект. Dear ImGui — MIT, шрифт Inter — SIL OFL 1.1
(см. [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)).
