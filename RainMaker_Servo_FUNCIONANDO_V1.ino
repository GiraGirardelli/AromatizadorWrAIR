#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include "AppInsights.h"
#include <ESP32Servo.h>
#include "time.h"


#define DEFAULT_POWER_MODE false // Começa desligado
const char *service_name = "PROV_aromatizador";
const char *pop = "abcd1234";

// --- CONFIGURAÇÃO MECÂNICA ---
#define PINO_SERVO 18
#define ANGULO_REPOUSO 0   // Posição de descanso
int angulo_aperto = 95; // Variável global (Começa em 170, mas pode mudar)
#define TEMPO_APERTO   1000 // Tempo segurando o spray apertado (ms)
int qtd_disparos = 1; // Padrão é 1 disparo
// --- CONTROLE ANTI-SPAM ---
unsigned long momento_liberacao = 0; // Marca a hora que pode usar de novo
#define TEMPO_COOLDOWN 1000 // 5000ms = 5 Segundos de descanso obrigatório
// --- VARIÁVEIS DO MODO NÃO PERTURBE ---
bool dnd_ativo = false;    
float dnd_inicio = 22.0;   // 22:00
float dnd_fim = 7.0;       // 07:00

Servo meuServo;

// GPIO for push button (BOOT)
#if CONFIG_IDF_TARGET_ESP32C3
static int gpio_0 = 9;
static int gpio_switch = 7;
#else
static int gpio_0 = 0;
static int gpio_switch = 2;
#endif

// Estado interno
bool switch_state = false;
static Switch *my_switch = NULL;

// Função que pega a hora do sistema e formata bonitinho
String getHoraAtual() {
    struct tm timeinfo;
    // Tenta pegar a hora (o RainMaker sincroniza sozinho via internet)
    if(!getLocalTime(&timeinfo)){
        return "Sincronizando...";
    }
    char timeStringBuff[50];
    // Formata como: "01/02 as 19:30"
    strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m as %H:%M", &timeinfo);
    return String(timeStringBuff);
}

void sysProvEvent(arduino_event_t *sys_event)
{
    switch (sys_event->event_id) {
    case ARDUINO_EVENT_PROV_START:
#if CONFIG_IDF_TARGET_ESP32S2
        Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on SoftAP\n", service_name, pop);
        printQR(service_name, pop, "softap");
#else
        Serial.printf("\nProvisioning Started with name \"%s\" and PoP \"%s\" on BLE\n", service_name, pop);
        printQR(service_name, pop, "ble");
#endif
        break;
    case ARDUINO_EVENT_PROV_INIT:
        wifi_prov_mgr_disable_auto_stop(10000);
        break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        wifi_prov_mgr_stop_provisioning();
        break;
    default:;
    }
}

// Função que verifica se está na hora de silêncio
bool verificarPodeDisparar() {
    if (!dnd_ativo) return true; // Se o modo estiver desligado, libera geral!

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        Serial.println("Erro: Relógio não sincronizado. Liberando disparo.");
        return true; // Se não sabe a hora, melhor funcionar do que travar
    }

    // Converte tudo para minutos (Ex: 22:30 = 1350 minutos)
    int minutos_agora = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
    int minutos_inicio = dnd_inicio * 60; 
    int minutos_fim = dnd_fim * 60;       

    bool dentro_do_bloqueio = false;

    // Lógica da virada de noite
    if (minutos_inicio < minutos_fim) {
        // Bloqueio no mesmo dia (Ex: 13:00 as 15:00)
        if (minutos_agora >= minutos_inicio && minutos_agora < minutos_fim) dentro_do_bloqueio = true;
    } else {
        // Bloqueio vira a noite (Ex: 22:00 as 07:00)
        if (minutos_agora >= minutos_inicio || minutos_agora < minutos_fim) dentro_do_bloqueio = true;
    }

    if (dentro_do_bloqueio) {
        Serial.println(">>> BLOQUEADO: MODO NAO PERTURBE ATIVO <<<");
        return false; // NÃO PODE DISPARAR
    }
    
    return true; // PODE DISPARAR
}

// --- AQUI ESTÁ A MUDANÇA PARA O "GATILHO" ---
void write_callback(Device *device, Param *param, const param_val_t val,
                    void *priv_data, write_ctx_t *ctx)
{
    const char *device_name = device->getDeviceName();
    const char *param_name = param->getParamName();

    if (strcmp(param_name, "Power") == 0) {
        Serial.printf("Comando recebido: %s\n", val.val.b ? "ATIVAR" : "PARAR");

        // Só executamos a ação se o comando for "LIGAR" (true)
        if (val.val.b == true) {

          // --- NOVO: CHECAGEM DND ---
            if (!verificarPodeDisparar()) {
                // Se estiver no horário proibido, desliga o botão e sai
                param->updateAndReport(value(false));
                return;
            }
            // --------------------------

            // --- TRAVA ANTI-SPAM ---
            // Se ainda não passou o tempo de descanso...
            if (millis() < momento_liberacao) {
                Serial.println("BLOCKED: Sistema em resfriamento!");
                // Força o botão do App a desligar na hora
                param->updateAndReport(value(false)); 
                return; // Sai da função e não faz nada!
            }
            // -----------------------

            // 1. Liga o LED para indicar funcionamento
            digitalWrite(gpio_switch, HIGH);

            // 2. Movimento de "Ataque" (Aperta o Spray)
            // --- BLOCO DE MULTI DISPAROS ---
            for (int i = 1; i <= qtd_disparos; i++) {
                Serial.printf(">>> Spray %d de %d\n", i, qtd_disparos);
                
                meuServo.attach(PINO_SERVO);
                meuServo.write(angulo_aperto); 
                delay(TEMPO_APERTO); 

                meuServo.write(ANGULO_REPOUSO);
                
                // Se NÃO for o último, espera um pouco e desliga o motor pra esfriar
                if (i < qtd_disparos) {
                    delay(1500); 
                    meuServo.detach(); 
                } else {
                    // Se for o ÚLTIMO, delay final curto
                    delay(600);
                    meuServo.detach(); 
                }
            }

            String hora = getHoraAtual();
            my_switch->updateAndReportParam("Ultimo Disparo", hora.c_str());

            // 4. Desliga o LED
            digitalWrite(gpio_switch, LOW);

            // 5. O TRUQUE: Manda o botão do App voltar para "Desligado" sozinho
            param->updateAndReport(value(false));
            // Só libera o próximo disparo daqui a 5 segundos
            momento_liberacao = millis() + TEMPO_COOLDOWN;
            // ------------------------

        } else {
            // Se alguém mandar "false", só garante que tá tudo desligado
            digitalWrite(gpio_switch, LOW);
            param->updateAndReport(val);
        }
    }
    if (strcmp(param_name, "Angulo") == 0) {
        angulo_aperto = val.val.i; // Atualiza a variável global
        Serial.printf("Novo Angulo Calibrado: %d\n", angulo_aperto);
        param->updateAndReport(val); // Confirma pro celular que aceitou
    }
    if (strcmp(param_name, "Qtd Sprays") == 0) {
        qtd_disparos = val.val.i;
        Serial.printf("Multi Disparos definido para: %d\n", qtd_disparos);
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
    Serial.begin(115200);
    pinMode(gpio_0, INPUT);
    pinMode(gpio_switch, OUTPUT);
    digitalWrite(gpio_switch, DEFAULT_POWER_MODE);

    // Configuração Inicial do Servo
    ESP32PWM::allocateTimer(0);
    meuServo.setPeriodHertz(50);
    meuServo.attach(PINO_SERVO);
    meuServo.write(ANGULO_REPOUSO); // Garante que inicia em 0
    delay(500);
    meuServo.detach();

    Node my_node;
    my_node = RMaker.initNode("ESP RainMaker Node");

    my_switch = new Switch("Switch", &gpio_switch);
    if (!my_switch) {
        return;
    }
    my_switch->addParam(Param("Ultimo Disparo", "esp.param.text", value("---"), PROP_FLAG_READ));

    Param calibra("Angulo", ESP_RMAKER_PARAM_RANGE, value(angulo_aperto), PROP_FLAG_READ | PROP_FLAG_WRITE);
    calibra.addBounds(value(0), value(180), value(1)); // Define min 0, max 180, passo 1
    calibra.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(calibra);

    Param qtd("Qtd Sprays", ESP_RMAKER_PARAM_RANGE, value(qtd_disparos), PROP_FLAG_READ | PROP_FLAG_WRITE);
    qtd.addBounds(value(1), value(5), value(1)); // Mínimo 1, Máximo 5 sprays
    qtd.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(qtd);

    // --- CONTROLES DND ---
    // 1. Botão de Ligar/Desligar o Modo
    Param dnd_sw("Modo Noturno", ESP_RMAKER_PARAM_POWER, value(dnd_ativo), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_sw.addUIType(ESP_RMAKER_UI_TOGGLE);
    my_switch->addParam(dnd_sw);

    // 2. Slider Hora Inicio (0 a 24)
    Param dnd_ini("DND Inicio", ESP_RMAKER_PARAM_RANGE, value(dnd_inicio), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_ini.addBounds(value(0.0f), value(24.0f), value(0.5f)); // Passo de 30 min
    dnd_ini.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(dnd_ini);

    // 3. Slider Hora Fim (0 a 24)
    Param dnd_end("DND Fim", ESP_RMAKER_PARAM_RANGE, value(dnd_fim), PROP_FLAG_READ | PROP_FLAG_WRITE);
    dnd_end.addBounds(value(0.0f), value(24.0f), value(0.5f)); 
    dnd_end.addUIType(ESP_RMAKER_UI_SLIDER);
    my_switch->addParam(dnd_end);
    // --------------------

    my_switch->addCb(write_callback);
    my_node.addDevice(*my_switch);

    // Mantendo todas as funções vitais do RainMaker
    RMaker.enableOTA(OTA_USING_TOPICS);
    RMaker.enableTZService();
    RMaker.enableSchedule();
    RMaker.enableScenes();
    initAppInsights();
    RMaker.enableSystemService(SYSTEM_SERV_FLAGS_ALL, 2, 2, 2);

    RMaker.start();

    WiFi.onEvent(sysProvEvent);
#if CONFIG_IDF_TARGET_ESP32S2
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SCHEME_HANDLER_NONE, WIFI_PROV_SECURITY_1, pop, service_name);
#else
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, pop, service_name);
#endif
}

void loop()
{
    // Lógica do Botão Físico (BOOT) também virou Gatilho Automático
    if (digitalRead(gpio_0) == LOW) {
        delay(100);
        int startTime = millis();
        while (digitalRead(gpio_0) == LOW) delay(50);
        int endTime = millis();

        if ((endTime - startTime) > 10000) {
            Serial.printf("Reset to factory.\n");
            RMakerFactoryReset(2);
        } else if ((endTime - startTime) > 3000) {
            Serial.printf("Reset Wi-Fi.\n");
            RMakerWiFiReset(2);
        } else {

            // Clique Rápido no botão físico -> Dispara o ciclo completo
            Serial.println("Botão Físico: Ciclo de Disparo!");

            // --- NOVO: CHECAGEM DND ---
            if (!verificarPodeDisparar()) {
                // Pisca o LED rapidinho só pra dizer que ouviu, mas não dispara
                digitalWrite(gpio_switch, HIGH); delay(200); digitalWrite(gpio_switch, LOW);
                return; 
            }
            // --------------------------

            // --- TRAVA ANTI-SPAM ---
            if (millis() < momento_liberacao) {
                Serial.println("BLOCKED: Botão físico ignorado (Resfriando)");
                return; // Ignora o clique
            }
            // -----------------------

            digitalWrite(gpio_switch, HIGH);

            // Atualiza o App dizendo que ativou
            if (my_switch) my_switch->updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, true);

            for (int i = 1; i <= qtd_disparos; i++) {
                Serial.printf(">>> Botao Fisico: Spray %d de %d\n", i, qtd_disparos);
                
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

            String hora = getHoraAtual();
            if (my_switch) my_switch->updateAndReportParam("Ultimo Disparo", hora.c_str());

            digitalWrite(gpio_switch, LOW);

            // Atualiza o App dizendo que terminou
            if (my_switch) my_switch->updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, false);
            // --- ATUALIZA O TEMPO ---
            momento_liberacao = millis() + TEMPO_COOLDOWN;
        }
    }
    delay(100);
}
