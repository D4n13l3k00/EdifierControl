# Edifier Control

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20x64-0078D6.svg)](https://microsoft.com/windows)
[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C.svg)](https://isocpp.org/)
[![UI: Dear ImGui](https://img.shields.io/badge/UI-Dear%20ImGui%20%2B%20DirectX%2011-orange.svg)](https://github.com/ocornut/imgui)

Трей-утилита для управления мониторами **EDIFIER** (MR3, MR5, MR4.5, MR4 MKII) по Bluetooth Low Energy (BLE) без мобильного приложения.

Написана на C++20, рендерится через Dear ImGui и DirectX 11. Собирается статически (`/MT`) в один компактный `.exe` без зависимостей от Visual C++ Redistributable, WebView2 или .NET.

---

## Возможности

### Звук и громкость
- Ползунок громкости (0–30) с тактильной ручкой захвата и быстрый Mute в один клик (по иконке динамика или по цифрам громкости).
- Всплывающий OSD-индикатор громкости (HUD) по центру экрана при регулировке колёсиком мыши над треем. Сквозной для кликов, не сворачивает полноэкранные игры.
- Переключение входов: Bluetooth, AUX, Line In / TRS.
- Калиброванный стерео-измеритель уровня в децибелах (-60 dB .. 0 dB, баллистика Arturia MiniFuse / OBS). Правый клик по шкале открывает выбор любого активного аудиоустройства Windows.

### Эквалайзер и акустическая коррекция
- Профили звучания: Monitor, Music, Custom.
- Пользовательский эквалайзер: 6 полос для MR3, 9 полос для MR5 / MR4.5 / MR4 MKII. Значения считываются напрямую из чипа DSP и отправляются обратно по BLE.
- Срез низких частот (20–100 Гц) с выбором крутизны спада (-6, -12, -18, -24 дБ/окт).
- Компенсация отражений от стола (Desktop Reflection) и акустического пространства (0 дБ в свободном поле, -2 дБ у стены, -4 дБ в углу).

### Система и трей
- Контекстное меню трея: быстрое переключение пресетов Monitor/Music/Custom, Mute/Unmute, автозапуск с Windows, открытие `config.json` в Блокноте.
- Индикатор уровня сигнала BLE (RSSI) в заголовке окна и на вкладке устройства.
- Автоматическое восстановление соединения при выходе Windows из спящего режима (`WM_POWERBROADCAST`).

---

## Поддерживаемые модели

| Модель | Статус | Примечания |
| :--- | :---: | :--- |
| **EDIFIER MR3** | Проверено на железе | Громкость, входы, 6-полосный EQ, фильтры среза и акустического пространства. |
| **EDIFIER MR5** | Поддерживается | Профиль подключения, 9-полосный EQ, протокол ConneX v2. |
| **EDIFIER MR4.5** | Поддерживается | Профиль поиска, 9-полосный EQ. |
| **EDIFIER MR4 MKII** | Поддерживается | Включая ревизии на DSP 8625. |

---

## Установка

1. Скачайте `EdifierControl.exe` или `.zip` со страницы [Releases](../../releases).
2. Запустите файл. Значок появится в системном трее рядом с часами.
3. Нажмите «Найти колонку», выберите свою модель и нажмите «Подключить».

Конфигурация и привязка к колонкам сохраняются в `%APPDATA%\EdifierControl\config.json`.

---

## Сборка

Требования:
- Visual Studio 2022 или 2026 (набор инструментов C++ x64)
- Windows 10 / 11 SDK

### В Visual Studio
1. Откройте `EdifierControl.sln`.
2. Выберите конфигурацию **Release | x64**.
3. Нажмите `Ctrl + Shift + B`. Бинарник соберется в `build/Release/EdifierControl.exe`.

### Через командную строку
```cmd
msbuild EdifierControl.sln /p:Configuration=Release /p:Platform=x64
```

---

## Структура репозитория

```
EdifierControl/
├── assets/                 # Иконки приложения (.ico) и ресурсы (.rc)
├── src/
│   ├── app/                # WinMain, OSD-окно, трей, меню, аудио-измеритель WASAPI
│   ├── ble/                # WinRT BLE клиент (сканирование, сопряжение, чтение/запись GATT)
│   ├── protocol/           # Протокол ConneX (v1/v2, срез НЧ, 6/9-band EQ)
│   └── ui/                 # Dear ImGui + DirectX 11 (анимации, вкладки, слайдеры)
├── third_party/imgui/      # Dear ImGui и бэкенды DX11 + Win32
├── .github/workflows/      # GitHub Actions CI/CD и авто-релиз
├── EdifierControl.sln      # Решение Visual Studio
├── EdifierControl.vcxproj  # Проект с настройками /std:c++20 и /MT
├── LICENSE                 # Лицензия
└── README.md
```

---

## Лицензия

[MIT](LICENSE)
