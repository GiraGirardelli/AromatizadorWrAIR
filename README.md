# Aromatizador Smart IoT (V1.0)

Um projeto open-source de um aromatizador automático inteligente, desenvolvido com foco em robustez mecânica, segurança de hardware e integração IoT em nuvem. 

O objetivo deste projeto foi solucionar as principais falhas de aromatizadores convencionais (refis proprietários caros e mecânicas frágeis) aplicando princípios de Design Mecatrônico e desenvolvendo uma infraestrutura de controle remoto completa.

## 💡 Arquitetura do Sistema

A abordagem escolhida para este projeto foi **100% Mecânica (Solid-State Control)**. Em vez de utilizar bombas hidráulicas ou diafragmas — que adicionam pontos de falha por vazamento e exigem manutenção de tubulação —, o sistema atua fisicamente sobre um frasco spray padrão de mercado (60ml/100ml) através de um mecanismo de came-seguidor.

### O Software e a Nuvem (ESP RainMaker)
O firmware foi desenvolvido utilizando o ecossistema ESP RainMaker, garantindo comunicação segura, provisionamento via Bluetooth e controle em tempo real pelo smartphone. As principais funcionalidades embarcadas incluem:
* **Controle Remoto:** Acionamento manual do mecanismo de qualquer lugar via Wi-Fi.
* **Agendamento (Scheduling):** Configuração de rotinas e timers pelo aplicativo, evitando o desperdício de essência em horários sem fluxo de pessoas.
* **Modo Noturno Automático:** Intervalo de tempo configurável onde o display OLED é desligado e qualquer acionamento mecânico é bloqueado pelo sistema.

## 🛠️ Hardware e Eletrônica

A eletrônica foi estruturada em uma perfboard com foco em segurança e modularidade, utilizando conectores JST para facilitar a manutenção.

* **Microcontrolador:** ESP32 (Responsável pelo gerenciamento do FreeRTOS, Wi-Fi e controle dos periféricos).
* **Atuador:** Servo Motor MG996R (Metal Gear). Escolhido pelo torque elevado (aprox. 10kg.cm), capaz de vencer a resistência da mola da válvula do spray com ampla margem de segurança.
* **Interface (IHM):** Display OLED I2C para feedback visual de status e conexão.
* **Proteção Elétrica:** Implementação de um capacitor eletrolítico de 1000uF na linha de 5V para suprir picos de corrente do servo motor e evitar *brownouts* (resets indesejados) no ESP32.

## ⚙️ Design Mecânico e Segurança

Toda a estrutura física, suportes e o mecanismo do came foram modelados em CAD e fabricados via impressão 3D. 
* **Design for Maintenance:** A tampa de acesso ao refil utiliza um sistema de fechamento magnético, permitindo a troca rápida do frasco.
* **Isolamento de Risco:** A eletrônica e o motor ficam fisicamente isolados em um compartimento superior, separados da área de manuseio e acima da linha do líquido, mitigando qualquer risco de curto-circuito em caso de vazamento do frasco.
* **Ajuste Fino:** O suporte do motor possui furos oblongos, permitindo a calibração da distância de ataque do came, evitando o *stall* (travamento) do motor contra o final de curso da válvula.

## 🚀 Roadmap e Próximos Passos (V2.0)

Com a V1.0 validada e em operação, o desenvolvimento atual está focado na transição do protótipo para um produto comercial escalável (V2.0). Os objetivos arquiteturais são:

1. **Custom PCB:** Substituir a perfboard e o ESP32 clássico por uma placa de circuito impresso dedicada, utilizando o módulo **ESP32-C3** (arquitetura RISC-V) para redução drástica de custos e tamanho.
3. **Sensores Inteligentes:** Integração de um sensor de presença (PIR) para acionamento condicionado à ocupação do ambiente. Além de um sistema para reconhecer a necessidade de troca da essência.

## 🖨️ Guia de Impressão 3D e Montagem

Todos os arquivos `.stl` necessários para a montagem da V1.0 estão na pasta `/3D_Files`. A maioria das peças foi otimizada para ser impressa sem suportes, mas requer atenção a alguns parâmetros específicos de fatiamento.

### Parâmetros Gerais Recomendados
* **Material:** PLA ou PETG (PETG recomendado para maior resistência térmica próxima ao motor).
* **Altura da Camada:** 0.20 mm.
* **Infill (Preenchimento):** 15% a 20% (Gyroid).
* **Orientação:** Respeitar a orientação original dos arquivos STL para garantir a resistência mecânica das camadas.

### ⚠️ Peças Críticas e Pausas de Impressão (M125)

**1. O Came (came_motor.stl)**
* Esta peça sofre alto estresse mecânico. É mandatório fatiar com **100% de Infill** (ou aumentar o número de paredes/perímetros para 6+) para garantir que o eixo do servo não espane o plástico.

**2. Base e Tampa Magnética (base_case.stl / tampa_case.stl)**
* Para embutir os ímãs de neodímio (tamanho 4mm x 2mm) de forma invisível, você deve configurar uma **pausa na impressão** no seu fatiador.
* **Carcaça princial:** Inserir pausa na camada **240** (ou na altura X.X mm). Coloque os ímãs, certifique-se de que estão rentes ao plástico e retome a impressão.
* **Pausa nas Tampas:** Inserir pausa na camada **15**. **Atenção à polaridade** dos ímãs antes de inserir, para garantir que a tampa seja atraída e não repelida pela base!
* * **Dica:**  Recomendo o uso de um pouco de cola instatânea (cianoacrilato) nos furos para os imãs para não ter problemas durante o resto da impressão

---
* Sinta-se livre para analisar o código, utilizar os arquivos STL e propor melhorias para as próximas versões.*
