#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "led_matrix.pio.h"
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define ADC_PIN 28 // GPIO para o voltímetro
#define OUT_PIN 7 // GPIO matriz de leds

//código RGB das cores de faixa dos resitores
const uint8_t codigo_cores[10][3] = {
  {0, 0, 0},    //0 -> preto
  {3, 1, 0},    //1 -> marrom
  {64, 0, 0},   //2 -> vermelho
  {64, 6, 0},   //3 -> laranja
  {32, 16, 0},  //4 -> amarelo
  {0, 32, 0},   //5 -> verde
  {0, 0, 32},   //6 -> azul
  {16, 0, 16},  //7 -> violeta
  {1, 1, 1},    //8 -> cinza
  {16, 16, 16}  //9 -> branco
};

//valores da série E24 de resistores
const uint8_t serie_E24[24] = {
  10, 11, 12, 13, 15, 16, 18, 20,
  22, 24, 27, 30, 33, 36, 39, 43,
  47, 51, 56, 62, 68, 75, 82, 91
};

//array de nomes das cores
const char *cores_resistor[10] = {
  "Preto",    // 0
  "Marrom",   // 1
  "Verm.", // 2
  "Laran.",  // 3
  "Amare.",  // 4
  "Verde",    // 5
  "Azul",     // 6
  "Viole.",  // 7
  "Cinza",    // 8
  "Branco"    // 9
};

//matriz para enviar cores para matriz de leds 5x5 
uint8_t matriz_resistor_pio[25][3] = {
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0},
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}
};

int R_conhecido = 5100;   // Resistor de 5,1k ohm
uint32_t R_x = 0;           // Resistor desconhecido
int ADC_RESOLUTION = 4095; // Resolução do ADC (12 bits)

//rotina para definição de cores de led na matriz 5x5
uint32_t matrix_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return (g << 24) | (r << 16) | (b << 8);
}

//rotina para desenhar faixa de cores do resistor na matrix de leds - ws2812b
void desenho_pio(PIO pio, uint sm, uint8_t *digito){
  //salva código de cores das faixas de resistencia na matriz de código de cores
  for (int16_t i = 0; i < 25; i++){
    if(i == 0 || i == 9 || i == 10 || i == 19 || i == 20){
      matriz_resistor_pio[i][0] = codigo_cores[digito[0]][0];
      matriz_resistor_pio[i][1] = codigo_cores[digito[0]][1];
      matriz_resistor_pio[i][2] = codigo_cores[digito[0]][2];
    }
    if(i%5 == 2){
      matriz_resistor_pio[i][0] = codigo_cores[digito[1]][0];
      matriz_resistor_pio[i][1] = codigo_cores[digito[1]][1];
      matriz_resistor_pio[i][2] = codigo_cores[digito[1]][2];
    }
    if(i == 4 || i == 5 || i == 14 || i == 15 || i == 24){
      matriz_resistor_pio[i][0] = codigo_cores[digito[2]][0];
      matriz_resistor_pio[i][1] = codigo_cores[digito[2]][1];
      matriz_resistor_pio[i][2] = codigo_cores[digito[2]][2];
    }
  }

  //envia cor para a matriz 5x5
  for (int16_t i = 0; i < 25; i++) {
    pio_sm_put_blocking(pio, sm, matrix_rgb(matriz_resistor_pio[24-i][0], matriz_resistor_pio[24-i][1], matriz_resistor_pio[24-i][2]));
  }
}

//função para encontrar resistência E24 mais próxima
uint32_t arredonda_E24(uint32_t resistencia) {
  uint32_t valor_norm = resistencia;
  uint8_t multiplicador = 0;

  //normaliza valor entre 10 e 100
  while (valor_norm >= 100) {
      valor_norm /= 10;
      multiplicador++;
  }

  //procura o valor mais próximo na série E24
  uint8_t melhor_valor = serie_E24[0];
  uint32_t menor_erro = abs(valor_norm - melhor_valor);

  for (int i = 1; i < 24; i++) {
      uint32_t erro = abs(valor_norm - serie_E24[i]);
      if (erro < menor_erro) {
          melhor_valor = serie_E24[i];
          menor_erro = erro;
      }
  }

  //valor comercial de resistência
  uint32_t resistencia_comercial = melhor_valor * pow(10, multiplicador);

  return resistencia_comercial;
}

//função para encontrar os 2 dígitos mais significativos da resistência e o multiplicador
void digitos_resistor(uint32_t resistencia, uint8_t *digitos) {
  //calcula a quantidade de dígitos da resistência
  uint32_t temp = resistencia;
  uint8_t n_digitos = 0;
  while (temp > 0) {
      temp = temp/10;
      n_digitos++;
  }

  //caso apenas 1 digito
  if (n_digitos == 1) {
      digitos[0] = 0;
      digitos[1] = resistencia;
      digitos[2] = 0;
      return;
  }

  //normaliza para encontrar os dois dígitos mais significativos
  temp = resistencia;
  while (temp >= 100) {
      temp = temp/10;
  }

  digitos[0] = temp / 10; //primeiro dígito
  digitos[1] = temp % 10;  //segundo dígito
  digitos[2] = n_digitos - 2; //multiplicador
}

int main()
{
  // I2C Initialisation. Using it at 400Khz.
  i2c_init(I2C_PORT, 400 * 1000);

  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_pull_up(I2C_SDA);                                        // Pull up the data line
  gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
  ssd1306_t ssd;                                                // Inicializa a estrutura do display
  ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
  ssd1306_config(&ssd);                                         // Configura o display
  ssd1306_send_data(&ssd);                                      // Envia os dados para o display

  // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);

  adc_init();
  adc_gpio_init(ADC_PIN); // GPIO 28 como entrada analógica

  //inicialização matriz de leds
  PIO pio = pio0; //seleciona a pio0
  uint offset = pio_add_program(pio, &pio_matrix_program);
  uint sm = pio_claim_unused_sm(pio, true);
  pio_matrix_program_init(pio, sm, offset, OUT_PIN);

  char str_x[18], str_y[18], str_z[18], str_w[18]; // Buffer para armazenar a string

  // Seleciona o ADC para eixo X. O pino 28 como entrada analógica
  adc_select_input(2);

  uint32_t soma;
  float media;
  uint8_t digitos[3] = {0,0,0};

  while (true)
  {
    soma = 0;

    for (int i = 0; i < 500; i++)
    {
      soma += adc_read();
      sleep_ms(1);
    }

    media = soma / 500.0f;
    
    // Fórmula simplificada: R_x = R_conhecido * ADC_encontrado /(ADC_RESOLUTION - adc_encontrado)
    R_x = (R_conhecido * media) / (ADC_RESOLUTION - media);
    R_x = arredonda_E24(R_x);

    //encontra dígitos do resistor
    digitos_resistor(R_x, digitos);

    //desenha faixas na matriz 5x5
    desenho_pio(pio, sm, digitos);

    //cria string para mostrar no display oled
    sprintf(str_x, "Resisten. %i", R_x);
    sprintf(str_y, "Faixa 1: %s", cores_resistor[digitos[0]]);
    sprintf(str_z, "Faixa 2: %s", cores_resistor[digitos[1]]);
    sprintf(str_w, "Faixa 3: %s", cores_resistor[digitos[2]]);

    //imprime informações no display
    ssd1306_fill(&ssd, false);                          // Limpa o display
    ssd1306_draw_string(&ssd, str_x, 2, 2);          // Desenha string
    ssd1306_draw_string(&ssd, str_y, 2, 11);    // Desenha string
    ssd1306_draw_string(&ssd, str_z, 2, 20);    // Desenha string
    ssd1306_draw_string(&ssd, str_w, 2, 29);    // Desenha string
    ssd1306_send_data(&ssd);                           // Atualiza o display

    sleep_ms(700);
  }
}