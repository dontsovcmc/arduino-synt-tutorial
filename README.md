# Синтезатор на Arduino

Интерактивные страницы про синтезатор на Arduino Nano и библиотеке [Mozzi](https://sensorium.github.io/Mozzi/).

**Открыть:** https://dontsovcmc.github.io/arduino-synt-tutorial/

| Страница | О чём |
|---|---|
| [Потенциометр и макетка](https://dontsovcmc.github.io/arduino-synt-tutorial/basics.html) | Как ручка превращается в число в `analogRead`, как устроена макетная плата |
| [Звуковые эффекты Mozzi](https://dontsovcmc.github.io/arduino-synt-tutorial/effects.html) | PM, LFO, тремоло, биткраш: формулы скетча, звук и осциллограмма в браузере |
| [Анализатор звука](https://dontsovcmc.github.io/arduino-synt-tutorial/scope/) | Спектр, осциллограмма, уровень и нота с микрофонного входа ([подробнее](scope/README.md)) |

## Скетч

[`sketches/sine_pm_am_crush_lfo2.ino`](sketches/sine_pm_am_crush_lfo2.ino) — синусоидальный генератор с фазовой модуляцией, тремоло, биткрашем и LFO (Arduino Nano + Mozzi 2.x, звук на D9).

Автор скетча — [t.me/deresonaa](https://t.me/deresonaa), скетч создан в рамках её курса по строительству синтезатора.
