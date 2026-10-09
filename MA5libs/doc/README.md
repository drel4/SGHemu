# Нативное воспроизведение MMF/SMAF через MA-5 DLL

Этот самостоятельный документ описывает встроенный файловый тракт
`M5_EmuSmw5.dll`: эмулятор получает **целый MMF/SMAF-файл в памяти**, сам
разбирает последовательность и запускает звук.

Сведения сопоставлены по трём источникам:

- `G:\Downloads\SmafEmulation.c`;
- `I:\Workspaces\PyMMF\player.py`;
- `I:\Workspaces\mmfrender\mmfrender\main.cpp`;
- декомпиляции исследованной `M5_EmuSmw5.dll` 2.0.1.4.

Последовательность вызовов и число аргументов подтверждаются обоими примерами и
обёртками DLL. Назначение некоторых безымянных аргументов, callback и способ
узнать окончание композиции пока не установлены.

## Ограничения ABI

- Процесс обязательно должен быть **Win32/x86**. Из 64-битного процесса эта DLL
  не загрузится.
- Рядом должны быть `M5_EmuSmw5.dll` и `M5_EmuHw.dll`, либо их каталог должен
  входить в DLL search path процесса.
- Имена экспортов предпочтительнее ординалов. Ординалы ниже относятся именно к
  исследованной DLL и могут отличаться в другой сборке.
- C-референс объявляет обычные указатели на функции, то есть в MSVC использует
  `__cdecl`. Python-референс
  загружает DLL через `ctypes.WinDLL`, то есть предполагает `stdcall`. Это
  противоречие нельзя считать доказательством `stdcall`: для нового C-кода надо
  использовать `__cdecl`, а стек x86 отдельно проверять длительным тестом.
- Автоматические сигнатуры Ghidra у экспортных thunk-функций часто ошибочно
  показывают `void`. Возвращаемые значения ниже взяты из вызывающего кода и
  внутренних обёрток.

## Сигнатуры C

```c
#include <windows.h>

typedef int  (__cdecl *PFN_MASOUND_EMU_INITIALIZE)(
    DWORD sample_rate, DWORD mode, BYTE *magic_pointer);
typedef void (__cdecl *PFN_MASOUND_EMU_TERMINATE)(void);

typedef int  (__cdecl *PFN_MASOUND_INITIALIZE)(
    int arg0, BYTE *work_buffer, int arg2);
typedef int  (__cdecl *PFN_MASOUND_TERMINATE)(void);
typedef int  (__cdecl *PFN_MASOUND_DEVICE_CONTROL)(
    int command, int arg1, int arg2, int arg3);

typedef int  (__cdecl *PFN_MASOUND_CREATE)(int object_id);
typedef int  (__cdecl *PFN_MASOUND_LOAD)(
    int object_id, BYTE *mmf_data, DWORD mmf_size,
    int load_mode, void *callback_or_context, int callback_arg);
typedef int  (__cdecl *PFN_MASOUND_OPEN)(
    int object_id, int sequence_id, int arg2, int arg3);
typedef int  (__cdecl *PFN_MASOUND_CONTROL)(
    int object_id, int sequence_id, int control,
    int *value, int arg4);
typedef int  (__cdecl *PFN_MASOUND_STANDBY)(
    int object_id, int sequence_id, int arg2);
typedef int  (__cdecl *PFN_MASOUND_SEEK)(
    int object_id, int sequence_id, int arg2, int arg3, int arg4);
typedef int  (__cdecl *PFN_MASOUND_START)(
    int object_id, int sequence_id, int loop_count, int arg3);
typedef int  (__cdecl *PFN_MASOUND_STOP)(
    int object_id, int sequence_id, int arg2);
typedef int  (__cdecl *PFN_MASOUND_CLOSE)(
    int object_id, int sequence_id, int arg2);
typedef int  (__cdecl *PFN_MASOUND_UNLOAD)(
    int object_id, int sequence_id, int arg2);
typedef int  (__cdecl *PFN_MASOUND_DELETE)(int object_id);
```

Имена `object_id`, `sequence_id`, `load_mode` и callback-параметров описывают
наблюдаемое использование, а не официальные названия Yamaha SDK. В проверенных
референсах для первых двух идентификаторов всегда передаётся `1`.

### Экспорты исследованной DLL

| Ординал | Имя | Аргументов |
|---:|---|---:|
| 2 | `MaSound_EmuInitialize` | 3 |
| 4 | `MaSound_EmuTerminate` | 0 |
| 5 | `MaSound_Initialize` | 3 |
| 6 | `MaSound_Terminate` | 0 |
| 7 | `MaSound_DeviceControl` | 4 |
| 8 | `MaSound_Create` | 1 |
| 9 | `MaSound_Load` | 6 |
| 10 | `MaSound_Open` | 4 |
| 11 | `MaSound_Control` | 5 |
| 12 | `MaSound_Standby` | 3 |
| 13 | `MaSound_Seek` | 5 |
| 14 | `MaSound_Start` | 4 |
| 15 | `MaSound_Stop` | 3 |
| 16 | `MaSound_Pause` | 3 |
| 17 | `MaSound_Restart` | 3 |
| 18 | `MaSound_Close` | 3 |
| 19 | `MaSound_Unload` | 3 |
| 20 | `MaSound_Delete` | 1 |

`Pause` и `Restart` присутствуют в DLL и имеют ту же форму, что `Stop`, но в
двух референсах они не используются. Их параметры и допустимые состояния пока
не проверены.

## Возвращаемые значения

У API две разные конвенции успеха — это не опечатка:

| Вызов | Успех в референсах |
|---|---:|
| `MaSound_Create(1)` | `1` |
| `MaSound_Load(...)` | `1` |
| `MaSound_EmuInitialize(...)` | `0` |
| `MaSound_Initialize(...)` | `0` |
| `MaSound_DeviceControl(...)` | `0` |
| `Open`, `Standby`, `Control`, `Seek`, `Start` | `0` |
| `Stop`, `Close`, `Unload` | предположительно `0`; референсы результат игнорируют |
| `Delete`, `Terminate`, `EmuTerminate` | результат для принятия решений не установлен |

Нельзя заменять проверки единым `if (result != 0)`: тогда успешные `Create` и
`Load` будут ошибочно приняты за сбой.

## Время жизни памяти

Это критическая часть контракта:

1. Рабочий блок размером не менее 1024 байт живёт от `EmuInitialize` до
   `EmuTerminate`.
2. `magic_pointer` указывает внутрь этого блока на адрес, младший байт которого
   равен `0x81`.
3. В `MaSound_Initialize` передаётся **начало исходного блока**, а не
   `magic_pointer`.
4. Буфер с байтами MMF нельзя освобождать или перемещать после `MaSound_Load`.
   Держите его до успешных `Stop`/`Close`/`Unload`/`Delete`.
5. DLL нельзя выгружать, пока её воспроизведение или внутренний audio thread
   ещё могут работать.

Безопасный поиск указателя:

```c
BYTE *work = (BYTE *)malloc(1024);
BYTE *magic = NULL;
size_t i;

for (i = 0; i < 1024; ++i) {
    if ((((UINT_PTR)(work + i)) & 0xffu) == 0x81u) {
        magic = work + i;
        break;
    }
}
if (magic == NULL) {
    /* Невозможно для непрерывного блока 1024 байт, но проверка нужна. */
}
```

## Инициализация файлового тракта

Минимальная последовательность из обоих референсов:

```c
if (MaSound_EmuInitialize(48000, 2, magic) != 0)
    goto fail;

if (MaSound_Initialize(0, work, 0) != 0)
    goto fail;

if (MaSound_DeviceControl(0x0d, 0, 0, 0) != 0)
    goto fail; /* PCM/output path */

if (MaSound_DeviceControl(0x05, 2, 0, 0) != 0 ||
    MaSound_DeviceControl(0x06, 0, 0, 0) != 0 ||
    MaSound_DeviceControl(0x08, 2, 0, 0) != 0 ||
    MaSound_DeviceControl(0x09, 0, 0, 0) != 0)
    goto fail;
```

Ключевые свойства именно файлового режима:

- второй аргумент `MaSound_EmuInitialize` равен `2`;
- дополнительно вызывается `MaSound_Initialize(0, work, 0)`;
- до четырёх общих команд вызывается `DeviceControl(0x0d, 0, 0, 0)`.

Во внутреннем файловом пути самой DLL видна более полная конфигурация:

```c
MaSound_DeviceControl(0x0d, 0,    0,    0);
MaSound_DeviceControl(0x02, 0x1f, 0,    0);
MaSound_DeviceControl(0x04, 0x1f, 0,    0);
MaSound_DeviceControl(0x03, 0,    0x1f, 0x1f);
MaSound_DeviceControl(0x05, 2,    0,    0);
MaSound_DeviceControl(0x06, 0,    0,    0);
MaSound_DeviceControl(0x08, 2,    0,    0);
MaSound_DeviceControl(0x09, 0,    0,    0);
```

Команды `0x02`/`0x03`/`0x04` отсутствуют в обоих внешних референсах, поэтому
их не надо бездумно считать обязательными. Начинать следует с минимальной
последовательности; полный вариант полезен как диагностический fallback, если
в MMF пропадают части или PCM.

## Загрузка и запуск MMF

```c
int volume = 100; /* 0..127 */
int tempo = 100;  /* проценты, 70..130 */
int key = 0;      /* полутона, -12..12 */

if (MaSound_Create(1) != 1)
    goto fail;

if (MaSound_Load(1, mmf_data, mmf_size, 1, NULL, 0) != 1)
    goto fail;

if (MaSound_Open(1, 1, 0, 0) != 0)
    goto fail;
if (MaSound_Standby(1, 1, 0) != 0)
    goto fail;

if (MaSound_Control(1, 1, 0, &volume, 0) != 0)
    goto fail;
if (MaSound_Control(1, 1, 2, &key, 0) != 0)
    goto fail;
if (MaSound_Control(1, 1, 1, &tempo, 0) != 0)
    goto fail;

if (MaSound_Seek(1, 1, 0, 0, 0) != 0)
    goto fail;
if (MaSound_Start(1, 1, 1, 0) != 0) /* один проход */
    goto fail;
```

### `MaSound_Control`

| `control` | Значение `*value` | Наблюдаемый диапазон |
|---:|---|---:|
| 0 | громкость | `0..127` |
| 1 | темп в процентах | `70..130` |
| 2 | транспонирование в полутонах | `-12..12` |

Значение передаётся по указателю на живой `int`. Не передавайте число,
приведённое к указателю.

## Зацикливание

Третий аргумент `MaSound_Start` задаёт количество проходов композиции. Это
подтверждается реализацией `mmfrender`, которая передаёт свой параметр
`loopCount` напрямую:

```c
int loop_count = 3;

if (MaSound_Start(1, 1, loop_count, 0) != 0)
    goto fail;
```

| `loop_count` | Результат |
|---:|---|
| `1` | воспроизвести один раз, без повтора |
| `N > 1` | воспроизвести всего `N` проходов |
| `0` | повторять бесконечно до явной остановки |

Отрицательные значения референс не допускает; их поведение не определено.

Таким образом, для обычного зацикленного фонового MMF достаточно:

```c
if (MaSound_Start(1, 1, 0, 0) != 0)
    goto fail;

/* ...позже, при смене музыки или завершении программы... */
MaSound_Stop(1, 1, 0);
MaSound_Close(1, 1, 0);
MaSound_Unload(1, 1, 0);
MaSound_Delete(1);
```

`MaSound_Start` запускает воспроизведение асинхронно. При `loop_count == 0`
естественного конца не будет, поэтому приложение обязано само вызвать
`MaSound_Stop` и последующий cleanup. MMF-буфер всё это время должен оставаться
живым.

Для конечного значения `N` ожидаемая длительность примерно равна длительности
одного прохода, умноженной на `N`. В `mmfrender` именно так рассчитывается
длина рендера. Если MMF содержит собственные phrase/seek points, точные границы
повтора определяет парсер Yamaha; из имеющихся референсов нельзя уверенно
сказать, всегда ли повторяется весь файл или отмеченный в нём диапазон.

Менять число повторов уже после `Start` известным `Control`-вызовом не
подтверждено. Для смены режима безопаснее выполнить полный
`Stop -> Close -> Unload -> Delete`, затем снова `Create -> Load -> Open ->
Standby -> Start` с новым `loop_count`.

## Остановка и освобождение

Для полностью запущенного объекта порядок такой:

```c
MaSound_Stop(1, 1, 0);
MaSound_Close(1, 1, 0);
MaSound_Unload(1, 1, 0);
MaSound_Delete(1);

MaSound_Terminate();
MaSound_EmuTerminate();

free(mmf_data);
free(work);
FreeLibrary(dll);
```

При ошибке посередине нельзя использовать один флаг `playing`: иначе ресурсы,
созданные до `Start`, утекут. Храните стадии отдельно и раскручивайте их назад:

```c
if (started) MaSound_Stop(1, 1, 0);
if (opened)  MaSound_Close(1, 1, 0);
if (loaded)  MaSound_Unload(1, 1, 0);
if (created) MaSound_Delete(1);
if (initialized) MaSound_Terminate();
if (emu_initialized) MaSound_EmuTerminate();
```

Буферы и DLL освобождаются только после этих вызовов. Даже если `Open` или
`Standby` завершился ошибкой, уже успешные `Load` и `Create` всё равно требуют
обратных вызовов.

## Загрузка экспортов

Загружайте по имени и проверяйте каждый результат:

```c
#define LOAD_FN(dst, type, name) do {                         \
    (dst) = (type)GetProcAddress(dll, (name));                \
    if ((dst) == NULL) goto fail;                             \
} while (0)

LOAD_FN(MaSound_Load, PFN_MASOUND_LOAD, "MaSound_Load");
```

Ординалы нужны только как fallback для экземпляра DLL с заранее проверенной
версией/хешем. Python-выражение `hdll[9]` означает поиск `MaSound_Load` по
ординалу 9, а не девятый элемент массива.

Для Python буфер файла должен находиться в глобальном объекте либо в поле
долгоживущего player-объекта:

```python
self.mmf_buffer = ctypes.create_string_buffer(data, len(data))
load(1, self.mmf_buffer, len(data), 1, None, 0)
# self.mmf_buffer = None допустимо только после Unload/Delete
```

## Что пока нельзя считать восстановленным

- точный тип и назначение последних двух параметров `MaSound_Load`;
- надёжный callback окончания композиции;
- команда `MaSound_GetEmuInfo`, возвращающая состояние playback;
- единицы и назначение трёх нулей в `MaSound_Seek`;
- назначение последнего аргумента `MaSound_Start` (в референсах всегда `0`);
- возможность безопасно держать несколько одновременно созданных объектов;
- официальные коды ошибок и допустимые переходы состояний;
- необходимость команд `DeviceControl` `0x02`/`0x03`/`0x04` для отдельных
  разновидностей SMAF.

Пока окончание MMF не восстановлено, приложение должно останавливать player по
явной команде пользователя или по собственной достоверной длительности файла.
Случайные значения `MaSound_GetEmuInfo` нельзя использовать как EOS без
отдельного теста и анализа.

## Рекомендуемая модель player-объекта

Один объект приложения должен владеть всеми связанными ресурсами:

```text
DLL handle
  -> таблица экспортов
  -> work[1024] + magic pointer
  -> MMF byte buffer
  -> флаги emu_initialized/initialized/created/loaded/opened/started
```

Все lifecycle-вызовы лучше выполнять в одном потоке или защищать одной
блокировкой. Внутренний `CRITICAL_SECTION` DLL защищает её отдельные обёртки,
но не гарантирует корректность одновременных переходов `Start`/`Unload` из
разных потоков.
