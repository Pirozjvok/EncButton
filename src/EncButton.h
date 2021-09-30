/*
    Ультра лёгкая и быстрая библиотека для энкодера, энкодера с кнопкой или просто кнопки
    Документация:
    GitHub: https://github.com/GyverLibs/EncButton
    Возможности:
    - Максимально быстрое чтение пинов для AVR (ATmega328/ATmega168, ATtiny85/ATtiny13)
    - Оптимизированный вес
    - Быстрые и лёгкие алгоритмы кнопки и энкодера
    - Энкодер: поворот, нажатый поворот, быстрый поворот, счётчик
    - Кнопка: антидребезг, клик, несколько кликов, счётчик кликов, удержание, режим step
    - Подключение - только HIGH PULL!
    - Опциональный режим callback (+22б SRAM на каждый экземпляр)
    
    AlexGyver, alex@alexgyver.ru
    https://alexgyver.ru/
    MIT License

    Версии:
    v1.1 - пуллап отдельныи методом
    v1.2 - можно передать конструктору параметр INPUT_PULLUP / INPUT(умолч)
    v1.3 - виртуальное зажатие кнопки энкодера вынесено в отдельную функцию + мелкие улучшения
    v1.4 - обработка нажатия и отпускания кнопки
    v1.5 - добавлен виртуальный режим
    v1.6 - оптимизация работы в прерывании
    v1.6.1 - PULLUP по умолчанию
    v1.7 - большая оптимизация памяти, переделан FastIO
    v1.8 - индивидуальная настройка таймаута удержания кнопки (была общая на всех)
    v1.8.1 - убран FastIO
    v1.9 - добавлена отдельная отработка нажатого поворота и запрос направления
    v1.10 - улучшил обработку released, облегчил вес в режиме callback и исправил баги
*/

#ifndef _EncButton_h
#define _EncButton_h

// ========= НАСТРОЙКИ (можно передефайнить из скетча) ==========
#define _EB_FAST 30     // таймаут быстрого поворота
#define _EB_DEB 80      // дебаунс кнопки
#define _EB_HOLD 1000   // таймаут удержания кнопки
#define _EB_STEP 500    // период срабатывания степ
#define _EB_CLICK 400	// таймаут накликивания

// =========== НЕ ТРОГАЙ ============
#include <Arduino.h>

// флаг макро
#define _EB_setFlag(x) (flags |= 1 << x)
#define _EB_clrFlag(x) (flags &= ~(1 << x))
#define _EB_readFlag(x) ((flags >> x) & 1)

#ifndef EB_FAST
#define EB_FAST _EB_FAST
#endif
#ifndef EB_DEB
#define EB_DEB _EB_DEB
#endif
#ifndef EB_HOLD
#define EB_HOLD _EB_HOLD
#endif
#ifndef EB_STEP
#define EB_STEP _EB_STEP
#endif
#ifndef EB_CLICK
#define EB_CLICK _EB_CLICK
#endif

enum eb_callback {
    TURN_HANDLER,       // 0
    RIGHT_HANDLER,      // 1
    LEFT_HANDLER,       // 2
    RIGHT_H_HANDLER,    // 3
    LEFT_H_HANDLER,     // 4
    CLICK_HANDLER,      // 5
    HOLDED_HANDLER,     // 6
    STEP_HANDLER,       // 7
    PRESS_HANDLER,      // 8
    CLICKS_HANDLER,     // 9
    RELEASE_HANDLER,    // 10
    HOLD_HANDLER,       // 11
    TURN_H_HANDLER,     // 12
};

// константы
#define EB_TICK 0
#define EB_CALLBACK 1

#define EB_NO_PIN 255

#define VIRT_ENC 254
#define VIRT_ENCBTN 253
#define VIRT_BTN 252

// ===================================== CLASS =====================================
template < uint8_t _EB_MODE, uint8_t _S1 = EB_NO_PIN, uint8_t _S2 = EB_NO_PIN, uint8_t _KEY = EB_NO_PIN >
class EncButton {
public:
    // можно указать режим работы пина
    EncButton(const uint8_t mode = INPUT_PULLUP) {
        if (_S1 < 252 && mode == INPUT_PULLUP) pullUp();
    }
    
    // подтянуть пины внутренней подтяжкой
    void pullUp() {
        if (_S1 < 252) {                        // реальное устройство
            if (_S2 == EB_NO_PIN) {             // обычная кнопка
                pinMode(_S1, INPUT_PULLUP);
            } else if (_KEY == EB_NO_PIN) {     // энк без кнопки
                pinMode(_S1, INPUT_PULLUP);
                pinMode(_S2, INPUT_PULLUP);
            } else {                            // энк с кнопкой
                pinMode(_S1, INPUT_PULLUP);
                pinMode(_S2, INPUT_PULLUP);
                pinMode(_KEY, INPUT_PULLUP);
            }
        }
    }
    
    // установить таймаут удержания кнопки для isHold(), мс (до 30 000)
    void setHoldTimeout(int tout) {
        _holdT = tout >> 7;
    }
    
    // виртуально зажать кнопку энкодера
    void holdEncButton(bool state) {
        if (state) _EB_setFlag(7);
        else _EB_clrFlag(7);
    }
    
    // ===================================== TICK =====================================
    // тикер, вызывать как можно чаще
    // вернёт отличное от нуля значение, если произошло какое то событие
    uint8_t tick(uint8_t s1 = 0, uint8_t s2 = 0, uint8_t key = 0) {
        tickISR(s1, s2, key);
        checkCallback();
        return EBState;
    }
    
    // тикер специально для прерывания, не проверяет коллбэки
    uint8_t tickISR(uint8_t s1 = 0, uint8_t s2 = 0, uint8_t key = 0) {
        if (!_isrFlag) {
            _isrFlag = 1;
            
            // обработка энка (компилятор вырежет блок если не используется)
            // если объявлены два пина или выбран вирт. энкодер или энкодер с кнопкой
            if ((_S1 < 252 && _S2 < 252) || _S1 == VIRT_ENC || _S1 == VIRT_ENCBTN) {
                uint8_t state;
                if (_S1 >= 252) state = s1 | (s2 << 1);             // получаем код
                else state = fastRead(_S1) | (fastRead(_S2) << 1);  // получаем код
                poolEnc(state);
            }

            // обработка кнопки (компилятор вырежет блок если не используется)
            // если S2 не указан (кнопка) или указан KEY или выбран вирт. энкодер с кнопкой или кнопка
            if ((_S1 < 252 && _S2 == EB_NO_PIN) || _KEY != EB_NO_PIN || _S1 == VIRT_BTN || _S1 == VIRT_ENCBTN) {
                if (_S1 < 252 && _S2 == EB_NO_PIN) _btnState = !fastRead(_S1);    // обычная кнопка
                if (_KEY != EB_NO_PIN) _btnState = !fastRead(_KEY);               // энк с кнопкой
                if (_S1 == VIRT_BTN) _btnState = s1;                              // вирт кнопка
                if (_S1 == VIRT_ENCBTN) _btnState = key;                          // вирт энк с кнопкой
                poolBtn();           
            }
        }
        _isrFlag = 0;
        return EBState;
    }
    
    // ===================================== CALLBACK =====================================
    // проверить callback, чтобы не дёргать в прерывании
    void checkCallback() {
        if (_EB_MODE) {
            if (*_callback[0] && isTurn()) _callback[0]();              // turn
            if (*_callback[12] && isTurnH()) _callback[12]();           // turnH
            if (EBState > 0 && EBState <= 8 && (*_callback[EBState])) _callback[EBState]();
            if (*_callback[10] && checkFlag(9)) _callback[10]();        // release
            if (*_callback[11] && _EB_readFlag(4)) _callback[11]();     // isHold
            if (checkFlag(6)) {			
                if (*_callback[9]) _callback[9]();				        // clicks
                if (*_callback[13] && clicks == _amount) _callback[13]();
            }
            EBState = 0;
        }
    }
    
    // подключить обработчик
    void attach(eb_callback type, void (*handler)()) {
        _callback[type] = *handler;
    }
    
    // отключить обработчик
    void detach(eb_callback type) {
        _callback[type] = nullptr;
    }
    
    // подключить обработчик на количество кликов (может быть только один!)
    void attachClicks(uint8_t amount, void (*handler)()) {
        _amount = amount;
        _callback[13] = *handler;
    }
    
    // отключить обработчик на количество кликов
    void detachClicks() {
        _callback[13] = nullptr;
    }
    
    // ===================================== STATUS =====================================
    // получить статус
    uint8_t getState() { return EBState; }
    
    // сбросить статус
    void resetState() { EBState = 0; }
    
    // поворот вправо
    bool isRight() { return _EB_MODE ? (_EBSbuf == 1 ? 1 : 0) : checkState(1); }
    bool right() { return isRight(); }
    
    // поворот влево
    bool isLeft() { return _EB_MODE ? (_EBSbuf == 2 ? 1 : 0) : checkState(2); }
    bool left() { return isLeft(); }
    
    // поворот вправо нажатый
    bool isRightH() { return _EB_MODE ? (_EBSbuf == 3 ? 1 : 0) : checkState(3); }
    bool rightH() { return isRightH(); }
    
    // поворот влево нажатый
    bool isLeftH() { return _EB_MODE ? (_EBSbuf == 4 ? 1 : 0) : checkState(4); }
    bool leftH() { return isLeftH(); }
    
    // быстрый поворот
    bool isFast() { return _EB_readFlag(1); }
    bool fast() { return isFast(); }
    
    // энкодер повёрнут
    bool isTurn() { return checkFlag(0); }
    bool turn() { return isTurn(); }
    
    // энкодер повёрнут нажато
    bool isTurnH() { return checkFlag(8); }
    bool turnH() { return isTurnH(); }
    
    // кнопка нажата
    bool isPress() { return checkState(8); }
    bool press() { return isPress(); }
    
    // кнопка отпущена
    bool isRelease() { return checkFlag(9); }
    bool release() { return isRelease(); }
    
    // клик по кнопке
    bool isClick() { return checkState(5); }
    bool click() { return isClick(); }
    
    // кнопка удержана
    bool isHolded() { return checkState(6); }    
    
    // кнопка удержана (грамотный аналог holded =)
    bool isHeld() { return isHolded(); }
    bool held() { return isHolded(); }
    
    // кнопка удерживается
    bool isHold() { return _EB_readFlag(4); }
    bool hold() { return isHold(); }
    
    // режим импульсного удержания
    bool isStep() { return checkState(7); }
    bool step() { return isStep(); }
    
    // статус кнопки
    bool state() { return _btnState; }
    
    // имеются клики
    bool hasClicks(uint8_t numClicks) {
        if (clicks == numClicks && _EB_readFlag(6)) {
            _EB_clrFlag(6);
            return 1;
        }
        return 0;
    }
    
    // имеются клики
    uint8_t hasClicks() {
        if (_EB_readFlag(6)) {
            _EB_clrFlag(6);
            return clicks;
        } return 0;	
    }
    
    // направление последнего поворота, 1 или -1
    int8_t getDir() {
        return _dir;
    }
    
    // счётчик энкодера
    int counter = 0;
    
    // счётчик кликов
    uint8_t clicks = 0;
    
    // ===================================== PRIVATE =====================================
private:
    bool fastRead(const uint8_t pin) {
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
        if (pin < 8) return bitRead(PIND, pin);
        else if (pin < 14) return bitRead(PINB, pin - 8);
        else if (pin < 20) return bitRead(PINC, pin - 14);
#elif defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny13__)
        return bitRead(PINB, pin);
#else
        return digitalRead(pin);
#endif
        return 0;
    }
    
    // ===================================== POOL ENC =====================================
    void poolEnc(uint8_t state) {
        if (_encRST && state == 0b11) {                                 // ресет и энк защёлкнул позицию
            if (_S2 == EB_NO_PIN || _KEY != EB_NO_PIN) {                // энкодер с кнопкой
                if (!_EB_readFlag(4)) {                                 // если кнопка не "удерживается"
                    if (_lastState == 0b10) EBState = (_btnState || _EB_readFlag(7)) ? 3 : 1;
                    else if (_lastState == 0b01) EBState = (_btnState || _EB_readFlag(7)) ? 4 : 2;
                }
            } else {                                                    // просто энкодер
                if (_lastState == 0b10) EBState = 1;
                else if (_lastState == 0b01) EBState = 2;
            }
            
            if (EBState > 0) {                                          // был поворот
                _dir = (EBState & 1) ? 1 : -1;                          // направление
                counter += _dir;                                        // счётчик
                if (_EB_MODE) _EBSbuf = EBState;				        // запомнили для callback	
                if (EBState <= 2) _EB_setFlag(0);			            // флаг поворота для юзера
                else if (EBState <= 4) _EB_setFlag(8);			        // флаг нажатого поворота для юзера
                if (millis() - _debTimer < EB_FAST) _EB_setFlag(1);     // быстрый поворот
                else _EB_clrFlag(1);						            // обычный поворот
            }

            _encRST = 0;
            _debTimer = millis();
        }
        if (state == 0b00) _encRST = 1;
        _lastState = state;
    }
    
    // ===================================== POOL BTN =====================================
    void poolBtn() {
        uint32_t thisMls = millis();
        uint32_t debounce = thisMls - _debTimer;
        if (_btnState) {                                                	// кнопка нажата
            if (!_EB_readFlag(3)) {                                         // и не была нажата ранее
                if (debounce > EB_DEB) {                                   	// и прошел дебаунс
                    _EB_setFlag(3);                                         // флаг кнопка была нажата
                    _debTimer = thisMls;                                    // сброс таймаутов
                    EBState = 8;                                           	// кнопка нажата
                }
                if (debounce > EB_CLICK) {									// кнопка нажата после EB_CLICK
                    clicks = 0;												// сбросить счётчик и флаг кликов
                    flags &= ~0b01100000;
                }
            } else {                                                      	// кнопка уже была нажата
                if (!_EB_readFlag(4)) {                                     // и удержание ещё не зафиксировано
                    if (debounce < (_holdT << 7)) {                         // прошло меньше удержания
                        if (EBState != 0 && EBState != 8) _EB_setFlag(2);   // но энкодер повёрнут! Запомнили
                    } else {                                                // прошло больше времени удержания
                        if (!_EB_readFlag(2)) {                             // и энкодер не повёрнут
                            EBState = 6;                                   	// значит это удержание (сигнал)
                            _EB_setFlag(4);                                 // запомнили что удерживается
                            _debTimer = thisMls;                            // сброс таймаута
                        }
                    }
                } else {                                                    // удержание зафиксировано
                    if (debounce > EB_STEP) {                              	// таймер степа
                        EBState = 7;                                       	// сигналим
                        _debTimer = thisMls;                                // сброс таймаута
                    }
                }
            }
        } else {                                                        	// кнопка не нажата
            if (_EB_readFlag(3)) {                                          // но была нажата
                if (debounce > EB_DEB && !_EB_readFlag(4) && !_EB_readFlag(2)) {	// энкодер не трогали и не удерживали - это клик
                    EBState = 5;
                    clicks++;
                }
                flags &= ~0b00011100;                                       // clear 2 3 4                    
                _debTimer = thisMls;                                        // сброс таймаута
                _EB_setFlag(9);                                             // кнопка отпущена
            } else if (clicks > 0 && debounce > EB_CLICK && !_EB_readFlag(5)) flags |= 0b01100000;	 // флаг на клики
        }
    }
    
    // ===================================== MISC =====================================
    bool checkState(uint8_t val) {
        if (EBState == val) {
            EBState = 0;
            return 1;
        } return 0;
    }
    bool checkFlag(uint8_t val) {
        if (_EB_readFlag(val)) {
            _EB_clrFlag(val);
            return 1;
        } return 0;
    }
    
    uint32_t _debTimer = 0;
    uint8_t _lastState = 0, EBState = 0;
    bool _btnState = 0, _encRST = 0, _isrFlag = 0;
    uint16_t flags = 0;
    uint8_t _holdT = EB_HOLD >> 7;
    int8_t _dir = 0;
    uint8_t _EBSbuf = 0;
    void (*_callback[_EB_MODE ? 14 : 0])() = {};
    uint8_t _amount = 0;

    // flags
    // 0 - enc turn
    // 1 - enc fast
    // 2 - enc был поворот
    // 3 - флаг кнопки
    // 4 - hold
    // 5 - clicks flag
    // 6 - clicks get
    // 7 - enc button hold    
    // 8 - enc turn holded
    // 9 - btn released

    // EBState
    // 0 - idle
    // 1 - right
    // 2 - left
    // 3 - rightH
    // 4 - leftH
    // 5 - click
    // 6 - holded
    // 7 - step
    // 8 - press
};

#endif