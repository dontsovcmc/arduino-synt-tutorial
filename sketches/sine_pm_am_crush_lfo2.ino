/*
  Автор: https://t.me/deresonaa
  Создано в рамках её курса по строительству синтезатора.
*/

/*
  Синусоидальный генератор с PM, AM, биткрашем и LFO
  Arduino Nano + Mozzi (версия 2.x)

  Подключение:
    - Аудиовыход: D9 (через конденсатор 10 мкФ на усилитель / наушники)
    - A3: потенциометр — несущая частота
    - A2: потенциометр — глубина фазовой модуляции (PM)
    - A4: потенциометр — LFO, который качает частоту модулятора PM
    - A1: фоторезистор (делитель с ~10 кОм) — амплитудная модуляция (AM)
    - A0: сенсор прикосновения/давления — биткраш

  Схема сигнала:
    LFO --> частота модулятора
    модулятор (синус) --> фаза несущей (PM) --> тремоло (AM) --> биткраш --> выход
*/

#define MOZZI_CONTROL_RATE 256  // частота вызова updateControl(), Гц (нужна для плавного LFO)

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

// ---------------- Пины ----------------
#define SENSOR_PIN     A1
#define LIGHT_PIN      A0
#define PM_PIN         A2
#define FREQ_PIN       A3
#define LFO_PIN        A4

// ---------------- Несущая частота (A3) ----------------
#define MIN_FREQ       55.0f     // частота при минимальном положении потенциометра
#define FREQ_OCTAVES   5.0f      // диапазон в октавах: 55..1760 Гц

// ---------------- Фазовая модуляция (A2) ----------------
#define MOD_RATIO      1.0f      // частота модулятора = несущая * MOD_RATIO
#define PM_MAX_CYCLES  1.5f      // максимальная глубина PM (в периодах несущей)

// ---------------- LFO для частоты модулятора (A4) ----------------
#define PMLFO_MODE     0         // 0 — потенциометр задаёт ЧАСТОТУ LFO (глубина фиксирована)
                                 // 1 — потенциометр задаёт ГЛУБИНУ LFO (частота фиксирована)
#define LFO_DEPTH      0.5f      // глубина: частота модулятора качается на ±50% (не более 0.9)
#define LFO_RATE_MIN   0.1f      // диапазон частоты LFO для режима 0, Гц
#define LFO_RATE_MAX   10.0f
#define LFO_RATE_HZ    2.0f      // фиксированная частота LFO для режима 1, Гц

// ---------------- Амплитудная модуляция (A1) ----------------
#define LIGHT_MIN      147
#define LIGHT_MAX      1023      // максимум АЦП Nano
#define AM_INVERT      0         // 1 — больше света = меньше эффекта
#define AM_MODE        0         // 0 — фоторезистор задаёт ГЛУБИНУ тремоло
                                 // 1 — фоторезистор задаёт ЧАСТОТУ тремоло
#define AM_RATE_HZ     5.0f      // фиксированная частота тремоло для AM_MODE 0
#define AM_RATE_MIN    0.5f      // диапазон частоты для AM_MODE 1
#define AM_RATE_MAX    60.0f

// ---------------- Сенсор -> биткраш (A0) ----------------
#define SENSOR_MIN       300     // лёгкое касание: эффект только начинается
#define SENSOR_MAX       500     // сильное сжатие: максимальный эффект
#define CRUSH_MAX_SHIFT  6       // сколько младших бит отбрасывать при максимуме (0..7)
#define CRUSH_MAX_HOLD   4       // во сколько раз снижать частоту дискретизации (1 = выкл.)

typedef Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> SineOsc;
SineOsc carrier(SIN2048_DATA);
SineOsc modulator(SIN2048_DATA);
SineOsc lfo(SIN2048_DATA);                                            // тремоло (AM)
Oscil<SIN2048_NUM_CELLS, MOZZI_CONTROL_RATE> pmLfo(SIN2048_DATA);     // LFO для PM, работает на control-rate

// Сглаженные показания (хранятся умноженными на 16 для точности)
int freqSm = 0, pmSm = 0, lightSm = 0, sensorSm = 0, lfoSm = 0;

// Параметры для updateAudio
int32_t pmDepth = 0;       // глубина PM в формате Q16.16 (65536 = один период)
uint8_t amDepth = 0;       // глубина тремоло 0..255

int16_t crushMask  = -1;   // маска, обнуляющая младшие биты
int16_t crushRound = 0;    // добавка для округления
uint8_t crushHold  = 1;    // каждый N-й отсчёт обновляется, остальные повторяются
uint8_t holdCnt    = 0;
int16_t heldSample = 0;

inline void smooth(int &s, int v) {
  s += ((v << 4) - s) >> 3;
}

void setup() {
  // Начальные значения читаем ДО запуска Mozzi
  freqSm   = analogRead(FREQ_PIN)   << 4;
  pmSm     = analogRead(PM_PIN)     << 4;
  lightSm  = analogRead(LIGHT_PIN)  << 4;
  sensorSm = analogRead(SENSOR_PIN) << 4;
  lfoSm    = analogRead(LFO_PIN)    << 4;

  lfo.setFreq(AM_RATE_HZ);
  pmLfo.setFreq(1.0f);
  startMozzi();
}

void updateControl() {
  smooth(freqSm,   mozziAnalogRead(FREQ_PIN));
  smooth(pmSm,     mozziAnalogRead(PM_PIN));
  smooth(lightSm,  mozziAnalogRead(LIGHT_PIN));
  smooth(sensorSm, mozziAnalogRead(SENSOR_PIN));
  smooth(lfoSm,    mozziAnalogRead(LFO_PIN));

  // ---- LFO для частоты модулятора ----
  int lv = lfoSm >> 4;                                    // 0..1023
  float lfoDepth;
#if PMLFO_MODE == 0
  float lfoRate = LFO_RATE_MIN * pow(LFO_RATE_MAX / LFO_RATE_MIN, lv / 1023.0f);
  pmLfo.setFreq(lfoRate);
  lfoDepth = LFO_DEPTH * constrain(lv / 40.0f, 0.0f, 1.0f);   // у самого низа потенциометра LFO плавно выключается
#else
  pmLfo.setFreq(LFO_RATE_HZ);
  lfoDepth = LFO_DEPTH * (lv / 1023.0f);
#endif
  float lfoVal = pmLfo.next() / 128.0f;                   // -1..+1

  // ---- Несущая и модулятор ----
  float f = MIN_FREQ * pow(2.0f, (freqSm >> 4) / 1023.0f * FREQ_OCTAVES);
  carrier.setFreq(f);
  modulator.setFreq(f * MOD_RATIO * (1.0f + lfoDepth * lfoVal));

  // ---- Глубина PM ----
  pmDepth = (int32_t)((pmSm >> 4) / 1023.0f * PM_MAX_CYCLES * 65536.0f);

  // ---- AM от фоторезистора ----
  int light = constrain(lightSm >> 4, LIGHT_MIN, LIGHT_MAX);
#if AM_INVERT
  light = LIGHT_MIN + LIGHT_MAX - light;
#endif
#if AM_MODE == 0
  amDepth = map(light, LIGHT_MIN, LIGHT_MAX, 0, 255);
#else
  amDepth = 255;
  float t = (light - LIGHT_MIN) / (float)(LIGHT_MAX - LIGHT_MIN);
  float amRate = AM_RATE_MIN * pow(AM_RATE_MAX / AM_RATE_MIN, t);
  lfo.setFreq(amRate);
#endif

  // ---- Сенсор -> биткраш ----
  const int range = SENSOR_MAX - SENSOR_MIN;
  int amount = constrain((sensorSm >> 4) - SENSOR_MIN, 0, range);   // 0..range
  uint8_t shift = (amount * CRUSH_MAX_SHIFT) / range;
  crushMask  = ~((1 << shift) - 1);
  crushRound = shift ? (1 << (shift - 1)) : 0;
  crushHold  = 1 + (amount * (CRUSH_MAX_HOLD - 1)) / range;
}

AudioOutput updateAudio() {
  // Фазовая модуляция: модулятор сдвигает фазу несущей
  int8_t m = modulator.next();                           // -128..127
  int8_t c = carrier.phMod((pmDepth * m) >> 7);

  // Амплитудная модуляция (тремоло): gain от (255 - amDepth) до 255
  uint8_t l = (uint8_t)(lfo.next() + 128);               // 0..255
  uint8_t gainAM = 255 - (((uint16_t)amDepth * (uint8_t)(255 - l)) >> 8);
  int16_t s = ((int16_t)c * gainAM) >> 8;

  // Биткраш: снижение частоты дискретизации и разрядности
  if (++holdCnt >= crushHold) {
    holdCnt = 0;
    heldSample = constrain((s + crushRound) & crushMask, -128, 127);
  }

  return MonoOutput::from8Bit(heldSample);
}

void loop() {
  audioHook();
}
