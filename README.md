# Синтезатор на Arduino

Интерактивные страницы про синтезатор на Arduino Nano и библиотеке [Mozzi](https://sensorium.github.io/Mozzi/).

**Открыть:** https://dontsovcmc.github.io/arduino-synt-tutorial/

| Страница | О чём |
|---|---|
| [Макетная плата](https://dontsovcmc.github.io/arduino-synt-tutorial/breadboard.html) | Какие отверстия соединены, первая цепь со светодиодом |
| [Потенциометр](https://dontsovcmc.github.io/arduino-synt-tutorial/potentiometer.html) | Как ручка превращается в число в `analogRead` |
| [Фоторезистор](https://dontsovcmc.github.io/arduino-synt-tutorial/photoresistor.html) | Датчик в коробке с ширмой на окошке, данные GL5528, калибровка темноты в Mozzi |
| [Сенсор касания](https://dontsovcmc.github.io/arduino-synt-tutorial/sensor.html) | Два провода к A0 и GND: пальцы или яблоко, подтяжка к +5 В |
| [Звуковые эффекты Mozzi](https://dontsovcmc.github.io/arduino-synt-tutorial/effects.html) | PM, LFO, тремоло, биткраш: формулы скетча, звук и осциллограмма в браузере |
| [Анализатор звука](https://dontsovcmc.github.io/arduino-synt-tutorial/scope/) | Спектр, осциллограмма, уровень и нота с микрофонного входа ([подробнее](scope/README.md)) |

## Поделки

| Поделка | О чём |
|---|---|
| [Паровоз](https://dontsovcmc.github.io/arduino-synt-tutorial/parovoz.html) | Паровоз звенит колоколами: три сенсора касания, FM-синтез в Mozzi, скетч [`fm_bells.ino`](sketches/fm_bells.ino) |

## Скетч

[`sketches/sine_pm_am_crush_lfo2.ino`](sketches/sine_pm_am_crush_lfo2.ino) — синусоидальный генератор с фазовой модуляцией, тремоло, биткрашем и LFO (Arduino Nano + Mozzi 2.x, звук на D9).

Автор скетча — [t.me/deresonaa](https://t.me/deresonaa), скетч создан в рамках её курса по строительству синтезатора.
