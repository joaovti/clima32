# Clima32 — Estação Meteorológica Inteligente com ESP32 & Python

> **Clima32** é uma estação meteorológica completa, moderna e autônoma desenvolvida com microcontrolador **ESP32** e backend em **Python**. 

O sistema coleta dados de temperatura e umidade em tempo real, consulta a previsão do tempo externa local via geocodificação de CEP, armazena histórico contínuo em disco, gera gráficos interativos e oferece integração por voz com a **Amazon Alexa** e notificações no celular via **NTFY**.

---

## Recursos Principais

- **Painel Web Moderno em Abas (Nativo no ESP32)**:
  - **Aba 1 (Painel Principal)**: Temperatura e umidade internas (DHT11/DHT22), temperatura externa, sensação térmica, banner inteligente comparativo de conforto térmico, médias consolidadas (hoje, semana e mês) e recap do dia de maior oscilação térmica.
  - **Aba 2 (Histórico 24h & Calendário)**: Seletor de data por calendário no padrão brasileiro (`DD/MM/AAAA`), cards de estatísticas diárias, matriz hora a hora (00:00 às 23:00) e **gráfico interativo SVG de curva dupla** comparando a temperatura interna (azul) e externa (laranja).
  - **Aba 3 (Alexa & Notificações)**: Envio manual de frases personalizadas e resumos climáticos para caixas Echo Dot via Voice Monkey e testes de alerta push.
  - **Aba 4 (Configurações & Diagnóstico)**: Seção de **telemetria em tempo real do ESP32** (temperatura interna do chip/CPU, memória RAM livre, sinal Wi-Fi e tempo de operação), além de ajuste de CEP, agendamento de avisos e personalização das frases da Alexa.
- **Arquitetura Leve & Histórico Sob Demanda**:
  - O ESP32 mantém em memória RAM os 7 dias mais recentes para navegação instantânea.
  - Ao selecionar dias mais antigos (semanas ou meses atrás), o ESP32 busca sob demanda apenas o JSON leve (~1 KB) daquele dia no servidor, **sem nunca estourar a memória RAM do ESP32**.
- **Avisos Contextuais Diários na Alexa**:
  - O ESP32 analisa automaticamente o clima no horário agendado e avisa na Alexa: *"Bom dia! Vai chover, não esqueça o guarda-chuva."*, *"Tempo seco, hidrate-se bem."*, etc.
- **Coleta Contínua em Servidor Local**:
  - Script Python que roda em segundo plano (no PC, Raspberry Pi ou servidor local), salvando leituras brutas em CSV e calculando relatórios consolidados diários, semanais e mensais.

---

## Estrutura do Projeto

```text
clima32/
├── esp32/
│   └── esp32.ino                   # Código-fonte C/C++ para o ESP32
├── servidor/
│   ├── coletor.py                  # Coletor de dados periódico e sincronizador
│   ├── servidor_dashboard.py       # Servidor web local com histórico completo
│   ├── config.example.json         # Modelo de configuração do servidor
│   └── requirements.txt            # Dependências Python
├── .gitignore
├── LICENSE                         # Licença MIT
└── README.md
```

---

## Requisitos de Hardware

1. **Placa ESP32** (ex: ESP32 DevKit v1, NodeMCU-32S).
2. **Sensor de Temperatura e Umidade**: DHT11 ou DHT22.
3. **Cabo Micro-USB** para alimentação e gravação.
4. **Resistor de 10kΩ** (necessário apenas se o sensor DHT não for um módulo com resistor embutido).
5. **Jumpers** de conexão.

### Esquema de Ligação

| Sensor DHT11 / DHT22 | Pino no ESP32 |
| :--- | :--- |
| **VCC** (Alimentação) | **3V3** (ou 5V dependendo do módulo) |
| **DATA** (Sinal) | **GPIO 4** |
| **GND** (Terra) | **GND** |

---

## Instalação e Gravação no ESP32

### 1. Preparar o Arduino IDE
1. Baixe e instale o [Arduino IDE](https://www.arduino.cc/en/software) (versão 2.x recomendada).
2. Adicione o suporte às placas ESP32:
   - Vá em **Arquivo** > **Preferências**.
   - No campo *URLs do Gerenciador de Placas Adicionais*, insira:
     ```text
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Vá em **Ferramentas** > **Placa** > **Gerenciador de Placas**, pesquise por `esp32` e instale a versão oficial da Espressif.

### 2. Instalar as Bibliotecas Necessárias
No Arduino IDE, abra o **Gerenciador de Bibliotecas** (Ctrl + Shift + I) e instale:
- **DHT sensor library** (por Adafruit)
- **Adafruit Unified Sensor** (por Adafruit)
- **ArduinoJson** (versão 7.x, por Benoit Blanchon)

### 3. Configurar e Gravar
1. Abra o arquivo [`esp32/esp32.ino`](esp32/esp32.ino).
2. No início do arquivo, ajuste o nome e a senha da sua rede Wi-Fi:
   ```cpp
   const char* ssid = "SUA_REDE_WIFI";
   const char* password = "SUA_SENHA_WIFI";
   ```
3. *(Opcional)* Configure seu CEP inicial, tópico NTFY e tokens da Alexa (você também poderá alterar tudo isso depois pelo próprio painel web no navegador).
4. Conecte o ESP32 ao computador via USB.
5. Selecione a placa correspondente (ex: *ESP32 Dev Module*) e a porta COM/Serial.
6. Clique em **Carregar (Upload)**.
7. Ao concluir, abra o **Monitor Serial** (115200 baud). O ESP32 exibirá o endereço IP atribuído:
   ```text
   Wi-Fi Conectado com sucesso!
   Painel Web disponivel em: http://192.168.0.XXX
   ```

---

## Instalação e Uso da Parte Python (Servidor / PC)

O backend Python é responsável por coletar as leituras periodicamente, salvar arquivos CSV no disco e manter as médias históricas atualizadas.

### 1. Requisitos
- **Python 3.8** ou superior.
- Computador, Raspberry Pi ou servidor conectado na mesma rede local do ESP32.
- **Sistema Operacional**: Compatível com Linux, Windows e macOS (testado e validado em ambiente **Debian Linux**).

### 2. Instalação das Dependências
No terminal, entre na pasta `servidor` e instale os pacotes:

```bash
cd servidor
pip install -r requirements.txt
```

### 3. Configuração do `config.json`
Copie o arquivo de exemplo e configure o IP do seu ESP32:

```bash
cp config.example.json config.json
```

Edite o `config.json` com seu editor preferido:
```json
{
  "esp32_url": "http://192.168.0.XXX/api/dados",
  "intervalo_segundos": 300,
  "pasta_dados": "./dados"
}
```
> **Nota:** Substitua `192.168.0.XXX` pelo IP real exibido no Monitor Serial do ESP32.

### 4. Executando o Coletor
Para iniciar a coleta automática a cada 5 minutos (300 segundos):

```bash
python3 coletor.py
```

O script criará a pasta `dados/` e salvará:
- `leituras_ANO-MES.csv`: todas as leituras de minuto em minuto.
- `relatorio_diario.csv`, `relatorio_semanal.csv`, `relatorio_mensal.csv`: tabelas consolidadas.
- `resumo_recente.json`: médias globais e histórico que são sincronizados automaticamente no painel do ESP32.

### 5. Executando o Servidor Dashboard Local (Opcional)
Se você também quiser rodar o servidor de visualização no próprio PC:

```bash
python3 servidor_dashboard.py
```
Acesse no navegador: `http://localhost:8089`.

---

## Execução em Segundo Plano (Modo Contínuo)

Para manter o coletor rodando sem precisar deixar uma janela de terminal aberta:

### Opção A: Usando PM2 (Recomendado)
```bash
npm install -g pm2
pm2 start coletor.py --name "clima32-coletor" --interpreter python3
pm2 start servidor_dashboard.py --name "clima32-dashboard" --interpreter python3
pm2 save
pm2 startup
```

### Opção B: Usando Systemd (Linux)
Crie um serviço em `/etc/systemd/system/clima32.service`:
```ini
[Unit]
Description=Clima32 - Coletor de Dados ESP32
After=network.target

[Service]
Type=simple
User=SEU_USUARIO
WorkingDirectory=/caminho/para/clima32/servidor
ExecStart=/usr/bin/python3 /caminho/para/clima32/servidor/coletor.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```
Ative e inicie o serviço:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now clima32
```

---

## Integração com Notificações Push e Alexa

### 1. Notificações no Celular (NTFY — Grátis e Sem Cadastro)
1. Instale o aplicativo **ntfy** no seu smartphone (Android / iOS).
2. No app, crie ou inscreva-se em um tópico exclusivo (exemplo: `meu-clima-casa-12345`).
3. No painel web do ESP32 (Aba Configurações), informe o nome do tópico e clique em salvar.
4. Clique no botão de teste para receber uma notificação instantânea.

### 2. Avisos Falados na Alexa (Voice Monkey)
1. Crie uma conta gratuita no [Voice Monkey](https://voicemonkey.io/).
2. Conecte sua conta da Amazon e autorize a Skill do Voice Monkey no app Alexa.
3. No painel do Voice Monkey, crie um dispositivo virtual (ex: `echo-quarto`).
4. Copie seu **User Token** e o **Device ID** gerados.
5. No painel web do ESP32 (Aba Configurações), cole essas credenciais.
6. Pronto! O ESP32 enviará comandos de voz nativos diretamente para sua Alexa.

---

## Licença

Este projeto está sob a licença [MIT](LICENSE) — sinta-se livre para usar, estudar, modificar e distribuir.
