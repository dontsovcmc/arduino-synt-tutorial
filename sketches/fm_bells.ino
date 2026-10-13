/*
  FM-колокола и металл на Mozzi: три ноты на сенсорах (до, ми, соль)
  Arduino Nano (16 МГц) + Mozzi 2.x, звук на D9

  FM-синтез (Джон Чоунинг, 1973): синус-модулятор качает фазу синуса-несущей.
  Если частоты модулятора и несущей в целом отношении — звук гармоничный (э-пиано, орган),
  в дробном (1.4, 2.76, 3.5...) — появляются негармоничные обертоны, как у колокола и гонга.
  Глубина модуляции затухает быстрее громкости: удар яркий, хвост мягкий — как у настоящего колокола.

  Звук собран только из средств Mozzi:
    Oscil           — несущая и модулятор для каждой ноты (синус), phMod — фазовая модуляция
    Ead             — огибающая «удар — затухание», считается 256 раз в секунду
    Line            — плавно ведёт громкость между тактами управления: без ступенек и щелчков
    Q16n16_mtof     — частота ноты по номеру MIDI (целочисленно, быстро)
    mozziAnalogRead — ручки и сенсоры читаются в фоне и не мешают звуку

  Подключение:
    - A5, A6, A7: сенсоры касания — до, ми, соль. Касание — удар, нота звенит и затухает сама.
    - A2: высота, ±12 полутонов
    - A3: материал — отношение частот модулятора и несущей, 8 положений:
          1 (мягкий), 2, 3 (полые, как э-пиано) → 1.4 (колокол), 2.76, 3.5 (колокольчики),
          4.95, 6.3 (гонг, металл)
    - A4: глубина модуляции: 0 — чистый синус, максимум — резкий металлический удар
    - A0: светодиод (анод к +5 В) — горит, пока звучит удар
    - D9: звук на усилитель / наушники
*/

#pragma GCC optimize ("O2")     // Mozzi советует оптимизацию по скорости: запас процессора +5 мкс на отсчёт
#define MOZZI_CONTROL_RATE 256  // частота вызова updateControl(), Гц

#include <Mozzi.h>
#include <Oscil.h>
#include <Ead.h>
#include <Line.h>
#include <mozzi_midi.h>
#include <tables/sin2048_int8.h>
#include <tables/sin256_int8.h>

// ---------------- Пины ----------------
#define LED_PIN     A0
#define PITCH_PIN   A2
#define RATIO_PIN   A3
#define DEPTH_PIN   A4
const uint8_t keyPin[3]  = {A5, A6, A7};
const uint8_t keyNote[3] = {60, 64, 67};   // до, ми, соль — номера MIDI

// Отношение частот модулятор/несущая ×256 для каждого положения ручки A3
const uint16_t ratioTab[8] = {256, 512, 768, 358, 707, 896, 1267, 1613};
//                            1    2    3    1.4  2.76 3.5  4.95  6.3

// ---------------- Настройки ----------------
#define PITCH_RANGE     12      // высота (A2): ±12 полутонов
#define ATTACK_MS       3       // удар: громкость нарастает за 3 мс
#define RING_MS         2500    // звон затухает за 2.5 с
#define DEPTH_MAX       255     // глубина при A4 = 1023: индекс модуляции ≈ 12 (фаза качается на ±2 периода)
#define VOICE_LEVEL     160     // одна нота на полной громкости: ±160 ступеней ШИМ из ±243
#define OUT_LEVEL       236     // сумма нот не больше: если больше — все ноты убавляются (ограничитель)
#define LED_LEVEL       24      // светодиод горит, пока громкость ноты выше этого (из VOICE_LEVEL)
#define TOUCH_ON        200     // падение показаний сенсора, при котором нота срабатывает
#define TOUCH_OFF       100     // падение, ниже которого сенсор отпущен
#define KNOB_DEADBAND   2       // пересчитываем параметры, только если ручка сдвинулась больше
#define RATIO_DEADBAND  12      // для ручки материала — больше, чтобы на границе положения не дребезжало
#define AUDIO_STEPS     (MOZZI_AUDIO_RATE / MOZZI_CONTROL_RATE)   // отсчётов звука на такт управления

// Несущая — таблица на 256 точек: phMod умножает модуляцию на размер таблицы, ×256 — просто сдвиг на байт,
// а ×2048 процессор считает циклом (с тремя нотами тогда не укладываемся в 61 мкс на отсчёт)
typedef Oscil<SIN256_NUM_CELLS, MOZZI_AUDIO_RATE> Sine256;
typedef Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> Sine2048;
Sine256 car[3] = {Sine256(SIN256_DATA), Sine256(SIN256_DATA), Sine256(SIN256_DATA)};        // несущие
Sine2048 mod[3] = {Sine2048(SIN2048_DATA), Sine2048(SIN2048_DATA), Sine2048(SIN2048_DATA)}; // модуляторы
Ead env[3] = {Ead(MOZZI_CONTROL_RATE), Ead(MOZZI_CONTROL_RATE), Ead(MOZZI_CONTROL_RATE)};
Line<unsigned int> gain[3];                // громкость ноты в ступенях ШИМ ×256, плавно между тактами
uint8_t dev[3] = {0, 0, 0};                // глубина модуляции каждой ноты сейчас: 255 — фаза на ±2 периода

int pitchSm = 0, ratioSm = 0, depthSm = 0;              // показания ручек ×16
int pitchRaw = -100, ratioRaw = -100, depthRaw = -100;  // при каком положении ручек последний раз пересчитывали
uint16_t ratio = 256;                      // текущее отношение ×256
uint8_t depth = 0;                         // глубина при ударе 0..DEPTH_MAX
int keyBase[3];                            // показания сенсора без пальца
bool keyOn[3] = {false, false, false};
uint8_t adcStep = 0;                       // шаг опроса АЦП 0..11 (см. updateControl)
uint8_t startTicks = 26;                   // пока каждый канал не измерен дважды, там нули — пропускаем

inline void smooth(int &s, int v) {        // ручка опрашивается ~21 раз в секунду — сглаживаем мягко
  s += ((v << 4) - s) >> 1;
}

void setFreqs() {
  Q16n16 shift = (long)(pitchRaw - 512) * PITCH_RANGE * 128;   // полутоны в Q16n16: 65536 / 512 = 128
  for (uint8_t i = 0; i < 3; i++) {
    Q16n16 f = Q16n16_mtof(((Q16n16)keyNote[i] << 16) + shift);
    car[i].setFreq_Q16n16(f);
    mod[i].setFreq_Q16n16((f >> 8) * ratio);                   // f × ratio / 256, без переполнения
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);             // погашен

  // До startMozzi() можно обычный analogRead()
  pitchSm = analogRead(PITCH_PIN) << 4;
  ratioSm = analogRead(RATIO_PIN) << 4;
  depthSm = analogRead(DEPTH_PIN) << 4;
  for (uint8_t i = 0; i < 3; i++) {
    int mx = 0;                            // без пальца сенсор показывает максимум
    for (uint8_t n = 0; n < 16; n++) {
      int r = analogRead(keyPin[i]);
      if (r > mx) mx = r;
    }
    keyBase[i] = mx;
  }

  startMozzi();

  // Mozzi ставит быстрый АЦП (делитель 16). Сенсоры — источник с большим сопротивлением:
  // за короткую выборку в их показание протекает соседний канал. Делитель 128, как у analogRead;
  // mozziAnalogRead по-прежнему в фоне: один канал за такт — 2 преобразования ≈ 0.2 мс.
  ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

void updateControl() {
  // ---- АЦП: ровно один канал за такт ----
  // Раньше все 6 каналов читались пачкой в начале каждого такта (12 преобразований, ~1.3 мс), и этот
  // всплеск работы 256 раз в секунду шёл помехой по питанию в звук (гребёнка 256, 512, 768... Гц).
  // Теперь каждый такт — одно короткое измерение. Порядок по 4 шага: сенсор 1, 2, 3, одна из ручек;
  // сенсор опрашивается каждые 15.6 мс, каждая ручка — каждые 47 мс.
  // mozziAnalogRead ставит канал в очередь и возвращает его показание с прошлого раза.
  uint8_t slot = adcStep & 3;              // 0..2 — сенсор, 3 — ручка
  uint8_t knob = adcStep >> 2;             // какая ручка, если slot == 3
  if (++adcStep >= 12) adcStep = 0;
  int v = mozziAnalogRead(slot < 3 ? keyPin[slot] : (knob == 0 ? PITCH_PIN : knob == 1 ? RATIO_PIN : DEPTH_PIN));
  if (startTicks) { startTicks--; return; }

  if (slot == 3) {
    // ---- Ручки: пересчёт только когда ручка сдвинулась (Q16n16 — float здесь слишком медленный) ----
    if (knob == 0) {
      smooth(pitchSm, v);
      int p = pitchSm >> 4;
      if (abs(p - pitchRaw) > KNOB_DEADBAND) { pitchRaw = p; setFreqs(); }
    } else if (knob == 1) {
      smooth(ratioSm, v);
      int r = ratioSm >> 4;
      if (abs(r - ratioRaw) > RATIO_DEADBAND) { ratioRaw = r; ratio = ratioTab[(r * 8) >> 10]; setFreqs(); }
    } else {
      smooth(depthSm, v);
      int d = depthSm >> 4;
      if (abs(d - depthRaw) > KNOB_DEADBAND) { depthRaw = d; depth = (long)d * DEPTH_MAX / 1023; }
    }
  } else {
    // ---- Сенсор slot ----
    uint8_t i = slot;
    if (v > keyBase[i]) keyBase[i] = v;            // без пальца показания максимальны
    int drop = keyBase[i] - v;                     // палец тянет показания вниз
    if (!keyOn[i] && drop > TOUCH_ON) {
      keyOn[i] = true;
      env[i].start(ATTACK_MS, RING_MS);            // удар
    } else if (keyOn[i] && drop < TOUCH_OFF) keyOn[i] = false;
    if (!keyOn[i]) keyBase[i] += (v - keyBase[i]) / 32;   // медленно подстраиваемся к дрейфу
  }

  bool ring = false;
  uint8_t level[3];
  unsigned int total = 0;
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t e = env[i].next();
    level[i] = ((unsigned int)e * VOICE_LEVEL) >> 8;
    total += level[i];
    // Глубина модуляции ~ квадрат огибающей: яркость уходит быстрее громкости
    dev[i] = ((unsigned int)depth * (((unsigned int)e * e) >> 8)) >> 8;

    ring |= level[i] > LED_LEVEL;
  }

  // Ограничитель: выход ШИМ — всего ±243 ступени. Одна нота занимает большую часть диапазона,
  // а если ноты вместе не влезают — убавляем все сразу
  unsigned int scale = 256;
  if (total > OUT_LEVEL) scale = ((unsigned long)OUT_LEVEL << 8) / total;
  for (uint8_t i = 0; i < 3; i++)   // Line плавно доводит громкость за AUDIO_STEPS отсчётов
    gain[i].set((unsigned int)level[i] * scale, AUDIO_STEPS);

  digitalWrite(LED_PIN, ring ? LOW : HIGH);
}

// Один голос FM: модулятор сдвигает фазу несущей (65536 = один период).
// Результат в ступенях ШИМ ×16: громкость g = 160 даёт синус ±160 ступеней
inline int16_t voice(uint8_t i) {
  uint8_t g = gain[i].next() >> 8;
  if (g == 0) return 0;                       // нота отзвучала — не считаем
  int8_t m = mod[i].next();
  int16_t pm = dev[i] * m;                    // умножение 8×8 — быстрое; ±32385
  int32_t x = (int32_t)pm << 2;               // ×4: до ±2 периодов (Q15n16, 65536 = период)
  asm("" : "+r"(x));                          // не даём компилятору слить ×4 с ×256 внутри phMod в медленный цикл сдвигов
  int8_t c = car[i].phMod(x);
  return (c * g) >> 3;
}

AudioOutput updateAudio() {
  int16_t sum = voice(0) + voice(1) + voice(2);       // ступени ×16, ограничитель держит до ±236×16
  return MonoOutput::fromAlmostNBit(13, sum).clip();  // 13 бит → почти 9 бит выхода Mozzi: ÷16
}

void loop() {
  audioHook();
}
