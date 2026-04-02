#include <Arduino.h>
#include <Wire.h>
#include "audio_i2c.hpp"
#include "ES8388_V1.h"
//#include <audio_hal.h>
//#include <audio_volume.h>

#define SYS_I2C_SDA_PIN 21
#define SYS_I2C_SCL_PIN 22

AudioI2c device;


// ============================================================
// RGB COLORS FOR M5 AUDIO MODULE
// ============================================================
static const uint32_t color[] = {
    0xFF0000, 0xFF0000, 0xFF0000,
    0x00FF00, 0xFFFF00, 0xFFFFFF,
    0xFFFFFF, 0xFFFFFF, 0xFFFFFF
};

static void scanI2C()
{
    Serial.println("\n========== I2C SCAN ==========");
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("Found I2C device at 0x%02X\n", address);
        }
    }
    Serial.println("========== END SCAN ==========\n");
}


static void setupM5AudioModule()
{
    Serial.println("Init M5 Audio helper MCU...");

    device.begin(&Wire, SYS_I2C_SDA_PIN, SYS_I2C_SCL_PIN);
    device.setHPMode(AUDIO_HPMODE_AMERICAN);////(AUDIO_HPMODE_NATIONAL);//
    device.setMICStatus(AUDIO_MIC_CLOSE);//(AUDIO_MIC_OPEN);//
    device.setRGBBrightness(100);

    Serial.printf("getHPMode    : %d\n", device.getHPMode());
    Serial.printf("getMICStatus : %d\n", device.getMICStatus());

    for (int i = 0; i < 3; i++) {
        device.setRGBLED(i, color[i + 3]);
        Serial.printf("Set RGB[%d]  : %06X\n", i, (unsigned int)color[i + 3]);
        Serial.printf("Read RGB[%d] : %06X\n", i, (unsigned int)device.getRGBLED(i));
    }
}


audio_hal_adc_input_t adc_input = AUDIO_HAL_ADC_INPUT_LINE1; // ADC_INPUT_LINPUT2_RINPUT2;
audio_hal_dac_output_t dac_output = AUDIO_HAL_DAC_OUTPUT_ALL;
audio_hal_iface_samples_t samples = AUDIO_HAL_48K_SAMPLES;
audio_hal_codec_config_t codec_cfg = {
    .adc_input = adc_input ,
    .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
    .i2s_iface = {
        .mode = AUDIO_HAL_MODE_MASTER,
        .fmt = AUDIO_HAL_I2S_NORMAL,
        .samples = samples,
    }
};

es_adc_input_t adc_input_a =ADC_INPUT_LINPUT1_RINPUT1;//// ADC_INPUT_LINPUT2_RINPUT2;//

es_module_t module = ES_MODULE_ADC;

es_sclk_div_t sclk_div_enum = MCLK_DIV_11;
es_lclk_div_t lclk_div_enum = LCLK_DIV_256;
es_i2s_clock_t i2s_sclk_div = {
    .sclk_div =sclk_div_enum,
    .lclk_div = lclk_div_enum
};



void setup()
{
    Serial.begin(115200);
    delay(1000);

    Wire.begin(SYS_I2C_SDA_PIN, SYS_I2C_SCL_PIN);
    Wire.setClock(100000);

    scanI2C();
      
    device.begin(&Wire, SYS_I2C_SDA_PIN, SYS_I2C_SCL_PIN);
   // device.setHPMode(AUDIO_HPMODE_NATIONAL);//(AUDIO_HPMODE_AMERICAN);
    device.setMICStatus(AUDIO_MIC_CLOSE);//(AUDIO_MIC_OPEN);////(AUDIO_MIC_OPEN);
    device.setRGBBrightness(100);
     for (int i = 0; i <= 2; i++) {
        device.setRGBLED(i, 0x00ff00);
    }
    delay(10);


    if(!es8388_deinit()) {
        Serial.println("ES8388 deinit failed");
    } else {
        Serial.println("ES8388 deinit OK");
    }

   if(!es8388_init()) {
        Serial.println("ES8388 init failed");
    } else {
        Serial.println("ES8388 init OK");
    }
   // if(!es8388_config_adc_input( adc_input_a )) {
     //   Serial.println("ES8388 config adc input failed");
   // } else {
       // Serial.println("ES8388 config adc input OK");
   // }

//  if(! es8388_i2s_config_clock( i2s_sclk_div)) {
       // Serial.println("ES8388 i2s config clock failed");
   // } else {
       // Serial.println("ES8388 i2s config clock OK");
  //  }

    // Force NORMAL I2S format on ADC
   // if (!es8388_config_fmt(ES_MODULE_ADC, ES_I2S_NORMAL)) {
       // Serial.println("ES8388 config fmt failed");
       // while (1) delay(100);
   // } else {
        //Serial.println("ES8388 config fmt OK");
   // }
    
    // Force 16-bit ADC samples
 //  if (!es8388_set_bits_per_sample(ES_MODULE_ADC, BIT_LENGTH_16BITS)) {
       // Serial.println("ES8388 set bits failed");
       // while (1) delay(100);
   //} else {
        //Serial.println("ES8388 set bits OK");
   // }


  //es8388_set_adc_dac_volume(ES_MODULE_ADC, 0, 0);       // 0db
        //delay(50);
    //if(!es8388_start(module)) {
       // Serial.println("ES8388 start failed");
   // } else {
      //  Serial.println("ES8388 start OK");
    //}
    


}
    

    /*
    void setup()
{
    Serial.begin(115200);
    delay(1000);

    Wire.begin(SYS_I2C_SDA_PIN, SYS_I2C_SCL_PIN);
    Wire.setClock(100000);

    scanI2C();

    device.begin(&Wire, SYS_I2C_SDA_PIN, SYS_I2C_SCL_PIN);
    device.setHPMode(AUDIO_HPMODE_NATIONAL);
    device.setMICStatus(AUDIO_MIC_OPEN);
    device.setRGBBrightness(100);

    for (int i = 0; i <= 2; i++) {
        device.setRGBLED(i, 0x00ff00);
    }
    delay(10);

    if (!es8388_deinit()) {
        Serial.println("ES8388 deinit failed");
    } else {
        Serial.println("ES8388 deinit OK");
    }

    delay(20);

    if (!es8388_init()) {
        Serial.println("ES8388 init failed");
        while (1) delay(100);
    } else {
        Serial.println("ES8388 init OK");
    }

    if (!es8388_config_adc_input(adc_input)) {
        Serial.println("ES8388 config adc input failed");
        while (1) delay(100);
    } else {
        Serial.println("ES8388 config adc input OK");
    }

    // Force NORMAL I2S format on ADC
   // if (!es8388_config_fmt(ES_MODULE_ADC, ES_I2S_NORMAL)) {
      //  Serial.println("ES8388 config fmt failed");
      //  while (1) delay(100);
   // } else {
       // Serial.println("ES8388 config fmt OK");
  //  }

    // Force 16-bit ADC samples
   // if (!es8388_set_bits_per_sample(ES_MODULE_ADC, BIT_LENGTH_32BITS)) {
      //  Serial.println("ES8388 set bits failed");
      //  while (1) delay(100);
   //} else {
      //  Serial.println("ES8388 set bits OK");
   // }

    // Keep codec in master mode and set LRCK divider
  //  if (!es8388_i2s_config_clock({ .sclk_div = MCLK_DIV_4, .lclk_div = LCLK_DIV_256 })) {
       // Serial.println("ES8388 i2s config clock failed");
      //  while (1) delay(100);
   // } else {
       // Serial.println("ES8388 i2s config clock OK");
    //}

    if (!es8388_start(module)) {
        Serial.println("ES8388 start failed");
        while (1) delay(100);
    } else {
        Serial.println("ES8388 start OK");
    }

    // ADC digital volume = 0 dB
    es8388_set_adc_dac_volume(ES_MODULE_ADC, 0, 0);

    delay(50);
    es8388_read_all();
}
*/
void loop()
{
}