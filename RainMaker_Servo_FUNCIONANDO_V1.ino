#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include "AppInsights.h"
#include <ESP32Servo.h>
#include "time.h"

// --- DISPLAY ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Prevenção de Brownout
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURAÇÕES ---
#define DEFAULT_POWER_MODE false 
const char *service_name = "PROV_aromatizador";
const char *pop = "12345678"; 

// Mecânica
#define PINO_SERVO 18
#define ANGULO_REPOUSO 0   
int angulo_aperto = 95; 
#define TEMPO_APERTO   1000 
int qtd_disparos = 1; 

// Controles
unsigned long momento_liberacao = 0; 
#define TEMPO_COOLDOWN 1000 
bool dnd_ativo = false;    
float dnd_inicio = 22.0;   
float dnd_fim = 7.0;       

// Trava de segurança do Display
bool sessao_provisionamento_encerrada = false; 

Servo meuServo;

// GPIO
#if CONFIG_IDF_TARGET_ESP32C3
static int gpio_0 = 9;
static int gpio_switch = 7;
#else
static int gpio_0 = 0;
static int gpio_switch = 2;
#endif

static Switch *my_switch = NULL;

// --- FUNÇÃO DE DESENHO ---
void atualizarTela(String titulo, String rodape, bool limpar = true) {
    // 1. GUARDA DE SEGURANÇA (CRÍTICA)
    // Se o provisionamento não acabou e não é a tela de Setup, NÃO DESENHA NADA.
    // Isso evita que o display trave a conexão do Wi-Fi/Bluetooth.
    if (!sessao_provisionamento_encerrada && titulo != "SETUP") return;

    if (limpar) display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // --- CABEÇALHO FIXO (STATUS) ---
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("NEXUS"); 
    
    // Mostra um pequeno indicador de Wi-Fi no canto
    display.setCursor(105, 0); 
    if (WiFi.status() == WL_CONNECTED) display.print("(W)");
    else display.print("(!)");
    
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // --- LÓGICA DE PERSONALIZAÇÃO (DASHBOARD) ---
    
    // CENÁRIO 1: O loop mandou a hora no título (Ex: "14:30")
    // Desenha o relógio GRANDE no meio
    if (titulo.indexOf(':') != -1 && titulo.length() <= 5) {
        display.setTextSize(3); // Fonte Grande (Dashboard)
        
        // Centraliza o relógio (Cálculo: 128 largura total - largura do texto)
        // Cada caractere tam 3 tem aprox 18 pixels de largura
        int x = (128 - (titulo.length() * 18)) / 2; 
        if(x < 0) x = 0;
        
        display.setCursor(x, 25);
        display.print(titulo);
        
        // Coloca a info extra (Ex: "Last: 12:00") bem pequena no rodapé
        display.setTextSize(1);
        display.setCursor(0, 55);
        display.print(rodape);
    }
    
    // CENÁRIO 2: O loop mandou "ONLINE" e a hora está no rodapé
    else if (titulo == "ONLINE") {
        display.setTextSize(3); // A hora do rodapé vira destaque
        
        // Centraliza
        int x = (128 - (rodape.length() * 18)) / 2;
        if(x < 0) x = 0;
        
        display.setCursor(x, 25);
        display.print(rodape); 
        
        display.setTextSize(1);
        display.setCursor(40, 55); // Centraliza "Standby"
        display.print("Standby");
    }
    
    // CENÁRIO 3: Avisos normais (SPRAY!, COOLDOWN, BLOQUEADO)
    else {
        display.setTextSize(2); // Texto Médio para alertas
        display.setCursor(0, 25);
        display.println(titulo);

        display.setTextSize(1);
        display.setCursor(0, 55);
        display.println(rodape);
    }

    display.display();
}
// Tela Estática de Boot
void mostrarTelaPareamento() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    display.setTextSize(1);
    display.setCursor(0, 0); 
    display.println("MODO PAREAMENTO");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    display.setCursor(0, 20);
    display.println("Abra App RainMaker");
    display.setCursor(0, 32);
    display.println("Use 'Sem QR Code'");

    display.setTextSize(2);
    display.setCursor(0, 48);
    display.print("POP:"); display.println(pop); 

    display.display();
}

String getHoraAtual() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return "---";
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
    return String(timeStringBuff);
}

// --- CORREÇÃO AQUI ---
void sysProvEvent(arduino_event_t *sys_event)
{
    switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
#if CONFIG_IDF_TARGET_ESP32S2
        Serial.printf("\nSoftAP Provisioning\n");
        printQR(service_name, pop, "softap");
#else
        Serial.printf("\nBLE Provisioning\n");
        printQR(service_name, pop, "ble");
#endif
        break;
        
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        // *** MUDANÇA CRÍTICA ***
        // REMOVEMOS O STOP AQUI.
        // Deixamos o Bluetooth ligado para o celular receber a confirmação de sucesso.
        Serial.println("Credenciais recebidas! Conectando...");
        break;

    case ARDUINO_EVENT_PROV_END:
        // Este evento acontece sozinho quando o App RainMaker termina o trabalho.
        Serial.println("\n--- FIM DO PROVISIONAMENTO ---");
        sessao_provisionamento_encerrada = true;
        break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.println("IP Obtido! Conectado ao Wi-Fi.");
        break;
        
    default:;
    }
}

bool verificarPodeDisparar() {
    if (!dnd_ativo) return true; 
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return true; 

    int minutos_agora = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
    int minutos_inicio = dnd_inicio * 60; 
    int minutos_fim = dnd_fim * 60;       

    bool dentro = false;
    if (minutos_inicio < minutos_fim) {
        if (minutos_agora >= minutos_inicio && minutos_agora < minutos_fim) dentro = true;
    } else {
        if (minutos_agora >= minutos_inicio || minutos_agora < minutos_fim) dentro = true;
    }

    if (dentro) {
        if (sessao_provisionamento_encerrada) { 
            atualizarTela("BLOQUEADO", "Modo DND Ativo");
            delay(2000);
            atualizarTela("ONLINE", getHoraAtual());
        }
        return false; 
    }
    return true; 
}

void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx)
{
    const char *param_name = param->getParamName();

    if (strcmp(param_name, "Power") == 0) {
        if (val.val.b == true) {

            if (!verificarPodeDisparar()) {
                param->updateAndReport(value(false));
                return;
            }
            
            if (millis() < momento_liberacao) {
                if(sessao_provisionamento_encerrada) {
                    atualizarTela("COOLDOWN", "Aguarde...");
                    delay(1000);
                    atualizarTela("ONLINE", getHoraAtual());
                }
                param->updateAndReport(value(false)); 
                return; 
            }

            digitalWrite(gpio_switch, HIGH);
            
            if(sessao_provisionamento_encerrada) atualizarTela("ATUANDO", "Spray...");

            for (int i = 1; i <= qtd_disparos; i++) {
                if(sessao_provisionamento_encerrada) {
                     String status = "SPRAY " + String(i) + "/" + String(qtd_disparos);
                     atualizarTela(status, "Em progresso...");
                }

                meuServo.attach(PINO_SERVO);
                meuServo.write(angulo_aperto); 
                delay(TEMPO_APERTO); 

                meuServo.write(ANGULO_REPOUSO);
                
                if (i < qtd_disparos) {
                    delay(1500); 
                    meuServo.detach(); 
                } else {
                    delay(600);
                    meuServo.detach(); 
                }
            }

            if (my_switch) {
                 struct tm timeinfo;
                 if(getLocalTime(&timeinfo)){
                     char timeStringBuff[50];
                     strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m as %H:%M", &timeinfo);
                     my_switch->updateAndReportParam("Ultimo Disparo", timeStringBuff);
                 }
            }

            digitalWrite(gpio_switch, LOW);
            param->updateAndReport(value(false));
            momento_liberacao = millis() + TEMPO_COOLDOWN;
            
            if(sessao_provisionamento_encerrada) atualizarTela("ONLINE", getHoraAtual());

        } else {
            digitalWrite(gpio_switch, LOW);
            param->updateAndReport(val);
        }
    }
    if (strcmp(param_name, "Angulo") == 0) {
        angulo_aperto = val.val.i; 
        param->updateAndReport(val); 
    }
    if (strcmp(param_name, "Qtd Sprays") == 0) {
        qtd_disparos = val.val.i;
        param->updateAndReport(val);
    }
    if (strcmp(param_name, "Modo Noturno") == 0) {
        dnd_ativo = val.val.b;
        param->updateAndReport(val);
    }
    if (strcmp(param_name, "DND Inicio") == 0) {
        dnd_inicio = val.val.f; 
        param->updateAndReport(val);
    }
    if (strcmp(param_name, "DND Fim") == 0) {
        dnd_fim = val.val.f;    
        param->updateAndReport(val);
    }
}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);

    // DISPLAY INICIAL - BLOQUEADO
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("Erro OLED"));
    }
    mostrarTelaPareamento(); 

    pinMode(gpio_0, INPUT);
    pinMode(gpio_switch, OUTPUT);
    digitalWrite(gpio_switch, DEFAULT_POWER_MODE);

    ESP32PWM::allocateTimer(0);
    meuServo.setPeriodHertz(50);
    meuServo.attach(PINO_SERVO);
    meuServo.write(ANGULO_REPOUSO); 
    delay(500);
    meuServo.detach();

    Node my_node;
    my_node = RMaker.initNode("ESP RainMaker Node");

    my_switch = new Switch("Switch", &gpio_switch);
    if (!my_switch) return;
    
    my_switch->addParam(Param("Ultimo Disparo", "esp.param.text", value("---"), PROP_FLAG_READ));

    Param calibra("Angulo", ESP_RMAKER_PARAM_RANGE, value(angulo_aperto), PROP_FLAG_READ | PROP_FLAG_WRITE);
    calibra.addBounds(value(0), value(180), value(1)); 
    calibra.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(calibra);

    Param qtd("Qtd Sprays", ESP_RMAKER_PARAM_RANGE, value(qtd_disparos), PROP_FLAG_READ | PROP_FLAG_WRITE);
    qtd.addBounds(value(1), value(5), value(1)); 
    qtd.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(qtd);

    Param dnd_sw("Modo Noturno", ESP_RMAKER_PARAM_POWER, value(dnd_ativo), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_sw.addUIType(ESP_RMAKER_UI_TOGGLE);
    my_switch->addParam(dnd_sw);

    Param dnd_ini("DND Inicio", ESP_RMAKER_PARAM_RANGE, value(dnd_inicio), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_ini.addBounds(value(0.0f), value(24.0f), value(0.5f)); 
    dnd_ini.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(dnd_ini);

    Param dnd_end("DND Fim", ESP_RMAKER_PARAM_RANGE, value(dnd_fim), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_end.addBounds(value(0.0f), value(24.0f), value(0.5f)); 
    dnd_end.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(dnd_end);

    my_switch->addCb(write_callback);
    my_node.addDevice(*my_switch);

    RMaker.enableOTA(OTA_USING_TOPICS);
    RMaker.enableTZService();
    RMaker.enableSchedule();
    RMaker.enableScenes();
    initAppInsights();
    RMaker.enableSystemService(SYSTEM_SERV_FLAGS_ALL, 2, 2, 2);

    RMaker.start();

    // Reset estado
    my_switch->updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, false);

    WiFi.onEvent(sysProvEvent);
#if CONFIG_IDF_TARGET_ESP32S2
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SCHEME_HANDLER_NONE, WIFI_PROV_SECURITY_1, pop, service_name);
#else
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, pop, service_name);
#endif
}

void loop()
{
    static unsigned long ultimoUpdate = 0;
    
    // SÓ ATUALIZA TELA SE O APP JÁ TERMINOU TUDO E ESTAMOS CONECTADOS
    if (sessao_provisionamento_encerrada && WiFi.status() == WL_CONNECTED) {
        if (millis() - ultimoUpdate > 60000) { 
            atualizarTela("ONLINE", getHoraAtual());
            ultimoUpdate = millis();
        }
    }

    if (digitalRead(gpio_0) == LOW) {
        delay(100);
        int startTime = millis();
        while (digitalRead(gpio_0) == LOW) delay(50);
        int endTime = millis();

        if ((endTime - startTime) > 10000) {
            RMakerFactoryReset(2);
        } else if ((endTime - startTime) > 3000) {
            RMakerWiFiReset(2);
        } else {
            if (!verificarPodeDisparar()) return;

            if (millis() < momento_liberacao) return;

            digitalWrite(gpio_switch, HIGH);
            if (my_switch) my_switch->updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, true);
            
            if(sessao_provisionamento_encerrada) atualizarTela("ATUANDO", "Spray...");

            for (int i = 1; i <= qtd_disparos; i++) {
                if(sessao_provisionamento_encerrada) {
                    String status = "SPRAY " + String(i) + "/" + String(qtd_disparos);
                    atualizarTela(status, "Manual");
                }
                
                meuServo.attach(PINO_SERVO);
                meuServo.write(angulo_aperto); 
                delay(TEMPO_APERTO); 

                meuServo.write(ANGULO_REPOUSO);
                if (i < qtd_disparos) {
                    delay(1500); meuServo.detach(); 
                } else {
                    delay(600); meuServo.detach(); 
                }
            }

            if(sessao_provisionamento_encerrada) {
                String hora = getHoraAtual();
                if (my_switch) my_switch->updateAndReportParam("Ultimo Disparo", hora.c_str());
                atualizarTela("ONLINE", hora);
            }

            digitalWrite(gpio_switch, LOW);
            if (my_switch) my_switch->updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, false);
            momento_liberacao = millis() + TEMPO_COOLDOWN;
        }
    }
    delay(50);
}
