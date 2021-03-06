/**
 * @file
 * @author     Scott Price <prices@hugllc.com>
 * @copyright  © 2018 Hunt Utilities Group, LLC
 * @brief      The main class for HUGnetCANLib.
 * @details
 */

#include "samc21_adc.h"
#include "Arduino.h"

#ifndef ADC_INPUTCTRL_MUXNEG_GND
#define ADC_INPUTCTRL_MUXNEG_GND (0x18ul << ADC_INPUTCTRL_MUXNEG_Pos)
#endif

void *samc21_adc0_callback_ptr;           //!< Extra pointer for _callback
samc21_adc_callback samc21_adc0_callback; //!< The callback function
void *samc21_adc1_callback_ptr;           //!< Extra pointer for _callback
samc21_adc_callback samc21_adc1_callback; //!< The callback function


SAMC21_ADC *samc21_adc_obj[3] = {NULL, NULL, NULL};

const uint8_t ADC0_pins[]  = {2, 3, 8, 9, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t ADC0_group[] = {0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
const uint8_t ADC1_pins[]  = {0, 1, 2, 3, 8, 9, 4, 5, 6, 7, 8, 9};
const uint8_t ADC1_group[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0};

SAMC21_ADC::SAMC21_ADC(Adc *Conv)
    : _adc(Conv), _count(0), _val(INT32_MIN), _window(NULL), _new(0), _int(0), _begun(false)
{
};

uint8_t SAMC21_ADC::begin(samc21_adc_ref vref, uint8_t clock_prescaler)
{
    uint32_t biasrefbuf = 0;
    uint32_t biascomp = 0;
    if ((_adc != NULL) && (_adc->CTRLA.bit.ENABLE == 0)) {
        if (_adc == ADC0) {
            if (samc21_adc_obj[0] != NULL) {
                return 2;
            }
            MCLK->APBCMASK.reg |= MCLK_APBCMASK_ADC0;
            biasrefbuf = (*((uint32_t *) ADC0_FUSES_BIASREFBUF_ADDR) & ADC0_FUSES_BIASREFBUF_Msk) >> ADC0_FUSES_BIASREFBUF_Pos;
            biascomp   = (*((uint32_t *) ADC0_FUSES_BIASCOMP_ADDR) & ADC0_FUSES_BIASCOMP_Msk) >> ADC0_FUSES_BIASCOMP_Pos;
            samc21_adc_obj[0] = this;
        } else if (_adc == ADC1) {
            if (samc21_adc_obj[1] != NULL) {
                return 2;
            }
            MCLK->APBCMASK.reg |= MCLK_APBCMASK_ADC1;
            biasrefbuf = (*((uint32_t *) ADC1_FUSES_BIASREFBUF_ADDR) & ADC1_FUSES_BIASREFBUF_Msk) >> ADC1_FUSES_BIASREFBUF_Pos;
            biascomp   = (*((uint32_t *) ADC1_FUSES_BIASCOMP_ADDR) & ADC1_FUSES_BIASCOMP_Msk) >> ADC1_FUSES_BIASCOMP_Pos;
            samc21_adc_obj[1] = this;
        }
        _sync_wait();
        _adc->CTRLA.bit.SWRST;
        _sync_wait();
        _adc->CALIB.reg = ADC_CALIB_BIASREFBUF(biasrefbuf) | ADC_CALIB_BIASCOMP(biascomp);
        _adc->CTRLB.reg = (clock_prescaler & ADC_CTRLB_PRESCALER_Msk);
        _adc->CTRLC.reg = ADC_CTRLC_RESSEL_12BIT | ADC_CTRLC_R2R;
        _sync_wait();
        samplingTime(0);
        gain(DEFAULT_GAIN);
        offset(DEFAULT_OFFSET);
        ref(vref);
        mux();
        average();
        _sync_wait();
        _begun = true;
    } else {
        return 1;
    }
    return 0;
};
SAMC21_ADC::~SAMC21_ADC(void)
{
    if (_adc == ADC0) {
        samc21_adc_obj[0] = NULL;
    } else if (_adc == ADC1) {
        samc21_adc_obj[1] = NULL;
    } else {
        samc21_adc_obj[2] = NULL;
    }
}

uint8_t SAMC21_ADC::end(void)
{
    if (_started()) {
        if (_adc != NULL) {
            _disable_irq();
            _sync_wait();
            _adc->CTRLA.bit.SWRST;
            _sync_wait();
            return 1;
        }
    }
    return 0;
}

bool SAMC21_ADC::average(samc21_adc_avg_samples samples, samc21_adc_avg_divisor div)
{
    if (_started()) {
        if (_adc != NULL) {
            if (samples != SAMC21_ADC_AVGSAMPLES_1) {
                _sync_wait();
                _adc->AVGCTRL.reg = samples | div;
                _adc->CTRLC.bit.RESSEL = 1; // 16 bit result
            } else {
                _sync_wait();
                _adc->AVGCTRL.reg = 0;
                _adc->CTRLC.bit.RESSEL = 0; // 12 bit result
            }
            return true;
        }
    }
    return false;
}

bool SAMC21_ADC::ref(samc21_adc_ref vref)
{
    bool set = false;
    if (_adc != NULL) {
        if (_enabled()) {
            _disable();
        }
        switch (vref) {
            case SAMC21_ADC_REF_1024:
            case SAMC21_ADC_REF_2048:
            case SAMC21_ADC_REF_4096:
                SUPC->VREF.reg &= ~SUPC_VREF_SEL_Msk;
                SUPC->VREF.reg |= SUPC_VREF_SEL(vref - REF_OFFSET);
                _adc->REFCTRL.reg &= ~ADC_REFCTRL_REFSEL_Msk;
                set = true;
                break;
            default:
                _adc->REFCTRL.bit.REFSEL = vref;
                set = true;
                break;
        }
    }
    return set;
};
bool SAMC21_ADC::mux(samc21_adc_mux_pos pos, samc21_adc_mux_neg neg)
{
    if (_started()) {
        if (_adc != NULL) {
            _sync_wait();
            _mux(pos, neg);
            pins(pos, neg);
            return true;
        }
    }
    return false;
}
bool SAMC21_ADC::pins(samc21_adc_mux_pos pos, samc21_adc_mux_neg neg)
{
    if (_adc != NULL) {
        uint8_t pgroup = 0xFF, ngroup = 0xFF;
        uint8_t ppin = 0xFF, npin = 0xFF;
        uint8_t mux;
        if (_adc == ADC0) {
            if (pos < sizeof(ADC0_pins)) {
                ppin   = ADC0_pins[pos];
                pgroup = ADC0_group[pos];
            }
            if (neg < sizeof(ADC0_pins)) {
                npin   = ADC0_pins[neg];
                ngroup = ADC0_group[neg];
                diff(true);
            } else {
                diff(false);
            }
        } else if (_adc == ADC1) {
            if (pos < sizeof(ADC1_pins)) {
                ppin   = ADC1_pins[pos];
                pgroup = ADC1_group[pos];
            }
            if (neg < sizeof(ADC1_pins)) {
                npin   = ADC1_pins[neg];
                ngroup = ADC1_group[neg];
                diff(true);
            } else {
                diff(false);
            }
        }
        if (ppin != 0xFF) {
            PORT->Group[pgroup].DIRCLR.reg = 1 << ppin;
            PORT->Group[pgroup].PINCFG[ppin].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN;
            mux = PORT->Group[pgroup].PMUX[ppin / 2].reg;
            if ((ppin & 1) == 0) {
                // Even pin
                mux = (mux & ~PORT_PMUX_PMUXE_Msk) | PORT_PMUX_PMUXE(1); // B
            } else {
                // Odd pin
                mux = (mux & ~PORT_PMUX_PMUXO_Msk) | PORT_PMUX_PMUXO(1); // B
            }
            PORT->Group[pgroup].PMUX[ppin / 2].reg = mux;
        }
        if (npin != 0xFF) {
            PORT->Group[ngroup].DIRCLR.reg = 1 << npin;
            PORT->Group[ngroup].PINCFG[npin].reg = PORT_PINCFG_INEN | PORT_PINCFG_PMUXEN;
            mux = PORT->Group[ngroup].PMUX[npin / 2].reg;
            if ((ppin & 1) == 0) {
                // Even pin
                mux = (mux & ~PORT_PMUX_PMUXE_Msk) | PORT_PMUX_PMUXE(1); // B
            } else {
                // Odd pin
                mux = (mux & ~PORT_PMUX_PMUXO_Msk) | PORT_PMUX_PMUXO(1); // B
            }
            PORT->Group[ngroup].PMUX[npin / 2].reg = mux;
        }
        return true;
    }
    return false;
}

int32_t SAMC21_ADC::read(samc21_adc_mux_pos pos, samc21_adc_mux_neg neg)
{
    int32_t val = INT32_MIN;
    if (_started()) {
        if (_adc != NULL) {
            mux(pos, neg);
            _start();
            _start();
            // Clear the Data Ready flag
            _adc->INTFLAG.reg = ADC_INTFLAG_RESRDY;
            // Start conversion again, since The first conversion after the reference is changed must not be used.
            _sync_wait();
            _start();
            if (_wait()) {
                val = value();
            }
        }
    }
    return val;
}
bool SAMC21_ADC::freerun(samc21_adc_mux_pos pos, samc21_adc_mux_neg neg)
{
    if (_started()) {
        if (_adc != NULL) {
            mux(pos, neg);
            _enable_irq();
            _sync_wait();
            _adc->CTRLC.bit.FREERUN = 1;
            _sync_wait();
            return _start();
        }
    }
    return false;
}

bool SAMC21_ADC::run(void)
{
    if (_started()) {
        return _start();
    }
    return false;
}

int32_t SAMC21_ADC::value(void)
{
    _checkNew();
    return _val;
}

/**
 * The handler for ADC0
 *
 * @return void
 */
void ADC0_Handler(void)
{
    int32_t read;
#ifdef ADC0_AVERAGE_BITS
    volatile static int32_t  acc = 0;
    volatile static uint16_t cnt = 0;
#endif
    uint8_t flags = ADC0->INTFLAG.reg;
    if (samc21_adc_obj[0] != NULL) {
        if ((flags & ADC_INTFLAG_RESRDY) == ADC_INTFLAG_RESRDY) {
#ifdef ADC0_AVERAGE_BITS
            acc += (int16_t)ADC0->RESULT.reg;
            cnt++;
            if (cnt >= (1 << ADC0_AVERAGE_BITS)) {
                read = (acc >> (ADC0_AVERAGE_BITS - 4));
                if (samc21_adc0_callback != NULL) {
                    samc21_adc0_callback(samc21_adc_obj[0], read, (uint8_t)ADC0->SEQSTATUS.bit.SEQSTATE, samc21_adc0_callback_ptr);
                } else {
                    samc21_adc_obj[0]->addNew(read, (uint8_t)ADC0->SEQSTATUS.bit.SEQSTATE);
                }
                acc = 0;
                cnt = 0;
            }
#else
            if (samc21_adc_obj[0]->diff()) {
                read = (int16_t)ADC0->RESULT.reg;
            } else {
                read = (uint16_t)ADC0->RESULT.reg;
            }
            if (samc21_adc0_callback != NULL) {
                samc21_adc0_callback(samc21_adc_obj[0], read, (uint8_t)ADC0->SEQSTATUS.bit.SEQSTATE, samc21_adc0_callback_ptr);
            } else {
                samc21_adc_obj[0]->addNew(ADC0->RESULT.reg, (uint8_t)ADC0->SEQSTATUS.bit.SEQSTATE);
            }
#endif
        }
        if ((flags & ADC_INTFLAG_WINMON) == ADC_INTFLAG_WINMON) {
            samc21_adc_obj[0]->window();
        }
    }
    ADC0->INTFLAG.reg = flags;
}


/**
 * The handler for ADC1
 *
 * @return void
 */
void ADC1_Handler(void)
{
    int32_t read;
#ifdef ADC1_AVERAGE_BITS
    volatile static int32_t  acc = 0;
    volatile static uint16_t cnt = 0;
#endif
    uint8_t flags = ADC1->INTFLAG.reg;
    if (samc21_adc_obj[1] != NULL) {
        if ((flags & ADC_INTFLAG_RESRDY) == ADC_INTFLAG_RESRDY) {
#ifdef ADC1_AVERAGE_BITS
            acc += (int16_t)ADC1->RESULT.reg;
            cnt++;
            if (cnt >= (1 << ADC1_AVERAGE_BITS)) {
                read = (acc >> (ADC1_AVERAGE_BITS - 4));
                if (samc21_adc1_callback != NULL) {
                    samc21_adc1_callback(samc21_adc_obj[1], read, (uint8_t)ADC1->SEQSTATUS.bit.SEQSTATE, samc21_adc1_callback_ptr);
                } else {
                    samc21_adc_obj[1]->addNew(read, (uint8_t)ADC1->SEQSTATUS.bit.SEQSTATE);
                }
                acc = 0;
                cnt = 0;
            }
#else
            if (samc21_adc_obj[1]->diff()) {
                read = (int16_t)ADC1->RESULT.reg;
            } else {
                read = (uint16_t)ADC1->RESULT.reg;
            }
            if (samc21_adc1_callback != NULL) {
                samc21_adc1_callback(samc21_adc_obj[1], read, (uint8_t)ADC1->SEQSTATUS.bit.SEQSTATE, samc21_adc1_callback_ptr);
            } else {
                samc21_adc_obj[1]->addNew(ADC1->RESULT.reg, (uint8_t)ADC1->SEQSTATUS.bit.SEQSTATE);
            }
#endif
        }
        if ((flags & ADC_INTFLAG_WINMON) == ADC_INTFLAG_WINMON) {
            samc21_adc_obj[1]->window();
        }
    }
    ADC1->INTFLAG.reg = flags;
}

/**
 * The handler for SDADC
 *
 * @return void
 */
void SDADC_Handler(void)
{
}
