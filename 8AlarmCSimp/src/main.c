#include "esp_common.h"
#include "freertos/task.h"
#include "gpio.h"
#include "fsm.h"

#define ETS_GPIO_INTR_ENABLE()  _xt_isr_unmask(1 << ETS_GPIO_INUM)  //ENABLE INTERRUPTS
#define ETS_GPIO_INTR_DISABLE() _xt_isr_mask(1 << ETS_GPIO_INUM)    //DISABLE INTERRUPTS

#define GPIO_BUTTON_A 0 //D3 "conectado al pin de G"
#define GPIO_BUTTON_P 15 //D8 "conectado al pin de 3.3V"
#define GPIO_LED 2 //LED

#define PERIOD_TICK 100/portTICK_RATE_MS

#define dim 3  //Longitud array de la contraseña

int CODE[3] = {1,1,1};

portTickType REBOUND_TICK = 200/portTICK_RATE_MS;
portTickType TIMEOUT = 1000/portTICK_RATE_MS;
portTickType nexTimeout;

int code_index;
int code_inserted[3] = {0,0,0};
volatile int done0, done15;
int is_code;

uint32 user_rf_cal_sector_set(void){
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;
    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

void isr_gpio(fsm_t *self){
  static portTickType xLastISRTick0 = 0;
  uint32 status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);          //READ STATUS OF INTERRUPT
  portTickType now = xTaskGetTickCount ();
  if (status & BIT(0)) {
    if (now > xLastISRTick0) {
      is_code = 1;
      done0 = 1;
      //nexTimeout = xTaskGetTickCount();// + TIMEOUT;
      printf("%s\n"," 0 " );
    }
    xLastISRTick0 = now + REBOUND_TICK;
  }
  if (status & BIT(15)){
    if (now > xLastISRTick0) {
      done15 = 1;
      printf("%s\n"," 15 " );
    }
    xLastISRTick0 = now + REBOUND_TICK;
  }
  GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, status);
}

int pres (fsm_t *self){
  return done15;
}

void blink(){
  GPIO_OUTPUT_SET(GPIO_LED, 0); //ON
  vTaskDelay(200/portTICK_RATE_MS);
  GPIO_OUTPUT_SET(GPIO_LED, 1); //OFF
}

int mirar_flags (fsm_t *self){
  if(code_index >3)
    return 0;
  if(code_inserted[code_index]>10)
    return 0;
  //printf("%s\n","miro" );
  return done0;
}

void update_code (fsm_t *self){
  done0 = 0;
  code_inserted[code_index]++;
  nexTimeout = xTaskGetTickCount() + TIMEOUT;
  for(int j = 0; j < 3; j++) {
    printf("%d ", code_inserted[j]);
  }
  printf("\n");
  printf("%d\n",code_index);
}

int  timeout (fsm_t *self){
  portTickType now = xTaskGetTickCount();
  if((nexTimeout <= now) && is_code){
    printf("%s\n","timeout" );
    blink();
    return 1;
  }
  return 0;
}

void next_index (fsm_t *self){
  code_index++;
  nexTimeout = 0xFFFFFFFF;
  printf("%s\n","next" );
}

//Función para comprobar que dos arrays son iguales
int passIguales (int a [], int b []){

    int i;
    int esIgual = 0;
    for(i=0; i<dim; ++i){
        if(a[i]!=b[i]) {++esIgual;}
    }
    if(esIgual==0) {return 1;}
    else {return 0;}
}

//Función para limpiar las passwords que metemos
void limpiarPassIn(){
  int i;
  for (i = 0; i<dim; ++i){
    code_inserted[i] = 0;
  }
}

int code_ok (fsm_t *self){
  if((passIguales(code_inserted,CODE))&&code_index>2){//(code_inserted[0]==CODE[0])&&(code_inserted[1]==CODE[1])&&(code_inserted[2]==CODE[2])){
    printf("%s\n","code ok" );
    return 1;
  }
  return 0;
}

int code_fail (fsm_t *self){
  if((!passIguales(code_inserted,CODE))&&code_index>2){//(code_inserted!=CODE)&&(code_inserted[0]!=0)&&(code_inserted[1]!=0)&&(code_inserted[2]!=0)){
    printf("%s\n","code fail" );
    return 1;
  }
  return 0;
}

void clean_flags (fsm_t *self){
  limpiarPassIn();
  led_off();
  code_index = 0;
  is_code = 0;
  done0 = 0;
  done15 = 0;
  printf("%s\n","clean" );
}

void led_on (fsm_t *self){
  printf("%s\n","on" );
  GPIO_OUTPUT_SET(GPIO_LED, 0); //ON
}

void led_off (){
  printf("%s\n","off" );
  GPIO_OUTPUT_SET(GPIO_LED, 1); //OFF
}

enum fsm_state {
    ALARM_ON,
    ALARM_OFF
};

static fsm_trans_t interruptor[] = {
  {ALARM_OFF, mirar_flags, ALARM_OFF, update_code},
  {ALARM_OFF, timeout, ALARM_OFF, next_index},
  {ALARM_OFF, code_fail, ALARM_OFF, clean_flags},

  {ALARM_OFF, code_ok, ALARM_ON, clean_flags},

  {ALARM_ON, mirar_flags, ALARM_ON, update_code},
  {ALARM_ON, timeout, ALARM_ON, next_index},
  {ALARM_ON, code_fail, ALARM_ON, clean_flags},
  {ALARM_ON, code_ok, ALARM_OFF, clean_flags},
  {ALARM_ON, pres, ALARM_ON, led_on},

  {-1, NULL, -1, NULL}
};

void inter(void* ignore){
  fsm_t* fsm = fsm_new(interruptor);
  //clean_flags(fsm);
  portTickType xLastWakeTime;
  PIN_FUNC_SELECT(GPIO_PIN_REG_15, FUNC_GPIO15);

  gpio_intr_handler_register((void*)isr_gpio, NULL);
  gpio_pin_intr_state_set(0, GPIO_PIN_INTR_NEGEDGE);
  gpio_pin_intr_state_set(15, GPIO_PIN_INTR_POSEDGE);
  ETS_GPIO_INTR_ENABLE();

  done0 = 0;
  done15 = 0;
  is_code = 0;
  limpiarPassIn();
  code_index = 2;

  xLastWakeTime = xTaskGetTickCount ();
  while(true) {
    fsm_fire(fsm);
    vTaskDelayUntil(&xLastWakeTime, PERIOD_TICK);
  }
}

void user_init(void){
    xTaskCreate(&inter, "startup", 2048, NULL, 1, NULL);
}
